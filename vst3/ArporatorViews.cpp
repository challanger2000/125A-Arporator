#include "ArporatorViews.h"
#include "ArporatorPlugin.h"
#include "ArporatorIDs.h"
#include "branding_master.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace arporator::vst3::gui {
namespace {

constexpr VSTGUI::CColor kBg {7,10,14,255};
constexpr VSTGUI::CColor kPanel {17,22,29,255};
constexpr VSTGUI::CColor kPanel2 {10,14,19,255};
constexpr VSTGUI::CColor kBorder {61,74,88,220};
constexpr VSTGUI::CColor kText {239,243,247,255};
constexpr VSTGUI::CColor kMuted {143,155,168,255};
constexpr VSTGUI::CColor kAccent {86,154,220,255};
constexpr VSTGUI::CColor kRed {215,25,32,255};
constexpr VSTGUI::CColor kSilver {217,217,217,255};
constexpr double kPi = 3.14159265358979323846;

bool ctrlReset(VSTGUI::CControl* control, const VSTGUI::CButtonState& buttons) {
    if (!control || !buttons.isLeftButton() || !buttons.isControlSet())
        return false;
    control->beginEdit();
    control->setValue(control->getDefaultValue());
    control->valueChanged();
    control->endEdit();
    control->invalid();
    return true;
}

struct LogoSubpath { std::vector<VSTGUI::CPoint> points; };
struct LogoPath { std::vector<LogoSubpath> subpaths; bool red {false}; };

std::vector<LogoPath> parseLogo() {
    std::vector<LogoPath> result;
    result.reserve(branding::kMasterPathCount);
    for (const auto& src : branding::kMasterPaths) {
        LogoPath path;
        path.red = src.red;
        std::string_view d{src.d};
        const char* p=d.data();
        const char* end=d.data()+d.size();
        char command=0;
        LogoSubpath* current=nullptr;
        while (p<end) {
            while (p<end && (std::isspace(static_cast<unsigned char>(*p)) || *p==',')) ++p;
            if (p>=end) break;
            if (std::isalpha(static_cast<unsigned char>(*p))) {
                command=*p++;
                if (command=='Z' || command=='z') { current=nullptr; command=0; continue; }
            }
            if (command!='M' && command!='L' && command!='m' && command!='l') { ++p; continue; }
            char* next=nullptr;
            double x=std::strtod(p,&next); if (next==p) break; p=next;
            while (p<end && (std::isspace(static_cast<unsigned char>(*p)) || *p==',')) ++p;
            double y=std::strtod(p,&next); if (next==p) break; p=next;
            if (command=='M' || command=='m') {
                path.subpaths.emplace_back();
                current=&path.subpaths.back();
                current->points.emplace_back(x,y);
                command=(command=='M')?'L':'l';
            } else if (current) {
                current->points.emplace_back(x,y);
            }
        }
        if (!path.subpaths.empty()) result.emplace_back(std::move(path));
    }
    return result;
}

void panel(VSTGUI::CDrawContext* c, const VSTGUI::CRect& r, bool darker=false) {
    c->setFillColor(darker ? kPanel2 : kPanel);
    c->setFrameColor(kBorder);
    c->setLineWidth(1.0);
    c->drawRect(r, VSTGUI::kDrawFilledAndStroked);
}

} // namespace

FaceplateView::FaceplateView(const VSTGUI::CRect& size) : CView(size) {
    setMouseEnabled(false);
}

void FaceplateView::draw(VSTGUI::CDrawContext* c) {
    const auto r=getViewSize();
    c->setDrawMode(VSTGUI::kAntiAliasing);
    c->setFillColor(kBg);
    c->drawRect(r,VSTGUI::kDrawFilled);

    const auto box=[&](double x,double y,double w,double h,bool dark=false){
        panel(c,{r.left+x,r.top+y,r.left+x+w,r.top+y+h},dark);
    };

    box(10,10,1160,660);
    box(22,20,1136,54);
    box(22,88,1136,116);
    box(22,216,1136,354,true);

    c->setFrameColor({86,154,220,70});
    c->setLineWidth(1.0);
    c->drawLine({r.left+34,r.top+210},{r.left+1146,r.top+210});
    c->drawLine({r.left+34,r.top+450},{r.left+1146,r.top+450});

    setDirty(false);
}

LogoView::LogoView(const VSTGUI::CRect& size) : CView(size) {
    setMouseEnabled(false);
}

