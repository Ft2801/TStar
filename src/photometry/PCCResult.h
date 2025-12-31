#ifndef PCCRESULT_H
#define PCCRESULT_H

#include <vector>

struct PCCResult {
    bool valid = false;
    double R_factor = 1.0;
    double G_factor = 1.0;
    double B_factor = 1.0;
    
    // Background Neutralization Offsets
    float bg_r = 0.0f;
    float bg_g = 0.0f;
    float bg_b = 0.0f;

    // Scatter Data for Distribution Plots
    std::vector<double> CatRG, ImgRG;
    std::vector<double> CatBG, ImgBG;
    
    // Fit Parameters (Slope, Intercept) for visualization
    double slopeRG = 1.0, iceptRG = 0.0;
    double slopeBG = 1.0, iceptBG = 0.0;
};

#endif // PCCRESULT_H
