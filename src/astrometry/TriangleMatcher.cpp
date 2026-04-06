#include "TriangleMatcher.h"
#include "../core/ThreadState.h"

#include <vector>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <cstring>
#include <numeric>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// Construction
// ============================================================================

TriangleMatcher::TriangleMatcher()
{
    m_maxStars       = AT_MATCH_CATALOG_NBRIGHT;
    m_maxImgStars    = AT_MATCH_NBRIGHT;
    m_triangleRadius = AT_TRIANGLE_RADIUS;
}

// ============================================================================
// Star magnitude comparator (strict weak ordering)
// ============================================================================

static bool compareStarsMag(const MatchStar& a, const MatchStar& b)
{
    const bool aFinite = std::isfinite(a.mag);
    const bool bFinite = std::isfinite(b.mag);

    // Finite magnitudes sort before non-finite values
    if (aFinite != bFinite)
        return aFinite;

    // Deterministic tie-breaking for strict-weak-order compliance
    if (a.mag == b.mag) {
        if (a.id == b.id)
            return a.index < b.index;
        return a.id < b.id;
    }

    return a.mag < b.mag;
}

// ============================================================================
// gaussSolve --- Gaussian elimination with partial pivoting
// ============================================================================

bool TriangleMatcher::gaussSolve(double* matrix, int n, double* vector)
{
    // Forward elimination with partial pivoting
    for (int i = 0; i < n; i++) {
        // Find the pivot row
        double biggest   = -1.0;
        int    pivot_row = -1;
        for (int j = i; j < n; j++) {
            double val = std::abs(matrix[j * n + i]);
            if (val > biggest) {
                biggest   = val;
                pivot_row = j;
            }
        }

        if (biggest < GAUSS_MATRIX_TOL || !std::isfinite(biggest))
            return false;  // Singular or numerically degenerate

        // Swap rows if the pivot is not on the diagonal
        if (pivot_row != i) {
            for (int k = i; k < n; k++)
                std::swap(matrix[i * n + k], matrix[pivot_row * n + k]);
            std::swap(vector[i], vector[pivot_row]);
        }

        // Eliminate entries below the pivot
        for (int j = i + 1; j < n; j++) {
            double factor = matrix[j * n + i] / matrix[i * n + i];
            if (!std::isfinite(factor)) return false;

            for (int k = i + 1; k < n; k++)
                matrix[j * n + k] -= factor * matrix[i * n + k];
            vector[j] -= factor * vector[i];
            matrix[j * n + i] = 0.0;
        }
    }

    // Back substitution
    for (int i = n - 1; i >= 0; i--) {
        double diag = matrix[i * n + i];
        if (std::abs(diag) < GAUSS_MATRIX_TOL || !std::isfinite(diag))
            return false;

        double sum = vector[i];
        for (int j = i + 1; j < n; j++)
            sum -= matrix[i * n + j] * vector[j];

        vector[i] = sum / diag;
        if (!std::isfinite(vector[i]))
            return false;
    }

    return true;
}

// ============================================================================
// calcTransCoords --- Apply linear transform to a single point
// ============================================================================

void TriangleMatcher::calcTransCoords(double x, double y,
                                      const GenericTrans& trans,
                                      double& newx, double& newy)
{
    newx = trans.x00 + trans.x10 * x + trans.x01 * y;
    newy = trans.y00 + trans.y10 * x + trans.y01 * y;
}

// ============================================================================
// setTriangle --- Populate a MatchTriangle from three stars and a distance matrix
// ============================================================================

