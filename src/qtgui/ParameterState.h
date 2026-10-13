#ifndef PARAMETER_STATE_H
#define PARAMETER_STATE_H

#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/scr-to-img-parameters.h"
#include "../libcolorscreen/include/scr-detect-parameters.h"
#include "../libcolorscreen/include/solver-parameters.h"
#include "../libcolorscreen/include/colorscreen.h"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace colorscreen {

/** Return true when S contains only trailing whitespace. */
inline bool qt_parameter_metadata_only_whitespace(const char *s) {
    if (!s)
        return true;
    while (*s) {
        if (!std::isspace(static_cast<unsigned char>(*s)))
            return false;
        ++s;
    }
    return true;
}

/** Save the core CSP payload followed by Qt-only document metadata.

    save_csp() terminates the portable payload with screen_alignment_end and
    load_csp() deliberately stops at that marker.  Appending the GUI metadata
    afterwards therefore keeps files readable by existing library/CLI clients
    while allowing the Qt editor to preserve state that is meaningful only to
    its document workflow. */
inline bool save_csp_with_profile_spots(
    FILE *f, const scr_to_img_parameters *param,
    const scr_detect_parameters *dparam, const render_parameters *rparam,
    const solver_parameters *sparam,
    const std::vector<point_t> &profileSpots) {
    if (!save_csp(f, param, dparam, rparam, sparam))
        return false;
    if (std::fprintf(f, "colorscreen_qt_metadata_version: 1\n") < 0)
        return false;
    for (const point_t &spot : profileSpots) {
        if (std::fprintf(f, "profile_spot: %.17g %.17g\n", spot.x, spot.y) < 0)
            return false;
    }
    return std::fprintf(f, "colorscreen_qt_metadata_end\n") >= 0;
}

/** Load the portable CSP payload and any Qt metadata postamble.

    Old parameter files have no postamble; in that case PROFILESPOTS is
    intentionally cleared so loading a new .par file cannot retain profiling
    points from the previously edited document state.  PROFILESPOTRESULTS are
    derived optimizer output rather than persistent document state, so every
    successful parameter load invalidates them.  GUI-only state is committed
    only after the complete postamble has parsed successfully, matching the
    transactional restore-on-error behaviour of MainWindow::loadParameterFile.
    Unknown trailing data is refused: accepting it would silently lose it
    on a later native JSON Save. */
inline bool load_csp_with_profile_spots(
    FILE *f, scr_to_img_parameters *param, scr_detect_parameters *dparam,
    render_parameters *rparam, solver_parameters *sparam, const char **error,
    std::vector<point_t> *profileSpots,
    std::vector<color_match> *profileSpotResults = nullptr) {
    if (!load_csp(f, param, dparam, rparam, sparam, error))
        return false;

    char line[256];
    bool metadataStarted = false;
    bool metadataEnded = false;
    std::vector<point_t> loadedSpots;

    while (std::fgets(line, sizeof(line), f)) {
        char *text = line;
        while (*text && std::isspace(static_cast<unsigned char>(*text)))
            ++text;
        if (!*text)
            continue;

        if (!metadataStarted) {
            int version = 0;
            int consumed = 0;
            if (std::sscanf(text, "colorscreen_qt_metadata_version: %d %n",
                            &version, &consumed) != 1 ||
                !qt_parameter_metadata_only_whitespace(text + consumed)) {
                if (error)
                    *error = "cannot import unrecognized trailing parameter metadata";
                return false;
            }
            if (version != 1) {
                if (error)
                    *error = "unsupported Qt parameter metadata version";
                return false;
            }
            metadataStarted = true;
            continue;
        }

        static const char endMarker[] = "colorscreen_qt_metadata_end";
        if (std::strncmp(text, endMarker, sizeof(endMarker) - 1) == 0 &&
            qt_parameter_metadata_only_whitespace(text + sizeof(endMarker) - 1)) {
            metadataEnded = true;
            break;
        }

        double x = 0;
        double y = 0;
        int consumed = 0;
        if (std::sscanf(text, "profile_spot: %lf %lf %n", &x, &y, &consumed) == 2 &&
            qt_parameter_metadata_only_whitespace(text + consumed) &&
            my_isfinite(x) && my_isfinite(y)) {
            loadedSpots.push_back({x, y});
            continue;
        }

        if (error)
            *error = "invalid Qt parameter metadata";
        return false;
    }

    if (metadataStarted && !metadataEnded) {
        if (error)
            *error = "truncated Qt parameter metadata";
        return false;
    }

    // A recognized prefix must not hide a second, unsupported extension.
    while (std::fgets(line, sizeof(line), f)) {
        if (!qt_parameter_metadata_only_whitespace(line)) {
            if (error)
                *error = "unexpected data after Qt parameter metadata end";
            return false;
        }
    }
    if (std::ferror(f)) {
        if (error)
            *error = "error reading parameter metadata";
        return false;
    }

    if (profileSpots)
        *profileSpots = std::move(loadedSpots);
    if (profileSpotResults)
        profileSpotResults->clear();
    return true;
}

} // namespace colorscreen

struct ParameterState {
    colorscreen::render_parameters rparams;
    colorscreen::scr_to_img_parameters scrToImg;
    colorscreen::scr_detect_parameters detect;
    colorscreen::solver_parameters solver;
    std::vector<colorscreen::point_t> profileSpots; // screen coords

    bool operator==(const ParameterState &other) const;
    bool operator!=(const ParameterState &other) const { return !(*this == other); }
};

#endif // PARAMETER_STATE_H
