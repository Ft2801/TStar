#include "AbeMath.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <random>

namespace AbeMath {

    // --- Linear Algebra Helper ---
    bool solveLinear(int N, std::vector<double>& A, std::vector<double>& x, const std::vector<double>& b) {
        // A is row-major N*N
        // Gaussian Elimination
        
        // Augment A with b
        std::vector<std::vector<double>> M(N, std::vector<double>(N + 1));
        for(int i=0; i<N; ++i) {
            for(int j=0; j<N; ++j) M[i][j] = A[i*N + j];
            M[i][N] = b[i];
        }

        for (int i = 0; i < N; ++i) {
            // Find pivot
            int pivot = i;
            for (int j = i + 1; j < N; ++j) {
                if (std::abs(M[j][i]) > std::abs(M[pivot][i])) pivot = j;
            }
            std::swap(M[i], M[pivot]);

            if (std::abs(M[i][i]) < 1e-9) return false; // Singular

            for (int j = i + 1; j < N; ++j) {
                double factor = M[j][i] / M[i][i];
                for (int k = i; k <= N; ++k) {
                    M[j][k] -= factor * M[i][k];
                }
            }
        }

        x.assign(N, 0.0);
        for (int i = N - 1; i >= 0; --i) {
            double sum = 0.0;
            for (int j = i + 1; j < N; ++j) {
                sum += M[i][j] * x[j];
            }
            x[i] = (M[i][N] - sum) / M[i][i];
        }
        return true;
    }

    // --- Polynomial ---
    int numPolyTerms(int degree) {
        return (degree + 1) * (degree + 2) / 2;
    }
    
    std::vector<float> getPolyTermValues(float x, float y, int degree) {
        std::vector<float> terms;
        terms.reserve(numPolyTerms(degree));
        for (int i = 0; i <= degree; ++i) {
            for (int j = 0; j <= degree - i; ++j) {
                terms.push_back(std::pow(x, i) * std::pow(y, j));
            }
        }
        return terms;
    }

    std::vector<float> fitPolynomial(const std::vector<Sample>& samples, int degree) {
        int terms = numPolyTerms(degree);
        int N = samples.size();
        if (N < terms) return {}; 

        // Normal Equations: (A^T A) x = A^T b
        // A is N x terms
        
        std::vector<double> ATA(terms * terms, 0.0);
        std::vector<double> ATb(terms, 0.0);
        
        for (const auto& s : samples) {
            auto vars = getPolyTermValues(s.x, s.y, degree);
            for (int i = 0; i < terms; ++i) {
                for (int j = 0; j < terms; ++j) {
                    ATA[i * terms + j] += (double)vars[i] * vars[j]; // Symmetric but full compute
                }
                ATb[i] += (double)vars[i] * s.z;
            }
        }
        
        std::vector<double> coeffsD;
        if (!solveLinear(terms, ATA, coeffsD, ATb)) return {};
        
        std::vector<float> coeffs(coeffsD.begin(), coeffsD.end());
        return coeffs;
    }

    float evalPolynomial(float x, float y, const std::vector<float>& coeffs, int degree) {
        if (coeffs.empty()) return 0.0f;
        auto vars = getPolyTermValues(x, y, degree);
        float sum = 0.0f;
        for (size_t i = 0; i < vars.size() && i < coeffs.size(); ++i) {
            sum += vars[i] * coeffs[i];
        }
        return sum;
    }

    // --- RBF ---
    float distSq(float x1, float y1, float x2, float y2) {
        return (x1-x2)*(x1-x2) + (y1-y2)*(y1-y2);
    }

    float rbfKernel(float r2, float smooth) {
        // Multiquadric kernel: sqrt(r^2 + 1) corresponds to epsilon=1.0
        return std::sqrt(r2 + 1.0f);
    }

