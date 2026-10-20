/**
 * @file GHSAlgo.cpp
 * @brief Implementation of the Generalized Hyperbolic Stretch algorithm.
 *
 * The piecewise transfer function is defined over four regions:
 *   [0, LP)    -- Linear tail (shadow region).
 *   [LP, SP)   -- Lower midtone curve (below symmetry point).
 *   [SP, HP)   -- Upper midtone curve (above symmetry point).
 *   [HP, 1]    -- Linear tail (highlight region).
 *
 * Coefficients for each region are precomputed in setup() so that
 * per-pixel evaluation in compute() requires only a single branch
 * and one transcendental function call.
 *
 * This file is part of TStar, a Professional Astrophotography Processing Suite.
 * Copyright (C) 2026 Fabio Tempera (Ft2801)
 *
 * TStar is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * TStar is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with TStar.  If not, see <https://www.gnu.org/licenses/>.
 *
 * -------------------------------------------------------------------
 * Prominent Notice of Modification & Original Copyright:
 * This file is derived from or incorporates code from Siril (hosted at
 * https://gitlab.com/free-astro/siril), which is licensed under GPLv3+.
 * Modified by Ft2801 in 2026.
 * Original copyright (C) 2012-2026 team free-astro.
 * -------------------------------------------------------------------
 */

#include "GHSAlgo.h"

#include <cmath>
#include <iostream>
#include <algorithm>

#ifndef FLT_MAX
#define FLT_MAX 3.402823466e+38F
#endif