void LogoView::draw(VSTGUI::CDrawContext* c) {
    static const auto logo=parseLogo();
    const auto r=getViewSize();
    constexpr double mw=1774.0, mh=887.0;
    const double scale=std::min(r.getWidth()/mw,r.getHeight()/mh);
    const double ox=r.left+(r.getWidth()-mw*scale)*0.5;
    const double oy=r.top +(r.getHeight()-mh*scale)*0.5;
    c->setDrawMode(VSTGUI::kAntiAliasing);
    for (const auto& lp:logo) {
        auto* gp=c->createGraphicsPath();
        if(!gp) continue;
        for (const auto& sp:lp.subpaths) {
            if(sp.points.empty()) continue;
            auto tr=[&](const VSTGUI::CPoint& q){return VSTGUI::CPoint{ox+q.x*scale,oy+q.y*scale};};
            gp->beginSubpath(tr(sp.points.front()));
            for(std::size_t i=1;i<sp.points.size();++i) gp->addLine(tr(sp.points[i]));
            gp->closeSubpath();
        }
        c->setFillColor(lp.red?kRed:kSilver);
        c->drawGraphicsPath(gp,VSTGUI::CDrawContext::kPathFilledEvenOdd);
        gp->forget();
    }
    setDirty(false);
}

KnobView::KnobView(const VSTGUI::CRect& size,
                   VSTGUI::IControlListener* listener,
                   std::int32_t tag)
: VSTGUI::CKnob(size,listener,tag,nullptr,nullptr) {
    setStartAngle(static_cast<float>(135.0/180.0*kPi));
    setRangeAngle(static_cast<float>(270.0/180.0*kPi));
    setWantsFocus(true);
    setTransparency(true);
}

void KnobView::draw(VSTGUI::CDrawContext* c) {
    const auto r=getViewSize();
    const auto center=r.getCenter();
    const double radius=std::min(r.getWidth(),r.getHeight())*0.34;
    const double n=std::clamp(static_cast<double>(getValueNormalized()),0.0,1.0);
    const double angle=(135.0+n*270.0)*kPi/180.0;

    c->setDrawMode(VSTGUI::kAntiAliasing);
    c->setFillColor({0,0,0,105});
    c->drawEllipse({center.x-radius-5,center.y-radius-2,center.x+radius+5,center.y+radius+8},VSTGUI::kDrawFilled);
    c->setFillColor({58,65,74,255});
    c->setFrameColor({104,114,126,220});
    c->drawEllipse({center.x-radius-3,center.y-radius-3,center.x+radius+3,center.y+radius+3},VSTGUI::kDrawFilledAndStroked);
    const double cap=radius*0.72;
    c->setFillColor({15,20,27,255});
    c->setFrameColor({5,7,10,255});
    c->drawEllipse({center.x-cap,center.y-cap,center.x+cap,center.y+cap},VSTGUI::kDrawFilledAndStroked);
    c->setFrameColor(kAccent);
    c->setLineWidth(2.0);
    c->drawArc({center.x-radius-2,center.y-radius-2,center.x+radius+2,center.y+radius+2},
               135.f,static_cast<float>(135.0+n*270.0));
    c->setFrameColor(kText);
    c->setLineWidth(2.0);
    c->drawLine({center.x+std::cos(angle)*cap*0.15,center.y+std::sin(angle)*cap*0.15},
                {center.x+std::cos(angle)*cap*0.82,center.y+std::sin(angle)*cap*0.82});
    setDirty(false);
}

VSTGUI::CMouseEventResult KnobView::onMouseDown(
    VSTGUI::CPoint& where,const VSTGUI::CButtonState& buttons) {
    if (ctrlReset(this,buttons))
        return VSTGUI::kMouseDownEventHandledButDontNeedMovedOrUpEvents;
    return VSTGUI::CKnob::onMouseDown(where,buttons);
}

PopupSelectorView::PopupSelectorView(
    const VSTGUI::CRect& size,
    VSTGUI::IControlListener* listener,
    std::int32_t tag,
    const std::vector<std::string>& labels)
: VSTGUI::COptionMenu(
      size,
      listener,
      tag,
      nullptr,
      nullptr,
      VSTGUI::COptionMenu::kPopupStyle) {
    setTransparency(true);
    setBackColor(kPanel2);
    setFrameColor(kBorder);
    setFont(VSTGUI::kNormalFontSmall);
    setFontColor(kText);
    setTextInset({5.0, 0.0});
    setHoriAlign(VSTGUI::kCenterText);

    for (const auto& label : labels)
        addEntry(label.c_str());

    if (getNbEntries() > 0)
        setCurrent(0);
}

PopupSelectorView::PopupSelectorView(const PopupSelectorView& other)
: VSTGUI::COptionMenu(other) {}

VSTGUI::CMouseEventResult PopupSelectorView::onMouseDown(
    VSTGUI::CPoint& where,
    const VSTGUI::CButtonState& buttons) {
    if (!getViewSize().pointInside(where))
        return VSTGUI::kMouseEventNotHandled;

    if (ctrlReset(this, buttons))
        return VSTGUI::kMouseDownEventHandledButDontNeedMovedOrUpEvents;

    if (!buttons.isLeftButton())
        return VSTGUI::kMouseEventNotHandled;

    return VSTGUI::COptionMenu::onMouseDown(where, buttons);
}

