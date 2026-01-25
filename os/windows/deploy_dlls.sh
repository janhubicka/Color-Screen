#!/bin/bash
# Usage: ./deploy_dlls.sh <target_dir> <prefix_to_search>
# Example: ./deploy_dlls.sh /c/Color-Screen-install/bin /mingw64

TARGET_DIR="$1"
PREFIX="$2"

if [ -z "$TARGET_DIR" ] || [ -z "$PREFIX" ]; then
    echo "Usage: $0 <target_dir> <prefix_to_search>"
    exit 1
fi

echo "Starting robust DLL deployment to $TARGET_DIR"
echo "Searching for dependencies in $PREFIX"

# Convert to absolute path if needed
# We use cygpath -u to be safe with Windows-style paths if they sneak in
TARGET_DIR_UNIX=$(cygpath -u "$TARGET_DIR")

if [ ! -d "$TARGET_DIR_UNIX" ]; then
    echo "Error: Target directory $TARGET_DIR_UNIX does not exist."
    exit 1
fi

# Robust multi-pass recursive deployment
MAX_PASSES=15
pass=0

echo "Starting deployment passes (max $MAX_PASSES)..."
while [ $pass -lt $MAX_PASSES ]; do
    pass=$((pass + 1))
    echo "Pass $pass..."
    added_in_pass=0
    
    # Collect current binaries to check
    # We use a temporary file to store the list of binaries to avoid subshell issues with loops
    bin_list=$(mktemp)
    find "$TARGET_DIR_UNIX" -maxdepth 1 \( -name "*.exe" -o -name "*.dll" \) > "$bin_list"
    
    while read -r bin; do
        [ -z "$bin" ] && continue
        
        # Extract dependencies that reside in the prefix
        # We use process substitution to keep 'added_in_pass' in the current shell
        while read -r dep_path; do
            if [ -n "$dep_path" ] && [ -f "$dep_path" ]; then
                dep_name=$(basename "$dep_path")
                if [ ! -f "$TARGET_DIR_UNIX/$dep_name" ]; then
                    echo "  Copying $dep_name (requested by $(basename "$bin"))"
                    cp "$dep_path" "$TARGET_DIR_UNIX/"
                    added_in_pass=$((added_in_pass + 1))
                fi
            fi
        done < <(ldd "$bin" 2>/dev/null | grep " => $PREFIX" | sed -e 's/.* => \(.*\) (0x.*/\1/')
    done < "$bin_list"
    
    rm -f "$bin_list"
    
    if [ "$added_in_pass" -eq 0 ]; then
        echo "No more dependencies added. Deployment stabilized."
        break
    fi
    echo "  Added $added_in_pass files this pass."
done

if [ $pass -eq $MAX_PASSES ]; then
    echo "Warning: Reached maximum number of passes ($MAX_PASSES). There might be circular dependencies or other issues."
fi

echo "DLL deployment complete."
