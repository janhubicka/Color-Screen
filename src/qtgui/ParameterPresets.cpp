#include "ParameterPresets.h"

#include <QCoreApplication>
#include <QSettings>
#include <QUuid>
#include <QtGlobal>

#include <algorithm>
#include <cstdio>
#include <optional>
#include <utility>
#include <vector>

namespace {

constexpr auto presetRoot = "parameterPresets/v1";

/** Serialize STATE through the existing portable CSP + Qt metadata writer. */
QByteArray serializePresetState(const ParameterState &state, QString *error)
{
    FILE *file = std::tmpfile();
    if (!file) {
        if (error)
            *error = QCoreApplication::translate(
                "ParameterPreset", "Could not create temporary preset data.");
        return {};
    }

    bool ok = colorscreen::save_csp_with_profile_spots(
        file, &state.scrToImg, &state.detect, &state.rparams, &state.solver,
        state.profileSpots);
    if (ok && std::fflush(file) != 0)
        ok = false;
    if (ok && std::fseek(file, 0, SEEK_SET) != 0)
        ok = false;

    QByteArray payload;
    char buffer[64 * 1024];
    while (ok) {
        const size_t count = std::fread(buffer, 1, sizeof(buffer), file);
        if (count > 0)
            payload.append(buffer, static_cast<qsizetype>(count));
        if (count < sizeof(buffer)) {
            if (std::ferror(file))
                ok = false;
            break;
        }
    }
    std::fclose(file);

    if (!ok || payload.isEmpty()) {
        if (error)
            *error = QCoreApplication::translate(
                "ParameterPreset", "Could not serialize the preset snapshot.");
        return {};
    }

    if (error)
        error->clear();
    return payload;
}

/** Decode one CSP/Qt preset PAYLOAD into STATE without touching a document. */
bool decodePresetState(const QByteArray &payload, ParameterState *state,
                       QString *error)
{
    if (!state || payload.isEmpty()) {
        if (error)
            *error = QCoreApplication::translate(
                "ParameterPreset", "The preset contains no parameter data.");
        return false;
    }

    FILE *file = std::tmpfile();
    if (!file) {
        if (error)
            *error = QCoreApplication::translate(
                "ParameterPreset", "Could not create temporary preset data.");
        return false;
    }

    bool ok =
        std::fwrite(payload.constData(), 1, static_cast<size_t>(payload.size()),
                    file) == static_cast<size_t>(payload.size()) &&
        std::fflush(file) == 0 && std::fseek(file, 0, SEEK_SET) == 0;

    ParameterState decoded;
    std::vector<colorscreen::color_match> ignoredSpotResults;
    const char *libraryError = nullptr;
    if (ok)
        ok = colorscreen::load_csp_with_profile_spots(
            file, &decoded.scrToImg, &decoded.detect, &decoded.rparams,
            &decoded.solver, &libraryError, &decoded.profileSpots,
            &ignoredSpotResults);
    std::fclose(file);

    if (!ok || libraryError) {
        if (error)
            *error = libraryError
                         ? QString::fromUtf8(libraryError)
                         : QCoreApplication::translate(
                               "ParameterPreset",
                               "The preset parameter snapshot is invalid.");
        return false;
    }

    *state = std::move(decoded);
    if (error)
        error->clear();
    return true;
}

/** Parse one stable scope storage key. */
std::optional<qtgui_presets::Scope> scopeFromKey(const QString &key)
{
    using Scope = qtgui_presets::Scope;
    if (key == QStringLiteral("capture"))
        return Scope::Capture;
    if (key == QStringLiteral("process"))
        return Scope::Process;
    if (key == QStringLiteral("reconstruction"))
        return Scope::Reconstruction;
    if (key == QStringLiteral("color"))
        return Scope::Color;
    return std::nullopt;
}

/** Clear registration that becomes meaningless after a screen-type change.

    Final presentation rotation/mirroring, scanner geometry policy, and the
    lens model are capture/device state rather than screen-lattice coordinates,
    so keep them. */
void clearScreenBoundStateForScreenChange(ParameterState *state)
{
    if (!state)
        return;

    state->scrToImg.center = {0, 0};
    state->scrToImg.coordinate1 = {0, 0};
    state->scrToImg.coordinate2 = {0, 0};
    state->scrToImg.projection_distance = 1;
    state->scrToImg.tilt_x = 0;
    state->scrToImg.tilt_y = 0;
    state->scrToImg.mesh_trans.reset();
    state->scrToImg.mesh_trans_is_scr_to_img = false;
    state->solver.remove_points();
    state->detect = colorscreen::scr_detect_parameters();
    state->profileSpots.clear();
}

/** Apply capture/device defaults without copying image-specific calibrations. */
void applyCaptureScope(ParameterState *target, const ParameterState &source)
{
    auto &dst = target->rparams;
    const auto &src = source.rparams;

    dst.capture_type = src.capture_type;
    dst.demosaic = src.demosaic;
    dst.gamma = src.gamma;
    dst.dark_point = src.dark_point;
    dst.scan_exposure = src.scan_exposure;

    // Camera/scanner matrix metadata is reusable across captures from the same
    // device. Flat-field grids, adaptive corrections, crop/orientation and
    // per-tile adjustments deliberately remain document-local.
    dst.scanner_red = src.scanner_red;
    dst.scanner_green = src.scanner_green;
    dst.scanner_blue = src.scanner_blue;

    auto &dstMtf = dst.sharpen.scanner_mtf;
    const auto &srcMtf = src.sharpen.scanner_mtf;
    dstMtf.scan_dpi = srcMtf.scan_dpi;
    dstMtf.f_stop = srcMtf.f_stop;
    dstMtf.wavelength = srcMtf.wavelength;
    dstMtf.wavelengths = srcMtf.wavelengths;
    dstMtf.pixel_pitch = srcMtf.pixel_pitch;
    dstMtf.sensor_fill_factor = srcMtf.sensor_fill_factor;
}

/** Apply historical-process/image-layer assumptions. */
bool applyProcessScope(ParameterState *target, const ParameterState &source)
{
    auto &dst = target->rparams;
    const auto &src = source.rparams;

    const bool screenChanged = target->scrToImg.type != source.scrToImg.type;
    if (screenChanged)
        clearScreenBoundStateForScreenChange(target);
    target->scrToImg.type = source.scrToImg.type;

    dst.ignore_infrared = src.ignore_infrared;
    dst.mix_dark = src.mix_dark;
    dst.mix_red = src.mix_red;
    dst.mix_green = src.mix_green;
    dst.mix_blue = src.mix_blue;

    dst.contact_copy = src.contact_copy;
    dst.red_strip_width = src.red_strip_width;
    dst.green_strip_width = src.green_strip_width;

    // Dye identity and physical aging/density are historical-process
    // assumptions. Fitted scanner/process profile matrices remain calibration
    // results and are intentionally not copied.
    dst.color_model = src.color_model;
    dst.age = src.age;
    dst.dye_density = src.dye_density;
    return screenChanged;
}

/** Apply reconstruction algorithms while retaining measured/fitted calibration. */
void applyReconstructionScope(ParameterState *target,
                              const ParameterState &source)
{
    auto &dst = target->rparams;
    const auto &src = source.rparams;

    dst.collection_quality = src.collection_quality;
    dst.screen_demosaic = src.screen_demosaic;
    dst.demosaiced_scaling = src.demosaiced_scaling;
    dst.screen_blur_radius = src.screen_blur_radius;
    dst.screen_denoise = src.screen_denoise;
    dst.demosaiced_denoise = src.demosaiced_denoise;
    dst.collection_threshold = src.collection_threshold;

    // The algorithm/regularization controls form the reusable reconstruction
    // recipe. scanner_mtf contains measurements/fitted optical provenance, and
    // scanner_blur_correction is an accepted spatial calibration; keep both.
    dst.sharpen.mode = src.sharpen.mode;
    dst.sharpen.usm_radius = src.sharpen.usm_radius;
    dst.sharpen.usm_amount = src.sharpen.usm_amount;
    dst.sharpen.scanner_snr = src.sharpen.scanner_snr;
    dst.sharpen.scanner_mtf_scale = src.sharpen.scanner_mtf_scale;
    dst.sharpen.richardson_lucy_iterations =
        src.sharpen.richardson_lucy_iterations;
    dst.sharpen.richardson_lucy_sigma = src.sharpen.richardson_lucy_sigma;
    dst.sharpen.supersample = src.sharpen.supersample;
    dst.sharpen.resampling = src.sharpen.resampling;
}

/** Apply appearance/output controls without copying fitted profile matrices. */
void applyColorScope(ParameterState *target, const ParameterState &source)
{
    auto &dst = target->rparams;
    const auto &src = source.rparams;

    dst.white_balance = src.white_balance;
    dst.presaturation = src.presaturation;
    dst.temperature = src.temperature;
    dst.backlight_temperature = src.backlight_temperature;
    dst.dye_balance = src.dye_balance;
    dst.saturation = src.saturation;
    dst.brightness = src.brightness;
    dst.output_tone_curve = src.output_tone_curve;
    // observer_whitepoint, custom tone-curve points, output profile/gamma and
    // gamut warning are not represented by the legacy CSP stream. They are
    // carried by Record::extras instead of being silently reset here.
}

} // namespace