ToggleView::ToggleView(const VSTGUI::CRect& size,
                       VSTGUI::IControlListener* listener,
                       std::int32_t tag,
                       std::string offText,
                       std::string onText)
: VSTGUI::CControl(size,listener,tag),
  offText_(std::move(offText)), onText_(std::move(onText)) {
    setTransparency(true);
}
ToggleView::ToggleView(const ToggleView& o)
: VSTGUI::CControl(o),offText_(o.offText_),onText_(o.onText_) {}

void ToggleView::draw(VSTGUI::CDrawContext* c) {
    const auto r=getViewSize();
    const bool on=getValueNormalized()>=0.5f;
    c->setFillColor(on?VSTGUI::CColor{39,89,128,255}:kPanel2);
    c->setFrameColor(on?VSTGUI::CColor{113,186,239,255}:kBorder);
    c->drawRect(r,VSTGUI::kDrawFilledAndStroked);
    c->setFont(VSTGUI::kNormalFontSmall);
    c->setFontColor(kText);
    c->drawString((on?onText_:offText_).c_str(),r,VSTGUI::kCenterText);
    setDirty(false);
}

VSTGUI::CMouseEventResult ToggleView::onMouseDown(
    VSTGUI::CPoint& where,const VSTGUI::CButtonState& buttons) {
    if (!getViewSize().pointInside(where)) return VSTGUI::kMouseEventNotHandled;
    if (ctrlReset(this,buttons))
        return VSTGUI::kMouseDownEventHandledButDontNeedMovedOrUpEvents;
    if (!buttons.isLeftButton()) return VSTGUI::kMouseEventNotHandled;
    beginEdit();
    setValueNormalized(getValueNormalized()>=0.5f?0.0f:1.0f);
    valueChanged();
    endEdit();
    invalid();
    return VSTGUI::kMouseDownEventHandledButDontNeedMovedOrUpEvents;
}

ActionButton::ActionButton(const VSTGUI::CRect& size,
                           VSTGUI::IControlListener* listener,
                           std::int32_t tag,
                           std::string text)
: VSTGUI::CControl(size,listener,tag),text_(std::move(text)) {
    setTransparency(true);
}
ActionButton::ActionButton(const ActionButton& o)
: VSTGUI::CControl(o),text_(o.text_) {}

void ActionButton::draw(VSTGUI::CDrawContext* c) {
    const auto r=getViewSize();
    c->setFillColor({34,77,108,255});
    c->setFrameColor({113,186,239,255});
    c->setLineWidth(1.4);
    c->drawRect(r,VSTGUI::kDrawFilledAndStroked);
    c->setFont(VSTGUI::kNormalFont,9.0,VSTGUI::kBoldFace);
    c->setFontColor(kText);
    c->drawString(text_.c_str(),r,VSTGUI::kCenterText);
    setDirty(false);
}

VSTGUI::CMouseEventResult ActionButton::onMouseDown(
    VSTGUI::CPoint& where,const VSTGUI::CButtonState& buttons) {
    if (!getViewSize().pointInside(where) || !buttons.isLeftButton())
        return VSTGUI::kMouseEventNotHandled;
    beginEdit();
    setValueNormalized(getValueNormalized() >= 0.5f ? 0.0f : 1.0f);
    valueChanged();
    endEdit();
    invalid();
    return VSTGUI::kMouseDownEventHandledButDontNeedMovedOrUpEvents;
}

StepGridView::StepGridView(const VSTGUI::CRect& size, Controller* controller)
: VSTGUI::CView(size),controller_(controller) {
    setTransparency(true);
    setMouseEnabled(true);
}