void TriangleMatcher::setTriangle(MatchTriangle& tri,
                                  const std::vector<MatchStar>& stars,
                                  int s1, int s2, int s3,
                                  const double* distMatrix, int n)
{
    double d12 = distMatrix[s1 * n + s2];
    double d23 = distMatrix[s2 * n + s3];
    double d13 = distMatrix[s1 * n + s3];

    double a = 0.0, b = 0.0, c = 0.0;

    // Identify the longest side and assign vertex roles accordingly.
    // The vertex OPPOSITE the longest side is labelled 'a'.
    if ((d12 >= d23) && (d12 >= d13)) {
        tri.a_index = stars[s3].index;
        a = d12;
        if (d23 >= d13) {
            tri.b_index = stars[s1].index; b = d23;
            tri.c_index = stars[s2].index; c = d13;
        } else {
            tri.b_index = stars[s2].index; b = d13;
            tri.c_index = stars[s1].index; c = d23;
        }
    } else if ((d23 > d12) && (d23 >= d13)) {
        tri.a_index = stars[s1].index;
        a = d23;
        if (d12 > d13) {
            tri.b_index = stars[s3].index; b = d12;
            tri.c_index = stars[s2].index; c = d13;
        } else {
            tri.b_index = stars[s2].index; b = d13;
            tri.c_index = stars[s3].index; c = d12;
        }
    } else {
        tri.a_index = stars[s2].index;
        a = d13;
        if (d12 > d23) {
            tri.b_index = stars[s3].index; b = d12;
            tri.c_index = stars[s1].index; c = d23;
        } else {
            tri.b_index = stars[s1].index; b = d23;
            tri.c_index = stars[s3].index; c = d12;
        }
    }

    tri.a_length = a;
    if (a > 0.0) {
        tri.ba = b / a;
        tri.ca = c / a;
    } else {
        tri.ba = 1.0;
        tri.ca = 1.0;
    }
}

// ============================================================================
// generateTriangles --- Build triangle array from brightest stars
// ============================================================================

std::vector<MatchTriangle> TriangleMatcher::generateTriangles(
    const std::vector<MatchStar>& stars, int nbright)
{
    // Hard cap at 150 stars to prevent O(n^3) triangle explosion
    int n = std::min((int)stars.size(), std::min(nbright, 150));
    std::vector<MatchTriangle> triangles;
    if (n < 3) return triangles;

    // Step 1: Compute pairwise distance matrix
    std::vector<double> distMatrix(n * n, 0.0);
    for (int i = 0; i < n - 1; i++) {
        for (int j = i + 1; j < n; j++) {
            double dx   = stars[i].x - stars[j].x;
            double dy   = stars[i].y - stars[j].y;
            double dist = std::sqrt(dx * dx + dy * dy);
            distMatrix[i * n + j] = dist;
            distMatrix[j * n + i] = dist;
        }
    }

    // Step 2: Enumerate all C(n,3) triangles
    int numTriangles = (n * (n - 1) * (n - 2)) / 6;
    triangles.reserve(numTriangles);

    int triId = 0;
    for (int i = 0; i < n - 2; i++) {
        for (int j = i + 1; j < n - 1; j++) {
            for (int k = j + 1; k < n; k++) {
                MatchTriangle t;
                t.id = triId++;
                setTriangle(t, stars, i, j, k, distMatrix.data(), n);
                triangles.push_back(t);
            }
        }
    }

    // Step 3: Prune near-degenerate triangles (b/a > AT_MATCH_RATIO)
    triangles.erase(
        std::remove_if(triangles.begin(), triangles.end(),
                        [](const MatchTriangle& t) {
                            return t.ba > AT_MATCH_RATIO;
                        }),
        triangles.end());

    // Step 4: Sort by b/a for efficient binary-search lookup
    std::sort(triangles.begin(), triangles.end(),
              [](const MatchTriangle& a, const MatchTriangle& b) {
                  return a.ba < b.ba;
              });

    return triangles;
}

// ============================================================================
// computeVotes --- Triangle-voting star correspondence
// ============================================================================

