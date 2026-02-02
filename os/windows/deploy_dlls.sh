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

# Optimized DLL deployment with work-queue
echo "Starting work-queue deployment..."

# Track scanned files to ensure O(N) complexity
declare -A scanned

# Initial work queue: all binaries currently in TARGET_DIR
# Using find to populate the initial list
work_queue=()
while IFS= read -r -d $'\0' file; do
    work_queue+=("$file")
done < <(find "$TARGET_DIR_UNIX" -maxdepth 1 \( -name "*.exe" -o -name "*.dll" \) -print0)

queue_index=0
echo "Initial queue size: ${#work_queue[@]}"

while [ $queue_index -lt ${#work_queue[@]} ]; do
    bin="${work_queue[$queue_index]}"
    queue_index=$((queue_index + 1))
    
    bin_base=$(basename "$bin")
    
    # Skip if already scanned
    [ -n "${scanned[$bin_base]}" ] && continue
    scanned[$bin_base]=1
    
    echo "  Scanning [$queue_index/${#work_queue[@]}] $bin_base..."
    
    # Process dependencies and copy new ones
    # Process dependencies and copy new ones
    # Use objdump to find direct dependencies statically (avoids execution/hangs)
    # Filter only for DLL names
    deps=$(objdump -p "$bin" | grep "DLL Name: " | awk '{print $NF}' | tr -d '\r')
    
    for dep_name in $deps; do
        # Check if this DLL exists in the source prefix (we only care about deploying those)
        # In MinGW/MSYS2, DLLs are typically in $PREFIX/bin
        dep_path="$PREFIX/bin/$dep_name"
        
        if [ -f "$dep_path" ]; then
            # If not in target dir, copy it and add to work queue
            if [ ! -f "$TARGET_DIR_UNIX/$dep_name" ]; then
                echo "    + Copying $dep_name"
                if cp "$dep_path" "$TARGET_DIR_UNIX/"; then
                    work_queue+=("$TARGET_DIR_UNIX/$dep_name")
                else
                    echo "    FAILED to copy $dep_name from $dep_path"
                fi
            fi
        fi
    done
done

echo "DLL deployment complete. Scanned ${#scanned[@]} unique binaries."