void StepGridView::draw(VSTGUI::CDrawContext* c) {
    if (!controller_) { setDirty(false); return; }
    const auto r=getViewSize();
    const double gap=5.0;
    const double cellW=(r.getWidth()-15.0*gap)/16.0;
    const double cellH=(r.getHeight()-gap)/2.0;
    const int length=1+static_cast<int>(std::lround(
        controller_->getParamNormalized(kPatternLengthId)*31.0));
    const double playheadNormalized =
        controller_->getParamNormalized(kPlayheadId);
    const int playheadState = std::clamp(
        static_cast<int>(std::lround(
            playheadNormalized * static_cast<double>(kStepParamCount))),
        0,
        static_cast<int>(kStepParamCount));
    const int playheadStep = playheadState > 0 ? playheadState - 1 : -1;

    for(int i=0;i<32;++i) {
        const int row=i/16,col=i%16;
        const double x=r.left+col*(cellW+gap);
        const double y=r.top+row*(cellH+gap);
        VSTGUI::CRect cell{x,y,x+cellW,y+cellH};
        const bool inLength=i<length;
        const bool enabled=controller_->getParamNormalized(kStepEnableBase+i)>=0.5;
        const bool locked=controller_->getParamNormalized(kStepLockBase+i)>=0.5;
        const bool selected=i==controller_->selectedStep();
        const bool playing=i==playheadStep;
        const double velocity=controller_->getParamNormalized(kStepVelocityBase+i);
        const int ratchet=1+static_cast<int>(std::lround(controller_->getParamNormalized(kStepRatchetBase+i)*3.0));
        const double probability=controller_->getParamNormalized(kStepProbabilityBase+i);

        c->setFillColor(!inLength?VSTGUI::CColor{7,10,14,255}:
                        enabled?VSTGUI::CColor{31,57,76,255}:VSTGUI::CColor{16,21,27,255});
        c->setFrameColor(selected?VSTGUI::CColor{123,191,241,255}:
                         locked?VSTGUI::CColor{215,25,32,210}:kBorder);
        c->setLineWidth(selected?2.0:1.0);
        c->drawRect(cell,VSTGUI::kDrawFilledAndStroked);

        // Playback and edit selection are intentionally distinct: the selected
        // step owns the outer border, while the running step gets a bright
        // inner top strip and a subtle inner frame.
        if (playing && inLength) {
            c->setFillColor({132,211,255,255});
            c->drawRect(
                {cell.left+3,cell.top+3,cell.right-3,cell.top+7},
                VSTGUI::kDrawFilled);
            c->setFrameColor({132,211,255,180});
            c->setLineWidth(1.0);
            c->drawRect(
                {cell.left+2,cell.top+2,cell.right-2,cell.bottom-2},
                VSTGUI::kDrawStroked);
        }

        char num[8]{};
        std::snprintf(num,sizeof(num),"%02d",i+1);
        c->setFont(VSTGUI::kNormalFontSmall);
        c->setFontColor(inLength?kText:kMuted);
        c->drawString(num,{cell.left+3,cell.top+2,cell.right-3,cell.top+15},VSTGUI::kLeftText);

        const double velocityH=(cellH-25.0)*std::clamp(velocity,0.0,1.0);
        c->setFillColor({86,154,220,125});
        c->drawRect(
            {cell.left+5,cell.bottom-10-velocityH,cell.left+10,cell.bottom-10},
            VSTGUI::kDrawFilled);

        const double gate=controller_->getParamNormalized(kStepGateBase+i);
        const double gateW=(cellW-16.0)*std::clamp(gate,0.0,1.0);
        c->setFillColor(kAccent);
        c->drawRect(
            {cell.left+13,cell.bottom-7,cell.left+13+gateW,cell.bottom-4},
            VSTGUI::kDrawFilled);

        if (ratchet>1) {
            char rt[8]{};
            std::snprintf(rt,sizeof(rt),"%dx",ratchet);
            c->setFontColor({185,213,235,255});
            c->drawString(rt,{cell.left+4,cell.top+18,cell.right-4,cell.top+31},VSTGUI::kLeftText);
        }
        if (probability<0.999) {
            char pr[8]{};
            std::snprintf(pr,sizeof(pr),"%d%%",static_cast<int>(std::lround(probability*100.0)));
            c->setFontColor({230,188,112,255});
            c->drawString(pr,{cell.left+4,cell.top+18,cell.right-4,cell.top+31},VSTGUI::kRightText);
        }
    }
    setDirty(false);
}

int StepGridView::stepAt(const VSTGUI::CPoint& where) const noexcept {
    const auto r=getViewSize();
    if (!r.pointInside(where))
        return -1;

    const double gap=5.0;
    const double cellW=(r.getWidth()-15.0*gap)/16.0;
    const double cellH=(r.getHeight()-gap)/2.0;
    const int col=std::clamp(static_cast<int>((where.x-r.left)/(cellW+gap)),0,15);
    const int row=std::clamp(static_cast<int>((where.y-r.top)/(cellH+gap)),0,1);
    const double cellLeft=r.left+col*(cellW+gap);
    const double cellTop=r.top+row*(cellH+gap);

    if (where.x>cellLeft+cellW || where.y>cellTop+cellH)
        return -1;
    return row*16+col;
}

