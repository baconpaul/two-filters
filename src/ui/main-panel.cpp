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

#include "main-panel.h"
#include "plugin-editor.h"
#include "patch-data-bindings.h"

namespace baconpaul::twofilters::ui
{

MainPanel::MainPanel(PluginEditor &e)
    : sst::jucegui::components::NamedPanel("Main Panel"), editor(e)
{
    knobs.resize(e.patchCopy.params.size());
    knobAs.resize(e.patchCopy.params.size());
    for (int i = 0; i < e.patchCopy.params.size(); i++)
    {
        createComponent(editor, *this, *editor.patchCopy.params[i], knobs[i], knobAs[i]);
        addAndMakeVisible(*knobs[i]);
    }
}

void MainPanel::resized()
{
    auto b = getContentArea();
    auto w = b.getWidth();
    auto x = b.getX();
    auto y = b.getY();
    auto spw = 50;
    auto sph = 70;

    for (int i = 0; i < editor.patchCopy.params.size(); i++)
    {
        knobs[i]->setBounds(x, y, spw - 5, sph - 5);
        x += spw;
        if (x + spw > w)
        {
            x = b.getX();
            y += sph;
        }
    }
}

/*
void MainPanel::paint(juce::Graphics &g)
{
    sst::jucegui::components::NamedPanel::paint(g);
    g.setColour(juce::Colours::white);
    g.drawLine(0,0,getWidth(),getHeight(), 1);

    // shitty hack
    static sst::filters::FilterPlotter plotter;

    auto crv = plotter.plotFilterMagnitudeResponse(sst::filters::FilterType::fut_obxd_4pole,
sst::filters::FilterSubType::st_obxd4pole_24dB, 0, 0.99, 0, 0, 0);

    auto xs = crv.first;
    for (auto &x : xs)
        x = log10(x);

    auto ys = crv.second;;
    auto xsc = 1.0 / log10(20000) * getWidth();
    auto ysc = 3.0;
    int yoff = 100;
    for (int i=1; i<xs.size(); i++)
    {
        g.drawLine(xs[i-1] * xsc, yoff - ysc * ys[i-1], xs[i] * xsc, yoff - ysc * ys[i], 1);

    }
}
*/

} // namespace baconpaul::twofilters::ui
