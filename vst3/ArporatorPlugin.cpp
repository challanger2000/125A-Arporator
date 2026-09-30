#include "ArporatorPlugin.h"
#include "ArporatorIDs.h"
#include "ArporatorViews.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace arporator::vst3 {

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

constexpr std::size_t kInputReserve = 4096;
constexpr std::size_t kOutputReserve = 8192;
constexpr std::size_t kVstOutputReserve = 12288;
constexpr int kRateCount = 11;
constexpr int kScaleModeCount = 10;

double clamp01(double value) noexcept {
    if (!std::isfinite(value))
        return 0.0;
    return std::clamp(value, 0.0, 1.0);
}

int normIndex(double value, int maxIndex) noexcept {
    return std::clamp(
        static_cast<int>(std::lround(clamp01(value) * maxIndex)),
        0,
        maxIndex);
}

std::uint16_t scaleMask(int index) noexcept {
    switch (std::clamp(index, 0, kScaleModeCount - 1)) {
        case 1: return 0x05ADu; // natural minor
        case 2: return 0x06ADu; // dorian
        case 3: return 0x05ABu; // phrygian
        case 4: return 0x0AD5u; // lydian
        case 5: return 0x06B5u; // mixolydian
        case 6: return 0x09ADu; // harmonic minor
        case 7: return 0x0295u; // major pentatonic
        case 8: return 0x04A9u; // minor pentatonic
        case 9: return 0x04E9u; // blues
        default: return 0x0AB5u; // major
    }
}

double rateStepsPerQuarter(int index) noexcept {
    static constexpr double values[kRateCount] = {
        1.0,                 // 1/4
        2.0,                 // 1/8
        4.0,                 // 1/16
        8.0,                 // 1/32
        16.0,                // 1/64
        3.0,                 // 1/8T
        6.0,                 // 1/16T
        12.0,                // 1/32T
        1.3333333333333333,  // 1/8D
        2.6666666666666665,  // 1/16D
        5.3333333333333330   // 1/32D
    };
    return values[std::clamp(index, 0, kRateCount - 1)];
}

bool writeRuntimeState(IBStream* state, const RuntimeState& runtime) noexcept {
    if (!state)
        return false;

    IBStreamer s(state, kLittleEndian);
    if (!s.writeInt32(kStateMagic) ||
        !s.writeInt32(kStateVersion) ||
        !s.writeInt32(static_cast<int32>(runtime.settings.mode)) ||
        !s.writeInt32(runtime.rateIndex) ||
        !s.writeInt32(runtime.settings.octaveRange) ||
        !s.writeInt32(runtime.settings.patternLength) ||
        !s.writeDouble(runtime.settings.globalGate) ||
        !s.writeDouble(runtime.settings.swing) ||
        !s.writeDouble(runtime.settings.humanize) ||
        !s.writeDouble(runtime.settings.groove) ||
        !s.writeDouble(runtime.settings.strum) ||
        !s.writeDouble(runtime.settings.evolve) ||
        !s.writeDouble(runtime.variationAmount) ||
        !s.writeInt32(runtime.variationLocks.rhythm ? 1 : 0) ||
        !s.writeInt32(runtime.variationLocks.velocity ? 1 : 0) ||
        !s.writeInt32(runtime.variationLocks.gate ? 1 : 0) ||
        !s.writeInt32(runtime.variationLocks.ratchet ? 1 : 0) ||
        !s.writeInt32(runtime.variationLocks.probability ? 1 : 0) ||
        !s.writeInt32(runtime.variationLocks.octave ? 1 : 0) ||
        !s.writeInt32(static_cast<int32>(runtime.variationCounter)) ||
        !s.writeInt32(runtime.variationLocks.note ? 1 : 0) ||
        !s.writeInt32(runtime.settings.restartOnTrigger ? 1 : 0) ||
        !s.writeInt32(static_cast<int32>(runtime.settings.scalePolicy)) ||
        !s.writeInt32(runtime.settings.keyRoot) ||
        !s.writeInt32(runtime.scaleMode) ||
        !s.writeInt32(static_cast<int32>(runtime.settings.randomSeed))) {
        return false;
    }

    for (const auto& step : runtime.settings.steps) {
        if (!s.writeInt32(step.enabled ? 1 : 0) ||
            !s.writeInt32(step.locked ? 1 : 0) ||
            !s.writeInt32(static_cast<int32>(step.noteOffset)) ||
            !s.writeDouble(step.velocity) ||
            !s.writeDouble(step.gate) ||
            !s.writeInt32(static_cast<int32>(step.ratchet)) ||
            !s.writeDouble(step.probability) ||
            !s.writeInt32(static_cast<int32>(step.octaveOffset))) {
            return false;
        }
    }

    if (!s.writeInt32(runtime.variationBaseValid ? 1 : 0))
        return false;
    for (const auto& step : runtime.variationBaseSteps) {
        if (!s.writeInt32(step.enabled ? 1 : 0) ||
            !s.writeInt32(step.locked ? 1 : 0) ||
            !s.writeInt32(static_cast<int32>(step.noteOffset)) ||
            !s.writeDouble(step.velocity) ||
            !s.writeDouble(step.gate) ||
            !s.writeInt32(static_cast<int32>(step.ratchet)) ||
            !s.writeDouble(step.probability) ||
            !s.writeInt32(static_cast<int32>(step.octaveOffset))) {
            return false;
        }
    }

    return true;
}