void StepGridView::showStepMenu(int step, const VSTGUI::CPoint& where) {
    if (!controller_ || !getFrame() || step<0 || step>=kStepParamCount)
        return;

    auto* menu=new VSTGUI::COptionMenu();
    auto* note=new VSTGUI::COptionMenu();
    auto* ratchet=new VSTGUI::COptionMenu();
    auto* probability=new VSTGUI::COptionMenu();
    auto* octave=new VSTGUI::COptionMenu();

    for (int i=-4;i<=4;++i) {
        char text[24]{};
        std::snprintf(text,sizeof(text),"%+d",i);
        note->addEntry(new VSTGUI::CMenuItem(text,100+(i+4)));
    }
    for (int i=1;i<=4;++i) {
        char text[24]{};
        std::snprintf(text,sizeof(text),"%dx",i);
        ratchet->addEntry(new VSTGUI::CMenuItem(text,200+(i-1)));
    }
    static constexpr int probs[5]={0,25,50,75,100};
    for (int i=0;i<5;++i) {
        char text[24]{};
        std::snprintf(text,sizeof(text),"%d%%",probs[i]);
        probability->addEntry(new VSTGUI::CMenuItem(text,300+i));
    }
    for (int i=-2;i<=2;++i) {
        char text[24]{};
        std::snprintf(text,sizeof(text),"%+d",i);
        octave->addEntry(new VSTGUI::CMenuItem(text,400+(i+2)));
    }

    menu->addEntry(note,"Note");
    menu->addEntry(ratchet,"Ratchet");
    menu->addEntry(probability,"Probability");
    menu->addEntry(octave,"Octave");
    menu->addSeparator();
    menu->addEntry(new VSTGUI::CMenuItem("Toggle Lock",500));
    menu->addEntry(new VSTGUI::CMenuItem("Reset Step",501));

    controller_->setSelectedStep(step);

    menu->popup(getFrame(),where,[this,step](VSTGUI::COptionMenu* selectedMenu){
        if (!controller_ || !selectedMenu)
            return;

        int idx=-1;
        auto* actual=selectedMenu->getLastItemMenu(idx);
        if (!actual || idx<0)
            return;
        auto* item=actual->getEntry(idx);
        if (!item)
            return;

        const int tag=item->getTag();
        const auto edit=[&](Steinberg::Vst::ParamID base,double n){
            controller_->editParameter(
                static_cast<Steinberg::Vst::ParamID>(base+step),
                std::clamp(n,0.0,1.0));
        };

        if (tag>=100 && tag<=108)
            edit(kStepNoteBase,static_cast<double>(tag-100)/8.0);
        else if (tag>=200 && tag<=203)
            edit(kStepRatchetBase,static_cast<double>(tag-200)/3.0);
        else if (tag>=300 && tag<=304) {
            static constexpr double p[5]={0.0,0.25,0.50,0.75,1.0};
            edit(kStepProbabilityBase,p[tag-300]);
        } else if (tag>=400 && tag<=404)
            edit(kStepOctaveBase,static_cast<double>(tag-400)/4.0);
        else if (tag==500) {
            const auto id=static_cast<Steinberg::Vst::ParamID>(kStepLockBase+step);
            edit(kStepLockBase,controller_->getParamNormalized(id)>=0.5?0.0:1.0);
        } else if (tag==501) {
            edit(kStepEnableBase,1.0);
            edit(kStepNoteBase,0.5);
            edit(kStepVelocityBase,1.0);
            edit(kStepGateBase,1.0);
            edit(kStepRatchetBase,0.0);
            edit(kStepProbabilityBase,1.0);
            edit(kStepOctaveBase,0.5);
            edit(kStepLockBase,0.0);
        }

        invalid();
    });
    menu->forget();
}

VSTGUI::CMouseEventResult StepGridView::onMouseDown(
    VSTGUI::CPoint& where,const VSTGUI::CButtonState& buttons) {
    if (!controller_)
        return VSTGUI::kMouseEventNotHandled;

    const int step=stepAt(where);
    if (step<0)
        return VSTGUI::kMouseEventNotHandled;

    const int length=1+static_cast<int>(std::lround(
        controller_->getParamNormalized(kPatternLengthId)*31.0));
    if (step>=length)
        return VSTGUI::kMouseEventNotHandled;

    controller_->setSelectedStep(step);

    if (buttons.isRightButton()) {
        showStepMenu(step,where);
        return VSTGUI::kMouseDownEventHandledButDontNeedMovedOrUpEvents;
    }
    if (!buttons.isLeftButton())
        return VSTGUI::kMouseEventNotHandled;

    dragStep_=step;
    dragStart_=where;
    dragStartVelocity_=controller_->getParamNormalized(
        static_cast<Steinberg::Vst::ParamID>(kStepVelocityBase+step));
    dragStartGate_=controller_->getParamNormalized(
        static_cast<Steinberg::Vst::ParamID>(kStepGateBase+step));
    dragAxis_=DragAxis::None;
    dragged_=false;
    return VSTGUI::kMouseEventHandled;
}

VSTGUI::CMouseEventResult StepGridView::onMouseMoved(
    VSTGUI::CPoint& where,const VSTGUI::CButtonState& buttons) {
    if (!controller_ || dragStep_<0 || !buttons.isLeftButton())
        return VSTGUI::kMouseEventNotHandled;

    const double dx=where.x-dragStart_.x;
    const double dy=where.y-dragStart_.y;
    if (dragAxis_==DragAxis::None) {
        if (std::abs(dx)<3.0 && std::abs(dy)<3.0)
            return VSTGUI::kMouseEventHandled;
        dragAxis_=std::abs(dy)>=std::abs(dx)
            ? DragAxis::Velocity
            : DragAxis::Gate;
        dragged_=true;
    }

    const auto r=getViewSize();
    const double gap=5.0;
    const double cellW=(r.getWidth()-15.0*gap)/16.0;
    const double cellH=(r.getHeight()-gap)/2.0;
    const auto snap=[](double n) {
        n=std::clamp(n,0.0,1.0);
        if (n>=0.95) return 1.0;
        if (n<=0.02) return 0.0;
        return n;
    };

    if (dragAxis_==DragAxis::Velocity) {
        controller_->editParameter(
            static_cast<Steinberg::Vst::ParamID>(kStepVelocityBase+dragStep_),
            snap(dragStartVelocity_-dy/cellH));
    } else {
        controller_->editParameter(
            static_cast<Steinberg::Vst::ParamID>(kStepGateBase+dragStep_),
            snap(dragStartGate_+dx/cellW));
    }

    invalid();
    return VSTGUI::kMouseEventHandled;
}