namespace qtgui_presets {

QString scopeKey(Scope scope)
{
    switch (scope) {
    case Scope::Capture:
        return QStringLiteral("capture");
    case Scope::Process:
        return QStringLiteral("process");
    case Scope::Reconstruction:
        return QStringLiteral("reconstruction");
    case Scope::Color:
        return QStringLiteral("color");
    }
    Q_UNREACHABLE();
}

QString scopeLabel(Scope scope)
{
    switch (scope) {
    case Scope::Capture:
        return QCoreApplication::translate("ParameterPreset", "Capture");
    case Scope::Process:
        return QCoreApplication::translate("ParameterPreset", "Process");
    case Scope::Reconstruction:
        return QCoreApplication::translate("ParameterPreset", "Reconstruction");
    case Scope::Color:
        return QCoreApplication::translate("ParameterPreset", "Color");
    }
    Q_UNREACHABLE();
}

QString scopeDescription(Scope scope)
{
    switch (scope) {
    case Scope::Capture:
        return QCoreApplication::translate(
            "ParameterPreset",
            "Capture type, RAW demosaic/gamma, exposure/black level, scanner "
            "matrix metadata, and reusable MTF hardware metadata. Crop, "
            "rotation, flat-field/adaptive correction grids, tile adjustments "
            "and measured/fitted MTF curves are not changed.");
    case Scope::Process:
        return QCoreApplication::translate(
            "ParameterPreset",
            "Historical screen type, image-layer mixing, contact-copy "
            "simulation, strip proportions, and dye model assumptions. If the "
            "screen type changes, incompatible registration coordinates, "
            "control points, detected screen-color calibration, and "
            "screen-coordinate profile spots are cleared. Fitted profile "
            "matrices are retained and become stale until refitted.");
    case Scope::Reconstruction:
        return QCoreApplication::translate(
            "ParameterPreset",
            "Screen collection/demosaic/denoise choices and sharpening "
            "algorithm/regularization controls. Measured or fitted MTF data "
            "and accepted spatial adaptive corrections are not changed.");
    case Scope::Color:
        return QCoreApplication::translate(
            "ParameterPreset",
            "White balance, viewing conditions, final saturation/brightness, "
            "tone curve, and output color settings. Historical dye identity "
            "and fitted profile matrices/spots are not changed.");
    }
    Q_UNREACHABLE();
}

QList<Record> records()
{
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(presetRoot));