bool readRuntimeState(IBStream* state, RuntimeState& runtime) noexcept {
    if (!state)
        return false;

    IBStreamer s(state, kLittleEndian);
    int32 magic = 0;
    int32 version = 0;
    int32 mode = 0;
    int32 octaveRange = 1;
    int32 patternLength = 16;
    int32 restart = 1;
    int32 scalePolicy = 0;
    int32 keyRoot = 0;
    int32 randomSeed = 0;

    double globalGate = 1.0;
    double swing = 0.0;
    double humanize = 0.0;
    double groove = 0.0;
    double strum = 0.0;
    double evolve = 0.0;
    double variationAmount = 0.35;
    int32 lockRhythm = 0;
    int32 lockVelocity = 0;
    int32 lockGate = 0;
    int32 lockRatchet = 0;
    int32 lockProbability = 0;
    int32 lockOctave = 0;
    int32 variationCounter = 0;
    int32 lockNote = 0;

    if (!s.readInt32(magic) || magic != kStateMagic ||
        !s.readInt32(version) || version < 1 || version > kStateVersion ||
        !s.readInt32(mode) ||
        !s.readInt32(runtime.rateIndex) ||
        !s.readInt32(octaveRange) ||
        !s.readInt32(patternLength) ||
        !s.readDouble(globalGate) ||
        !s.readDouble(swing)) {
        return false;
    }

    if (version >= 2) {
        if (!s.readDouble(humanize) ||
            !s.readDouble(groove) ||
            !s.readDouble(strum)) {
            return false;
        }
    }

    if (version >= 4) {
        if (!s.readDouble(evolve))
            return false;
    }

    if (version >= 3) {
        if (!s.readDouble(variationAmount) ||
            !s.readInt32(lockRhythm) ||
            !s.readInt32(lockVelocity) ||
            !s.readInt32(lockGate) ||
            !s.readInt32(lockRatchet) ||
            !s.readInt32(lockProbability) ||
            !s.readInt32(lockOctave) ||
            !s.readInt32(variationCounter)) {
            return false;
        }
    }

    if (version >= 5 && !s.readInt32(lockNote))
        return false;

    if (!s.readInt32(restart) ||
        !s.readInt32(scalePolicy) ||
        !s.readInt32(keyRoot) ||
        !s.readInt32(runtime.scaleMode) ||
        !s.readInt32(randomSeed)) {
        return false;
    }

    RuntimeState clean {};
    clean.rateIndex = std::clamp(runtime.rateIndex, 0, kRateCount - 1);
    clean.scaleMode = std::clamp(runtime.scaleMode, 0, kScaleModeCount - 1);
    clean.settings.mode =
        static_cast<Mode>(std::clamp(mode, 0, static_cast<int>(Mode::Random)));
    clean.settings.octaveRange = std::clamp(octaveRange, 1, 4);
    clean.settings.patternLength = std::clamp(patternLength, 1, kMaxSteps);
    clean.settings.globalGate =
        static_cast<float>(std::clamp(globalGate, 0.01, 1.0));
    clean.settings.swing =
        static_cast<float>(std::clamp(swing, 0.0, 1.0));
    clean.settings.humanize =
        static_cast<float>(std::clamp(humanize, 0.0, 1.0));
    clean.settings.groove =
        static_cast<float>(std::clamp(groove, 0.0, 1.0));
    clean.settings.strum =
        static_cast<float>(std::clamp(strum, 0.0, 1.0));
    clean.settings.evolve =
        static_cast<float>(std::clamp(evolve, 0.0, 1.0));
    clean.variationAmount =
        static_cast<float>(std::clamp(variationAmount, 0.0, 1.0));
    clean.variationLocks.rhythm = lockRhythm != 0;
    clean.variationLocks.velocity = lockVelocity != 0;
    clean.variationLocks.gate = lockGate != 0;
    clean.variationLocks.ratchet = lockRatchet != 0;
    clean.variationLocks.probability = lockProbability != 0;
    clean.variationLocks.octave = lockOctave != 0;
    clean.variationLocks.note = lockNote != 0;
    clean.variationCounter =
        variationCounter >= 0 ? static_cast<std::uint32_t>(variationCounter) : 0u;
    clean.settings.restartOnTrigger = restart != 0;
    clean.settings.scalePolicy =
        static_cast<ScalePolicy>(std::clamp(scalePolicy, 0, 2));
    clean.settings.keyRoot = ((keyRoot % 12) + 12) % 12;
    clean.settings.scaleMask = scaleMask(clean.scaleMode);
    clean.settings.randomSeed =
        randomSeed != 0 ? static_cast<std::uint32_t>(randomSeed) : 0x125A0001u;
    clean.settings.stepsPerQuarter = rateStepsPerQuarter(clean.rateIndex);

    for (auto& step : clean.settings.steps) {
        int32 enabled = 1;
        int32 locked = 0;
        int32 noteOffset = 0;
        int32 ratchet = 1;
        int32 octave = 0;
        double velocity = 1.0;
        double gate = 1.0;
        double probability = 1.0;

        if (!s.readInt32(enabled) ||
            (version >= 3 && !s.readInt32(locked)) ||
            (version >= 5 && !s.readInt32(noteOffset)) ||
            !s.readDouble(velocity) ||
            !s.readDouble(gate) ||
            !s.readInt32(ratchet) ||
            !s.readDouble(probability) ||
            !s.readInt32(octave)) {
            return false;
        }

        step.enabled = enabled != 0;
        step.locked = locked != 0;
        step.noteOffset =
            static_cast<std::int8_t>(std::clamp(noteOffset, -4, 4));
        step.velocity = static_cast<float>(std::clamp(velocity, 0.0, 1.0));
        step.gate = static_cast<float>(std::clamp(gate, 0.01, 1.0));
        step.ratchet =
            static_cast<std::uint8_t>(std::clamp(ratchet, 1, 4));
        step.probability =
            static_cast<float>(std::clamp(probability, 0.0, 1.0));
        step.octaveOffset =
            static_cast<std::int8_t>(std::clamp(octave, -2, 2));
    }

    if (version >= 6) {
        int32 baseValid = 0;
        if (!s.readInt32(baseValid))
            return false;
        clean.variationBaseValid = baseValid != 0;

        for (auto& step : clean.variationBaseSteps) {
            int32 enabled = 1;
            int32 locked = 0;
            int32 noteOffset = 0;
            int32 ratchet = 1;
            int32 octave = 0;
            double velocity = 1.0;
            double gate = 1.0;
            double probability = 1.0;

            if (!s.readInt32(enabled) ||
                !s.readInt32(locked) ||
                !s.readInt32(noteOffset) ||
                !s.readDouble(velocity) ||
                !s.readDouble(gate) ||
                !s.readInt32(ratchet) ||
                !s.readDouble(probability) ||
                !s.readInt32(octave)) {
                return false;
            }

            step.enabled = enabled != 0;
            step.locked = locked != 0;
            step.noteOffset =
                static_cast<std::int8_t>(std::clamp(noteOffset, -4, 4));
            step.velocity =
                static_cast<float>(std::clamp(velocity, 0.0, 1.0));
            step.gate =
                static_cast<float>(std::clamp(gate, 0.01, 1.0));
            step.ratchet =
                static_cast<std::uint8_t>(std::clamp(ratchet, 1, 4));
            step.probability =
                static_cast<float>(std::clamp(probability, 0.0, 1.0));
            step.octaveOffset =
                static_cast<std::int8_t>(std::clamp(octave, -2, 2));
        }
    }

    runtime = clean;
    return true;
}

std::u16string makeStepTitle(int step, const char16_t* suffix) {
    std::u16string title = u"Step ";
    if (step + 1 < 10)
        title += u'0';
    const int n = step + 1;
    if (n >= 10)
        title += static_cast<char16_t>(u'0' + (n / 10));
    title += static_cast<char16_t>(u'0' + (n % 10));
    title += u' ';
    title += suffix;
    return title;
}

} // namespace

Processor::Processor() {
    setControllerClass(kControllerUID);
    state_.rateIndex = 2;
    state_.scaleMode = 0;
    state_.settings.stepsPerQuarter = rateStepsPerQuarter(state_.rateIndex);
    state_.settings.globalGate = 0.75f;
    state_.settings.scaleMask = scaleMask(state_.scaleMode);
    engine_.setSettings(state_.settings);
    inputBuffer_.reserve(kInputReserve);
    outputBuffer_.reserve(kOutputReserve);
    passthroughBuffer_.reserve(kInputReserve);
    vstOutputBuffer_.reserve(kVstOutputReserve);
    publishedState_.store(state_);
}

tresult PLUGIN_API Processor::initialize(FUnknown* context) {
    const auto result = AudioEffect::initialize(context);
    if (result != kResultOk)
        return result;

    addEventInput(STR16("MIDI In"), 16, kMain, BusInfo::kDefaultActive);
    addEventOutput(STR16("MIDI Out"), 16, kMain, BusInfo::kDefaultActive);
    return kResultOk;
}

tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& setup) {
    const auto result = AudioEffect::setupProcessing(setup);
    if (result != kResultOk)
        return result;

    if (setup.sampleRate > 0.0)
        sampleRate_ = setup.sampleRate;

    if (inputBuffer_.capacity() < kInputReserve)
        inputBuffer_.reserve(kInputReserve);
    if (outputBuffer_.capacity() < kOutputReserve)
        outputBuffer_.reserve(kOutputReserve);
    if (passthroughBuffer_.capacity() < kInputReserve)
        passthroughBuffer_.reserve(kInputReserve);
    if (vstOutputBuffer_.capacity() < kVstOutputReserve)
        vstOutputBuffer_.reserve(kVstOutputReserve);

    return kResultOk;
}

uint32 PLUGIN_API Processor::getProcessContextRequirements() {
    return IProcessContextRequirements::kNeedTempo |
           IProcessContextRequirements::kNeedTransportState;
}

tresult PLUGIN_API Processor::setActive(TBool state) {
    if (state) {
        engine_.reset();
        hadTransportState_ = false;
        wasPlaying_ = false;
        lastPlayheadPublished_ = -1;
    }
    return AudioEffect::setActive(state);
}

tresult PLUGIN_API Processor::setProcessing(TBool state) {
    if (!state) {
        engine_.reset();
        inputBuffer_.clear();
        outputBuffer_.clear();
        passthroughBuffer_.clear();
        vstOutputBuffer_.clear();
        hadTransportState_ = false;
        wasPlaying_ = false;
        lastPlayheadPublished_ = -1;
    }
    return kResultOk;
}

double Processor::stepsPerQuarterForRate(int index) noexcept {
    return rateStepsPerQuarter(index);
}

std::uint16_t Processor::scaleMaskForMode(int index) noexcept {
    return scaleMask(index);
}

std::uint16_t Controller::scaleMaskForMode(int index) noexcept {
    return scaleMask(index);
}

void Processor::applyRuntimeState(const RuntimeState& state) noexcept {
    state_ = state;
    state_.rateIndex = std::clamp<int32>(state_.rateIndex, 0, kRateCount - 1);
    state_.scaleMode = std::clamp<int32>(state_.scaleMode, 0, kScaleModeCount - 1);
    state_.settings.stepsPerQuarter =
        stepsPerQuarterForRate(state_.rateIndex);
    state_.settings.scaleMask =
        scaleMaskForMode(state_.scaleMode);
    state_.settings.evolveLocks = state_.variationLocks;
    engine_.setSettings(state_.settings);
    variateTrigger_ = 0.0;
    variateResetTrigger_ = 0.0;
    settingsDirty_ = false;
}

void Processor::consumePendingState() noexcept {
    const auto pendingSequence = pendingState_.sequence();
    const auto applied =
        appliedPendingSequence_.load(std::memory_order_acquire);
    if (pendingSequence == 0u || pendingSequence == applied ||
        (pendingSequence & 1u) != 0u) {
        return;
    }

    RuntimeState pending {};
    std::uint64_t sequence = 0u;
    if (!pendingState_.tryLoad(pending, sequence) ||
        sequence == 0u || sequence == applied) {
        return;
    }

    applyRuntimeState(pending);
    engine_.reset();
    publishedState_.store(state_);
    appliedPendingSequence_.store(sequence, std::memory_order_release);
}

void Processor::publishState() noexcept {
    publishedState_.store(state_);
}

void Processor::applyNormalizedParameter(ParamID id, double value) noexcept {
    value = clamp01(value);

    if (id == kModeId) {
        state_.settings.mode =
            static_cast<Mode>(normIndex(value, static_cast<int>(Mode::Random)));
    } else if (id == kRateId) {
        state_.rateIndex = normIndex(value, kRateCount - 1);
        state_.settings.stepsPerQuarter =
            stepsPerQuarterForRate(state_.rateIndex);
    } else if (id == kOctavesId) {
        state_.settings.octaveRange = 1 + normIndex(value, 3);
    } else if (id == kPatternLengthId) {
        state_.settings.patternLength = 1 + normIndex(value, 31);
    } else if (id == kGateId) {
        state_.settings.globalGate =
            static_cast<float>(0.01 + value * 0.99);
    } else if (id == kSwingId) {
        state_.settings.swing = static_cast<float>(value);
    } else if (id == kTriggerModeId) {
        state_.settings.restartOnTrigger = value < 0.5;
    } else if (id == kScalePolicyId) {
        state_.settings.scalePolicy =
            static_cast<ScalePolicy>(normIndex(value, 2));
    } else if (id == kKeyRootId) {
        state_.settings.keyRoot = normIndex(value, 11);
    } else if (id == kScaleModeId) {
        state_.scaleMode = normIndex(value, kScaleModeCount - 1);
        state_.settings.scaleMask = scaleMaskForMode(state_.scaleMode);
    } else if (id == kHumanizeId) {
        state_.settings.humanize = static_cast<float>(value);
    } else if (id == kGrooveId) {
        state_.settings.groove = static_cast<float>(value);
    } else if (id == kStrumId) {
        state_.settings.strum = static_cast<float>(value);
    } else if (id == kEvolveId) {
        state_.settings.evolve = static_cast<float>(value);
    } else if (id == kVariationAmountId) {
        state_.variationAmount = static_cast<float>(value);
    } else if (id == kLockRhythmId) {
        state_.variationLocks.rhythm = value >= 0.5;
        state_.settings.evolveLocks.rhythm = state_.variationLocks.rhythm;
    } else if (id == kLockVelocityId) {
        state_.variationLocks.velocity = value >= 0.5;
        state_.settings.evolveLocks.velocity = state_.variationLocks.velocity;
    } else if (id == kLockGateId) {
        state_.variationLocks.gate = value >= 0.5;
        state_.settings.evolveLocks.gate = state_.variationLocks.gate;
    } else if (id == kLockRatchetId) {
        state_.variationLocks.ratchet = value >= 0.5;
        state_.settings.evolveLocks.ratchet = state_.variationLocks.ratchet;
    } else if (id == kLockProbabilityId) {
        state_.variationLocks.probability = value >= 0.5;
        state_.settings.evolveLocks.probability = state_.variationLocks.probability;
    } else if (id == kLockOctaveId) {
        state_.variationLocks.octave = value >= 0.5;
        state_.settings.evolveLocks.octave = state_.variationLocks.octave;
    } else if (id == kLockNoteId) {
        state_.variationLocks.note = value >= 0.5;
        state_.settings.evolveLocks.note = state_.variationLocks.note;
    } else if (id == kVariateTriggerId) {
        if (std::abs(value - variateTrigger_) > 0.25) {
            variateTrigger_ = value;

            if (!state_.variationBaseValid) {
                state_.variationBaseSteps = state_.settings.steps;
                state_.variationBaseValid = true;
                state_.variationCounter = 0u;
            }

            VariationRequest request {};
            request.amount = state_.variationAmount;
            request.locks = state_.variationLocks;
            ++state_.variationCounter;
            request.seed = state_.settings.randomSeed ^
                (0x9E3779B9u * state_.variationCounter);

            auto base = state_.settings;
            base.steps = state_.variationBaseSteps;
            state_.settings = variateSettings(base, request);
            variateParametersDirty_ = true;
        }
    } else if (id == kVariateResetId) {
        if (std::abs(value - variateResetTrigger_) > 0.25) {
            variateResetTrigger_ = value;
            if (state_.variationBaseValid) {
                state_.settings.steps = state_.variationBaseSteps;
                state_.variationBaseValid = false;
                state_.variationCounter = 0u;
                variateParametersDirty_ = true;
            }
        }
    } else if (id >= kStepEnableBase &&
               id < kStepEnableBase + kStepParamCount) {
        const auto index = static_cast<std::size_t>(id - kStepEnableBase);
        state_.settings.steps[index].enabled = value >= 0.5;
    } else if (id >= kStepVelocityBase &&
               id < kStepVelocityBase + kStepParamCount) {
        const auto index = static_cast<std::size_t>(id - kStepVelocityBase);
        state_.settings.steps[index].velocity = static_cast<float>(value);
    } else if (id >= kStepGateBase &&
               id < kStepGateBase + kStepParamCount) {
        const auto index = static_cast<std::size_t>(id - kStepGateBase);
        state_.settings.steps[index].gate =
            static_cast<float>(0.01 + value * 0.99);
    } else if (id >= kStepRatchetBase &&
               id < kStepRatchetBase + kStepParamCount) {
        const auto index = static_cast<std::size_t>(id - kStepRatchetBase);
        state_.settings.steps[index].ratchet =
            static_cast<std::uint8_t>(1 + normIndex(value, 3));
    } else if (id >= kStepProbabilityBase &&
               id < kStepProbabilityBase + kStepParamCount) {
        const auto index = static_cast<std::size_t>(id - kStepProbabilityBase);
        state_.settings.steps[index].probability = static_cast<float>(value);
    } else if (id >= kStepOctaveBase &&
               id < kStepOctaveBase + kStepParamCount) {
        const auto index = static_cast<std::size_t>(id - kStepOctaveBase);
        state_.settings.steps[index].octaveOffset =
            static_cast<std::int8_t>(normIndex(value, 4) - 2);
    } else if (id >= kStepLockBase &&
               id < kStepLockBase + kStepParamCount) {
        const auto index = static_cast<std::size_t>(id - kStepLockBase);
        state_.settings.steps[index].locked = value >= 0.5;
    } else if (id >= kStepNoteBase &&
               id < kStepNoteBase + kStepParamCount) {
        const auto index = static_cast<std::size_t>(id - kStepNoteBase);
        state_.settings.steps[index].noteOffset =
            static_cast<std::int8_t>(normIndex(value, 8) - 4);
    } else {
        return;
    }

    settingsDirty_ = true;
}

