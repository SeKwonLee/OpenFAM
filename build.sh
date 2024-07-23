#!/bin/bash
CURRENTDIR=`pwd`
BUILD_DIR=$CURRENTDIR/build/
TOOL_DIR=$CURRENTDIR/tools
MAKE_CMD="make -j"
THIRDPARTY_BUILD=$CURRENTDIR/third-party/build/
INSTALL_DIR=$BUILD_DIR

#creating build directory if not present
if [ ! -d "$BUILD_DIR" ]; then
  echo "Creating $BUILD_DIR"
  mkdir -p $BUILD_DIR
fi

#set library path
export LD_LIBRARY_PATH=$THIRDPARTY_BUILD/lib/:$THIRDPARTY_BUILD/lib64/:$LD_LIBRARY_PATH

#Build and run test with MemoryServer allocator
cd $BUILD_DIR

#cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_CHECK_OFFSETS=1 -DENABLE_THALLIUM=1 -DLIBFABRIC_PATH=/usr/lib64 -DPMIX_PATH=/usr/lib64; $MAKE_CMD ; make install
cmake .. -DENABLE_LOCAL_CACHE=ON; $MAKE_CMD ; make install
#cmake ..; $MAKE_CMD ; make install
if [[ $? > 0 ]]
then
        echo "OpenFAM build with memoryserver version failed.. exit..."
        exit 1
fi

cd $CURRENTDIR
