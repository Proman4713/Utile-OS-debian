#!/bin/bash
set -e # Exit on non-zero return value

SRC_DIR=$1
PACKAGE_NAME=$(basename $SRC_DIR)
PACKAGE_VERSION=$(cat $1/DEBIAN/control | grep Version | cut -d ' ' -f 2)

if ! [ -d "$SRC_DIR" ]; then
	echo "Directory $SRC_DIR doesn't exist"
	exit 1
fi

echo "Building $PACKAGE_NAME..."

echo "Fixing permissions..."
find "$SRC_DIR" -type d -exec chmod 755 {} +
find "$SRC_DIR" -type f -exec chmod 644 {} +

rm -f "$PACKAGE_NAME.deb"

# Fix permissions for Debian scripts
if [ -d "$SRC_DIR/DEBIAN" ]; then
    find "$SRC_DIR/DEBIAN" -type f -exec chmod 755 {} +
    # Bring 'control' and 'copyright' back to 644 as it doesn't need execution
    chmod 644 "$SRC_DIR/DEBIAN/control" 2>/dev/null || true
    chmod 644 "$SRC_DIR/DEBIAN/copyright" 2>/dev/null || true
fi

# Build the package
echo "Building $PACKAGE_NAME.deb version $PACKAGE_VERSION..."
fakeroot dpkg-deb --build "$SRC_DIR"

echo "Build complete"