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

#include "steplfo-panel.h"
#include "sst/jucegui/components/VSlider.h"

namespace baconpaul::twofilters::ui
{

struct StepSlider : sst::jucegui::components::VSlider
{
};

StepLFOPanel::StepLFOPanel(PluginEditor &editor, int instance)
    : editor(editor), instance(instance),
      sst::jucegui::components::NamedPanel("StepLFO " + std::to_string(instance + 1))
{
    auto &sn = editor.patchCopy.stepLfoNodes[instance];
    for (int i = 0; i < maxSteps; i++)
    {
        createComponent(editor, *this, sn.steps[i], steps[i], stepDs[i]);
        addAndMakeVisible(*steps[i]);
    }
}
StepLFOPanel::~StepLFOPanel() = default;
void StepLFOPanel::resized()
{
    auto q = getContentArea().withTrimmedRight(90).withTrimmedBottom(90);
    auto qw = q.getWidth() / maxSteps;
    q = q.withWidth(qw);
    for (int i = 0; i < maxSteps; i++)
    {
        steps[i]->setBounds(q);
        q = q.translated(qw, 0);
    }
}

} // namespace baconpaul::twofilters::ui