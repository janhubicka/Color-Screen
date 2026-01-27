#!/bin/bash

# extract-infrared.sh
# Extracts the alpha channel (infrared) from a list of images using ImageMagick.
# Fails if an image does not have an alpha channel.
# Generates name-ir.ext for each input name.ext.

# Function to display usage
usage() {
    echo "Usage: $0 image1.ext [image2.ext ...]"
    exit 1
}

# Check for at least one argument
if [ $# -eq 0 ]; then
    usage
fi

# Determine ImageMagick command (magick for v7+, convert/identify for v6)
if command -v magick >/dev/null 2>&1; then
    IM_CONVERT="magick"
    IM_IDENTIFY="magick identify"
elif command -v convert >/dev/null 2>&1; then
    IM_CONVERT="convert"
    IM_IDENTIFY="identify"
else
    echo "Error: ImageMagick (magick or convert) not found." >&2
    exit 1
fi

set -e

for img in "$@"; do
    if [ ! -f "$img" ]; then
        echo "Error: File '$img' not found. Skipping." >&2
        continue
    fi

    # Check for alpha channel or extra channels
    # %[channels] returns 'srgb  3.0' (no alpha, no extra)
    # %[channels] returns 'srgba 4.0' (alpha, no extra)
    # %[channels] returns 'srgb  4.1' (no alpha, 1 extra)
    # %[channels] returns 'srgba 5.1' (alpha, 1 extra)
    
    CHANNELS=$($IM_IDENTIFY -format "%[channels]" "$img" 2>/dev/null || echo "")

    METHOD=""
    EXTRA_CHANNEL_INDEX=""
    
    if [[ "$CHANNELS" =~ srgba ]]; then
        METHOD="alpha"
    elif [[ "$CHANNELS" =~ \.?([1-9][0-9]*)$ ]]; then
        # Extract the number after the dot or at the end
        EXTRA_CHANNELS="${BASH_REMATCH[1]}"
        if [ "$EXTRA_CHANNELS" -gt 0 ]; then
            METHOD="extra"
            # In IM, channels are 0-indexed. srgb is 0,1,2. So first extra is 3.
            EXTRA_CHANNEL_INDEX=3
        fi
    fi

    # Fallback for older ImageMagick or cases where %[channels] is less descriptive
    if [ -z "$METHOD" ]; then
        HAS_ALPHA=$($IM_IDENTIFY -format "%A" "$img" 2>/dev/null || echo "False")
        if [[ "$HAS_ALPHA" == "True" ]]; then
            METHOD="alpha"
        fi
    fi

    if [ -z "$METHOD" ]; then
        echo "Error: '$img' has no alpha channel or extra channels. Failing." >&2
        exit 1
    fi

    # Extract directory, filename, and extension
    dirname=$(dirname -- "$img")
    filename=$(basename -- "$img")
    
    # Split filename and extension
    if [[ "$filename" == *.* ]]; then
        filename_no_ext="${filename%.*}"
        extension="${filename##*.}"
        output_file="$dirname/${filename_no_ext}-ir.${extension}"
    else
        output_file="$dirname/${filename}-ir"
    fi

    echo "Extracting infrared from '$img' to '$output_file' (method: $METHOD)..."
    if [ "$METHOD" == "alpha" ]; then
        $IM_CONVERT "$img" -alpha extract "$output_file"
    else
        $IM_CONVERT "$img" -channel "$EXTRA_CHANNEL_INDEX" -separate "$output_file"
    fi
done

echo "All extractions completed successfully."
