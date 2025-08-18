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

#ifndef BACONPAUL_SIDEQUEST_ENGINE_VOICE_H
#define BACONPAUL_SIDEQUEST_ENGINE_VOICE_H

#include "configuration.h"
#include "patch.h"

struct MTSClient;

namespace baconpaul::sidequest_ns
{
struct Patch;

struct Voice
{
    Voice(const Patch &p) : pitch(p.sqParams.pitch), harmlev(p.sqParams.harmlev) {}
    ~Voice() = default;

    void attack()
    {
        gated = true;
        fadeInSamples = 1000;
        fadeOutSamples = 0;
        finval = false;
        phase = 0.f;
        freq = 440 * pow(2.f, (key - 69) / 12.f);
        dPhase = freq / sampleRate;
    }
    void renderBlock()
    {
        auto udp = dPhase * pow(2.f, pitch / 12);

        auto amp = 1.0;
        auto damp = 0.f;
        if (fadeInSamples > 0)
        {
            amp = 1 - fadeInSamples / 1000.0;
            damp = 1 / 1000.0;
            fadeInSamples -= blockSize;
            if (fadeInSamples < 0)
            {
                fadeInSamples = 0;
            }
        }
        if (fadeOutSamples > 0)
        {
            amp = fadeOutSamples / 1000.0;
            damp = -1 / 1000.0;
        }
        for (int i = 0; i < blockSize; ++i)
        {
            auto sv = 0.3 * amp * std::sin(phase * 2.0 * M_PI);
            auto sv2 = 0.3 * harmlev * amp * std::sin(phase * 4.0 * M_PI);
            amp += damp;
            phase += udp;
            output[0][i] = sv + sv2;
            output[1][i] = sv + sv2;
        }
        if (phase > 1)
            phase -= 1;
        if (fadeOutSamples > 0)
        {
            fadeOutSamples -= blockSize;
            ;
            if (fadeOutSamples <= 0)
            {
                fadeOutSamples = 0;
                finval = true;
            }
        }
    }
    void release()
    {
        gated = false;
        fadeOutSamples = 1000;
    }
    void cleanup();

    bool finished() const { return finval; }

    void retriggerAllEnvelopesForKeyPress();
    void retriggerAllEnvelopesForReGate();

    void setupPortaTo(uint16_t newKey, float log2Time);
    void restartPortaTo(float sourceKey, uint16_t newKey, float log2Time, float portaFrac);

    bool used{false}, finval{false};
    bool gated{false};
    int32_t key;

    float freq{440.0};
    float phase{0.f}, dPhase{0.f};

    int32_t fadeInSamples{0}, fadeOutSamples{0};
    double sampleRate;

    float output alignas(16)[2][blockSize];
    Voice *prior{nullptr}, *next{nullptr};

    const float &pitch, &harmlev;
};
} // namespace baconpaul::sidequest_ns
#endif // VOICE_H
