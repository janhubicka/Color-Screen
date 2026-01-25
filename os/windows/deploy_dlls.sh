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

# Multi-pass recursive deployment
while true; do
    added_any=0
    # Collect all binaries to check (EXE and DLL)
    # Use find to get full paths
    binaries=$(find "$TARGET_DIR_UNIX" -maxdepth 1 -name "*.exe" -o -name "*.dll")
    
    for bin in $binaries; do
        # echo "Checking $bin..."
        # Extract dependencies that reside in the MSYS2 prefix
        # ldd output: "  libcurl-4.dll => /mingw64/bin/libcurl-4.dll (0x0...)"
        # We use sed to extract the path between '=> ' and ' ('
        ldd "$bin" | grep " => $PREFIX" | sed -e 's/.* => \(.*\) (0x.*/\1/' | while read -r dep_path; do
            if [ -n "$dep_path" ]; then
                dep_name=$(basename "$dep_path")
                if [ ! -f "$TARGET_DIR_UNIX/$dep_name" ]; then
                    echo "  Copying $dep_name (requested by $(basename "$bin"))"
                    cp "$dep_path" "$TARGET_DIR_UNIX/"
                    added_any=$((added_any + 1))
                fi
            fi
        done
        # Note: the pipe to while loop runs in a subshell, so added_any update won't persist
        # We need a different way to track adding.
    done
    
    # Check if any DLLs were actually added in this pass
    # Let's count files in TARGET_DIR before and after
    # Or use a temporary file to signal
    
    # Better approach for the loop:
    # Just run it a few times or use a more clever bash way.
    # Actually, let's use a temporary file to communicate count.
    
    added_count=$(find "$TARGET_DIR_UNIX" -maxdepth 1 -name "*.dll" | wc -l)
    # ... logic above would need to be rewritten to avoid subshell issue ...
    break # placeholder for now, let's refine
done

# Refined version without subshell issue
echo "Starting deployment loop..."
while true; do
    count_before=$(find "$TARGET_DIR_UNIX" -maxdepth 1 -name "*.dll" -o -name "*.exe" | wc -l)
    
    # Process all binaries
    for bin in $(find "$TARGET_DIR_UNIX" -maxdepth 1 -name "*.exe" -o -name "*.dll"); do
        # Extract deps and copy them
        # We use a process substitution to avoid subshell for the 'added' variable if we used one
        while read -r dep_path; do
            if [ -n "$dep_path" ] && [ -f "$dep_path" ]; then
                dep_name=$(basename "$dep_path")
                if [ ! -f "$TARGET_DIR_UNIX/$dep_name" ]; then
                    echo "  Adding $dep_name (dependency of $(basename "$bin"))"
                    cp "$dep_path" "$TARGET_DIR_UNIX/"
                fi
            fi
        done < <(ldd "$bin" | grep " => $PREFIX" | sed -e 's/.* => \(.*\) (0x.*/\1/')
    done
    
    count_after=$(find "$TARGET_DIR_UNIX" -maxdepth 1 -name "*.dll" -o -name "*.exe" | wc -l)
    if [ "$count_after" -eq "$count_before" ]; then
        break
    fi
    echo "  Added $((count_after - count_before)) files, scanning again..."
done

echo "DLL deployment complete."
