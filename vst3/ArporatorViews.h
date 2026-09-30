#pragma once

#include "vstgui/lib/cview.h"
#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/controls/cknob.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "vstgui/uidescription/uiattributes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace arporator::vst3 {
class Controller;
}

namespace arporator::vst3::gui {

class FaceplateView final : public VSTGUI::CView {
public:
    explicit FaceplateView(const VSTGUI::CRect& size);
    void draw(VSTGUI::CDrawContext* context) override;
};

class LogoView final : public VSTGUI::CView {
public:
    explicit LogoView(const VSTGUI::CRect& size);
    void draw(VSTGUI::CDrawContext* context) override;
};

class KnobView final : public VSTGUI::CKnob {
public:
    KnobView(const VSTGUI::CRect& size,
             VSTGUI::IControlListener* listener,
             std::int32_t tag);
    void draw(VSTGUI::CDrawContext* context) override;
    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override;
};

class PopupSelectorView final : public VSTGUI::COptionMenu {
public:
    PopupSelectorView(const VSTGUI::CRect& size,
                      VSTGUI::IControlListener* listener,
                      std::int32_t tag,
                      const std::vector<std::string>& labels);
    PopupSelectorView(const PopupSelectorView& other);
    VSTGUI::CBaseObject* newCopy() const override { return new PopupSelectorView(*this); }
    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override;
};

class ToggleView final : public VSTGUI::CControl {
public:
    ToggleView(const VSTGUI::CRect& size,
               VSTGUI::IControlListener* listener,
               std::int32_t tag,
               std::string offText,
               std::string onText);
    ToggleView(const ToggleView& other);
    VSTGUI::CBaseObject* newCopy() const override { return new ToggleView(*this); }
    void draw(VSTGUI::CDrawContext* context) override;
    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override;
private:
    std::string offText_;
    std::string onText_;
};

class ActionButton final : public VSTGUI::CControl {
public:
    ActionButton(const VSTGUI::CRect& size,
                 VSTGUI::IControlListener* listener,
                 std::int32_t tag,
                 std::string text);
    ActionButton(const ActionButton& other);
    VSTGUI::CBaseObject* newCopy() const override { return new ActionButton(*this); }
    void draw(VSTGUI::CDrawContext* context) override;
    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override;
private:
    std::string text_;
};

class StepGridView final : public VSTGUI::CView {
public:
    StepGridView(const VSTGUI::CRect& size, Controller* controller);
    void draw(VSTGUI::CDrawContext* context) override;
    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override;
    VSTGUI::CMouseEventResult onMouseMoved(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override;
    VSTGUI::CMouseEventResult onMouseUp(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override;
private:
    int stepAt(const VSTGUI::CPoint& where) const noexcept;
    void showStepMenu(int step, const VSTGUI::CPoint& where);
    void showRatchetMenu(int step, const VSTGUI::CPoint& where);
    void showProbabilityMenu(int step, const VSTGUI::CPoint& where);
    Controller* controller_ {nullptr};
    int dragStep_ {-1};
    VSTGUI::CPoint dragStart_ {};
    double dragStartVelocity_ {1.0};
    double dragStartGate_ {1.0};
    enum class DragAxis { None, Velocity, Gate };
    DragAxis dragAxis_ {DragAxis::None};
    bool dragged_ {false};
};

class UIScaleView final : public VSTGUI::CView {
public:
    UIScaleView(const VSTGUI::CRect& size,
                VSTGUI::VST3Editor* editor,
                Controller* controller);
    void draw(VSTGUI::CDrawContext* context) override;
    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override;
private:
    VSTGUI::VST3Editor* editor_ {nullptr};
    Controller* controller_ {nullptr};
};

void configureEditor(VSTGUI::VST3Editor* editor,
                     double width,
                     double height,
                     double zoom);

VSTGUI::CView* createCustomView(VSTGUI::UTF8StringPtr name,
                                const VSTGUI::UIAttributes& attributes,
                                VSTGUI::VST3Editor* editor,
                                Controller* controller);

} // namespace arporator::vst3::gui