std::vector<int> TriangleMatcher::computeVotes(
    const std::vector<MatchTriangle>& triA,
    const std::vector<MatchTriangle>& triB,
    int numStarsA, int numStarsB,
    double minScale, double maxScale)
{
    // Clamp dimensions to prevent oversized allocations
    int clampedA = std::min(numStarsA, AT_MATCH_MAX_VOTE_DIM);
    int clampedB = std::min(numStarsB, AT_MATCH_MAX_VOTE_DIM);

    if (clampedA <= 0 || clampedB <= 0)
        return std::vector<int>();

    std::vector<int> votes;
    try {
        votes.assign(static_cast<size_t>(clampedA) * clampedB, 0);
    } catch (const std::bad_alloc&) {
        return std::vector<int>();
    }

    numStarsA = clampedA;
    numStarsB = clampedB;

    double rad2           = m_triangleRadius * m_triangleRadius;
    bool   useScaleFilter = (minScale > 0 && maxScale > 0);
    int    totalB         = (int)triB.size();

    // For each catalog triangle, find matching image triangles via binary search
    for (int i = 0; i < totalB; i++) {
        if (!Threading::getThreadRun()) return votes;

        const auto& tb = triB[i];
        double ba_min = tb.ba - m_triangleRadius;
        double ba_max = tb.ba + m_triangleRadius;

        // Binary search for the lower bound on b/a
        auto it_start = std::lower_bound(
            triA.begin(), triA.end(), ba_min,
            [](const MatchTriangle& t, double val) { return t.ba < val; });

        for (auto it = it_start; it != triA.end(); ++it) {
            if (it->ba > ba_max) break;

            // Shape proximity check in (b/a, c/a) space
            double d_ba = it->ba - tb.ba;
            double d_ca = it->ca - tb.ca;
            if (d_ba * d_ba + d_ca * d_ca >= rad2) continue;

            // Optional scale filter: ratio of longest sides
            if (useScaleFilter) {
                double ratio = it->a_length / tb.a_length;
                if (ratio < minScale || ratio > maxScale) continue;
            }

            // Cast votes for each vertex-pair correspondence
            if (it->a_index >= 0 && it->a_index < numStarsA &&
                tb.a_index >= 0 && tb.a_index < numStarsB)
                votes[it->a_index * numStarsB + tb.a_index]++;

            if (it->b_index >= 0 && it->b_index < numStarsA &&
                tb.b_index >= 0 && tb.b_index < numStarsB)
                votes[it->b_index * numStarsB + tb.b_index]++;

            if (it->c_index >= 0 && it->c_index < numStarsA &&
                tb.c_index >= 0 && tb.c_index < numStarsB)
                votes[it->c_index * numStarsB + tb.c_index]++;
        }
    }

    return votes;
}

// ============================================================================
// calcTransLinear --- Solve for affine transform via least-squares
// ============================================================================

bool TriangleMatcher::calcTransLinear(
    int nbright,
    const std::vector<MatchStar>& starsA,
    const std::vector<MatchStar>& starsB,
    const std::vector<int>& idxA,
    const std::vector<int>& idxB,
    GenericTrans& trans)
{
    // Count valid, in-bounds pairs
    int n     = 0;
    int limit = std::min(nbright, std::min((int)idxA.size(), (int)idxB.size()));

    for (int i = 0; i < limit; i++) {
        if (idxA[i] >= 0 && idxB[i] >= 0 &&
            idxA[i] < (int)starsA.size() && idxB[i] < (int)starsB.size())
            n++;
    }
    if (n < AT_MATCH_REQUIRE_LINEAR) return false;

    // Accumulate normal-equation sums
    double sum1  = 0, sumx  = 0, sumy  = 0;
    double sumx2 = 0, sumy2 = 0, sumxy = 0;
    double sumxp = 0, sumxpx = 0, sumxpy = 0;
    double sumyp = 0, sumypx = 0, sumypy = 0;

    for (int i = 0; i < limit; i++) {
        if (idxA[i] < 0 || idxB[i] < 0) continue;
        if (idxA[i] >= (int)starsA.size() || idxB[i] >= (int)starsB.size())
            continue;

        double xa = starsA[idxA[i]].x;
        double ya = starsA[idxA[i]].y;
        double xb = starsB[idxB[i]].x;
        double yb = starsB[idxB[i]].y;

        sum1  += 1.0;
        sumx  += xa;     sumy  += ya;
        sumx2 += xa*xa;  sumy2 += ya*ya;  sumxy += xa*ya;
        sumxp += xb;     sumxpx += xb*xa; sumxpy += xb*ya;
        sumyp += yb;     sumypx += yb*xa; sumypy += yb*ya;
    }

    // Solve for the X' coefficients: x' = x00 + x10*x + x01*y
    double matX[9] = {
        sum1, sumx, sumy,
        sumx, sumx2, sumxy,
        sumy, sumxy, sumy2
    };
    double vecX[3] = { sumxp, sumxpx, sumxpy };
    if (!gaussSolve(matX, 3, vecX)) return false;

    // Solve for the Y' coefficients: y' = y00 + y10*x + y01*y
    double matY[9] = {
        sum1, sumx, sumy,
        sumx, sumx2, sumxy,
        sumy, sumxy, sumy2
    };
    double vecY[3] = { sumyp, sumypx, sumypy };
    if (!gaussSolve(matY, 3, vecY)) return false;

    trans.x00 = vecX[0];  trans.x10 = vecX[1];  trans.x01 = vecX[2];
    trans.y00 = vecY[0];  trans.y10 = vecY[1];  trans.y01 = vecY[2];
    trans.order = 1;
    trans.nr    = n;

    return true;
}

