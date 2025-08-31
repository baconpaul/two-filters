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

#ifndef BACONPAUL_TWOFILTERS_UI_STEPLFO_PANEL_H
#define BACONPAUL_TWOFILTERS_UI_STEPLFO_PANEL_H

#include "sst/jucegui/components/NamedPanel.h"
#include "sst/jucegui/components/Knob.h"
#include "sst/jucegui/components/MenuButton.h"
#include "patch-data-bindings.h"
#include "plugin-editor.h"

namespace baconpaul::twofilters::ui
{

struct StepLFOPanel : sst::jucegui::components::NamedPanel
{
    StepLFOPanel(PluginEditor &editor, int instance);
    ~StepLFOPanel();
    void resized() override;

    PluginEditor &editor;

    void beginEdit() {}
    void endEdit() {}

    void onIdle();

    int instance;
};
} // namespace baconpaul::twofilters::ui
#endif // STEPLFO_PANEL_H
