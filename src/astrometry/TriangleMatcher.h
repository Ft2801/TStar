#ifndef TRIANGLEMATCHER_H
#define TRIANGLEMATCHER_H

#include <vector>
#include <cmath>
#include <algorithm>

// ============================================================================
// Constants 
// ============================================================================

// Max radius in triangle-space (ba,ca) for matching
#define AT_TRIANGLE_RADIUS    0.002

// Matching radius after applying TRANS, in units of list B (arcsec)
#define AT_MATCH_RADIUS       5.0

// Max distance (arcsec) in iter_trans — hard upper limit
#define AT_MATCH_MAXDIST     50.0

// Sigma clipping multiplier in iter_trans 
#define AT_MATCH_NSIGMA       10.0

// Percentile for sigma estimation used in iter_trans clipping 
#define AT_MATCH_PERCENTILE   0.35

// Percentile used only for the sig diagnostic field (not for clipping)
#define ONE_STDEV_PERCENTILE  0.683

// Halt sigma threshold (if dist² at percentile is below this, stop iterating)
#define AT_MATCH_HALTSIGMA    0.1

// Max image stars for triangle generation 
// Fewer image-star triangles → much cleaner vote matrix signal-to-noise
#define AT_MATCH_NBRIGHT          20
// Max catalog stars for triangle generation 
#define AT_MATCH_CATALOG_NBRIGHT  60

// Min votes in vote matrix for a pair to be valid
#define AT_MATCH_MINVOTES     2

// Max iterations in iter_trans
#define AT_MATCH_MAXITER      5

// Prune triangles with ba > this ratio
#define AT_MATCH_RATIO        0.9

// Minimum and initial pairs for linear fit
#define AT_MATCH_STARTN_LINEAR  6
#define AT_MATCH_REQUIRE_LINEAR 3

// Tolerance for Gauss elimination pivot
#define GAUSS_MATRIX_TOL      1.0e-4

// Marker for "no angle constraint"
#define AT_MATCH_NOANGLE     -999.0

// ============================================================================
// Data structures 
// ============================================================================

struct MatchStar {
    int id = 0;
    int index = 0;      // position in the sorted array
    double x = 0;
    double y = 0;
    double mag = 0;
    int match_id = -1;  // ID of star in other list which matches
};

struct MatchTriangle {
    int id = 0;
    int a_index = 0;    // index of vertex OPPOSITE the longest side
    int b_index = 0;    // index of vertex OPPOSITE the intermediate side
    int c_index = 0;    // index of vertex OPPOSITE the shortest side
    double a_length = 0; // Length of longest side (not normalized)
    double ba = 0;      // Ratio b/a (0..1)
    double ca = 0;      // Ratio c/a (0..1)
};

// Linear transformation: x' = x00 + x10*x + x01*y, y' = y00 + y10*x + y01*y
struct GenericTrans {
    double x00 = 0, x10 = 0, x01 = 0;
    double y00 = 0, y10 = 0, y01 = 0;
    int order = 1;
    int nr = 0;     // Number of matched pairs remaining after sigma-clip
    int nm = 0;     // Number of matched pairs found by atMatchLists
    double sx = 0;  // RMS residual in x
    double sy = 0;  // RMS residual in y
};

// ============================================================================
// TriangleMatcher 
// ============================================================================

class TriangleMatcher {
public:
    TriangleMatcher();

    // Max catalog/image stars for triangle generation (AT_MATCH_CATALOG_NBRIGHT = 60)
    void setMaxStars(int n) { m_maxStars = n; }
    // Max image stars for triangle generation (AT_MATCH_NBRIGHT = 20).
    void setMaxImgStars(int n) { m_maxImgStars = n; }
    // Triangle radius in (ba,ca) space — default AT_TRIANGLE_RADIUS (0.002).
    // Increase (e.g. 0.003) for images with significant lens distortion or wider fallback.
    void setTriangleRadius(double r) { m_triangleRadius = r; }
    double triangleRadius() const { return m_triangleRadius; }

