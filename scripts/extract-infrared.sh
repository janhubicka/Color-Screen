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

    # Identify channel count and resolution info
    # %[channels] returns something like 'srgb 4.1' or 'srgba 4.0'
    CHANNEL_SPEC=$($IM_IDENTIFY -format "%[channels]" "$img" 2>/dev/null || echo "")
    RES_INFO=$($IM_IDENTIFY -format "-units %U -density %x" "$img" 2>/dev/null || echo "")
    
    # Calculate total channel count (e.g., 4.1 -> 5, 4.0 -> 4)
    if [[ "$CHANNEL_SPEC" =~ ([0-9]+)\.([0-9]+)$ ]]; then
        NUM_CHANS=$((${BASH_REMATCH[1]} + ${BASH_REMATCH[2]}))
    else
        NUM_CHANS=0
    fi
    
    if [ "$NUM_CHANS" -le 3 ]; then
        echo "Error: '$img' has no alpha channel or extra channels (only $NUM_CHANS channels found). Failing." >&2
        exit 1
    fi

    echo "Extracting infrared from '$img' to '$output_file' (optimized pipe)..."
    
    # Use PAM pipe to robustly separate and extract the last channel (index 3 or higher).
    # This strips persistent TIFF meta-channel tags while avoiding disk I/O.
    $IM_CONVERT "$img" -separate -delete 0--2 PAM:- | \
        $IM_CONVERT PAM:- $RES_INFO -alpha off -set colorspace Gray -type Grayscale "$output_file"
done

echo "All extractions completed successfully."
