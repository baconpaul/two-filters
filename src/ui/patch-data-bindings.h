/*
 * Two Filters
 *
 * Two Filters, and some controls thereof
 *
 * Copyright 2024-2025, Paul Walker and Various authors, as described in the github
 * transaction log.
 *
 * This source repo is released under the MIT license, but has
 * GPL3 dependencies, as such the combined work will be
 * released under GPL3.
 *
 * The source code and license are at https://github.com/baconpaul/two-filters
 */

#ifndef BACONPAUL_TWOFILTERS_UI_PATCH_DATA_BINDINGS_H
#define BACONPAUL_TWOFILTERS_UI_PATCH_DATA_BINDINGS_H

#include <cstdint>
#include "sst/jucegui/data/Continuous.h"
#include "sst/jucegui/data/Discrete.h"
#include <sst/jucegui/component-adapters/ThrowRescaler.h>
#include <sst/jucegui/component-adapters/ComponentTags.h>

#include "sst/jucegui/components/Knob.h"
#include "sst/jucegui/components/DraggableTextEditableValue.h"

#include "plugin-editor.h"

namespace baconpaul::twofilters::ui
{
struct PatchContinuous : jdat::Continuous
{
    PluginEditor &editor;
    uint32_t pid;
    Param *p{nullptr};
    std::function<void()> onGuiSetValue{nullptr};

    bool tempoSynced{false};

    PatchContinuous(PluginEditor &e, uint32_t id) : editor(e), pid(id)
    {
        if (e.patchCopy.paramMap.find(id) == e.patchCopy.paramMap.end())
        {
            SQLOG("You were unable to find param " << id
                                                   << " - its probably not in patch::params()");
            assert(false);
            std::terminate();
        }

        p = e.patchCopy.paramMap.at(id);
    }
    ~PatchContinuous() override = default;

    std::string labelOverride{};
    std::string getLabel() const override
    {
        if (!labelOverride.empty())
            return labelOverride;
        auto r = p->meta.name;
        return r;
    }
    float getValue() const override { return p->value; }
    std::string getValueAsStringFor(float f) const override
    {
        if (tempoSynced)
        {
            auto r = p->meta.valueToString(
                p->meta.snapToTemposync(f),
                sst::basic_blocks::params::ParamMetaData::FeatureState().withTemposync(true));
            if (r.has_value())
            {
                return *r;
            }
        }
        auto r = p->meta.valueToString(f);
        if (r.has_value())
            return *r;
        return "error";
    }
    void setValueAsString(const std::string &s) override
    {
        if (tempoSynced)
        {
            auto f = p->meta.valueFromTemposyncNotation(s);
            if (f.has_value())
            {
                setValueFromGUI(*f);
            }
        }
        else
        {
            std::string em;
            auto v = p->meta.valueFromString(s, em);
            if (v.has_value())
            {
                setValueFromGUI(*v);
            }
        }
    }
    void setValueFromGUI(const float &f) override
    {
        if (p->value == p->meta.minVal && f != p->value)
        {
            if (onPullFromMin)
                onPullFromMin();
        }

        if (p->value == p->meta.defaultVal && f != p->value)
        {
            if (onPullFromDef)
                onPullFromDef();
        }
        if (tempoSynced)
        {
            p->value = p->meta.snapToTemposync(f);
        }
        else
        {
            p->value = f;
        }
        editor.mainToAudio.push({Engine::MainToAudioMsg::Action::SET_PARAM, pid, f});
        editor.requestParamsFlush();
        editor.updateTooltip(this);

        if (onGuiSetValue)
            onGuiSetValue();
    }
    void setValueFromModel(const float &f) override { p->value = f; }
    float getDefaultValue() const override { return p->meta.defaultVal; }
    bool isBipolar() const override { return p->meta.isBipolar(); }
    float getMin() const override { return p->meta.minVal; }
    float getMax() const override { return p->meta.maxVal; }

    std::function<void()> onPullFromMin{nullptr}, onPullFromDef{nullptr};

    using cubic_t = sst::jucegui::component_adapters::CubicThrowRescaler<PatchContinuous>;
};

struct PatchDiscrete : jdat::Discrete
{
    PluginEditor &editor;
    uint32_t pid;
    Param *p{nullptr};
    std::function<void()> onGuiSetValue{nullptr};

    PatchDiscrete(PluginEditor &e, uint32_t id) : editor(e), pid(id)
    {
        if (e.patchCopy.paramMap.find(id) == e.patchCopy.paramMap.end())
        {
            SQLOG("You were unable to find param " << id
                                                   << " - its probably not in patch::params()");
            assert(false);
            std::terminate();
        }
        p = e.patchCopy.paramMap.at(id);
    }
    ~PatchDiscrete() override = default;