// ============================================================================
// iterTrans --- Iterative transform fitting with sigma-clipping
// ============================================================================

bool TriangleMatcher::iterTrans(
    int nbright,
    const std::vector<MatchStar>& starsA,
    const std::vector<MatchStar>& starsB,
    std::vector<int>& winnerIndexA,
    std::vector<int>& winnerIndexB,
    bool recalc,
    GenericTrans& trans)
{
    int required_pairs = AT_MATCH_REQUIRE_LINEAR;
    int start_pairs    = AT_MATCH_STARTN_LINEAR;

    if (nbright < required_pairs) return false;

    // Clamp to the actual size of the index vectors
    nbright = std::min(nbright,
                       std::min((int)winnerIndexA.size(),
                                (int)winnerIndexB.size()));
    if (nbright < required_pairs) return false;

    // On first call (RECALC_NO), start with a small subset of top pairs.
    // On recalculation (RECALC_YES), use all available pairs.
    int use_pairs = recalc ? nbright : std::min(nbright, start_pairs);

    // Compute initial transform
    if (!calcTransLinear(use_pairs, starsA, starsB,
                         winnerIndexA, winnerIndexB, trans))
        return false;

    // Iterative sigma-clipping loop
    int    nr                 = nbright;
    double max_dist2_absolute = AT_MATCH_MAXDIST * AT_MATCH_MAXDIST;
    int    is_ok              = 0;
    int    iters_so_far       = 0;

    while (iters_so_far < AT_MATCH_MAXITER) {
        // Compute residuals for all surviving pairs
        std::vector<double> dist2(nr);
        for (int i = 0; i < nr; i++) {
            int iA = winnerIndexA[i];
            int iB = winnerIndexB[i];

            if (iA < 0 || iA >= (int)starsA.size() ||
                iB < 0 || iB >= (int)starsB.size())
            {
                dist2[i] = max_dist2_absolute + 1.0;
                continue;
            }

            double newx, newy;
            calcTransCoords(starsA[iA].x, starsA[iA].y, trans, newx, newy);
            double dx = newx - starsB[iB].x;
            double dy = newy - starsB[iB].y;
            dist2[i]  = dx * dx + dy * dy;
        }

        // Pass 1: Remove pairs exceeding the absolute distance threshold
        int nb = 0;
        {
            int writePos = 0;
            for (int i = 0; i < nr; i++) {
                if (dist2[i] <= max_dist2_absolute) {
                    winnerIndexA[writePos] = winnerIndexA[i];
                    winnerIndexB[writePos] = winnerIndexB[i];
                    dist2[writePos]        = dist2[i];
                    writePos++;
                } else {
                    nb++;
                }
            }
            nr = writePos;
        }

        if (nr < required_pairs) return false;

        // Compute sigma at the configured percentile
        double sigma;
        if (nr < 2) {
            sigma = 0.0;
        } else {
            std::vector<double> dist2_sorted(dist2.begin(),
                                              dist2.begin() + nr);
            std::sort(dist2_sorted.begin(), dist2_sorted.end());

            int percentile_idx = (int)std::floor(nr * AT_MATCH_PERCENTILE + 0.5);
            percentile_idx = std::clamp(percentile_idx, 0, nr - 1);
            sigma = dist2_sorted[percentile_idx];
        }

        if (sigma <= AT_MATCH_HALTSIGMA)
            is_ok = 1;

        // Pass 2: Remove pairs exceeding the sigma-clipping threshold
        double threshold = AT_MATCH_NSIGMA * sigma;
        {
            int writePos = 0;
            for (int i = 0; i < nr; i++) {
                if (dist2[i] <= threshold) {
                    winnerIndexA[writePos] = winnerIndexA[i];
                    winnerIndexB[writePos] = winnerIndexB[i];
                    dist2[writePos]        = dist2[i];
                    writePos++;
                } else {
                    nb++;
                }
            }
            nr = writePos;
        }

        // Resize index vectors to the surviving count
        winnerIndexA.resize(nr);
        winnerIndexB.resize(nr);

        // If no pairs were removed, the solution is stable
        if (nb == 0) is_ok = 1;

        if (nr < required_pairs) return false;

        // Recompute transform with surviving pairs
        if (!calcTransLinear(nr, starsA, starsB,
                             winnerIndexA, winnerIndexB, trans))
            return false;

        iters_so_far++;
        if (is_ok) break;
    }

    trans.nr = nr;
    return true;
}