    QList<Record> result;
    for (const QString &id : settings.childGroups()) {
        settings.beginGroup(id);
        const QString name = settings.value(QStringLiteral("name")).toString();
        const auto scope =
            scopeFromKey(settings.value(QStringLiteral("scope")).toString());
        const QByteArray payload =
            settings.value(QStringLiteral("payload")).toByteArray();
        const QVariantMap extras =
            settings.value(QStringLiteral("extras")).toMap();
        settings.endGroup();

        if (name.trimmed().isEmpty() || !scope || payload.isEmpty())
            continue;
        result.append({id, name, *scope, payload, extras});
    }
    settings.endGroup();

    std::sort(result.begin(), result.end(),
              [](const Record &a, const Record &b) {
                  const int scopeOrder =
                      static_cast<int>(a.scope) - static_cast<int>(b.scope);
                  if (scopeOrder != 0)
                      return scopeOrder < 0;
                  const int nameOrder =
                      QString::localeAwareCompare(a.name, b.name);
                  if (nameOrder != 0)
                      return nameOrder < 0;
                  return a.id < b.id;
              });
    return result;
}

bool save(const QString &name, Scope scope, const ParameterState &state,
          QString *error)
{
    const QString normalizedName = name.trimmed();
    if (normalizedName.isEmpty()) {
        if (error)
            *error = QCoreApplication::translate(
                "ParameterPreset", "Preset name cannot be empty.");
        return false;
    }

    // Persist only the declared reusable domain. Besides keeping QSettings
    // compact, this prevents unused image-specific calibration from being
    // retained inside a preset payload that would never apply it.
    ParameterState stored;
    switch (scope) {
    case Scope::Capture:
        applyCaptureScope(&stored, state);
        break;
    case Scope::Process:
        applyProcessScope(&stored, state);
        break;
    case Scope::Reconstruction:
        applyReconstructionScope(&stored, state);
        break;
    case Scope::Color:
        applyColorScope(&stored, state);
        break;
    }

    const QByteArray payload = serializePresetState(stored, error);
    if (payload.isEmpty())
        return false;

    QVariantMap extras;
    if (scope == Scope::Reconstruction) {
        extras.insert(QStringLiteral("demosaicedScaling"),
                      static_cast<int>(state.rparams.demosaiced_scaling));
    } else if (scope == Scope::Color) {
        extras.insert(QStringLiteral("observerWhitepointX"),
                      state.rparams.observer_whitepoint.x);
        extras.insert(QStringLiteral("observerWhitepointY"),
                      state.rparams.observer_whitepoint.y);
        extras.insert(QStringLiteral("outputProfile"),
                      static_cast<int>(state.rparams.output_profile));
        extras.insert(QStringLiteral("outputGamma"), state.rparams.output_gamma);
        extras.insert(QStringLiteral("gamutWarning"), state.rparams.gamut_warning);

        QVariantList controlPoints;
        for (const colorscreen::point_t &point :
             state.rparams.output_tone_curve_control_points) {
            QVariantList encodedPoint;
            encodedPoint.append(point.x);
            encodedPoint.append(point.y);
            controlPoints.append(QVariant(encodedPoint));
        }
        extras.insert(QStringLiteral("toneCurveControlPoints"), controlPoints);
    }

    QString id;
    for (const Record &record : records())
        if (record.scope == scope &&
            record.name.compare(normalizedName, Qt::CaseInsensitive) == 0) {
            id = record.id;
            break;
        }
    if (id.isEmpty())
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);

    QSettings settings;
    settings.beginGroup(QString::fromLatin1(presetRoot));
    settings.beginGroup(id);
    settings.setValue(QStringLiteral("name"), normalizedName);
    settings.setValue(QStringLiteral("scope"), scopeKey(scope));
    settings.setValue(QStringLiteral("payload"), payload);
    settings.setValue(QStringLiteral("extras"), extras);
    settings.setValue(QStringLiteral("applicationVersion"),
                      QCoreApplication::applicationVersion());
    settings.endGroup();
    settings.endGroup();
    settings.sync();

    if (settings.status() != QSettings::NoError) {
        if (error)
            *error = QCoreApplication::translate(
                "ParameterPreset", "Could not write application preset settings.");
        return false;
    }

    if (error)
        error->clear();
    return true;
}

