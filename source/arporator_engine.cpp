#include "arporator_engine.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace arporator {
namespace {

int clampInt(int v, int lo, int hi) noexcept {
    return v < lo ? lo : (v > hi ? hi : v);
}

float clamp01(float v) noexcept {
    if (!std::isfinite(v))
        return 0.0f;
    return std::clamp(v, 0.0f, 1.0f);
}

int positiveMod(int value, int mod) noexcept {
    if (mod <= 0)
        return 0;
    const int r = value % mod;
    return r < 0 ? r + mod : r;
}

} // namespace

Engine::Engine() {
    setSettings(Settings{});
    reset();
}

void Engine::setSettings(const Settings& settings) noexcept {
    settings_ = settings;
    settings_.patternLength = clampInt(settings_.patternLength, 1, kMaxSteps);
    settings_.octaveRange = clampInt(settings_.octaveRange, 1, 4);
    settings_.stepsPerQuarter =
        std::isfinite(settings_.stepsPerQuarter)
            ? std::clamp(settings_.stepsPerQuarter, 0.125, 32.0)
            : 4.0;
    settings_.swing = clamp01(settings_.swing);
    settings_.keyRoot = positiveMod(settings_.keyRoot, 12);
    for (auto& step : settings_.steps) {
        step.velocity = clamp01(step.velocity);
        step.gate = std::clamp(step.gate, 0.01f, 1.0f);
        step.ratchet = static_cast<std::uint8_t>(clampInt(step.ratchet, 1, 4));
        step.probability = clamp01(step.probability);
        step.octaveOffset =
            static_cast<std::int8_t>(clampInt(step.octaveOffset, -4, 4));
    }
    rng_ = settings_.randomSeed ? settings_.randomSeed : 0x125A0001u;
}

void Engine::reset() noexcept {
    clearHeld();
    for (auto& out : activeOutputs_)
        out = {};
    absoluteSample_ = 0.0;
    nextStepSample_ = 0.0;
    currentStep_ = 0;
    directionIndex_ = 0;
    directionSign_ = 1;
    running_ = false;
    orderCounter_ = 0;
    rng_ = settings_.randomSeed ? settings_.randomSeed : 0x125A0001u;
    nextNoteId_ = 1;
}

bool Engine::anyHeld() const noexcept {
    for (const auto& channel : held_)
        for (const auto& note : channel)
            if (note.count > 0)
                return true;
    return false;
}

void Engine::clearHeld() noexcept {
    for (auto& channel : held_)
        for (auto& note : channel)
            note = {};
}

std::uint32_t Engine::nextRandom() noexcept {
    std::uint32_t x = rng_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_ = x ? x : 0x125A0001u;
    return rng_;
}

bool Engine::stepPassesProbability(int step) noexcept {
    const auto& s = settings_.steps[static_cast<std::size_t>(step)];
    if (s.probability >= 1.0f)
        return true;
    if (s.probability <= 0.0f)
        return false;
    const float unit =
        static_cast<float>(nextRandom() & 0x00FFFFFFu) /
        static_cast<float>(0x01000000u);
    return unit < s.probability;
}

std::vector<int> Engine::orderedPitches(int channel) const {
    std::vector<int> result;
    channel = clampInt(channel, 0, 15);
    result.reserve(16);
    for (int pitch = 0; pitch < kMidiNotes; ++pitch) {
        if (held_[static_cast<std::size_t>(channel)][static_cast<std::size_t>(pitch)].count > 0)
            result.push_back(pitch);
    }
    return result;
}

