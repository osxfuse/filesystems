#! /bin/zsh
# Copyright (C) 2006-2007 Amit Singh. All Rights Reserved.
# 

MACFUSE_BUNDLE="com.google.filesystems.fusefs"
MACFUSE_CONFIG="Debug"
MACFUSE_KEXT="fusefs.kext"
MACFUSE_SRCDIR="/work/macfuse/local/fusefs"
MACFUSE_TMPDIR="/tmp"

PATH=/sbin:/usr/sbin:/usr/local/sbin:/bin:/usr/bin:/usr/local/bin:/Developer/Tools:/Developer/Applications:

function exit_on_error
{
    if [ "$?" -ne "0" ]
    then
        echo "*** $1" 1>&2
        exit 1
    fi
}

sudo kextstat -l -b "$MACFUSE_BUNDLE" | grep "$MACFUSE_BUNDLE" >/dev/null
if [ "$?" -eq "0" ]
then
    sudo kextunload -v -b "$MACFUSE_BUNDLE"
    exit_on_error "Failed to unload MacFUSE kernel extension"
fi

cd "$MACFUSE_SRCDIR"
exit_on_error "Failed to cd to $MACFUSE_SRCDIR"

xcodebuild -configuration $MACFUSE_CONFIG -target fusefs
exit_on_error "Failed to build configuration $MACFUSE_CONFIG"

sudo rm -rf "$MACFUSE_TMPDIR"/"$MACFUSE_KEXT"
exit_on_error "Failed to remove old copy of MacFUSE kernel extension"

sudo rm -rf "$MACFUSE_TMPDIR"/fuse-symbols
exit_on_error "Failed to remove old copy of MacFUSE kernel extension symbols"

mkdir "$MACFUSE_TMPDIR"/fuse-symbols
exit_on_error "Failed to create directory for symbols"

sudo cp -R "$MACFUSE_SRCDIR"/build/"$MACFUSE_CONFIG"/"$MACFUSE_KEXT" "$MACFUSE_TMPDIR"/"$MACFUSE_KEXT"
exit_on_error "Failed to copy rebuilt MacFUSE kernel extension"

sudo chown -R root:wheel "$MACFUSE_TMPDIR"/"$MACFUSE_KEXT"
exit_on_error "Failed to set permissions on rebuilt MacFUSE kernel extension"

sudo kextload -s "$MACFUSE_TMPDIR"/fuse-symbols -v "$MACFUSE_TMPDIR"/"$MACFUSE_KEXT"
exit_on_error "Failed to load rebuilt MacFUSE kernel extension"

cd "$MACFUSE_TMPDIR"
exit_on_error "Failed to change directory to $MACFUSE_TMPDIR"

# Make some testing directories
mkdir /tmp/dir /tmp/hello /tmp/ssh /tmp/xmp 2>/dev/null

echo
kextstat -l -b "$MACFUSE_BUNDLE"
echo

echo "Success"

exit 0
