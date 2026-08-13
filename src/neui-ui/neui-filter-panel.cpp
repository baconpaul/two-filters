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

#include "neui-filter-panel.h"
#include "neui-steplfo-panel.h"
#include "neui-patch-bindings.h"
#include "neui-menus.h"
#include "neui-filter-plotter.h"

#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>

#include <fmt/core.h>

#include <sst/neuigui/components/BaseStyles.h>

namespace baconpaul::twofilters::ui
{

/*
 * The response plot. Same background-thread plotter as the juce FilterCurve;
 * the paint is direct rather than image-cached (neui repaints the whole
 * frame per invalidate anyway, so a cache buys nothing yet).
 */
struct NFilterCurve : npp::Component<NFilterCurve, npp::Paints, npp::MouseEvents>
{
    std::unique_ptr<std::thread> thread;
    std::mutex sendM, dataM;
    std::condition_variable sendCV;
    std::atomic<bool> running{true};
    int updateRequest{0};
    int repaintReq{0}, lastRepaintReq{0};

    NFilterCurve(npp::Parent p, NFilterPanel &pan) : Component(p), panel(pan)
    {
        thread = std::make_unique<std::thread>([this]() { run(); });
    }
    ~NFilterCurve()
    {
        running = false;
        {
            std::unique_lock<std::mutex> l(sendM);
            sendCV.notify_one();
        }
        thread->join();
    }

    void run()
    {
        int lur{0};
        auto lastWait = std::chrono::high_resolution_clock::now();

        while (running)
        {
            int nur{lur};
            {
                std::unique_lock<std::mutex> l(sendM);
                if (nur != updateRequest)
                {
                    nur = updateRequest;
                }
                else
                {
                    sendCV.wait(l);
                }
            }
            auto postLastWait = std::chrono::high_resolution_clock::now();
            auto dur =
                std::chrono::duration_cast<std::chrono::microseconds>(postLastWait - lastWait);
            auto atLeast = 1000000 / 60.0;

            if (dur.count() < atLeast)
            {
                std::this_thread::sleep_for(
                    std::chrono::microseconds((int)(atLeast - dur.count())));
            }
            if (running && nur != lur)
            {
                float lco, lre, lmo;
                sst::filtersplusplus::FilterModel lmodel;
                sst::filtersplusplus::ModelConfig lconfig;

                {
                    std::unique_lock<std::mutex> l(dataM);

                    nur = updateRequest;
                    lur = nur;

                    lco = co;
                    lre = res;
                    lmo = morph;
                    lmodel = snapModel;
                    lconfig = snapConfig;
                }
                if (lmodel == sst::filtersplusplus::FilterModel::None)
                {
                    {
                        std::unique_lock<std::mutex> l(dataM);
                        cX.clear();
                        cY.clear();
                        cX.push_back(0.5);
                        cX.push_back(5.0);
                        cY.push_back(0.0);
                        cY.push_back(0.0);

                        repaintReq++;
                    }
                }
                else
                {
                    if (sst::filtersplusplus::Filter::coefficientsExtraIsBipolar(lmodel, lconfig,
                                                                                 0))
                        lmo = lmo * 2 - 1;

                    auto par = NeuiFilterPlotParameters();
                    par.freqSmoothOctaves = 1.0 / 36.0;
                    auto crv = plotter.plotFilterMagnitudeResponse(lmodel, lconfig, lco, lre, lmo,
                                                                   0, 0, par);
                    auto tcX = crv.first;
                    for (auto &x : tcX)
                        x = (x > 0 ? log10(x) : 0);

                    {
                        std::unique_lock<std::mutex> l(dataM);
                        cX = tcX;
                        cY = crv.second;
                        repaintReq++;
                    }
                }
            }
            lastWait = std::chrono::high_resolution_clock::now();
        }
    }

    bool showDragEdit{false};
    void mouseEnter(const npp::MouseEvent & /*e*/) override
    {
        showDragEdit = true;
        repaint();
    }
    void mouseExit(const npp::MouseEvent & /*e*/) override
    {
        showDragEdit = false;
        repaint();
    }