int Engine::choosePitch(int channel, int logicalStep) noexcept {
    auto pitches = orderedPitches(channel);
    if (pitches.empty())
        return -1;

    const int count = static_cast<int>(pitches.size());
    const int octaveRange = settings_.octaveRange;
    const int expandedCount = count * octaveRange;

    auto pitchAtExpanded = [&](int index) {
        index = positiveMod(index, expandedCount);
        const int octave = index / count;
        const int baseIndex = index % count;
        return pitches[static_cast<std::size_t>(baseIndex)] + 12 * octave;
    };

    switch (settings_.mode) {
        case Mode::Up:
            return pitchAtExpanded(logicalStep);

        case Mode::Down: {
            const int index = expandedCount - 1 - positiveMod(logicalStep, expandedCount);
            return pitchAtExpanded(index);
        }

        case Mode::UpDown: {
            if (expandedCount <= 1)
                return pitchAtExpanded(0);
            const int cycle = expandedCount * 2 - 2;
            const int p = positiveMod(logicalStep, cycle);
            const int index = p < expandedCount ? p : cycle - p;
            return pitchAtExpanded(index);
        }

        case Mode::DownUp: {
            if (expandedCount <= 1)
                return pitchAtExpanded(0);
            const int cycle = expandedCount * 2 - 2;
            const int p = positiveMod(logicalStep, cycle);
            const int index = p < expandedCount
                ? expandedCount - 1 - p
                : p - expandedCount + 1;
            return pitchAtExpanded(index);
        }

        case Mode::Played: {
            struct Played {
                int pitch;
                std::uint32_t order;
            };
            std::vector<Played> played;
            played.reserve(pitches.size());
            for (int p : pitches) {
                const auto& n =
                    held_[static_cast<std::size_t>(channel)][static_cast<std::size_t>(p)];
                played.push_back({p, n.order});
            }
            std::stable_sort(played.begin(), played.end(),
                             [](const Played& a, const Played& b) {
                                 return a.order < b.order;
                             });
            const int expanded = static_cast<int>(played.size()) * octaveRange;
            const int idx = positiveMod(logicalStep, expanded);
            return played[static_cast<std::size_t>(idx % played.size())].pitch +
                   12 * (idx / static_cast<int>(played.size()));
        }

        case Mode::Random: {
            const int index =
                static_cast<int>(nextRandom() % static_cast<std::uint32_t>(expandedCount));
            return pitchAtExpanded(index);
        }
    }

    return pitches.front();
}

int Engine::constrainPitch(int pitch, const std::vector<int>& chord) const noexcept {
    pitch = clampInt(pitch, 0, 127);

    if (settings_.scalePolicy == ScalePolicy::Chromatic)
        return pitch;

    if (settings_.scalePolicy == ScalePolicy::ChordOnly && !chord.empty()) {
        int best = chord.front();
        int bestDistance = std::numeric_limits<int>::max();
        for (int base : chord) {
            for (int octave = -8; octave <= 8; ++octave) {
                const int candidate = base + octave * 12;
                if (candidate < 0 || candidate > 127)
                    continue;
                const int distance = std::abs(candidate - pitch);
                if (distance < bestDistance ||
                    (distance == bestDistance && candidate < best)) {
                    best = candidate;
                    bestDistance = distance;
                }
            }
        }
        return best;
    }

    int best = pitch;
    int bestDistance = std::numeric_limits<int>::max();
    for (int candidate = 0; candidate < 128; ++candidate) {
        const int pc = positiveMod(candidate - settings_.keyRoot, 12);
        if ((settings_.scaleMask & (1u << pc)) == 0)
            continue;
        const int distance = std::abs(candidate - pitch);
        if (distance < bestDistance ||
            (distance == bestDistance && candidate < best)) {
            best = candidate;
            bestDistance = distance;
        }
    }
    return best;
}

void Engine::stopAll(double when,
                     double blockStart,
                     int blockSize,
                     std::vector<MidiOutput>& output) noexcept {
    const int offset = clampInt(
        static_cast<int>(std::floor(when - blockStart + 1.0e-9)),
        0, std::max(0, blockSize - 1));

    for (auto& active : activeOutputs_) {
        if (!active.active)
            continue;
        output.push_back({
            MidiInput::Type::NoteOff,
            offset,
            active.channel,
            active.pitch,
            0.0f,
            active.noteId
        });
        active = {};
    }
}