    std::string getLabel() const override
    {
        auto r = p->meta.name;
        return r;
    }
    int getValue() const override { return static_cast<int>(std::round(p->value)); }
    std::string getValueAsStringFor(int i) const override
    {
        auto res = p->meta.valueToString(i);
        if (res.has_value())
            return *res;
        return "error";
    }
    void setValueFromGUI(const int &f) override
    {
        p->value = f;
        editor.mainToAudio.push(
            {Engine::MainToAudioMsg::Action::SET_PARAM, pid, static_cast<float>(f)});
        editor.requestParamsFlush();

        if (onGuiSetValue)
            onGuiSetValue();
    }
    void setValueFromModel(const int &f) override { p->value = f; }
    int getDefaultValue() const override
    {
        return static_cast<int>(std::round(p->meta.defaultVal));
    }
    int getMin() const override { return static_cast<int>(std::round(p->meta.minVal)); }
    int getMax() const override { return static_cast<int>(std::round(p->meta.maxVal)); }
};

template <typename P, typename T, typename Q, typename... Args>
void createComponent(PluginEditor &e, P &panel, const Param &parm, std::unique_ptr<T> &cm,
                     std::unique_ptr<Q> &pc, Args... args)
{
    auto id = parm.meta.id;
    pc = std::make_unique<Q>(e, id);
    cm = std::make_unique<T>();

    if constexpr (std::is_same_v<Q, PatchContinuous> &&
                  !std::is_same_v<T, jcmp::DraggableTextEditableValue>) // hack
    {
        cm->onPopupMenu = [&e, ptr = cm.get()](auto &mods)
        {
            e.hideTooltip();
            e.popupMenuForContinuous(ptr);
        };
    }
    cm->onBeginEdit = [&e, &cm, &pc, args..., id, &panel]()
    {
        e.mainToAudio.push({Engine::MainToAudioMsg::Action::BEGIN_EDIT, id});
        if (std::is_same_v<Q, PatchContinuous>)
        {
            e.updateTooltip(pc.get());
            e.showTooltipOn(cm.get());
        }

        panel.beginEdit(args...);
    };
    cm->onEndEdit = [&e, id, &panel]()
    {
        e.mainToAudio.push({Engine::MainToAudioMsg::Action::END_EDIT, id});
        if (std::is_same_v<Q, PatchContinuous>)
        {
            e.hideTooltip();
        }
    };
    cm->onWheelEditOccurred = [&cm]()
    {
        if (std::is_same_v<Q, PatchContinuous>)
        {
            cm->immediatelyInitiateIdleAction(2000);
        }
    };

    cm->onIdleHover = [&e, &cm, &pc]()
    {
        e.updateTooltip(pc.get());
        e.showTooltipOn(cm.get());
    };
    cm->onIdleHoverEnd = [&e]() { e.hideTooltip(); };

    cm->setSource(pc.get());
    sst::jucegui::component_adapters::setClapParamId(cm.get(), id);
    e.componentByID[id] = juce::Component::SafePointer<juce::Component>(cm.get());
}

template <typename P, typename T, typename Rescaler, typename... Args>
void createRescaledComponent(PluginEditor &e, P &panel, const Param &parm, std::unique_ptr<T> &cm,
                             std::unique_ptr<Rescaler> &rc, Args... args)
{
    auto id = parm.meta.id;
    auto pc = std::make_unique<PatchContinuous>(e, id);
    rc = std::make_unique<Rescaler>(std::move(pc));
    cm = std::make_unique<T>();

    if constexpr (!std::is_same_v<T, jcmp::DraggableTextEditableValue>) // hack
    {
        cm->onPopupMenu = [&e, ptr = cm.get()](auto &mods)
        {
            e.hideTooltip();
            e.popupMenuForContinuous(ptr);
        };
    }
    cm->onBeginEdit = [&e, &cm, &rc, args..., id, &panel]()
    {
        e.mainToAudio.push({Engine::MainToAudioMsg::Action::BEGIN_EDIT, id});
        e.updateTooltip(rc.get());
        e.showTooltipOn(cm.get());

        panel.beginEdit(args...);
    };
    cm->onEndEdit = [&e, id, &panel]()
    {
        e.mainToAudio.push({Engine::MainToAudioMsg::Action::END_EDIT, id});
        e.hideTooltip();
    };
    cm->onIdleHover = [&e, &cm, &rc]()
    {
        e.updateTooltip(rc.get());
        e.showTooltipOn(cm.get());
    };
    cm->onIdleHoverEnd = [&e]() { e.hideTooltip(); };
    cm->setSource(rc.get());
    sst::jucegui::component_adapters::setClapParamId(cm.get(), id);

    e.componentByID[id] = juce::Component::SafePointer<juce::Component>(cm.get());
}

} // namespace baconpaul::twofilters::ui
#endif // PATCH_CONTINUOUS_H