void Processor::readParameterChanges(IParameterChanges* changes) noexcept {
    if (!changes)
        return;

    const int32 count = changes->getParameterCount();
    for (int32 i = 0; i < count; ++i) {
        auto* queue = changes->getParameterData(i);
        if (!queue || queue->getPointCount() <= 0)
            continue;

        const auto id = queue->getParameterId();

        // Trigger parameters must consume every point so a short 0->1->0 pulse
        // inside one host block cannot disappear when only the final value is 0.
        if (id == kVariateTriggerId || id == kVariateResetId) {
            for (int32 point = 0; point < queue->getPointCount(); ++point) {
                int32 sampleOffset = 0;
                ParamValue value = 0.0;
                if (queue->getPoint(point, sampleOffset, value) == kResultTrue)
                    applyNormalizedParameter(id, value);
            }
            continue;
        }

        int32 sampleOffset = 0;
        ParamValue value = 0.0;
        if (queue->getPoint(queue->getPointCount() - 1,
                            sampleOffset,
                            value) == kResultTrue) {
            applyNormalizedParameter(id, value);
        }
    }
}

tresult PLUGIN_API Processor::process(ProcessData& data) {
    consumePendingState();
    readParameterChanges(data.inputParameterChanges);

    if (settingsDirty_) {
        engine_.setSettings(state_.settings);
        settingsDirty_ = false;
        publishState();
    }

    // Variate mutates the stored pattern in the processor. Mirror the changed
    // step values back to the host/controller exactly once so GUI, automation
    // state and processor playback cannot diverge.
    if (variateParametersDirty_ && data.outputParameterChanges) {
        const auto publishParam = [&](ParamID id, ParamValue normalized) {
            int32 queueIndex = 0;
            if (auto* queue =
                    data.outputParameterChanges->addParameterData(id, queueIndex)) {
                int32 pointIndex = 0;
                queue->addPoint(0, clamp01(normalized), pointIndex);
            }
        };

        for (int i = 0; i < kStepParamCount; ++i) {
            const auto& step =
                state_.settings.steps[static_cast<std::size_t>(i)];
            publishParam(kStepEnableBase + i, step.enabled ? 1.0 : 0.0);
            publishParam(kStepVelocityBase + i, step.velocity);
            publishParam(
                kStepGateBase + i,
                (static_cast<double>(step.gate) - 0.01) / 0.99);
            publishParam(
                kStepRatchetBase + i,
                static_cast<double>(step.ratchet - 1) / 3.0);
            publishParam(kStepProbabilityBase + i, step.probability);
            publishParam(
                kStepNoteBase + i,
                static_cast<double>(step.noteOffset + 4) / 8.0);
            publishParam(
                kStepOctaveBase + i,
                static_cast<double>(step.octaveOffset + 2) / 4.0);
        }

        variateParametersDirty_ = false;
    }

    if (data.numSamples <= 0)
        return kResultOk;

    if (data.processContext && data.processContext->sampleRate > 0.0)
        sampleRate_ = data.processContext->sampleRate;

    double tempo = 120.0;
    bool hasTransportState = false;
    bool playing = false;

    if (data.processContext) {
        hasTransportState = true;
        playing =
            (data.processContext->state & ProcessContext::kPlaying) != 0;
        if ((data.processContext->state & ProcessContext::kTempoValid) != 0 &&
            data.processContext->tempo > 0.0 &&
            std::isfinite(data.processContext->tempo)) {
            tempo = data.processContext->tempo;
        }
    }

    inputBuffer_.clear();
    outputBuffer_.clear();
    passthroughBuffer_.clear();
    vstOutputBuffer_.clear();

    if (hasTransportState && hadTransportState_ &&
        wasPlaying_ && !playing) {
        inputBuffer_.push_back(
            {MidiInput::Type::AllNotesOff, 0, 0, 0, 0.0f});
    }

    hadTransportState_ = hasTransportState;
    wasPlaying_ = playing;

    bool inputOverflow = false;
    if (data.inputEvents) {
        const int32 count = data.inputEvents->getEventCount();
        for (int32 i = 0; i < count; ++i) {
            Event event {};
            if (data.inputEvents->getEvent(i, event) != kResultTrue)
                continue;

            MidiInput converted {};
            bool accept = false;

            if (event.type == Event::kNoteOnEvent) {
                converted.type =
                    event.noteOn.velocity > 0.0f
                        ? MidiInput::Type::NoteOn
                        : MidiInput::Type::NoteOff;
                converted.sampleOffset = event.sampleOffset;
                converted.channel = event.noteOn.channel;
                converted.pitch = event.noteOn.pitch;
                converted.velocity = event.noteOn.velocity;
                accept = true;
            } else if (event.type == Event::kNoteOffEvent) {
                converted.type = MidiInput::Type::NoteOff;
                converted.sampleOffset = event.sampleOffset;
                converted.channel = event.noteOff.channel;
                converted.pitch = event.noteOff.pitch;
                converted.velocity = event.noteOff.velocity;
                accept = true;
            }

            if (accept) {
                if (inputBuffer_.size() >= inputBuffer_.capacity()) {
                    inputOverflow = true;
                    break;
                }
                inputBuffer_.push_back(converted);
            } else {
                // Arporator replaces note traffic, but non-note MIDI/event data
                // should continue downstream whenever the host supplies it.
                if (passthroughBuffer_.size() < passthroughBuffer_.capacity()) {
                    event.busIndex = 0;
                    event.sampleOffset =
                        std::clamp<int32>(event.sampleOffset, 0, data.numSamples - 1);
                    passthroughBuffer_.push_back(event);
                }
            }
        }
    }

    if (inputOverflow) {
        inputBuffer_.clear();
        inputBuffer_.push_back(
            {MidiInput::Type::AllNotesOff, 0, 0, 0, 0.0f});
    }

    engine_.process(
        sampleRate_,
        tempo,
        data.numSamples,
        inputBuffer_,
        outputBuffer_);

    const int playheadState =
        engine_.running() && engine_.lastEmittedStep() >= 0
            ? std::clamp(engine_.lastEmittedStep() + 1, 1, kStepParamCount)
            : 0;

    if (playheadState != lastPlayheadPublished_ &&
        data.outputParameterChanges) {
        int32 queueIndex = 0;
        if (auto* queue = data.outputParameterChanges->addParameterData(
                kPlayheadId, queueIndex)) {
            int32 pointIndex = 0;
            const ParamValue normalized =
                static_cast<ParamValue>(playheadState) /
                static_cast<ParamValue>(kStepParamCount);
            const int32 sampleOffset =
                std::max<int32>(0, data.numSamples - 1);
            if (queue->addPoint(
                    sampleOffset, normalized, pointIndex) == kResultTrue) {
                lastPlayheadPublished_ = playheadState;
            }
        }
    }

    if (!data.outputEvents)
        return kResultOk;

    for (const auto& event : passthroughBuffer_) {
        if (vstOutputBuffer_.size() < vstOutputBuffer_.capacity())
            vstOutputBuffer_.push_back(event);
    }

    for (const auto& event : outputBuffer_) {
        Event out {};
        out.busIndex = 0;
        out.sampleOffset =
            std::clamp<int32>(event.sampleOffset, 0, data.numSamples - 1);

        if (event.type == MidiInput::Type::NoteOn) {
            out.type = Event::kNoteOnEvent;
            out.noteOn.channel =
                static_cast<int16>(std::clamp(event.channel, 0, 15));
            out.noteOn.pitch =
                static_cast<int16>(std::clamp(event.pitch, 0, 127));
            out.noteOn.velocity =
                static_cast<float>(std::clamp<double>(event.velocity, 0.0, 1.0));
            out.noteOn.noteId = event.noteId;
        } else if (event.type == MidiInput::Type::NoteOff) {
            out.type = Event::kNoteOffEvent;
            out.noteOff.channel =
                static_cast<int16>(std::clamp(event.channel, 0, 15));
            out.noteOff.pitch =
                static_cast<int16>(std::clamp(event.pitch, 0, 127));
            out.noteOff.velocity = 0.0f;
            out.noteOff.noteId = event.noteId;
        } else {
            continue;
        }

        if (vstOutputBuffer_.size() < vstOutputBuffer_.capacity())
            vstOutputBuffer_.push_back(out);
    }

    const auto eventPriority = [](const Event& event) noexcept {
        if (event.type == Event::kNoteOffEvent)
            return 0;
        if (event.type == Event::kNoteOnEvent)
            return 2;
        return 1;
    };
    const auto before = [&](const Event& a, const Event& b) noexcept {
        if (a.sampleOffset != b.sampleOffset)
            return a.sampleOffset < b.sampleOffset;
        return eventPriority(a) < eventPriority(b);
    };

    for (std::size_t i = 1; i < vstOutputBuffer_.size(); ++i) {
        Event key = vstOutputBuffer_[i];
        std::size_t j = i;
        while (j > 0 && before(key, vstOutputBuffer_[j - 1])) {
            vstOutputBuffer_[j] = vstOutputBuffer_[j - 1];
            --j;
        }
        vstOutputBuffer_[j] = key;
    }

    for (auto& event : vstOutputBuffer_)
        data.outputEvents->addEvent(event);

    return kResultOk;
}

