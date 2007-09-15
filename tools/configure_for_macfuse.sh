#! /bin/sh
# Copyright (C) 2006-2007 Amit Singh. All Rights Reserved.
#

UNAME=/usr/bin/uname

os_name=`$UNAME -s`
os_codename="Unknown"
os_release=`$UNAME -r`
current_product=`pwd`
sdk_dir="/dev/null"

if [ "$os_name" != "Darwin" ]
then
    echo "This script can only be run on Darwin"
    exit 1
fi

case "$os_release" in
  8*)
      sdk_dir="/Developer/SDKs/MacOSX10.4u.sdk"
      os_codename="Tiger"
  ;;
  9*)
      sdk_dir="/Developer/SDKs/MacOSX10.5.sdk"
      os_codename="Leopard"
  ;;
  *)
      echo "Unsupported Mac OS X release $os_release"
      exit 1
  ;;
esac

case "$current_product" in

  *gettext-*)
      echo "Configuring gettext for MacFUSE"
      CFLAGS="-O0 -g -D_POSIX_C_SOURCE=200112L -arch i386 -arch ppc -isysroot $sdk_dir" LDFLAGS="-Wl,-syslibroot,$sdk_dir -arch i386 -arch ppc -fno-common" ./configure --prefix=/usr/local --disable-dependency-tracking --with-libiconv-prefix=$sdk_dir/usr
  ;;

  *glib-*)
      echo "Configuring glib for MacFUSE"
      CFLAGS="-O0 -g -D_POSIX_C_SOURCE=200112L -arch i386 -arch ppc -isysroot $sdk_dir -I/usr/local/include" LDFLAGS="-Wl,-syslibroot,$sdk_dir -arch i386 -arch ppc -L/usr/local/lib" ./configure --prefix=/usr/local --disable-dependency-tracking --enable-static
  ;;

  *pkg-config-*) 
      echo "Configuring pkg-config for MacFUSE"
      CFLAGS="-O -g -D_POSIX_C_SOURCE=200112L -arch i386 -arch ppc -isysroot $sdk_dir" LDFLAGS="-arch i386 -arch ppc" ./configure --prefix=/usr/local --disable-dependency-tracking
  ;;

  *)
      echo "Don't know how to configure unrecognized product $current_product"
      exit 1
  ;;
esac

exit $?
