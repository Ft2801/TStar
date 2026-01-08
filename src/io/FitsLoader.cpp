#include "FitsLoader.h"
#include <fitsio.h>
#include <QDebug>
#include <vector>
#include <algorithm>
#include <QCoreApplication>
#include <QRegularExpression>

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

    // Read CTYPE for WCS
    char strVal[FLEN_VALUE];
    if (!fits_read_key(fptr, TSTRING, "CTYPE1", strVal, comment, &status_meta)) {
        meta.ctype1 = QString::fromUtf8(strVal).trimmed().remove('\'');
    }
    status_meta = 0;
    if (!fits_read_key(fptr, TSTRING, "CTYPE2", strVal, comment, &status_meta)) {
        meta.ctype2 = QString::fromUtf8(strVal).trimmed().remove('\'');
    }
    status_meta = 0;
    
    // Equinox
    if (!fits_read_key(fptr, TDOUBLE, "EQUINOX", &dv, comment, &status_meta)) {
        meta.equinox = dv;
    }
    status_meta = 0;
    
    // Read SIP coefficients if present
    readSIPCoefficients(fptr, meta);

    if (!fits_read_key(fptr, TSTRING, "OBJECT", strVal, comment, &status_meta)) meta.objectName = QString::fromUtf8(strVal);
    status_meta = 0;
    if (!fits_read_key(fptr, TSTRING, "DATE-OBS", strVal, comment, &status_meta)) meta.dateObs = QString::fromUtf8(strVal);
    
    // Store file path for reference
    meta.filePath = filePath;
    
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
                 // Variable unused - removed
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

// Parse RA string (HMS or decimal degrees)
double FitsLoader::parseRAString(const QString& str, bool* ok) {
    if (ok) *ok = false;
    QString trimmed = str.trimmed();
    
    // Try decimal first
    bool parseOk;
    double val = trimmed.toDouble(&parseOk);
    if (parseOk) {
        if (ok) *ok = true;
        return val;
    }
    
    // Try HMS: "HH MM SS.ss" or "HH:MM:SS.ss"
    QStringList parts = trimmed.split(QRegularExpression("[:\\s]+"));
    if (parts.size() >= 3) {
        double h = parts[0].toDouble();
        double m = parts[1].toDouble();
        double s = parts[2].toDouble();
        if (ok) *ok = true;
        return (h + m/60.0 + s/3600.0) * 15.0;  // Hours to degrees
    }
    
    return 0.0;
}

// Parse Dec string (DMS or decimal degrees)
double FitsLoader::parseDecString(const QString& str, bool* ok) {
    if (ok) *ok = false;
    QString trimmed = str.trimmed();
    
    // Try decimal first
    bool parseOk;
    double val = trimmed.toDouble(&parseOk);
    if (parseOk) {
        if (ok) *ok = true;
        return val;
    }
    
    // Try DMS: "DD MM SS.ss" or "DD:MM:SS.ss"
    QStringList parts = trimmed.split(QRegularExpression("[:\\s]+"));
    if (parts.size() >= 3) {
        QString dStr = parts[0];
        double d = std::abs(dStr.toDouble());
        double m = parts[1].toDouble();
        double s = parts[2].toDouble();
        double sign = dStr.startsWith('-') ? -1.0 : 1.0;
        if (ok) *ok = true;
        return sign * (d + m/60.0 + s/3600.0);
    }
    
    return 0.0;
}

