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

#include "routing-panel.h"

namespace baconpaul::twofilters::ui
{

RoutingPanel::RoutingPanel(PluginEditor &editor)
    : sst::jucegui::components::NamedPanel("Routing"), editor(editor)
{
}
void RoutingPanel::resized() {}

} // namespace baconpaul::twofilters::ui