/*
 * SideQuest Starting Point
 *
 * Basically lets paul bootstrap his projects.
 *
 * Copyright 2024-2025, Paul Walker and Various authors, as described in the github
 * transaction log.
 *
 * This source repo is released under the MIT license, but has
 * GPL3 dependencies, as such the combined work will be
 * released under GPL3.
 *
 * The source code and license are at https://github.com/baconpaul/sidequest-startingpoint
 */

#ifndef BACONPAUL_SIDEQUEST_ENGINE_ENGINE_H
#define BACONPAUL_SIDEQUEST_ENGINE_ENGINE_H

#include <memory>
#include <array>

#include "sst/basic-blocks/dsp/LanczosResampler.h"

#include <clap/clap.h>
#include "sst/basic-blocks/dsp/Lag.h"
#include "sst/basic-blocks/dsp/VUPeak.h"
#include "sst/basic-blocks/tables/EqualTuningProvider.h"
#include "sst/voicemanager/voicemanager.h"
#include "sst/cpputils/ring_buffer.h"

#include "filesystem/import.h"

#include "configuration.h"

#include "engine/voice.h"
#include "engine/patch.h"

#include "sst/basic-blocks/dsp/LagCollection.h"

namespace baconpaul::sidequest_ns
{
struct Engine
{
    float output alignas(16)[2][blockSize];

    Patch patch;
    sst::basic_blocks::dsp::LagCollection<130> midiCCLagCollection; // 130 for 128 + pitch + chanat

    struct VMConfig
    {
        static constexpr size_t maxVoiceCount{maxVoices};
        using voice_t = Voice;
    };

    std::array<Voice, VMConfig::maxVoiceCount> voices;
    Voice *head{nullptr};
    void addToVoiceList(Voice *);
    Voice *removeFromVoiceList(Voice *); // returns next
    void dumpVoiceList();
    int voiceCount{0};

    struct PortaContinuation
    {
        bool active{false};
        bool updateEveryBlock{false};
        float sourceKey{0.f};
        float dKey{0.f};
        float portaFrac{0.f};
        float dPortaFrac{0.f};
    } portaContinuation;

    struct VMResponder
    {
        Engine &engine;
        VMResponder(Engine &s) : engine(s) {}

        std::function<void(Voice *)> doVoiceEndCallback = [](auto) {};
        void setVoiceEndCallback(std::function<void(Voice *)> f) { doVoiceEndCallback = f; }
        void retriggerVoiceWithNewNoteID(Voice *v, int32_t nid, float vel)
        {
            SQLOG_UNIMPL;
            v->retriggerAllEnvelopesForReGate();
        }
        void moveVoice(Voice *v, uint16_t p, uint16_t c, uint16_t k, float ve)
        {
            SQLOG_UNIMPL;
            v->retriggerAllEnvelopesForKeyPress();
        }

        void moveAndRetriggerVoice(Voice *v, uint16_t p, uint16_t c, uint16_t k, float ve)
        {
            SQLOG_UNIMPL;
            v->retriggerAllEnvelopesForReGate();
        }

        int32_t beginVoiceCreationTransaction(
            typename sst::voicemanager::VoiceBeginBufferEntry<VMConfig>::buffer_t &buffer, uint16_t,
            uint16_t, uint16_t, int32_t, float)
        {
            auto vc = 1;
            for (int i = 0; i < vc; ++i)
                buffer[i].polyphonyGroup = 0;
            return vc;
        };

        void endVoiceCreationTransaction(uint16_t, uint16_t, uint16_t, int32_t, float) {}

        void discardHostVoice(int32_t vid) {}
        void terminateVoice(Voice *voice) { SQLOG_UNIMPL; }
        int32_t initializeMultipleVoices(
            int32_t ct,
            const typename sst::voicemanager::VoiceInitInstructionsEntry<VMConfig>::buffer_t &ibuf,
            typename sst::voicemanager::VoiceInitBufferEntry<VMConfig>::buffer_t &obuf, uint16_t pt,
            uint16_t ch, uint16_t key, int32_t nid, float vel, float rt)
        {
            int lastStart{0};
            for (int vc = 0; vc < ct; ++vc)
            {
                if (ibuf[vc].instruction !=
                    sst::voicemanager::VoiceInitInstructionsEntry<
                        baconpaul::sidequest_ns::Engine::VMConfig>::Instruction::SKIP)
                {
                    for (int i = lastStart; i < VMConfig::maxVoiceCount; ++i)
                    {
                        if (engine.voices[i].used == false)
                        {
                            obuf[vc].voice = &engine.voices[i];
                            auto &v = engine.voices[i];
                            v.used = true;
                            v.key = key;
                            v.sampleRate = engine.sampleRate;

                            v.attack();

                            engine.addToVoiceList(&v);

                            lastStart = i + 1;
                            break;
                        }
                    }
                }
            }
            return 1;
        }
        void releaseVoice(Voice *v, float rv) { v->release(); }
        void setNoteExpression(Voice *v, int32_t e, double val) { SQLOG_UNIMPL; }
        void setVoicePolyphonicParameterModulation(Voice *, uint32_t, double) {}
        void setVoiceMonophonicParameterModulation(Voice *, uint32_t, double) {}
        void setPolyphonicAftertouch(Voice *v, int8_t a) { SQLOG_UNIMPL; }

