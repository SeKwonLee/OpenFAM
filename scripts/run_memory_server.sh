#!/bin/bash
CURRENTDIR=/home/leesek/projects/OpenFAM
BUILD_DIR=$CURRENTDIR/build/
THIRDPARTY_BUILD=$CURRENTDIR/third-party/build/
INSTALL_DIR=$BUILD_DIR

#set library path
export LD_LIBRARY_PATH=$THIRDPARTY_BUILD/lib/:$THIRDPARTY_BUILD/lib64/:$LD_LIBRARY_PATH

#set environment variable for huge pages
#export RDMAV_HUGEPAGES_SAFE=1

#Build and run test with MemoryServer allocator
cd $BUILD_DIR

CONFIG_OUT_DIR=$BUILD_DIR/test/config_files/config-cis-rpc-meta-rpc-mem-rpc
export OPENFAM_ROOT=$CONFIG_OUT_DIR
numactl --physcpubind=0 --membind=0 bin/memory_server -m 0 > /dev/null 2>&1 &
