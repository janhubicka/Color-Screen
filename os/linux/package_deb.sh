#!/bin/bash
set -e

# Usage: ./package_deb.sh <DESTDIR> <VERSION> <ARCH>

DESTDIR=$1
VERSION=$2
ARCH=$3

if [ -z "$DESTDIR" ] || [ -z "$VERSION" ] || [ -z "$ARCH" ]; then
    echo "Usage: $0 <DESTDIR> <VERSION> <ARCH>"
    exit 1
fi

echo "Packaging for $ARCH version $VERSION..."

# Create DEBIAN directory
mkdir -p "$DESTDIR/DEBIAN"

# Copy control file
if [ -f "os/linux/control" ]; then
    cp "os/linux/control" "$DESTDIR/DEBIAN/control"
else
    echo "Error: os/linux/control not found!"
    exit 1
fi

# Update Architecture and Version in control file
sed -i "s/Architecture: .*/Architecture: $ARCH/" "$DESTDIR/DEBIAN/control"
sed -i "s/Version: .*/Version: $VERSION/" "$DESTDIR/DEBIAN/control"

# Calculate Installed-Size
INSTALLED_SIZE=$(du -s "$DESTDIR/usr" | cut -f1)
echo "Installed-Size: $INSTALLED_SIZE" >> "$DESTDIR/DEBIAN/control"

# Build the package
DEB_NAME="colorscreen_${VERSION}_${ARCH}.deb"
dpkg-deb --build "$DESTDIR" "$DEB_NAME"

echo "Package created: $DEB_NAME"
