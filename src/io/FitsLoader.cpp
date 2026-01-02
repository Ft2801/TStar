#include "FitsLoader.h"
#include <fitsio.h>
#include <QDebug>
#include <vector>
#include <algorithm>
#include <QCoreApplication>

bool FitsLoader::load(const QString& filePath, ImageBuffer& buffer, QString* errorMsg) {
    fitsfile* fptr;
    int status = 0;
    
    // Open file
    if (fits_open_file(&fptr, filePath.toUtf8().constData(), READONLY, &status)) {
        if (errorMsg) {
            char statusStr[FLEN_STATUS];
            fits_get_errstatus(status, statusStr);
            *errorMsg = QCoreApplication::translate("FitsLoader", "CFITSIO Open Error %1: %2\nPath: %3").arg(status).arg(statusStr).arg(filePath);
        }
        return false;
    }

    // Get image parameters
    int bitpix, naxis;
    int maxdim = 9;
    long naxes[9] = {1, 1, 1, 1, 1, 1, 1, 1, 1};
    
    if (fits_get_img_param(fptr, maxdim, &bitpix, &naxis, naxes, &status)) {
        if (errorMsg) {
             char statusStr[FLEN_STATUS];
             fits_get_errstatus(status, statusStr);
             *errorMsg = QCoreApplication::translate("FitsLoader", "CFITSIO Param Error %1: %2").arg(status).arg(statusStr);
        }
        status = 0; 
        fits_close_file(fptr, &status);
        return false;
    }
    
    // Check dimensions
    if (naxis < 2) {
         if (errorMsg) *errorMsg = QCoreApplication::translate("FitsLoader", "Image has < 2 dimensions (NAXIS=%1)").arg(naxis);
         fits_close_file(fptr, &status);
         return false;
    }
    
    // Check if 3D (e.g. RGB)
    int nChannels = 1;
    if (naxis == 3) {
        // usually NAXIS3 = 3 for RGB
        if (naxes[2] == 3) nChannels = 3;
    }
    
    int width = naxes[0];
    int height = naxes[1];
    long npixelsPerPlane = width * height;
    
    // Resize buffer (interleaved)
    std::vector<float> allPixels(npixelsPerPlane * nChannels);
    
    float nulval = 0.0;
    int anynul = 0;
    
    for (int c = 0; c < nChannels; ++c) {
        long firstpix[3] = {1, 1, c + 1}; // FITS is 1-based. Plane starts at c+1
        std::vector<float> planePixels(npixelsPerPlane);
        
        if (fits_read_pix(fptr, TFLOAT, firstpix, npixelsPerPlane, &nulval, planePixels.data(), &anynul, &status)) {
             if (errorMsg) {
                 char statusStr[FLEN_STATUS];
                 fits_get_errstatus(status, statusStr);
                 *errorMsg = QCoreApplication::translate("FitsLoader", "CFITSIO Read Error (Plane %1) %2: %3").arg(c).arg(status).arg(statusStr);
             }
             fits_close_file(fptr, &status);
             return false;
        }
        
        // Normalize plane immediately
        // Read plane and copy to interleaved buffer
        for (int i = 0; i < npixelsPerPlane; ++i) {
            allPixels[i * nChannels + c] = planePixels[i];
        }
    }
    
    // Normalize Data (Global Max across all channels to preserve color balance)
    float globalMax = -1e30f;
    float globalMin = 1e30f;
    
    for (float v : allPixels) {
        if (v > globalMax) globalMax = v;
        if (v < globalMin) globalMin = v;
    }
    
    // Heuristic: if values are ints > 1.0, normalize.
    bool needsNorm = (globalMax > 1.0f);
    
    if (needsNorm) {
        float range = globalMax - globalMin;
        if (range < 1e-9) range = 1.0f;
        
        // Normalize integer data by bit depth divisor to maintain absolute black at 0.
        
        float divisor = 1.0f;
        if (bitpix == 8) divisor = 255.0f;
        else if (bitpix == 16) divisor = 65535.0f;
        else if (bitpix == 32) divisor = 4294967295.0f;
        
        // If max value is roughly within range of divisor, use it.
        // Otherwise use min/max scaling.
        if (bitpix > 0 && globalMax <= divisor * 1.01f) {
             for (auto& p : allPixels) p /= divisor;
        } else {
             // Arbitrary float range
             for (auto& p : allPixels) p = (p - globalMin) / range;
        }
    }
    
    // Extract Metadata
    ImageBuffer::Metadata meta;
    char comment[FLEN_COMMENT];
    double dv;
    int status_meta = 0;
    
    // Focal Length (mm)
    if (!fits_read_key(fptr, TDOUBLE, "FOCALLEN", &dv, comment, &status_meta)) meta.focalLength = dv;
    else { status_meta = 0; if (!fits_read_key(fptr, TDOUBLE, "FOCAL", &dv, comment, &status_meta)) meta.focalLength = dv; }
    status_meta = 0;
    
    // Pixel Size (microns)
    if (!fits_read_key(fptr, TDOUBLE, "PIXSIZE", &dv, comment, &status_meta)) meta.pixelSize = dv;
    else { status_meta = 0; if (!fits_read_key(fptr, TDOUBLE, "XPIXSZ", &dv, comment, &status_meta)) meta.pixelSize = dv; }
    status_meta = 0;

    // RA / Dec (degrees)
    if (!fits_read_key(fptr, TDOUBLE, "RA", &dv, comment, &status_meta)) meta.ra = dv;
    else { 
        status_meta = 0; 
        char raStr[FLEN_VALUE];
        if (!fits_read_key(fptr, TSTRING, "OBJCTRA", raStr, comment, &status_meta)) {
            // Attempt decimal string parse; HH:MM:SS conversion not yet implemented
            meta.ra = QString(raStr).toDouble(); 
        }
    }
    status_meta = 0;
    
    if (!fits_read_key(fptr, TDOUBLE, "DEC", &dv, comment, &status_meta)) meta.dec = dv;
    else { 
        status_meta = 0; 
        char decStr[FLEN_VALUE];
        if (!fits_read_key(fptr, TSTRING, "OBJCTDEC", decStr, comment, &status_meta)) {
            meta.dec = QString(decStr).toDouble();
        }
    }
    status_meta = 0;
    
    // WCS Keywords (Critical for PCC and Image Annotator)
    if (!fits_read_key(fptr, TDOUBLE, "CRVAL1", &dv, comment, &status_meta)) { meta.ra = dv; }
    status_meta = 0;
    if (!fits_read_key(fptr, TDOUBLE, "CRVAL2", &dv, comment, &status_meta)) { meta.dec = dv; }
    status_meta = 0;
    if (!fits_read_key(fptr, TDOUBLE, "CRPIX1", &dv, comment, &status_meta)) { meta.crpix1 = dv; }
    status_meta = 0;
    if (!fits_read_key(fptr, TDOUBLE, "CRPIX2", &dv, comment, &status_meta)) { meta.crpix2 = dv; }
    status_meta = 0;
    
    // Read CD Matrix or CDELT/PC
    double cd11=0, cd12=0, cd21=0, cd22=0;
    bool hasCD = false;
    if (!fits_read_key(fptr, TDOUBLE, "CD1_1", &dv, comment, &status_meta)) { cd11 = dv; hasCD = true; }
    status_meta = 0;
    if (!fits_read_key(fptr, TDOUBLE, "CD1_2", &dv, comment, &status_meta)) { cd12 = dv; hasCD = true; }
    status_meta = 0;
    if (!fits_read_key(fptr, TDOUBLE, "CD2_1", &dv, comment, &status_meta)) { cd21 = dv; hasCD = true; }
    status_meta = 0;
    if (!fits_read_key(fptr, TDOUBLE, "CD2_2", &dv, comment, &status_meta)) { cd22 = dv; hasCD = true; }
    status_meta = 0;

    if (!hasCD) {
        // Try CDELT + PC or CDELT + CROTA
        double cdelt1=1, cdelt2=1;
        if (!fits_read_key(fptr, TDOUBLE, "CDELT1", &dv, comment, &status_meta)) cdelt1 = dv;
        status_meta = 0;
        if (!fits_read_key(fptr, TDOUBLE, "CDELT2", &dv, comment, &status_meta)) cdelt2 = dv;
        status_meta = 0;
        
        double pc11=1, pc12=0, pc21=0, pc22=1;
        bool hasPC = false;
        if (!fits_read_key(fptr, TDOUBLE, "PC1_1", &dv, comment, &status_meta)) { pc11 = dv; hasPC = true; }
        status_meta = 0;
        if (!fits_read_key(fptr, TDOUBLE, "PC1_2", &dv, comment, &status_meta)) { pc12 = dv; hasPC = true; }
        status_meta = 0;
        if (!fits_read_key(fptr, TDOUBLE, "PC2_1", &dv, comment, &status_meta)) { pc21 = dv; hasPC = true; }
        status_meta = 0;
        if (!fits_read_key(fptr, TDOUBLE, "PC2_2", &dv, comment, &status_meta)) { pc22 = dv; hasPC = true; }
        status_meta = 0;

        if (hasPC) {
            cd11 = pc11 * cdelt1;
            cd12 = pc12 * cdelt1;
            cd21 = pc21 * cdelt2;
            cd22 = pc22 * cdelt2;
        } else {
            // Check for CROTA2
            double crota2 = 0;
            if (!fits_read_key(fptr, TDOUBLE, "CROTA2", &dv, comment, &status_meta)) {
                crota2 = dv * M_PI / 180.0;
                cd11 = cdelt1 * cos(crota2);
                cd12 = -cdelt2 * sin(crota2);
                cd21 = cdelt1 * sin(crota2);
                cd22 = cdelt2 * cos(crota2);
            } else {
                // Just Scale
                cd11 = cdelt1;
                cd22 = cdelt2;
            }
            status_meta = 0;
        }
    }
    
    meta.cd1_1 = cd11; meta.cd1_2 = cd12;
    meta.cd2_1 = cd21; meta.cd2_2 = cd22;

    char strVal[FLEN_VALUE];
    if (!fits_read_key(fptr, TSTRING, "OBJECT", strVal, comment, &status_meta)) meta.objectName = QString::fromUtf8(strVal);
    status_meta = 0;
    if (!fits_read_key(fptr, TSTRING, "DATE-OBS", strVal, comment, &status_meta)) meta.dateObs = QString::fromUtf8(strVal);
    
    // Read ALL Header Keys for Viewer
    int nkeys = 0;
    int morekeys = 0;
    status_meta = 0;
    if (fits_get_hdrspace(fptr, &nkeys, &morekeys, &status_meta) == 0) {
        for (int i = 1; i <= nkeys; ++i) { // 1-indexed
            char card[FLEN_CARD];
            if (fits_read_record(fptr, i, card, &status_meta) == 0) {
                 // Parse key, value, and comment using fits_read_keyn
                 char keyname[FLEN_KEYWORD], value[FLEN_VALUE], comm[FLEN_COMMENT];
                 int len = 0;
                 if (fits_read_keyn(fptr, i, keyname, value, comm, &status_meta) == 0) {
                     meta.rawHeaders.push_back({QString(keyname), QString(value), QString(comm)});
                 } else {
                     status_meta = 0; // reset
                     // Maybe it's a comment or history line without value
                     // Store raw card?
                     meta.rawHeaders.push_back({QString("RAW"), QString::fromUtf8(card, 80).trimmed(), ""});
                 }
            } else {
                status_meta = 0; // reset
            }
        }
    }
    
    // Final Close
    status = 0;
    fits_close_file(fptr, &status);

    buffer.setMetadata(meta);
    buffer.setData(width, height, nChannels, allPixels);   
    return true;
}