bool Processor::readState(IBStream* stream, RuntimeState& state) const noexcept {
    return readRuntimeState(stream, state);
}

bool Processor::writeState(IBStream* stream,
                           const RuntimeState& state) const noexcept {
    return writeRuntimeState(stream, state);
}

tresult PLUGIN_API Processor::setState(IBStream* state) {
    RuntimeState restored {};
    if (!readState(state, restored))
        return kResultFalse;
    pendingState_.store(restored);
    return kResultOk;
}

tresult PLUGIN_API Processor::getState(IBStream* state) {
    if (!state)
        return kInvalidArgument;

    RuntimeState snapshot {};
    std::uint64_t sequence = 0u;

    const auto pendingSequence = pendingState_.sequence();
    const auto applied =
        appliedPendingSequence_.load(std::memory_order_acquire);

    if (pendingSequence != 0u && pendingSequence != applied &&
        (pendingSequence & 1u) == 0u &&
        pendingState_.tryLoad(snapshot, sequence)) {
        return writeState(state, snapshot) ? kResultOk : kResultFalse;
    }

    for (int attempt = 0; attempt < 32; ++attempt) {
        if (publishedState_.tryLoad(snapshot, sequence))
            return writeState(state, snapshot) ? kResultOk : kResultFalse;
    }

    return kResultFalse;
}

