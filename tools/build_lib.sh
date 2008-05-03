#! /bin/sh
# Copyright (C) 2006-2007 Amit Singh. All Rights Reserved.
#

AWK=/usr/bin/awk
BASENAME=/usr/bin/basename
CP=/bin/cp
CUT=/usr/bin/cut
DIRNAME=/usr/bin/dirname
HEAD=/usr/bin/head
MAKE=/usr/bin/make
PATCH=/usr/bin/patch
RM=/bin/rm
TAR=/usr/bin/tar
UNAME=/usr/bin/uname

os_name=`$UNAME -s`
os_codename="Unknown"

is_absolute_path=`echo "$0" | $CUT -c1`
if [ "$is_absolute_path" = "/" ]
then
    macfuse_dir="`$DIRNAME $0`/.."
else
    macfuse_dir="`pwd`/`$DIRNAME $0`/.."
fi
pushd . > /dev/null
cd "$macfuse_dir" || exit 1
macfuse_dir=`pwd`
popd > /dev/null

echo "Using MacFUSE source root $macfuse_dir"

os_release=`$UNAME -r`
if [ "$1" != "" ]
then
    os_release="$1"
fi

src_dir="/dev/null"

if [ "$os_name" != "Darwin" ]
then
    echo "This script can only be run on Darwin"
    exit 1
fi

case "$os_release" in
  8*)
      lib_dir="$macfuse_dir/core/10.4/libfuse/"
      kext_dir="$macfuse_dir/core/10.4/fusefs/"
      os_codename="Tiger"
  ;;
  9*)
      lib_dir="$macfuse_dir/core/10.5/libfuse/"
      kext_dir="$macfuse_dir/core/10.5/fusefs/"
      os_codename="Leopard"
  ;;
  *)
      echo "Unsupported Mac OS X release $os_release"
      exit 1
  ;;
esac

echo "Initiating Universal build of libfuse for Mac OS X \"$os_codename\""

package_dir=`$TAR -tzvf "$lib_dir/fuse-current.tar.gz" | $HEAD -1 | $AWK '{print $NF}'`
package_name=`$BASENAME "$package_dir"`

if [ "$package_name" = "" ]
then
    echo "*** failed to determine libfuse version"
    exit 1
fi

echo "Using package name $package_name"

$RM -rf /tmp/"$package_name"

$TAR -C /tmp/ -xzvf "$lib_dir"/fuse-current.tar.gz             || exit 1
cd /tmp/"$package_name"                                        || exit 1
$PATCH -p1 < "$lib_dir"/fuse-current-macosx.patch              || exit 1
/bin/sh ./darwin_configure.sh "$kext_dir"                      || exit 1
$MAKE                                                          || exit 1

echo
echo "$package_name compiled OK in /tmp/$package_name"
echo

exit 0
