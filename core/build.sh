# Copyright (C) 2020 - 2022 OPPO. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

#!/bin/bash
set -e
ROOT=`dirname "$(realpath $0)"`
BUILD_DIR="${ROOT}/build"
SNN_EXPORT_DIR="${ROOT}/../snn-core-install/"
ANDROID_NDK_HOME="$ANDROID_SDK_ROOT/ndk/21.4.7075529"

make_dir()
{
    if [ ! -d $1 ]
    then
        mkdir $1
    fi
}

print_usage()
{
    echo
    echo "SNN Build Script..."
    echo
    echo "Usage: Linux Build: `basename $0` linux [d|r|p]"
    echo
    echo "d : Debug; r: Release; p: Release with symbols unstripped"
    echo
    echo "Usage: Android Build: Default(64 bit, debug): `basename $0` android"
    echo
    echo "Usage: Android Build: Specific Use: `basename $0` android [d|r|p] [32|64]"
    echo
    echo "d : Debug; r: Release; p: Profile"
    echo "32: 32 bit build; 64: 64 bit build"
    echo
    echo "Usage: Clean: `basename $0` clean [all|linux|android]"
    echo
}

clean()
{
    echo "Deleting Build Files..."
    rm -rf ${BUILD_DIR}
    if [ "$1" == "linux" ]; then
        rm -rf ${SNN_EXPORT_DIR}linux*
    elif [ "$1" == "android" ]; then
        rm -rf ${SNN_EXPORT_DIR}arm*
    else
        rm -rf ${SNN_EXPORT_DIR}
    fi
}

build_android()
{   
    if [[ -z "$ANDROID_SDK_ROOT" ]]; then
        echo "Please set ANDROID_SDK_ROOT"
        echo "export ANDROID_SDK_ROOT=/path/to/sdk"
        exit 1
    elif [ ! -d "$ANDROID_NDK_HOME" ]; then
        echo "NDK version doesn't match"
        echo "Required version: 21.4.7075529"
        exit 1
    else
        echo "ANDROID_SDK_ROOT = $ANDROID_SDK_ROOT"
        echo "ANDROID_NDK_HOME = ${ANDROID_NDK_HOME}"
    fi

    build_type="Debug"
    if [ "$1" == "r" ]; then
        echo "Build Type: Release"
        build_type="Release"
    elif [ "$1" == "p" ]; then
        export SNN_PROFILING=1
        echo "Build Type: RelWithDebInfo"
        build_type="RelWithDebInfo"
    else
        echo "Build Type: Debug"
    fi
    
    make_dir ${BUILD_DIR}

    if [ "$2" == "32" ]; then
        cd ${BUILD_DIR} && \
        cmake ../ -DCMAKE_BUILD_TYPE=${build_type} \
            -DCMAKE_TOOLCHAIN_FILE=${ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake \
            -DANDROID_ABI=armeabi-v7a \
            -DANDROID_NATIVE_API_LEVEL=30
    else
    # Default build is for 64 bits
        cd ${BUILD_DIR} && \
        cmake ../ -DCMAKE_BUILD_TYPE=${build_type} \
            -DCMAKE_TOOLCHAIN_FILE=${ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake \
            -DANDROID_ABI=arm64-v8a \
            -DANDROID_NATIVE_API_LEVEL=30
    fi
    
    cd ${BUILD_DIR} && make -j8 install
}

build_linux()
{
    build_type="Release"
    if [ "$1" == "r" ]; then
        echo "Build Type: Release"
        build_type="Release"
    elif [ "$1" == "p" ]; then
        echo "Build Type: RelWithDebInfo"
        build_type="RelWithDebInfo"
    else
        echo "Build Type: Debug"
        build_type="Debug"
    fi

    make_dir ${BUILD_DIR}

    cd ${BUILD_DIR} && \
    cmake ../ -DOpenGL_GL_PREFERENCE=GLVND -DCMAKE_BUILD_TYPE=${build_type} && \
    make -j8 install
}

if [ "$1" == "clean" ]; then
    clean $2
    exit 0
elif [ "$1" == "android" ]; then
    build_android $2 $3
    exit 0
elif [ "$1" == "linux" ]; then
    build_linux $2
    exit 0
else 
    print_usage
    exit 1
fi

