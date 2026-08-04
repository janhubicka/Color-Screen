#!/bin/bash
# generate-slanted-edge-tests.sh -- Generate synthetic slanted-edge test images
#
# This script uses mtf_generate_rectangle.exe from MTF Mapper (via Wine) to
# create test images with known MTF characteristics.  The generated images
# are pre-committed to the repository so Wine/MTF Mapper is NOT required
# to run the testsuite.
#
# Only run this script to regenerate the reference images if needed.
#
# MTF Mapper: https://sourceforge.net/projects/mtfmapper/
# Installed at: ~/.wine/drive_c/Program Files/MTF Mapper/bin/

set -e

mtfgen() {
    wine "$HOME/.wine/drive_c/Program Files/MTF Mapper/bin/mtf_generate_rectangle.exe" "$@"
}

OUTDIR="$(dirname "$0")"
TMPDIR="$(mktemp -d)"
trap "rm -rf $TMPDIR" EXIT

# Common parameters: 5-degree angle, 800-pixel dimension, 16-bit linear, no noise
COMMON="-a 5 -d 800 --b16 --linear -n 0"

generate_edge() {
    local name="$1"
    shift
    local extra="$@"

    echo "=== Generating $name ==="

    # Generate the full rectangle image
    mtfgen $COMMON $extra -o "$TMPDIR/${name}-full.tif" 2>&1 | \
        grep -v "^[0-9a-f]*:" | tee "$TMPDIR/${name}.log"

    # Find image dimensions
    local dim=$(identify "$TMPDIR/${name}-full.tif" 2>/dev/null | grep -oP '\d+x\d+' | head -1)
    local w=$(echo $dim | cut -dx -f1)
    local h=$(echo $dim | cut -dx -f2)
    echo "  Full image: ${w}x${h}"

    # Find the left vertical edge location (transition in middle row)
    local edge_x=$(python3 -c "
from PIL import Image
import numpy as np
img = np.array(Image.open('$TMPDIR/${name}-full.tif'))
row = img[$((h/2)), :]
diffs = np.abs(np.diff(row.astype(float)))
transitions = np.where(diffs > 5000)[0]
if len(transitions) > 0:
    print(transitions[0])
else:
    print($((w/4)))
")
    echo "  Left edge at x=$edge_x"

    # Crop region: 200px wide centered on edge, middle 60% of height
    # (avoids top/bottom rectangle edges).
    # Offset 6 pixels down to avoid subpixel alignment artifacts.
    local crop_x=$((edge_x - 100))
    local crop_y=$((h * 20 / 100 + 6))
    local crop_h=$((h * 60 / 100))
    if [ $crop_x -lt 0 ]; then crop_x=0; fi

    echo "  Cropping: 200x${crop_h}+${crop_x}+${crop_y}"
    magick "$TMPDIR/${name}-full.tif" -crop "200x${crop_h}+${crop_x}+${crop_y}" \
        +repage "$OUTDIR/slanted-${name}.tif"

    # Extract the analytical MTF formula from mtfmapper output
    local mtf_formula=$(grep "MTF curve:" "$TMPDIR/${name}.log" | head -1 | sed 's/.*MTF curve:  //')
    local mtf50=$(grep "MTF50 = " "$TMPDIR/${name}.log" | head -1 | sed 's/.*MTF50 = //')
    echo "  MTF curve: $mtf_formula"
    echo "  MTF50: $mtf50"

    # Generate reference MTF file from mtfmapper's analytical formula.
    # For Gaussian PSF sigma=s: MTF(f) = exp(-2*pi^2*s^2*f^2)
    # The coefficient is reported in the formula as exp(-K*x*x).
    python3 -c "
import numpy as np
import re

formula = '''$mtf_formula'''

# Parse the Gaussian coefficient from exp(-K*x*x) or Airy formula
# For Gaussian: exp(-K*x*x)
m = re.match(r'exp\((-[\d.]+)\*x\*x\)', formula)
if m:
    K = float(m.group(1))
    # MTF(f) = exp(K * f^2)  (K is negative)
    freqs = np.arange(0, 0.501, 0.0025)
    mtf_vals = np.exp(K * freqs**2) * 100.0
else:
    # Airy disk: 2/pi * (acos(x*k) - (x*k)*sqrt(1-(x*k)^2))
    m2 = re.search(r'acos\(x\*([\d.]+)\)', formula)
    if m2:
        k = float(m2.group(1))
        freqs = np.arange(0, 0.501, 0.0025)
        mtf_vals = np.zeros_like(freqs)
        for i, f in enumerate(freqs):
            u = f * k
            if u >= 1.0:
                mtf_vals[i] = 0.0
            else:
                mtf_vals[i] = (2.0/np.pi) * (np.arccos(u) - u * np.sqrt(1 - u*u)) * 100.0
    else:
        print(f'ERROR: cannot parse formula: {formula}')
        exit(1)

with open('$OUTDIR/slanted-${name}-mtf.txt', 'w') as f:
    for freq, val in zip(freqs, mtf_vals):
        f.write(f'{freq:.3f}\t{val:.3f}\t{val:.3f}\t{val:.3f}\t{val:.3f}\n')
print('  Reference MTF written (' + str(len(freqs)) + ' points, 0..0.5 cy/px)')
"
    echo ""
}

echo "Generating slanted-edge test images for Color-Screen testsuite"
echo "=============================================================="
echo ""
echo "Test images simulate lenses used for high-resolution (4000 PPI)"
echo "macro captures with PhaseOne cameras."
echo ""

# Test 1: Gaussian blur, sigma=0.8 px
# Good quality macro lens at optimal aperture.
# MTF50 ~ 0.375 cy/px.  Realistic for Schneider 120mm Makro at f/5.6.
generate_edge "gaussian-sharp" -p gaussian -b 0.8

# Test 2: Gaussian blur, sigma=1.2 px
# Average macro lens or good lens slightly stopped down.
# MTF50 ~ 0.25 cy/px.  Typical for 4000 PPI PhaseOne workflow.
generate_edge "gaussian-medium" -p gaussian -b 1.2

# Test 3: Gaussian blur, sigma=2.0 px
# Soft lens or significant defocus / diffraction at small aperture.
# MTF50 ~ 0.094 cy/px.  Worst-case scenario.
generate_edge "gaussian-soft" -p gaussian -b 2.0

# Test 4: Gaussian blur, sigma=0.5 px
# Near-perfect optics, stress test for high MTF50 detection.
# MTF50 ~ 0.375 cy/px.  Ensures we don't clip sharp lenses.
generate_edge "gaussian-vsharp" -p gaussian -b 0.5

echo "Done. Generated test images in $OUTDIR"
echo ""
echo "Summary of known MTF50 values (analytical, from mtfmapper):"
echo "  gaussian-sharp  (sigma=0.8): MTF50 ~ 0.234 cy/px"
echo "  gaussian-medium (sigma=1.2): MTF50 ~ 0.156 cy/px"
echo "  gaussian-soft   (sigma=2.0): MTF50 ~ 0.094 cy/px"
echo "  gaussian-vsharp (sigma=0.5): MTF50 ~ 0.375 cy/px"