    void positionToCoRes(npp::Point p)
    {
        auto b = localBounds();
        auto rs = 1 - std::clamp(p.y / b.getHeight(), 0.f, 1.f);

        auto lfr = std::clamp(p.x / b.getWidth(), 0.f, 1.f) / xsc - xoff;
        auto fr = pow(10.0, lfr);
        auto key = 12 * log2(fr / 440.0);

        panel.cutoffD->setValueFromGUI(key);
        panel.resonanceD->setValueFromGUI(rs);

        panel.cutoffK->repaint();
        panel.resonanceK->repaint();
    }
    void mouseDown(const npp::MouseEvent &e) override
    {
        panel.cutoffK->onBeginEdit();
        panel.resonanceK->onBeginEdit();
        positionToCoRes(e.position);
        repaint();
    }

    void mouseDrag(const npp::MouseEvent &e) override
    {
        positionToCoRes(e.position);
        repaint();
    }

    void mouseUp(const npp::MouseEvent & /*e*/) override
    {
        panel.resonanceK->onEndEdit();
        panel.cutoffK->onEndEdit();
    }

    void mouseRightButtonDown(const npp::MouseEvent &e) override { showContextMenu(e.position); }

    void showContextMenu(npp::Point at)
    {
        std::vector<npp::MenuItem> items;
        items.push_back(npp::MenuItem::makeHeader("Filter " + std::to_string(panel.instance + 1)));
        items.push_back(npp::MenuItem::makeSeparator());
        items.push_back(npp::MenuItem::makeEntry("Reset", [this]() { panel.resetFilter(); }));
        items.push_back(npp::MenuItem::makeEntry("Randomize", [this]() { panel.randomize(); }));
        items.push_back(npp::MenuItem::makeSeparator());
        items.push_back(npp::MenuItem::makeEntry("Swap Filters",
                                                 [this]() { panel.editor.swapFilters(false); }));
        items.push_back(npp::MenuItem::makeEntry("Swap Filters and Modulation",
                                                 [this]() { panel.editor.swapFilters(true); }));

        auto o = editorLocalOrigin(this, &panel.editor);
        panel.editor.showMenu(std::move(items), o + at);
    }

    float xsc = 1.0 / (log10(20000) - log10(6));
    float xoff = -log10(6);

    float dbMax{24}, dbMin{-48 - 12};
    float ysc = 1.0 / (dbMax - dbMin);
    float yoff = dbMax / (dbMax - dbMin);

    float tx(float x) const { return (xoff + x) * xsc * localBounds().getWidth(); };
    float ty(float y) const { return (yoff - y * ysc) * localBounds().getHeight(); };

