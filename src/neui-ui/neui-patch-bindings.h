/*
 * Two Filters
 *
 * Two Filters, and some controls thereof
 *
 * Copyright 2024-2026, Paul Walker and Various authors, as described in the github
 * transaction log.
 *
 * This source repo is released under the MIT license, but has
 * GPL3 dependencies, as such the combined work will be
 * released under GPL3.
 *
 * The source code and license are at https://github.com/baconpaul/two-filters
 */

#ifndef BACONPAUL_TWOFILTERS_NEUI_UI_NEUI_PATCH_BINDINGS_H
#define BACONPAUL_TWOFILTERS_NEUI_UI_NEUI_PATCH_BINDINGS_H

#include <cstdint>
#include "sst/neuigui/data/Continuous.h"
#include "sst/neuigui/data/Discrete.h"

#include "neui-plugin-editor.h"

/*
 * The sst-neuigui port of patch-data-bindings.h. PatchContinuous and
 * PatchDiscrete are near verbatim; createComponent differs in ownership -
 * neuiplusplus parents own their children through add<T>, so the component
 * member is a non-owning pointer filled in here rather than a unique_ptr.
 */
namespace baconpaul::twofilters::ui
{
namespace ndat = sst::neuigui::data;

struct PatchContinuous : ndat::Continuous
{
    NeuiPluginEditor &editor;
    uint32_t pid;
    Param *p{nullptr};
    std::function<void()> onGuiSetValue{nullptr};

    bool tempoSynced{false};

    PatchContinuous(NeuiPluginEditor &e, uint32_t id);
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
    void setValueFromGUI(const float &f) override;
    void setValueFromModel(const float &f) override { p->value = f; }
    float getDefaultValue() const override { return p->meta.defaultVal; }
    bool isBipolar() const override { return p->meta.isBipolar(); }
    float getMin() const override { return p->meta.minVal; }
    float getMax() const override { return p->meta.maxVal; }

    std::function<void()> onPullFromMin{nullptr}, onPullFromDef{nullptr};
};

struct PatchDiscrete : ndat::Discrete
{
    NeuiPluginEditor &editor;
    uint32_t pid;
    Param *p{nullptr};
    std::function<void()> onGuiSetValue{nullptr};

    PatchDiscrete(NeuiPluginEditor &e, uint32_t id);
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
    void setValueFromGUI(const int &f) override;
    void setValueFromModel(const int &f) override { p->value = f; }
    int getDefaultValue() const override
    {
        return static_cast<int>(std::round(p->meta.defaultVal));
    }
    int getMin() const override { return static_cast<int>(std::round(p->meta.minVal)); }
    int getMax() const override { return static_cast<int>(std::round(p->meta.maxVal)); }
};

/*
 * panel is both the neui parent (owns the widget) and the beginEdit/endEdit
 * target, exactly as in the juce ui. cm comes back as a non-owning pointer.
 */
template <typename P, typename T, typename Q, typename... Args>
void createComponent(NeuiPluginEditor &e, P &panel, const Param &parm, T *&cm,
                     std::unique_ptr<Q> &pc, Args... args)
{
    auto id = parm.meta.id;
    pc = std::make_unique<Q>(e, id);
    cm = &panel.template add<T>();

    if constexpr (std::is_same_v<Q, PatchContinuous>)
    {
        cm->onPopupMenu = [&e, ptr = cm](auto &mods) { e.popupMenuForContinuous(ptr); };
    }
    cm->onBeginEdit = [&e, cm, &pc, args..., id, &panel]()
    {
        e.mainToAudio.push({Engine::MainToAudioMsg::Action::BEGIN_EDIT, id});
        panel.beginEdit(args...);
    };
    cm->onEndEdit = [&e, id, &panel]()
    {
        e.mainToAudio.push({Engine::MainToAudioMsg::Action::END_EDIT, id});
        panel.endEdit(id);
    };

    cm->setSource(pc.get());
    e.componentByID[id] = cm;
    e.componentRepaintByID[id] = [cm]() { cm->repaint(); };
}

} // namespace baconpaul::twofilters::ui
#endif // BACONPAUL_TWOFILTERS_NEUI_UI_NEUI_PATCH_BINDINGS_H
