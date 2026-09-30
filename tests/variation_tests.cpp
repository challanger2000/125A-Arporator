#include "variation.h"

#include <cstdlib>
#include <iostream>

using namespace arporator;

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed: " #x << " @ line " << __LINE__ << "\n"; \
    return EXIT_FAILURE; } } while (0)

static bool sameStep(const Step& a, const Step& b) {
    return a.enabled == b.enabled &&
           a.locked == b.locked &&
           a.velocity == b.velocity &&
           a.gate == b.gate &&
           a.ratchet == b.ratchet &&
           a.probability == b.probability &&
           a.noteOffset == b.noteOffset &&
           a.octaveOffset == b.octaveOffset;
}

int main() {
    Settings base;
    base.patternLength = 8;
    base.mode = Mode::DownUp;
    base.scalePolicy = ScalePolicy::Scale;
    base.keyRoot = 9;
    base.scaleMask = 0x05ADu;
    base.octaveRange = 3;
    base.stepsPerQuarter = 6.0;
    base.restartOnTrigger = false;
    base.swing = 0.23f;
    base.humanize = 0.17f;
    base.groove = 0.31f;
    base.strum = 0.22f;
    base.globalGate = 0.81f;
    base.randomSeed = 0x12345678u;

    for (int i = 0; i < kMaxSteps; ++i) {
        auto& step = base.steps[static_cast<std::size_t>(i)];
        step.enabled = (i % 3) != 1;
        step.velocity = 0.55f + 0.01f * static_cast<float>(i % 10);
        step.gate = 0.60f + 0.01f * static_cast<float>(i % 8);
        step.ratchet = static_cast<std::uint8_t>(1 + (i % 4));
        step.probability = 0.65f + 0.01f * static_cast<float>(i % 7);
        step.noteOffset = static_cast<std::int8_t>((i % 5) - 2);
        step.octaveOffset = static_cast<std::int8_t>((i % 3) - 1);
    }

    VariationRequest request;
    request.amount = 0.0f;
    request.seed = 0xCAFEBABEu;

    const auto neutral = variateSettings(base, request);
    for (int i = 0; i < kMaxSteps; ++i)
        CHECK(sameStep(neutral.steps[static_cast<std::size_t>(i)],
                       base.steps[static_cast<std::size_t>(i)]));

    // Deterministic for the same input + seed.
    request.amount = 0.8f;
    const auto a = variateSettings(base, request);
    const auto b = variateSettings(base, request);
    for (int i = 0; i < kMaxSteps; ++i)
        CHECK(sameStep(a.steps[static_cast<std::size_t>(i)],
                       b.steps[static_cast<std::size_t>(i)]));

    // Global pitch/timing semantics are invariant under Variate.
    CHECK(a.mode == base.mode);
    CHECK(a.scalePolicy == base.scalePolicy);
    CHECK(a.keyRoot == base.keyRoot);
    CHECK(a.scaleMask == base.scaleMask);
    CHECK(a.patternLength == base.patternLength);
    CHECK(a.octaveRange == base.octaveRange);
    CHECK(a.stepsPerQuarter == base.stepsPerQuarter);
    CHECK(a.restartOnTrigger == base.restartOnTrigger);
    CHECK(a.swing == base.swing);
    CHECK(a.humanize == base.humanize);
    CHECK(a.groove == base.groove);
    CHECK(a.strum == base.strum);
    CHECK(a.globalGate == base.globalGate);
    CHECK(a.randomSeed == base.randomSeed);

    // Steps outside active Pattern Length are untouched.
    for (int i = base.patternLength; i < kMaxSteps; ++i)
        CHECK(sameStep(a.steps[static_cast<std::size_t>(i)],
                       base.steps[static_cast<std::size_t>(i)]));

    // A per-step lock freezes every mutable field.
    Settings lockedBase = base;
    lockedBase.steps[2].locked = true;
    const auto lockedResult = variateSettings(lockedBase, request);
    CHECK(sameStep(lockedResult.steps[2], lockedBase.steps[2]));

    // Dimension locks freeze their respective values across all active steps.
    VariationRequest dimensionLocked = request;
    dimensionLocked.locks.rhythm = true;
    dimensionLocked.locks.velocity = true;
    dimensionLocked.locks.gate = true;
    dimensionLocked.locks.ratchet = true;
    dimensionLocked.locks.probability = true;
    dimensionLocked.locks.octave = true;
    dimensionLocked.locks.note = true;
    const auto allLocked = variateSettings(base, dimensionLocked);
    for (int i = 0; i < base.patternLength; ++i)
        CHECK(sameStep(allLocked.steps[static_cast<std::size_t>(i)],
                       base.steps[static_cast<std::size_t>(i)]));

    // With all dimensions open and a strong amount, something should change.
    bool changed = false;
    for (int i = 0; i < base.patternLength; ++i)
        changed = changed ||
            !sameStep(a.steps[static_cast<std::size_t>(i)],
                      base.steps[static_cast<std::size_t>(i)]);
    CHECK(changed);

    // Variate never returns a completely silent active pattern.
    Settings silentCandidate = base;
    silentCandidate.patternLength = 1;
    silentCandidate.steps[0].enabled = false;
    VariationRequest strong;
    strong.amount = 1.0f;
    strong.seed = 1u;
    const auto protectedPattern = variateSettings(silentCandidate, strong);
    CHECK(protectedPattern.steps[0].enabled);

    std::cout << "ArporatorVariationTests PASS\n";
    return EXIT_SUCCESS;
}
