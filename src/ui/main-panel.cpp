/*
 * SideQuest Starting Point
 *
 * Basically lets paul bootstrap his projects.
 *
 * Copyright 2024-2025, Paul Walker and Various authors, as described in the github
 * transaction log.
 *
 * This source repo is released under the MIT license, but has
 * GPL3 dependencies, as such the combined work will be
 * released under GPL3.
 *
 * The source code and license are at https://github.com/baconpaul/sidequest-startingpoint
 */

#include "main-panel.h"
#include "plugin-editor.h"
#include "patch-data-bindings.h"

namespace baconpaul::sidequest_ns::ui
{
MainPanel::MainPanel(PluginEditor &e)
    : sst::jucegui::components::NamedPanel("Main Panel"), editor(e)
{
    createComponent(editor, *this, editor.patchCopy.sqParams.pitch, pitchK, pitchA);
    addAndMakeVisible(*pitchK);

    createComponent(editor, *this, editor.patchCopy.sqParams.harmlev, harmK, harmA);
    addAndMakeVisible(*harmK);
}

void MainPanel::resized()
{
    pitchK->setBounds(10, 50, 100, 150);
    harmK->setBounds(115, 50, 100, 150);
}

} // namespace baconpaul::sidequest_ns::ui
