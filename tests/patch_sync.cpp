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

// Exercises the patch / patchMain sync seams introduced by the ownership rework:
//   - Patch::copyValuesFrom (value-only copy used by activate())
//   - Engine::drainAudioToMainInto (audio -> patchMain main-thread drain)
//   - processUIQueue (UI -> audio-thread patch)
//   - toState / fromState round-trip
// No CLAP host is needed: Engine works standalone, and handleParamValue only calls
// request_callback when clapHost is set (it is null here).

#include "catch2/catch2.hpp"

#include <cmath>
#include <string>
#include <vector>

#include "engine/engine.h"
#include "sst/filters++.h"

using namespace baconpaul::twofilters;

namespace
{
// A minimal output-events sink that accepts and discards everything.
bool outTryPush(const clap_output_events_t *, const clap_event_header_t *) { return true; }
clap_output_events_t makeOut() { return clap_output_events_t{nullptr, outTryPush}; }

bool approxEq(float a, float b, float tol = 1e-5f) { return std::abs(a - b) < tol; }

// A minimal clap_input_events source backed by a vector of param-value events.
struct InputEvents
{
    std::vector<clap_event_param_value_t> events;

    void pushParam(uint32_t id, double value)
    {
        clap_event_param_value_t p{};
        p.header.size = sizeof(p);
        p.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        p.header.type = CLAP_EVENT_PARAM_VALUE;
        p.param_id = id;
        p.note_id = -1;
        p.port_index = -1;
        p.channel = -1;
        p.key = -1;
        p.value = value;
        events.push_back(p);
    }

    clap_input_events_t asClap()
    {
        clap_input_events_t in;
        in.ctx = this;
        in.size = [](const clap_input_events_t *l)
        { return (uint32_t)static_cast<InputEvents *>(l->ctx)->events.size(); };
        in.get = [](const clap_input_events_t *l, uint32_t i) -> const clap_event_header_t *
        { return &static_cast<InputEvents *>(l->ctx)->events[i].header; };
        return in;
    }
};
} // namespace

TEST_CASE("copyValuesFrom copies values, filter config, name and dirty", "[patch-sync]")
{
    Patch a, b;

    // Distinct per-param values in the source.
    for (auto &[id, p] : a.paramMap)
        p->value = 0.01f * (float)(id % 97) + 0.123f;

    a.filterNodes[1].model = sst::filtersplusplus::FilterModel::CytomicSVF;
    a.filterNodes[1].config.pt = sst::filtersplusplus::Passband::HP;
    std::strncpy(a.name, "CopyTest", 255);
    a.dirty = true;

    b.copyValuesFrom(a);

    SECTION("all param values match")
    {
        for (auto &[id, p] : a.paramMap)
            REQUIRE(b.paramMap.at(id)->value == p->value);
    }

    SECTION("filter model/config, name, dirty match")
    {
        REQUIRE(b.filterNodes[1].model == a.filterNodes[1].model);
        REQUIRE(b.filterNodes[1].config.pt == a.filterNodes[1].config.pt);
        REQUIRE(std::string(b.name) == "CopyTest");
        REQUIRE(b.dirty == true);
    }

    SECTION("pointer identity: b's maps point inside b, not a")
    {
        // Guards against an accidental memberwise operator= that would alias the
        // Param* containers across the two Patch objects.
        for (auto &[id, p] : b.paramMap)
            REQUIRE(p != a.paramMap.at(id));
    }
}

TEST_CASE("toState / fromState round-trips values and filter config", "[patch-sync]")
{
    Patch a;
    for (auto &[id, p] : a.paramMap)
    {
        auto &m = p->meta;
        p->value = m.minVal + 0.37f * (m.maxVal - m.minVal);
    }
    a.filterNodes[0].model = sst::filtersplusplus::FilterModel::CytomicSVF;
    a.filterNodes[0].config.pt = sst::filtersplusplus::Passband::BP;

    auto state = a.toState();

    Patch b;
    REQUIRE(b.fromState(state));

    for (auto &[id, p] : a.paramMap)
        REQUIRE(approxEq(b.paramMap.at(id)->value, p->value));
    REQUIRE(b.filterNodes[0].model == a.filterNodes[0].model);
    REQUIRE(b.filterNodes[0].config.pt == a.filterNodes[0].config.pt);
}

