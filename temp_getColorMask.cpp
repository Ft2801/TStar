
std::vector<float> MaskGenerationDialog::getColorMask(const QString& color) const {
    
    int w = m_sourceImage.width();
    int h = m_sourceImage.height();
    
    // Return zeros if not RGB
    if (m_sourceImage.channels() < 3) {
        return std::vector<float>(w*h, 0.0f);
    }
    
    // Convert to 8-bit for OpenCV
    const std::vector<float>& data = m_sourceImage.data();
    cv::Mat rgb8(h, w, CV_8UC3);
    
    #pragma omp parallel for
    for (int i = 0; i < h*w; ++i) {
        rgb8.at<cv::Vec3b>(i)[0] = static_cast<uchar>(std::clamp(data[i*3] * 255.0f, 0.0f, 255.0f));
        rgb8.at<cv::Vec3b>(i)[1] = static_cast<uchar>(std::clamp(data[i*3+1] * 255.0f, 0.0f, 255.0f));
        rgb8.at<cv::Vec3b>(i)[2] = static_cast<uchar>(std::clamp(data[i*3+2] * 255.0f, 0.0f, 255.0f));
    }
    
    // Convert RGB to HLS
    cv::Mat hls;
    cv::cvtColor(rgb8, hls, cv::COLOR_RGB2HLS);
    
    // Extract hue channel (0-180 in OpenCV) and convert to 0-360
    cv::Mat hue_float(h, w, CV_32FC1);
    for (int i = 0; i < h*w; ++i) {
        float h_val = hls.at<cv::Vec3b>(i)[0]; // H channel
        hue_float.at<float>(i) = (h_val / 180.0f) * 360.0f;
    }
    
    // Define color ranges (in degrees, 0-360)
    struct Range { float lo; float hi; };
    std::vector<Range> ranges;
    
    if (color == "Red") {
        ranges = {{0, 10}, {350, 360}}; // Wraps around
    } else if (color == "Orange") {
        ranges = {{10, 40}};
    } else if (color == "Yellow") {
        ranges = {{40, 70}};
    } else if (color == "Green") {
        ranges = {{70, 170}};
    } else if (color == "Cyan") {
        ranges = {{170, 200}};
    } else if (color == "Blue") {
        ranges = {{200, 270}};
    } else if (color == "Magenta") {
        ranges = {{270, 350}};
    } else {
        // Unknown color, return zeros
        return std::vector<float>(w*h, 0.0f);
    }
    
    // Create mask based on hue ranges
    std::vector<float> mask(w*h, 0.0f);
    
    #pragma omp parallel for
    for (int i = 0; i < h*w; ++i) {
        float hue = hue_float.at<float>(i);
        float val = 0.0f;
        
        for (const auto& r : ranges) {
            if (hue >= r.lo && hue <= r.hi) {
                val = 1.0f;
                break;
            }
        }
        
        mask[i] = val;
    }
    
    return mask;
}