void Engine::stopExpired(double blockStart,
                         double blockEnd,
                         int blockSize,
                         std::vector<MidiOutput>& output) noexcept {
    for (auto& active : activeOutputs_) {
        if (!active.active || active.offSample >= blockEnd)
            continue;

        const double when = std::max(active.offSample, blockStart);
        const int offset = clampInt(
            static_cast<int>(std::floor(when - blockStart + 1.0e-9)),
            0, std::max(0, blockSize - 1));

        output.push_back({
            MidiInput::Type::NoteOff,
            offset,
            active.channel,
            active.pitch,
            0.0f,
            active.noteId
        });
        active = {};
    }
}

void Engine::emitStep(double stepSample,
                      double stepDuration,
                      double blockStart,
                      int blockSize,
                      std::vector<MidiOutput>& output) noexcept {
    const int stepIndex = clampInt(currentStep_, 0, settings_.patternLength - 1);
    const auto& step = settings_.steps[static_cast<std::size_t>(stepIndex)];

    if (!step.enabled || !stepPassesProbability(stepIndex))
        return;

    int channel = -1;
    for (int ch = 0; ch < 16 && channel < 0; ++ch) {
        for (const auto& n : held_[static_cast<std::size_t>(ch)]) {
            if (n.count > 0) {
                channel = ch;
                break;
            }
        }
    }
    if (channel < 0)
        return;

    const auto chord = orderedPitches(channel);
    if (chord.empty())
        return;

    int pitch = choosePitch(channel, directionIndex_);
    if (pitch < 0)
        return;

    pitch += static_cast<int>(step.octaveOffset) * 12;
    pitch = constrainPitch(pitch, chord);

    float sourceVelocity = 1.0f;
    const int basePc = positiveMod(pitch, 12);
    for (int p : chord) {
        if (positiveMod(p, 12) == basePc) {
            sourceVelocity = held_[static_cast<std::size_t>(channel)]
                                  [static_cast<std::size_t>(p)].velocity;
            break;
        }
    }

    const int ratchet = clampInt(step.ratchet, 1, 4);
    const double subDuration = stepDuration / static_cast<double>(ratchet);
    const double gateDuration =
        std::max(1.0, subDuration * static_cast<double>(step.gate));

    for (int hit = 0; hit < ratchet; ++hit) {
        const double onSample = stepSample + subDuration * static_cast<double>(hit);
        const int offset = clampInt(
            static_cast<int>(std::floor(onSample - blockStart + 1.0e-9)),
            0, std::max(0, blockSize - 1));

        const int noteId = nextNoteId_++;
        output.push_back({
            MidiInput::Type::NoteOn,
            offset,
            channel,
            pitch,
            clamp01(sourceVelocity * step.velocity),
            noteId
        });

        for (auto& active : activeOutputs_) {
            if (active.active)
                continue;
            active.active = true;
            active.channel = channel;
            active.pitch = pitch;
            active.noteId = noteId;
            active.offSample = onSample + gateDuration;
            break;
        }
    }
}

