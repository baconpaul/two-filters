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

#ifndef BACONPAUL_TWOFILTERS_UI_ROUTING_PANEL_H
#define BACONPAUL_TWOFILTERS_UI_ROUTING_PANEL_H

#include "sst/jucegui/components/NamedPanel.h"
#include "patch-data-bindings.h"
#include "plugin-editor.h"

namespace baconpaul::twofilters::ui
{
struct RoutingPanel : sst::jucegui::components::NamedPanel
{
    RoutingPanel(PluginEditor &editor);
    void resized() override;

    PluginEditor &editor;

    void beginEdit() {}
    void endEdit() {}
};
} // namespace baconpaul::twofilters::ui
#endif // ROUTING_PANEL_H
