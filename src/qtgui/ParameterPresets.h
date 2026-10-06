#ifndef PARAMETER_PRESETS_H
#define PARAMETER_PRESETS_H

#include "ParameterState.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QVariantMap>

namespace qtgui_presets {

/** Independent parameter domains exposed as named application presets.

    Registration/geometry, control points, crop/orientation, tile adjustments,
    fitted profile values, flat-field/adaptive correction grids, and profile
    spots are intentionally not preset domains: they are tied to a particular
    image or calibration. */
enum class Scope {
    Capture,
    Process,
    Reconstruction,
    Color
};

/** One persisted named preset backed by the existing versioned CSP payload. */
struct Record {
    QString id;
    QString name;
    Scope scope = Scope::Capture;
    QByteArray payload;
    /** Qt-only fields not represented by the legacy CSP stream. */
    QVariantMap extras;
};

/** Stable storage key for SCOPE. */
QString scopeKey(Scope scope);

/** User-facing label for SCOPE. */
QString scopeLabel(Scope scope);

/** Explain exactly which values SCOPE changes and which calibrations it omits. */
QString scopeDescription(Scope scope);

/** Return all valid preset records in stable name/scope order. */
QList<Record> records();

/** Save or replace NAME within SCOPE using STATE as the source snapshot. */
bool save(const QString &name, Scope scope, const ParameterState &state,
          QString *error = nullptr);

/** Remove preset ID. */
bool remove(const QString &id);

/** Decode RECORD's stored state. */
bool decode(const Record &record, ParameterState *state,
            QString *error = nullptr);

/** Apply RECORD's one domain to TARGET.

    CLEAREDREGISTRATION is set when changing the historical screen type made
    screen-bound registration/detection/profile evidence semantically invalid
    and therefore cleared it atomically. */
bool apply(const Record &record, ParameterState *target,
           bool *clearedRegistration = nullptr, QString *error = nullptr);

} // namespace qtgui_presets

#endif // PARAMETER_PRESETS_H
