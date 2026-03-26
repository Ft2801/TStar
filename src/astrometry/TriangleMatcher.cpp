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

TriangleMatcher::TriangleMatcher() {
    m_maxStars     = AT_MATCH_CATALOG_NBRIGHT; // 60 catalog stars
    m_maxImgStars  = AT_MATCH_NBRIGHT;         // 20 image stars
    m_triangleRadius = AT_TRIANGLE_RADIUS;     // 0.002 in (ba,ca) space
}

static bool compareStarsMag(const MatchStar& a, const MatchStar& b) {
    return a.mag < b.mag;
}

// ============================================================================
// gaussSolve — Gaussian elimination with partial pivoting
// Solves in-place: matrix[n][n] * x = vector[n], result → vector
// ============================================================================
bool TriangleMatcher::gaussSolve(std::vector<std::vector<double>>& matrix, int n,
                                  std::vector<double>& vector)
{
    // Forward elimination with partial pivoting
    for (int i = 0; i < n; i++) {
        // Find pivot
        double biggest = -1.0;
        int pivot_row = -1;
        for (int j = i; j < n; j++) {
            if (std::abs(matrix[j][i]) > biggest) {
                biggest = std::abs(matrix[j][i]);
                pivot_row = j;
            }
        }
        if (biggest < GAUSS_MATRIX_TOL) {
            return false; // Singular or near-singular
        }

        // Swap rows if needed
        if (pivot_row != i) {
            std::swap(matrix[i], matrix[pivot_row]);
            std::swap(vector[i], vector[pivot_row]);
        }

        // Eliminate below
        for (int j = i + 1; j < n; j++) {
            double factor = matrix[j][i] / matrix[i][i];
            for (int k = i + 1; k < n; k++) {
                matrix[j][k] -= factor * matrix[i][k];
            }
            vector[j] -= factor * vector[i];
            matrix[j][i] = 0.0;
        }
    }

    // Back substitution
    for (int i = n - 1; i >= 0; i--) {
        if (std::abs(matrix[i][i]) < GAUSS_MATRIX_TOL) {
            return false;
        }
        double sum = vector[i];
        for (int j = i + 1; j < n; j++) {
            sum -= matrix[i][j] * vector[j];
        }
        vector[i] = sum / matrix[i][i];
    }

    return true;
}

// ============================================================================
// calcTransCoords — Apply linear transform to a single point
// ============================================================================
void TriangleMatcher::calcTransCoords(double x, double y, const GenericTrans& trans,
                                       double& newx, double& newy)
{
    newx = trans.x00 + trans.x10 * x + trans.x01 * y;
    newy = trans.y00 + trans.y10 * x + trans.y01 * y;
}

