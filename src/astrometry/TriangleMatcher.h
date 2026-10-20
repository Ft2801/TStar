#ifndef TRIANGLEMATCHER_H
#define TRIANGLEMATCHER_H

// ============================================================================
// TriangleMatcher
//
// Geometric triangle-voting plate-solving engine. Matches two sets of 2D
// points (image stars and catalog stars) by comparing invariant triangle
// shape ratios (b/a, c/a). The algorithm:
//
//   1. Generates all triangles from the N brightest stars in each list.
//   2. Votes for star-pair correspondences when triangle shapes match.
//   3. Extracts top vote-getters and fits an affine transformation.
//   4. Iteratively refines the transform via sigma-clipped least squares.
//
// Based on the geometric voting technique described in Valdes et al. (1995)
// and the open-source astrometry implementations.
//
// This file is part of TStar, a Professional Astrophotography Processing Suite.
// Copyright (C) 2026 Fabio Tempera (Ft2801)
//
// TStar is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TStar is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TStar.  If not, see <https://www.gnu.org/licenses/>.
//
// -------------------------------------------------------------------
// Prominent Notice of Modification & Original Copyright:
// This file is a derivative of atpmatch.c (distributed with Siril at
// https://gitlab.com/free-astro/siril), originally licensed under GPLv2+.
// Modified by Ft2801 in 2026.
// Original copyright (C) 2012-2026 team free-astro.
// -------------------------------------------------------------------
// ============================================================================

#include <vector>
#include <cmath>
#include <algorithm>

// ============================================================================
// Compile-time constants
// ============================================================================

// Maximum radius in triangle-space (b/a, c/a) for shape matching
#define AT_TRIANGLE_RADIUS          0.002

// Matching radius after applying TRANS (arcsec)
#define AT_MATCH_RADIUS             5.0

// Maximum allowed residual distance in iterTrans (arcsec)
#define AT_MATCH_MAXDIST            50.0

// Sigma-clipping multiplier for outlier rejection in iterTrans
#define AT_MATCH_NSIGMA             10.0

// Percentile for sigma estimation in iterTrans clipping
#define AT_MATCH_PERCENTILE         0.35

// Percentile for the 1-sigma diagnostic (not used for clipping)
#define ONE_STDEV_PERCENTILE        0.683

// Halt threshold: stop iterating if sigma falls below this value
#define AT_MATCH_HALTSIGMA          0.1

// Maximum image stars used for triangle generation
#define AT_MATCH_NBRIGHT            20

// Maximum catalog stars used for triangle generation
#define AT_MATCH_CATALOG_NBRIGHT    60

// Minimum vote count for a star pair to be considered valid
#define AT_MATCH_MINVOTES           2

// Maximum iterations in the sigma-clipping loop
#define AT_MATCH_MAXITER            5

// Prune triangles with b/a ratio above this threshold (near-degenerate)
#define AT_MATCH_RATIO              0.9

// Minimum number of star pairs required to compute a linear fit
#define AT_MATCH_STARTN_LINEAR      6
#define AT_MATCH_REQUIRE_LINEAR     3

// Tolerance for pivot detection in Gaussian elimination
#define GAUSS_MATRIX_TOL            1.0e-4

// Sentinel value indicating no angle constraint
#define AT_MATCH_NOANGLE            -999.0

// Maximum vote matrix dimension to prevent oversized allocations
#define AT_MATCH_MAX_VOTE_DIM       500

// ============================================================================
// Data structures
// ============================================================================

/**
 * Represents a single star with position, magnitude, and match linkage.
 * Aligned to 16 bytes for cache-line efficiency.
 */
struct alignas(16) MatchStar {
    int    id       = 0;
    int    index    = 0;      // Position in the magnitude-sorted array
    double x        = 0;
    double y        = 0;
    double mag      = 0;
    int    match_id = -1;     // ID of the matched counterpart (-1 = unmatched)
    int    _pad     = 0;      // Explicit padding for 16-byte alignment
    double _res     = 0;      // Reserved (total struct size: 48 bytes)
};

/**
 * Represents a triangle formed by three stars. Vertex labels refer to
 * the sides opposite each vertex:
 *   a_index -> vertex opposite the longest  side (length = a_length)
 *   b_index -> vertex opposite the intermediate side
 *   c_index -> vertex opposite the shortest side
 */
struct alignas(16) MatchTriangle {
    int    id       = 0;
    int    a_index  = 0;      // Vertex opposite the longest side
    int    b_index  = 0;      // Vertex opposite the intermediate side
    int    c_index  = 0;      // Vertex opposite the shortest side
    double a_length = 0;      // Absolute length of the longest side
    double ba       = 0;      // Ratio b/a (range [0, 1])
    double ca       = 0;      // Ratio c/a (range [0, 1])
    double _res     = 0;      // Explicit padding (total: 48 bytes)
};

/**
 * First-order (affine) linear transformation:
 *   x' = x00 + x10 * x + x01 * y
 *   y' = y00 + y10 * x + y01 * y
 */
struct GenericTrans {
    double x00 = 0, x10 = 0, x01 = 0;
    double y00 = 0, y10 = 0, y01 = 0;
    int    order = 1;
    int    nr    = 0;     // Number of matched pairs after sigma-clipping
    int    nm    = 0;     // Number of matched pairs found by applyTransAndMatch
    double sx    = 0;     // RMS residual in X
    double sy    = 0;     // RMS residual in Y
};

// ============================================================================
// TriangleMatcher class
// ============================================================================

class TriangleMatcher {
public:
    TriangleMatcher();

    // -- Configuration ----------------------------------------------------

    /** Sets the maximum number of stars used for triangle generation. */
    void setMaxStars(int n) { m_maxStars = n; }