// Read SIP distortion coefficients from FITS header
void FitsLoader::readSIPCoefficients(void* fitsptr, ImageBuffer::Metadata& meta) {
    fitsfile* fptr = static_cast<fitsfile*>(fitsptr);
    int status = 0;
    char comment[FLEN_COMMENT];
    long lval;
    double dv;
    
    // Read polynomial orders
    if (!fits_read_key(fptr, TLONG, "A_ORDER", &lval, comment, &status)) {
        meta.sipOrderA = static_cast<int>(lval);
    }
    status = 0;
    if (!fits_read_key(fptr, TLONG, "B_ORDER", &lval, comment, &status)) {
        meta.sipOrderB = static_cast<int>(lval);
    }
    status = 0;
    if (!fits_read_key(fptr, TLONG, "AP_ORDER", &lval, comment, &status)) {
        meta.sipOrderAP = static_cast<int>(lval);
    }
    status = 0;
    if (!fits_read_key(fptr, TLONG, "BP_ORDER", &lval, comment, &status)) {
        meta.sipOrderBP = static_cast<int>(lval);
    }
    status = 0;
    
    // Read forward coefficients (A_i_j, B_i_j)
    int maxOrder = std::max(meta.sipOrderA, meta.sipOrderB);
    for (int i = 0; i <= maxOrder; ++i) {
        for (int j = 0; j <= maxOrder - i; ++j) {
            if (i == 0 && j == 0) continue;
            
            // A coefficients
            if (i + j <= meta.sipOrderA) {
                QString key = QString("A_%1_%2").arg(i).arg(j);
                if (!fits_read_key(fptr, TDOUBLE, key.toLatin1().constData(), &dv, comment, &status)) {
                    meta.sipCoeffs[key] = dv;
                }
                status = 0;
            }
            
            // B coefficients
            if (i + j <= meta.sipOrderB) {
                QString key = QString("B_%1_%2").arg(i).arg(j);
                if (!fits_read_key(fptr, TDOUBLE, key.toLatin1().constData(), &dv, comment, &status)) {
                    meta.sipCoeffs[key] = dv;
                }
                status = 0;
            }
        }
    }
    
    // Read inverse coefficients (AP_i_j, BP_i_j) if present
    maxOrder = std::max(meta.sipOrderAP, meta.sipOrderBP);
    for (int i = 0; i <= maxOrder; ++i) {
        for (int j = 0; j <= maxOrder - i; ++j) {
            if (i == 0 && j == 0) continue;
            
            // AP coefficients
            if (i + j <= meta.sipOrderAP) {
                QString key = QString("AP_%1_%2").arg(i).arg(j);
                if (!fits_read_key(fptr, TDOUBLE, key.toLatin1().constData(), &dv, comment, &status)) {
                    meta.sipCoeffs[key] = dv;
                }
                status = 0;
            }
            
            // BP coefficients
            if (i + j <= meta.sipOrderBP) {
                QString key = QString("BP_%1_%2").arg(i).arg(j);
                if (!fits_read_key(fptr, TDOUBLE, key.toLatin1().constData(), &dv, comment, &status)) {
                    meta.sipCoeffs[key] = dv;
                }
                status = 0;
            }
        }
    }
}

QMap<QString, FitsExtensionInfo> FitsLoader::listExtensions(const QString& filePath, QString* errorMsg) {
    QMap<QString, FitsExtensionInfo> result;
    fitsfile* fptr;
    int status = 0;
    
    if (fits_open_file(&fptr, filePath.toUtf8().constData(), READONLY, &status)) {
        if (errorMsg) {
            char statusStr[FLEN_STATUS];
            fits_get_errstatus(status, statusStr);
            *errorMsg = QCoreApplication::translate("FitsLoader", "CFITSIO Open Error: %1").arg(statusStr);
        }
        return result;
    }
    
    // Get number of HDUs
    int numHdus = 0;
    fits_get_num_hdus(fptr, &numHdus, &status);
    
    for (int i = 1; i <= numHdus; ++i) { // 1-based in CFITSIO
        if (fits_movabs_hdu(fptr, i, nullptr, &status)) {
            status = 0;
            continue;
        }
        
        int hduType = 0;
        fits_get_hdu_type(fptr, &hduType, &status);
        
        // Only process IMAGE HDUs
        if (hduType != IMAGE_HDU) {
            status = 0;
            continue;
        }
        
        // Get image parameters
        int bitpix, naxis;
        long naxes[9] = {1, 1, 1, 1, 1, 1, 1, 1, 1};
        if (fits_get_img_param(fptr, 9, &bitpix, &naxis, naxes, &status)) {
            status = 0;
            continue;
        }
        
        // Skip if no data
        if (naxis < 2) {
            status = 0;
            continue;
        }
        
        // Get extension name
        char extname[FLEN_VALUE] = "";
        status = 0;
        fits_read_key(fptr, TSTRING, "EXTNAME", extname, nullptr, &status);
        status = 0; // Reset even if not found
        
        FitsExtensionInfo info;
        info.index = i - 1; // Convert to 0-based for our API
        info.name = strlen(extname) > 0 ? QString::fromUtf8(extname) : QString::number(i - 1);
        info.width = naxes[0];
        info.height = naxes[1];
        info.channels = (naxis >= 3 && naxes[2] == 3) ? 3 : 1;
        info.bitpix = bitpix;
        
        // Determine dtype string
        switch (bitpix) {
            case 8:  info.dtype = "uint8"; break;
            case 16: info.dtype = "int16"; break;
            case 32: info.dtype = "int32"; break;
            case -32: info.dtype = "float32"; break;
            case -64: info.dtype = "float64"; break;
            default: info.dtype = QString("bitpix%1").arg(bitpix); break;
        }
        
        QString key = info.name.toUpper();
        result[key] = info;
    }
    
    fits_close_file(fptr, &status);
    return result;
}

