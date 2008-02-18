#! /bin/sh
# Copyright (C) 2006-2007 Amit Singh. All Rights Reserved.
#

UNAME=/usr/bin/uname

os_name=`$UNAME -s`
os_codename="Unknown"
os_release=`$UNAME -r`
sdk_dir="/dev/null"

current_product=${1:-`pwd`}
current_product=`basename $current_product`

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

  gettext*)
      echo "Configuring Universal build of gettext for MacFUSE"
      CFLAGS="-O0 -g -D_POSIX_C_SOURCE=200112L -arch i386 -arch ppc -isysroot $sdk_dir" LDFLAGS="-Wl,-syslibroot,$sdk_dir -arch i386 -arch ppc -fno-common" ./configure --prefix=/usr/local --disable-dependency-tracking --with-libiconv-prefix=$sdk_dir/usr
  ;;

  glib*)
      echo "Configuring Universal build of glib for MacFUSE"
      if [ "$os_codename" = "Leopard" ]
      then      
          CFLAGS="-O0 -g -arch i386 -arch ppc -isysroot $sdk_dir -I/usr/local/include" LDFLAGS="-Wl,-syslibroot,$sdk_dir -arch i386 -arch ppc -L/usr/local/lib" ./configure --prefix=/usr/local --disable-dependency-tracking --enable-static
      else
          CFLAGS="-O0 -g -D_POSIX_C_SOURCE=200112L -arch i386 -arch ppc -isysroot $sdk_dir -I/usr/local/include" LDFLAGS="-Wl,-syslibroot,$sdk_dir -arch i386 -arch ppc -L/usr/local/lib" ./configure --prefix=/usr/local --disable-dependency-tracking --enable-static
      fi
  ;;

  pkg-config-*) 
      echo "Configuring Universal build of pkg-config for MacFUSE"
      if [ "$os_codename" = "Leopard" ]
      then
          CFLAGS="-O -g -D_POSIX_C_SOURCE=200112L -arch i386 -arch ppc -isysroot $sdk_dir" LDFLAGS="-arch i386 -arch ppc" ./configure --prefix=/usr/local --disable-dependency-tracking
      else
          CFLAGS="-O -g -arch i386 -arch ppc -isysroot $sdk_dir" LDFLAGS="-arch i386 -arch ppc" ./configure --prefix=/usr/local --disable-dependency-tracking
      fi
  ;;

  *sshfs*) 
      echo "Configuring Universal build of sshfs for MacFUSE"
      if [ "$os_codename" = "Leopard" ]
      then
          CFLAGS="-D__FreeBSD__=10 -DDARWIN_SEMAPHORE_COMPAT -DSSH_NODELAY_WORKAROUND -D_POSIX_C_SOURCE=200112L -O -g -arch i386 -arch ppc -isysroot /Developer/SDKs/MacOSX10.5.sdk" LDFLAGS="-arch i386 -arch ppc" ./configure --prefix=/usr/local --disable-dependency-tracking
      else
          CFLAGS="-D__FreeBSD__=10 -DDARWIN_SEMAPHORE_COMPAT -DSSH_NODELAY_WORKAROUND -D_POSIX_C_SOURCE=200112L -O -g -arch i386 -arch ppc -isysroot /Developer/SDKs/MacOSX10.4u.sdk" LDFLAGS="-arch i386 -arch ppc" ./configure --prefix=/usr/local --disable-dependency-tracking
      fi
  ;;

  *)
      echo "Don't know how to configure unrecognized product $current_product"
      exit 1
  ;;
esac

exit $?
