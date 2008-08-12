#! /bin/zsh
# Copyright (C) 2006-2007 Amit Singh. All Rights Reserved.
#

PATH=/sbin:/usr/sbin:/bin:/usr/bin:
export PATH

os_name=`uname -s`
os_codename="Unknown"

is_absolute_path=`echo "$0" | cut -c1`
if [ "$is_absolute_path" = "/" ]
then
    macfuse_dir="`dirname $0`/.."
else
    macfuse_dir="`pwd`/`dirname $0`/.."
fi
pushd . > /dev/null
cd "$macfuse_dir" || exit 1
macfuse_dir=`pwd`
popd > /dev/null

foreach mrel (`ls -d "$macfuse_dir"/core/10.*`)
    echo "# $mrel"
    echo -n "MacFUSE.xcconfig: "
    grep MACFUSE_VERSION "$mrel"/fusefs/MacFUSE.xcconfig | awk -F= '{print $2}'
    echo -n "fuse_version.h: "
    grep 'define MACFUSE_VERSION_LITERAL' "$mrel"/fusefs/common/fuse_version.h | awk '{print $NF}'
    echo
end
