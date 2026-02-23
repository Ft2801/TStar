#include "TriangleMatcher.h"
#include "../core/ThreadState.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <map>
#include <iostream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Replicate Reference logic for vote counting and iterative fitting.

TriangleMatcher::TriangleMatcher() {
    m_maxStars = 75; // Increased for better robustness (from 40)
}

// Helper to sort stars by mag
bool compareStarsMag(const MatchStar& a, const MatchStar& b) {
    return a.mag < b.mag;
}

std::vector<MatchTriangle> TriangleMatcher::generateTriangles(const std::vector<MatchStar>& stars, int limit) {
    std::vector<MatchTriangle> triangles;
    int n = std::min((int)stars.size(), limit);
    if (n < 3) return triangles;

    // Generate all combinations
    for (int i = 0; i < n - 2; i++) {
        for (int j = i + 1; j < n - 1; j++) {
            for (int k = j + 1; k < n; k++) {
                double x1 = stars[i].x, y1 = stars[i].y;
                double x2 = stars[j].x, y2 = stars[j].y;
                double x3 = stars[k].x, y3 = stars[k].y;

                // Side lengths squared
                double d12 = (x1 - x2)*(x1 - x2) + (y1 - y2)*(y1 - y2); // c
                double d23 = (x2 - x3)*(x2 - x3) + (y2 - y3)*(y2 - y3); // a
                double d31 = (x3 - x1)*(x3 - x1) + (y3 - y1)*(y3 - y1); // b

// Distance squares - variables removed

                // Sort sides so a is largest
                // Start with assignments matching standard convention if possible
                // Reference logic checks sides and reorders indices so a is longest.
                
                // Bubble sort 3 items logic to find longest side 'a'
                // Find longest side 'a'
                // a = BC, b = AC, c = AB.
                
                // Identify vertices to permute to canonical form
                // Canonical: Side 'a' is longest.
                // We need to map (i,j,k) to (A,B,C) such that dist(B,C) is max.
                
                // distances: d_ij, d_jk, d_ki
                double d_ij = std::sqrt(d12);
                double d_jk = std::sqrt(d23);
                double d_ki = std::sqrt(d31);
                
                double max_d = d_ij;
                if (d_jk > max_d) max_d = d_jk;
                if (d_ki > max_d) max_d = d_ki;
                
                int A_idx, B_idx, C_idx; // Indices in the stars array
                double a_len, b_len, c_len;
                
                if (max_d == d_jk) {
                    // Side opposite i is longest. i is A. a = jk.
                    A_idx = i; B_idx = j; C_idx = k;
                    
                    // Let's check neighbors.
                    // Side b = AC = d_ki. Side c = AB = d_ij.
                    b_len = d_ki;
                    c_len = d_ij;
                    
                } else if (max_d == d_ki) {
                    // Side opposite j is longest. j is A.
                    // a = ki.
                    A_idx = j; B_idx = k; C_idx = i;
                    b_len = d_ij; // AC => j-i? No, A=j, C=i. dist(j,i)=d_ij. Correct.
                    c_len = d_jk; // AB => j-k? dist(j,k)=d_jk. Correct.
                } else {
                    // d_ij is max. Side opposite k. k is A.
                    A_idx = k; B_idx = i; C_idx = j;
                    b_len = d_jk; // AC => k-j? dist(k,j)=d_jk.
                    c_len = d_ki; // AB => k-i? dist(k,i)=d_ki.
                }
                
                // Ensure b >= c to reduce search space (invariant)
                if (b_len < c_len) {
                    std::swap(B_idx, C_idx);
                    std::swap(b_len, c_len);
                }
                
                a_len = max_d;
                
                MatchTriangle t;
                t.a_index = A_idx;
                t.b_index = B_idx;
                t.c_index = C_idx;
                t.a_length = a_len;
                t.ba = b_len / a_len;
                t.ca = c_len / a_len;
                
                
                triangles.push_back(t);
            }
        }
    }
    
    // Sort by ba for binary search in voting
    std::sort(triangles.begin(), triangles.end(), [](const MatchTriangle& a, const MatchTriangle& b) {
        return a.ba < b.ba;
    });
    
    return triangles;
}

