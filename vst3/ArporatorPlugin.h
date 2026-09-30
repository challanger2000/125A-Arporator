#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "vstgui/lib/controls/icontrollistener.h"

#include "AtomicSnapshot.h"
#include "../source/arporator_engine.h"
#include "../source/variation.h"

#include <atomic>
#include <cstdint>
#include <vector>

namespace arporator::vst3 {

struct RuntimeState {
    Settings settings {};
    Steinberg::int32 rateIndex {2};   // 1/16
    Steinberg::int32 scaleMode {0};   // Major
    float variationAmount {0.35f};
    VariationLocks variationLocks {};
    std::uint32_t variationCounter {0u};
};

class Processor final : public Steinberg::Vst::AudioEffect {
public:
    Processor();

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new Processor());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API setupProcessing(
        Steinberg::Vst::ProcessSetup& setup) override;
    Steinberg::uint32 PLUGIN_API getProcessContextRequirements() override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API setProcessing(Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API process(
        Steinberg::Vst::ProcessData& data) override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;

private:
    void readParameterChanges(
        Steinberg::Vst::IParameterChanges* changes) noexcept;
    void applyNormalizedParameter(
        Steinberg::Vst::ParamID id,
        double value) noexcept;
    void applyRuntimeState(const RuntimeState& state) noexcept;
    void consumePendingState() noexcept;
    void publishState() noexcept;

    bool readState(Steinberg::IBStream* stream, RuntimeState& state) const noexcept;
    bool writeState(Steinberg::IBStream* stream, const RuntimeState& state) const noexcept;

    static double stepsPerQuarterForRate(int index) noexcept;
    static std::uint16_t scaleMaskForMode(int index) noexcept;

    RuntimeState state_ {};
    Engine engine_ {};

    AtomicSnapshot<RuntimeState> pendingState_ {};
    AtomicSnapshot<RuntimeState> publishedState_ {};
    std::atomic<std::uint64_t> appliedPendingSequence_ {0u};

    std::vector<MidiInput> inputBuffer_ {};
    std::vector<MidiOutput> outputBuffer_ {};
    std::vector<Steinberg::Vst::Event> passthroughBuffer_ {};
    std::vector<Steinberg::Vst::Event> vstOutputBuffer_ {};

    double sampleRate_ {48000.0};
    bool settingsDirty_ {true};
    bool variateParametersDirty_ {false};
    double variateTrigger_ {0.0};
    bool hadTransportState_ {false};
    bool wasPlaying_ {false};
    int lastPlayheadPublished_ {-1};
};

class Controller final :
    public Steinberg::Vst::EditControllerEx1,
    public VSTGUI::VST3EditorDelegate,
    public VSTGUI::IControlListener {
public:
    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IEditController*>(new Controller());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API setComponentState(
        Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API setParamNormalized(
        Steinberg::Vst::ParamID tag,
        Steinberg::Vst::ParamValue value) override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) override;

    VSTGUI::CView* createCustomView(
        VSTGUI::UTF8StringPtr name,
        const VSTGUI::UIAttributes& attributes,
        const VSTGUI::IUIDescription* description,
        VSTGUI::VST3Editor* editor) override;
    VSTGUI::CView* verifyView(
        VSTGUI::CView* view,
        const VSTGUI::UIAttributes& attributes,
        const VSTGUI::IUIDescription* description,
        VSTGUI::VST3Editor* editor) override;
    void valueChanged(VSTGUI::CControl* control) override;
    void willClose(VSTGUI::VST3Editor* editor) override;

    void setGuiZoom(double zoom);
    int selectedStep() const noexcept { return selectedStep_; }
    void setSelectedStep(int step) noexcept;
    void editParameter(Steinberg::Vst::ParamID id, double normalized);

private:
    static std::uint16_t scaleMaskForMode(int index) noexcept;

    double guiZoom_ {1.0};
    int selectedStep_ {0};
    VSTGUI::VST3Editor* editor_ {nullptr};
};

} // namespace arporator::vst3
