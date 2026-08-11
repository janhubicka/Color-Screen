#!/usr/bin/env python3
"""High-precision validation of Color-Screen's physical radial MTF model.

This script is intentionally independent of the C++ implementation.  It uses
mpmath to integrate the defocused circular-pupil autocorrelation at high
precision, compares it with the composite 16-point Gauss--Legendre rule used by
mtf.C, and records the Hurley capture's unit conversions.
"""

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path

import mpmath as mp

NODES = (
    mp.mpf("0.095012509837637440185319335424958063"),
    mp.mpf("0.281603550779258913230460501460496106"),
    mp.mpf("0.458016777657227386342419442983577574"),
    mp.mpf("0.617876244402643748446671764048791019"),
    mp.mpf("0.755404408355003033895101194847442268"),
    mp.mpf("0.865631202387831743880467897712393132"),
    mp.mpf("0.944575023073232576077988415534608345"),
    mp.mpf("0.989400934991649932596154173450332627"),
)
WEIGHTS = (
    mp.mpf("0.189450610455068496285396723208283105"),
    mp.mpf("0.182603415044923588866763667969219939"),
    mp.mpf("0.169156519395002538189312079030359962"),
    mp.mpf("0.149595988816576732081501730547478549"),
    mp.mpf("0.124628971255533872052476282192016420"),
    mp.mpf("0.095158511682492784809925107602246226"),
    mp.mpf("0.062253523938647892862843836994377694"),
    mp.mpf("0.027152459411754094851780572456018104"),
)


def diffraction_otf(q: mp.mpf) -> mp.mpf:
    """Return the incoherent circular-pupil diffraction OTF at Q."""
    if q <= 0:
        return mp.mpf(1)
    if q >= 1:
        return mp.mpf(0)
    return 2 / mp.pi * (mp.acos(q) - q * mp.sqrt((1 - q) * (1 + q)))


def transformed_integral_reference(q: mp.mpf, edge_phase: mp.mpf) -> mp.mpf:
    """Return the transformed pupil-overlap integral at Q and EDGE_PHASE."""
    overlap = 1 - q
    integrand = lambda u: (
        u * u
        * mp.sqrt(2 - overlap * u * u)
        * mp.cos(edge_phase * (1 - u * u))
    )
    return mp.quad(integrand, [0, 1])


def defocus_factor_reference(q: mp.mpf, edge_phase: mp.mpf) -> mp.mpf:
    """Return exact signed defocus factor relative to in-focus diffraction."""
    if q <= 0 or q >= 1 or not edge_phase:
        return mp.mpf(1)
    numerator = transformed_integral_reference(q, edge_phase)
    denominator = transformed_integral_reference(q, mp.mpf(0))
    return numerator / denominator


def transformed_integral_gl16(q: mp.mpf, edge_phase: mp.mpf) -> mp.mpf:
    """Replicate mtf.C's composite 16-point Gauss--Legendre integral."""
    overlap = 1 - q
    phase = abs(edge_phase)
    panels = max(1, int(mp.ceil(phase / mp.pi)))
    total = mp.mpf(0)
    for panel in range(panels):
        left = mp.mpf(panel) / panels
        right = mp.mpf(panel + 1) / panels
        center = (left + right) / 2
        half_width = (right - left) / 2
        panel_sum = mp.mpf(0)
        for node, weight in zip(NODES, WEIGHTS):
            for sign in (-1, 1):
                u = center + sign * half_width * node
                u2 = u * u
                panel_sum += (
                    weight
                    * u2
                    * mp.sqrt(2 - overlap * u2)
                    * mp.cos(edge_phase * (1 - u2))
                )
        total += half_width * panel_sum
    return total


def defocus_factor_gl16(q: mp.mpf, edge_phase: mp.mpf) -> mp.mpf:
    """Return mtf.C's signed GL16 defocus factor."""
    if q <= 0 or q >= 1 or not edge_phase:
        return mp.mpf(1)
    return transformed_integral_gl16(q, edge_phase) / transformed_integral_gl16(
        q, mp.mpf(0)
    )