bool TriangleMatcher::solve(const std::vector<MatchStar>& imgStars,
                            const std::vector<MatchStar>& catStars,
                            GenericTrans& resultTrans,
                            double minScale, double maxScale,
                            double centerX, double centerY, double posTolerance) 
{
    // 1. Sort and Filter Brightest
    int nA = std::min((int)imgStars.size(), m_maxStars);
    int nB = std::min((int)catStars.size(), m_maxStars);
    
    if (nA < 5 || nB < 5) return false;
    
    std::vector<MatchStar> sA = imgStars;
    std::vector<MatchStar> sB = catStars;
    std::sort(sA.begin(), sA.end(), compareStarsMag);
    std::sort(sB.begin(), sB.end(), compareStarsMag);
    sA.resize(nA);
    sB.resize(nB);
    
    // Update indices
    for(int i=0; i<nA; ++i) sA[i].index = i;
    for(int i=0; i<nB; ++i) sB[i].index = i;
    
    // 2. Generate Triangles
    auto triA = generateTriangles(sA, nA);
    auto triB = generateTriangles(sB, nB);
    
    // 3. Vote
    auto votes = computeVotes(triA, triB, sA, sB, nA, nB, minScale, maxScale, centerX, centerY, posTolerance);
    
    // 4. Find Winners (Top vote getters)
    // We need pairs (i, j) with high votes.
    // Reference uses a loop to pick max vote in matrix, zero out row/col, repeat.
    
    std::vector<int> winnerA;
    std::vector<int> winnerB;
    
    // Copy votes to temp to destructively find max
    auto tempVotes = votes;
    
    // Minimum 6 pairs for a valid linear transform.
    int required_pairs = 6;
    for(int k=0; k<std::min(nA, nB); ++k) {
        int maxv = 0;
        int max_i = -1, max_j = -1;
        
        for(int i=0; i<nA; ++i) {
            for(int j=0; j<nB; ++j) {
                if(tempVotes[i][j] > maxv) {
                    maxv = tempVotes[i][j];
                    max_i = i; 
                    max_j = j;
                }
            }
        }
        
        if (maxv < 1) break; // No more votes
        
        winnerA.push_back(max_i);
        winnerB.push_back(max_j);
        
        // Zero out row/col to enforce 1-to-1 in this greedy step
        for(int j=0; j<nB; ++j) tempVotes[max_i][j] = -1;
        for(int i=0; i<nA; ++i) tempVotes[i][max_j] = -1;
    }
    
    if ((int)winnerA.size() < required_pairs) return false;
    
    // 5. Iterative Fit
    return iterativeFit(sA, sB, winnerA, winnerB, resultTrans);
}

// ... (skipping generateTriangles, it is unchanged)