tresult PLUGIN_API Controller::initialize(FUnknown* context) {
    const auto result = EditControllerEx1::initialize(context);
    if (result != kResultOk)
        return result;

    auto* mode = new StringListParameter(STR16("Mode"), kModeId);
    mode->appendString(STR16("UP"));
    mode->appendString(STR16("DOWN"));
    mode->appendString(STR16("UP-DOWN"));
    mode->appendString(STR16("DOWN-UP"));
    mode->appendString(STR16("PLAYED"));
    mode->appendString(STR16("RANDOM"));
    parameters.addParameter(mode);

    auto* rate = new StringListParameter(STR16("Rate"), kRateId);
    rate->appendString(STR16("1/4"));
    rate->appendString(STR16("1/8"));
    rate->appendString(STR16("1/16"));
    rate->appendString(STR16("1/32"));
    rate->appendString(STR16("1/64"));
    rate->appendString(STR16("1/8T"));
    rate->appendString(STR16("1/16T"));
    rate->appendString(STR16("1/32T"));
    rate->appendString(STR16("1/8D"));
    rate->appendString(STR16("1/16D"));
    rate->appendString(STR16("1/32D"));
    rate->getInfo().defaultNormalizedValue = 2.0 / 10.0;
    rate->setNormalized(rate->getInfo().defaultNormalizedValue);
    parameters.addParameter(rate);

    auto* octaves = new StringListParameter(STR16("Octaves"), kOctavesId);
    octaves->appendString(STR16("1"));
    octaves->appendString(STR16("2"));
    octaves->appendString(STR16("3"));
    octaves->appendString(STR16("4"));
    parameters.addParameter(octaves);

    parameters.addParameter(new RangeParameter(
        STR16("Pattern Length"), kPatternLengthId, STR16("steps"),
        1.0, 32.0, 16.0, 31));

    parameters.addParameter(new RangeParameter(
        STR16("Gate"), kGateId, STR16("%"),
        1.0, 100.0, 75.0, 0));

    parameters.addParameter(new RangeParameter(
        STR16("Swing"), kSwingId, STR16("%"),
        0.0, 100.0, 0.0, 0));

    auto* trigger =
        new StringListParameter(STR16("Trigger"), kTriggerModeId);
    trigger->appendString(STR16("RESTART"));
    trigger->appendString(STR16("CONTINUE"));
    parameters.addParameter(trigger);

    auto* policy =
        new StringListParameter(STR16("Pitch Policy"), kScalePolicyId);
    policy->appendString(STR16("CHORD ONLY"));
    policy->appendString(STR16("SCALE"));
    policy->appendString(STR16("CHROMATIC"));
    parameters.addParameter(policy);

    auto* key = new StringListParameter(STR16("Key"), kKeyRootId);
    key->appendString(STR16("C"));
    key->appendString(STR16("C#"));
    key->appendString(STR16("D"));
    key->appendString(STR16("D#"));
    key->appendString(STR16("E"));
    key->appendString(STR16("F"));
    key->appendString(STR16("F#"));
    key->appendString(STR16("G"));
    key->appendString(STR16("G#"));
    key->appendString(STR16("A"));
    key->appendString(STR16("A#"));
    key->appendString(STR16("B"));
    parameters.addParameter(key);

    auto* scale =
        new StringListParameter(STR16("Scale"), kScaleModeId);
    scale->appendString(STR16("MAJOR"));
    scale->appendString(STR16("MINOR"));
    scale->appendString(STR16("DORIAN"));
    scale->appendString(STR16("PHRYGIAN"));
    scale->appendString(STR16("LYDIAN"));
    scale->appendString(STR16("MIXOLYDIAN"));
    scale->appendString(STR16("HARMONIC MINOR"));
    scale->appendString(STR16("MAJOR PENT"));
    scale->appendString(STR16("MINOR PENT"));
    scale->appendString(STR16("BLUES"));
    parameters.addParameter(scale);

    parameters.addParameter(new RangeParameter(
        STR16("Humanize"), kHumanizeId, STR16("%"),
        0.0, 100.0, 0.0, 0));

    parameters.addParameter(new RangeParameter(
        STR16("Groove"), kGrooveId, STR16("%"),
        0.0, 100.0, 0.0, 0));

    parameters.addParameter(new RangeParameter(
        STR16("Strum"), kStrumId, STR16("%"),
        0.0, 100.0, 0.0, 0));

    parameters.addParameter(new RangeParameter(
        STR16("Playhead"), kPlayheadId, STR16(""),
        0.0, 32.0, 0.0, 32,
        ParameterInfo::kIsHidden | ParameterInfo::kIsReadOnly));

    parameters.addParameter(new RangeParameter(
        STR16("Evolve"), kEvolveId, STR16("%"),
        0.0, 100.0, 0.0, 0));

    parameters.addParameter(new RangeParameter(
        STR16("Variation Amount"), kVariationAmountId, STR16("%"),
        0.0, 100.0, 35.0, 0));

    auto addLock = [&](const TChar* title, ParamID id) {
        auto* lock = new StringListParameter(title, id);
        lock->appendString(STR16("OPEN"));
        lock->appendString(STR16("LOCK"));
        parameters.addParameter(lock);
    };
    addLock(STR16("Lock Rhythm"), kLockRhythmId);
    addLock(STR16("Lock Velocity"), kLockVelocityId);
    addLock(STR16("Lock Gate"), kLockGateId);
    addLock(STR16("Lock Ratchet"), kLockRatchetId);
    addLock(STR16("Lock Probability"), kLockProbabilityId);
    addLock(STR16("Lock Octave"), kLockOctaveId);
    addLock(STR16("Lock Note"), kLockNoteId);

    auto* variate = new StringListParameter(
        STR16("Variate Trigger"), kVariateTriggerId);
    variate->appendString(STR16("READY"));
    variate->appendString(STR16("VARIATE"));
    parameters.addParameter(variate);

    auto* variateReset = new StringListParameter(
        STR16("Variate Reset"), kVariateResetId);
    variateReset->appendString(STR16("READY"));
    variateReset->appendString(STR16("RESET"));
    parameters.addParameter(variateReset);

    for (int i = 0; i < kStepParamCount; ++i) {
        auto enableTitle = makeStepTitle(i, u"On");
        auto* enabled =
            new StringListParameter(enableTitle.c_str(), kStepEnableBase + i);
        enabled->appendString(STR16("OFF"));
        enabled->appendString(STR16("ON"));
        enabled->getInfo().defaultNormalizedValue = 1.0;
        enabled->setNormalized(1.0);
        parameters.addParameter(enabled);

        auto velocityTitle = makeStepTitle(i, u"Velocity");
        parameters.addParameter(new RangeParameter(
            velocityTitle.c_str(), kStepVelocityBase + i, STR16("%"),
            0.0, 100.0, 100.0, 0));

        auto gateTitle = makeStepTitle(i, u"Gate");
        parameters.addParameter(new RangeParameter(
            gateTitle.c_str(), kStepGateBase + i, STR16("%"),
            1.0, 100.0, 100.0, 0));

        auto ratchetTitle = makeStepTitle(i, u"Ratchet");
        auto* ratchet =
            new StringListParameter(ratchetTitle.c_str(), kStepRatchetBase + i);
        ratchet->appendString(STR16("1x"));
        ratchet->appendString(STR16("2x"));
        ratchet->appendString(STR16("3x"));
        ratchet->appendString(STR16("4x"));
        parameters.addParameter(ratchet);

        auto probabilityTitle = makeStepTitle(i, u"Probability");
        parameters.addParameter(new RangeParameter(
            probabilityTitle.c_str(), kStepProbabilityBase + i, STR16("%"),
            0.0, 100.0, 100.0, 0));

        auto noteTitle = makeStepTitle(i, u"Note");
        auto* note =
            new StringListParameter(noteTitle.c_str(), kStepNoteBase + i);
        note->appendString(STR16("-4"));
        note->appendString(STR16("-3"));
        note->appendString(STR16("-2"));
        note->appendString(STR16("-1"));
        note->appendString(STR16("0"));
        note->appendString(STR16("+1"));
        note->appendString(STR16("+2"));
        note->appendString(STR16("+3"));
        note->appendString(STR16("+4"));
        note->getInfo().defaultNormalizedValue = 0.5;
        note->setNormalized(note->getInfo().defaultNormalizedValue);
        parameters.addParameter(note);

        auto octaveTitle = makeStepTitle(i, u"Octave");
        auto* octave =
            new StringListParameter(octaveTitle.c_str(), kStepOctaveBase + i);
        octave->appendString(STR16("-2"));
        octave->appendString(STR16("-1"));
        octave->appendString(STR16("0"));
        octave->appendString(STR16("+1"));
        octave->appendString(STR16("+2"));
        octave->getInfo().defaultNormalizedValue = 0.5;
        octave->setNormalized(octave->getInfo().defaultNormalizedValue);
        parameters.addParameter(octave);

        auto lockTitle = makeStepTitle(i, u"Lock");
        auto* stepLock =
            new StringListParameter(lockTitle.c_str(), kStepLockBase + i);
        stepLock->appendString(STR16("OPEN"));
        stepLock->appendString(STR16("LOCK"));
        parameters.addParameter(stepLock);
    }

    return kResultOk;
}

