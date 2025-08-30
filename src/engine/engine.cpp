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

Engine::Engine() : lfos{tuningProvider, tuningProvider} { tuningProvider.init(); }

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

    for (int i = 0; i < numFilters; ++i)
    {
        setupFilter(i);
    }
}

void Engine::processControl(const clap_output_events_t *outq)
{
    for (int i = 0; i < numFilters; ++i)
    {
        filters[i].concludeBlock();

        filters[i].makeCoefficients(0, patch.filterNodes[i].cutoff, patch.filterNodes[i].resonance,
                                    patch.filterNodes[i].morph);
        filters[i].copyCoefficientsFromVoiceToVoice(0, 1);
        filters[i].prepareBlock();
    }

    useFeedback = patch.routingNode.feedbackPower > 0.5;

    processUIQueue(outq);

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
            if (notify)
            {
                if (beginEndParamGestureCount == 0)
                {
                    SQLOG("Non-begin/end bound param edit for '" << dest->meta.name << "'");
                }
                if (dest->meta.type == md_t::FLOAT)
                    lagHandler.setNewDestination(&(dest->value), uiM->value);
                else
                    dest->value = uiM->value;

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
    filters[f].setFilterModel(fn.model);
    filters[f].setModelConfiguration(fn.config);
    filters[f].setStereo();
    filters[f].setSampleRateAndBlockSize(sampleRate, blockSize);
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
} // namespace baconpaul::twofilters