    RbfModel fitRbf(const std::vector<Sample>& samples, float smooth) {
        int N = samples.size();
        std::vector<double> A(N * N);
        std::vector<double> b(N);
        std::vector<double> w;
        
        for (int i = 0; i < N; ++i) {
            b[i] = samples[i].z;
            for (int j = 0; j < N; ++j) {
                float d2 = distSq(samples[i].x, samples[i].y, samples[j].x, samples[j].y);
                double val = rbfKernel(d2, 0.0f); // Epsilon=1 impl
                if (i == j) val += smooth; // Regularization
                A[i*N + j] = val;
            }
        }
        
        if (!solveLinear(N, A, w, b)) return {};
        
        RbfModel m;
        m.centers = samples;
        for(double v : w) m.weights.push_back((float)v);
        m.smooth = smooth;
        return m;
    }

    float evalRbf(float x, float y, const RbfModel& model) {
        float sum = 0.0f;
        for (size_t i = 0; i < model.centers.size(); ++i) {
            float d2 = distSq(x, y, model.centers[i].x, model.centers[i].y);
            sum += model.weights[i] * rbfKernel(d2, 0.0f);
        }
        return sum;
    }
    
    // --- Sampling ---
    float getMedianBox(const std::vector<float>& data, int w, int h, int cx, int cy, int size) {
        int half = size / 2;
        std::vector<float> vals;
        vals.reserve(size*size);
        for(int y=cy-half; y<=cy+half; ++y) {
            for(int x=cx-half; x<=cx+half; ++x) {
                if(x>=0 && x<w && y>=0 && y<h) {
                    vals.push_back(data[y*w + x]);
                }
            }
        }
        if (vals.empty()) return 0.0f;
        std::sort(vals.begin(), vals.end());
        return vals[vals.size()/2];
    }
    
    Point findDimmest(const std::vector<float>& data, int w, int h, int cx, int cy, int patchSize) {
        // Gradient Descent to find min median
        int curX = cx;
        int curY = cy;
        float curVal = getMedianBox(data, w, h, curX, curY, patchSize);
        
        for(int iter=0; iter<20; ++iter) { // Limit iter
            int bestX = curX;
            int bestY = curY;
            float bestVal = curVal;
            
            // Checks neighbors (step 5 pixels?)
            int step = 2; 
            int dx[] = {-step, 0, step, -step, step, -step, 0, step};
            int dy[] = {-step, -step, -step, 0, 0, step, step, step};
            
            bool found = false;
            for(int i=0; i<8; ++i) {
                int nx = curX + dx[i];
                int ny = curY + dy[i];
                if(nx >= 0 && nx < w && ny >= 0 && ny < h) {
                    float v = getMedianBox(data, w, h, nx, ny, patchSize);
                    if (v < bestVal) {
                        bestVal = v;
                        bestX = nx;
                        bestY = ny;
                        found = true;
                    }
                }
            }
            if(!found) break;
            curX = bestX;
            curY = bestY;
            curVal = bestVal;
        }
        return { (float)curX, (float)curY };
    }

    std::vector<Point> generateSamples(const std::vector<float>& data, int w, int h, int numSamples, int patchSize, const std::vector<bool>& exclusionMask) {
        std::vector<Point> points;
        
        // Grid approach
        int gridM = (int)std::sqrt(numSamples);
        float stepX = (float)w / gridM;
        float stepY = (float)h / gridM;
        
        for(int gy=0; gy<gridM; ++gy) {
            for(int gx=0; gx<gridM; ++gx) {
                int cx = (int)(stepX * (gx + 0.5f));
                int cy = (int)(stepY * (gy + 0.5f));
                
                // Dimmest
                Point p = findDimmest(data, w, h, cx, cy, patchSize);
                
                // Check Exclusion
                if (!exclusionMask.empty()) {
                    int px = (int)p.x;
                    int py = (int)p.y;
                    if (px >= 0 && px < w && py >= 0 && py < h) {
                        if (!exclusionMask[py*w + px]) { // 0 = excluded
                             continue;
                        }
                    }
                }
                
                points.push_back(p);
            }
        }
        return points;
    }
}
