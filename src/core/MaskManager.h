#ifndef MASKMANAGER_H
#define MASKMANAGER_H

#include <QObject>
#include <QMap>
#include <QString>
#include "MaskLayer.h"

class MaskManager : public QObject {
    Q_OBJECT
public:
    static MaskManager& instance() {
        static MaskManager inst;
        return inst;
    }

    void addMask(const QString& name, const MaskLayer& mask) {
        MaskLayer m = mask;
        m.name = name;
        m_masks[name] = m;
        emit masksChanged();
    }

    void removeMask(const QString& name) {
        if (m_masks.remove(name)) {
            emit masksChanged();
        }
    }

    MaskLayer getMask(const QString& name) const {
        return m_masks.value(name);
    }

    QStringList getMaskNames() const {
        return m_masks.keys();
    }

    QMap<QString, MaskLayer> getAllMasks() const {
        return m_masks;
    }

signals:
    void masksChanged();

private:
    MaskManager() = default;
    ~MaskManager() = default;
    MaskManager(const MaskManager&) = delete;
    MaskManager& operator=(const MaskManager&) = delete;

    QMap<QString, MaskLayer> m_masks;
};

#endif // MASKMANAGER_H
