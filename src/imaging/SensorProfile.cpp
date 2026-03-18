#include "SensorProfile.h"
#include <QDebug>

SensorProfileDatabase& SensorProfileDatabase::instance() {
    static SensorProfileDatabase db;
    return db;
}

void SensorProfileDatabase::initBuiltins() {
    // Generic CCD (default)
    {
        SensorProfile ccd;
        ccd.name = "Generic CCD";
        ccd.type = "CCD";
        ccd.pixelWidth = 4.65;
        ccd.pixelHeight = 4.65;
        ccd.sensorWidth = 2048;
        ccd.sensorHeight = 1536;
        ccd.gain = 0.7;           // e-/ADU typical for CCD
        ccd.readNoise = 5.0;       // RMS electrons
        ccd.darkCurrent = 0.001;   // e-/s very low for CCD
        ccd.fullWell = 50000.0;    // electrons
        ccd.maxADU = 65535.0;
        ccd.tempRefC = 25.0;
        ccd.darkCurrentGradient = 0.05; // doubles per 5-6°C
        m_profiles["Generic CCD"] = ccd;
    }
    
    // Generic CMOS (high gain, high read noise)
    {
        SensorProfile cmos;
        cmos.name = "Generic CMOS";
        cmos.type = "CMOS";
        cmos.pixelWidth = 3.45;
        cmos.pixelHeight = 3.45;
        cmos.sensorWidth = 3840;
        cmos.sensorHeight = 2160;
        cmos.gain = 3.5;           // e-/ADU higher for CMOS
        cmos.readNoise = 25.0;      // RMS electrons (higher noise)
        cmos.darkCurrent = 0.1;     // Higher dark current
        cmos.fullWell = 40000.0;
        cmos.maxADU = 65535.0;
        cmos.tempRefC = 25.0;
        cmos.darkCurrentGradient = 0.08;
        m_profiles["Generic CMOS"] = cmos;
    }
    
    // Canon EOS series
    {
        SensorProfile eos;
        eos.name = "Canon EOS R7";
        eos.type = "DSLR";
        eos.pixelWidth = 3.31;
        eos.pixelHeight = 3.31;
        eos.sensorWidth = 7360;
        eos.sensorHeight = 4912;
        eos.gain = 1.2;            // Canon DSLRs typically
        eos.readNoise = 8.0;
        eos.darkCurrent = 0.05;
        eos.fullWell = 65800.0;
        eos.maxADU = 65535.0;
        eos.tempRefC = 25.0;
        eos.darkCurrentGradient = 0.06;
        m_profiles["Canon EOS R7"] = eos;
    }
    
    // ZWO ASI
    {
        SensorProfile zwoPro;
        zwoPro.name = "ZWO ASI533MM-Pro";
        zwoPro.type = "CMOS";
        zwoPro.pixelWidth = 3.76;
        zwoPro.pixelHeight = 3.76;
        zwoPro.sensorWidth = 3856;
        zwoPro.sensorHeight = 2574;
        zwoPro.gain = 0.8;
        zwoPro.readNoise = 6.0;
        zwoPro.darkCurrent = 0.02;
        zwoPro.fullWell = 100000.0;  // High capacity
        zwoPro.maxADU = 65535.0;
        zwoPro.tempRefC = 20.0;
        zwoPro.darkCurrentGradient = 0.07;
        m_profiles["ZWO ASI533MM-Pro"] = zwoPro;
    }
}

bool SensorProfileDatabase::loadProfile(const QString& name, SensorProfile& out) {
    static bool initialized = false;
    if (!initialized) {
        const_cast<SensorProfileDatabase*>(this)->initBuiltins();
        initialized = true;
    }
    
    auto it = m_profiles.find(name);
    if (it != m_profiles.end()) {
        out = it->second;
        return true;
    }
    
    qWarning().noquote() << "[SensorProfile] Profile not found:" << name << "- using generic CCD";
    out = getDefault();
    return false;
}

void SensorProfileDatabase::registerProfile(const SensorProfile& profile) {
    m_profiles[profile.name] = profile;
}

std::vector<QString> SensorProfileDatabase::listProfiles() const {
    std::vector<QString> list;
    for (const auto& [name, _] : m_profiles) {
        list.push_back(name);
    }
    return list;
}

SensorProfile SensorProfileDatabase::getDefault() {
    SensorProfile def;
    def.name = "Generic CCD (Default)";
    def.type = "CCD";
    def.pixelWidth = 4.65;
    def.pixelHeight = 4.65;
    def.sensorWidth = 2048;
    def.sensorHeight = 1536;
    def.gain = 0.7;
    def.readNoise = 5.0;
    def.darkCurrent = 0.001;
    def.fullWell = 50000.0;
    def.maxADU = 65535.0;
    def.tempRefC = 25.0;
    def.darkCurrentGradient = 0.05;
    def.isCustom = false;
    return def;
}