void Engine::process(double sampleRate,
                     double tempo,
                     int numSamples,
                     const std::vector<MidiInput>& input,
                     std::vector<MidiOutput>& output) noexcept {
    output.clear();
    if (!(sampleRate > 0.0) || !(tempo > 0.0) || numSamples <= 0)
        return;

    const double blockStart = absoluteSample_;
    const double blockEnd = blockStart + static_cast<double>(numSamples);
    const double samplesPerQuarter = sampleRate * 60.0 / tempo;
    const double stepDuration =
        samplesPerQuarter / std::max(0.125, settings_.stepsPerQuarter);

    std::vector<MidiInput> events = input;
    std::stable_sort(events.begin(), events.end(),
                     [numSamples](const MidiInput& a, const MidiInput& b) {
                         return clampInt(a.sampleOffset, 0, numSamples - 1) <
                                clampInt(b.sampleOffset, 0, numSamples - 1);
                     });

    auto emitUntil = [&](double limitSample) {
        stopExpired(blockStart, limitSample, numSamples, output);

        if (!running_ || !anyHeld())
            return;

        int safety = 0;
        while (nextStepSample_ < limitSample && safety++ < 512) {
            double scheduled = nextStepSample_;
            if ((currentStep_ & 1) != 0) {
                const double maxDelay = stepDuration * 0.49;
                scheduled += maxDelay * static_cast<double>(settings_.swing);
            }

            if (scheduled >= blockStart && scheduled < limitSample)
                emitStep(scheduled, stepDuration, blockStart, numSamples, output);

            currentStep_ = (currentStep_ + 1) % settings_.patternLength;
            ++directionIndex_;
            nextStepSample_ += stepDuration;
        }

        stopExpired(blockStart, limitSample, numSamples, output);
    };

    std::size_t i = 0;
    while (i < events.size()) {
        const int offset = clampInt(events[i].sampleOffset, 0, numSamples - 1);
        const double eventSample = blockStart + static_cast<double>(offset);

        // Render everything strictly before this timestamp using the old note set.
        emitUntil(eventSample);

        const bool hadHeldBeforeGroup = anyHeld();
        bool sawNoteOn = false;
        bool sawAllNotesOff = false;

        std::size_t j = i;
        for (; j < events.size(); ++j) {
            if (clampInt(events[j].sampleOffset, 0, numSamples - 1) != offset)
                break;

            const auto& event = events[j];
            const int channel = clampInt(event.channel, 0, 15);
            const int pitch = clampInt(event.pitch, 0, 127);

            if (event.type == MidiInput::Type::AllNotesOff) {
                clearHeld();
                sawAllNotesOff = true;
                continue;
            }

            if (event.type == MidiInput::Type::NoteOn && event.velocity > 0.0f) {
                sawNoteOn = true;
                auto& note = held_[static_cast<std::size_t>(channel)]
                                   [static_cast<std::size_t>(pitch)];
                if (note.count == 0)
                    note.order = ++orderCounter_;
                if (note.count < std::numeric_limits<std::uint16_t>::max())
                    ++note.count;
                note.velocity = clamp01(event.velocity);
                continue;
            }

            if (event.type == MidiInput::Type::NoteOff ||
                (event.type == MidiInput::Type::NoteOn && event.velocity <= 0.0f)) {
                auto& note = held_[static_cast<std::size_t>(channel)]
                                   [static_cast<std::size_t>(pitch)];
                if (note.count > 0)
                    --note.count;
            }
        }

        if (sawAllNotesOff) {
            stopAll(eventSample, blockStart, numSamples, output);
            running_ = false;
            currentStep_ = 0;
            directionIndex_ = 0;
        } else if (!anyHeld()) {
            stopAll(eventSample, blockStart, numSamples, output);
            running_ = false;
            currentStep_ = 0;
            directionIndex_ = 0;
        } else if (sawNoteOn &&
                   (!hadHeldBeforeGroup || settings_.restartOnTrigger)) {
            // All note-ons sharing this exact timestamp form one trigger.
            // Capture the whole chord first, then restart once at Step 1.
            stopAll(eventSample, blockStart, numSamples, output);
            running_ = true;
            currentStep_ = 0;
            directionIndex_ = 0;
            directionSign_ = 1;
            nextStepSample_ = eventSample;
            emitUntil(std::min(blockEnd, eventSample + 1.0));
        } else if (!running_ && anyHeld()) {
            running_ = true;
            nextStepSample_ = eventSample;
            emitUntil(std::min(blockEnd, eventSample + 1.0));
        }

        i = j;
    }

    emitUntil(blockEnd);

    std::stable_sort(output.begin(), output.end(),
                     [](const MidiOutput& a, const MidiOutput& b) {
                         if (a.sampleOffset != b.sampleOffset)
                             return a.sampleOffset < b.sampleOffset;
                         if (a.type == b.type)
                             return a.noteId < b.noteId;
                         return a.type == MidiInput::Type::NoteOff;
                     });

    absoluteSample_ = blockEnd;
}

} // namespace arporator