// ============================================================================
// applyTransAndMatch --- Transform image stars, find nearest catalog matches
// ============================================================================

int TriangleMatcher::applyTransAndMatch(
    const std::vector<MatchStar>& imgStars,
    const std::vector<MatchStar>& catStars,
    const GenericTrans& trans,
    double matchRadius,
    std::vector<MatchStar>& matchedListA,
    std::vector<MatchStar>& matchedListB)
{
    matchedListA.clear();
    matchedListB.clear();

    double matchRadSq = matchRadius * matchRadius;
    int    numA       = (int)imgStars.size();
    int    numB       = (int)catStars.size();
    if (numA <= 0 || numB <= 0) return 0;

    // Apply the transform to all image stars
    std::vector<MatchStar> transformedA(numA);
    for (int i = 0; i < numA; i++) {
        transformedA[i] = imgStars[i];
        calcTransCoords(imgStars[i].x, imgStars[i].y, trans,
                        transformedA[i].x, transformedA[i].y);
    }

    // 1-to-1 nearest-neighbour matching: each catalog star is matched
    // to at most one image star (the closest one within the radius).
    std::vector<int>    bestMatchA(numB, -1);
    std::vector<double> bestMatchDist(numB, matchRadSq);

    for (int i = 0; i < numA; i++) {
        double best_dist2 = matchRadSq;
        int    best_j     = -1;

        for (int j = 0; j < numB; j++) {
            double dx = transformedA[i].x - catStars[j].x;
            double dy = transformedA[i].y - catStars[j].y;
            double d2 = dx * dx + dy * dy;

            if (d2 < best_dist2) {
                best_dist2 = d2;
                best_j     = j;
            }
        }

        if (best_j >= 0) {
            if (bestMatchA[best_j] < 0 ||
                best_dist2 < bestMatchDist[best_j])
            {
                bestMatchA[best_j]    = i;
                bestMatchDist[best_j] = best_dist2;
            }
        }
    }

    // Collect matched pairs using the ORIGINAL (untransformed) image coordinates
    int num_matches = 0;
    for (int j = 0; j < numB; j++) {
        if (bestMatchA[j] >= 0) {
            matchedListA.push_back(imgStars[bestMatchA[j]]);
            matchedListB.push_back(catStars[j]);
            num_matches++;
        }
    }

    return num_matches;
}

// ============================================================================
// solve --- Main entry point for triangle-voting star matching
//
// Pipeline:
//   1. Filter, sort, and select the brightest stars from each list.
//   2. Generate triangles for both lists.
//   3. Build the vote matrix by comparing triangle shapes.
//   4. Extract the top vote-getters as candidate star correspondences.
//   5. Disqualify pairs below the minimum vote threshold.
//   6. Fit an initial transform (iterTrans, RECALC_NO) on vote winners.
//   7. Apply the transform to ALL stars and find nearest-neighbour matches.
//   8. Recalculate the transform (iterTrans, RECALC_YES) on matched pairs.
//   9. Second refinement round: tighter match radius, final recalculation.
//  10. Output the final matched pairs for use by the convergence loop.
// ============================================================================