namespace GHSAlgo {

/* =========================================================================
 * Helper
 * ========================================================================= */

/** @brief Safe wrapper around std::log1p for consistency. */
inline float log1p_safe(float x) { return std::log1p(x); }

/* =========================================================================
 * Coefficient Precomputation
 * ========================================================================= */

void setup(GHSComputeParams& c, float B, float D, float LP,
           float SP, float HP, int stretchtype)
{
    /* No-op for identity stretches. */
    if (D == 0.0f || stretchtype == STRETCH_LINEAR) {
        c = {};
        return;
    }

    /* -----------------------------------------------------------------
     * PAYNE NORMAL STRETCH
     * ----------------------------------------------------------------- */
    if (stretchtype == STRETCH_PAYNE_NORMAL) {

        if (B == -1.0f) {
            /* Logarithmic case (B = -1). */
            c.qlp = -1.0f * log1p_safe(D * (SP - LP));
            c.q0  = c.qlp - D * LP / (1.0f + D * (SP - LP));
            c.qwp = log1p_safe(D * (HP - SP));
            c.q1  = c.qwp + D * (1.0f - HP) / (1.0f + D * (HP - SP));
            c.q   = 1.0f / (c.q1 - c.q0);

            c.b1 = (1.0f + D * (SP - LP)) / (D * c.q);
            c.a2 = (-c.q0) * c.q;
            c.b2 = -c.q;
            c.c2 = 1.0f + D * SP;
            c.d2 = -D;
            c.a3 = (-c.q0) * c.q;
            c.b3 = c.q;
            c.c3 = 1.0f - D * SP;
            c.d3 = D;
            c.a4 = (c.qwp - c.q0 - D * HP / (1.0f + D * (HP - SP))) * c.q;
            c.b4 = c.q * D / (1.0f + D * (HP - SP));

        } else if (B < 0.0f) {
            /* Negative-B power-law case. */
            B = -B;
            c.qlp = (1.0f - std::pow(1.0f + D * B * (SP - LP), (B - 1.0f) / B)) / (B - 1.0f);
            c.q0  = c.qlp - D * LP * std::pow(1.0f + D * B * (SP - LP), -1.0f / B);
            c.qwp = (std::pow(1.0f + D * B * (HP - SP), (B - 1.0f) / B) - 1.0f) / (B - 1.0f);
            c.q1  = c.qwp + D * (1.0f - HP) * std::pow(1.0f + D * B * (HP - SP), -1.0f / B);
            c.q   = 1.0f / (c.q1 - c.q0);

            c.b1 = D * std::pow(1.0f + D * B * (SP - LP), -1.0f / B) * c.q;
            c.a2 = (1.0f / (B - 1.0f) - c.q0) * c.q;
            c.b2 = -c.q / (B - 1.0f);
            c.c2 = 1.0f + D * B * SP;
            c.d2 = -D * B;
            c.e2 = (B - 1.0f) / B;
            c.a3 = (-1.0f / (B - 1.0f) - c.q0) * c.q;
            c.b3 = c.q / (B - 1.0f);
            c.c3 = 1.0f - D * B * SP;
            c.d3 = D * B;
            c.e3 = (B - 1.0f) / B;
            c.a4 = (c.qwp - c.q0 - D * HP * std::pow(1.0f + D * B * (HP - SP), -1.0f / B)) * c.q;
            c.b4 = D * std::pow(1.0f + D * B * (HP - SP), -1.0f / B) * c.q;

        } else if (B == 0.0f) {
            /* Exponential case (B = 0). */
            c.qlp = std::exp(-D * (SP - LP));
            c.q0  = c.qlp - D * LP * std::exp(-D * (SP - LP));
            c.qwp = 2.0f - std::exp(-D * (HP - SP));
            c.q1  = c.qwp + D * (1.0f - HP) * std::exp(-D * (HP - SP));
            c.q   = 1.0f / (c.q1 - c.q0);

            c.a1 = 0.0f;
            c.b1 = D * std::exp(-D * (SP - LP)) * c.q;
            c.a2 = -c.q0 * c.q;
            c.b2 = c.q;
            c.c2 = -D * SP;
            c.d2 = D;
            c.a3 = (2.0f - c.q0) * c.q;
            c.b3 = -c.q;
            c.c3 = D * SP;
            c.d3 = -D;
            c.a4 = (c.qwp - c.q0 - D * HP * std::exp(-D * (HP - SP))) * c.q;
            c.b4 = D * std::exp(-D * (HP - SP)) * c.q;

        } else {
            /* Positive-B power-law case. */
            c.qlp = std::pow(1.0f + D * B * (SP - LP), -1.0f / B);
            c.q0  = c.qlp - D * LP * std::pow(1.0f + D * B * (SP - LP), -(1.0f + B) / B);
            c.qwp = 2.0f - std::pow(1.0f + D * B * (HP - SP), -1.0f / B);
            c.q1  = c.qwp + D * (1.0f - HP) * std::pow(1.0f + D * B * (HP - SP), -(1.0f + B) / B);
            c.q   = 1.0f / (c.q1 - c.q0);

            c.b1 = D * std::pow(1.0f + D * B * (SP - LP), -(1.0f + B) / B) * c.q;
            c.a2 = -c.q0 * c.q;
            c.b2 = c.q;
            c.c2 = 1.0f + D * B * SP;
            c.d2 = -D * B;
            c.e2 = -1.0f / B;
            c.a3 = (2.0f - c.q0) * c.q;
            c.b3 = -c.q;
            c.c3 = 1.0f - D * B * SP;
            c.d3 = D * B;
            c.e3 = -1.0f / B;
            c.a4 = (c.qwp - c.q0 - D * HP * std::pow(1.0f + D * B * (HP - SP), -(B + 1.0f) / B)) * c.q;
            c.b4 = D * std::pow(1.0f + D * B * (HP - SP), -(B + 1.0f) / B) * c.q;
        }

    /* -----------------------------------------------------------------
     * PAYNE INVERSE STRETCH
     * ----------------------------------------------------------------- */
    } else if (stretchtype == STRETCH_PAYNE_INVERSE) {

        if (B == -1.0f) {
            c.qlp = -1.0f * log1p_safe(D * (SP - LP));
            c.q0  = c.qlp - D * LP / (1.0f + D * (SP - LP));
            c.qwp = log1p_safe(D * (HP - SP));
            c.q1  = c.qwp + D * (1.0f - HP) / (1.0f + D * (HP - SP));
            c.q   = 1.0f / (c.q1 - c.q0);

            c.LPT = (c.qlp - c.q0) * c.q;
            c.SPT = c.q0 * c.q;
            c.HPT = (c.qwp - c.q0) * c.q;

            c.b1 = (1.0f + D * (SP - LP)) / (D * c.q);
            c.a2 = (1.0f + D * SP) / D;
            c.b2 = -1.0f / D;
            c.c2 = -c.q0;
            c.d2 = -1.0f / c.q;
            c.a3 = -(1.0f - D * SP) / D;
            c.b3 = 1.0f / D;
            c.c3 = c.q0;
            c.d3 = 1.0f / c.q;
            c.a4 = HP + (c.q0 - c.qwp) * (1.0f + D * (HP - SP)) / D;
            c.b4 = (1.0f + D * (HP - SP)) / (c.q * D);

        } else if (B < 0.0f) {
            B = -B;
            c.qlp = (1.0f - std::pow(1.0f + D * B * (SP - LP), (B - 1.0f) / B)) / (B - 1.0f);
            c.q0  = c.qlp - D * LP * std::pow(1.0f + D * B * (SP - LP), -1.0f / B);
            c.qwp = (std::pow(1.0f + D * B * (HP - SP), (B - 1.0f) / B) - 1.0f) / (B - 1.0f);
            c.q1  = c.qwp + D * (1.0f - HP) * std::pow(1.0f + D * B * (HP - SP), -1.0f / B);
            c.q   = 1.0f / (c.q1 - c.q0);

            c.LPT = (c.qlp - c.q0) * c.q;
            c.SPT = -c.q0 * c.q;
            c.HPT = (c.qwp - c.q0) * c.q;

            c.b1 = std::pow(1.0f + D * B * (SP - LP), 1.0f / B) / (c.q * D);
            c.a2 = (1.0f + D * B * SP) / (D * B);
            c.b2 = -1.0f / (D * B);
            c.c2 = -c.q0 * (B - 1.0f) + 1.0f;
            c.d2 = (1.0f - B) / c.q;
            c.e2 = B / (B - 1.0f);
            c.a3 = (D * B * SP - 1.0f) / (D * B);
            c.b3 = 1.0f / (D * B);
            c.c3 = 1.0f + c.q0 * (B - 1.0f);
            c.d3 = (B - 1.0f) / c.q;
            c.e3 = B / (B - 1.0f);
            c.a4 = (c.q0 - c.qwp) / (D * std::pow(1.0f + D * B * (HP - SP), -1.0f / B)) + HP;
            c.b4 = 1.0f / (D * std::pow(1.0f + D * B * (HP - SP), -1.0f / B) * c.q);

        } else if (B == 0.0f) {
            c.qlp = std::exp(-D * (SP - LP));
            c.q0  = c.qlp - D * LP * std::exp(-D * (SP - LP));
            c.qwp = 2.0f - std::exp(-D * (HP - SP));
            c.q1  = c.qwp + D * (1.0f - HP) * std::exp(-D * (HP - SP));
            c.q   = 1.0f / (c.q1 - c.q0);

            c.LPT = (c.qlp - c.q0) * c.q;
            c.SPT = (1.0f - c.q0) * c.q;
            c.HPT = (c.qwp - c.q0) * c.q;

            c.a1 = 0.0f;
            c.b1 = 1.0f / (D * std::exp(-D * (SP - LP)) * c.q);
            c.a2 = SP;
            c.b2 = 1.0f / D;
            c.c2 = c.q0;
            c.d2 = 1.0f / c.q;
            c.a3 = SP;
            c.b3 = -1.0f / D;
            c.c3 = (2.0f - c.q0);
            c.d3 = -1.0f / c.q;
            c.a4 = (c.q0 - c.qwp) / (D * std::exp(-D * (HP - SP))) + HP;
            c.b4 = 1.0f / (D * std::exp(-D * (HP - SP)) * c.q);

        } else {
            c.qlp = std::pow(1.0f + D * B * (SP - LP), -1.0f / B);
            c.q0  = c.qlp - D * LP * std::pow(1.0f + D * B * (SP - LP), -(1.0f + B) / B);
            c.qwp = 2.0f - std::pow(1.0f + D * B * (HP - SP), -1.0f / B);
            c.q1  = c.qwp + D * (1.0f - HP) * std::pow(1.0f + D * B * (HP - SP), -(1.0f + B) / B);
            c.q   = 1.0f / (c.q1 - c.q0);

            c.LPT = (c.qlp - c.q0) * c.q;
            c.SPT = (1.0f - c.q0) * c.q;
            c.HPT = (c.qwp - c.q0) * c.q;

            c.b1 = 1.0f / (D * std::pow(1.0f + D * B * (SP - LP), -(1.0f + B) / B) * c.q);
            c.a2 = 1.0f / (D * B) + SP;
            c.b2 = -1.0f / (D * B);
            c.c2 = c.q0;
            c.d2 = 1.0f / c.q;
            c.e2 = -B;
            c.a3 = -1.0f / (D * B) + SP;
            c.b3 = 1.0f / (D * B);
            c.c3 = (2.0f - c.q0);
            c.d3 = -1.0f / c.q;
            c.e3 = -B;
            c.a4 = (c.q0 - c.qwp) / (D * std::pow(1.0f + D * B * (HP - SP), -(B + 1.0f) / B)) + HP;
            c.b4 = 1.0f / (D * std::pow(1.0f + D * B * (HP - SP), -(B + 1.0f) / B) * c.q);
        }

    /* -----------------------------------------------------------------
     * ASINH STRETCH
     * ----------------------------------------------------------------- */
    } else if (stretchtype == STRETCH_ASINH) {
        c.qlp = -std::log(D * (SP - LP) + std::sqrt(D * D * (SP - LP) * (SP - LP) + 1.0f));
        c.q0  = c.qlp - LP * D * std::pow(D * D * (SP - LP) * (SP - LP) + 1.0f, -0.5f);
        c.qwp = std::log(D * (HP - SP) + std::sqrt(D * D * (HP - SP) * (HP - SP) + 1.0f));
        c.q1  = c.qwp + (1.0f - HP) * D * std::pow(D * D * (HP - SP) * (HP - SP) + 1.0f, -0.5f);
        c.q   = 1.0f / (c.q1 - c.q0);

        c.a1 = 0.0f;
        c.b1 = D * std::pow(D * D * (SP - LP) * (SP - LP) + 1.0f, -0.5f) * c.q;
        c.a2 = -c.q0 * c.q;
        c.b2 = -c.q;
        c.c2 = -D;
        c.d2 = D * D;
        c.e2 = SP;
        c.a3 = -c.q0 * c.q;
        c.b3 = c.q;
        c.c3 = D;
        c.d3 = D * D;
        c.e3 = SP;
        c.a4 = (c.qwp - HP * D * std::pow(D * D * (HP - SP) * (HP - SP) + 1.0f, -0.5f) - c.q0) * c.q;
        c.b4 = D * std::pow(D * D * (HP - SP) * (HP - SP) + 1.0f, -0.5f) * c.q;

    /* -----------------------------------------------------------------
     * INVERSE ASINH STRETCH
     * ----------------------------------------------------------------- */
    } else if (stretchtype == STRETCH_INVASINH) {
        c.qlp = -std::log(D * (SP - LP) + std::sqrt(D * D * (SP - LP) * (SP - LP) + 1.0f));
        c.q0  = c.qlp - LP * D * std::pow(D * D * (SP - LP) * (SP - LP) + 1.0f, -0.5f);
        c.qwp = std::log(D * (HP - SP) + std::sqrt(D * D * (HP - SP) * (HP - SP) + 1.0f));
        c.q1  = c.qwp + (1.0f - HP) * D * std::pow(D * D * (HP - SP) * (HP - SP) + 1.0f, -0.5f);
        c.q   = 1.0f / (c.q1 - c.q0);

        c.a1 = 0.0f;
        c.b1 = D * std::pow(D * D * (SP - LP) * (SP - LP) + 1.0f, -0.5f) * c.q;
        c.a2 = -c.q0 * c.q;
        c.b2 = -c.q;
        c.c2 = -D;
        c.d2 = D * D;
        c.e2 = SP;
        c.a3 = -c.q0 * c.q;
        c.b3 = c.q;
        c.c3 = D;
        c.d3 = D * D;
        c.e3 = SP;
        c.a4 = (c.qwp - HP * D * std::pow(D * D * (HP - SP) * (HP - SP) + 1.0f, -0.5f) - c.q0) * c.q;
        c.b4 = D * std::pow(D * D * (HP - SP) * (HP - SP) + 1.0f, -0.5f) * c.q;

        /* Precompute piecewise boundaries for inverse evaluation. */
        c.LPT = c.a1 + c.b1 * LP;
        c.SPT = c.a2 + c.b2 * std::log(
            c.c2 * (SP - c.e2) + std::sqrt(c.d2 * (SP - c.e2) * (SP - c.e2) + 1.0f));
        c.HPT = c.a4 + c.b4 * HP;
    }
}

/* =========================================================================
 * Per-Pixel Stretch Evaluation
 * ========================================================================= */

float compute(float in, const GHSParams& params, const GHSComputeParams& c)
{
    float out = in;
    float res1, res2;

    const float B  = params.B;
    const float D  = params.D;
    const float LP = params.LP;
    const float SP = params.SP;
    const float HP = params.HP;
    const float BP = params.BP;
    const int   stretchtype = params.type;

    /* Apply black-point normalization. */
    in = std::max(0.0f, (in - BP) / (1.0f - BP));

    /* Identity stretches. */
    if (stretchtype == STRETCH_LINEAR) return in;
    if (D == 0.0f) return in;

    /* --- Payne Normal ---------------------------------------------------- */
    if (stretchtype == STRETCH_PAYNE_NORMAL) {
        if (B == -1.0f) {
            res1 = c.a2 + c.b2 * std::log(c.c2 + c.d2 * in);
            res2 = c.a3 + c.b3 * std::log(c.c3 + c.d3 * in);
        } else if (B < 0.0f || B > 0.0f) {
            res1 = c.a2 + c.b2 * std::pow(c.c2 + c.d2 * in, c.e2);
            res2 = c.a3 + c.b3 * std::pow(c.c3 + c.d3 * in, c.e3);
        } else {
            res1 = c.a2 + c.b2 * std::exp(c.c2 + c.d2 * in);
            res2 = c.a3 + c.b3 * std::exp(c.c3 + c.d3 * in);
        }
        out = (in < LP) ? c.b1 * in
            : (in < SP) ? res1
            : (in < HP) ? res2
            : c.a4 + c.b4 * in;

    /* --- Payne Inverse --------------------------------------------------- */
    } else if (stretchtype == STRETCH_PAYNE_INVERSE) {
        if (B == -1.0f) {
            res1 = c.a2 + c.b2 * std::exp(c.c2 + c.d2 * in);
            res2 = c.a3 + c.b3 * std::exp(c.c3 + c.d3 * in);
        } else if (B < 0.0f || B > 0.0f) {
            res1 = c.a2 + c.b2 * std::pow(c.c2 + c.d2 * in, c.e2);
            res2 = c.a3 + c.b3 * std::pow(c.c3 + c.d3 * in, c.e3);
        } else {
            res1 = c.a2 + c.b2 * std::log(c.c2 + c.d2 * in);
            res2 = c.a3 + c.b3 * std::log(c.c3 + c.d3 * in);
        }
        out = (in < c.LPT) ? c.b1 * in
            : (in < c.SPT) ? res1
            : (in < c.HPT) ? res2
            : c.a4 + c.b4 * in;

    /* --- Asinh ----------------------------------------------------------- */
    } else if (stretchtype == STRETCH_ASINH) {
        float val;
        val  = c.c2 * (in - c.e2) + std::sqrt(c.d2 * (in - c.e2) * (in - c.e2) + 1.0f);
        res1 = c.a2 + c.b2 * std::log(val);
        val  = c.c3 * (in - c.e3) + std::sqrt(c.d3 * (in - c.e3) * (in - c.e3) + 1.0f);
        res2 = c.a3 + c.b3 * std::log(val);
        out = (in < LP) ? c.a1 + c.b1 * in
            : (in < SP) ? res1
            : (in < HP) ? res2
            : c.a4 + c.b4 * in;

    /* --- Inverse Asinh --------------------------------------------------- */
    } else if (stretchtype == STRETCH_INVASINH) {
        float ex;
        ex   = std::exp((c.a2 - in) / c.b2);
        res1 = c.e2 - (ex - (1.0f / ex)) / (2.0f * c.c2);
        ex   = std::exp((c.a3 - in) / c.b3);
        res2 = c.e3 - (ex - (1.0f / ex)) / (2.0f * c.c3);
        out = (in < c.LPT) ? (in - c.a1) / c.b1
            : (in < c.SPT) ? res1
            : (in < c.HPT) ? res2
            : (in - c.a4) / c.b4;
    }

    return out;
}

/* =========================================================================
 * Buffer-Wide Application
 * ========================================================================= */

void applyToBuffer(std::vector<float>& data, const GHSParams& params)
{
    GHSComputeParams c;
    setup(c, params.B, params.D, params.LP, params.SP, params.HP, params.type);

    #pragma omp parallel for
    for (int i = 0; i < (int)data.size(); ++i) {
        data[i] = compute(data[i], params, c);
        data[i] = std::max(0.0f, std::min(1.0f, data[i]));
    }
}

} // namespace GHSAlgo