#!/bin/bash
##########################################################################
# If not stated otherwise in this file or this component's Licenses.txt
# file the following copyright and licenses apply:
#
# Copyright 2016 RDK Management
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
##########################################################################
#

#######################################
#
# Build Framework standard script for
#
# netmonitor component

# use -e to fail on any shell issue
# -e is the requirement from Build Framework
set -e


# default PATHs - use `man readlink` for more info
# the path to combined build
export RDK_PROJECT_ROOT_PATH=${RDK_PROJECT_ROOT_PATH-`readlink -m ..`}
export COMBINED_ROOT=$RDK_PROJECT_ROOT_PATH

# path to build script (this script)
export RDK_SCRIPTS_PATH=${RDK_SCRIPTS_PATH-`readlink -m $0 | xargs dirname`}

# path to components sources and target
export RDK_SOURCE_PATH=${RDK_SOURCE_PATH-`readlink -m .`}
export RDK_TARGET_PATH=${RDK_TARGET_PATH-$RDK_SOURCE_PATH}


# fsroot and toolchain (valid for all devices)
export RDK_FSROOT_PATH=${RDK_FSROOT_PATH-`readlink -m $RDK_PROJECT_ROOT_PATH/sdk/fsroot/ramdisk`}
export RDK_TOOLCHAIN_PATH=$RDK_PROJECT_ROOT_PATH/sdk/toolchain/arm-linux-gnueabihf

if [ "$XCAM_MODEL" != "XHB1" ];then
    export RDK_DUMP_SYMS=${RDK_PROJECT_ROOT_PATH}/utility/prebuilts/breakpad-prebuilts/x86/dump_syms
    export STRIP=${RDK_TOOLCHAIN_PATH}/arm-linux-gnueabihf/bin/arm-linux-gnueabihf-strip
else
    export RDK_DUMP_SYMS=${RDK_PROJECT_ROOT_PATH}/utility/prebuilts/breakpad-prebuilts/x64/dump_syms
fi
if [ "$XCAM_MODEL" == "SCHC2" ]; then
    export RDK_PATCHES=${RDK_PROJECT_ROOT_PATH}/build/components/amba/opensource/patch
else
    export RDK_PATCHES=${RDK_PROJECT_ROOT_PATH}/build/components/opensource/patch
fi

# default component name
export RDK_COMPONENT_NAME=${RDK_COMPONENT_NAME-`basename $RDK_SOURCE_PATH`}

export RDK_DIR=$RDK_PROJECT_ROOT_PATH
export RDK_DIR=$RDK_PROJECT_ROOT_PATH

if  [ "$XCAM_MODEL" == "XHB1" ] || [ "$XCAM_MODEL" == "XHC3" ]; then
    export CFLAGS="-I$RDK_FSROOT_PATH/usr/include"
    export CXXFLAGS=$CFLAGS
    export LDFLAGS="-L$RDK_FSROOT_PATH/usr/lib -L$PROJ_INSTALL/usr/lib"
fi

if [ "$XCAM_MODEL" == "SCHC2" ]; then
    echo "Setting environmental variables and Pre rule makefile for xCam2"
. ${RDK_PROJECT_ROOT_PATH}/build/components/amba/sdk/setenv2
elif [ "$XCAM_MODEL" == "XHB1" ] || [ "$XCAM_MODEL" == "XHC3" ]; then
    echo "Setting environmental variables and Pre rule makefile for DBC"
. ${RDK_PROJECT_ROOT_PATH}/build/components/sdk/setenv2
else #No Matching platform
    echo "Source environment that include packages for your platform. The environment variables PROJ_PRERULE_MAK_FILE should refer to the platform s PreRule make"
fi


# parse arguments
INITIAL_ARGS=$@

function usage()
{
    set +x
    echo "Usage: `basename $0` [-h|--help] [-v|--verbose] [action]"
    echo "    -h    --help                  : this help"
    echo "    -v    --verbose               : verbose output"
    echo "    -p    --platform  =PLATFORM   : specify platform for netmonitor"
    echo
    echo "Supported actions:"
    echo "      configure, clean, build (DEFAULT), rebuild, install"
}

# options may be followed by one colon to indicate they have a required argument
if ! GETOPT=$(getopt -n "build.sh" -o hvp: -l help,verbose,platform: -- "$@")
then
    usage
    exit 1
fi

eval set -- "$GETOPT"

while true; do
    case "$1" in
        -h | --help ) usage; exit 0 ;;
        -v | --verbose ) set -x ;;
        -p | --platform ) CC_PLATFORM="$2" ; shift ;;
        -- ) shift; break;;
        * ) break;;
    esac
    shift
done

ARGS=$@

# component-specific vars
if [ "$XCAM_MODEL" == "SCHC2" ]; then
    export CROSS_COMPILE=$RDK_TOOLCHAIN_PATH/bin/arm-linux-gnueabihf-
    export CC=${CROSS_COMPILE}gcc
    export CXX=${CROSS_COMPILE}g++
    export DEFAULT_HOST=arm-linux
    export PKG_CONFIG_PATH="$RDK_PROJECT_ROOT_PATH/opensource/lib/pkgconfig/:$RDK_FSROOT_PATH/img/fs/shadow_root/usr/local/lib/pkgconfig/:$RDK_TOOLCHAIN_PATH/lib/pkgconfig/:$PKG_CONFIG_PATH"
