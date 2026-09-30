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

std::uint32_t feelHash(std::uint32_t seed,
                       int step,
                       int pitch,
                       std::uint32_t salt) noexcept {
    std::uint32_t x = seed ^
        (static_cast<std::uint32_t>(step) * 0x9E3779B9u) ^
        (static_cast<std::uint32_t>(pitch + 1) * 0x85EBCA6Bu) ^
        salt;
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

float unitFromHash(std::uint32_t h) noexcept {
    return static_cast<float>(h & 0x00FFFFFFu) /
           static_cast<float>(0x01000000u);
}

float bipolarFromHash(std::uint32_t h) noexcept {
    return unitFromHash(h) * 2.0f - 1.0f;
}

} // namespace

Engine::Engine() {
    setSettings(Settings{});
    reset();
}

void Engine::setSettings(const Settings& settings) noexcept {
    const std::uint32_t previousSeed = settings_.randomSeed;
    settings_ = settings;
    settings_.patternLength = clampInt(settings_.patternLength, 1, kMaxSteps);
    settings_.octaveRange = clampInt(settings_.octaveRange, 1, 4);
    settings_.stepsPerQuarter =
        std::isfinite(settings_.stepsPerQuarter)
            ? std::clamp(settings_.stepsPerQuarter, 0.125, 32.0)
            : 4.0;
    settings_.swing = clamp01(settings_.swing);
    settings_.humanize = clamp01(settings_.humanize);
    settings_.groove = clamp01(settings_.groove);
    settings_.strum = clamp01(settings_.strum);
    settings_.evolve = clamp01(settings_.evolve);
    settings_.globalGate = std::clamp(settings_.globalGate, 0.01f, 1.0f);
    settings_.keyRoot = positiveMod(settings_.keyRoot, 12);
    for (auto& step : settings_.steps) {
        step.velocity = clamp01(step.velocity);
        step.gate = std::clamp(step.gate, 0.01f, 1.0f);
        step.ratchet = static_cast<std::uint8_t>(clampInt(step.ratchet, 1, 4));
        step.probability = clamp01(step.probability);
        step.noteOffset =
            static_cast<std::int8_t>(clampInt(step.noteOffset, -4, 4));
        step.octaveOffset =
            static_cast<std::int8_t>(clampInt(step.octaveOffset, -4, 4));
    }
    if (settings_.randomSeed != previousSeed)
        rng_ = settings_.randomSeed ? settings_.randomSeed : 0x125A0001u;
}

