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

#include "debug-panel.h"
#include "plugin-editor.h"
#include "patch-data-bindings.h"

namespace baconpaul::twofilters::ui
{

DebugPanel::DebugPanel(PluginEditor &e)
    : sst::jucegui::components::NamedPanel("Debug Leftovers Panel"), editor(e)
{
    knobs.resize(e.patchCopy.params.size());
    knobAs.resize(e.patchCopy.params.size());
    for (int i = 0; i < e.patchCopy.params.size(); i++)
    {
        auto &p = editor.patchCopy.params[i];
        if (p->meta.groupName == "Routing" || p->meta.groupName == "Filter 1" ||
            p->meta.groupName == "Filter 2")
            continue;
        createComponent(editor, *this, *editor.patchCopy.params[i], knobs[i], knobAs[i]);
        addAndMakeVisible(*knobs[i]);
    }
}

void DebugPanel::resized()
{
    auto b = getContentArea();
    auto w = b.getWidth();
    auto x = b.getX();
    auto y = b.getY();
    auto spw = 50;
    auto sph = 70;

    for (int i = 0; i < editor.patchCopy.params.size(); i++)
    {
        if (!knobs[i])
            continue;
        knobs[i]->setBounds(x, y, spw - 5, sph - 5);
        x += spw;
        if (x + spw > w)
        {
            x = b.getX();
            y += sph;
        }
    }
}

} // namespace baconpaul::twofilters::ui