        void setVoiceMIDIMPEChannelPitchBend(Voice *v, uint16_t b) { SQLOG_UNIMPL; }
        void setVoiceMIDIMPEChannelPressure(Voice *v, int8_t p) { SQLOG_UNIMPL; }
        void setVoiceMIDIMPETimbre(Voice *v, int8_t t) { SQLOG_UNIMPL; }
    };
    struct VMMonoResponder
    {
        Engine &engine;
        VMMonoResponder(Engine &s) : engine(s) {}

        void setMIDIPitchBend(int16_t c, int16_t v)
        {
            SQLOG_UNIMPL;
            auto val = (v - 8192) * 1.0 / 8192;
            // engine.midiCCLagCollection.setTarget(129, val, &synth.monoValues.pitchBend);
        }
        void setMIDI1CC(int16_t ch, int16_t cc, int8_t v)
        {
            SQLOG_UNIMPL;
            // engine.monoValues.midiCC[cc] = v;
            // synth.monoValues.midiCCFloat[cc] = v / 127.0;
            // engine.midiCCLagCollection.setTarget(cc, v / 127.0,
            // &synth.monoValues.midiCCFloat[cc]);
        }
        void setMIDIChannelPressure(int16_t ch, int16_t v)
        {
            SQLOG_UNIMPL;
            // synth.midiCCLagCollection.setTarget(128, v / 127.0, &synth.monoValues.channelAT);
            // synth.monoValues.channelAT = v / 127.0;
        }
    };
    using voiceManager_t = sst::voicemanager::VoiceManager<VMConfig, VMResponder, VMMonoResponder>;

    VMResponder responder;
    VMMonoResponder monoResponder;
    std::unique_ptr<voiceManager_t> voiceManager;

    Engine();
    ~Engine();

    bool audioRunning{true};
    int beginEndParamGestureCount{0};

    double sampleRate{1}, sampleRateInv{1};
    void setSampleRate(double sampleRate);

    template <bool multiOut> void processInternal(const clap_output_events_t *);

    void process(const clap_output_events_t *);
    void processUIQueue(const clap_output_events_t *);

    void handleParamValue(Param *p, uint32_t pid, float value);

    static_assert(sst::voicemanager::constraints::ConstraintsChecker<VMConfig, VMResponder,
                                                                     VMMonoResponder>::satisfies());

    // UI Communication
    struct AudioToUIMsg
    {
        enum Action : uint32_t
        {
            UPDATE_PARAM,
            UPDATE_VU,
            UPDATE_VOICE_COUNT,
            SET_PATCH_NAME,
            SET_PATCH_DIRTY_STATE,
            DO_PARAM_RESCAN,
            SEND_SAMPLE_RATE
        } action;
        uint32_t paramId{0};
        float value{0}, value2{0};
        const char *patchNamePointer{0};
    };
    struct MainToAudioMsg
    {
        enum Action : uint32_t
        {
            REQUEST_REFRESH,
            SET_PARAM,
            SET_PARAM_WITHOUT_NOTIFYING,
            BEGIN_EDIT,
            END_EDIT,
            STOP_AUDIO,
            START_AUDIO,
            SEND_PATCH_NAME,
            SEND_PATCH_IS_CLEAN,
            SEND_POST_LOAD,
            SEND_REQUEST_RESCAN,
            EDITOR_ATTACH_DETATCH, // paramid is true for attach and false for detach
            SEND_PREP_FOR_STREAM,
            PANIC_STOP_VOICES
        } action;
        uint32_t paramId{0};
        float value{0};
        const char *uiManagedPointer{nullptr};
    };
    using audioToUIQueue_t = sst::cpputils::SimpleRingBuffer<AudioToUIMsg, 1024 * 16>;
    using mainToAudioQueue_T = sst::cpputils::SimpleRingBuffer<MainToAudioMsg, 1024 * 64>;
    audioToUIQueue_t audioToUi;
    mainToAudioQueue_T mainToAudio;
    std::atomic<bool> doFullRefresh{false};
    bool isEditorAttached{false};
    sst::basic_blocks::dsp::UIComponentLagHandler lagHandler;

    std::atomic<bool> readyForStream{false};
    void prepForStream()
    {
        SQLOG("Ready for Stream");
        if (lagHandler.active)
            lagHandler.instantlySnap();

        for (auto &p : paramLagSet)
        {
            p.lag.snapToTarget();
            p.value = p.lag.v;
        }
        paramLagSet.removeAll();

        patch.dirty = false;
        doFullRefresh = true;
        readyForStream = true;
    }

    void pushFullUIRefresh();
    void postLoad()
    {
        doFullRefresh = true;

        for (auto &[i, p] : patch.paramMap)
        {
            p->lag.snapTo(p->value);
        }
    }

    std::atomic<bool> onMainRescanParams{false};
    void onMainThread();

    sst::cpputils::active_set_overlay<Param> paramLagSet;

    sst::basic_blocks::dsp::VUPeak vuPeak;
    int32_t updateVuEvery{(int32_t)(48000 * 2.5 / 60 / blockSize)}; // approx
    int32_t lastVuUpdate{updateVuEvery};

    const clap_host_t *clapHost{nullptr};
};
} // namespace baconpaul::sidequest_ns
#endif // SYNTH_H
