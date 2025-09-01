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

#ifndef BACONPAUL_TWOFILTERS_ENGINE_PATCH_H
#define BACONPAUL_TWOFILTERS_ENGINE_PATCH_H

#include <vector>
#include <array>
#include <unordered_map>
#include <algorithm>
#include <clap/clap.h>
#include "configuration.h"
#include "sst/cpputils/constructors.h"
#include "sst/cpputils/active_set_overlay.h"
#include "sst/basic-blocks/params/ParamMetadata.h"
#include "sst/basic-blocks/dsp/Lag.h"
#include "sst/plugininfra/patch-support/patch_base.h"
#include "sst/filters++.h"

namespace baconpaul::twofilters
{
namespace scpu = sst::cpputils;
namespace pats = sst::plugininfra::patch_support;
using md_t = sst::basic_blocks::params::ParamMetaData;
struct Param : pats::ParamBase, sst::cpputils::active_set_overlay<Param>::participant
{
    Param(const md_t &m) : pats::ParamBase(m) {}

    Param &operator=(const float &val)
    {
        value = val;
        return *this;
    }

    uint64_t adhocFeatures{0};
    enum AdHocFeatureValues : uint64_t
    {
    };

    bool isTemposynced() const
    {
        if (tempoSyncPartner)
            return tempoSyncPartner->value;

        return false;
    }

    Param *tempoSyncPartner{nullptr};

    sst::basic_blocks::dsp::LinearLag<float, false> lag;
};

struct Patch : pats::PatchBase<Patch, Param>
{
    static constexpr uint32_t patchVersion{1};
    static constexpr const char *id{"org.baconpaul.two-filters"};

    static constexpr uint32_t floatFlags{CLAP_PARAM_IS_AUTOMATABLE};
    static constexpr uint32_t boolFlags{CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED};

    static md_t floatMd() { return md_t().asFloat().withFlags(floatFlags); }
    static md_t floatEnvRateMd()
    {
        return md_t().asFloat().withFlags(floatFlags).as25SecondExpTime();
    }
    static md_t boolMd() { return md_t().asBool().withFlags(boolFlags); }
    static md_t intMd() { return md_t().asInt().withFlags(boolFlags); }

    Patch() : pats::PatchBase<Patch, Param>()

    {
        onResetToInit = [](auto &patch)
        {
            for (auto &fn : patch.filterNodes)
            {
                fn.model = sst::filtersplusplus::FilterModel::None;
                fn.config = {};
            }
        };
        auto pushParams = [this](auto &from) { this->pushMultipleParams(from.params()); };

        filterNodes[0].model = sst::filtersplusplus::FilterModel::CytomicSVF;
        filterNodes[0].config.pt = sst::filtersplusplus::Passband::LP;

        filterNodes[1].model = sst::filtersplusplus::FilterModel::None;
        filterNodes[1].config = {};

        pushParams(filterNodes[0]);
        pushParams(filterNodes[1]);
        pushParams(routingNode);

        pushParams(stepLfoNodes[0]);
        pushParams(stepLfoNodes[1]);

        std::sort(params.begin(), params.end(),
                  [](const Param *a, const Param *b)
                  {
                      auto ga = a->meta.groupName;
                      auto gb = b->meta.groupName;
                      if (ga != gb)
                      {
                          if (ga == "Main")
                              return true;
                          if (gb == "Main")
                              return false;

                          return ga < gb;
                      }

                      auto an = a->meta.name;
                      auto bn = b->meta.name;
                      auto ane = an.find("Env ") != std::string::npos;
                      auto bne = bn.find("Env ") != std::string::npos;

                      if (ane != bne)
                      {
                          if (ane)
                              return false;

                          return true;
                      }
                      if (ane && bne)
                          return a->meta.id < b->meta.id;

                      return a->meta.name < b->meta.name;
                  });

        additionalToState = [this](auto &state) { additionalToStateImpl(state); };
        additionalFromState = [this](auto *state, auto ver)
        { additionalFromStateImpl(state, ver); };
    }

    struct FilterNode
    {
        static constexpr uint32_t idBase{500};
        static constexpr uint32_t idStride{100};

        FilterNode(int instance)
            : cutoff(floatMd()
                         .asAudibleFrequency()
                         .withGroupName(groupName(instance))
                         .withName("Cutoff " + std::to_string(instance + 1))
                         .withID(id(instance, 0))),
              resonance(floatMd()
                            .asPercent()
                            .withGroupName(groupName(instance))
                            .withName("Resonance " + std::to_string(instance + 1))
                            .withID(id(instance, 1))),
              morph(floatMd()
                        .asPercent()
                        .withGroupName(groupName(instance))
                        .withName("Morph " + std::to_string(instance + 1))
                        .withID(id(instance, 2)))
        {
        }

        std::string groupName(int i) const { return "Filter " + std::to_string(i + 1); }
        uint32_t id(int instance, int f) const { return idBase + f + idStride * instance; }

        Param cutoff, resonance, morph;

        sst::filtersplusplus::FilterModel model{sst::filtersplusplus::FilterModel::None};
        sst::filtersplusplus::ModelConfig config{};

        std::vector<Param *> params()
        {
            std::vector<Param *> res{&cutoff, &resonance, &morph};
            return res;
        }
    } filterNodes[numFilters]{0, 1};

    struct RoutingNode
    {
        static constexpr uint32_t idBase{1000};