bool FitsLoader::loadExtension(const QString& filePath, const QString& extensionKey, 
                               ImageBuffer& buffer, QString* errorMsg) {
    // First, list extensions to find the matching one
    QMap<QString, FitsExtensionInfo> exts = listExtensions(filePath, errorMsg);
    if (exts.isEmpty()) {
        if (errorMsg && errorMsg->isEmpty()) {
            *errorMsg = QCoreApplication::translate("FitsLoader", "No image extensions found in file");
        }
        return false;
    }
    
    // Try to find by key (case-insensitive)
    QString upperKey = extensionKey.toUpper();
    if (exts.contains(upperKey)) {
        return loadExtension(filePath, exts[upperKey].index, buffer, errorMsg);
    }
    
    // Try as numeric index
    bool ok;
    int idx = extensionKey.toInt(&ok);
    if (ok) {
        return loadExtension(filePath, idx, buffer, errorMsg);
    }
    
    if (errorMsg) {
        *errorMsg = QCoreApplication::translate("FitsLoader", "Extension '%1' not found").arg(extensionKey);
    }
    return false;
}

bool FitsLoader::loadExtension(const QString& filePath, int hduIndex, 
                               ImageBuffer& buffer, QString* errorMsg) {
    fitsfile* fptr;
    int status = 0;
    
    if (fits_open_file(&fptr, filePath.toUtf8().constData(), READONLY, &status)) {
        if (errorMsg) {
            char statusStr[FLEN_STATUS];
            fits_get_errstatus(status, statusStr);
            *errorMsg = QCoreApplication::translate("FitsLoader", "CFITSIO Open Error: %1").arg(statusStr);
        }
        return false;
    }
    
    // Move to specified HDU (convert 0-based to 1-based)
    int hduType;
    if (fits_movabs_hdu(fptr, hduIndex + 1, &hduType, &status)) {
        if (errorMsg) {
            char statusStr[FLEN_STATUS];
            fits_get_errstatus(status, statusStr);
            *errorMsg = QCoreApplication::translate("FitsLoader", "Failed to move to HDU %1: %2").arg(hduIndex).arg(statusStr);
        }
        fits_close_file(fptr, &status);
        return false;
    }
    
    if (hduType != IMAGE_HDU) {
        if (errorMsg) {
            *errorMsg = QCoreApplication::translate("FitsLoader", "HDU %1 is not an image extension").arg(hduIndex);
        }
        fits_close_file(fptr, &status);
        return false;
    }
    
    bool result = loadHDU(fptr, hduIndex, buffer, errorMsg);
    
    status = 0;
    fits_close_file(fptr, &status);
    return result;
}

