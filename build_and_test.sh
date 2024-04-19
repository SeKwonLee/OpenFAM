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
cmake ..; $MAKE_CMD ; make install
if [[ $? > 0 ]]
then
        echo "OpenFAM build with memoryserver version failed.. exit..."
        exit 1
fi

#echo "==========================================================="
#echo "Test OpenFAM with cis-rpc-meta-direct-mem-rpc configuration"
#echo "==========================================================="
#CONFIG_OUT_DIR=$BUILD_DIR/test/config_files/config-cis-rpc-meta-direct-mem-rpc
#export OPENFAM_ROOT=$CONFIG_OUT_DIR
#$BUILD_DIR/bin/openfam_adm @$TOOL_DIR/common-config-arg.txt --install_path $INSTALL_DIR --model memory_server --cisinterface rpc --memserverinterface rpc --metaserverinterface direct --create_config_files --config_file_path $CONFIG_OUT_DIR --start_service --runtests
#if [[ $? > 0 ]]
#then
#        echo "OpenFAM test with cis-rpc-meta-direct-mem-rpc configuration failed. exit..."
#        exit 1
#fi
#
#sleep 5
#
#$BUILD_DIR/bin/openfam_adm --stop_service --clean 
#
#sleep 5
#
#echo "========================================================"
#echo "Test OpenFAM with cis-rpc-meta-rpc-mem-rpc configuration"
#echo "========================================================"
#CONFIG_OUT_DIR=$BUILD_DIR/test/config_files/config-cis-rpc-meta-rpc-mem-rpc
#export OPENFAM_ROOT=$CONFIG_OUT_DIR
#$BUILD_DIR/bin/openfam_adm @$TOOL_DIR/common-config-arg.txt --install_path $INSTALL_DIR --model memory_server --cisinterface rpc --memserverinterface rpc --metaserverinterface rpc --create_config_files --config_file_path $CONFIG_OUT_DIR --start_service --runtests
#
#if [[ $? > 0 ]]
#then
#        echo "OpenFAM test with cis-rpc-meta-rpc-mem-rpc configuration failed. exit..."
#        exit 1
#fi
#
#sleep 5
#
#$BUILD_DIR/bin/openfam_adm --stop_service --clean 
#
#sleep 5
#
#echo "=============================================================="
#echo "Test OpenFAM with cis-rpc-meta-direct-mem-direct configuration"
#echo "=============================================================="
#CONFIG_OUT_DIR=$BUILD_DIR/test/config_files/config-cis-rpc-meta-direct-mem-direct
#export OPENFAM_ROOT=$CONFIG_OUT_DIR
#$BUILD_DIR/bin/openfam_adm @$TOOL_DIR/common-config-arg.txt --install_path $INSTALL_DIR --model memory_server --cisinterface rpc --memserverinterface direct --metaserverinterface direct --create_config_files --config_file_path $CONFIG_OUT_DIR --start_service --runtests
#
#if [[ $? > 0 ]]
#then
#        echo "OpenFAM test with cis-rpc-meta-direct-mem-direct configuration failed. exit..."
#        exit 1
#fi
#
#sleep 5
#
#$BUILD_DIR/bin/openfam_adm --stop_service --clean 
#
#sleep 5
#
#echo "==========================================================="
#echo "Test OpenFAM with cis-direct-meta-rpc-mem-rpc configuration"
#echo "==========================================================="
#CONFIG_OUT_DIR=$BUILD_DIR/test/config_files/config-cis-direct-meta-rpc-mem-rpc
#export OPENFAM_ROOT=$CONFIG_OUT_DIR
#$BUILD_DIR/bin/openfam_adm @$TOOL_DIR/common-config-arg.txt --install_path $INSTALL_DIR --model memory_server --cisinterface direct --memserverinterface rpc --metaserverinterface rpc --create_config_files --config_file_path $CONFIG_OUT_DIR --start_service --runtests
#
#if [[ $? > 0 ]]
#then
#        echo "OpenFAM test with cis-direct-meta-rpc-mem-rpc configuration failed. exit..."
#        exit 1
#fi
#
#sleep 5 
#
#$BUILD_DIR/bin/openfam_adm --stop_service --clean 
#
#sleep 5
#
#echo "==========================================================="
#echo "Test OpenFAM with shared memory configuration"
#echo "==========================================================="
#CONFIG_OUT_DIR=$BUILD_DIR/test/config_files/config-shared-memory
#export OPENFAM_ROOT=$CONFIG_OUT_DIR
#$BUILD_DIR/bin/openfam_adm @$TOOL_DIR/common-config-arg.txt --install_path $INSTALL_DIR --model shared_memory --cisinterface direct --memserverinterface direct --metaserverinterface direct --create_config_files --config_file_path $CONFIG_OUT_DIR --start_service --runtests
#
#if [[ $? > 0 ]]
#then
#        echo "OpenFAM test with shared memory configuration failed. exit..."
#        exit 1
#fi
#
#sleep 5 
#
#$BUILD_DIR/bin/openfam_adm --stop_service --clean 
#
#sleep 5
#
#echo "==========================================================="
#echo "Test OpenFAM with multiple memory servers in all rpc configuration"
#echo "==========================================================="
#CONFIG_OUT_DIR=$BUILD_DIR/test/config_files/config-multi-mem
#export OPENFAM_ROOT=$CONFIG_OUT_DIR
#$BUILD_DIR/bin/openfam_adm @$TOOL_DIR/multi-mem-config-arg.txt --install_path $INSTALL_DIR --create_config_files --config_file_path $CONFIG_OUT_DIR --start_service --runtests
#
#if [[ $? > 0 ]]
#then
#        echo "OpenFAM test with multiple memory servers in all rpc configuration failed. exit..."
#        exit 1
#fi
#
#$BUILD_DIR/bin/openfam_adm --stop_service --clean 


