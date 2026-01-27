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

    echo "Extracting infrared from '$img' to '$output_file'..."
    
    # Create a temporary directory for channel separation
    TEMP_DIR=$(mktemp -d)
    
    # Separate all channels into PNG files (robustly strips TIFF metadata)
    $IM_CONVERT "$img" -separate +adjoin "$TEMP_DIR/chan_%d.png"
    
    # Count channels
    NUM_CHANS=$(ls "$TEMP_DIR/chan_"*.png 2>/dev/null | wc -l)
    
    if [ "$NUM_CHANS" -le 3 ]; then
        echo "Error: '$img' has no alpha channel or extra channels (only $NUM_CHANS channels found). Failing." >&2
        rm -rf "$TEMP_DIR"
        exit 1
    fi

    # Identify the last channel (usually Alpha or Infrared)
    # Using sort -V for natural sorting of indices
    LAST_CHAN_PNG=$(ls "$TEMP_DIR/chan_"*.png 2>/dev/null | sort -V | tail -n 1)
    
    if [ -z "$LAST_CHAN_PNG" ]; then
        echo "Error: Failed to extract channels from '$img'." >&2
        rm -rf "$TEMP_DIR"
        exit 1
    fi

    # Convert the last channel to the final 1-channel grayscale output
    $IM_CONVERT "$LAST_CHAN_PNG" -alpha off -set colorspace Gray -type Grayscale "$output_file"
    
    # Clean up
    rm -rf "$TEMP_DIR"
done

echo "All extractions completed successfully."