// ============================================================================
// Given 3 star indices and a distance matrix, fill in a triangle structure
// ============================================================================
void TriangleMatcher::setTriangle(MatchTriangle& tri, const std::vector<MatchStar>& stars,
                                    int s1, int s2, int s3,
                                    const std::vector<std::vector<double>>& distMatrix)
{
    double d12 = distMatrix[s1][s2];
    double d23 = distMatrix[s2][s3];
    double d13 = distMatrix[s1][s3];

    double a = 0.0, b = 0.0, c = 0.0;

    if ((d12 >= d23) && (d12 >= d13)) {
        // Longest side connects stars s1 and s2 → opposite vertex is s3
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
        // Longest side connects stars s2 and s3 → opposite vertex is s1
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
        // Longest side connects stars s1 and s3 → opposite vertex is s2
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
// Step 1: Compute distance matrix (calc_distances)
// Step 2: Fill triangle array (fill_triangle_array)
// Step 3: Sort by ba and prune high-ba triangles
// ============================================================================
std::vector<MatchTriangle> TriangleMatcher::generateTriangles(const std::vector<MatchStar>& stars, int nbright)
{
    int n = std::min((int)stars.size(), nbright);
    std::vector<MatchTriangle> triangles;
    if (n < 3) return triangles;

    // Step 1: Compute distance matrix (port of calc_distances)
    std::vector<std::vector<double>> distMatrix(n, std::vector<double>(n, 0.0));
    for (int i = 0; i < n - 1; i++) {
        for (int j = i + 1; j < n; j++) {
            double dx = stars[i].x - stars[j].x;
            double dy = stars[i].y - stars[j].y;
            double dist = std::sqrt(dx * dx + dy * dy);
            distMatrix[i][j] = dist;
            distMatrix[j][i] = dist;
        }
    }

    // Step 2: Fill triangle array (port of fill_triangle_array)
    int numTriangles = (n * (n - 1) * (n - 2)) / 6;
    triangles.reserve(numTriangles);

    int triId = 0;  // local counter, reset each call (not static)
    for (int i = 0; i < n - 2; i++) {
        for (int j = i + 1; j < n - 1; j++) {
            for (int k = j + 1; k < n; k++) {
                MatchTriangle t;
                t.id = triId++;
                setTriangle(t, stars, i, j, k, distMatrix);
                triangles.push_back(t);
            }
        }
    }

    // Step 3: Prune degenerate triangles (ba > AT_MATCH_RATIO)
    // Port of prune_triangle_array
    triangles.erase(
        std::remove_if(triangles.begin(), triangles.end(),
                        [](const MatchTriangle& t) { return t.ba > AT_MATCH_RATIO; }),
        triangles.end());

    // Step 4: Sort by ba for binary search (port of sort_triangle_array)
    std::sort(triangles.begin(), triangles.end(),
              [](const MatchTriangle& a, const MatchTriangle& b) { return a.ba < b.ba; });

    return triangles;
}

// ============================================================================
// computeVotes
// ============================================================================
std::vector<std::vector<int>> TriangleMatcher::computeVotes(
    const std::vector<MatchTriangle>& triA,
    const std::vector<MatchTriangle>& triB,
    int numStarsA, int numStarsB,
    double minScale, double maxScale)
{
    std::vector<std::vector<int>> votes(numStarsA, std::vector<int>(numStarsB, 0));

    double rad2 = m_triangleRadius * m_triangleRadius;
    bool useScaleFilter = (minScale > 0 && maxScale > 0);

    int totalB = (int)triB.size();

    // For each triangle in B, find matching triangles in A using binary search on ba
    for (int i = 0; i < totalB; i++) {
        if (!Threading::getThreadRun()) return votes;

        const auto& tb = triB[i];

        double ba_min = tb.ba - m_triangleRadius;
        double ba_max = tb.ba + m_triangleRadius;

        // Binary search for ba_min in sorted triA
        auto it_start = std::lower_bound(triA.begin(), triA.end(), ba_min,
            [](const MatchTriangle& t, double val) { return t.ba < val; });

        for (auto it = it_start; it != triA.end(); ++it) {
            if (it->ba > ba_max) break;

            // Shape check: distance in (ba, ca) space
            double d_ba = it->ba - tb.ba;
            double d_ca = it->ca - tb.ca;
            if (d_ba * d_ba + d_ca * d_ca >= rad2) continue;

            // Scale check: ratio = sideA / sideB
            if (useScaleFilter) {
                double ratio = it->a_length / tb.a_length;
                if (ratio < minScale || ratio > maxScale) continue;
            }

            // Vote for each vertex pair
            if (it->a_index < numStarsA && tb.a_index < numStarsB)
                votes[it->a_index][tb.a_index]++;
            if (it->b_index < numStarsA && tb.b_index < numStarsB)
                votes[it->b_index][tb.b_index]++;
            if (it->c_index < numStarsA && tb.c_index < numStarsB)
                votes[it->c_index][tb.c_index]++;
        }
    }

    return votes;
}

// ============================================================================
// calcTransLinear 
// Builds 3×3 system and solves via Gauss elimination
// x' = x00 + x10*x + x01*y  (similarly for y')
// ============================================================================
bool TriangleMatcher::calcTransLinear(int nbright,
                                       const std::vector<MatchStar>& starsA,
                                       const std::vector<MatchStar>& starsB,
                                       const std::vector<int>& idxA,
                                       const std::vector<int>& idxB,
                                       GenericTrans& trans)
{
    // Count valid pairs
    int n = 0;
    for (int i = 0; i < nbright; i++) {
        if (idxA[i] >= 0 && idxB[i] >= 0 &&
            idxA[i] < (int)starsA.size() && idxB[i] < (int)starsB.size())
            n++;
    }

    if (n < AT_MATCH_REQUIRE_LINEAR) return false;

    // Build the normal equations for least-squares:
    //   x' = c0 + c1*x + c2*y
    //   y' = d0 + d1*x + d2*y
    //
    // Normal equations matrix (3x3):
    // | Σ1    Σx    Σy  |   | c0 |   | Σx' |
    // | Σx    Σx²   Σxy | × | c1 | = | Σx'x |
    // | Σy    Σxy   Σy² |   | c2 |   | Σx'y |

    double sum1 = 0, sumx = 0, sumy = 0;
    double sumx2 = 0, sumy2 = 0, sumxy = 0;
    double sumxp = 0, sumxpx = 0, sumxpy = 0;
    double sumyp = 0, sumypx = 0, sumypy = 0;

    for (int i = 0; i < nbright; i++) {
        if (idxA[i] < 0 || idxB[i] < 0) continue;
        if (idxA[i] >= (int)starsA.size() || idxB[i] >= (int)starsB.size()) continue;

        double xa = starsA[idxA[i]].x;
        double ya = starsA[idxA[i]].y;
        double xb = starsB[idxB[i]].x;
        double yb = starsB[idxB[i]].y;

        sum1  += 1.0;
        sumx  += xa;
        sumy  += ya;
        sumx2 += xa * xa;
        sumy2 += ya * ya;
        sumxy += xa * ya;
        sumxp  += xb;
        sumxpx += xb * xa;
        sumxpy += xb * ya;
        sumyp  += yb;
        sumypx += yb * xa;
        sumypy += yb * ya;
    }

    // Solve for x' coefficients
    std::vector<std::vector<double>> matX = {
        {sum1, sumx, sumy},
        {sumx, sumx2, sumxy},
        {sumy, sumxy, sumy2}
    };
    std::vector<double> vecX = {sumxp, sumxpx, sumxpy};

    if (!gaussSolve(matX, 3, vecX)) return false;

    // Solve for y' coefficients
    std::vector<std::vector<double>> matY = {
        {sum1, sumx, sumy},
        {sumx, sumx2, sumxy},
        {sumy, sumxy, sumy2}
    };
    std::vector<double> vecY = {sumyp, sumypx, sumypy};

    if (!gaussSolve(matY, 3, vecY)) return false;

    trans.x00 = vecX[0];
    trans.x10 = vecX[1];
    trans.x01 = vecX[2];

    trans.y00 = vecY[0];
    trans.y10 = vecY[1];
    trans.y01 = vecY[2];

    trans.order = 1;
    trans.nr = n;

    return true;
}

// ============================================================================
// iterTrans 
// Iteratively fits a linear transform, removing outliers via sigma clipping
// ============================================================================
bool TriangleMatcher::iterTrans(int nbright,
                                 const std::vector<MatchStar>& starsA,
                                 const std::vector<MatchStar>& starsB,
                                 std::vector<int>& winnerIndexA,
                                 std::vector<int>& winnerIndexB,
                                 bool recalc,
                                 GenericTrans& trans)
{
    int required_pairs = AT_MATCH_REQUIRE_LINEAR;
    int start_pairs = AT_MATCH_STARTN_LINEAR;

    if (nbright < required_pairs) return false;

    // Validate that the index vectors are large enough to hold nbright entries.
    // This guards against any caller that passes mismatched sizes.
    if ((int)winnerIndexA.size() < nbright || (int)winnerIndexB.size() < nbright)
        return false;

    // On first call (RECALC_NO), use only start_pairs best stars
    // On RECALC_YES, use all pairs
    int use_pairs;
    if (recalc) {
        use_pairs = nbright;
    } else {
        use_pairs = std::min(nbright, start_pairs);
    }

    // Calculate initial TRANS
    if (!calcTransLinear(use_pairs, starsA, starsB, winnerIndexA, winnerIndexB, trans)) {
        return false;
    }

    // Now iterate: apply trans, compute residuals, sigma-clip, recalculate
    int nr = nbright; // start with all pairs for residual computation
    double max_dist2_absolute = AT_MATCH_MAXDIST * AT_MATCH_MAXDIST;

    int is_ok = 0;
    int iters_so_far = 0;

    while (iters_so_far < AT_MATCH_MAXITER) {
        int nb = 0;

        // Compute residuals for all nr pairs
        std::vector<double> dist2(nr);
        for (int i = 0; i < nr; i++) {
            int idxA = winnerIndexA[i];
            int idxB = winnerIndexB[i];
            // Skip pairs whose indices are out of bounds (defensive check)
            if (idxA < 0 || idxA >= (int)starsA.size() ||
                idxB < 0 || idxB >= (int)starsB.size()) {
                dist2[i] = max_dist2_absolute + 1.0; // will be removed in step 1
                continue;
            }
            double newx, newy;
            calcTransCoords(starsA[idxA].x, starsA[idxA].y, trans, newx, newy);
            double dx = newx - starsB[idxB].x;
            double dy = newy - starsB[idxB].y;
            dist2[i] = dx * dx + dy * dy;
        }

        // Step 1: Remove pairs with dist2 > AT_MATCH_MAXDIST^2
        for (int i = 0; i < nr; i++) {
            if (dist2[i] > max_dist2_absolute) {
                for (int j = i + 1; j < nr; j++) {
                    winnerIndexA[j-1] = winnerIndexA[j];
                    winnerIndexB[j-1] = winnerIndexB[j];
                    dist2[j-1] = dist2[j];
                }
                nr--;
                nb++;
                i--;
            }
        }

        // Step 2: Compute sigma at percentile (port of find_percentile)
        double sigma;
        if (nr < 2) {
            if (nr == 0) return false;
            sigma = 0.0;
        } else {
            std::vector<double> dist2_sorted(dist2.begin(), dist2.begin() + nr);
            std::sort(dist2_sorted.begin(), dist2_sorted.end());
            int percentile_idx = (int)std::floor(nr * AT_MATCH_PERCENTILE + 0.5); // round-to-nearest
            if (percentile_idx >= nr) percentile_idx = nr - 1;
            if (percentile_idx < 0) percentile_idx = 0;
            sigma = dist2_sorted[percentile_idx];
        }

        // Check halt condition
        if (sigma <= AT_MATCH_HALTSIGMA) {
            is_ok = 1;
        }

        // Step 3: Remove pairs with dist2 > AT_MATCH_NSIGMA * sigma
        double threshold = AT_MATCH_NSIGMA * sigma;
        for (int i = 0; i < nr; i++) {
            if (dist2[i] > threshold) {
                for (int j = i + 1; j < nr; j++) {
                    winnerIndexA[j-1] = winnerIndexA[j];
                    winnerIndexB[j-1] = winnerIndexB[j];
                    dist2[j-1] = dist2[j];
                }
                nr--;
                nb++;
                i--;
            }
        }

        // Check if no removals (all remaining are good)
        if (nb == 0) {
            is_ok = 1;
        }

        // Check minimum viable pairs
        if (nr < required_pairs) {
            return false;
        }

        // Recalculate TRANS with remaining pairs
        if (!calcTransLinear(nr, starsA, starsB, winnerIndexA, winnerIndexB, trans))
            return false;

        iters_so_far++;
        if (is_ok) break;
    }

    trans.nr = nr;
    return true;
}

// ============================================================================
// applyTransAndMatch 
// Applies transform to all A stars, finds nearest match in B within radius
// Uses 1-to-1 matching (each B star matched to at most one A star)
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
    int numA = (int)imgStars.size();
    int numB = (int)catStars.size();

    // Transform all image stars
    std::vector<MatchStar> transformedA(numA);
    for (int i = 0; i < numA; i++) {
        transformedA[i] = imgStars[i];
        calcTransCoords(imgStars[i].x, imgStars[i].y, trans,
                        transformedA[i].x, transformedA[i].y);
    }

    // Sort transformed A by x for potential optimization
    // For each A, find nearest B within radius (1-to-1 matching)
    // Track which B stars are already matched
    std::vector<int> bestMatchA(numB, -1);
    std::vector<double> bestMatchDist(numB, matchRadSq);

    for (int i = 0; i < numA; i++) {
        double best_dist2 = matchRadSq;
        int best_j = -1;

        for (int j = 0; j < numB; j++) {
            double dx = transformedA[i].x - catStars[j].x;
            double dy = transformedA[i].y - catStars[j].y;
            double d2 = dx * dx + dy * dy;
            if (d2 < best_dist2) {
                best_dist2 = d2;
                best_j = j;
            }
        }

        if (best_j >= 0) {
            if (bestMatchA[best_j] < 0 || best_dist2 < bestMatchDist[best_j]) {
                bestMatchA[best_j] = i;
                bestMatchDist[best_j] = best_dist2;
            }
        }
    }

    // Collect matched pairs using ORIGINAL (untransformed) A coordinates
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
// solve — Main entry point
//   1. Sort by mag, select brightest
//   2. Generate triangles for A and B
//   3. Compute vote matrix
//   4. Extract top vote getters
//   5. Disqualify pairs with < AT_MATCH_MINVOTES
//   6. iter_trans (RECALC_NO) on vote winners
//   7. atApplyTrans + atMatchLists using the TRANS
//   8. atRecalcTrans (iter_trans RECALC_YES) on matched pairs
//   9. Second round: re-apply improved TRANS, re-match, recalc
//  10. Fill outMatchedA / outMatchedB with final matched pairs (original coords)
//      These are used by the convergence loop in matchCatalog.
// ============================================================================
bool TriangleMatcher::solve(const std::vector<MatchStar>& imgStars,
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

    // === Step 1: Sort and select brightest ===
    // Both use m_maxStars (default 60). Using fewer image stars (20) degrades matching
    // when star detections are not PSF-quality — needs the full set for reliable votes.
    int nA = std::min((int)imgStars.size(), m_maxStars);
    int nB = std::min((int)catStars.size(), m_maxStars);

    if (nA < AT_MATCH_STARTN_LINEAR || nB < AT_MATCH_STARTN_LINEAR) {
        m_lastFailStage = 0;
        return false;
    }

    std::vector<MatchStar> sA = imgStars;
    std::vector<MatchStar> sB = catStars;
    std::sort(sA.begin(), sA.end(), compareStarsMag);
    std::sort(sB.begin(), sB.end(), compareStarsMag);
    sA.resize(nA);
    sB.resize(nB);

    // Set indices 
    for (int i = 0; i < nA; ++i) sA[i].index = i;
    for (int i = 0; i < nB; ++i) sB[i].index = i;

    // nbright = min(max(nA, nB), m_maxStars), clamped to actual sizes
    int nbright = std::min(std::max(nA, nB), m_maxStars);

    // === Step 2: Generate triangles ===
    auto triA = generateTriangles(sA, std::min(nA, nbright));
    auto triB = generateTriangles(sB, std::min(nB, nbright));

    if (triA.empty() || triB.empty()) return false;

    // === Step 3: Vote matrix ===
    auto votes = computeVotes(triA, triB, nA, nB, minScale, maxScale);

    // === Step 4: Top vote getters ===
    // Port of top_vote_getters: extract nbright best pairs from vote_matrix
    std::vector<int> winnerIndexA(nbright, -1);
    std::vector<int> winnerIndexB(nbright, -1);
    std::vector<int> winnerVotes(nbright, 0);

    for (int k = 0; k < nbright; k++) {
        int max_vote = 0;
        int max_i = -1;
        int max_j = -1;

        for (int i = 0; i < nA; i++) {
            for (int j = 0; j < nB; j++) {
                if (votes[i][j] > max_vote) {
                    max_vote = votes[i][j];
                    max_i = i;
                    max_j = j;
                }
            }
        }

        if (max_vote < 1) break;

        if (k == 0) m_lastMaxVote = max_vote; // record the single highest vote

        winnerVotes[k] = max_vote;
        winnerIndexA[k] = max_i;
        winnerIndexB[k] = max_j;

        // Zero out row and column (star already used)
        for (int j = 0; j < nB; j++) votes[max_i][j] = 0;
        for (int i = 0; i < nA; i++) votes[i][max_j] = 0;
    }

    // === Step 5: Disqualify pairs with < AT_MATCH_MINVOTES ===
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

    // === Step 6: iter_trans (RECALC_NO) on vote winners ===
    // TRANS now maps: image pixel coords  →  catalog arcsec (tangent plane)
    // This TRANS is valid for ALL image stars, not just the 60 used for triangles.
    if (!iterTrans(validNbright, sA, sB, winnerIndexA, winnerIndexB, false, resultTrans)) {
        m_lastFailStage = 3;
        return false;
    }

    // === Step 7: Apply TRANS to ALL imgStars, match against ALL catStars ===
    // AT_MATCH_MAXDIST=50 arcsec — lenient first pass since TRANS came from ~6 pairs.
    std::vector<MatchStar> matchedA, matchedB;
    int numMatches = applyTransAndMatch(imgStars, catStars, resultTrans, AT_MATCH_MAXDIST, matchedA, matchedB);
    resultTrans.nm = numMatches;
    m_lastNmatched = numMatches;

    if (numMatches < AT_MATCH_STARTN_LINEAR) {
        m_lastFailStage = 4;
        return false;
    }

    // === Step 8: atRecalcTrans (iter_trans RECALC_YES on matched pairs) ===
    std::vector<int> recalcIdxA(numMatches), recalcIdxB(numMatches);
    for (int i = 0; i < numMatches; i++) {
        recalcIdxA[i] = i;
        recalcIdxB[i] = i;
    }

    if (!iterTrans(numMatches, matchedA, matchedB, recalcIdxA, recalcIdxB, true, resultTrans)) {
        m_lastFailStage = 5;
        return false;
    }

    // === Step 9: Second round — apply improved TRANS to ALL imgStars, tight radius ===
    // TRANS is now well-calibrated, 5 arcsec is appropriate.
    matchedA.clear();
    matchedB.clear();
    numMatches = applyTransAndMatch(imgStars, catStars, resultTrans, AT_MATCH_RADIUS, matchedA, matchedB);
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

    if (!iterTrans(numMatches, matchedA, matchedB, recalcIdxA, recalcIdxB, true, resultTrans)) {
        m_lastFailStage = 7;
        return false;
    }

    // === Step 10: Output matched pairs for the convergence loop ===
    // matchedA contains original (untransformed) image star coords
    // matchedB contains catalog star projected coords
    
    // Cull the arrays to contain only the valid pairs selected by iterTrans
    std::vector<MatchStar> finalA(resultTrans.nr);
    std::vector<MatchStar> finalB(resultTrans.nr);
    for (int i = 0; i < resultTrans.nr; i++) {
        finalA[i] = matchedA[recalcIdxA[i]];
        finalB[i] = matchedB[recalcIdxB[i]];
    }
    
    outMatchedA = finalA;
    outMatchedB = finalB;

    m_lastFailStage  = 8; // success
    m_lastNmatched   = resultTrans.nr;
    return true;
}