VSTGUI::CMouseEventResult StepGridView::onMouseUp(
    VSTGUI::CPoint&,const VSTGUI::CButtonState&) {
    if (!controller_ || dragStep_<0)
        return VSTGUI::kMouseEventNotHandled;

    if (!dragged_) {
        const auto id=static_cast<Steinberg::Vst::ParamID>(kStepEnableBase+dragStep_);
        controller_->editParameter(
            id,
            controller_->getParamNormalized(id)>=0.5?0.0:1.0);
    }

    dragStep_=-1;
    dragAxis_=DragAxis::None;
    dragged_=false;
    invalid();
    return VSTGUI::kMouseEventHandled;
}


SelectedStepView::SelectedStepView(const VSTGUI::CRect& size, Controller* controller)
: VSTGUI::CView(size), controller_(controller) {
    setTransparency(true);
    setMouseEnabled(true);
}

void SelectedStepView::draw(VSTGUI::CDrawContext* c) {
    if (!controller_) { setDirty(false); return; }

    const auto r = getViewSize();
    const int step = controller_->selectedStep();
    const auto value = [&](Steinberg::Vst::ParamID base) {
        return std::clamp(
            controller_->getParamNormalized(
                static_cast<Steinberg::Vst::ParamID>(base + step)),
            0.0, 1.0);
    };

    const double on = value(kStepEnableBase);
    const double note = value(kStepNoteBase);
    const double velocity = value(kStepVelocityBase);
    const double gate = value(kStepGateBase);
    const double ratchet = value(kStepRatchetBase);
    const double probability = value(kStepProbabilityBase);
    const double octave = value(kStepOctaveBase);
    const double lock = value(kStepLockBase);

    const int noteValue = static_cast<int>(std::lround(note * 8.0)) - 4;
    const int ratchetValue = 1 + static_cast<int>(std::lround(ratchet * 3.0));
    const int octaveValue = static_cast<int>(std::lround(octave * 4.0)) - 2;

    const double gap = 8.0;
    const double widths[8] = {64,82,96,96,82,104,78,70};
    const double controlTop = r.top + 22.0;
    double x = r.left;

    const auto drawCell = [&](double w,
                              const char* label,
                              const std::string& display,
                              double level,
                              bool active,
                              bool levelBar) {
        VSTGUI::CRect cell{x,controlTop,x+w,r.bottom};
        c->setFillColor(active ? VSTGUI::CColor{27,49,66,255} : kPanel2);
        c->setFrameColor(active ? VSTGUI::CColor{105,175,228,230} : kBorder);
        c->setLineWidth(active ? 1.5 : 1.0);
        c->drawRect(cell,VSTGUI::kDrawFilledAndStroked);

        c->setFont(VSTGUI::kNormalFontSmall);
        c->setFontColor(kMuted);
        c->drawString(label,
                      {cell.left+3,cell.top+4,cell.right-3,cell.top+18},
                      VSTGUI::kCenterText);

        c->setFont(VSTGUI::kNormalFont,10.0,VSTGUI::kBoldFace);
        c->setFontColor(kText);
        c->drawString(display.c_str(),
                      {cell.left+3,cell.top+23,cell.right-3,cell.top+46},
                      VSTGUI::kCenterText);

        if (levelBar) {
            const double normalized = std::clamp(level,0.0,1.0);
            const double bw = (w-12.0)*normalized;
            c->setFillColor({9,13,18,255});
            c->drawRect({cell.left+6,cell.bottom-16,cell.right-6,cell.bottom-9},
                        VSTGUI::kDrawFilled);
            c->setFillColor(kAccent);
            c->drawRect({cell.left+6,cell.bottom-16,cell.left+6+bw,cell.bottom-9},
                        VSTGUI::kDrawFilled);
        }
        x += w + gap;
    };

    char stepLabel[16]{};
    std::snprintf(stepLabel,sizeof(stepLabel),"STEP %02d",step+1);
    c->setFont(VSTGUI::kNormalFont,10.0,VSTGUI::kBoldFace);
    c->setFontColor(kAccent);
    c->drawString(stepLabel,
                  {r.left,r.top+1,r.left+120,r.top+18},
                  VSTGUI::kLeftText);

    drawCell(widths[0],"ON",on>=0.5 ? "ON" : "OFF",on,on>=0.5,false);

    char noteText[16]{};
    std::snprintf(noteText,sizeof(noteText),"%+d",noteValue);
    drawCell(widths[1],"NOTE",noteText,note,true,false);

    char velocityText[16]{};
    std::snprintf(velocityText,sizeof(velocityText),"%d%%",
                  static_cast<int>(std::lround(velocity*100.0)));
    drawCell(widths[2],"VELOCITY",velocityText,velocity,true,true);

    char gateText[16]{};
    std::snprintf(gateText,sizeof(gateText),"%d%%",
                  static_cast<int>(std::lround((0.01+gate*0.99)*100.0)));
    drawCell(widths[3],"GATE",gateText,gate,true,true);

    char ratchetText[16]{};
    std::snprintf(ratchetText,sizeof(ratchetText),"%dx",ratchetValue);
    drawCell(widths[4],"RATCHET",ratchetText,ratchet,true,false);

    char probabilityText[16]{};
    std::snprintf(probabilityText,sizeof(probabilityText),"%d%%",
                  static_cast<int>(std::lround(probability*100.0)));
    drawCell(widths[5],"PROBABILITY",probabilityText,probability,true,true);

    char octaveText[16]{};
    std::snprintf(octaveText,sizeof(octaveText),"%+d",octaveValue);
    drawCell(widths[6],"OCTAVE",octaveText,octave,true,false);

    drawCell(widths[7],"LOCK",lock>=0.5 ? "LOCK" : "OPEN",lock,lock>=0.5,false);

    setDirty(false);
}

