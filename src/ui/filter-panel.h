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

#ifndef BACONPAUL_TWOFILTERS_UI_FILTER_PANEL_H
#define BACONPAUL_TWOFILTERS_UI_FILTER_PANEL_H

#include "sst/jucegui/components/NamedPanel.h"
#include "sst/jucegui/components/Knob.h"
#include "sst/jucegui/components/MenuButton.h"
#include "patch-data-bindings.h"
#include "plugin-editor.h"

namespace baconpaul::twofilters::ui
{
struct FilterCurve;

struct FilterPanel : sst::jucegui::components::NamedPanel
{
    FilterPanel(PluginEditor &editor, int instance);
    ~FilterPanel();
    void resized() override;

    PluginEditor &editor;

    void onModelChanged();

    void beginEdit() {}
    void endEdit(int id);

    void onIdle();

    std::unique_ptr<FilterCurve> curve;

    std::unique_ptr<PatchContinuous> cutoffD, resonanceD, morphD, panD;
    std::unique_ptr<PatchDiscrete> activeD;

    std::unique_ptr<sst::jucegui::components::Knob> cutoffK, resonanceK, morphK, panK;

    std::unique_ptr<sst::jucegui::components::MenuButton> modelMenu, configMenu;

    void showModelMenu();
    void showConfigMenu();

    int instance;
};
} // namespace baconpaul::twofilters::ui
#endif // FILTER_PANEL_H