    /** Sets the maximum number of image stars for triangle generation. */
    void setMaxImgStars(int n) { m_maxImgStars = n; }

    /**
     * Sets the triangle matching radius in (b/a, c/a) space.
     * Default is AT_TRIANGLE_RADIUS (0.002). Increase for images with
     * significant lens distortion (e.g. 0.003).
     */
    void   setTriangleRadius(double r) { m_triangleRadius = r; }
    double triangleRadius() const      { return m_triangleRadius; }

    // -- Post-solve diagnostics -------------------------------------------

    int lastMaxVote()    const { return m_lastMaxVote; }
    int lastValidPairs() const { return m_lastValidPairs; }
    int lastNmatched()   const { return m_lastNmatched; }

    /**
     * Stage at which the last solve() call failed or succeeded:
     *   0 = insufficient input stars
     *   1 = no triangle votes (or vote allocation failed)
     *   2 = too few vote-winners above minimum threshold
     *   3 = initial iterTrans failed
     *   4 = first applyTransAndMatch yielded too few matches
     *   5 = first recalculation iterTrans failed
     *   6 = second applyTransAndMatch yielded too few matches
     *   7 = second recalculation iterTrans failed
     *   8 = success
     */
    int lastFailStage() const { return m_lastFailStage; }

    // -- Main solve interface ---------------------------------------------

    /**
     * Performs triangle-voting star matching between image and catalog stars.
     *
     * @param imgStars     Image star list (centered pixel coordinates).
     * @param catStars     Catalog star list (projected tangent-plane coordinates).
     * @param resultTrans  Output: fitted affine transformation.
     * @param outMatchedA  Output: matched image stars (original coordinates).
     * @param outMatchedB  Output: matched catalog stars (projected coordinates).
     * @param minScale     Minimum allowed scale ratio (-1 to disable).
     * @param maxScale     Maximum allowed scale ratio (-1 to disable).
     * @return             True on success.
     */
    bool solve(const std::vector<MatchStar>& imgStars,
               const std::vector<MatchStar>& catStars,
               GenericTrans& resultTrans,
               std::vector<MatchStar>& outMatchedA,
               std::vector<MatchStar>& outMatchedB,
               double minScale = -1, double maxScale = -1);

    /**
     * Iteratively fits a linear transform with sigma-clipping outlier rejection.
     *
     * @param nbright       Number of star pairs to consider.
     * @param starsA        First star list (image stars).
     * @param starsB        Second star list (catalog stars).
     * @param winnerIndexA  Indices into starsA (modified in place).
     * @param winnerIndexB  Indices into starsB (modified in place).
     * @param recalc        If true (RECALC_YES), use all pairs from the start.
     *                      If false (RECALC_NO), start with a small subset.
     * @param trans         Output: fitted transformation.
     * @return              True on success.
     */
    bool iterTrans(int nbright,
                   const std::vector<MatchStar>& starsA,
                   const std::vector<MatchStar>& starsB,
                   std::vector<int>& winnerIndexA,
                   std::vector<int>& winnerIndexB,
                   bool recalc,
                   GenericTrans& trans);

    /**
     * Applies a transform to all image stars and finds nearest-neighbour
     * matches in the catalog star list within the given radius.
     * Uses 1-to-1 matching (each catalog star matched at most once).
     *
     * @return Number of matched pairs.
     */
    int applyTransAndMatch(const std::vector<MatchStar>& imgStars,
                           const std::vector<MatchStar>& catStars,
                           const GenericTrans& trans,
                           double matchRadius,
                           std::vector<MatchStar>& matchedListA,
                           std::vector<MatchStar>& matchedListB);

private:
    // -- Configuration state ----------------------------------------------
    int    m_maxStars       = AT_MATCH_CATALOG_NBRIGHT;
    int    m_maxImgStars    = AT_MATCH_NBRIGHT;
    double m_triangleRadius = AT_TRIANGLE_RADIUS;

    // -- Diagnostics (updated on every solve() call) ----------------------
    int m_lastMaxVote    = 0;
    int m_lastValidPairs = 0;
    int m_lastNmatched   = 0;
    int m_lastFailStage  = -1;

    // -- Internal methods -------------------------------------------------

    /** Generates all triangles from the brightest N stars. */
    std::vector<MatchTriangle> generateTriangles(
        const std::vector<MatchStar>& stars, int nbright);

    /** Fills a single MatchTriangle from three star indices and a distance matrix. */
    void setTriangle(MatchTriangle& tri,
                     const std::vector<MatchStar>& stars,
                     int s1, int s2, int s3,
                     const double* distMatrix, int numStars);

    /** Computes the vote matrix by comparing triangles from both lists. */
    std::vector<int> computeVotes(
        const std::vector<MatchTriangle>& triA,
        const std::vector<MatchTriangle>& triB,
        int numStarsA, int numStarsB,
        double minScale, double maxScale);

    /** Solves for linear transform coefficients via Gaussian elimination. */
    bool calcTransLinear(int nbright,
                         const std::vector<MatchStar>& starsA,
                         const std::vector<MatchStar>& starsB,
                         const std::vector<int>& idxA,
                         const std::vector<int>& idxB,
                         GenericTrans& trans);

    /** Applies a linear transform to a single (x, y) point. */
    static void calcTransCoords(double x, double y,
                                const GenericTrans& trans,
                                double& newx, double& newy);

    /**
     * Gaussian elimination with partial pivoting.
     * Solves the system matrix * x = vector in-place (result stored in vector).
     * @param matrix  Flat n*n array (row-major).
     */
    static bool gaussSolve(double* matrix, int n, double* vector);
};

#endif // TRIANGLEMATCHER_H