#include "variation.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace arporator {
namespace {

std::uint32_t nextRandom(std::uint32_t& state) noexcept {
    std::uint32_t x = state ? state : 0x125A7001u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state = x ? x : 0x125A7001u;
    return state;
}

float unit(std::uint32_t& state) noexcept {
    return static_cast<float>(nextRandom(state) & 0x00FFFFFFu) /
           static_cast<float>(0x01000000u);
}

float bipolar(std::uint32_t& state) noexcept {
    return unit(state) * 2.0f - 1.0f;
}

bool chance(std::uint32_t& state, float probability) noexcept {
    return unit(state) < std::clamp(probability, 0.0f, 1.0f);
}

} // namespace

Settings variateSettings(const Settings& source,
                         const VariationRequest& request) noexcept {
    Settings result = source;
    const float amount = std::clamp(request.amount, 0.0f, 1.0f);
    if (amount <= 0.0f)
        return result;

    std::uint32_t rng = request.seed ? request.seed : 0x125A7001u;
    const int activeLength = std::clamp(source.patternLength, 1, kMaxSteps);

    for (int i = 0; i < activeLength; ++i) {
        auto& step = result.steps[static_cast<std::size_t>(i)];
        const auto& original = source.steps[static_cast<std::size_t>(i)];

        // Consume a stable amount of random state for each active step so one
        // lock does not unpredictably re-seed all later steps.
        const float rhythmRoll = unit(rng);
        const float velocityDelta = bipolar(rng);
        const float gateDelta = bipolar(rng);
        const float ratchetRoll = unit(rng);
        const float ratchetDirection = bipolar(rng);
        const float probabilityDelta = bipolar(rng);
        const float octaveRoll = unit(rng);
        const float octaveDirection = bipolar(rng);

        if (original.locked)
            continue;

        if (!request.locks.rhythm &&
            rhythmRoll < 0.30f * amount) {
            step.enabled = !step.enabled;
        }

        if (!request.locks.velocity) {
            step.velocity = std::clamp(
                original.velocity + velocityDelta * 0.28f * amount,
                0.10f,
                1.0f);
        }

        if (!request.locks.gate) {
            step.gate = std::clamp(
                original.gate + gateDelta * 0.35f * amount,
                0.10f,
                1.0f);
        }

        if (!request.locks.ratchet &&
            ratchetRoll < 0.35f * amount) {
            const int direction = ratchetDirection >= 0.0f ? 1 : -1;
            step.ratchet = static_cast<std::uint8_t>(std::clamp(
                static_cast<int>(original.ratchet) + direction,
                1,
                4));
        }

        if (!request.locks.probability) {
            step.probability = std::clamp(
                original.probability +
                    probabilityDelta * 0.35f * amount,
                0.10f,
                1.0f);
        }

        if (!request.locks.octave &&
            octaveRoll < 0.30f * amount) {
            const int direction = octaveDirection >= 0.0f ? 1 : -1;
            step.octaveOffset = static_cast<std::int8_t>(std::clamp(
                static_cast<int>(original.octaveOffset) + direction,
                -2,
                2));
        }
    }

    // Never return a silent active pattern purely because Variate toggled every
    // active step off. Prefer the first unlocked step, otherwise preserve step 1.
    bool anyEnabled = false;
    for (int i = 0; i < activeLength; ++i)
        anyEnabled = anyEnabled || result.steps[static_cast<std::size_t>(i)].enabled;

    if (!anyEnabled) {
        int restore = 0;
        for (int i = 0; i < activeLength; ++i) {
            if (!source.steps[static_cast<std::size_t>(i)].locked) {
                restore = i;
                break;
            }
        }
        result.steps[static_cast<std::size_t>(restore)].enabled = true;
    }

    // Variation must never silently alter these global semantics.
    result.mode = source.mode;
    result.scalePolicy = source.scalePolicy;
    result.keyRoot = source.keyRoot;
    result.scaleMask = source.scaleMask;
    result.patternLength = source.patternLength;
    result.octaveRange = source.octaveRange;
    result.stepsPerQuarter = source.stepsPerQuarter;
    result.restartOnTrigger = source.restartOnTrigger;
    result.swing = source.swing;
    result.humanize = source.humanize;
    result.groove = source.groove;
    result.strum = source.strum;
    result.globalGate = source.globalGate;
    result.randomSeed = source.randomSeed;

    return result;
}

} // namespace arporator
