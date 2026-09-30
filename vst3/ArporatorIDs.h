#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace arporator::vst3 {

static const Steinberg::FUID kProcessorUID(
    0x125A4152, 0x504F5241, 0x544F5200, 0x00000001);
static const Steinberg::FUID kControllerUID(
    0x125A4152, 0x504F5241, 0x544F5200, 0x00000002);

constexpr Steinberg::int32 kStateMagic = 0x41525031; // ARP1
constexpr Steinberg::int32 kStateVersion = 4;

constexpr Steinberg::Vst::ParamID kModeId = 1000;
constexpr Steinberg::Vst::ParamID kRateId = 1001;
constexpr Steinberg::Vst::ParamID kOctavesId = 1002;
constexpr Steinberg::Vst::ParamID kPatternLengthId = 1003;
constexpr Steinberg::Vst::ParamID kGateId = 1004;
constexpr Steinberg::Vst::ParamID kSwingId = 1005;
constexpr Steinberg::Vst::ParamID kTriggerModeId = 1006;
constexpr Steinberg::Vst::ParamID kScalePolicyId = 1007;
constexpr Steinberg::Vst::ParamID kKeyRootId = 1008;
constexpr Steinberg::Vst::ParamID kScaleModeId = 1009;
constexpr Steinberg::Vst::ParamID kHumanizeId = 1010;
constexpr Steinberg::Vst::ParamID kGrooveId = 1011;
constexpr Steinberg::Vst::ParamID kStrumId = 1012;
constexpr Steinberg::Vst::ParamID kVariationAmountId = 1013;
constexpr Steinberg::Vst::ParamID kLockRhythmId = 1014;
constexpr Steinberg::Vst::ParamID kLockVelocityId = 1015;
constexpr Steinberg::Vst::ParamID kLockGateId = 1016;
constexpr Steinberg::Vst::ParamID kLockRatchetId = 1017;
constexpr Steinberg::Vst::ParamID kLockProbabilityId = 1018;
constexpr Steinberg::Vst::ParamID kLockOctaveId = 1019;
constexpr Steinberg::Vst::ParamID kVariateTriggerId = 1020;
constexpr Steinberg::Vst::ParamID kEvolveId = 1021;

constexpr Steinberg::Vst::ParamID kStepEnableBase = 2000;
constexpr Steinberg::Vst::ParamID kStepVelocityBase = 2100;
constexpr Steinberg::Vst::ParamID kStepGateBase = 2200;
constexpr Steinberg::Vst::ParamID kStepRatchetBase = 2300;
constexpr Steinberg::Vst::ParamID kStepProbabilityBase = 2400;
constexpr Steinberg::Vst::ParamID kStepOctaveBase = 2500;
constexpr Steinberg::Vst::ParamID kStepLockBase = 2600;
constexpr Steinberg::int32 kStepParamCount = 32;

} // namespace arporator::vst3
