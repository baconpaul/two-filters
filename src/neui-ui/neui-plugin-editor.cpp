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

#include "neui-plugin-editor.h"
#include "neui-filter-panel.h"
#include "neui-routing-panel.h"
#include "neui-steplfo-panel.h"
#include "neui-patch-bindings.h"
#include "neui-preset-data-binding.h"
#include "neui-menus.h"

#include <fmt/core.h>

#include <sst/neuigui/style/StyleSheet.h>
#include <sst/plugininfra/version_information.h>

#include "configuration.h"

namespace baconpaul::twofilters::ui
{
namespace nstl = sst::neuigui::style;

using sheet_t = nstl::StyleSheet;
static constexpr sheet_t::Class PatchMenu("twofilters.patch-menu");

NeuiPluginEditor::NeuiPluginEditor(npp::Parent p, Patch &patchMain,
                                   Engine::audioToMainQueue_t &atou,
                                   Engine::mainToAudioQueue_T &utoa,
                                   std::atomic<bool> &editorActiveIn,
                                   std::atomic<uint32_t> &uiForceRebuildIn, const clap_host_t *h)
    : sngc::WindowPanelBase<NeuiPluginEditor>(p), patchMainRef(patchMain), audioToMain(atou),
      mainToAudio(utoa), editorActive(editorActiveIn), uiForceRebuild(uiForceRebuildIn), clapHost(h)
{
    lastForceRebuild = uiForceRebuild.load();
    setAccessibleName("Two Filters");

    nstl::StyleSheet::initializeStyleSheets(
        []()
        {
            static bool once{false};
            if (!once)
            {
                once = true;
                sheet_t::addClass(PatchMenu).withBaseClass(
                    sngc::JogUpDownButton::Styles::styleClass);
            }
        });

    setStyle(nstl::StyleSheet::getBuiltInStyleSheet(nstl::StyleSheet::DARK));

    style()->setFont(PatchMenu, sngc::JogUpDownButton::Styles::labelfont,
                     style()
                         ->getFont(sngc::JogUpDownButton::Styles::styleClass,
                                   sngc::JogUpDownButton::Styles::labelfont)
                         .withSize(nstl::fromJuceHeight(18)));

    routingPanel = &add<NRoutingPanel>(*this);

    for (int i = 0; i < numFilters; ++i)
    {
        filterPanel[i] = &add<NFilterPanel>(*this, i);
    }

    for (int i = 0; i < numStepLFOs; ++i)
    {
        stepLFOPanel[i] = &add<NStepLFOPanel>(*this, i);
    }

    // Idle owns draining audioToMain while the editor is open.
    editorActive = true;

    presetManager = std::make_unique<presets::PresetManager>(clapHost);
    presetManager->onPresetLoaded = [this](auto s)
    {
        this->postPatchChange(s);
        repaint();
    };

    presetDataBinding =
        std::make_unique<PresetDataBinding>(*presetManager, patchMainRef, mainToAudio);
    presetDataBinding->setStateForDisplayName(patchMainRef.name);

    presetButton = &add<sngc::JogUpDownButton>();
    presetButton->setCustomClass(PatchMenu);
    presetButton->setSource(presetDataBinding.get());
    presetButton->onPopupTrigger = [this]() { showPresetPopup(); };
    setPatchNameDisplay();

    defaultsProvider = std::make_unique<defaultsProvider_t>(presetManager->userPath, "TwoFilters",
                                                            defaultName, [](auto e, auto b)
                                                            { SQLOG("[ERROR]" << e << " " << b); });

    cpuGraphicsMode = (NeuiPluginEditor::GraphicsMode)defaultsProvider->getUserDefaultValue(
        Defaults::useLowCpuGraphics, NeuiPluginEditor::FULL);

    setSkinFromDefaults();

    vuMeter = &add<sngc::VUMeter>(sngc::VUMeter::HORIZONTAL);

    mainToAudio.push({Engine::MainToAudioMsg::REQUEST_NON_PATCH_STATE, true});
    requestParamsFlush();

    resetEnablement();
}

NeuiPluginEditor::~NeuiPluginEditor()
{
    // Hand draining of audioToMain back to onMainThread.
    editorActive = false;
    if (clapHost)
        clapHost->request_callback(clapHost);
}

void NeuiPluginEditor::resized()
{
    float presetHeight{33}, footerHeight{15};

    auto lb = localBounds();
    auto presetArea = lb.withHeight(presetHeight);
    auto panelArea = lb.withTrimmedTop(presetHeight).withTrimmedBottom(footerHeight);

    float panelMargin{2};
    float uicMargin{4};
    // Preset button
    auto but = presetArea.reduced(191, 0).withTrimmedTop(uicMargin);
    presetButton->setBounds(but);

    but = npp::Rect{but.getRight() + uicMargin + 20, but.getY(),
                    lb.getWidth() - uicMargin - (but.getRight() + uicMargin + 20), but.getHeight()};
    vuMeter->setBounds(but);

    static constexpr float routingWidth{100};
    auto ra = panelArea.withWidth(routingWidth);
    routingPanel->setBounds(ra.reduced(panelMargin));

    panelArea = panelArea.withTrimmedLeft(routingWidth).withTrimmedRight(3);

    auto fH = 290.0f;
    auto fa = panelArea.withHeight(fH).withWidth(panelArea.getWidth() / 2);
    filterPanel[0]->setBounds(fa.reduced(panelMargin));
    filterPanel[1]->setBounds(fa.translated(fa.getWidth(), 0).reduced(panelMargin));

    auto ma = panelArea.withTrimmedTop(fH);

    auto sp = ma.withWidth(ma.getWidth() / 2);
    stepLFOPanel[0]->setBounds(sp.reduced(panelMargin));
    sp = sp.translated(sp.getWidth(), 0);
    stepLFOPanel[1]->setBounds(sp.reduced(panelMargin));
}

void NeuiPluginEditor::paint(npp::Canvas &g)
{
    sngc::WindowPanelBase<NeuiPluginEditor>::paint(g);

    auto b = g.bounds();
    auto ft = style()->getFont(sngc::Label::Styles::styleClass, sngc::Label::Styles::labelfont);

    float ht = 30;
    float np = 121;

    auto dimText = npp::Color::rgb(0xFF, 0xFF, 0xFF).withAlpha(0.5f);
    auto footer = b.reduced(3, 3);
    g.drawText(PRODUCT_NAME, footer, ft.withSize(nstl::fromJuceHeight(12)), dimText, npp::HAlign::left,
               npp::VAlign::bottom);

    std::string os = "";
#if defined(__APPLE__)
    os = "macOS";
#elif defined(_WIN32)
    os = "Windows";
#else
    os = "Linux";
#endif

    auto bi = os + " " + std::string(sst::plugininfra::VersionInformation::git_commit_hash) +
              fmt::format(" @ {:.1f}k", sampleRate / 1000.0);
    g.drawText(bi, footer, ft.withSize(nstl::fromJuceHeight(12)), dimText, npp::HAlign::right, npp::VAlign::bottom);

    g.drawText(sst::plugininfra::VersionInformation::git_implied_display_version, footer,
               ft.withSize(nstl::fromJuceHeight(12)), dimText, npp::HAlign::centre, npp::VAlign::bottom);

    auto dr = npp::Rect{0, 0, np, ht};
    g.drawText(PRODUCT_NAME, dr.reduced(2, 2), ft.withSize(nstl::fromJuceHeight(25)), npp::Color::rgb(0xFF, 0xFF, 0xFF),
               npp::HAlign::left, npp::VAlign::middle);

#if !defined(NDEBUG) || !NDEBUG
    g.drawText("DEBUG", dr.translated(1, 1), ft.withSize(nstl::fromJuceHeight(30)),
               npp::Color::rgb(0xFF, 0xFF, 0xFF).withAlpha(0.6f), npp::HAlign::centre,
               npp::VAlign::middle);
    g.drawText("DEBUG", dr, ft.withSize(nstl::fromJuceHeight(30)), npp::Color::rgb(0xFF, 0, 0).withAlpha(0.6f),
               npp::HAlign::centre, npp::VAlign::middle);
#endif
}

void NeuiPluginEditor::idle()
{
    // An out-of-band load (host stateLoad) wrote patchMain directly and bumped the counter.
    auto fr = uiForceRebuild.load();
    if (fr != lastForceRebuild)
    {
        lastForceRebuild = fr;
        rebuildFromPatchMain();
    }

    auto aum = audioToMain.pop();
    while (aum.has_value())
    {
        if (Engine::handleAudioToMainMessage(patchMainRef, *aum))
        {
            auto xit = componentRefreshByID.find(aum->paramId);
            if (xit != componentRefreshByID.end())
                xit->second();
            auto rit = componentRepaintByID.find(aum->paramId);
            if (rit != componentRepaintByID.end())
                rit->second();
        }
        else if (aum->action == Engine::AudioToMainMsg::UPDATE_VU)
        {
            vuMeter->setLevels(aum->value, aum->value2);
        }
        else if (aum->action == Engine::AudioToMainMsg::SEND_SAMPLE_RATE)
        {
            sampleRate = aum->value;
            repaint();
        }
        else if (aum->action == Engine::AudioToMainMsg::UPDATE_LFOSTEP)
        {
            if (aum->paramId == 0)
            {
                stepLFOPanel[0]->setCurrentStep(aum->value);
                stepLFOPanel[1]->setCurrentStep(aum->value2);
            }
            if (aum->paramId == 1)
            {
                stepLFOPanel[0]->setCurrentPhase(aum->value);
                stepLFOPanel[1]->setCurrentPhase(aum->value2);
            }
            if (aum->paramId == 2)
            {
                stepLFOPanel[0]->setCurrentLevel(aum->value);
                stepLFOPanel[1]->setCurrentLevel(aum->value2);
            }
        }
        else
        {
            SQLOG("Ignored patch message " << aum->action);
        }
        aum = audioToMain.pop();
    }

    for (auto &f : filterPanel)
        f->onIdle();
}

void NeuiPluginEditor::showMenu(std::vector<npp::MenuItem> items, npp::Point at)
{
    if (menu)
        menu->show(std::move(items), at);
}

void NeuiPluginEditor::popupMenuForContinuous(PatchContinuous *c, npp::Point at)
{
    if (!c)
        return;

    std::vector<npp::MenuItem> items;
    items.push_back(npp::MenuItem::makeHeader(c->getLabel()));
    items.push_back(npp::MenuItem::makeSeparator());
    items.push_back(npp::MenuItem::makeTypeIn(
        "Value", c->getValueAsString(),
        [this, c](const std::string &s)
        {
            mainToAudio.push({Engine::MainToAudioMsg::Action::BEGIN_EDIT, c->pid});
            if (s.empty())
                c->setValueFromGUI(c->getDefaultValue());
            else
                c->setValueAsString(s);
            mainToAudio.push({Engine::MainToAudioMsg::Action::END_EDIT, c->pid});
            repaint();
        }));
    items.push_back(npp::MenuItem::makeSeparator());
    items.push_back(npp::MenuItem::makeEntry("Set to Default",
                                             [this, c]()
                                             {
                                                 c->setValueFromGUI(c->getDefaultValue());
                                                 repaint();
                                             }));

    showMenu(std::move(items), at);
}

std::vector<npp::MenuItem> NeuiPluginEditor::configDisplayMenuItems()
{
    auto m = defaultsProvider->getUserDefaultValue(Defaults::modelConfigMode, SINGLE_LIST);

    std::vector<npp::MenuItem> items;
    auto setMode = [this](ConfigDisplayMode md)
    {
        return [this, md]()
        {
            defaultsProvider->updateUserDefaultValue(Defaults::modelConfigMode, md);
            for (auto &f : filterPanel)
                f->onModelChanged();
            repaint();
        };
    };
    items.push_back(
        npp::MenuItem::makeToggle("Single List", m == SINGLE_LIST, setMode(SINGLE_LIST)));
    items.push_back(npp::MenuItem::makeToggle("Four Menus", m == FOUR_ALL, setMode(FOUR_ALL)));
    items.push_back(npp::MenuItem::makeToggle("Four Menus, Hiding Unsupported", m == FOUR_HIDE,
                                              setMode(FOUR_HIDE)));
    return items;
}

void NeuiPluginEditor::showPresetPopup()
{
    std::vector<npp::MenuItem> items;
    items.push_back(npp::MenuItem::makeHeader("Main Menu"));
    items.push_back(npp::MenuItem::makeSeparator());

    std::vector<npp::MenuItem> factory;
    for (auto &[c, ent] : presetManager->factoryPatchNames)
    {
        std::vector<npp::MenuItem> em;
        for (auto &e : ent)
        {
            auto noExt = e;
            auto ps = noExt.find(PATCH_EXTENSION);
            if (ps != std::string::npos)
            {
                noExt = noExt.substr(0, ps);
            }
            em.push_back(npp::MenuItem::makeEntry(
                noExt, [cat = c, pat = e, this]()
                { this->presetManager->loadFactoryPreset(patchMainRef, mainToAudio, cat, pat); }));
        }
        factory.push_back(npp::MenuItem::makeSubmenu(c, std::move(em)));
    }
    items.push_back(npp::MenuItem::makeSubmenu("Factory Presets", std::move(factory)));

    std::vector<npp::MenuItem> user;
    auto cat = fs::path();
    std::vector<npp::MenuItem> s;
    for (const auto &up : presetManager->userPatches)
    {
        auto pp = up.parent_path();
        auto dn = up.filename().replace_extension("").u8string();
        auto loader = [this, pth = up]()
        {
            presetManager->loadUserPresetDirect(patchMainRef, mainToAudio,
                                                presetManager->userPatchesPath / pth);
        };
        if (pp.empty())
        {
            user.push_back(npp::MenuItem::makeEntry(dn, loader));
        }
        else
        {
            if (pp != cat)
            {
                if (cat.empty())
                {
                    user.push_back(npp::MenuItem::makeSeparator());
                }
                if (!s.empty())
                {
                    user.push_back(npp::MenuItem::makeSubmenu(cat.u8string(), std::move(s)));
                    s = {};
                }
                cat = pp;
            }
            s.push_back(npp::MenuItem::makeEntry(dn, loader));
        }
    }
    if (!s.empty() && !cat.empty())
    {
        user.push_back(npp::MenuItem::makeSubmenu(cat.u8string(), std::move(s)));
    }
    items.push_back(npp::MenuItem::makeSubmenu("User Presets", std::move(user)));
    items.push_back(npp::MenuItem::makeSeparator());

    items.push_back(npp::MenuItem::makeEntry("Reset to Init", [this]() { resetToDefault(); }));

    std::vector<npp::MenuItem> rsetm;
    rsetm.push_back(
        npp::MenuItem::makeEntry("Filter 1", [this]() { filterPanel[0]->resetFilter(); }));
    rsetm.push_back(
        npp::MenuItem::makeEntry("Filter 2", [this]() { filterPanel[1]->resetFilter(); }));
    rsetm.push_back(npp::MenuItem::makeSeparator());
    rsetm.push_back(
        npp::MenuItem::makeEntry("LFO 1 Steps", [this]() { stepLFOPanel[0]->resetSteps(); }));
    rsetm.push_back(
        npp::MenuItem::makeEntry("LFO 1 Routes", [this]() { stepLFOPanel[0]->resetRoutes(); }));
    rsetm.push_back(
        npp::MenuItem::makeEntry("LFO 2 Steps", [this]() { stepLFOPanel[1]->resetSteps(); }));
    rsetm.push_back(
        npp::MenuItem::makeEntry("LFO 2 Routes", [this]() { stepLFOPanel[1]->resetRoutes(); }));
    items.push_back(npp::MenuItem::makeSubmenu("Reset", std::move(rsetm)));

    std::vector<npp::MenuItem> rsm;
    rsm.push_back(npp::MenuItem::makeEntry("Filter 1", [this]() { filterPanel[0]->randomize(); }));
    rsm.push_back(npp::MenuItem::makeEntry("Filter 2", [this]() { filterPanel[1]->randomize(); }));
    rsm.push_back(npp::MenuItem::makeSeparator());
    rsm.push_back(
        npp::MenuItem::makeEntry("LFO 1 Steps", [this]() { stepLFOPanel[0]->randomizeSteps(); }));
    rsm.push_back(
        npp::MenuItem::makeEntry("LFO 1 Routes", [this]() { stepLFOPanel[0]->randomizeRoutes(); }));
    rsm.push_back(
        npp::MenuItem::makeEntry("LFO 2 Steps", [this]() { stepLFOPanel[1]->randomizeSteps(); }));
    rsm.push_back(
        npp::MenuItem::makeEntry("LFO 2 Routes", [this]() { stepLFOPanel[1]->randomizeRoutes(); }));
    rsm.push_back(npp::MenuItem::makeSeparator());
    rsm.push_back(npp::MenuItem::makeEntry("Main", [this]() { routingPanel->randomize(); }));
    rsm.push_back(npp::MenuItem::makeSeparator());
    rsm.push_back(npp::MenuItem::makeEntry("Everything",
                                           [this]()
                                           {
                                               filterPanel[0]->randomize();
                                               filterPanel[1]->randomize();
                                               stepLFOPanel[0]->randomize();
                                               stepLFOPanel[1]->randomize();
                                               routingPanel->randomize();
                                           }));
    items.push_back(npp::MenuItem::makeSubmenu("Randomize", std::move(rsm)));

    items.push_back(npp::MenuItem::makeSeparator());
    items.push_back(npp::MenuItem::makeEntry("Load Patch", [this]() { doLoadPatch(); }));
    items.push_back(npp::MenuItem::makeEntry("Save Patch", [this]() { doSavePatch(); }));
    items.push_back(npp::MenuItem::makeSeparator());

    std::vector<npp::MenuItem> uim;
    auto isLight = defaultsProvider->getUserDefaultValue(Defaults::useLightSkin, 0);

    auto lcg = (NeuiPluginEditor::GraphicsMode)defaultsProvider->getUserDefaultValue(
        Defaults::useLowCpuGraphics, NeuiPluginEditor::GraphicsMode::FULL);

    for (auto [mode, nm] :
         {std::make_pair(NeuiPluginEditor::GraphicsMode::FULL, std::string("Full")),
          {NeuiPluginEditor::GraphicsMode::REDUCES, "Reduced"},
          {NeuiPluginEditor::GraphicsMode::MINIMAL, "Minimal"}})
    {
        uim.push_back(npp::MenuItem::makeToggle("Use " + nm + " Graphics", lcg == mode,
                                                [this, um = mode]()
                                                {
                                                    defaultsProvider->updateUserDefaultValue(
                                                        Defaults::useLowCpuGraphics, um);
                                                    cpuGraphicsMode = um;
                                                    resetEnablement();
                                                    repaint();
                                                }));
    }
    uim.push_back(npp::MenuItem::makeSeparator());
    uim.push_back(npp::MenuItem::makeToggle("Dark Mode", !isLight,
                                            [this]()
                                            {
                                                defaultsProvider->updateUserDefaultValue(
                                                    Defaults::useLightSkin, false);
                                                setSkinFromDefaults();
                                            }));
    uim.push_back(npp::MenuItem::makeToggle("Light Mode", isLight,
                                            [this]()
                                            {
                                                defaultsProvider->updateUserDefaultValue(
                                                    Defaults::useLightSkin, true);
                                                setSkinFromDefaults();
                                            }));
    items.push_back(npp::MenuItem::makeSubmenu("UI Settings", std::move(uim)));

    auto o = editorLocalOrigin(presetButton, this);
    showMenu(std::move(items), {o.x, o.y + presetButton->bounds().getHeight()});
}

void NeuiPluginEditor::doLoadPatch()
{
    npp::FileDialogOptions opts;
    opts.title = "Load Patch";
    opts.initialDir = presetManager->userPatchesPath.u8string();
    opts.filters = {{"Two Filters Patches", std::string("*") + PATCH_EXTENSION}};

    auto res = session().openFile(*parent(), opts);
    if (!res)
        return;

    auto loadPath = fs::path{res.first()};
    presetManager->loadUserPresetDirect(patchMainRef, mainToAudio, loadPath);
}

void NeuiPluginEditor::doSavePatch()
{
    npp::FileDialogOptions opts;
    opts.title = "Save Patch";
    opts.initialDir = presetManager->userPatchesPath.u8string();
    if (strcmp(patchMainRef.name, "Init") != 0)
        opts.initialName = std::string(patchMainRef.name) + PATCH_EXTENSION;
    opts.filters = {{"Two Filters Patches", std::string("*") + PATCH_EXTENSION}};

    auto res = session().saveFile(*parent(), opts);
    if (!res)
        return;

    auto pn = fs::path{res.first()};
    setPatchNameTo(pn.filename().replace_extension("").u8string());

#if USE_WCHAR_PRESET
    presetManager->saveUserPresetDirect(patchMainRef, pn.wstring().c_str());
#else
    presetManager->saveUserPresetDirect(patchMainRef, pn);
#endif

    // Saving makes the patch clean; update the model, view follows.
    patchMainRef.dirty = false;
    presetDataBinding->setDirtyState(false);
    presetManager->rescanUserPresets();
    repaint();
}

void NeuiPluginEditor::resetToDefault() { presetManager->loadInit(patchMainRef, mainToAudio); }

void NeuiPluginEditor::setPatchNameTo(const std::string &s)
{
    memset(patchMainRef.name, 0, sizeof(patchMainRef.name));
    strncpy(patchMainRef.name, s.c_str(), 255);
    // Name is main-thread-only patch state; the audio patch never needs it.
    setPatchNameDisplay();
}

void NeuiPluginEditor::setPatchNameDisplay()
{
    if (!presetButton)
        return;
    presetDataBinding->setStateForDisplayName(patchMainRef.name);
    presetButton->repaint();
}

void NeuiPluginEditor::markPatchDirty()
{
    if (patchMainRef.dirty)
        return;
    patchMainRef.dirty = true;
    presetDataBinding->setDirtyState(true);
    if (presetButton)
        presetButton->repaint();
}

void NeuiPluginEditor::resetEnablement()
{
    for (auto &f : filterPanel)
        if (f)
            f->onModelChanged();
    for (auto &f : stepLFOPanel)
        if (f)
            f->onModelChanged();
    if (routingPanel)
        routingPanel->enableFB();
}

void NeuiPluginEditor::postPatchChange(const std::string &s)
{
    resetEnablement();
    presetDataBinding->setStateForDisplayName(s);
    // Mirror the dirty indicator from patchMain (the model owns dirty; the view follows).
    presetDataBinding->setDirtyState(patchMainRef.dirty);
    for (auto &[id, f] : componentRefreshByID)
        f();

    repaint();
}

void NeuiPluginEditor::pushFilterSetup(int instance)
{
    auto &fn = patchMainRef.filterNodes[instance];

    Engine::MainToAudioMsg msg;
    msg.action = Engine::MainToAudioMsg::SET_FILTER_MODEL;
    msg.paramId = instance;
    msg.uintValues[0] = (uint32_t)fn.model;
    msg.uintValues[1] = (uint32_t)fn.config.pt;
    msg.uintValues[2] = (uint32_t)fn.config.st;
    msg.uintValues[3] = (uint32_t)fn.config.dt;
    msg.uintValues[4] = (uint32_t)fn.config.mt;
    mainToAudio.push(msg);
}

void NeuiPluginEditor::swapFilters(bool alsoSwapMod)
{
    markPatchDirty();
    auto &fn1 = patchMainRef.filterNodes[0];
    auto &fn2 = patchMainRef.filterNodes[1];

    auto snd = [this](auto &par)
    {
        mainToAudio.push({Engine::MainToAudioMsg::Action::BEGIN_EDIT, par.meta.id});
        mainToAudio.push({Engine::MainToAudioMsg::Action::SET_PARAM, par.meta.id, par.value});
        mainToAudio.push({Engine::MainToAudioMsg::Action::END_EDIT, par.meta.id});
    };
    auto swp = [this, snd](auto &p1, auto &p2)
    {
        float v1 = p1.value;
        float v2 = p2.value;
        p1.value = v2;
        p2.value = v1;
        snd(p1);
        snd(p2);
    };
    auto p1 = fn1.params();
    auto p2 = fn2.params();
    for (int i = 0; i < (int)p1.size(); ++i)
    {
        swp(*p1[i], *p2[i]);
    }

    auto m1 = fn1.model;
    auto m2 = fn2.model;
    auto c1 = fn1.config;
    auto c2 = fn2.config;
    fn1.model = m2;
    fn2.model = m1;
    fn1.config = c2;
    fn2.config = c1;
    pushFilterSetup(0);
    pushFilterSetup(1);

    if (alsoSwapMod)
    {
        for (int lf = 0; lf < numStepLFOs; ++lf)
        {
            auto &lm = patchMainRef.stepLfoNodes[lf];
            swp(lm.toCO[0], lm.toCO[1]);
            swp(lm.toRes[0], lm.toRes[1]);
            swp(lm.toMorph[0], lm.toMorph[1]);
            swp(lm.toPan[0], lm.toPan[1]);
        }
    }

    requestParamsFlush();
    resetEnablement();
    repaint();
}

void NeuiPluginEditor::setSkinFromDefaults()
{
    auto b = defaultsProvider->getUserDefaultValue(Defaults::useLightSkin, 0);
    if (b)
    {
        setStyle(nstl::StyleSheet::getBuiltInStyleSheet(nstl::StyleSheet::LIGHT));
    }
    else
    {
        setStyle(nstl::StyleSheet::getBuiltInStyleSheet(nstl::StyleSheet::DARK));
    }

    style()->setFont(PatchMenu, sngc::JogUpDownButton::Styles::labelfont,
                     style()
                         ->getFont(sngc::JogUpDownButton::Styles::styleClass,
                                   sngc::JogUpDownButton::Styles::labelfont)
                         .withSize(nstl::fromJuceHeight(18)));

    resetEnablement();
    repaint();
}

void NeuiPluginEditor::requestParamsFlush()
{
    if (!clapParamsExtension)
        clapParamsExtension = static_cast<const clap_host_params_t *>(
            clapHost->get_extension(clapHost, CLAP_EXT_PARAMS));
    if (clapParamsExtension)
    {
        clapParamsExtension->request_flush(clapHost);
    }
}

void NeuiPluginEditor::rebuildFromPatchMain()
{
    // patchMainRef is engine->patchMain; it already carries the new values, name, dirty
    // state and filter model/config.
    postPatchChange(patchMainRef.name);
}

// ---------------------------------------------------------------------------
// Bindings

PatchContinuous::PatchContinuous(NeuiPluginEditor &e, uint32_t id) : editor(e), pid(id)
{
    if (e.patchMainRef.paramMap.find(id) == e.patchMainRef.paramMap.end())
    {
        SQLOG("You were unable to find param " << id << " - its probably not in patch::params()");
        assert(false);
        std::terminate();
    }

    p = e.patchMainRef.paramMap.at(id);
}

void PatchContinuous::setValueFromGUI(const float &f)
{
    if (p->value == p->meta.minVal && f != p->value)
    {
        if (onPullFromMin)
            onPullFromMin();
    }

    if (p->value == p->meta.defaultVal && f != p->value)
    {
        if (onPullFromDef)
            onPullFromDef();
    }
    if (tempoSynced)
    {
        p->value = p->meta.snapToTemposync(f);
    }
    else
    {
        p->value = f;
    }
    editor.mainToAudio.push({Engine::MainToAudioMsg::Action::SET_PARAM, pid, p->value});
    editor.markPatchDirty();
    editor.requestParamsFlush();

    if (onGuiSetValue)
        onGuiSetValue();
}

PatchDiscrete::PatchDiscrete(NeuiPluginEditor &e, uint32_t id) : editor(e), pid(id)
{
    if (e.patchMainRef.paramMap.find(id) == e.patchMainRef.paramMap.end())
    {
        SQLOG("You were unable to find param " << id << " - its probably not in patch::params()");
        assert(false);
        std::terminate();
    }
    p = e.patchMainRef.paramMap.at(id);
}

void PatchDiscrete::setValueFromGUI(const int &f)
{
    p->value = f;
    editor.mainToAudio.push(
        {Engine::MainToAudioMsg::Action::SET_PARAM, pid, static_cast<float>(f)});
    editor.markPatchDirty();
    editor.requestParamsFlush();

    if (onGuiSetValue)
        onGuiSetValue();
}

} // namespace baconpaul::twofilters::ui