        RoutingNode()
            : feedback(floatMd()
                           .asCubicDecibelAttenuation()
                           .withGroupName("Routing")
                           .withName("Feedback")
                           .withID(id(0))),
              feedbackPower(intMd()
                                .asOnOffBool()
                                .withGroupName("Routing")
                                .withName("Feedback Power")
                                .withID(id(1))),
              routingMode(intMd()
                              .withRange(0, 4)
                              .withGroupName("Routing")
                              .withName("Mode")
                              .withID(3)
                              .withUnorderedMapFormatting({{0, "Serial"},
                                                           {1, "Serial (F1)"},
                                                           {2, "Par"},
                                                           {3, "Par / FB 1"},
                                                           {4, "Par / FB Each"}})),
              mix(floatMd()
                      .asPercent()
                      .withGroupName("Routing")
                      .withName("Mix")
                      .withDefault(1)
                      .withID(2)),
              inputGain(floatMd()
                            .asCubicDecibelUpTo(18)
                            .withGroupName("Routing")
                            .withName("Pre Gain")
                            .withID(5)),
              outputGain(floatMd()
                             .asCubicDecibelUpTo(18)
                             .withGroupName("Routing")
                             .withName("Post Gain")
                             .withID(6))
        {
        }

        uint32_t id(int f) const { return idBase + f; }

        Param feedback, feedbackPower;
        Param mix;
        Param routingMode;
        Param inputGain, outputGain;

        std::vector<Param *> params()
        {
            std::vector<Param *> res{&feedback, &feedbackPower, &routingMode,
                                     &mix,      &inputGain,     &outputGain};
            return res;
        }
    } routingNode;

    struct MorphTargetMixin
    {
        MorphTargetMixin(const std::string gn, int id0) :
        toCO{
            floatMd().asSemitoneRange(-36, 36).withDefault(0)
            .withGroupName(gn).withName("To F1 CO")
            .withID(id0),

            floatMd().asSemitoneRange(-36, 36).withDefault(0)
            .withGroupName(gn).withName("To F2 CO")
            .withID(id0 + 5),
        },
        toRes{
            floatMd().asPercentBipolar()
            .withGroupName(gn).withName("To F1 Res")
            .withID(id0 + 1),

            floatMd().asPercentBipolar()
            .withGroupName(gn).withName("To F2 Res")
            .withID(id0 + 6),
        },

        toMorph{
            floatMd().asPercentBipolar()
            .withGroupName(gn).withName("To F1 Morph")
            .withID(id0 + 2),

            floatMd().asPercentBipolar()
            .withGroupName(gn).withName("To F2 Morph")
            .withID(id0 + 7),
        },
        toFB(floatMd().asPercentBipolar()
            .withGroupName(gn).withName("To FB")
            .withID(id0 + 10)),

        toMix(floatMd().asPercentBipolar()
            .withGroupName(gn).withName("To Mix")
            .withID(id0 + 11))
        {
        }

        Param toCO[numFilters], toRes[numFilters], toMorph[numFilters];
        Param toFB, toMix;

        std::vector<Param *> params()
        {
            std::vector<Param *> res = {&toFB, &toMix};
            res.push_back(&toCO[0]);
            res.push_back(&toCO[1]);
            res.push_back(&toRes[0]);
            res.push_back(&toRes[1]);
            res.push_back(&toMorph[0]);
            res.push_back(&toMorph[1]);

            return res;
        }
    };
    struct StepLFONode : MorphTargetMixin
    {
        static constexpr uint32_t idBase{5000};
        static constexpr uint32_t idStride{200};

        StepLFONode(int i)
            : rate(floatMd()
                       .asLfoRate()
                       .withGroupName(gn(i))
                       .withDefault(i == 0 ? 2 : 3)
                       .withName("Rate")
                       .withID(id(0, i))),
              smooth(floatMd()
                         .withRange(-2, 2)
                         .withLinearScaleFormatting("")
                         .withDefault(0)
                         .withGroupName(gn(i))
                         .withName("Smooth")
                         .withID(id(1, i))),
              stepCount(intMd()
                            .withRange(1, maxSteps)
                            .withDefault(16)
                            .withGroupName(gn(i))
                            .withName("Steps")
                            .withID(id(2, i))),
              steps(sst::cpputils::make_array_lambda<Param, maxSteps>(
                  [i, this](auto idx)
                  {
                      return Param(floatMd()
                                       .asPercentBipolar()
                                       .withGroupName(gn(i))
                                       .withName("Step " + std::to_string(i + 1) + "." +
                                                 std::to_string(idx + 1))
                                       .withID(id(100 + idx, i)));
                  })),
              MorphTargetMixin(gn(i), id(20, i))
        {
        }

        std::string gn(int i) const { return "Step LFO " + std::to_string(i + 1); }
        uint32_t id(int f, int i) const { return idBase + f + i * idStride; }

        Param rate, smooth, stepCount;
        std::array<Param, maxSteps> steps;

        std::vector<Param *> params()
        {
            auto res = MorphTargetMixin::params();
            std::vector<Param *> lres{&rate, &smooth, &stepCount};
            for (auto &p : lres)
                res.push_back(p);
            for (auto &p : steps)
                res.push_back(&p);
            return res;
        }
    } stepLfoNodes[numStepLFOs]{0, 1};

    char name[256]{"Init"};

    float migrateParamValueFromVersion(Param *p, float value, uint32_t version);
    void migratePatchFromVersion(uint32_t version);

    void additionalToStateImpl(TiXmlElement &root);
    void additionalFromStateImpl(TiXmlElement *root, uint32_t version);
};
} // namespace baconpaul::twofilters
#endif // PATCH_H