tresult PLUGIN_API Controller::setComponentState(IBStream* state) {
    RuntimeState runtime {};
    if (!readRuntimeState(state, runtime))
        return kResultFalse;

    auto setNorm = [&](ParamID id, double value) {
        if (auto* parameter = parameters.getParameter(id))
            parameter->setNormalized(clamp01(value));
    };

    setNorm(kModeId,
            static_cast<double>(runtime.settings.mode) /
            static_cast<double>(Mode::Random));
    setNorm(kRateId,
            static_cast<double>(runtime.rateIndex) /
            static_cast<double>(kRateCount - 1));
    setNorm(kOctavesId,
            static_cast<double>(runtime.settings.octaveRange - 1) / 3.0);
    setNorm(kPatternLengthId,
            static_cast<double>(runtime.settings.patternLength - 1) / 31.0);
    setNorm(kGateId,
            (static_cast<double>(runtime.settings.globalGate) - 0.01) / 0.99);
    setNorm(kSwingId, runtime.settings.swing);
    setNorm(kTriggerModeId,
            runtime.settings.restartOnTrigger ? 0.0 : 1.0);
    setNorm(kScalePolicyId,
            static_cast<double>(runtime.settings.scalePolicy) / 2.0);
    setNorm(kKeyRootId,
            static_cast<double>(runtime.settings.keyRoot) / 11.0);
    setNorm(kScaleModeId,
            static_cast<double>(runtime.scaleMode) /
            static_cast<double>(kScaleModeCount - 1));
    setNorm(kHumanizeId, runtime.settings.humanize);
    setNorm(kGrooveId, runtime.settings.groove);
    setNorm(kStrumId, runtime.settings.strum);
    setNorm(kEvolveId, runtime.settings.evolve);
    setNorm(kVariationAmountId, runtime.variationAmount);
    setNorm(kLockRhythmId, runtime.variationLocks.rhythm ? 1.0 : 0.0);
    setNorm(kLockVelocityId, runtime.variationLocks.velocity ? 1.0 : 0.0);
    setNorm(kLockGateId, runtime.variationLocks.gate ? 1.0 : 0.0);
    setNorm(kLockRatchetId, runtime.variationLocks.ratchet ? 1.0 : 0.0);
    setNorm(kLockProbabilityId, runtime.variationLocks.probability ? 1.0 : 0.0);
    setNorm(kLockOctaveId, runtime.variationLocks.octave ? 1.0 : 0.0);
    setNorm(kLockNoteId, runtime.variationLocks.note ? 1.0 : 0.0);
    setNorm(kVariateTriggerId, 0.0);
    setNorm(kVariateResetId, 0.0);
    setNorm(kPlayheadId, 0.0);

    // Preserve the captured pre-Variate pattern in the controller as well.
    // RESET must update the visible grid immediately after project recall,
    // even when the audio processor is currently idle and cannot mirror
    // output parameter changes back to the host yet.
    variationBasePreviewValid_ = runtime.variationBaseValid;
    variationBasePreviewSteps_ = runtime.variationBaseSteps;

    for (int i = 0; i < kStepParamCount; ++i) {
        const auto& step =
            runtime.settings.steps[static_cast<std::size_t>(i)];
        setNorm(kStepEnableBase + i, step.enabled ? 1.0 : 0.0);
        setNorm(kStepVelocityBase + i, step.velocity);
        setNorm(kStepGateBase + i,
                (static_cast<double>(step.gate) - 0.01) / 0.99);
        setNorm(kStepRatchetBase + i,
                static_cast<double>(step.ratchet - 1) / 3.0);
        setNorm(kStepProbabilityBase + i, step.probability);
        setNorm(kStepNoteBase + i,
                static_cast<double>(step.noteOffset + 4) / 8.0);
        setNorm(kStepOctaveBase + i,
                static_cast<double>(step.octaveOffset + 2) / 4.0);
        setNorm(kStepLockBase + i, step.locked ? 1.0 : 0.0);
    }

    if (editor_ && editor_->getFrame())
        editor_->getFrame()->invalid();
    return kResultOk;
}


tresult PLUGIN_API Controller::setParamNormalized(
    ParamID tag,
    ParamValue value) {
    const auto result = EditControllerEx1::setParamNormalized(tag, value);
    if (result == kResultOk &&
        editor_ &&
        editor_->getFrame()) {
        // StepGridView and SelectedStepView intentionally aggregate many
        // parameters and are not individually bound VSTGUI controls. Redraw
        // them for host automation, processor output (playhead) and recall.
        editor_->getFrame()->invalid();
    }
    return result;
}

tresult PLUGIN_API Controller::setState(IBStream* state) {
    if (!state)
        return kInvalidArgument;
    IBStreamer stream(state, kLittleEndian);
    double zoom = 1.0;
    int32 selected = 0;
    if (!stream.readDouble(zoom))
        return kResultOk;
    if (!stream.readInt32(selected))
        selected = 0;
    guiZoom_ = zoom >= 1.35 ? 1.5 : (zoom >= 1.10 ? 1.2 : 1.0);
    selectedStep_ = std::clamp<int32>(selected, 0, kStepParamCount - 1);

    // Controller-state extension (backward compatible): Studio One may restore
    // controller and processor state independently. Persist the captured
    // pre-Variate pattern here as well so RESET never depends on a later
    // setComponentState() callback to recover its baseline.
    int32 previewMagic = 0;
    if (stream.readInt32(previewMagic) && previewMagic == 0x41525056) { // "ARPV"
        int32 previewValid = 0;
        if (stream.readInt32(previewValid)) {
            std::array<Step, kMaxSteps> restored {};
            bool ok = true;
            for (auto& step : restored) {
                int32 enabled = 1;
                int32 locked = 0;
                int32 noteOffset = 0;
                int32 ratchet = 1;
                int32 octave = 0;
                double velocity = 1.0;
                double gate = 1.0;
                double probability = 1.0;
                ok = ok &&
                    stream.readInt32(enabled) &&
                    stream.readInt32(locked) &&
                    stream.readInt32(noteOffset) &&
                    stream.readDouble(velocity) &&
                    stream.readDouble(gate) &&
                    stream.readInt32(ratchet) &&
                    stream.readDouble(probability) &&
                    stream.readInt32(octave);
                if (!ok)
                    break;
                step.enabled = enabled != 0;
                step.locked = locked != 0;
                step.noteOffset =
                    static_cast<std::int8_t>(std::clamp(noteOffset, -4, 4));
                step.velocity =
                    static_cast<float>(std::clamp(velocity, 0.0, 1.0));
                step.gate =
                    static_cast<float>(std::clamp(gate, 0.01, 1.0));
                step.ratchet =
                    static_cast<std::uint8_t>(std::clamp(ratchet, 1, 4));
                step.probability =
                    static_cast<float>(std::clamp(probability, 0.0, 1.0));
                step.octaveOffset =
                    static_cast<std::int8_t>(std::clamp(octave, -2, 2));
            }
            if (ok) {
                variationBasePreviewValid_ = previewValid != 0;
                variationBasePreviewSteps_ = restored;
            }
        }
    }

    if (editor_)
        editor_->setZoomFactor(guiZoom_);
    return kResultOk;
}