std::vector<std::vector<int>> TriangleMatcher::computeVotes(const std::vector<MatchTriangle>& triA,
                                           const std::vector<MatchTriangle>& triB,
                                           const std::vector<MatchStar>& starsA,
                                           const std::vector<MatchStar>& starsB,
                                           int numStarsA, int numStarsB,
                                           double minScale, double maxScale,
                                           double centerX, double centerY, double posTolerance) 
{
    // Allocate vote matrix
    std::vector<std::vector<int>> votes(numStarsA, std::vector<int>(numStarsB, 0));
    
    double max_r = AT_MATCH_MAX_RADIUS;
    double max_r2 = max_r * max_r;
    double posTol2 = posTolerance * posTolerance;

    // Walk through B
    for (const auto& tb : triB) {
        if (!Threading::getThreadRun()) return votes; // Cancellation check
        double ba_min = tb.ba - max_r;
        double ba_max = tb.ba + max_r;
        
        // Find range in A
        auto it_start = std::lower_bound(triA.begin(), triA.end(), ba_min, [](const MatchTriangle& t, double val){
            return t.ba < val;
        });
        
        for (auto it = it_start; it != triA.end(); ++it) {
            if (it->ba > ba_max) break;
            
            // SCALE CHECK
            double ratio = it->a_length / tb.a_length;
            if (ratio < minScale || ratio > maxScale) {
                continue; 
            }
            
            // POSITION CHECK (Centroid)
            if (posTolerance > 0) {
                // Calculate geometric centers (approximate)
                // Use star coordinates from starsA/starsB using indices in triangle
                // Tri contains indices into the SORTED subset sA/sB passed to this function
                
                const auto& sA1 = starsA[it->a_index];
                const auto& sA2 = starsA[it->b_index];
                const auto& sA3 = starsA[it->c_index];
                
                const auto& sB1 = starsB[tb.a_index];
                const auto& sB2 = starsB[tb.b_index];
                const auto& sB3 = starsB[tb.c_index];
                
                double cxA = (sA1.x + sA2.x + sA3.x) / 3.0;
                double cyA = (sA1.y + sA2.y + sA3.y) / 3.0;
                
                double cxB = (sB1.x + sB2.x + sB3.x) / 3.0;
                double cyB = (sB1.y + sB2.y + sB3.y) / 3.0;
                
                // Estimated shift = Img - Cat (since Cat is near 0)
                // Shift should be near centerX, centerY
                double dx = cxA - cxB; 
                double dy = cyA - cyB;
                
                double distSq = (dx - centerX)*(dx - centerX) + (dy - centerY)*(dy - centerY);
                if (distSq > posTol2) {
                    continue; // Reject match too far from expected position
                }
            }

            // Check distance in triangle space (ba, ca)
            double d_ba = it->ba - tb.ba;
            double d_ca = it->ca - tb.ca;
            if (d_ba*d_ba + d_ca*d_ca < max_r2) {
                // Match! Vote for vertices.
                if (it->a_index < numStarsA && tb.a_index < numStarsB) votes[it->a_index][tb.a_index]++;
                if (it->b_index < numStarsA && tb.b_index < numStarsB) votes[it->b_index][tb.b_index]++;
                if (it->c_index < numStarsA && tb.c_index < numStarsB) votes[it->c_index][tb.c_index]++;
            }
        }
    }
    return votes;
}

bool TriangleMatcher::solveLinearSystem3x3(double M[3][3], double V[3], double res[3]) {
    // Determinant
    double det = M[0][0] * (M[1][1] * M[2][2] - M[2][1] * M[1][2]) -
                 M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0]) +
                 M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0]);

    if (std::abs(det) < 1e-9) return false;

    double invDet = 1.0 / det;

    res[0] = (V[0] * (M[1][1] * M[2][2] - M[2][1] * M[1][2]) -
              V[1] * (M[0][1] * M[2][2] - M[0][2] * M[2][1]) +
              V[2] * (M[0][1] * M[1][2] - M[0][2] * M[1][1])) * invDet;

    res[1] = (V[1] * (M[0][0] * M[2][2] - M[0][2] * M[2][0]) -
              V[0] * (M[1][0] * M[2][2] - M[1][2] * M[2][0]) -
              V[2] * (M[0][0] * M[1][2] - M[0][2] * M[1][0])) * invDet;

    res[2] = (V[2] * (M[0][0] * M[1][1] - M[0][1] * M[1][0]) -
              V[1] * (M[0][0] * M[2][1] - M[2][0] * M[0][1]) +
              V[0] * (M[1][0] * M[2][1] - M[2][0] * M[1][1])) * invDet;
              
    return true;
}

