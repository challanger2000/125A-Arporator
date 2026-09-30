#pragma once

#include "arporator_engine.h"

#include <cstdint>

namespace arporator {

struct VariationRequest {
    float amount {0.35f}; // 0..1
    std::uint32_t seed {0x125A7001u};
    VariationLocks locks {};
};

// Pure, deterministic settings mutation. It does not touch global pitch policy,
// key/scale, host timing or transport state. Only active, unlocked steps may
// change.
Settings variateSettings(const Settings& source,
                         const VariationRequest& request) noexcept;

} // namespace arporator