##cmake .. -DENABLE_SINGLE_DATA_ITEM=OFF -DENABLE_FAM_OPS_SYNC=OFF; $MAKE_CMD ; make install
##CONFIG_OUT_DIR=$BUILD_DIR/test/config_files/config-cis-rpc-meta-rpc-mem-rpc
##export OPENFAM_ROOT=$CONFIG_OUT_DIR
##
##for num_threads in 1 2 4 8
##do
##    for op_type in BlockingFamGet BlockingFamPut
##    do
##        for access_pattern in Sequential Random
##        do
##            printf "${op_type} ${access_pattern} ${num_threads}\n" &>> log_multi_nosync.txt
##            for io_size in 64 128 256 512 1024 2048 4096 8192 16384 32768 65536 131072 262144 524288 1048576 2097152
##            do
##                echo "==============================================================="
##                echo "${op_type}, ${access_pattern}, ${io_size}, ${num_threads}"
##                echo "==============================================================="
##
##                if [[ $access_pattern = "Random" ]]
##                then
##                    $BUILD_DIR/bin/openfam_adm @$TOOL_DIR/common-config-arg.txt --install_path $INSTALL_DIR --model memory_server --cisinterface rpc --memserverinterface rpc --metaserverinterface rpc --create_config_files --config_file_path $CONFIG_OUT_DIR --start_service --runtests --bench_args ${io_size},1,1,${num_threads},--gtest_filter=FamPutGet.${op_type} &>> log_multi_nosync.txt
##                else
##                    $BUILD_DIR/bin/openfam_adm @$TOOL_DIR/common-config-arg.txt --install_path $INSTALL_DIR --model memory_server --cisinterface rpc --memserverinterface rpc --metaserverinterface rpc --create_config_files --config_file_path $CONFIG_OUT_DIR --start_service --runtests --bench_args ${io_size},1,0,${num_threads},--gtest_filter=FamPutGet.${op_type} &>> log_multi_nosync.txt
##                fi
##
##                sleep 5
##                $BUILD_DIR/bin/openfam_adm --stop_service --clean 
##                sleep 5
##            done
##        done
##    done
##done
##
##cmake .. -DENABLE_SINGLE_DATA_ITEM=ON -DENABLE_FAM_OPS_SYNC=OFF; $MAKE_CMD ; make install
##CONFIG_OUT_DIR=$BUILD_DIR/test/config_files/config-cis-rpc-meta-rpc-mem-rpc
##export OPENFAM_ROOT=$CONFIG_OUT_DIR
##
##for num_threads in 1 2 4 8
##do
##    for op_type in BlockingFamGet BlockingFamPut
##    do
##        for access_pattern in Sequential Random
##        do
##            printf "${op_type} ${access_pattern} ${num_threads}\n" &>> log_single_nosync.txt
##            for io_size in 64 128 256 512 1024 2048 4096 8192 16384 32768 65536 131072 262144 524288 1048576 2097152
##            do
##                echo "==============================================================="
##                echo "${op_type}, ${access_pattern}, ${io_size}, ${num_threads}"
##                echo "==============================================================="
##
##                if [[ $access_pattern = "Random" ]]
##                then
##                    $BUILD_DIR/bin/openfam_adm @$TOOL_DIR/common-config-arg.txt --install_path $INSTALL_DIR --model memory_server --cisinterface rpc --memserverinterface rpc --metaserverinterface rpc --create_config_files --config_file_path $CONFIG_OUT_DIR --start_service --runtests --bench_args ${io_size},1,1,${num_threads},--gtest_filter=FamPutGet.${op_type} &>> log_single_nosync.txt
##                else
##                    $BUILD_DIR/bin/openfam_adm @$TOOL_DIR/common-config-arg.txt --install_path $INSTALL_DIR --model memory_server --cisinterface rpc --memserverinterface rpc --metaserverinterface rpc --create_config_files --config_file_path $CONFIG_OUT_DIR --start_service --runtests --bench_args ${io_size},1,0,${num_threads},--gtest_filter=FamPutGet.${op_type} &>> log_single_nosync.txt
##                fi
##
##                sleep 5
##                $BUILD_DIR/bin/openfam_adm --stop_service --clean 
##                sleep 5
##            done
##        done
##    done
##done
##
##
##cmake .. -DENABLE_SINGLE_DATA_ITEM=OFF -DENABLE_FAM_OPS_SYNC=ON; $MAKE_CMD ; make install
##CONFIG_OUT_DIR=$BUILD_DIR/test/config_files/config-cis-rpc-meta-rpc-mem-rpc
##export OPENFAM_ROOT=$CONFIG_OUT_DIR
##
##for num_threads in 1 2 4 8
##do
##    for op_type in BlockingFamGet BlockingFamPut
##    do
##        for access_pattern in Sequential Random
##        do
##            printf "${op_type} ${access_pattern} ${num_threads}\n" &>> log_multi_sync.txt
##            for io_size in 64 128 256 512 1024 2048 4096 8192 16384 32768 65536 131072 262144 524288 1048576 2097152
##            do
##                echo "==============================================================="
##                echo "${op_type}, ${access_pattern}, ${io_size}, ${num_threads}"
##                echo "==============================================================="
##
##                if [[ $access_pattern = "Random" ]]
##                then
##                    $BUILD_DIR/bin/openfam_adm @$TOOL_DIR/common-config-arg.txt --install_path $INSTALL_DIR --model memory_server --cisinterface rpc --memserverinterface rpc --metaserverinterface rpc --create_config_files --config_file_path $CONFIG_OUT_DIR --start_service --runtests --bench_args ${io_size},1,1,${num_threads},--gtest_filter=FamPutGet.${op_type} &>> log_multi_sync.txt
##                else
##                    $BUILD_DIR/bin/openfam_adm @$TOOL_DIR/common-config-arg.txt --install_path $INSTALL_DIR --model memory_server --cisinterface rpc --memserverinterface rpc --metaserverinterface rpc --create_config_files --config_file_path $CONFIG_OUT_DIR --start_service --runtests --bench_args ${io_size},1,0,${num_threads},--gtest_filter=FamPutGet.${op_type} &>> log_multi_sync.txt
##                fi
##
##                sleep 5
##                $BUILD_DIR/bin/openfam_adm --stop_service --clean 
##                sleep 5
##            done
##        done
##    done
##done
##
##cmake .. -DENABLE_SINGLE_DATA_ITEM=ON -DENABLE_FAM_OPS_SYNC=ON; $MAKE_CMD ; make install
##CONFIG_OUT_DIR=$BUILD_DIR/test/config_files/config-cis-rpc-meta-rpc-mem-rpc
##export OPENFAM_ROOT=$CONFIG_OUT_DIR
##
##for num_threads in 1 2 4 8
##do
##    for op_type in BlockingFamGet BlockingFamPut
##    do
##        for access_pattern in Sequential Random
##        do
##            printf "${op_type} ${access_pattern} ${num_threads}\n" &>> log_single_sync.txt
##            for io_size in 64 128 256 512 1024 2048 4096 8192 16384 32768 65536 131072 262144 524288 1048576 2097152
##            do
##                echo "==============================================================="
##                echo "${op_type}, ${access_pattern}, ${io_size}, ${num_threads}"
##                echo "==============================================================="
##
##                if [[ $access_pattern = "Random" ]]
##                then
##                    $BUILD_DIR/bin/openfam_adm @$TOOL_DIR/common-config-arg.txt --install_path $INSTALL_DIR --model memory_server --cisinterface rpc --memserverinterface rpc --metaserverinterface rpc --create_config_files --config_file_path $CONFIG_OUT_DIR --start_service --runtests --bench_args ${io_size},1,1,${num_threads},--gtest_filter=FamPutGet.${op_type} &>> log_single_sync.txt
##                else
##                    $BUILD_DIR/bin/openfam_adm @$TOOL_DIR/common-config-arg.txt --install_path $INSTALL_DIR --model memory_server --cisinterface rpc --memserverinterface rpc --metaserverinterface rpc --create_config_files --config_file_path $CONFIG_OUT_DIR --start_service --runtests --bench_args ${io_size},1,0,${num_threads},--gtest_filter=FamPutGet.${op_type} &>> log_single_sync.txt
##                fi
##
##                sleep 5
##                $BUILD_DIR/bin/openfam_adm --stop_service --clean 
##                sleep 5
##            done
##        done
##    done
##done

