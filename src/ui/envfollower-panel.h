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
#include "patch-data-bindings.h"
#include "plugin-editor.h"

namespace baconpaul::twofilters::ui
{

struct EnvTrace;
struct EnvLevel;

struct EnvFollowerPanel : sst::jucegui::components::NamedPanel
{
    EnvFollowerPanel(PluginEditor &editor);
    ~EnvFollowerPanel();
    void resized() override;

    PluginEditor &editor;

    // linear input and follower peaks since the last send
    void setLevels(float in, float out);

    void beginEdit() {}
    void endEdit(int id) {}

    std::unique_ptr<PatchContinuous> attackD, releaseD, gainD;
    std::unique_ptr<sst::jucegui::components::Knob> attack, release, gain;
    void paintHeader(juce::Graphics &g) override;
    int headerRuleEnd{0};

    std::unique_ptr<PatchDiscrete> sourceD, channelD;
    std::unique_ptr<sst::jucegui::components::MenuButtonDiscreteEditor> source, channel;
    std::unique_ptr<EnvLevel> level;
    std::unique_ptr<EnvTrace> trace;

    static constexpr int numRoutes{2 * 2 + 5};
    std::array<std::unique_ptr<PatchContinuous>, numRoutes> routeD;
    std::array<std::unique_ptr<sst::jucegui::components::Knob>, numRoutes> routeK;
    std::unique_ptr<sst::jucegui::components::RuledLabel> toF1, toF2, toRt;
};
} // namespace baconpaul::twofilters::ui
#endif
