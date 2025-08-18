/*
 * Six Sines
 *
 * A synth with audio rate modulation.
 *
 * Copyright 2024-2025, Paul Walker and Various authors, as described in the github
 * transaction log.
 *
 * This source repo is released under the MIT license, but has
 * GPL3 dependencies, as such the combined work will be
 * released under GPL3.
 *
 * The source code and license are at https://github.com/baconpaul/six-sines
 */

#ifndef BACONPAUL_SIX_SINES_SYNTH_VOICE_H
#define BACONPAUL_SIX_SINES_SYNTH_VOICE_H

#include "configuration.h"

struct MTSClient;

namespace baconpaul::sidequest_ns
{
struct Patch;

struct Voice
{
    Voice(const Patch &);
    ~Voice() = default;

    void attack();
    void renderBlock();
    void cleanup();

    bool finished() const { return false; }

    void retriggerAllEnvelopesForKeyPress();
    void retriggerAllEnvelopesForReGate();

    void setupPortaTo(uint16_t newKey, float log2Time);
    void restartPortaTo(float sourceKey, uint16_t newKey, float log2Time, float portaFrac);

    float output alignas(16)[2][blockSize];
    Voice *prior{nullptr}, *next{nullptr};
};
} // namespace baconpaul::six_sines
#endif // VOICE_H