tresult PLUGIN_API Controller::getState(IBStream* state) {
    if (!state)
        return kInvalidArgument;
    IBStreamer stream(state, kLittleEndian);
    if (!stream.writeDouble(guiZoom_) ||
        !stream.writeInt32(static_cast<int32>(selectedStep_)) ||
        !stream.writeInt32(0x41525056) || // "ARPV"
        !stream.writeInt32(variationBasePreviewValid_ ? 1 : 0)) {
        return kResultFalse;
    }

    for (const auto& step : variationBasePreviewSteps_) {
        if (!stream.writeInt32(step.enabled ? 1 : 0) ||
            !stream.writeInt32(step.locked ? 1 : 0) ||
            !stream.writeInt32(static_cast<int32>(step.noteOffset)) ||
            !stream.writeDouble(step.velocity) ||
            !stream.writeDouble(step.gate) ||
            !stream.writeInt32(static_cast<int32>(step.ratchet)) ||
            !stream.writeDouble(step.probability) ||
            !stream.writeInt32(static_cast<int32>(step.octaveOffset))) {
            return kResultFalse;
        }
    }
    return kResultOk;
}

IPlugView* PLUGIN_API Controller::createView(FIDString name) {
    if (!name || std::strcmp(name, ViewType::kEditor) != 0)
        return nullptr;
    auto* editor = new VSTGUI::VST3Editor(this, "view", "Arporator.uidesc");
    gui::configureEditor(editor, 1180.0, 680.0, guiZoom_);
    editor_ = editor;
    return editor;
}

VSTGUI::CView* Controller::createCustomView(
    VSTGUI::UTF8StringPtr name,
    const VSTGUI::UIAttributes& attributes,
    const VSTGUI::IUIDescription*,
    VSTGUI::VST3Editor* editor) {
    editor_ = editor;
    return gui::createCustomView(name, attributes, editor, this);
}

VSTGUI::CView* Controller::verifyView(
    VSTGUI::CView* view,
    const VSTGUI::UIAttributes&,
    const VSTGUI::IUIDescription*,
    VSTGUI::VST3Editor* editor) {
    editor_ = editor;
    return view;
}

void Controller::valueChanged(VSTGUI::CControl* control) {
    if (!control)
        return;
    const auto tag = static_cast<ParamID>(control->getTag());
    editParameter(tag, control->getValueNormalized());
}

void Controller::willClose(VSTGUI::VST3Editor* editor) {
    if (editor_ == editor)
        editor_ = nullptr;
}

void Controller::setGuiZoom(double zoom) {
    guiZoom_ = zoom >= 1.35 ? 1.5 : (zoom >= 1.10 ? 1.2 : 1.0);
    if (editor_ && std::abs(editor_->getZoomFactor() - guiZoom_) > 1.0e-9)
        editor_->setZoomFactor(guiZoom_);
}

void Controller::setSelectedStep(int step) noexcept {
    selectedStep_ = std::clamp(step, 0, static_cast<int>(kStepParamCount - 1));
    if (editor_ && editor_->getFrame())
        editor_->getFrame()->invalid();
}

void Controller::editParameter(ParamID id, double normalized) {
    normalized = std::clamp(normalized, 0.0, 1.0);

    // Capture the visible stored pattern before the first Variate in a
    // variation session. This mirrors the processor's variation base and
    // gives RESET a synchronous GUI source even while transport is stopped.
    if (id == kVariateTriggerId && !variationBasePreviewValid_) {
        for (int i = 0; i < kStepParamCount; ++i) {
            auto& step = variationBasePreviewSteps_[static_cast<std::size_t>(i)];
            step.enabled = getParamNormalized(kStepEnableBase + i) >= 0.5;
            step.velocity = static_cast<float>(
                getParamNormalized(kStepVelocityBase + i));
            step.gate = static_cast<float>(
                0.01 + getParamNormalized(kStepGateBase + i) * 0.99);
            step.ratchet = static_cast<std::uint8_t>(
                1 + normIndex(getParamNormalized(kStepRatchetBase + i), 3));
            step.probability = static_cast<float>(
                getParamNormalized(kStepProbabilityBase + i));
            step.noteOffset = static_cast<std::int8_t>(
                normIndex(getParamNormalized(kStepNoteBase + i), 8) - 4);
            step.octaveOffset = static_cast<std::int8_t>(
                normIndex(getParamNormalized(kStepOctaveBase + i), 4) - 2);
            step.locked = getParamNormalized(kStepLockBase + i) >= 0.5;
        }
        variationBasePreviewValid_ = true;
    }

    // RESET must be authoritative at host level as well. Merely calling
    // setParamNormalized() changes the controller cache but does not tell the
    // host that automation/state values changed, so a host can immediately
    // restore the stale varied values. Push every restored step parameter
    // through the full VST3 edit gesture.
    if (id == kVariateResetId && variationBasePreviewValid_) {
        const auto push = [&](ParamID pid, double value) {
            value = std::clamp(value, 0.0, 1.0);
            setParamNormalized(pid, value);
            beginEdit(pid);
            performEdit(pid, value);
            endEdit(pid);
        };

        for (int i = 0; i < kStepParamCount; ++i) {
            const auto& step =
                variationBasePreviewSteps_[static_cast<std::size_t>(i)];
            push(kStepEnableBase + i, step.enabled ? 1.0 : 0.0);
            push(kStepVelocityBase + i, step.velocity);
            push(kStepGateBase + i,
                 (static_cast<double>(step.gate) - 0.01) / 0.99);
            push(kStepRatchetBase + i,
                 static_cast<double>(step.ratchet - 1) / 3.0);
            push(kStepProbabilityBase + i, step.probability);
            push(kStepNoteBase + i,
                 static_cast<double>(step.noteOffset + 4) / 8.0);
            push(kStepOctaveBase + i,
                 static_cast<double>(step.octaveOffset + 2) / 4.0);
            push(kStepLockBase + i, step.locked ? 1.0 : 0.0);
        }
        variationBasePreviewValid_ = false;
    }

    setParamNormalized(id, normalized);
    beginEdit(id);
    performEdit(id, normalized);
    endEdit(id);
    if (editor_ && editor_->getFrame())
        editor_->getFrame()->invalid();
}

} // namespace arporator::vst3

using namespace Steinberg;
using namespace Steinberg::Vst;

BEGIN_FACTORY_DEF(
    "125A",
    "https://github.com/challanger2000/125A-Arporator",
    "")

DEF_CLASS2(
    INLINE_UID_FROM_FUID(arporator::vst3::kProcessorUID),
    PClassInfo::kManyInstances,
    kVstAudioEffectClass,
    "125A Arporator",
    Vst::kDistributable,
    Vst::PlugType::kInstrumentSynth,
    "1.0.0",
    kVstVersionString,
    arporator::vst3::Processor::createInstance)

DEF_CLASS2(
    INLINE_UID_FROM_FUID(arporator::vst3::kControllerUID),
    PClassInfo::kManyInstances,
    kVstComponentControllerClass,
    "125A Arporator Controller",
    0,
    "",
    "1.0.0",
    kVstVersionString,
    arporator::vst3::Controller::createInstance)

END_FACTORY