    // Diagnostics from the last solve() call (accessible after failure for logging)
    int  lastMaxVote()    const { return m_lastMaxVote; }
    int  lastValidPairs() const { return m_lastValidPairs; }
    int  lastNmatched()   const { return m_lastNmatched; }
    // Stage at which the last solve() failed:
    //  0 = not enough stars, 1 = no triangle votes, 2 = too few vote-winners,
    //  3 = iterTrans failed, 4 = first match too few, 5 = second iterTrans,
    //  6 = second match too few, 7 = third iterTrans, 8 = succeeded
    int  lastFailStage()  const { return m_lastFailStage; }

    // Main solve method 
    //   1. Triangle voting (atFindTrans)
    //   2. iter_trans (RECALC_NO)
    //   3. atApplyTrans + atMatchLists
    //   4. atRecalcTrans (iter_trans RECALC_YES on matched pairs)
    //   5. Repeat 3-4 for second refinement
    //
    // outMatchedA: matched image stars (original coords, used in convergence loop)
    // outMatchedB: matched catalog stars (current projected coords, updated each iteration)
    bool solve(const std::vector<MatchStar>& imgStars,
               const std::vector<MatchStar>& catStars,
               GenericTrans& resultTrans,
               std::vector<MatchStar>& outMatchedA,
               std::vector<MatchStar>& outMatchedB,
               double minScale = -1, double maxScale = -1);

    // === iter_trans — Iterative transformation fitting ===
    // recalc: if true (RECALC_YES), use all pairs from the start
    //         if false (RECALC_NO), start with AT_MATCH_STARTN_LINEAR top pairs
    bool iterTrans(int nbright,
                   const std::vector<MatchStar>& starsA,
                   const std::vector<MatchStar>& starsB,
                   std::vector<int>& winnerIndexA,
                   std::vector<int>& winnerIndexB,
                   bool recalc,
                   GenericTrans& trans);

    // === atApplyTrans + atMatchLists ===
    // Applies trans to ALL imgStars, finds nearest catStar within radius
    // Returns number of matched pairs; fills matchedA and matchedB
    int applyTransAndMatch(const std::vector<MatchStar>& imgStars,
                           const std::vector<MatchStar>& catStars,
                           const GenericTrans& trans,
                           double matchRadius,
                           std::vector<MatchStar>& matchedListA,
                           std::vector<MatchStar>& matchedListB);

private:
    int    m_maxStars      = 60;    // AT_MATCH_CATALOG_NBRIGHT
    int    m_maxImgStars   = 20;    // AT_MATCH_NBRIGHT (unused in solve, kept for API)
    double m_triangleRadius = AT_TRIANGLE_RADIUS; // configurable, default 0.002

    // Last-solve diagnostics (updated even on failure)
    int m_lastMaxVote    = 0;
    int m_lastValidPairs = 0;
    int m_lastNmatched   = 0;
    int m_lastFailStage  = -1;

    // === Triangle Generation (stars_to_triangles) ===
    // Pre-computes distance matrix, then fills triangle array
    std::vector<MatchTriangle> generateTriangles(const std::vector<MatchStar>& stars, int nbright);

    // Helper: fill a single triangle from distance matrix (port of set_triangle)
    void setTriangle(MatchTriangle& tri, const std::vector<MatchStar>& stars,
                     int s1, int s2, int s3,
                     const double* distMatrix, int numStars);

    // === Vote Matrix (make_vote_matrix) ===
    std::vector<int> computeVotes(const std::vector<MatchTriangle>& triA,
                                  const std::vector<MatchTriangle>& triB,
                                  int numStarsA, int numStarsB,
                                  double minScale, double maxScale);

    // === calc_trans_linear — Gauss elimination ===
    bool calcTransLinear(int nbright,
                         const std::vector<MatchStar>& starsA,
                         const std::vector<MatchStar>& starsB,
                         const std::vector<int>& idxA,
                         const std::vector<int>& idxB,
                         GenericTrans& trans);

    // === Apply transform to a single point ===
    static void calcTransCoords(double x, double y, const GenericTrans& trans,
                                double& newx, double& newy);

    // === Gauss elimination solver ===
    // Solves in-place: matrix * x = vector, result stored in vector
    // matrix must be n*n flat array
    static bool gaussSolve(double* matrix, int n, double* vector);
};

#endif // TRIANGLEMATCHER_H
