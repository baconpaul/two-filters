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

#ifndef BACONPAUL_TWOFILTERS_NEUI_UI_NEUI_FILTER_PLOTTER_H
#define BACONPAUL_TWOFILTERS_NEUI_UI_NEUI_FILTER_PLOTTER_H

#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>
#include <utility>
#include <vector>

#include <sst/filters.h>
#include <sst/filters++.h>

/*
 * sst-filters' FilterPlotter with its juce::dsp::FFT swapped for a small
 * radix-2 magnitude FFT, so the curve display needs no JUCE. Everything else
 * is the sweep-divide algorithm from include-extras verbatim.
 */
namespace baconpaul::twofilters::ui
{

struct NeuiFilterPlotParameters
{
    float sampleRate = 96000.0f;
    float startFreqHz = 20.0f;
    float endFreqHz = 20000.0f;
    float inputAmplitude = 0.70710678f;
    float freqSmoothOctaves = 1.0f / 12.0f;
};

class NeuiFilterPlotter
{
  public:
    explicit NeuiFilterPlotter(int fftOrder = 15) : fftSize(1 << fftOrder) {}

    std::pair<std::vector<float>, std::vector<float>>
    plotFilterMagnitudeResponse(sst::filtersplusplus::FilterModel model,
                                sst::filtersplusplus::ModelConfig config, float pitch, float res,
                                float extra1, float extra2, float extra3,
                                const NeuiFilterPlotParameters &params = {})
    {
        auto lt = sst::filtersplusplus::Filter::getLegacyTypeFor(model, config);
        if (lt.has_value())
            return plotFilterMagnitudeResponse(lt->first, lt->second, pitch, res, extra1, extra2,
                                               extra3, params);

        return {};
    }

    std::pair<std::vector<float>, std::vector<float>>
    plotFilterMagnitudeResponse(sst::filters::FilterType filterType,
                                sst::filters::FilterSubType filterSubType, float pitch, float res,
                                float extra1, float extra2, float extra3,
                                const NeuiFilterPlotParameters &params = {})
    {
        // set up input sweep
        std::vector<float> sweepBuffer(fftSize, 0.0f);
        generateLogSweep(sweepBuffer.data(), fftSize, params);

        // set up filter
        float delayBuffer[4][sst::filters::utilities::MAX_FB_COMB +
                             sst::filters::utilities::SincTable::FIRipol_N];
        auto filterState = sst::filters::QuadFilterUnitState{};
        for (auto i = 0; i < 4; ++i)
        {
            filterState.DB[i] = &(delayBuffer[i][0]);
        }
        auto filterUnitPtr = sst::filters::GetQFPtrFilterUnit(filterType, filterSubType);

        sst::filters::FilterCoefficientMaker coefMaker;
        coefMaker.setSampleRateAndBlockSize(params.sampleRate, 512);
        coefMaker.MakeCoeffs(pitch, res, filterType, filterSubType, nullptr, false, extra1, extra2,
                             extra3);
        coefMaker.updateState(filterState);

        // process filter
        std::vector<float> filterBuffer(fftSize, 0.0f);
        if (filterUnitPtr != nullptr)
            runFilter(filterState, filterUnitPtr, sweepBuffer.data(), filterBuffer.data(), fftSize);
        else
            std::copy(sweepBuffer.begin(), sweepBuffer.end(), filterBuffer.begin());

        auto magResponseDB =
            computeFrequencyResponse(sweepBuffer.data(), filterBuffer.data(), fftSize);
        auto magResponseDBSmoothed =
            freqSmooth(magResponseDB.data(), (int)magResponseDB.size(), params.freqSmoothOctaves);
        auto freqAxis = fftFreqs((int)magResponseDB.size(), 1.0f / params.sampleRate);

        return {std::move(freqAxis), std::move(magResponseDBSmoothed)};
    }

  private:
    static void generateLogSweep(float *buffer, int nSamples,
                                 const NeuiFilterPlotParameters &params)
    {
        const auto beta = (float)nSamples / std::log(params.endFreqHz / params.startFreqHz);

        for (int i = 0; i < nSamples; i++)
        {
            float phase =
                2.0f * (float)M_PI * beta * params.startFreqHz *
                (std::pow(params.endFreqHz / params.startFreqHz, (float)i / (float)nSamples) -
                 1.0f);

            buffer[i] = params.inputAmplitude *
                        std::sin((phase + (float)M_PI / 180.0f) / params.sampleRate);
        }
    }