TEST_CASE("UI edit reaches the audio patch through processUIQueue", "[patch-sync]")
{
    Engine engine;
    auto out = makeOut();

    const uint32_t pid = 500; // Filter 1 cutoff
    const float target = engine.patch.paramMap.at(pid)->value + 5.0f;

    // The UI writes patchMain directly, then queues the change for the audio thread.
    engine.patchMain.paramMap.at(pid)->value = target;
    engine.mainToAudio.push({Engine::MainToAudioMsg::BEGIN_EDIT, pid});
    engine.mainToAudio.push({Engine::MainToAudioMsg::SET_PARAM, pid, target});
    engine.mainToAudio.push({Engine::MainToAudioMsg::END_EDIT, pid});

    engine.processUIQueue(&out);
    engine.lagHandler.instantlySnap(); // settle the smoothed destination
    engine.snapAllParams();

    REQUIRE(approxEq(engine.patch.paramMap.at(pid)->value, target));
}

TEST_CASE("Audio-thread param change drains back into patchMain", "[patch-sync]")
{
    Engine engine;

    const uint32_t pid = 501; // Filter 1 resonance
    const float target = 0.42f;

    // Simulate host automation landing on the audio thread.
    engine.handleParamValue(nullptr, pid, target);
    engine.snapAllParams();
    REQUIRE(approxEq(engine.patch.paramMap.at(pid)->value, target));

    // patchMain is stale until the main thread drains audioToMain.
    engine.drainAudioToMainInto(engine.patchMain);
    REQUIRE(approxEq(engine.patchMain.paramMap.at(pid)->value, target));
}

TEST_CASE("paramsFlushMainThread applies host values to patchMain and forces a UI rebuild",
          "[patch-sync]")
{
    Engine engine;
    auto out = makeOut();

    const uint32_t pid = 500; // Filter 1 cutoff
    const float target = engine.patchMain.paramMap.at(pid)->value + 3.0f;
    const auto rebuildBefore = engine.uiForceRebuild.load();

    InputEvents in;
    in.pushParam(pid, target);
    auto inC = in.asClap();

    // Inactive path: host param changes land here, not on the audio thread.
    engine.paramsFlushMainThread(&inC, &out);

    // The value reaches patchMain (the main-thread source of truth) ...
    REQUIRE(approxEq(engine.patchMain.paramMap.at(pid)->value, target));
    // ... an open editor is told to rebuild (no audio thread to push UPDATE_PARAM) ...
    REQUIRE(engine.uiForceRebuild.load() == rebuildBefore + 1);
    // ... and the audio-thread `patch` is left untouched.
    REQUIRE_FALSE(approxEq(engine.patch.paramMap.at(pid)->value, target));
}

TEST_CASE("paramsFlushMainThread with no incoming param values does not bump uiForceRebuild",
          "[patch-sync]")
{
    Engine engine;
    auto out = makeOut();
    const auto rebuildBefore = engine.uiForceRebuild.load();

    InputEvents in; // empty
    auto inC = in.asClap();
    engine.paramsFlushMainThread(&inC, &out);

    REQUIRE(engine.uiForceRebuild.load() == rebuildBefore);
}

TEST_CASE("drainAudioToMainInto applies only patch-affecting messages", "[patch-sync]")
{
    Engine engine;

    const uint32_t pid = 502; // Filter 1 morph
    const float before = engine.patchMain.paramMap.at(pid)->value;
    const float target = before + 0.25f;

    // Interleave UI-only chatter with a real param update.
    engine.audioToMain.push({Engine::AudioToMainMsg::UPDATE_VU, 0, 0.9f, 0.8f});
    engine.audioToMain.push({Engine::AudioToMainMsg::UPDATE_LFOSTEP, 1, 3.0f, 4.0f});
    engine.audioToMain.push({Engine::AudioToMainMsg::UPDATE_PARAM, pid, target});
    engine.audioToMain.push({Engine::AudioToMainMsg::SEND_SAMPLE_RATE, 0, 48000.f});

    engine.drainAudioToMainInto(engine.patchMain);

    REQUIRE(approxEq(engine.patchMain.paramMap.at(pid)->value, target));
    // The queue is fully consumed (VU/LFO/sample-rate were discarded, not left behind).
    REQUIRE_FALSE(engine.audioToMain.pop().has_value());
}