fi

# functional modules
function configure()
{
    pd=`pwd`
    cd ${RDK_SOURCE_PATH}
    if [ ! -f $RDK_PATCHES/.netmonitor.patched ]; then
        echo "applying patch for netmonitor"
    git apply $RDK_PATCHES/netmonitor.patch
    touch $RDK_PATCHES/.netmonitor.patched
    else
        echo "Patch already applied so going ahead with the build"
    fi
    aclocal
    libtoolize --automake
    autoheader
    automake --foreign --add-missing
    rm -f configure
    autoconf
    configure_options=" "
    if [ $XCAM_MODEL == "SCHC2" ]; then
        configure_options="--host=$DEFAULT_HOST --target=$DEFAULT_HOST --enable-rdklogger"
        export LDFLAGS+="-L${RDK_PROJECT_ROOT_PATH}/rdklogger/src/.libs -lrdkloggers -Wl,-rpath-link=${RDK_PROJECT_ROOT_PATH}/opensource/lib -L${RDK_PROJECT_ROOT_PATH}/breakpadwrap -lbreakpadwrap"
        export CXXFLAGS+="-I${RDK_PROJECT_ROOT_PATH}/rdklogger/include"
        export CFLAGS+="-I${RDK_PROJECT_ROOT_PATH}/opensource/include -I${RDK_PROJECT_ROOT_PATH}opensource/include/cjson/"
    else
        configure_options="--host=arm-linux --target=arm-linuxi --enable-rdklogger"
    fi
    ./configure --prefix=${RDK_FSROOT_PATH}/usr --sysconfdir=${RDK_FSROOT_PATH}/etc $configure_options
    cd $pd
}

function clean()
{
    echo "Start Clean"
    cd ${RDK_SOURCE_PATH}
    if [ -f Makefile ]; then
        make clean
    fi
    rm -f configure;
    rm -rf aclocal.m4 autom4te.cache config.log config.status libtool
    find . -iname "Makefile.in" -exec rm -f {} \;
    find . -iname "Makefile" | xargs rm -f
}

function build()
{
    echo "Building netmonitor common code"
    cd ${RDK_SOURCE_PATH}
    make
    cp .libs/libnlmonitor.so.0.0.0 .libs/libnlmonitor_debug.so.0.0.0

    $RDK_DUMP_SYMS .libs/libnlmonitor.so.0.0.0 > .libs/libnlmonitor.so.0.0.0.sym

    cp .libs/nlmon .libs/nlmon_debug

    $RDK_DUMP_SYMS .libs/nlmon > .libs/nlmon.sym

    mv .libs/*.sym $PLATFORM_SYMBOL_PATH
    echo "Debug symbol created for netmonitor"

    install
}

function rebuild()
{
    clean
    configure
    build
}

function install()
{
    cd ${RDK_SOURCE_PATH}
    cp ./.libs/nlmon ${RDK_FSROOT_PATH}/usr/bin/
    cp nlmon_rdkc.cfg ${RDK_FSROOT_PATH}/etc/nlmon.cfg
    if [ "$XCAM_MODEL" == "SCHC2" ]; then
        cp -p ./xcam2/addressmonitor.sh ${RDK_FSROOT_PATH}/lib/rdk/addressmonitor.sh
        cp -p ./xcam2/netmonitor_recovery.sh ${RDK_FSROOT_PATH}/lib/rdk/netmonitor_recovery.sh
    elif [ "$XCAM_MODEL" == "XHC3" ]; then
        cp -p ./xcam3/addressmonitor.sh ${RDK_FSROOT_PATH}/lib/rdk/addressmonitor.sh
        cp -p ./xcam3/netmonitor_recovery.sh ${RDK_FSROOT_PATH}/lib/rdk/netmonitor_recovery.sh
    elif [ "$XCAM_MODEL" == "XHB1" ]; then
        cp -p ./dbc/addressmonitor.sh ${RDK_FSROOT_PATH}/lib/rdk/addressmonitor.sh
        cp -p ./dbc/netmonitor_recovery.sh ${RDK_FSROOT_PATH}/lib/rdk/netmonitor_recovery.sh
    fi
    cp ./.libs/libnlmonitor.so* ${RDK_FSROOT_PATH}/usr/lib/
}
# run the logic

#these args are what left untouched after parse_args
HIT=false

for i in "$ARGS"; do
    case $i in
        configure)  HIT=true; configure ;;
        clean)      HIT=true; clean ;;
        build)      HIT=true; build ;;
        rebuild)    HIT=true; rebuild ;;
        install)    HIT=true; install ;;
        *)
            #skip unknown
        ;;
    esac
done

# if not HIT do build by default
if ! $HIT; then
  build
fi