    void paint(npp::Canvas &g) override
    {
        namespace bst = sst::neuigui::components::base_styles;
        auto b = g.bounds();
        auto W = b.getWidth();
        auto H = b.getHeight();

        std::unique_lock<std::mutex> l(dataM);

        g.fillAll(panel.style()->getColour(bst::ValueGutter::styleClass, bst::ValueGutter::gutter));
        auto olc = panel.style()->getColour(bst::Outlined::styleClass, bst::Outlined::outline);
        auto gridFont = npp::Font(sst::neuigui::style::fromJuceHeight(10.0f));
        for (int i = 1; i < 5; ++i)
        {
            g.drawLine({tx(i), 0}, {tx(i), H}, 1, olc);
            auto hz = pow(10.0, i);
            auto txt = fmt::format("{:.0f} Hz", hz);
            if (hz >= 1000)
                txt = fmt::format("{:.0f} kHz", hz / 1000);

            g.drawText(txt, npp::Rect{tx(i) + 2, H - 22, 100, 20}, gridFont, olc, npp::HAlign::left,
                       npp::VAlign::bottom);
        }

        for (float db = dbMax; db >= dbMin; db -= 24)
        {
            g.drawLine({0, ty(db)}, {W, ty(db)}, 1, olc);
            auto txt = fmt::format("{:.0f} dB", db);
            g.drawText(txt, npp::Rect{2, ty(db) + 2, 100, 20}, gridFont, olc, npp::HAlign::left,
                       npp::VAlign::top);
        }
        g.drawRect(b, 1, olc);

        if (!cX.empty() && !cY.empty())
        {
            auto vlc =
                panel.style()->getColour(bst::ValueBearing::styleClass, bst::ValueBearing::value);
            auto bolc =
                panel.style()->getColour(bst::BaseLabel::styleClass, bst::BaseLabel::labelcolor);

            auto my = std::min(cY[0], dbMin);
            for (int i = 1; i < (int)cX.size(); i++)
                if (cY[i] < my)
                    my = cY[i];

            // The filled area under the curve, as its own path with a
            // vertical gradient (FULL) or a flat wash (REDUCED)
            if (cX.size() > 10 && panel.editor.cpuGraphicsMode != NeuiPluginEditor::MINIMAL)
            {
                buildCurvePath(g, true, my);
                if (panel.editor.cpuGraphicsMode != NeuiPluginEditor::FULL)
                {
                    g.fillPath(vlc.withAlpha(0.2f));
                }
                else
                {
                    neui_gradient_stop_t stops[2] = {{0.0f, vlc.withAlpha(0.6f).argb()},
                                                     {1.0f, vlc.withAlpha(0.1f).argb()}};
                    neui_gradient_t grad{};
                    grad.kind = NEUI_GRADIENT_LINEAR;
                    grad.stops = stops;
                    grad.stop_count = 2;
                    grad.extend = NEUI_GRADIENT_EXTEND_CLAMP;
                    grad.start_x = 0;
                    grad.start_y = ty(6);
                    grad.end_x = 0;
                    grad.end_y = ty(-48);
                    if (g.rawApi()->fill_path_gradient)
                        g.rawApi()->fill_path_gradient(g.rawHandle(), &grad);
                    else
                        g.fillPath(vlc.withAlpha(0.3f));
                }
            }

            buildCurvePath(g, false, my);
            g.strokePath(1.5f, bolc);
        }

        if (showDragEdit)
        {
            drawCrosshairs(g);
        }
        if (!isEnabled())
        {
            g.fillAll(panel.style()
                          ->getColour(bst::ValueGutter::styleClass, bst::ValueGutter::gutter)
                          .withAlpha(0.5f));
        }
    }

    // Rebuilds the painter's current path from the curve data. Caller fills
    // or strokes it.
    void buildCurvePath(npp::Canvas &g, bool closedFill, float my)
    {
        g.beginPath();
        g.moveTo({tx(cX[0]), ty(cY[0])});
        auto lX = tx(cX[0]);
        for (int i = 1; i < (int)cX.size(); i++)
        {
            if (tx(cX[i]) - tx(lX) > 0.5)
            {
                lX = cX[i];
                g.lineTo({tx(cX[i]), ty(cY[i])});
            }
        }
        if (closedFill)
        {
            g.lineTo({tx(cX.back()), ty(my)});
            g.lineTo({tx(cX[0]), ty(my)});
            g.closePath();
        }
    }

    void drawCrosshairs(npp::Canvas &g)
    {
        auto &fn = panel.editor.patchMainRef.filterNodes[panel.instance];
        co = fn.cutoff;
        res = fn.resonance;

        auto freq = 440 * pow(2, co / 12.0);
        auto lfre = log10(freq);
        auto cx = tx(lfre);
        auto cy = float((1.0 - res) * localBounds().getHeight());

        namespace bst = sst::neuigui::components::base_styles;

        auto vlc = panel.style()->getColour(bst::BaseLabel::styleClass, bst::BaseLabel::labelcolor);

        g.drawLine({cx, 0}, {cx, localBounds().getHeight()}, 1, vlc);
        g.drawLine({0, cy}, {localBounds().getWidth(), cy}, 1, vlc);

        g.fillEllipse({cx - 3, cy - 3, 6, 6}, vlc);
    }

