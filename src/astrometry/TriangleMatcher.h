#ifndef TRIANGLEMATCHER_H
#define TRIANGLEMATCHER_H

#include <vector>
#include <cmath>
#include <algorithm>

// Standard Constants
#define AT_MATCH_NB_BRIGHTEST 20 
#define AT_MATCH_TOLERANCE 0.002 
#define AT_MATCH_MAX_RADIUS 0.002 
#define AT_MATCH_MIN_SCALE -1.0
#define AT_MATCH_MAX_SCALE -1.0
#define AT_MATCH_NOANGLE -999.0

struct MatchStar {
    int id;
    int index;
    double x;
    double y;
    double mag;
    // double BV; // Not used for solving usually
    int match_id; // For connecting listA to listB
};

struct MatchTriangle {
    int id;
    int a_index;
    int b_index;
    int c_index;
    double a_length; // Longest side
    double ba; // Ratio b/a
    double ca; // Ratio c/a
    double side_a_angle; 
};

// Trans structure to match reference TRANS
struct GenericTrans {
    double x00, x10, x01;
    double y00, y10, y01;
    // Add quadratic/cubic support if needed, but we start with Linear (Order 1)
    int order; 
    int nr; // Number of pairs used
    double sig; // Sigma of residuals
};

class TriangleMatcher {
public:
    TriangleMatcher();

    void setMaxStars(int n) { m_maxStars = n; }
    
    // Strict solve method matching reference approach
    // scaleMin/max: filter false matches by size
    // centerX/Y, posTolerance: filter matches that imply a translation far from image center (hint)
    // pass posTolerance < 0 to disable position check
    bool solve(const std::vector<MatchStar>& imgStars,
               const std::vector<MatchStar>& catStars,
               GenericTrans& resultTrans,
               double minScale = 0.9, double maxScale = 1.1,
               double centerX = 0, double centerY = 0, double posTolerance = -1);

private:
    int m_maxStars = 20;

    std::vector<MatchTriangle> generateTriangles(const std::vector<MatchStar>& stars, int limit);
    
    // make_vote_matrix
    // Requires star lists to compute centroids for position check
    std::vector<std::vector<int>> computeVotes(const std::vector<MatchTriangle>& triA,
                                               const std::vector<MatchTriangle>& triB,
                                               const std::vector<MatchStar>& starsA,
                                               const std::vector<MatchStar>& starsB,
                                               int numStarsA, int numStarsB,
                                               double minScale, double maxScale,
                                               double centerX, double centerY, double posTolerance);
                                               
    // iter_trans logic
    bool iterativeFit(const std::vector<MatchStar>& listA,
                      const std::vector<MatchStar>& listB,
                      const std::vector<int>& votesA,
                      const std::vector<int>& votesB,
                      GenericTrans& trans);
                      
    int verifyTransform(const std::vector<MatchStar>& imgStars,
                        const std::vector<MatchStar>& catStars,
                        const GenericTrans& trans,
                        double tol,
                        std::vector<int>& inlierA,
                        std::vector<int>& inlierB);
                      
    // calc_trans_linear
    bool calcTransLinear(int n, 
                         const std::vector<MatchStar>& listA,
                         const std::vector<MatchStar>& listB,
                         const std::vector<int>& idxA,
                         const std::vector<int>& idxB,
                         GenericTrans& trans);
                         
    // Helper for linear equation solving (Gauss-Jordan or simple Cramer for 3x3)
    bool solveLinearSystem3x3(double M[3][3], double V[3], double res[3]);
};

#endif // TRIANGLEMATCHER_H
