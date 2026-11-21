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

#include "envfollower-panel.h"
#include <array>
#include "sst/jucegui/components/BaseStyles.h"
#include "sst/jucegui/components/VUMeter.h"

namespace baconpaul::twofilters::ui
{

namespace bst = sst::jucegui::components::base_styles;
using vuSt = sst::jucegui::components::VUMeter::Styles;

// scrolling input peaks with the follower over them, both in modulation depth
struct EnvTrace : juce::Component
{
    EnvTrace(EnvFollowerPanel &p) : panel(p) {}

    static constexpr int histSize{1024};
    std::array<float, histSize> inHist{}, outHist{};
    int writePos{0};

    void push(float in, float out)
    {
        inHist[writePos] = in;
        outHist[writePos] = out;
        writePos = (writePos + 1) % histSize;
        repaint();
    }

    void paint(juce::Graphics &g) override
    {
        auto st = panel.style();
        auto gCol = st->getColour(bst::ValueGutter::styleClass, bst::ValueGutter::gutter);
        auto vCol = st->getColour(bst::ValueBearing::styleClass, bst::ValueBearing::value);
        auto lCol = st->getColour(bst::BaseLabel::styleClass, bst::BaseLabel::labelcolor);

        g.setColour(gCol);
        g.fillRect(getLocalBounds());

        auto w = std::min(getWidth(), histSize);
        auto h = (float)getHeight();
        auto gain = panel.editor.patchMainRef.envFollowerNode.gain.value;
        auto y = [&](float lev) { return h * (1 - 0.9f * Engine::envToMod(lev, gain)); };
        auto at = [&](int x) { return (writePos - w + x + histSize) % histSize; };

        g.setColour(lCol.withAlpha(0.35f));
        for (int x = 0; x < w; ++x)
            g.drawVerticalLine(x, y(inHist[at(x)]), h);

        juce::Path p;
        p.startNewSubPath(0, y(outHist[at(0)]));
        for (int x = 1; x < w; ++x)
            p.lineTo(x, y(outHist[at(x)]));
        g.setColour(vCol);
        g.strokePath(p, juce::PathStrokeType(1.5f));
    }

    EnvFollowerPanel &panel;
};

// how much of each route depth the follower is giving right now
struct EnvLevel : juce::Component
{
    EnvLevel(EnvFollowerPanel &p) : panel(p) {}

    float level{0};

    void paint(juce::Graphics &g) override
    {
        auto st = panel.style();
        auto b = getLocalBounds().toFloat();
        g.setColour(st->getColour(vuSt::styleClass, vuSt::vu_gutter));
        g.fillRect(b);

        auto gain = panel.editor.patchMainRef.envFollowerNode.gain.value;
        auto v = Engine::envToMod(level, gain) * b.getWidth();
        g.setGradientFill(juce::ColourGradient::horizontal(
            st->getColour(vuSt::styleClass, vuSt::vu_gradstart), 0,
            st->getColour(vuSt::styleClass, vuSt::vu_gradend), b.getWidth()));
        g.fillRect(b.withWidth(v));
    }