bool FitsLoader::loadHDU(void* fitsptr, [[maybe_unused]] int hduIndex, ImageBuffer& buffer, QString* errorMsg) {
    fitsfile* fptr = static_cast<fitsfile*>(fitsptr);
    int status = 0;
    
    // Get image parameters
    int bitpix, naxis;
    long naxes[9] = {1, 1, 1, 1, 1, 1, 1, 1, 1};
    
    if (fits_get_img_param(fptr, 9, &bitpix, &naxis, naxes, &status)) {
        if (errorMsg) {
            char statusStr[FLEN_STATUS];
            fits_get_errstatus(status, statusStr);
            *errorMsg = QCoreApplication::translate("FitsLoader", "CFITSIO Param Error: %1").arg(statusStr);
        }
        return false;
    }
    
    if (naxis < 2) {
        if (errorMsg) *errorMsg = QCoreApplication::translate("FitsLoader", "Image has < 2 dimensions");
        return false;
    }
    
    int nChannels = (naxis == 3 && naxes[2] == 3) ? 3 : 1;
    int width = naxes[0];
    int height = naxes[1];
    long npixelsPerPlane = width * height;
    
    std::vector<float> allPixels(npixelsPerPlane * nChannels);
    float nulval = 0.0;
    int anynul = 0;
    
    for (int c = 0; c < nChannels; ++c) {
        long firstpix[3] = {1, 1, c + 1};
        std::vector<float> planePixels(npixelsPerPlane);
        
        if (fits_read_pix(fptr, TFLOAT, firstpix, npixelsPerPlane, &nulval, planePixels.data(), &anynul, &status)) {
            if (errorMsg) {
                char statusStr[FLEN_STATUS];
                fits_get_errstatus(status, statusStr);
                *errorMsg = QCoreApplication::translate("FitsLoader", "CFITSIO Read Error: %1").arg(statusStr);
            }
            return false;
        }
        
        for (int i = 0; i < npixelsPerPlane; ++i) {
            allPixels[i * nChannels + c] = planePixels[i];
        }
    }
    
    // Normalize Data
    float globalMax = -1e30f;
    float globalMin = 1e30f;
    
    for (float v : allPixels) {
        if (v > globalMax) globalMax = v;
        if (v < globalMin) globalMin = v;
    }
    
    bool needsNorm = (globalMax > 1.0f);
    
    if (needsNorm) {
        float range = globalMax - globalMin;
        if (range < 1e-9) range = 1.0f;
        
        float divisor = 1.0f;
        if (bitpix == 8) divisor = 255.0f;
        else if (bitpix == 16) divisor = 65535.0f;
        else if (bitpix == 32) divisor = 4294967295.0f;
        
        if (bitpix > 0 && globalMax <= divisor * 1.01f) {
            for (auto& p : allPixels) p /= divisor;
        } else {
            for (auto& p : allPixels) p = (p - globalMin) / range;
        }
    }
    
    // Extract Metadata (same as load())
    ImageBuffer::Metadata meta;
    char comment[FLEN_COMMENT];
    double dv;
    int status_meta = 0;
    
    if (!fits_read_key(fptr, TDOUBLE, "FOCALLEN", &dv, comment, &status_meta)) meta.focalLength = dv;
    else { status_meta = 0; if (!fits_read_key(fptr, TDOUBLE, "FOCAL", &dv, comment, &status_meta)) meta.focalLength = dv; }
    status_meta = 0;
    
    if (!fits_read_key(fptr, TDOUBLE, "PIXSIZE", &dv, comment, &status_meta)) meta.pixelSize = dv;
    else { status_meta = 0; if (!fits_read_key(fptr, TDOUBLE, "XPIXSZ", &dv, comment, &status_meta)) meta.pixelSize = dv; }
    status_meta = 0;
    
    // WCS
    if (!fits_read_key(fptr, TDOUBLE, "CRVAL1", &dv, comment, &status_meta)) { meta.ra = dv; }
    status_meta = 0;
    if (!fits_read_key(fptr, TDOUBLE, "CRVAL2", &dv, comment, &status_meta)) { meta.dec = dv; }
    status_meta = 0;
    if (!fits_read_key(fptr, TDOUBLE, "CRPIX1", &dv, comment, &status_meta)) { meta.crpix1 = dv; }
    status_meta = 0;
    if (!fits_read_key(fptr, TDOUBLE, "CRPIX2", &dv, comment, &status_meta)) { meta.crpix2 = dv; }
    status_meta = 0;
    
    // CD Matrix
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
    
    if (hasCD) {
        meta.cd1_1 = cd11; meta.cd1_2 = cd12;
        meta.cd2_1 = cd21; meta.cd2_2 = cd22;
    }
    
    char strVal[FLEN_VALUE];
    if (!fits_read_key(fptr, TSTRING, "CTYPE1", strVal, comment, &status_meta)) {
        meta.ctype1 = QString::fromUtf8(strVal).trimmed().remove('\'');
    }
    status_meta = 0;
    if (!fits_read_key(fptr, TSTRING, "CTYPE2", strVal, comment, &status_meta)) {
        meta.ctype2 = QString::fromUtf8(strVal).trimmed().remove('\'');
    }
    status_meta = 0;
    
    if (!fits_read_key(fptr, TDOUBLE, "EQUINOX", &dv, comment, &status_meta)) {
        meta.equinox = dv;
    }
    status_meta = 0;
    
    readSIPCoefficients(fptr, meta);
    
    if (!fits_read_key(fptr, TSTRING, "OBJECT", strVal, comment, &status_meta)) meta.objectName = QString::fromUtf8(strVal);
    status_meta = 0;
    if (!fits_read_key(fptr, TSTRING, "DATE-OBS", strVal, comment, &status_meta)) meta.dateObs = QString::fromUtf8(strVal);
    
    // Store extension info in metadata
    char extname[FLEN_VALUE] = "";
    status_meta = 0;
    fits_read_key(fptr, TSTRING, "EXTNAME", extname, nullptr, &status_meta);
    if (strlen(extname) > 0) {
        meta.objectName = meta.objectName.isEmpty() ? QString::fromUtf8(extname) : meta.objectName;
    }
    
    buffer.setMetadata(meta);
    buffer.setData(width, height, nChannels, allPixels);
    return true;
}

