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
#include <cmath>
#include "sst/jucegui/components/BaseStyles.h"

namespace baconpaul::twofilters::ui
{

// fake input bursts with a follower drawn over them
struct EnvTrace : juce::Component
{
    EnvTrace(EnvFollowerPanel &p) : panel(p) {}

    void paint(juce::Graphics &g) override
    {
        namespace bst = sst::jucegui::components::base_styles;
        auto st = panel.style();
        auto gCol = st->getColour(bst::ValueGutter::styleClass, bst::ValueGutter::gutter);
        auto vCol = st->getColour(bst::ValueBearing::styleClass, bst::ValueBearing::value);
        auto lCol = st->getColour(bst::BaseLabel::styleClass, bst::BaseLabel::labelcolor);
        auto lFt = st->getFont(bst::BaseLabel::styleClass, bst::BaseLabel::labelfont);

        g.setColour(gCol);
        g.fillRect(getLocalBounds());

        auto w = getWidth();
        auto h = (float)getHeight();
        std::vector<float> in(w), fol(w);
        uint32_t seed{22};
        auto rnd = [&seed]()
        {
            seed = seed * 1664525u + 1013904223u;
            return (seed >> 8) / 16777216.f;
        };
        for (int x = 0; x < w; ++x)
        {
            auto ph = std::fmod(x * 4.0f / w, 1.0f);
            auto hit = std::exp(-ph * 6.f) * (x * 4 / w % 2 == 0 ? 1.f : 0.6f);
            in[x] = hit * (0.4f + 0.6f * rnd());
        }
        float f{0};
        for (int x = 0; x < w; ++x)
        {
            auto c = in[x] > f ? 0.35f : 0.04f;
            f += c * (in[x] - f);
            fol[x] = f;
        }

        g.setColour(lCol.withAlpha(0.35f));
        for (int x = 0; x < w; ++x)
            g.drawVerticalLine(x, h * (1 - in[x] * 0.9f), h);

        juce::Path p;
        p.startNewSubPath(0, h * (1 - fol[0] * 0.9f));
        for (int x = 1; x < w; ++x)
            p.lineTo(x, h * (1 - fol[x] * 0.9f));
        g.setColour(vCol);
        g.strokePath(p, juce::PathStrokeType(1.5f));

        g.setColour(lCol);
        g.setFont(lFt);
        g.drawText("Input / Follower (placeholder)", getLocalBounds().reduced(4, 2),
                   juce::Justification::topLeft);
    }

    EnvFollowerPanel &panel;
};

EnvFollowerPanel::EnvFollowerPanel(PluginEditor &e)
    : sst::jucegui::components::NamedPanel("Envelope Follower"), editor(e)
{
    auto mkKnob = [this](auto &k, auto &d, const std::string &lab, float v, float mn, float mx)
    {
        d = std::make_unique<MockContinuous>(lab, v, mn, mx);
        k = std::make_unique<sst::jucegui::components::Knob>();
        k->setSource(d.get());
        addAndMakeVisible(*k);
    };

    mkKnob(attack, attackD, "Attack", 0.2f, 0.f, 1.f);
    mkKnob(release, releaseD, "Release", 0.5f, 0.f, 1.f);

    auto mkMenu = [this](auto &m, const std::string &init, std::vector<std::string> choices)
    {
        m = std::make_unique<sst::jucegui::components::MenuButton>();
        m->setLabel(init);
        m->setOnCallback([this, b = m.get(), choices]() { showChoiceMenu(b, choices); });
        addAndMakeVisible(*m);
    };
    mkMenu(source, "Main", {"Main", "SideChain"});
    mkMenu(mode, "Mid", {"L", "R", "Mid", "Side"});

    level = std::make_unique<sst::jucegui::components::VUMeter>(
        sst::jucegui::components::VUMeter::HORIZONTAL);
    level->setLevels(0.55f, 0.55f);
    addAndMakeVisible(*level);

    trace = std::make_unique<EnvTrace>(*this);
    addAndMakeVisible(*trace);

    const char *names[numRoutes] = {"Cutoff", "Res", "Cutoff", "Res",  "Pre",
                                    "Blend",  "Mix", "F/Back", "Noise"};
    for (int i = 0; i < numRoutes; ++i)
        mkKnob(routeK[i], routeD[i], names[i], 0.f, -1.f, 1.f);

    auto mkLab = [this](auto &l, const std::string &t)
    {
        l = std::make_unique<sst::jucegui::components::RuledLabel>();
        l->setText(t);
        addAndMakeVisible(*l);
    };
    mkLab(toF1, "To Filter 1");
    mkLab(toF2, "To Filter 2");
    mkLab(toRt, "To Main");
}

EnvFollowerPanel::~EnvFollowerPanel() = default;

void EnvFollowerPanel::showChoiceMenu(sst::jucegui::components::MenuButton *b,
                                      const std::vector<std::string> &choices)
{
    juce::PopupMenu p;
    for (const auto &c : choices)
        p.addItem(c, true, b->getLabel() == c,
                  [w = juce::Component::SafePointer(b), c]()
                  {
                      if (w)
                          w->setLabel(c);
                  });
    p.showMenuAsync(juce::PopupMenu::Options().withParentComponent(&editor));
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
    attack->setBounds(knobs.withX(x0 + kmarg).withWidth(kw));
    release->setBounds(knobs.withX(x0 + kmarg + kw + kmarg).withWidth(kw));

    auto cA = knobs.withX(x0 + group(2) + 4).withWidth(chanW - 8);
    source->setBounds(cA.withHeight(22));
    mode->setBounds(cA.withHeight(22).translated(0, 25));
    level->setBounds(cA.withHeight(6).translated(0, 51));

    auto rx = ca.getRight() - routeW;
    auto mx = x0 + group(2) + chanW + kmarg;
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