bool TriangleMatcher::solve(
    const std::vector<MatchStar>& imgStars,
    const std::vector<MatchStar>& catStars,
    GenericTrans& resultTrans,
    std::vector<MatchStar>& outMatchedA,
    std::vector<MatchStar>& outMatchedB,
    double minScale, double maxScale)
{
    // Reset diagnostics
    m_lastMaxVote    = 0;
    m_lastValidPairs = 0;
    m_lastNmatched   = 0;
    m_lastFailStage  = -1;

    // === Step 1: Filter, sanitize, sort, and truncate ====================

    std::vector<MatchStar> cleanA;
    std::vector<MatchStar> cleanB;
    cleanA.reserve(imgStars.size());
    cleanB.reserve(catStars.size());

    for (const auto& s : imgStars) {
        if (!std::isfinite(s.x) || !std::isfinite(s.y)) continue;
        MatchStar t = s;
        if (!std::isfinite(t.mag)) t.mag = 99.0;
        cleanA.push_back(t);
    }
    for (const auto& s : catStars) {
        if (!std::isfinite(s.x) || !std::isfinite(s.y)) continue;
        MatchStar t = s;
        if (!std::isfinite(t.mag)) t.mag = 99.0;
        cleanB.push_back(t);
    }

    int nA = std::min((int)cleanA.size(), m_maxStars);
    int nB = std::min((int)cleanB.size(), m_maxStars);

    if (nA < AT_MATCH_STARTN_LINEAR || nB < AT_MATCH_STARTN_LINEAR) {
        m_lastFailStage = 0;
        return false;
    }

    std::sort(cleanA.begin(), cleanA.end(), compareStarsMag);
    std::sort(cleanB.begin(), cleanB.end(), compareStarsMag);
    cleanA.resize(nA);
    cleanB.resize(nB);

    // Reassign indices to [0, N) so that all triangle vertex indices
    // and vote matrix accesses are guaranteed to be in-bounds.
    std::vector<MatchStar> sA = std::move(cleanA);
    std::vector<MatchStar> sB = std::move(cleanB);
    for (int i = 0; i < nA; ++i) sA[i].index = i;
    for (int i = 0; i < nB; ++i) sB[i].index = i;

    int nbright = std::min(std::max(nA, nB), m_maxStars);

    // === Step 2: Generate triangles ======================================

    auto triA = generateTriangles(sA, std::min(nA, nbright));
    auto triB = generateTriangles(sB, std::min(nB, nbright));

    if (triA.empty() || triB.empty()) {
        m_lastFailStage = 0;
        return false;
    }

    // === Step 3: Compute vote matrix =====================================

    const int vA = std::min(nA, AT_MATCH_MAX_VOTE_DIM);
    const int vB = std::min(nB, AT_MATCH_MAX_VOTE_DIM);

    auto votes = computeVotes(triA, triB, vA, vB, minScale, maxScale);
    if (votes.empty()) {
        m_lastFailStage = 1;
        return false;
    }

    // === Step 4: Extract top vote-getters ================================

    std::vector<int> winnerIndexA(nbright, -1);
    std::vector<int> winnerIndexB(nbright, -1);
    std::vector<int> winnerVotes(nbright, 0);

    for (int k = 0; k < nbright; k++) {
        int max_vote = 0;
        int max_i = -1, max_j = -1;

        for (int i = 0; i < vA; i++) {
            for (int j = 0; j < vB; j++) {
                size_t offset = static_cast<size_t>(i) * vB + j;
                if (offset >= votes.size()) continue;

                int voteCount = votes[offset];
                if (voteCount > max_vote) {
                    max_vote = voteCount;
                    max_i    = i;
                    max_j    = j;
                }
            }
        }

        if (max_vote < 1) break;
        if (k == 0) m_lastMaxVote = max_vote;

        winnerVotes[k]  = max_vote;
        winnerIndexA[k] = max_i;
        winnerIndexB[k] = max_j;

        // Zero out the row and column of the winner (star already used)
        if (max_i >= 0 && max_i < vA) {
            for (int j = 0; j < vB; j++) {
                size_t offset = static_cast<size_t>(max_i) * vB + j;
                if (offset < votes.size()) votes[offset] = 0;
            }
        }
        if (max_j >= 0 && max_j < vB) {
            for (int i = 0; i < vA; i++) {
                size_t offset = static_cast<size_t>(i) * vB + max_j;
                if (offset < votes.size()) votes[offset] = 0;
            }
        }
    }

    // === Step 5: Disqualify pairs below the minimum vote threshold ========

    int validNbright = nbright;
    for (int i = 0; i < nbright; i++) {
        if (winnerVotes[i] < AT_MATCH_MINVOTES) {
            validNbright = i;
            break;
        }
    }
    m_lastValidPairs = validNbright;

    if (validNbright < AT_MATCH_STARTN_LINEAR) {
        m_lastFailStage = (m_lastMaxVote < 1) ? 1 : 2;
        return false;
    }

    // === Step 6: Initial iterTrans (RECALC_NO) on vote winners ===========

    winnerIndexA.resize(validNbright);
    winnerIndexB.resize(validNbright);

    if (!iterTrans(validNbright, sA, sB,
                   winnerIndexA, winnerIndexB, false, resultTrans))
    {
        m_lastFailStage = 3;
        return false;
    }

    // === Step 7: Apply transform to ALL stars, match with wide radius ====

    std::vector<MatchStar> matchedA, matchedB;
    int numMatches = applyTransAndMatch(imgStars, catStars, resultTrans,
                                        AT_MATCH_MAXDIST,
                                        matchedA, matchedB);
    resultTrans.nm = numMatches;
    m_lastNmatched = numMatches;

    if (numMatches < AT_MATCH_STARTN_LINEAR) {
        m_lastFailStage = 4;
        return false;
    }

    // === Step 8: Recalculate transform (RECALC_YES) on matched pairs =====

    std::vector<int> recalcIdxA(numMatches), recalcIdxB(numMatches);
    for (int i = 0; i < numMatches; i++) {
        recalcIdxA[i] = i;
        recalcIdxB[i] = i;
    }

    if (!iterTrans(numMatches, matchedA, matchedB,
                   recalcIdxA, recalcIdxB, true, resultTrans))
    {
        m_lastFailStage = 5;
        return false;
    }

    // === Step 9: Second refinement with tighter match radius =============

    matchedA.clear();
    matchedB.clear();
    numMatches = applyTransAndMatch(imgStars, catStars, resultTrans,
                                    AT_MATCH_RADIUS,
                                    matchedA, matchedB);
    resultTrans.nm = numMatches;
    m_lastNmatched = numMatches;

    if (numMatches < AT_MATCH_STARTN_LINEAR) {
        m_lastFailStage = 6;
        return false;
    }

    recalcIdxA.resize(numMatches);
    recalcIdxB.resize(numMatches);
    for (int i = 0; i < numMatches; i++) {
        recalcIdxA[i] = i;
        recalcIdxB[i] = i;
    }

    if (!iterTrans(numMatches, matchedA, matchedB,
                   recalcIdxA, recalcIdxB, true, resultTrans))
    {
        m_lastFailStage = 7;
        return false;
    }

    // === Step 10: Output final matched pairs for the convergence loop ====

    int resultCount = std::min((int)resultTrans.nr,
                               std::min((int)recalcIdxA.size(),
                                        (int)recalcIdxB.size()));

    std::vector<MatchStar> finalA; finalA.reserve(resultCount);
    std::vector<MatchStar> finalB; finalB.reserve(resultCount);

    for (int i = 0; i < resultCount; i++) {
        int iA = recalcIdxA[i];
        int iB = recalcIdxB[i];
        if (iA >= 0 && iA < (int)matchedA.size() &&
            iB >= 0 && iB < (int)matchedB.size())
        {
            finalA.push_back(matchedA[iA]);
            finalB.push_back(matchedB[iB]);
        }
    }

    outMatchedA    = finalA;
    outMatchedB    = finalB;
    resultTrans.nr = (int)outMatchedA.size();

    m_lastFailStage = 8;  // Success
    m_lastNmatched  = resultTrans.nr;

    return true;
}