    EnvFollowerPanel &panel;
};

EnvFollowerPanel::EnvFollowerPanel(PluginEditor &e)
    : sst::jucegui::components::NamedPanel("Envelope Follower"), editor(e)
{
    auto &en = editor.patchMainRef.envFollowerNode;

    createComponent(editor, *this, en.attack, attack, attackD);
    addAndMakeVisible(*attack);
    createComponent(editor, *this, en.release, release, releaseD);
    addAndMakeVisible(*release);
    createComponent(editor, *this, en.gain, gain, gainD);
    addAndMakeVisible(*gain);

    createComponent(editor, *this, en.source, source, sourceD);
    addAndMakeVisible(*source);
    createComponent(editor, *this, en.channel, channel, channelD);
    addAndMakeVisible(*channel);

    level = std::make_unique<EnvLevel>(*this);
    addAndMakeVisible(*level);

    trace = std::make_unique<EnvTrace>(*this);
    addAndMakeVisible(*trace);

    auto mkRoute = [this](int idx, const Param &p, const std::string &lab)
    {
        createComponent(editor, *this, p, routeK[idx], routeD[idx]);
        routeD[idx]->labelOverride = lab;
        addAndMakeVisible(*routeK[idx]);
    };
    mkRoute(0, en.toCO[0], "Cutoff");
    mkRoute(1, en.toRes[0], "Res");
    mkRoute(2, en.toCO[1], "Cutoff");
    mkRoute(3, en.toRes[1], "Res");
    mkRoute(4, en.toPreG, "Pre");
    mkRoute(5, en.toFiltBlend, "Blend");
    mkRoute(6, en.toMix, "Mix");
    mkRoute(7, en.toFB, "F/Back");
    mkRoute(8, en.toNoise, "Noise");

    auto mkLab = [this](auto &l, const std::string &t)
    {
        l = std::make_unique<sst::jucegui::components::RuledLabel>();
        l->setText(t);
        addAndMakeVisible(*l);
    };
    mkLab(toF1, "To Filter 1");
    mkLab(toF2, "To Filter 2");
    mkLab(toRt, "To Main");

    namespace jcad = sst::jucegui::component_adapters;
    auto bi = 15000;
    jcad::setTraversalId(attack.get(), bi++);
    jcad::setTraversalId(release.get(), bi++);
    jcad::setTraversalId(gain.get(), bi++);
    jcad::setTraversalId(source.get(), bi++);
    jcad::setTraversalId(channel.get(), bi++);
    for (auto &k : routeK)
        jcad::setTraversalId(k.get(), bi++);
}

EnvFollowerPanel::~EnvFollowerPanel() = default;

void EnvFollowerPanel::setLevels(float in, float out)
{
    trace->push(in, out);
    level->level = out;
    level->repaint();
}

// the route group labels share the header row, so stop the title rule short of them
void EnvFollowerPanel::paintHeader(juce::Graphics &g)
{
    auto ht = getLocalBounds().reduced(outerMargin).withHeight(headerHeight).reduced(4, 0);
    g.setFont(getFont(Styles::labelfont));
    g.setColour(getColour(Styles::labelcolor));
    g.drawText(name, ht, juce::Justification::centredLeft);

    auto lw = SST_STRING_WIDTH_INT(g.getCurrentFont(), name);
    auto rule = ht.toFloat()
                    .withLeft(ht.getX() + lw + 8)
                    .withRight(headerRuleEnd)
                    .withY(ht.getCentreY() - 0.5f)
                    .withHeight(1);
    g.setColour(getColour(Styles::labelrule));
    if (rule.getWidth() > 0)
        g.fillRect(rule);
}

void EnvFollowerPanel::resized()
{
    auto ca = getContentArea();

    // knob pitch matches the step lfo panels
    auto kw = 45;
    auto kmarg = 10;
    auto group = [&](int n) { return kmarg + n * (kw + kmarg); };
    auto routeW = group(2) + group(2) + group(5);
    auto chanW = 90;

    // header label to knob spacing already matches the step lfo panels
    auto knobs = ca.withHeight(60);
    auto header = getLocalBounds().reduced(outerMargin).withHeight(headerHeight).withX(ca.getX());

    // controls on the left
    auto x0 = ca.getX();
    auto kn = 0;
    for (auto *k : {attack.get(), release.get(), gain.get()})
        k->setBounds(knobs.withX(x0 + group(kn++)).withWidth(kw));

    auto cA = knobs.withX(x0 + group(3) + 4).withWidth(chanW - 8);
    source->setBounds(cA.withHeight(22));
    channel->setBounds(cA.withHeight(22).translated(0, 25));
    level->setBounds(cA.withHeight(6).translated(0, 51));

    auto rx = ca.getRight() - routeW;
    auto mx = x0 + group(3) + chanW + kmarg;
    trace->setBounds(knobs.withX(mx).withRight(rx - kmarg));

    // routes on the right, their labels up in the header row
    headerRuleEnd = rx - 4;
    auto lab = [&](auto &l, int x, int w)
    { l->setBounds(header.withX(x).withWidth(w).reduced(4, 0)); };
    lab(toF1, rx, group(2));
    lab(toF2, rx + group(2), group(2));
    lab(toRt, rx + 2 * group(2), group(5));

    auto x = rx;
    for (int i = 0; i < numRoutes; ++i)
    {
        if (i == 0 || i == 2 || i == 4)
            x += kmarg;
        routeK[i]->setBounds(knobs.withX(x).withWidth(kw));
        x += kw + kmarg;
    }
}
} // namespace baconpaul::twofilters::ui
