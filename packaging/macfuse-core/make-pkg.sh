#!/bin/sh
#
# Copyright (C) 2006 Google. All Rights Reserved.
#
# Creates the "MacFUSE Core.pkg"  

MACFUSE_VERSION=$1
BUILD_DIR="/tmp/macfuse-core-$MACFUSE_VERSION"

PACKAGEMAKER="/Developer/Tools/packagemaker"

OUTPUT_PACKAGE="$BUILD_DIR/MacFUSE Core.pkg"
SRC_TARBALL="$BUILD_DIR/macfuse-core-$MACFUSE_VERSION.tar.bz2"

DISTRIBUTION_FOLDER="$BUILD_DIR/Distribution_folder"
INSTALL_RESOURCES="./Install_resources"
INFO_PLIST_IN="Info.plist.in"
INFO_PLIST_OUT="${BUILD_DIR}/Info.plist"
DESCRIPTION_PLIST="./Description.plist"
UNINSTALL_SCRIPT="./uninstall-core.sh"

# Make sure they gave a version!
if [ x"$MACFUSE_VERSION" = x"" ]
then
  echo "Usage: make-pkg.sh <version>"
  exit 1
fi

# Check input sources
if [ ! -d "$BUILD_DIR" ]
then
  echo "Build dir '$BUILD_DIR' does not exist."
  exit 1
fi
if [ ! -f "$SRC_TARBALL" ]
then
  echo "Unable to find src tar: '$SRC_TARBALL'"
  exit 1
fi
if [ ! -d "$INSTALL_RESOURCES" ]
then
  echo "Unable to find install resources dir: '$INSTALL_RESOURCES'"
  exit 1
fi
if [ ! -f "$INFO_PLIST_IN" ]
then
  echo "Unable to find Info.plist: '$INFO_PLIST_IN'"
  exit 1
fi
if [ ! -f "$DESCRIPTION_PLIST" ]
then
  echo "Unable to find Description.plist: '$DESCRIPTION_PLIST'"
  exit 1
fi
if [ ! -f "$UNINSTALL_SCRIPT" ]
then
  echo "Unable to find Description.plist: '$UNINSTALL_SCRIPT'"
  exit 1
fi

# Remove any previous runs
sudo rm -rf "$DISTRIBUTION_FOLDER"
sudo rm -rf "$OUTPUT_PACKAGE"
sudo rm -f "$INFO_PLIST_OUT"

# Create the distribution folder
mkdir $DISTRIBUTION_FOLDER
sudo tar -C $DISTRIBUTION_FOLDER -jxvpf $SRC_TARBALL 
if [ $? -ne 0 ]
then
  echo "Unable to untar!"
  exit 1
fi

# Copy the uninstall script
UNINSTALL_DST="$DISTRIBUTION_FOLDER/System/Library/Filesystems/fusefs.fs/uninstall-macfuse-core.sh"
sudo cp "$UNINSTALL_SCRIPT" "$UNINSTALL_DST"
sudo chmod 755 "$UNINSTALL_DST"
sudo chown root:wheel "$UNINSTALL_DST"

# Fix up the Info.plist
sed -e "s/MACFUSE_VERSION_LITERAL/$MACFUSE_VERSION/g" < "$INFO_PLIST_IN" > "$INFO_PLIST_OUT"

sudo $PACKAGEMAKER -build -p "$OUTPUT_PACKAGE" -f "$DISTRIBUTION_FOLDER" -b /tmp -ds -v \
                   -r "$INSTALL_RESOURCES" -i "$INFO_PLIST_OUT" -d "$DESCRIPTION_PLIST"
if [ $? -eq 0 ]
then
  sudo chown -R root:admin "$OUTPUT_PACKAGE"
fi