bool remove(const QString &id)
{
    if (id.isEmpty())
        return false;

    QSettings settings;
    settings.beginGroup(QString::fromLatin1(presetRoot));
    const bool existed = settings.childGroups().contains(id);
    if (existed)
        settings.remove(id);
    settings.endGroup();
    settings.sync();
    return existed && settings.status() == QSettings::NoError;
}

bool decode(const Record &record, ParameterState *state, QString *error)
{
    return decodePresetState(record.payload, state, error);
}

bool apply(const Record &record, ParameterState *target,
           bool *clearedRegistration, QString *error)
{
    if (!target) {
        if (error)
            *error = QCoreApplication::translate(
                "ParameterPreset", "No target document state was supplied.");
        return false;
    }

    ParameterState source;
    if (!decode(record, &source, error))
        return false;

    bool registrationCleared = false;
    switch (record.scope) {
    case Scope::Capture:
        applyCaptureScope(target, source);
        break;
    case Scope::Process:
        registrationCleared = applyProcessScope(target, source);
        break;
    case Scope::Reconstruction:
        applyReconstructionScope(target, source);
        if (record.extras.contains(QStringLiteral("demosaicedScaling"))) {
            const int scaling =
                record.extras.value(QStringLiteral("demosaicedScaling")).toInt();
            if (scaling >= 0 &&
                scaling <
                    static_cast<int>(colorscreen::render_parameters::
                                         max_demosaiced_scaling))
                target->rparams.demosaiced_scaling =
                    static_cast<colorscreen::render_parameters::
                                    demosaiced_scaling_t>(scaling);
        }
        break;
    case Scope::Color:
        applyColorScope(target, source);
        if (record.extras.contains(QStringLiteral("observerWhitepointX")) &&
            record.extras.contains(QStringLiteral("observerWhitepointY"))) {
            target->rparams.observer_whitepoint = colorscreen::xy_t(
                record.extras.value(QStringLiteral("observerWhitepointX"))
                    .toDouble(),
                record.extras.value(QStringLiteral("observerWhitepointY"))
                    .toDouble());
        }
        if (record.extras.contains(QStringLiteral("outputProfile"))) {
            const int profile =
                record.extras.value(QStringLiteral("outputProfile")).toInt();
            if (profile >= 0 &&
                profile <
                    static_cast<int>(colorscreen::render_parameters::
                                         output_profile_max))
                target->rparams.output_profile =
                    static_cast<colorscreen::render_parameters::output_profile_t>(
                        profile);
        }
        if (record.extras.contains(QStringLiteral("outputGamma")))
            target->rparams.output_gamma =
                record.extras.value(QStringLiteral("outputGamma")).toDouble();
        if (record.extras.contains(QStringLiteral("gamutWarning")))
            target->rparams.gamut_warning =
                record.extras.value(QStringLiteral("gamutWarning")).toBool();

        if (record.extras.contains(QStringLiteral("toneCurveControlPoints"))) {
            std::vector<colorscreen::point_t> points;
            const QVariantList encoded =
                record.extras.value(QStringLiteral("toneCurveControlPoints"))
                    .toList();
            bool valid = !encoded.isEmpty();
            for (const QVariant &entry : encoded) {
                const QVariantList pair = entry.toList();
                if (pair.size() != 2) {
                    valid = false;
                    break;
                }
                const double x = pair[0].toDouble();
                const double y = pair[1].toDouble();
                if (!colorscreen::my_isfinite(x) ||
                    !colorscreen::my_isfinite(y)) {
                    valid = false;
                    break;
                }
                points.push_back({x, y});
            }
            if (valid)
                target->rparams.output_tone_curve_control_points =
                    std::move(points);
        }
        break;
    }

    if (clearedRegistration)
        *clearedRegistration = registrationCleared;
    if (error)
        error->clear();
    return true;
}

} // namespace qtgui_presets