VSTGUI::CMouseEventResult SelectedStepView::onMouseDown(
    VSTGUI::CPoint& where,
    const VSTGUI::CButtonState& buttons) {
    if (!controller_ || !buttons.isLeftButton() ||
        !getViewSize().pointInside(where))
        return VSTGUI::kMouseEventNotHandled;

    const auto r = getViewSize();
    const int step = controller_->selectedStep();
    const double gap = 8.0;
    const double widths[8] = {64,82,96,96,82,104,78,70};

    int zone = -1;
    double zoneLeft = r.left;
    for (int i=0;i<8;++i) {
        if (where.x >= zoneLeft && where.x <= zoneLeft+widths[i]) {
            zone=i;
            break;
        }
        zoneLeft += widths[i]+gap;
    }
    if (zone < 0)
        return VSTGUI::kMouseEventNotHandled;

    const bool reset = buttons.isControlSet();

    const auto continuousValue = [](double x, double left, double width) {
        double n = std::clamp((x - left) / width, 0.0, 1.0);
        if (n >= 0.90)
            return 1.0;
        if (n <= 0.02)
            return 0.0;
        return n;
    };

    auto edit = [&](Steinberg::Vst::ParamID base,double v) {
        controller_->editParameter(
            static_cast<Steinberg::Vst::ParamID>(base+step),
            std::clamp(v,0.0,1.0));
    };
    auto current = [&](Steinberg::Vst::ParamID base) {
        return controller_->getParamNormalized(
            static_cast<Steinberg::Vst::ParamID>(base+step));
    };

    switch(zone) {
        case 0:
            edit(kStepEnableBase, reset ? 1.0 : (current(kStepEnableBase)>=0.5?0.0:1.0));
            break;
        case 1: {
            if (reset) {
                edit(kStepNoteBase,0.5);
            } else {
                int index=std::clamp(static_cast<int>(std::lround(current(kStepNoteBase)*8.0)),0,8);
                index += where.x < zoneLeft+widths[zone]*0.5 ? -1 : 1;
                index=std::clamp(index,0,8);
                edit(kStepNoteBase,static_cast<double>(index)/8.0);
            }
            break;
        }
        case 2:
            edit(kStepVelocityBase, reset ? 1.0 :
                 continuousValue(where.x, zoneLeft, widths[zone]));
            break;
        case 3:
            edit(kStepGateBase, reset ? 1.0 :
                 continuousValue(where.x, zoneLeft, widths[zone]));
            break;
        case 4: {
            if (reset) {
                edit(kStepRatchetBase,0.0);
            } else {
                int index=std::clamp(static_cast<int>(std::lround(current(kStepRatchetBase)*3.0)),0,3);
                index += where.x < zoneLeft+widths[zone]*0.5 ? -1 : 1;
                index=std::clamp(index,0,3);
                edit(kStepRatchetBase,static_cast<double>(index)/3.0);
            }
            break;
        }
        case 5:
            edit(kStepProbabilityBase, reset ? 1.0 :
                 continuousValue(where.x, zoneLeft, widths[zone]));
            break;
        case 6: {
            if (reset) {
                edit(kStepOctaveBase,0.5);
            } else {
                int index=std::clamp(static_cast<int>(std::lround(current(kStepOctaveBase)*4.0)),0,4);
                index += where.x < zoneLeft+widths[zone]*0.5 ? -1 : 1;
                index=std::clamp(index,0,4);
                edit(kStepOctaveBase,static_cast<double>(index)/4.0);
            }
            break;
        }
        case 7:
            edit(kStepLockBase, reset ? 0.0 : (current(kStepLockBase)>=0.5?0.0:1.0));
            break;
    }

    invalid();
    return VSTGUI::kMouseDownEventHandledButDontNeedMovedOrUpEvents;
}

