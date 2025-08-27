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

#ifndef BACONPAUL_TWOFILTERS_UI_MAIN_PANEL_H
#define BACONPAUL_TWOFILTERS_UI_MAIN_PANEL_H

#include "sst/jucegui/components/NamedPanel.h"
#include "patch-data-bindings.h"
#include "plugin-editor.h"

namespace baconpaul::twofilters::ui
{
struct MainPanel : sst::jucegui::components::NamedPanel
{
    MainPanel(PluginEditor &editor);
    void resized() override;

    std::vector<std::unique_ptr<sst::jucegui::components::Knob>> knobs;
    std::vector<std::unique_ptr<PatchContinuous>> knobAs;

    PluginEditor &editor;

    void beginEdit() {}
    void endEdit() {}
};
} // namespace baconpaul::twofilters::ui
#endif // MAIN_PANEL_H