    static void runFilter(sst::filters::QuadFilterUnitState &filterState,
                          sst::filters::FilterUnitQFPtr &filterUnitPtr, const float *inBuffer,
                          float *outBuffer, int numSamples)
    {
        // reset filter state
        std::fill(filterState.R, &filterState.R[sst::filters::n_filter_registers],
                  SIMD_MM(setzero_ps)());

        for (int i = 0; i < 4; ++i)
        {
            filterState.WP[i] = 0;
            filterState.active[i] = 0;
        }
        filterState.active[0] = 0xFFFFFFFF;

        for (int i = 0; i < numSamples; ++i)
        {
            auto yVec = filterUnitPtr(&filterState, SIMD_MM(set_ps1)(inBuffer[i]));

            float yArr alignas(16)[4];
            SIMD_MM(store_ps)(yArr, yVec);
            outBuffer[i] = yArr[0];
        }
    };

    // In-place iterative radix-2 complex FFT; enough for a magnitude plot.
    static void fftComplex(std::vector<std::complex<float>> &a)
    {
        const std::size_t n = a.size();
        for (std::size_t i = 1, j = 0; i < n; ++i)
        {
            std::size_t bit = n >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j)
                std::swap(a[i], a[j]);
        }
        for (std::size_t len = 2; len <= n; len <<= 1)
        {
            const float ang = -2.0f * (float)M_PI / (float)len;
            const std::complex<float> wl{std::cos(ang), std::sin(ang)};
            for (std::size_t i = 0; i < n; i += len)
            {
                std::complex<float> w{1.0f, 0.0f};
                for (std::size_t k = 0; k < len / 2; ++k)
                {
                    auto u = a[i + k];
                    auto v = a[i + k + len / 2] * w;
                    a[i + k] = u + v;
                    a[i + k + len / 2] = u - v;
                    w *= wl;
                }
            }
        }
    }

    static void magnitudeSpectrum(const float *in, int numSamples, std::vector<float> &out)
    {
        std::vector<std::complex<float>> buf(numSamples);
        for (int i = 0; i < numSamples; ++i)
            buf[std::size_t(i)] = {in[i], 0.0f};
        fftComplex(buf);
        const auto outSize = numSamples / 2 + 1;
        out.resize(std::size_t(outSize));
        for (int i = 0; i < outSize; ++i)
            out[std::size_t(i)] = std::abs(buf[std::size_t(i)]);
    }

    std::vector<float> computeFrequencyResponse(float *sweepBuffer, float *filterBuffer,
                                                int numSamples)
    {
        std::vector<float> sweepFFT, filtFFT;
        magnitudeSpectrum(sweepBuffer, numSamples, sweepFFT);
        magnitudeSpectrum(filterBuffer, numSamples, filtFFT);

        const auto fftOutSize = numSamples / 2 + 1;
        std::vector<float> magnitudeResponseDB(fftOutSize, 0.0f);
        for (int i = 0; i < fftOutSize; ++i)
        {
            auto gain = sweepFFT[i] > 0.f ? filtFFT[i] / sweepFFT[i] : 0.f;
            magnitudeResponseDB[i] = gain > 0.f ? 20.0f * std::log10(gain) : -100.0f;
        }

        return magnitudeResponseDB;
    }

    static std::vector<float> fftFreqs(int N, float T)
    {
        auto val = 0.5f / ((float)N * T);

        std::vector<float> results(N, 0.0f);
        std::iota(results.begin(), results.end(), 0.0f);
        std::transform(results.begin(), results.end(), results.begin(),
                       [val](auto x) { return x * val; });

        return results;
    }

    static std::vector<float> freqSmooth(const float *data, int numSamples,
                                         float smFactor = 1.0f / 24.0f)
    {
        const auto s = smFactor > 1.0f ? smFactor : std::sqrt(std::pow(2.0f, smFactor));

        std::vector<float> smoothedVec(numSamples, 0.0f);
        for (int i = 0; i < numSamples; ++i)
        {
            auto i1 = std::max(int((float)i / s), 0);
            auto i2 = std::min(int((float)i * s) + 1, numSamples - 1);

            smoothedVec[i] =
                i2 > i1 ? std::accumulate(data + i1, data + i2, 0.0f) / float(i2 - i1) : 0.0f;
        }

        return smoothedVec;
    }

    const int fftSize;
};

} // namespace baconpaul::twofilters::ui

#endif // BACONPAUL_TWOFILTERS_NEUI_UI_NEUI_FILTER_PLOTTER_H
