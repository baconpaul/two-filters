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

#ifndef BACONPAUL_TWOFILTERS_UI_ENVFOLLOWER_PANEL_H
#define BACONPAUL_TWOFILTERS_UI_ENVFOLLOWER_PANEL_H

#include "sst/jucegui/components/NamedPanel.h"
#include "sst/jucegui/components/Knob.h"
#include "sst/jucegui/components/MenuButton.h"
#include "sst/jucegui/components/RuledLabel.h"
#include "sst/jucegui/components/VUMeter.h"
#include "plugin-editor.h"

namespace baconpaul::twofilters::ui
{

// layout mock: placeholder data, not bound to the patch
struct MockContinuous : jdat::Continuous
{
    std::string label;
    float value, minV, maxV;

    MockContinuous(const std::string &l, float v, float mn = 0, float mx = 1)
        : label(l), value(v), minV(mn), maxV(mx)
    {
    }
    std::string getLabel() const override { return label; }
    float getValue() const override { return value; }
    void setValueFromGUI(const float &f) override { value = f; }
    void setValueFromModel(const float &f) override { value = f; }
    float getDefaultValue() const override { return isBipolar() ? 0.f : minV; }
    float getMin() const override { return minV; }
    float getMax() const override { return maxV; }
};

struct EnvTrace;

struct EnvFollowerPanel : sst::jucegui::components::NamedPanel
{
    EnvFollowerPanel(PluginEditor &editor);
    ~EnvFollowerPanel();
    void resized() override;

    PluginEditor &editor;

    std::unique_ptr<MockContinuous> attackD, releaseD;
    std::unique_ptr<sst::jucegui::components::Knob> attack, release;
    void paintHeader(juce::Graphics &g) override;
    int headerRuleEnd{0};

    std::unique_ptr<sst::jucegui::components::MenuButton> source, mode;
    void showChoiceMenu(sst::jucegui::components::MenuButton *b,
                        const std::vector<std::string> &choices);
    std::unique_ptr<sst::jucegui::components::VUMeter> level;
    std::unique_ptr<EnvTrace> trace;

    static constexpr int numRoutes{2 * 2 + 5};
    std::array<std::unique_ptr<MockContinuous>, numRoutes> routeD;
    std::array<std::unique_ptr<sst::jucegui::components::Knob>, numRoutes> routeK;
    std::unique_ptr<sst::jucegui::components::RuledLabel> toF1, toF2, toRt;
};
} // namespace baconpaul::twofilters::ui
#endif
