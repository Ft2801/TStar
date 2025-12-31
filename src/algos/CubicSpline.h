#ifndef CUBICSPLINE_H
#define CUBICSPLINE_H

#include <vector>
#include <cmath>
#include <algorithm>

struct SplinePoint {
    double x;
    double y;
    
    bool operator<(const SplinePoint& other) const {
        return x < other.x;
    }
};

struct SplineData {
    std::vector<double> x_values;
    std::vector<double> y_values;
    std::vector<double> b;
    std::vector<double> c;
    std::vector<double> d;
    int n = 0;
};

class CubicSpline {
public:
    static SplineData fit(const std::vector<SplinePoint>& points) {
        SplineData data;
        if (points.size() < 2) return data;

        data.n = static_cast<int>(points.size());
        int n = data.n;
        
        data.x_values.resize(n);
        data.y_values.resize(n);
        
        // Ensure sorted
        auto sortedPoints = points;
        std::sort(sortedPoints.begin(), sortedPoints.end());
        
        for (int i=0; i<n; ++i) {
            data.x_values[i] = sortedPoints[i].x;
            data.y_values[i] = sortedPoints[i].y;
        }

        std::vector<double> h(n - 1);
        std::vector<double> alpha(n - 1);
        std::vector<double> l(n);
        std::vector<double> mu(n);
        std::vector<double> z(n);
        
        data.b.resize(n);
        data.c.resize(n);
        data.d.resize(n);

        l[0] = 1.0;
        mu[0] = 0.0;
        z[0] = 0.0;

        for (int i = 0; i < n - 1; i++) {
            h[i] = data.x_values[i + 1] - data.x_values[i];
        }

        for (int i = 1; i < n - 1; i++) {
            alpha[i] = (3.0 / h[i]) * (data.y_values[i + 1] - data.y_values[i]) -
                       (3.0 / h[i - 1]) * (data.y_values[i] - data.y_values[i - 1]);
        }

        for (int i = 1; i < n - 1; i++) {
            l[i] = 2.0 * (data.x_values[i + 1] - data.x_values[i - 1]) - h[i - 1] * mu[i - 1];
            mu[i] = h[i] / l[i];
            z[i] = (alpha[i] - h[i - 1] * z[i - 1]) / l[i];
        }

        l[n - 1] = 1.0;
        z[n - 1] = 0.0;
        data.c[n - 1] = 0.0;

        double factor = 1.0 / 3.0;
        for (int j = n - 2; j >= 0; j--) {
            data.c[j] = z[j] - mu[j] * data.c[j + 1];
            data.b[j] = (data.y_values[j + 1] - data.y_values[j]) / h[j] -
                        h[j] * (data.c[j + 1] + 2.0 * data.c[j]) * factor;
            data.d[j] = (data.c[j + 1] - data.c[j]) / (3.0 * h[j]);
        }
        
        return data;
    }

    static double interpolate(double x, const SplineData& data) {
        if (data.n < 2) return x; 
        
        if (x >= data.x_values[data.n - 1])
            return data.y_values[data.n - 1];
        else if (x <= data.x_values[0])
            return data.y_values[0];

        x = std::max(0.0, std::min(1.0, x));
        
        // Binary search for interval
        auto it = std::upper_bound(data.x_values.begin(), data.x_values.end(), x);
        int i = static_cast<int>(std::distance(data.x_values.begin(), it)) - 1;
        
        if (i < 0) i = 0;
        if (i >= data.n - 1) i = data.n - 2;

        double diff = x - data.x_values[i];
        double diff_sq = diff * diff;
        double val = data.y_values[i] + data.b[i] * diff + data.c[i] * diff_sq + data.d[i] * diff * diff_sq;
        
        return std::max(0.0, std::min(1.0, val));
    }
    
    // Legacy Linear Interpolation (optional)
    static double interpolateLinear(double x, const std::vector<SplinePoint>& points) {
        if (points.empty()) return x;
        if (points.size() == 1) return points[0].y;
        
        auto p = points;
        // Assume sorted or sort
        // std::sort(p.begin(), p.end()); // Better to pass sorted
        
        if (x <= p[0].x) return p[0].y;
        if (x >= p.back().x) return p.back().y;
        
        for (size_t i = 0; i < p.size() - 1; ++i) {
            if (x >= p[i].x && x <= p[i+1].x) {
                double t = (x - p[i].x) / (p[i+1].x - p[i].x);
                return p[i].y + t * (p[i+1].y - p[i].y);
            }
        }
        return x;
    }
};

#endif // CUBICSPLINE_H
