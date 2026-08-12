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
#include "neui-patch-bindings.h"

#include <sst/neuigui/style/StyleSheet.h>

#include "configuration.h"

namespace baconpaul::twofilters::ui
{
namespace nstl = sst::neuigui::style;

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

    nstl::StyleSheet::initializeStyleSheets([]() {});
    setStyle(nstl::StyleSheet::getBuiltInStyleSheet(nstl::StyleSheet::DARK));

    for (int i = 0; i < numFilters; ++i)
    {
        filterPanel[i] = &add<NFilterPanel>(*this, i);
    }

    // Idle owns draining audioToMain while the editor is open.
    editorActive = true;

    mainToAudio.push({Engine::MainToAudioMsg::REQUEST_NON_PATCH_STATE, true});
    requestParamsFlush();
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
    auto b = localBounds();

    // First slice: the two filter panels side by side across the top
    auto panelHeight = 220.0f;
    auto row = b.withTrimmedTop(40).withHeight(panelHeight);
    auto half = row.getWidth() / 2;
    filterPanel[0]->setBounds(row.removeFromLeft(half));
    filterPanel[1]->setBounds(row);
}

void NeuiPluginEditor::paint(npp::Canvas &g)
{
    sngc::WindowPanelBase<NeuiPluginEditor>::paint(g);

    auto ft = style()->getFont(sngc::Label::Styles::styleClass, sngc::Label::Styles::labelfont);
    g.drawText("Two Filters", g.bounds().withHeight(40).withTrimmedLeft(8), ft.withSize(30),
               npp::Color::rgb(0xFF, 0xFF, 0xFF).withAlpha(0.9f), npp::HAlign::left,
               npp::VAlign::middle);
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

    bool anyChange{false};
    auto aum = audioToMain.pop();
    while (aum.has_value())
    {
        if (Engine::handleAudioToMainMessage(patchMainRef, *aum))
        {
            auto rit = componentRepaintByID.find(aum->paramId);
            if (rit != componentRepaintByID.end())
                rit->second();
            anyChange = true;
        }
        else if (aum->action == Engine::AudioToMainMsg::UPDATE_VU)
        {
            // VUMeter arrives with a later widget round
        }
        else if (aum->action == Engine::AudioToMainMsg::SEND_SAMPLE_RATE)
        {
            sampleRate = aum->value;
        }
        else if (aum->action == Engine::AudioToMainMsg::UPDATE_LFOSTEP)
        {
            // StepLFO panel arrives with a later widget round
        }
        else
        {
            SQLOG("Ignored patch message " << aum->action);
        }
        aum = audioToMain.pop();
    }
    (void)anyChange;
}

void NeuiPluginEditor::markPatchDirty()
{
    if (patchMainRef.dirty)
        return;
    patchMainRef.dirty = true;
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
    for (auto &[id, f] : componentRepaintByID)
        f();
    repaint();
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

// ---------------------------------------------------------------------------
// Filter panel

NFilterPanel::NFilterPanel(npp::Parent p, NeuiPluginEditor &e, int inst)
    : sngc::NamedPanelBase<NFilterPanel>(p, "Filter " + std::to_string(inst + 1)), editor(e),
      instance(inst)
{
    auto &fn = editor.patchMainRef.filterNodes[instance];

    createComponent(editor, *this, fn.cutoff, cutoffK, cutoffD);
    createComponent(editor, *this, fn.resonance, resonanceK, resonanceD);
    createComponent(editor, *this, fn.morph, morphK, morphD);
    createComponent(editor, *this, fn.pan, panK, panD);
}

NFilterPanel::~NFilterPanel() = default;

void NFilterPanel::resized()
{
    auto b = getContentArea();

    auto kRow = b.withTrimmedTop(b.getHeight() - 78);
    auto kw = 55.0f;
    auto pad = (kRow.getWidth() - 4 * kw) / 5;

    auto place = [&](sngc::Knob *k, int idx)
    {
        if (!k)
            return;
        auto r = npp::Rect{kRow.getX() + pad + idx * (kw + pad), kRow.getY(), kw, 76.0f};
        k->setBounds(r);
    };
    place(cutoffK, 0);
    place(resonanceK, 1);
    place(morphK, 2);
    place(panK, 3);
}

} // namespace baconpaul::twofilters::ui
