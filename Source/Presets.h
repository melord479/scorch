#pragma once

#include <vector>
#include "BassDrive.h"

namespace bd
{
struct Preset
{
    const char* name;
    Params p;
};

inline Params P(float drive, int mode, float bias, float fold, float crush, float decim,
                float punch, float split, float sub, bool mono, float lowCut, float highCut,
                float ceil, float width, float mix, float out, int os)
{
    Params q;
    q.driveDb = drive; q.mode = mode; q.bias = bias; q.fold = fold; q.crush = crush; q.decimate = decim;
    q.punchDb = punch; q.splitHz = split; q.sub = sub; q.monoSub = mono; q.lowCutHz = lowCut;
    q.highCutHz = highCut; q.ceilDb = ceil; q.width = width; q.mix = mix; q.outDb = out; q.os = os;
    return q;
}

inline const std::vector<Preset>& presets()
{
    //                          drive mode  bias  fold crush dec  punch split sub  mono  lc   hc     ceil wid  mix out os
    static const std::vector<Preset> list = {
        { "Init",          P(12, Tube,  0.00f, 0.00f, 0.00f, 0.00f,  0.f, 120.f, 0.50f, true,  20.f, 20000.f, -1.f,  1.0f, 1.f,  0.f, 1) },
        { "Rage 808",      P(30, Fuzz,  0.20f, 0.12f, 0.00f, 0.00f,  3.f, 110.f, 0.60f, true,  20.f, 12000.f, -4.f,  1.2f, 1.f,  3.f, 1) },
        { "Opium Grit",    P(26, Rage,  0.30f, 0.25f, 0.08f, 0.10f,  2.f, 130.f, 0.55f, true,  25.f,  9000.f, -3.f,  1.4f, 1.f,  2.f, 1) },
        { "Carson Crush",  P(36, Hard,  0.10f, 0.00f, 0.50f, 0.30f,  4.f,  95.f, 0.80f, true,  25.f,  7500.f, -5.f,  1.2f, 1.f,  4.f, 1) },
        { "Hyper Fold",    P(22, Fold,  0.15f, 0.55f, 0.00f, 0.00f,  0.f, 140.f, 0.50f, true,  20.f, 14000.f, -3.f,  1.6f, 1.f,  2.f, 1) },
        { "Sub Saver",     P(18, Tube,  0.00f, 0.00f, 0.00f, 0.00f,  2.f,  90.f, 0.95f, true,  20.f, 16000.f, -0.5f, 1.0f, 1.f,  0.f, 1) },
        { "Wall Of Bass",  P(42, Rage,  0.35f, 0.40f, 0.15f, 0.00f,  3.f, 120.f, 0.40f, true,  30.f,  8000.f, -6.f,  1.3f, 1.f,  5.f, 1) },
        { "Vamp Fuzz",     P(28, Fuzz,  0.55f, 0.10f, 0.00f, 0.00f,  1.f, 100.f, 0.65f, true,  20.f, 10000.f, -3.f,  1.0f, 1.f,  2.f, 1) },
        { "Blown Speaker", P(38, Hard, -0.20f, 0.20f, 0.30f, 0.45f,  0.f, 150.f, 0.45f, true,  60.f,  4500.f, -4.f,  0.8f, 1.f,  3.f, 1) },
        { "Warm Tube",     P(10, Tube,  0.10f, 0.00f, 0.00f, 0.00f,  2.f, 100.f, 0.80f, true,  20.f, 18000.f, -1.f,  1.0f, 1.f,  0.f, 1) },
    };
    return list;
}

} // namespace bd