void Engine::reset() noexcept {
    clearHeld();
    for (auto& out : activeOutputs_)
        out = {};
    absoluteSample_ = 0.0;
    nextStepSample_ = 0.0;
    lastStepDuration_ = 0.0;
    currentStep_ = 0;
    directionIndex_ = 0;
    directionSign_ = 1;
    running_ = false;
    orderCounter_ = 0;
    rng_ = settings_.randomSeed ? settings_.randomSeed : 0x125A0001u;
    nextNoteId_ = 1;
    patternCycle_ = 0u;
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

bool Engine::probabilityPasses(float probability) noexcept {
    probability = clamp01(probability);
    if (probability >= 1.0f)
        return true;
    if (probability <= 0.0f)
        return false;
    const float unit =
        static_cast<float>(nextRandom() & 0x00FFFFFFu) /
        static_cast<float>(0x01000000u);
    return unit < probability;
}

int Engine::collectOrderedPitches(
    int channel,
    std::array<int, kMidiNotes>& out) const noexcept {
    channel = clampInt(channel, 0, 15);
    int count = 0;
    for (int pitch = 0; pitch < kMidiNotes; ++pitch) {
        if (held_[static_cast<std::size_t>(channel)]
                 [static_cast<std::size_t>(pitch)].count > 0) {
            out[static_cast<std::size_t>(count++)] = pitch;
        }
    }
    return count;
}

int Engine::choosePitch(int channel, int logicalStep) noexcept {
    std::array<int, kMidiNotes> pitches {};
    const int count = collectOrderedPitches(channel, pitches);
    if (count <= 0)
        return -1;

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
            const int playedIndex = positiveMod(logicalStep, expandedCount);
            const int rank = playedIndex % count;
            const int octave = playedIndex / count;

            std::uint32_t previousOrder = 0;
            int selectedPitch = pitches[0];
            for (int r = 0; r <= rank; ++r) {
                std::uint32_t bestOrder = std::numeric_limits<std::uint32_t>::max();
                int bestPitch = selectedPitch;
                for (int n = 0; n < count; ++n) {
                    const int p = pitches[static_cast<std::size_t>(n)];
                    const auto order =
                        held_[static_cast<std::size_t>(channel)]
                             [static_cast<std::size_t>(p)].order;
                    if (order > previousOrder && order < bestOrder) {
                        bestOrder = order;
                        bestPitch = p;
                    }
                }
                selectedPitch = bestPitch;
                previousOrder = bestOrder;
            }
            return selectedPitch + octave * 12;
        }

        case Mode::Random: {
            const int index =
                static_cast<int>(nextRandom() % static_cast<std::uint32_t>(expandedCount));
            return pitchAtExpanded(index);
        }
    }

    return pitches[0];
}
int Engine::constrainPitch(
    int pitch,
    const std::array<int, kMidiNotes>& chord,
    int chordCount) const noexcept {
    pitch = clampInt(pitch, 0, 127);

    if (settings_.scalePolicy == ScalePolicy::Chromatic)
        return pitch;

    if (settings_.scalePolicy == ScalePolicy::ChordOnly && chordCount > 0) {
        int best = chord[0];
        int bestDistance = std::numeric_limits<int>::max();
        for (int chordIndex = 0; chordIndex < chordCount; ++chordIndex) {
            const int base = chord[static_cast<std::size_t>(chordIndex)];
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
    const auto& baseStep = settings_.steps[static_cast<std::size_t>(stepIndex)];
    Step step = baseStep;

    // EVOLVE is non-destructive: derive a temporary effective step from the
    // stored base step. Phase 0 leaves the first four full pattern cycles
    // untouched; later phases change only unlocked dimensions.
    const std::uint64_t evolvePhase = patternCycle_ / 4u;
    if (settings_.evolve > 0.0f &&
        evolvePhase > 0u &&
        !baseStep.locked) {
        const int phaseKey = static_cast<int>(
            (evolvePhase * 131u) & 0x7FFFFFFFu);

        const auto h0 = feelHash(
            settings_.randomSeed, phaseKey + stepIndex, 0, 0x45565230u);
        const auto h1 = feelHash(
            settings_.randomSeed, phaseKey + stepIndex, 0, 0x45565231u);
        const auto h2 = feelHash(
            settings_.randomSeed, phaseKey + stepIndex, 0, 0x45565232u);
        const auto h3 = feelHash(
            settings_.randomSeed, phaseKey + stepIndex, 0, 0x45565233u);
        const auto h4 = feelHash(
            settings_.randomSeed, phaseKey + stepIndex, 0, 0x45565234u);
        const auto h5 = feelHash(
            settings_.randomSeed, phaseKey + stepIndex, 0, 0x45565235u);
        const auto h6 = feelHash(
            settings_.randomSeed, phaseKey + stepIndex, 0, 0x45565236u);

        if (!settings_.evolveLocks.rhythm &&
            unitFromHash(h0) < 0.10f * settings_.evolve) {
            step.enabled = !step.enabled;
        }

        if (!settings_.evolveLocks.velocity) {
            step.velocity = std::clamp(
                baseStep.velocity +
                    bipolarFromHash(h1) * 0.12f * settings_.evolve,
                0.10f,
                1.0f);
        }

        if (!settings_.evolveLocks.gate) {
            step.gate = std::clamp(
                baseStep.gate +
                    bipolarFromHash(h2) * 0.14f * settings_.evolve,
                0.10f,
                1.0f);
        }

        if (!settings_.evolveLocks.ratchet &&
            unitFromHash(h3) < 0.12f * settings_.evolve) {
            const int direction = (h3 & 1u) != 0u ? 1 : -1;
            step.ratchet = static_cast<std::uint8_t>(std::clamp(
                static_cast<int>(baseStep.ratchet) + direction,
                1,
                4));
        }

        if (!settings_.evolveLocks.probability) {
            step.probability = std::clamp(
                baseStep.probability +
                    bipolarFromHash(h4) * 0.14f * settings_.evolve,
                0.10f,
                1.0f);
        }

        if (!settings_.evolveLocks.octave &&
            unitFromHash(h5) < 0.10f * settings_.evolve) {
            const int direction = (h5 & 1u) != 0u ? 1 : -1;
            step.octaveOffset = static_cast<std::int8_t>(std::clamp(
                static_cast<int>(baseStep.octaveOffset) + direction,
                -2,
                2));
        }

        if (!settings_.evolveLocks.note &&
            unitFromHash(h6) < 0.14f * settings_.evolve) {
            const int direction = (h6 & 1u) != 0u ? 1 : -1;
            step.noteOffset = static_cast<std::int8_t>(std::clamp(
                static_cast<int>(baseStep.noteOffset) + direction,
                -4,
                4));
        }
    }

    if (!step.enabled || !probabilityPasses(step.probability))
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

    std::array<int, kMidiNotes> chord {};
    const int chordCount = collectOrderedPitches(channel, chord);
    if (chordCount <= 0)
        return;

    int pitch = choosePitch(
        channel,
        directionIndex_ + static_cast<int>(step.noteOffset));
    if (pitch < 0)
        return;

    pitch += static_cast<int>(step.octaveOffset) * 12;
    pitch = constrainPitch(pitch, chord, chordCount);

    float sourceVelocity = 1.0f;
    const int basePc = positiveMod(pitch, 12);
    int chordRank = 0;
    for (int chordIndex = 0; chordIndex < chordCount; ++chordIndex) {
        const int p = chord[static_cast<std::size_t>(chordIndex)];
        if (positiveMod(p, 12) == basePc) {
            sourceVelocity = held_[static_cast<std::size_t>(channel)]
                                  [static_cast<std::size_t>(p)].velocity;
            chordRank = chordIndex;
            break;
        }
    }

    // Musical feel is intentionally delay-only. This preserves exact trigger
    // starts and avoids look-ahead/negative-time scheduling in a MIDI effect.
    // Combined feel delay is capped below half a step so note ordering remains
    // stable even at extreme settings.
    static constexpr double grooveDelay[8] {
        0.00, 0.12, 0.03, 0.16, 0.00, 0.10, 0.02, 0.14
    };
    static constexpr double grooveAccent[8] {
        1.05, 0.97, 1.02, 0.95, 1.04, 0.98, 1.01, 0.96
    };

    const int phraseStep = directionIndex_;
    const int grooveIndex = positiveMod(phraseStep, 8);
    const double grooveDelaySamples =
        stepDuration * static_cast<double>(grooveDelay[grooveIndex]) *
        static_cast<double>(settings_.groove);

    const float downbeatTightness =
        (positiveMod(phraseStep, 4) == 0) ? 0.25f : 1.0f;
    const float humanTimingUnit =
        unitFromHash(feelHash(settings_.randomSeed, phraseStep, pitch, 0x484D4E54u));
    const double humanDelaySamples =
        stepDuration * 0.06 *
        static_cast<double>(settings_.humanize) *
        static_cast<double>(downbeatTightness) *
        static_cast<double>(humanTimingUnit);

    const double strumRank =
        chordCount > 1
            ? static_cast<double>(chordRank) /
              static_cast<double>(chordCount - 1)
            : 0.0;
    const double strumDelaySamples =
        stepDuration * 0.12 *
        static_cast<double>(settings_.strum) *
        strumRank;

    const double feelDelay = std::min(
        stepDuration * 0.45,
        grooveDelaySamples + humanDelaySamples + strumDelaySamples);
    const double feltStepSample = stepSample + feelDelay;

    const float humanVelocity =
        1.0f +
        bipolarFromHash(
            feelHash(settings_.randomSeed, phraseStep, pitch, 0x56454C4Fu)) *
        0.12f * settings_.humanize;

    const float grooveVelocity =
        1.0f +
        static_cast<float>(
            (grooveAccent[grooveIndex] - 1.0) *
            static_cast<double>(settings_.groove));

    const float finalVelocity = clamp01(
        sourceVelocity * step.velocity * humanVelocity * grooveVelocity);

    const int ratchet = clampInt(step.ratchet, 1, 4);
    const double subDuration = stepDuration / static_cast<double>(ratchet);
    const double gateDuration =
        std::max(1.0,
                 subDuration * static_cast<double>(step.gate) *
                 static_cast<double>(settings_.globalGate));

    for (int hit = 0; hit < ratchet; ++hit) {
        const double onSample =
            feltStepSample + subDuration * static_cast<double>(hit);
        const int offset = clampInt(
            static_cast<int>(std::floor(onSample - blockStart + 1.0e-9)),
            0, std::max(0, blockSize - 1));

        const int noteId = nextNoteId_++;
        output.push_back({
            MidiInput::Type::NoteOn,
            offset,
            channel,
            pitch,
            finalVelocity,
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

    // Preserve the fractional phase of the currently pending step when host
    // tempo or Rate changes between process blocks. Without this correction,
    // the next step would still arrive on the old grid and only subsequent
    // steps would follow the new tempo.
    if (running_ &&
        lastStepDuration_ > 0.0 &&
        std::abs(stepDuration - lastStepDuration_) > 1.0e-9 &&
        nextStepSample_ > blockStart) {
        const double remaining = nextStepSample_ - blockStart;
        const double phaseRemaining =
            std::clamp(remaining / lastStepDuration_, 0.0, 1.0);
        nextStepSample_ =
            blockStart + phaseRemaining * stepDuration;
    }
    lastStepDuration_ = stepDuration;

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

            if (scheduled >= limitSample)
                break;

            stopExpired(blockStart,
                        std::min(limitSample, scheduled + 1.0),
                        numSamples,
                        output);

            if (scheduled >= blockStart)
                emitStep(scheduled, stepDuration, blockStart, numSamples, output);

            const int previousStep = currentStep_;
            currentStep_ = (currentStep_ + 1) % settings_.patternLength;
            if (previousStep == settings_.patternLength - 1 &&
                currentStep_ == 0) {
                ++patternCycle_;
            }
            ++directionIndex_;
            nextStepSample_ += stepDuration;
        }

        stopExpired(blockStart, limitSample, numSamples, output);
    };

    std::size_t i = 0;
    while (i < input.size()) {
        const int offset = clampInt(input[i].sampleOffset, 0, numSamples - 1);
        const double eventSample = blockStart + static_cast<double>(offset);

        // Render everything strictly before this timestamp using the old note set.
        emitUntil(eventSample);

        const bool hadHeldBeforeGroup = anyHeld();
        bool sawNoteOn = false;
        bool sawAllNotesOff = false;

        std::size_t j = i;
        for (; j < input.size(); ++j) {
            if (clampInt(input[j].sampleOffset, 0, numSamples - 1) != offset)
                break;

            const auto& event = input[j];
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
            currentStep_ = 0;
            directionIndex_ = 0;
            directionSign_ = 1;
            patternCycle_ = 0u;

            // Preserve event ordering semantics inside one timestamp:
            // AllNotesOff followed by a fresh NoteOn at the same sample
            // must be allowed to start a new phrase immediately.
            if (sawNoteOn && anyHeld()) {
                running_ = true;
                nextStepSample_ = eventSample;
                emitUntil(std::min(blockEnd, eventSample + 1.0));
            } else {
                running_ = false;
            }
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
            patternCycle_ = 0u;
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

    const auto before = [](const MidiOutput& a, const MidiOutput& b) {
        if (a.sampleOffset != b.sampleOffset)
            return a.sampleOffset < b.sampleOffset;
        if (a.type != b.type)
            return a.type == MidiInput::Type::NoteOff;
        return a.noteId < b.noteId;
    };
    for (std::size_t n = 1; n < output.size(); ++n) {
        MidiOutput key = output[n];
        std::size_t j = n;
        while (j > 0 && before(key, output[j - 1])) {
            output[j] = output[j - 1];
            --j;
        }
        output[j] = key;
    }

    absoluteSample_ = blockEnd;
}

} // namespace arporator
