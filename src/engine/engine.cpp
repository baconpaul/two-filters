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

#include "engine/engine.h"
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

    audioToUi.push({AudioToUIMsg::SEND_SAMPLE_RATE, 0, (float)sampleRate});

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
    for (int i = 0; i < numFilters; ++i)
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

    if (!isPlaying(lastStatus) && isPlaying(transport.status))
    {
        restartLfos();
    }
    lastStatus = transport.status;

    auto btIncr = blockSize * transport.tempo / (60 * sampleRate);
    transport.hostTimeInBeats += btIncr;
    transport.timeInBeats += btIncr;

    auto rtm = (int)std::round(patch.routingNode.retriggerMode);
    if (rtm != (int)RetrigModes::OnTransport)
    {
        auto mMul = 1 << rtm;
        auto isCand = (int)transport.timeInBeats % ((int)(mMul * beatsPerMeasure));
        if (transport.timeInBeats >= transport.lastBarStartInBeats + beatsPerMeasure &&
            !didResetInLargerBlock)
        {
            if (isCand == 0)
            {
                restartLfos();
            }
            if (!isPlaying(transport.status))
                transport.lastBarStartInBeats += beatsPerMeasure;
            didResetInLargerBlock = true;
        }
    }

    updateLfoStorage();

    for (int i = 0; i < numStepLFOs; ++i)
    {
        lfos[i].process(patch.stepLfoNodes[i].rate, 0, true, false, blockSize);
    }

    for (int i = 0; i < numFilters; ++i)
    {
        filters[i].concludeBlock();

        auto &fn = patch.filterNodes[i];
        auto &sn = patch.stepLfoNodes[i];

        float co = fn.cutoff;
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

    if (isEditorAttached)
    {
        if (lastVuUpdate >= updateVuEvery)
        {
            AudioToUIMsg msg{AudioToUIMsg::UPDATE_VU, 0, vuPeak.vu_peak[0], vuPeak.vu_peak[1]};
            audioToUi.push(msg);

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
    bool didRefresh{false};
    if (doFullRefresh)
    {
        pushFullUIRefresh();
        doFullRefresh = false;
        didRefresh = true;
    }
    auto uiM = mainToAudio.pop();
    while (uiM.has_value())
    {
        switch (uiM->action)
        {
        case MainToAudioMsg::REQUEST_REFRESH:
        {
            if (!didRefresh)
            {
                // don't do it twice in one process obvs
                pushFullUIRefresh();
            }
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

            auto d = patch.dirty;
            if (!d)
            {
                patch.dirty = true;
                audioToUi.push({AudioToUIMsg::SET_PATCH_DIRTY_STATE, patch.dirty});
            }
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
        case MainToAudioMsg::SEND_PATCH_NAME:
        {
            memset(patch.name, 0, sizeof(patch.name));
            strncpy(patch.name, uiM->uiManagedPointer, 255);
            audioToUi.push({AudioToUIMsg::SET_PATCH_NAME, 0, 0, 0, patch.name});
        }
        break;
        case MainToAudioMsg::SEND_PATCH_IS_CLEAN:
        {
            patch.dirty = false;
            audioToUi.push({AudioToUIMsg::SET_PATCH_DIRTY_STATE, patch.dirty});
        }
        break;
        case MainToAudioMsg::SEND_POST_LOAD:
        {
            postLoad();
        }
        break;
        case MainToAudioMsg::SEND_PREP_FOR_STREAM:
        {
            prepForStream();
        }
        break;
        case MainToAudioMsg::SEND_REQUEST_RESCAN:
        {
            onMainRescanParams = true;
            audioToUi.push({AudioToUIMsg::DO_PARAM_RESCAN});
            clapHost->request_callback(clapHost);
        }
        break;
        case MainToAudioMsg::EDITOR_ATTACH_DETATCH:
        {
            isEditorAttached = uiM->paramId;
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

    AudioToUIMsg au = {AudioToUIMsg::UPDATE_PARAM, pid, value};
    audioToUi.push(au);
}

void Engine::pushFullUIRefresh()
{
    for (const auto *p : patch.params)
    {
        AudioToUIMsg au = {AudioToUIMsg::UPDATE_PARAM, p->meta.id, p->value};
        audioToUi.push(au);
    }

    for (int i = 0; i < numFilters; ++i)
    {
        AudioToUIMsg fm;
        fm.action = AudioToUIMsg::SEND_FILTER_CONFIG;
        fm.paramId = i;
        fm.uintValues[0] = (uint32_t)patch.filterNodes[i].model;
        fm.uintValues[1] = (uint32_t)patch.filterNodes[i].config.pt;
        fm.uintValues[2] = (uint32_t)patch.filterNodes[i].config.st;
        fm.uintValues[3] = (uint32_t)patch.filterNodes[i].config.dt;
        fm.uintValues[4] = (uint32_t)patch.filterNodes[i].config.mt;
        audioToUi.push(fm);
    }
    audioToUi.push({AudioToUIMsg::SET_PATCH_NAME, 0, 0, 0, patch.name});
    audioToUi.push({AudioToUIMsg::SET_PATCH_DIRTY_STATE, patch.dirty});
    audioToUi.push({AudioToUIMsg::SEND_SAMPLE_RATE, 0, (float)sampleRate});
}

void Engine::onMainThread()
{
    bool ex{true}, re{false};
    if (onMainRescanParams.compare_exchange_strong(ex, re))
    {
        auto pe = static_cast<const clap_host_params_t *>(
            clapHost->get_extension(clapHost, CLAP_EXT_PARAMS));
        if (pe)
        {
            pe->rescan(clapHost, CLAP_PARAM_RESCAN_VALUES | CLAP_PARAM_RESCAN_TEXT);
        }
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
    audioToUi.push({AudioToUIMsg::UPDATE_LFOSTEP, 0, (float)lfos[0].getCurrentStep(),
                    (float)lfos[1].getCurrentStep()});
    audioToUi.push({AudioToUIMsg::UPDATE_LFOSTEP, 1, (float)lfos[0].phase, (float)lfos[1].phase});
    audioToUi.push({AudioToUIMsg::UPDATE_LFOSTEP, 2, (float)lfos[0].output, (float)lfos[1].output});
}
} // namespace baconpaul::twofilters
