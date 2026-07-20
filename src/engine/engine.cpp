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

#include "engine/engine.h"
#include "engine/steplfo_songpos.h"
#include "sst/cpputils/constructors.h"
#include "sst/basic-blocks/mechanics/block-ops.h"
#include "sst/basic-blocks/dsp/PanLaws.h"

#include "libMTSClient.h"

namespace baconpaul::twofilters
{

int debugLevel{0};

namespace mech = sst::basic_blocks::mechanics;
namespace sdsp = sst::basic_blocks::dsp;

Engine::Engine() : lfos{tuningProvider, tuningProvider}, hrUp{6, true}, hrDn{6, true}
{
    tuningProvider.init();
    updateLfoStorage();
}

Engine::~Engine() {}

void Engine::setSampleRate(double sr)
{
    sampleRate = sr;
    sampleRateInv = 1.0 / sr;
    for (auto &[i, p] : patch.paramMap)
    {
        p->lag.setRateInMilliseconds(1000.0 * 64.0 / 48000.0, sampleRate, 1.0 / blockSize);
        p->lag.snapTo(p->value);
    }
    paramLagSet.removeAll();

    vuPeak.setSampleRate(sampleRate);

    audioToMain.push({AudioToMainMsg::SEND_SAMPLE_RATE, 0, (float)sampleRate});

    auto nq = sampleRate * 0.495;
    // so we are note from 69 which is 440
    // freq = 440 2^(note/12)
    // 12 log2(freq/440) = note
    maxCutoff = 12.0 * log2(nq / 440.0);
    reassignLfos();
    sendUpdateLfo();
    for (int i = 0; i < numFilters; ++i)
    {
        setupFilter(i);
    }

    for (auto &pl : panLag)
        pl.setRateInMilliseconds(25, sampleRate, 1.0 / blockSize);
}

void Engine::updateLfoStorage()
{
    updateLfoStorageFromTo(patch, 0, lfoStorage[0]);
    updateLfoStorageFromTo(patch, 1, lfoStorage[1]);
}
void Engine::updateLfoStorageFromTo(const Patch &p, int node, stepLfo_t::Storage &s)
{
    for (int j = 0; j < maxSteps; ++j)
    {
        s.data[j] = p.stepLfoNodes[node].steps[j];
    }

    s.repeat = (int)std::round(p.stepLfoNodes[node].stepCount);
    s.smooth = p.stepLfoNodes[node].smooth;
    s.rateIsForSingleStep = true;
}
void Engine::reassignLfos()
{
    updateLfoStorage();
    for (int i = 0; i < numStepLFOs; ++i)
    {
        lfos[i].assign(&lfoStorage[i], patch.stepLfoNodes[i].rate, &transport, rng, true);
        lfos[i].setSampleRate(sampleRate, sampleRateInv);
        lfos[i].retrigger();
    }

    sendUpdateLfo();
}

void Engine::processControl(const clap_output_events_t *outq)
{
    auto beatsPerMeasure = 4.0 * transport.signature.numerator / transport.signature.denominator;

    processUIQueue(outq);

    auto pos = patch.routingNode.oversample > 0.5;
    if (pos != overSampling)
    {
        overSampling = pos;
        hrUp.reset();
        hrDn.reset();
        setupFilter(0);
        setupFilter(1);
    }

    auto a0 = patch.filterNodes[0].active > 0.5;
    auto a1 = patch.filterNodes[1].active > 0.5;

    if (a0 != activeFilter[0])
    {
        activeFilter[0] = a0;
        setupFilter(0);
    }

    if (a1 != activeFilter[1])
    {
        activeFilter[1] = a1;
        setupFilter(1);
    }

    auto isPlaying = [](auto v)
    {
        auto b = (v & sst::basic_blocks::modulators::Transport::PLAYING) ||
                 (v & sst::basic_blocks::modulators::Transport::RECORDING);
        return b;
    };

    auto rtm = (int)std::round(patch.routingNode.retriggerMode);
    auto playing = isPlaying(transport.status);

    // On transport start give a clean retrigger. For OnTransport mode this is the only
    // retrigger and the LFOs free-run from here; the bar-synced modes are positioned
    // explicitly from song position below, so this just seeds a sane initial state.
    if (!isPlaying(lastStatus) && playing)
        restartLfos();

    lastStatus = transport.status;

    auto btIncr = blockSize * transport.tempo / (60 * sampleRate);
    transport.timeInBeats += btIncr;

    updateLfoStorage();

    // Lock each LFO to the song position so a relocate, loop or scrub lands at exactly the
    // right step/phase (see steplfo_songpos.h). transport.timeInBeats is host-anchored while
    // playing and free-runs off the engine clock while stopped, so this tracks both.
    //   SongPos (rtm < 0): never reset; position is a pure function of the song beats.
    //   EveryBar/2/4 (0/1/2): reset to step 0 every N bars (barsPerGroup = 1 << rtm).
    //   OnTransport (3): free-run, retriggering only on transport start.
    bool freeRun = rtm == (int)RetrigModes::OnTransport;
    int barsPerGroup = (rtm < 0) ? 0 : (1 << rtm); // 0 == never reset
    for (int i = 0; i < numStepLFOs; ++i)
    {
        if (freeRun)
        {
            lfos[i].process(patch.stepLfoNodes[i].rate, 0, true, false, blockSize);
        }
        else
        {
            auto sp = stepLFOSongPos(transport.timeInBeats, transport.lastBarStartInBeats,
                                     beatsPerMeasure, barsPerGroup, patch.stepLfoNodes[i].rate,
                                     lfoStorage[i].repeat);
            lfos[i].setPhaseTo(sp.step, sp.phase);
        }
    }

    for (int i = 0; i < numFilters; ++i)
    {
        filters[i].concludeBlock();

        auto &fn = patch.filterNodes[i];
        auto &sn = patch.stepLfoNodes[i];

        float co = std::min((float)fn.cutoff, (float)(maxCutoff * (overSampling + 1)));
        float re = fn.resonance;
        float mo = fn.morph;
        for (int j = 0; j < numStepLFOs; ++j)
        {
            co += lfos[j].output * patch.stepLfoNodes[j].toCO[i];
            re += lfos[j].output * patch.stepLfoNodes[j].toRes[i];
            mo += lfos[j].output * patch.stepLfoNodes[j].toMorph[i];
        }
        if (sst::filtersplusplus::Filter::coefficientsExtraIsBipolar(fn.model, fn.config, 0))
            mo = mo * 2 - 1;

        filters[i].makeCoefficients(0, co, re, mo);
        filters[i].copyCoefficientsFromVoiceToVoice(0, 1);
        filters[i].prepareBlock();
    }

    auto mode = (RoutingModes)(int)patch.routingNode.routingMode;

    if (mode == RoutingModes::Serial)
    {
        auto bv = patch.routingNode.filterBlendSerial +
                  lfos[0].output * patch.stepLfoNodes[0].toFiltBlend +
                  lfos[1].output * patch.stepLfoNodes[1].toFiltBlend;
        bv = std::clamp(bv, 0.f, 1.f);
        blendLipol1.newValue(sqrt(1 - bv)); // so blend of 0 is all 1 or all 2 with sum at half
        blendLipol2.newValue(sqrt(bv));
    }
    else
    {
        auto bv = patch.routingNode.filterBlendParallel +
                  lfos[0].output * patch.stepLfoNodes[0].toFiltBlend +
                  lfos[1].output * patch.stepLfoNodes[1].toFiltBlend;
        bv = (std::clamp(bv, -1.f, 1.f) + 1) * 0.5;

        blendLipol1.newValue(sqrt(1 - bv) * 1.4142135); // so blend of 0 == bv of 0.5 has lipol of 1
        blendLipol2.newValue(sqrt(bv) * 1.4142135);
    }

    auto p1 = patch.filterNodes[0].pan + lfos[0].output * patch.stepLfoNodes[0].toPan[0] +
              lfos[1].output * patch.stepLfoNodes[1].toPan[0];
    auto p2 = patch.filterNodes[1].pan + lfos[0].output * patch.stepLfoNodes[0].toPan[1] +
              lfos[1].output * patch.stepLfoNodes[1].toPan[1];
    p1 = std::clamp(p1 * 0.5f + 0.5f, 0.f, 1.f);
    p2 = std::clamp(p2 * 0.5f + 0.5f, 0.f, 1.f);
    panLag[0].setTarget(p1);
    panLag[1].setTarget(p2);
    sst::basic_blocks::dsp::pan_laws::stereoEqualPower(panLag[0].getValue(), panMatrix[0]);
    sst::basic_blocks::dsp::pan_laws::stereoEqualPower(panLag[1].getValue(), panMatrix[1]);
    panLag[0].process();
    panLag[1].process();

    useFeedback = patch.routingNode.feedbackPower > 0.5;

    float inG = patch.routingNode.inputGain + lfos[0].output * patch.stepLfoNodes[0].toPreG +
                lfos[1].output * patch.stepLfoNodes[1].toPreG;
    inG = std::clamp(inG, 0.f, patch.routingNode.inputGain.meta.maxVal);
    inG = inG * inG * inG;
    inGainLipol.newValue(inG);

    float nsG =
        std::clamp(patch.routingNode.noiseLevel + lfos[0].output * patch.stepLfoNodes[0].toNoise +
                       lfos[1].output * patch.stepLfoNodes[1].toNoise,
                   0.f, 1.f);
    nsG = nsG * nsG * nsG;
    noiseGainLipol.newValue(nsG);

    float fblev = patch.routingNode.feedback;
    fblev +=
        lfos[0].output * patch.stepLfoNodes[0].toFB + lfos[1].output * patch.stepLfoNodes[1].toFB;
    fblev = std::clamp(fblev, 0.f, 1.f);
    fblev = fblev * fblev * fblev;
    fbLevelLipol.newValue(fblev);

    float mx = patch.routingNode.mix;
    mx +=
        lfos[0].output * patch.stepLfoNodes[0].toMix + lfos[1].output * patch.stepLfoNodes[1].toMix;
    mx = std::clamp(mx, 0.f, 1.f);
    mixLipol.newValue(mx);

    float outG = patch.routingNode.outputGain + lfos[0].output * patch.stepLfoNodes[0].toPostG +
                 lfos[1].output * patch.stepLfoNodes[1].toPostG;
    outG = std::clamp(outG, 0.f, patch.routingNode.outputGain.meta.maxVal);
    outG = outG * outG * outG;
    outGainLipol.newValue(outG);

    for (auto it = paramLagSet.begin(); it != paramLagSet.end();)
    {
        it->lag.process();
        it->value = it->lag.v;
        if (!it->lag.isActive())
        {
            it = paramLagSet.erase(it);
        }
        else
        {
            ++it;
        }
    }

    lagHandler.process();

    if (editorActive.load(std::memory_order_relaxed))
    {
        if (lastVuUpdate >= updateVuEvery)
        {
            AudioToMainMsg msg{AudioToMainMsg::UPDATE_VU, 0, vuPeak.vu_peak[0], vuPeak.vu_peak[1]};
            audioToMain.push(msg);

            sendUpdateLfo();

            lastVuUpdate = 0;
        }
        else
        {
            lastVuUpdate++;
        }
    }
}

void Engine::processUIQueue(const clap_output_events_t *outq)
{
    auto uiM = mainToAudio.pop();
    while (uiM.has_value())
    {
        switch (uiM->action)
        {
        case MainToAudioMsg::REQUEST_NON_PATCH_STATE:
        {
            // The editor reads all patch state straight from patchMain; it only needs the
            // engine-owned bits echoed back. Today that is just the sample rate.
            audioToMain.push({AudioToMainMsg::SEND_SAMPLE_RATE, 0, (float)sampleRate});
        }
        break;
        case MainToAudioMsg::SET_PARAM_WITHOUT_NOTIFYING:
        case MainToAudioMsg::SET_PARAM:
        {
            bool notify = uiM->action == MainToAudioMsg::SET_PARAM;

            auto dest = patch.paramMap.at(uiM->paramId);
            notify = notify && (dest->meta.flags & CLAP_PARAM_IS_AUTOMATABLE);
            if (notify)
            {
                if (beginEndParamGestureCount == 0)
                {
                    SQLOG("Non-begin/end bound param edit for '" << dest->meta.name << "'");
                }
                if (dest->meta.type == md_t::FLOAT &&
                    (dest->adhocFeatures & Param::AdHocFeatureValues::DONT_SMOOTH) == 0)
                {
                    lagHandler.setNewDestination(&(dest->value), uiM->value);
                }
                else
                {
                    dest->value = uiM->value;
                }

                clap_event_param_value_t p;
                p.header.size = sizeof(clap_event_param_value_t);
                p.header.time = 0;
                p.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                p.header.type = CLAP_EVENT_PARAM_VALUE;
                p.header.flags = 0;
                p.param_id = uiM->paramId;
                p.cookie = dest;

                p.note_id = -1;
                p.port_index = -1;
                p.channel = -1;
                p.key = -1;

                p.value = uiM->value;

                outq->try_push(outq, &p.header);
            }
            else
            {
                dest->value = uiM->value;
            }

            // Side Effects and Ad Hoc Features go here
            // Patch dirty state is main-thread-only now: the UI marks patchMain dirty at the
            // edit site, so the audio thread no longer tracks or echoes it.
        }
        break;
        case MainToAudioMsg::BEGIN_EDIT:
        case MainToAudioMsg::END_EDIT:
        {
            auto dest = patch.paramMap.at(uiM->paramId);
            bool notify = (dest->meta.flags & CLAP_PARAM_IS_AUTOMATABLE);
            if (notify)
            {
                if (uiM->action == MainToAudioMsg::BEGIN_EDIT)
                {
                    beginEndParamGestureCount++;
                }
                else
                {
                    beginEndParamGestureCount--;
                }
                clap_event_param_gesture_t p;
                p.header.size = sizeof(clap_event_param_gesture_t);
                p.header.time = 0;
                p.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                p.header.type = uiM->action == MainToAudioMsg::BEGIN_EDIT
                                    ? CLAP_EVENT_PARAM_GESTURE_BEGIN
                                    : CLAP_EVENT_PARAM_GESTURE_END;
                p.header.flags = 0;
                p.param_id = uiM->paramId;

                outq->try_push(outq, &p.header);
            }
        }
        break;
        case MainToAudioMsg::STOP_AUDIO:
        {
            if (lagHandler.active)
                lagHandler.instantlySnap();
            audioRunning = false;
        }
        break;
        case MainToAudioMsg::START_AUDIO:
        {
            audioRunning = true;
        }
        break;
        case MainToAudioMsg::SEND_POST_LOAD:
        {
            postLoad();
        }
        break;
        case MainToAudioMsg::SET_FILTER_MODEL:
        {
            auto &fn = patch.filterNodes[uiM->paramId];
            fn.model = (sst::filtersplusplus::FilterModel)uiM->uintValues[0];
            fn.config.pt = (sst::filtersplusplus::Passband)uiM->uintValues[1];
            fn.config.st = (sst::filtersplusplus::Slope)uiM->uintValues[2];
            fn.config.dt = (sst::filtersplusplus::DriveMode)uiM->uintValues[3];
            fn.config.mt = (sst::filtersplusplus::FilterSubModel)uiM->uintValues[4];

            setupFilter(uiM->paramId);
        }
        break;
        }
        uiM = mainToAudio.pop();
    }
}

void Engine::handleParamValue(Param *p, uint32_t pid, float value)
{
    if (!p)
    {
        p = patch.paramMap.at(pid);
    }

    // p->value = value;
    p->lag.setTarget(value);
    paramLagSet.addToActive(p);

    AudioToMainMsg au = {AudioToMainMsg::UPDATE_PARAM, pid, value};
    audioToMain.push(au);

    // If no editor is open to drain audioToMain, ask the main thread to drain it into
    // patchMain. Coalesce so we schedule at most one callback per pending drain.
    if (clapHost && !editorActive.load(std::memory_order_relaxed) &&
        !mainThreadDrainRequested.exchange(true))
    {
        clapHost->request_callback(clapHost);
    }
}

void Engine::onMainThread()
{
    // When no editor is open, this callback owns draining audioToMain into patchMain so
    // that host-driven param changes stay reflected in the main-thread source of truth.
    if (!editorActive.load(std::memory_order_relaxed))
    {
        mainThreadDrainRequested.store(false);
        drainAudioToMainInto(patchMain);
    }
}

void Engine::setupFilter(int f)
{
    memset(combDelays[f], 0, sizeof(combDelays[f]));
    auto &fn = patch.filterNodes[f];

    auto model = fn.model;
    auto cfg = fn.config;

    if (!activeFilter[f])
    {
        model = sst::filtersplusplus::FilterModel::None;
        cfg = {};
    }

    auto osf = overSampling ? 2 : 1;
    filters[f].setFilterModel(model);
    filters[f].setModelConfiguration(cfg);
    filters[f].setStereo();
    filters[f].setSampleRateAndBlockSize(osf * sampleRate, osf * blockSize);
    for (int i = 0; i < 4; ++i)
        filters[f].provideDelayLine(i, combDelays[f][i]);
    if (!filters[f].prepareInstance())
        SQLOG("Failed to prepare filter instance");
    filters[f].reset();
    fbL = 0;
    fbR = 0;
    fb2L = 0;
    fb2R = 0;
}

void Engine::restartLfos()
{
    lfos[0].retrigger();
    lfos[1].retrigger();
    sendUpdateLfo();
}

void Engine::sendUpdateLfo()
{
    audioToMain.push({AudioToMainMsg::UPDATE_LFOSTEP, 0, (float)lfos[0].getCurrentStep(),
                      (float)lfos[1].getCurrentStep()});
    audioToMain.push(
        {AudioToMainMsg::UPDATE_LFOSTEP, 1, (float)lfos[0].phase, (float)lfos[1].phase});
    audioToMain.push(
        {AudioToMainMsg::UPDATE_LFOSTEP, 2, (float)lfos[0].output, (float)lfos[1].output});
}

bool Engine::handleAudioToMainMessage(Patch &dest, const AudioToMainMsg &m)
{
    // Applies the patch-model message (a host-automation param value) to `dest`. Returns
    // true if it handled one; false for UI-only telemetry (VU, LFO step, sample rate) which
    // the editor idle loop deals with itself. Name / dirty / filter config are UI-owned and
    // never travel audio -> main.
    switch (m.action)
    {
    case AudioToMainMsg::UPDATE_PARAM:
    {
        auto it = dest.paramMap.find(m.paramId);
        if (it != dest.paramMap.end())
            it->second->value = m.value;
    }
        return true;
    default:
        return false;
    }
}

void Engine::drainAudioToMainInto(Patch &dest)
{
    auto m = audioToMain.pop();
    while (m.has_value())
    {
        handleAudioToMainMessage(dest, *m);
        m = audioToMain.pop();
    }
}

void Engine::paramsFlushMainThread(const clap_input_events_t *in, const clap_output_events_t *out)
{
    // host -> plugin: apply incoming param changes in place to patchMain
    bool appliedIncoming{false};
    auto sz = in->size(in);
    for (uint32_t i = 0; i < sz; ++i)
    {
        auto ev = in->get(in, i);
        if (ev->space_id == CLAP_CORE_EVENT_SPACE_ID && ev->type == CLAP_EVENT_PARAM_VALUE)
        {
            auto pevt = reinterpret_cast<const clap_event_param_value *>(ev);
            auto it = patchMain.paramMap.find(pevt->param_id);
            if (it != patchMain.paramMap.end())
            {
                it->second->value = pevt->value;
                appliedIncoming = true;
            }
        }
    }
    // We are inactive, so no audio thread is pushing UPDATE_PARAM to refresh an open editor.
    // Treat a host-driven value change as an out-of-band patchMain write and force the editor
    // to rebuild from it (the same mechanism stateLoad uses).
    if (appliedIncoming)
        uiForceRebuild++;

    // plugin -> host: drain queued UI edits into patchMain and echo automation out.
    // patchMain is not running audio, so no lag; values are written directly.
    auto uiM = mainToAudio.pop();
    while (uiM.has_value())
    {
        switch (uiM->action)
        {
        case MainToAudioMsg::SET_PARAM:
        case MainToAudioMsg::SET_PARAM_WITHOUT_NOTIFYING:
        {
            auto it = patchMain.paramMap.find(uiM->paramId);
            if (it != patchMain.paramMap.end())
            {
                auto *dest = it->second;
                dest->value = uiM->value;
                bool notify = (uiM->action == MainToAudioMsg::SET_PARAM) &&
                              (dest->meta.flags & CLAP_PARAM_IS_AUTOMATABLE);
                if (notify)
                {
                    clap_event_param_value_t p;
                    p.header.size = sizeof(clap_event_param_value_t);
                    p.header.time = 0;
                    p.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                    p.header.type = CLAP_EVENT_PARAM_VALUE;
                    p.header.flags = 0;
                    p.param_id = uiM->paramId;
                    p.cookie = dest;
                    p.note_id = -1;
                    p.port_index = -1;
                    p.channel = -1;
                    p.key = -1;
                    p.value = uiM->value;
                    out->try_push(out, &p.header);
                }
            }
        }
        break;
        case MainToAudioMsg::BEGIN_EDIT:
        case MainToAudioMsg::END_EDIT:
        {
            auto it = patchMain.paramMap.find(uiM->paramId);
            if (it != patchMain.paramMap.end() &&
                (it->second->meta.flags & CLAP_PARAM_IS_AUTOMATABLE))
            {
                clap_event_param_gesture_t p;
                p.header.size = sizeof(clap_event_param_gesture_t);
                p.header.time = 0;
                p.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                p.header.type = uiM->action == MainToAudioMsg::BEGIN_EDIT
                                    ? CLAP_EVENT_PARAM_GESTURE_BEGIN
                                    : CLAP_EVENT_PARAM_GESTURE_END;
                p.header.flags = 0;
                p.param_id = uiM->paramId;
                out->try_push(out, &p.header);
            }
        }
        break;
        case MainToAudioMsg::SET_FILTER_MODEL:
        {
            namespace sfpp = sst::filtersplusplus;
            auto &fn = patchMain.filterNodes[uiM->paramId];
            fn.model = (sfpp::FilterModel)uiM->uintValues[0];
            fn.config.pt = (sfpp::Passband)uiM->uintValues[1];
            fn.config.st = (sfpp::Slope)uiM->uintValues[2];
            fn.config.dt = (sfpp::DriveMode)uiM->uintValues[3];
            fn.config.mt = (sfpp::FilterSubModel)uiM->uintValues[4];
        }
        break;
        default:
            // STOP_AUDIO/START_AUDIO/SEND_POST_LOAD/REQUEST_NON_PATCH_STATE:
            // audio/refresh concerns. When inactive these are handled at activate() (which
            // copies patchMain into patch and rebuilds filters/LFOs) or are irrelevant.
            break;
        }
        uiM = mainToAudio.pop();
    }
}

void Engine::sendEntirePatchToAudio(Patch &patch, mainToAudioQueue_T &mainToAudio,
                                    const clap_host_t *h, const clap_host_params_t *hostPar)
{
    if (!h)
        return;

    if (hostPar == nullptr)
    {
        hostPar = static_cast<const clap_host_params_t *>(h->get_extension(h, CLAP_EXT_PARAMS));
    }

    mainToAudio.push({MainToAudioMsg::STOP_AUDIO});
    for (const auto &p : patch.params)
    {
        mainToAudio.push({MainToAudioMsg::SET_PARAM_WITHOUT_NOTIFYING, p->meta.id, p->value});
    }
    mainToAudio.push({MainToAudioMsg::START_AUDIO});
    mainToAudio.push({MainToAudioMsg::SEND_POST_LOAD, true});

    for (int instance = 0; instance < numFilters; ++instance)
    {
        auto &fn = patch.filterNodes[instance];
        MainToAudioMsg msg;
        msg.action = MainToAudioMsg::SET_FILTER_MODEL;
        msg.paramId = instance;
        msg.uintValues[0] = (uint32_t)fn.model;
        msg.uintValues[1] = (uint32_t)fn.config.pt;
        msg.uintValues[2] = (uint32_t)fn.config.st;
        msg.uintValues[3] = (uint32_t)fn.config.dt;
        msg.uintValues[4] = (uint32_t)fn.config.mt;

        mainToAudio.push(msg);
    }

    // A load is a bulk out-of-band value change. We are on the main thread and the host reads
    // values/text from patchMain, which the caller already updated, so tell the host to
    // re-read directly rather than round-tripping a rescan request through the audio thread.
    if (hostPar)
    {
        hostPar->rescan(h, CLAP_PARAM_RESCAN_VALUES | CLAP_PARAM_RESCAN_TEXT);
        hostPar->request_flush(h);
    }
}
} // namespace baconpaul::twofilters