cmake .. -DENABLE_SINGLE_DATA_ITEM=ON -DENABLE_FAM_OPS_SYNC=OFF; $MAKE_CMD ; make install
CONFIG_OUT_DIR=$BUILD_DIR/test/config_files/config-cis-rpc-meta-rpc-mem-rpc
export OPENFAM_ROOT=$CONFIG_OUT_DIR

$BUILD_DIR/bin/openfam_adm @$TOOL_DIR/common-config-arg.txt --install_path $INSTALL_DIR \
    --model memory_server --cisinterface rpc --memserverinterface rpc --metaserverinterface rpc \
    --create_config_files --config_file_path $CONFIG_OUT_DIR --start_service --runtests \
    --bench_args 1048576,1,4,--gtest_filter=FamPutGet.BlockingFamGetSingleRegionDataItem

sleep 5
$BUILD_DIR/bin/openfam_adm --stop_service --clean 
sleep 5

$BUILD_DIR/bin/openfam_adm @$TOOL_DIR/common-config-arg.txt --install_path $INSTALL_DIR \
    --model memory_server --cisinterface rpc --memserverinterface rpc --metaserverinterface rpc \
    --create_config_files --config_file_path $CONFIG_OUT_DIR --start_service --runtests \
    --bench_args 1048576,1,4,--gtest_filter=FamPutGet.BlockingFamPutSingleRegionDataItem

sleep 5
$BUILD_DIR/bin/openfam_adm --stop_service --clean 
sleep 5

$BUILD_DIR/bin/openfam_adm @$TOOL_DIR/common-config-arg.txt --install_path $INSTALL_DIR \
    --model memory_server --cisinterface rpc --memserverinterface rpc --metaserverinterface rpc \
    --create_config_files --config_file_path $CONFIG_OUT_DIR --start_service --runtests \
    --bench_args 1048576,1,4,--gtest_filter=FamPutGet.BlockingFamGetMultipleRegionDataItem

sleep 5
$BUILD_DIR/bin/openfam_adm --stop_service --clean 
sleep 5

$BUILD_DIR/bin/openfam_adm @$TOOL_DIR/common-config-arg.txt --install_path $INSTALL_DIR \
    --model memory_server --cisinterface rpc --memserverinterface rpc --metaserverinterface rpc \
    --create_config_files --config_file_path $CONFIG_OUT_DIR --start_service --runtests \
    --bench_args 1048576,1,4,--gtest_filter=FamPutGet.BlockingFamPutMultipleRegionDataItem

sleep 5
$BUILD_DIR/bin/openfam_adm --stop_service --clean 
sleep 5

cd $CURRENTDIR