bool TriangleMatcher::calcTransLinear(int n, 
                                      const std::vector<MatchStar>& listA,
                                      const std::vector<MatchStar>& listB,
                                      const std::vector<int>& idxA,
                                      const std::vector<int>& idxB,
                                      GenericTrans& trans) 
{
    // Solve x' = A + Bx + Cy
    // Solve y' = D + Ex + Fy
    // Using Least Squares
    
    double sumx = 0, sumy = 0, sumx2 = 0, sumy2 = 0, sumxy = 0;
    double sumxp = 0, sumxpx = 0, sumxpy = 0;
    double sumyp = 0, sumypx = 0, sumypy = 0;
    
    int used = 0;
    for(int i=0; i<n; ++i) {
        if(idxA[i] < 0 || idxB[i] < 0) continue;
        
        const auto& s1 = listA[idxA[i]]; // Source (x,y)
        const auto& s2 = listB[idxB[i]]; // Target (x',y')
        
        sumx += s1.x;
        sumy += s1.y;
        sumx2 += s1.x * s1.x;
        sumy2 += s1.y * s1.y;
        sumxy += s1.x * s1.y;
        
        sumxp += s2.x;
        sumxpx += s2.x * s1.x;
        sumxpy += s2.x * s1.y;
        
        sumyp += s2.y;
        sumypx += s2.y * s1.x;
        sumypy += s2.y * s1.y;
        
        used++;
    }
    
    if (used < 3) return false;
    
    // Matrix M is same for both systems
    // | N    sumx  sumy |
    // | sumx sumx2 sumxy|
    // | sumy sumxy sumy2|
    
    double M[3][3] = {
        {(double)used, sumx, sumy},
        {sumx, sumx2, sumxy},
        {sumy, sumxy, sumy2}
    };
    
    // Solve for (A,B,C) -> (x00, x10, x01)
    double V1[3] = {sumxp, sumxpx, sumxpy};
    double sol1[3];
    if (!solveLinearSystem3x3(M, V1, sol1)) return false;
    
    // Solve for (D,E,F) -> (y00, y10, y01)
    double V2[3] = {sumyp, sumypx, sumypy};
    double sol2[3];
    if (!solveLinearSystem3x3(M, V2, sol2)) return false;
    
    trans.x00 = sol1[0]; trans.x10 = sol1[1]; trans.x01 = sol1[2];
    trans.y00 = sol2[0]; trans.y10 = sol2[1]; trans.y01 = sol2[2];
    trans.order = 1;
    trans.nr = used;
    
    return true;
}

bool TriangleMatcher::iterativeFit(const std::vector<MatchStar>& listA,
                                   const std::vector<MatchStar>& listB,
                                   const std::vector<int>& votesA,
                                   const std::vector<int>& votesB,
                                   GenericTrans& trans)
{
    // Replicate iter_trans
    std::vector<int> idxA = votesA;
    std::vector<int> idxB = votesB;
    int nr = idxA.size();
    
    int max_iter = 10;
    double halt_sigma = 0.5; // Pixel tolerance
    
    for(int iter=0; iter<max_iter; ++iter) {
        if (nr < 6) return false; // Need min points
        
        // Calculate current transformation
        if (!calcTransLinear(nr, listA, listB, idxA, idxB, trans)) return false;
        
        // Calculate residuals
        std::vector<double> dist2(nr);
        std::vector<double> sorted_dist2(nr);
        
        for(int i=0; i<nr; ++i) {
            const auto& s1 = listA[idxA[i]];
            const auto& s2 = listB[idxB[i]];
            
            // Transform s1
            double tx = trans.x00 + trans.x10 * s1.x + trans.x01 * s1.y;
            double ty = trans.y00 + trans.y10 * s1.x + trans.y01 * s1.y;
            
            double dx = tx - s2.x;
            double dy = ty - s2.y;
            dist2[i] = dx*dx + dy*dy;
            sorted_dist2[i] = dist2[i];
        }
        
        std::sort(sorted_dist2.begin(), sorted_dist2.end());
        
        // Find sigma (approx percentile)
        // Standard uses AT_MATCH_PERCENTILE (usually 0.5 or 0.68)
        double sigma2 = sorted_dist2[(int)(nr * 0.68)]; 
        
        if (sigma2 <= halt_sigma*halt_sigma) break; // Good enough
        
        // Filter outliers
        int new_nr = 0;
        int nb = 0;
        double threshold = 9.0 * sigma2; // 3 sigma squared
        
        for(int i=0; i<nr; ++i) {
            if (dist2[i] <= threshold) {
                idxA[new_nr] = idxA[i];
                idxB[new_nr] = idxB[i];
                new_nr++;
            } else {
                nb++;
            }
        }
        
        nr = new_nr;
        if (nb == 0) break; // No more outliers removed
    }
    
    trans.nr = nr;
    return (nr >= 6); 
}


