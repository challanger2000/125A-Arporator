#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace arporator::vst3 {

static const Steinberg::FUID kProcessorUID(
    0x125A4152, 0x504F5241, 0x544F5200, 0x00000001);
static const Steinberg::FUID kControllerUID(
    0x125A4152, 0x504F5241, 0x544F5200, 0x00000002);

constexpr Steinberg::int32 kStateMagic = 0x41525031; // ARP1
constexpr Steinberg::int32 kStateVersion = 1;

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

constexpr Steinberg::Vst::ParamID kStepEnableBase = 2000;
constexpr Steinberg::Vst::ParamID kStepVelocityBase = 2100;
constexpr Steinberg::Vst::ParamID kStepGateBase = 2200;
constexpr Steinberg::Vst::ParamID kStepRatchetBase = 2300;
constexpr Steinberg::Vst::ParamID kStepProbabilityBase = 2400;
constexpr Steinberg::Vst::ParamID kStepOctaveBase = 2500;
constexpr Steinberg::int32 kStepParamCount = 32;

} // namespace arporator::vst3