def stokseth_factor(edge_phase: mp.mpf) -> mp.mpf:
    """Return the historical 2*J1(Z)/Z approximation."""
    if not edge_phase:
        return mp.mpf(1)
    return 2 * mp.besselj(1, edge_phase) / edge_phase


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--csv", type=Path, help="write detailed comparison CSV")
    args = parser.parse_args()

    mp.mp.dps = 80

    scan_dpi = mp.mpf(1887)
    f_stop = mp.mpf(8)
    wavelength_nm = mp.mpf(750)
    pixel_pitch_um = mp.mpf("3.760")
    object_pitch_um = mp.mpf(25400) / scan_dpi
    magnification = pixel_pitch_um / object_pitch_um
    effective_f_stop = f_stop * (1 + magnification)
    wavelength_mm = wavelength_nm * mp.mpf("1e-6")
    cutoff_cycles_per_pixel = (
        pixel_pitch_um * mp.mpf("1e-3")
        / (wavelength_mm * effective_f_stop)
    )

    defocus_mm = mp.mpf("0.17939247226072069")
    q_values = [
        mp.mpf(0),
        mp.mpf("1e-8"),
        mp.mpf("0.001"),
        mp.mpf("0.01"),
        mp.mpf("0.05"),
        mp.mpf("0.1"),
        mp.mpf("0.25"),
        mp.mpf("0.5"),
        mp.mpf("0.75"),
        mp.mpf("0.9"),
        mp.mpf("0.99"),
        mp.mpf("0.999999"),
        mp.mpf(1),
    ]
    defocus_values = [
        mp.mpf(0),
        mp.mpf("0.001"),
        mp.mpf("0.01"),
        defocus_mm,
        mp.mpf("0.5"),
        mp.mpf("2.0"),
        mp.mpf("20.0"),
    ]

    rows = []
    maximum_gl16_error = mp.mpf(0)
    maximum_stokseth_error = mp.mpf(0)
    maximum_bound_error = mp.mpf(0)
    for displacement in defocus_values:
        for q in q_values:
            edge_phase = (
                mp.pi
                * displacement
                * q
                * (1 - q)
                / (wavelength_mm * effective_f_stop * effective_f_stop)
            )
            exact_factor = defocus_factor_reference(q, edge_phase)
            gl16_factor = defocus_factor_gl16(q, edge_phase)
            approximation = stokseth_factor(edge_phase)
            gl16_error = abs(gl16_factor - exact_factor)
            stokseth_error = abs(approximation - exact_factor)
            maximum_gl16_error = max(maximum_gl16_error, gl16_error)
            maximum_stokseth_error = max(maximum_stokseth_error, stokseth_error)

            diffraction = diffraction_otf(q)
            exact_otf = diffraction * exact_factor
            maximum_bound_error = max(
                maximum_bound_error, max(mp.mpf(0), abs(exact_otf) - diffraction)
            )
            rows.append(
                (
                    float(displacement),
                    float(q),
                    float(edge_phase),
                    float(diffraction),
                    float(exact_factor),
                    float(gl16_factor),
                    float(gl16_error),
                    float(approximation),
                    float(stokseth_error),
                )
            )

    if args.csv:
        with args.csv.open("w", newline="") as output:
            writer = csv.writer(output)
            writer.writerow(
                (
                    "defocus_mm",
                    "normalized_frequency",
                    "edge_phase_rad",
                    "diffraction_otf",
                    "reference_defocus_factor",
                    "gl16_defocus_factor",
                    "gl16_absolute_error",
                    "stokseth_factor",
                    "stokseth_absolute_error",
                )
            )
            writer.writerows(rows)

    print(f"object pixel pitch:       {mp.nstr(object_pitch_um, 18)} um")
    print(f"magnification:            {mp.nstr(magnification, 18)}")
    print(f"effective f-number:       {mp.nstr(effective_f_stop, 18)}")
    print(f"diffraction cutoff:       {mp.nstr(cutoff_cycles_per_pixel, 18)} cycles/pixel")
    print(f"maximum GL16 error:       {mp.nstr(maximum_gl16_error, 8)}")
    print(f"maximum Stokseth error:   {mp.nstr(maximum_stokseth_error, 8)}")
    print(f"maximum OTF bound error:  {mp.nstr(maximum_bound_error, 8)}")

    return int(maximum_gl16_error > mp.mpf("5e-13") or maximum_bound_error > mp.mpf("1e-70"))


if __name__ == "__main__":
    raise SystemExit(main())