    int idleCount{0};
    void onIdle()
    {
        if (idleCount == 0)
        {
            bool rp{false};
            {
                std::unique_lock<std::mutex> l(dataM);
                rp = lastRepaintReq != repaintReq;
                lastRepaintReq = repaintReq;
            }
            if (rp)
            {
                repaint();
            }
        }
        idleCount++;
        if (idleCount > ((panel.editor.cpuGraphicsMode != NeuiPluginEditor::FULL) ? 3 : 0))
            idleCount = 0;
    }

    void rebuild()
    {
        std::unique_lock<std::mutex> l(sendM);
        updateRequest++;
        auto &fn = panel.editor.patchMainRef.filterNodes[panel.instance];
        co = fn.cutoff;
        res = fn.resonance;
        morph = fn.morph;
        snapModel = fn.model;
        snapConfig = fn.config;
        sendCV.notify_one();
    }

    float co, res, morph;
    sst::filtersplusplus::FilterModel snapModel{sst::filtersplusplus::FilterModel::None};
    sst::filtersplusplus::ModelConfig snapConfig{};

    NFilterPanel &panel;

    std::vector<float> cX, cY;

    NeuiFilterPlotter plotter{14};
};

NFilterPanel::NFilterPanel(npp::Parent p, NeuiPluginEditor &ed, int ins)
    : sngc::NamedPanelBase<NFilterPanel>(p, "Filter " + std::to_string(ins + 1)), editor(ed),
      instance(ins)
{
    auto &fn = editor.patchMainRef.filterNodes[instance];

    setTogglable(true);

    activeD = std::make_unique<PatchDiscrete>(editor, fn.active.meta.id);
    setToggleDataSource(activeD.get());
    toggleButton->onBeginEdit = [this, &fn]()
    { editor.mainToAudio.push({Engine::MainToAudioMsg::Action::BEGIN_EDIT, fn.active.meta.id}); };

    toggleButton->onEndEdit = [this, &fn]()
    { editor.mainToAudio.push({Engine::MainToAudioMsg::Action::END_EDIT, fn.active.meta.id}); };
    editor.componentRefreshByID[fn.active.meta.id] = [this]() { onModelChanged(); };

    activeD->onGuiSetValue = [this]() { onModelChanged(); };

    curve = &add<NFilterCurve>(*this);

    createComponent(editor, *this, fn.cutoff, cutoffK, cutoffD);
    cutoffD->onGuiSetValue = [this]() { curve->rebuild(); };
    cutoffD->labelOverride = "Cutoff";
    editor.componentRefreshByID[fn.cutoff.meta.id] = [this]() { curve->rebuild(); };

    createComponent(editor, *this, fn.resonance, resonanceK, resonanceD);
    resonanceD->labelOverride = "Res";
    editor.componentRefreshByID[fn.resonance.meta.id] = [this]() { curve->rebuild(); };
    resonanceD->onGuiSetValue = [this]() { curve->rebuild(); };

    createComponent(editor, *this, fn.morph, morphK, morphD);
    editor.componentRefreshByID[fn.morph.meta.id] = [this]() { curve->rebuild(); };
    morphD->onGuiSetValue = [this]() { curve->rebuild(); };
    morphD->labelOverride = "Morph";

    createComponent(editor, *this, fn.pan, panK, panD);
    panD->labelOverride = "Pan";

    modelMenu = &add<sngc::MenuButton>();
    modelMenu->setOnCallback([this]() { showModelMenu(); });
    modelMenu->setOnJogCallback([this](auto i) { jogModel(i); });

    configMenu = &add<sngc::MenuButton>();
    configMenu->setOnCallback([this]() { showConfigMenu(); });
    configMenu->setOnJogCallback([this](auto i) { jogConfig(i); });

    pbMenu = &add<sngc::MenuButton>();
    pbMenu->setOnCallback([this]() { showConfigStructuredMenu(0); });
    pbMenu->setVisible(false);

    slpMenu = &add<sngc::MenuButton>();
    slpMenu->setOnCallback([this]() { showConfigStructuredMenu(1); });
    slpMenu->setVisible(false);

    drvMenu = &add<sngc::MenuButton>();
    drvMenu->setOnCallback([this]() { showConfigStructuredMenu(2); });
    drvMenu->setVisible(false);

    fsmMenu = &add<sngc::MenuButton>();
    fsmMenu->setOnCallback([this]() { showConfigStructuredMenu(3); });
    fsmMenu->setVisible(false);
}

NFilterPanel::~NFilterPanel() = default;

void NFilterPanel::resized()
{
    layoutHeaderControls();
    if (!curve)
        return;

    auto ourHeight = localBounds().getHeight();
    auto plotH = 190 - (300 - ourHeight);

    auto b = getContentArea().withHeight(plotH);
    curve->setBounds(b);

    auto rs = getContentArea().withTrimmedTop(plotH + 5);
    auto q = 62.0f;
    auto pad = 3.0f;

    auto bk = rs.withWidth(q).withTrimmedLeft(5).withTrimmedRight(5);
    cutoffK->setBounds(bk);
    resonanceK->setBounds(bk.translated(q + pad, 0));
    morphK->setBounds(bk.translated(2 * q + 2 * pad, 0));
    panK->setBounds(bk.translated(3 * q + 3 * pad, 0));

    auto rest = rs.withTrimmedLeft(4 * q + 4 * pad);
    modelMenu->setBounds(rest.withHeight(20));
    configMenu->setBounds(rest.withHeight(20).translated(0, 22));
    auto bx = rest.translated(0, 22).withHeight(20);
    auto bxw = bx.getWidth() / 2;
    pbMenu->setBounds(bx.withTrimmedRight(bxw - 2));
    slpMenu->setBounds(bx.withTrimmedLeft(bxw + 2));
    bx = bx.translated(0, 22);
    drvMenu->setBounds(bx.withTrimmedRight(bxw - 2));
    fsmMenu->setBounds(bx.withTrimmedLeft(bxw + 2));
}

template <typename E> void setSub(auto &a, auto &m, E val)
{
    namespace sfpp = sst::filtersplusplus;

    if (sfpp::supportsChoice<E>(m))
    {
        if (val == E::UNSUPPORTED)
        {
            a->setLabel("-");
        }
        else
        {
            a->setLabel(sfpp::toString(val));
        }
        a->setEnabled(true);
    }
    else
    {
        a->setLabel("");
        a->setEnabled(false);
    }
}

void NFilterPanel::onModelChanged()
{
    displayMode = (NeuiPluginEditor::ConfigDisplayMode)editor.defaultsProvider->getUserDefaultValue(
        Defaults::modelConfigMode, NeuiPluginEditor::ConfigDisplayMode::SINGLE_LIST);

    namespace sfpp = sst::filtersplusplus;

    auto &fn = editor.patchMainRef.filterNodes[instance];
    auto mn = sfpp::toString(fn.model);
    auto cn = fn.config.toString();

    auto xtra = sst::filtersplusplus::Filter::coefficientsExtraCount(fn.model, fn.config);
    morphK->setEnabled(xtra > 0);

    auto tl = mn + " " + cn;
    setName("Filter " + std::to_string(instance + 1) + ": " + tl);
    curve->rebuild();

    modelMenu->setLabel(mn);
    configMenu->setLabel(cn);

    setSub(pbMenu, fn.model, fn.config.pt);
    setSub(slpMenu, fn.model, fn.config.st);
    setSub(drvMenu, fn.model, fn.config.dt);
    setSub(fsmMenu, fn.model, fn.config.mt);

    switch (displayMode)
    {
    case NeuiPluginEditor::SINGLE_LIST:
        pbMenu->setVisible(false);
        slpMenu->setVisible(false);
        drvMenu->setVisible(false);
        fsmMenu->setVisible(false);
        configMenu->setVisible(true);
        break;
    case NeuiPluginEditor::FOUR_ALL:
        pbMenu->setVisible(true);
        slpMenu->setVisible(true);
        drvMenu->setVisible(true);
        fsmMenu->setVisible(true);
        configMenu->setVisible(false);
        break;
    case NeuiPluginEditor::FOUR_HIDE:
        updateFourHideMenuVisibility();
        configMenu->setVisible(false);
        break;
    }

    auto act = fn.active > 0.5;
    cutoffK->setEnabled(act);
    resonanceK->setEnabled(act);
    morphK->setEnabled(act && (xtra > 0));
    modelMenu->setEnabled(act);
    configMenu->setEnabled(act);
    curve->setEnabled(act);
    repaint();
}

void NFilterPanel::updateFourHideMenuVisibility()
{
    if (displayMode != NeuiPluginEditor::FOUR_HIDE)
        return;

    namespace sfpp = sst::filtersplusplus;
    auto &fn = editor.patchMainRef.filterNodes[instance];

    pbMenu->setVisible(sfpp::supportsChoice<sfpp::Passband>(fn.model));
    slpMenu->setVisible(sfpp::supportsChoice<sfpp::Slope>(fn.model) &&
                        !sfpp::noChoicesOrOnlyUnsupported<sfpp::Slope>(fn.model, fn.config.pt));
    drvMenu->setVisible(
        sfpp::supportsChoice<sfpp::DriveMode>(fn.model) &&
        !sfpp::noChoicesOrOnlyUnsupported<sfpp::DriveMode>(fn.model, fn.config.pt, fn.config.st));
    fsmMenu->setVisible(sfpp::supportsChoice<sfpp::FilterSubModel>(fn.model) &&
                        !sfpp::noChoicesOrOnlyUnsupported<sfpp::FilterSubModel>(
                            fn.model, fn.config.pt, fn.config.st, fn.config.dt));
}

void NFilterPanel::showModelMenu()
{
    std::vector<npp::MenuItem> items;
    items.push_back(npp::MenuItem::makeHeader("Filter Models"));
    items.push_back(npp::MenuItem::makeSeparator());

    namespace sfpp = sst::filtersplusplus;

    auto curr = editor.patchMainRef.filterNodes[instance].model;
    for (auto &m : sfpp::Filter::availableModels())
    {
        items.push_back(npp::MenuItem::makeToggle(
            sfpp::toString(m), m == curr,
            [this, m]()
            {
                auto configs = sfpp::Filter::availableModelConfigurations(m, true);
                editor.patchMainRef.filterNodes[instance].model = m;
                if (configs.empty())
                    editor.patchMainRef.filterNodes[instance].config = {};
                else
                    editor.patchMainRef.filterNodes[instance].config = configs.front();
                editor.pushFilterSetup(instance);
                onModelChanged();
                for (auto &s : editor.stepLFOPanel)
                    s->onModelChanged();
            }));
    }

    items.push_back(npp::MenuItem::makeSeparator());
    items.push_back(
        npp::MenuItem::makeSubmenu("Configuration UI", editor.configDisplayMenuItems()));

    auto o = editorLocalOrigin(modelMenu, &editor);
    editor.showMenu(std::move(items), {o.x, o.y + modelMenu->bounds().getHeight()});
}

void NFilterPanel::showConfigMenu()
{
    std::vector<npp::MenuItem> items;
    items.push_back(npp::MenuItem::makeHeader("Filter Models"));
    items.push_back(npp::MenuItem::makeSeparator());

    namespace sfpp = sst::filtersplusplus;

    auto cfgs = sfpp::Filter::availableModelConfigurations(
        editor.patchMainRef.filterNodes[instance].model, true);

    auto o = editorLocalOrigin(configMenu, &editor);
    auto at = npp::Point{o.x, o.y + configMenu->bounds().getHeight()};

    if (cfgs.empty() || (cfgs.size() == 1 && cfgs[0] == sst::filtersplusplus::ModelConfig()))
    {
        items.push_back(npp::MenuItem::makeHeader("No Sub-Configurations"));
        editor.showMenu(std::move(items), at);
        return;
    }

    std::map<sst::filtersplusplus::Passband, int> countByBand;
    for (const auto &c : cfgs)
    {
        countByBand[c.pt]++;
    }

    auto currConf = editor.patchMainRef.filterNodes[instance].config;
    auto priorPassType = cfgs[0].pt;
    for (auto &c : cfgs)
    {
        if (c.pt != priorPassType && (countByBand[c.pt] > 1 || countByBand[priorPassType] > 1))
        {
            priorPassType = c.pt;
            items.push_back(npp::MenuItem::makeSeparator());
        }
        items.push_back(
            npp::MenuItem::makeToggle(c.toString(), c == currConf,
                                      [this, c]()
                                      {
                                          editor.patchMainRef.filterNodes[instance].config = c;
                                          editor.pushFilterSetup(instance);
                                          onModelChanged();
                                          for (auto &s : editor.stepLFOPanel)
                                              s->onModelChanged();
                                      }));
    }

    editor.showMenu(std::move(items), at);
}

void NFilterPanel::jogConfig(int dir)
{
    namespace sfpp = sst::filtersplusplus;
    auto &fn = editor.patchMainRef.filterNodes[instance];

    auto am = sfpp::Filter::availableModelConfigurations(fn.model, true);
    if (am.empty())
        return;
    int cm{-1};
    auto currConf = editor.patchMainRef.filterNodes[instance].config;
    for (int i = 0; i < (int)am.size(); ++i)
    {
        if (am[i] == currConf)
        {
            cm = i;
            break;
        }
    }

    cm = cm + dir;
    if (cm < 0)
        cm = am.size() - 1;
    if (cm >= (int)am.size())
        cm = 0;
    auto newConf = am[cm];
    editor.patchMainRef.filterNodes[instance].config = newConf;
    editor.pushFilterSetup(instance);
    onModelChanged();
}

void NFilterPanel::jogModel(int dir)
{
    namespace sfpp = sst::filtersplusplus;
    auto am = sfpp::Filter::availableModels();
    int cm{-1};
    auto currMod = editor.patchMainRef.filterNodes[instance].model;
    for (int i = 0; i < (int)am.size(); ++i)
    {
        if (am[i] == currMod)
        {
            cm = i;
            break;
        }
    }

    cm = cm + dir;
    if (cm < 0)
        cm = am.size() - 1;
    if (cm >= (int)am.size())
        cm = 0;
    auto newMod = am[cm];
    editor.patchMainRef.filterNodes[instance].model = newMod;

    auto configs = sfpp::Filter::availableModelConfigurations(newMod, true);
    if (configs.empty())
        editor.patchMainRef.filterNodes[instance].config = {};
    else
        editor.patchMainRef.filterNodes[instance].config = configs.front();
    editor.pushFilterSetup(instance);
    onModelChanged();
}

void NFilterPanel::onIdle() { curve->onIdle(); }

void NFilterPanel::endEdit(int /*id*/) { curve->rebuild(); }

void NFilterPanel::randomize()
{
    auto &fn = editor.patchMainRef.filterNodes[instance];
    auto wr = [&, this](auto &par, auto &cont, auto &wid)
    {
        auto range = par.meta.maxVal - par.meta.minVal;
        auto nv = editor.rng.unif01() * range + par.meta.minVal;
        wid->onBeginEdit();
        cont->setValueFromGUI(nv);
        wid->onEndEdit();
    };

    namespace sfpp = sst::filtersplusplus;
    auto mods = sfpp::Filter::availableModels();
    // 1 since we dont want off
    auto &rng = editor.rng;
    auto mod = mods[std::clamp(rng.unifInt(1, (int)mods.size()), 1, (int)mods.size() - 1)];
    fn.model = mod;

    auto am = sfpp::Filter::availableModelConfigurations(fn.model, true);
    if (!am.empty())
        fn.config = am[std::clamp(rng.unifInt(0, (int)am.size()), 0, (int)am.size() - 1)];

    wr(fn.cutoff, cutoffD, cutoffK);
    wr(fn.resonance, resonanceD, resonanceK);
    wr(fn.morph, morphD, morphK);
    wr(fn.pan, panD, panK);

    editor.pushFilterSetup(instance);
    onModelChanged();
    repaint();
}

void NFilterPanel::resetFilter()
{
    namespace sfpp = sst::filtersplusplus;
    auto &fn = editor.patchMainRef.filterNodes[instance];

    fn.model = sfpp::FilterModel::None;
    fn.config = {};

    auto wr = [&, this](auto &dat, auto &wid)
    {
        wid->onBeginEdit();
        dat->setValueFromGUI(dat->getDefaultValue());
        wid->onEndEdit();
    };

    wr(cutoffD, cutoffK);
    wr(resonanceD, resonanceK);
    wr(morphD, morphK);
    wr(panD, panK);

    editor.pushFilterSetup(instance);
    onModelChanged();
    repaint();
}

void NFilterPanel::showConfigStructuredMenu(int component)
{
    namespace sfpp = sst::filtersplusplus;

    std::vector<npp::MenuItem> items;

    sngc::MenuButton *anchor{nullptr};
    switch (component)
    {
    case 0:
        addConfigStructuredMenu<sfpp::Passband>(items, "Passband");
        anchor = pbMenu;
        break;

    case 1:
        addConfigStructuredMenu<sfpp::Slope>(items, "Slope");
        anchor = slpMenu;
        break;

    case 2:
        addConfigStructuredMenu<sfpp::DriveMode>(items, "Drive Mode");
        anchor = drvMenu;
        break;

    case 3:
        addConfigStructuredMenu<sfpp::FilterSubModel>(items, "Sub Model");
        anchor = fsmMenu;
        break;
    }
    auto o = editorLocalOrigin(anchor, &editor);
    editor.showMenu(std::move(items), {o.x, o.y + anchor->bounds().getHeight()});
}

template <typename E>
void NFilterPanel::addConfigStructuredMenu(std::vector<npp::MenuItem> &items, const std::string &sh)
{
    namespace sfpp = sst::filtersplusplus;
    auto &fn = editor.patchMainRef.filterNodes[instance];

    auto applyCf = [fn, this](const auto &cfc)
    {
        auto nc = sfpp::closestValidModelTo(fn.model, cfc);

        editor.patchMainRef.filterNodes[instance].config = nc;
        editor.pushFilterSetup(instance);
        onModelChanged();
        for (auto &s : editor.stepLFOPanel)
            s->onModelChanged();
        updateFourHideMenuVisibility();
    };

    items.push_back(npp::MenuItem::makeHeader(sh));
    items.push_back(npp::MenuItem::makeSeparator());

    std::vector<std::pair<E, bool>> opts;
    if constexpr (std::is_same_v<E, sfpp::Passband>)
    {
        opts = sfpp::valuesAndValidityForPartialConfig<E>(fn.model);
    }
    if constexpr (std::is_same_v<E, sfpp::Slope>)
    {
        opts = sfpp::valuesAndValidityForPartialConfig<E>(fn.model, fn.config.pt);
    }
    if constexpr (std::is_same_v<E, sfpp::DriveMode>)
    {
        opts = sfpp::valuesAndValidityForPartialConfig<E>(fn.model, fn.config.pt, fn.config.st);
    }
    if constexpr (std::is_same_v<E, sfpp::FilterSubModel>)
    {
        opts = sfpp::valuesAndValidityForPartialConfig<E>(fn.model, fn.config.pt, fn.config.st,
                                                          fn.config.dt);
    }
    for (auto &[o, active] : opts)
    {
        if (displayMode == NeuiPluginEditor::FOUR_HIDE && !active)
            continue;
        auto m = sfpp::toString(o);
        if (o == E::UNSUPPORTED)
            m = "None";
        auto item = npp::MenuItem::makeToggle(
            m, o == sfpp::get<E>(fn.config),
            [applyCf, fn, val = o]()
            {
                auto cfc = fn.config;
                sfpp::set<E>(cfc, val);
                applyCf(cfc);
            },
            active);
        items.push_back(item);
    }
}

} // namespace baconpaul::twofilters::ui