UIScaleView::UIScaleView(const VSTGUI::CRect& size,
                         VSTGUI::VST3Editor* editor,
                         Controller* controller)
: VSTGUI::CView(size),editor_(editor),controller_(controller) {
    setTransparency(true); setMouseEnabled(true);
}
void UIScaleView::draw(VSTGUI::CDrawContext* c) {
    const auto r=getViewSize();
    panel(c,r,true);
    c->setFont(VSTGUI::kNormalFontSmall);
    c->setFontColor(kText);
    c->drawString(editor_ && editor_->getZoomFactor()>=1.25?"150%":"100%",r,VSTGUI::kCenterText);
    setDirty(false);
}
VSTGUI::CMouseEventResult UIScaleView::onMouseDown(
    VSTGUI::CPoint& where,const VSTGUI::CButtonState& buttons) {
    if (!controller_ || !getViewSize().pointInside(where) || !buttons.isLeftButton())
        return VSTGUI::kMouseEventNotHandled;
    controller_->setGuiZoom(editor_ && editor_->getZoomFactor()>=1.25?1.0:1.5);
    invalid();
    return VSTGUI::kMouseDownEventHandledButDontNeedMovedOrUpEvents;
}

void configureEditor(VSTGUI::VST3Editor* editor,double width,double height,double zoom) {
    if (!editor) return;
    editor->setAllowedZoomFactors(std::vector<double>{1.0,1.5});
    editor->setEditorSizeConstrains({width,height},{width,height});
    editor->setZoomFactor(zoom>=1.25?1.5:1.0);
}

VSTGUI::CView* createCustomView(VSTGUI::UTF8StringPtr name,
                                const VSTGUI::UIAttributes& attributes,
                                VSTGUI::VST3Editor* editor,
                                Controller* controller) {
    if(!name || !editor) return nullptr;
    VSTGUI::CPoint origin{0,0},size{80,80};
    attributes.getPointAttribute("origin",origin);
    attributes.getPointAttribute("size",size);
    const VSTGUI::CRect rect(origin.x,origin.y,origin.x+size.x,origin.y+size.y);

    Steinberg::int32 tag=-1;
    attributes.getIntegerAttribute("control-tag",tag);

    if(std::strcmp(name,"ArpFaceplate")==0) return new FaceplateView(rect);
    if(std::strcmp(name,"ArpLogo")==0) return new LogoView(rect);
    if(std::strcmp(name,"ArpKnob")==0 && tag>=0) return new KnobView(rect,editor,tag);
    if(std::strcmp(name,"ArpUIScale")==0) return new UIScaleView(rect,editor,controller);
    if(std::strcmp(name,"ArpStepGrid")==0) return new StepGridView(rect,controller);
    if(std::strcmp(name,"ArpSelectedStep")==0) return new SelectedStepView(rect,controller);
    if(std::strcmp(name,"ArpVariate")==0 && tag>=0) return new ActionButton(rect,controller,tag,"VARIATE");

    if(std::strcmp(name,"ArpMode")==0) return new PopupSelectorView(rect,editor,tag,{"UP","DOWN","UP-DOWN","DOWN-UP","PLAYED","RANDOM"});
    if(std::strcmp(name,"ArpRate")==0) return new PopupSelectorView(rect,editor,tag,{"1/4","1/8","1/16","1/32","1/64","1/8T","1/16T","1/32T","1/8D","1/16D","1/32D"});
    if(std::strcmp(name,"ArpOctaves")==0) return new PopupSelectorView(rect,editor,tag,{"1","2","3","4"});
    if(std::strcmp(name,"ArpTrigger")==0) return new PopupSelectorView(rect,editor,tag,{"RESTART","CONTINUE"});
    if(std::strcmp(name,"ArpPolicy")==0) return new PopupSelectorView(rect,editor,tag,{"CHORD","SCALE","CHROM"});
    if(std::strcmp(name,"ArpKey")==0) return new PopupSelectorView(rect,editor,tag,{"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"});
    if(std::strcmp(name,"ArpScale")==0) return new PopupSelectorView(
        rect,editor,tag,
        {"MAJOR","MINOR","DORIAN","PHRYGIAN","LYDIAN",
         "MIXOLYD","HARM MIN","MAJ PENT","MIN PENT","BLUES"});
    if(std::strcmp(name,"ArpLock")==0) return new ToggleView(rect,editor,tag,"OPEN","LOCK");

    return nullptr;
}

} // namespace arporator::vst3::gui
