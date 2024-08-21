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

CONFIG_OUT_DIR=$BUILD_DIR/test/config_files/config-cis-rpc-meta-rpc-mem-rpc
export OPENFAM_ROOT=$CONFIG_OUT_DIR

######## Direct access
##for op_type in BlockingFamGetSingleRegionDataItem BlockingFamPutSingleRegionDataItem BlockingFamGetMultipleRegionDataItem BlockingFamPutMultipleRegionDataItem BlockingFamGetSingleRegionMultipleDataItem BlockingFamPutSingleRegionMultipleDataItem
##for op_type in BlockingFamGetSingleRegionDataItem BlockingFamPutSingleRegionDataItem
##for op_type in BlockingFamGetSingleRegionDataItem NonBlockingFamGetSingleRegionDataItem
#for op_type in NonBlockingFamGetSingleRegionDataItem
#do
#    #for access_pattern in Sequential UnifRand Zipf05 Zipf099
#    for access_pattern in UnifRand
#    do
#        for num_threads in 1 2 4 8
#        do
#            printf "${op_type} ${access_pattern} ${num_threads}\n" &>> log_${op_type}_${access_pattern}.txt
#            for io_size in 64 128 256 512 1024 2048 4096 8192 16384 32768 65536 131072 262144 524288 1048576 2097152
#            do
#                echo "========s======================================================"
#                echo "${op_type}, ${access_pattern}, ${io_size}, ${num_threads}"
#                echo "==============================================================="
#
#                if [[ $access_pattern = "UnifRand" ]]
#                then
#                    $BUILD_DIR/bin/openfam_adm @$TOOL_DIR/common-config-arg.txt --install_path $INSTALL_DIR \
#                        --model memory_server --cisinterface rpc --memserverinterface rpc --metaserverinterface rpc \
#                        --create_config_files --config_file_path $CONFIG_OUT_DIR --start_service --runtests \
#                        --bench_args ${io_size},1,0,${num_threads},--gtest_filter=FamPutGet.${op_type} &>> log_${op_type}_${access_pattern}.txt
#                elif [[ $access_pattern = "Zipf05" ]]
#                then
#                    $BUILD_DIR/bin/openfam_adm @$TOOL_DIR/common-config-arg.txt --install_path $INSTALL_DIR \
#                        --model memory_server --cisinterface rpc --memserverinterface rpc --metaserverinterface rpc \
#                        --create_config_files --config_file_path $CONFIG_OUT_DIR --start_service --runtests \
#                        --bench_args ${io_size},1,0.5,${num_threads},--gtest_filter=FamPutGet.${op_type} &>> log_${op_type}_${access_pattern}.txt
#                elif [[ $access_pattern = "Zipf099" ]]
#                then
#                    $BUILD_DIR/bin/openfam_adm @$TOOL_DIR/common-config-arg.txt --install_path $INSTALL_DIR \
#                        --model memory_server --cisinterface rpc --memserverinterface rpc --metaserverinterface rpc \
#                        --create_config_files --config_file_path $CONFIG_OUT_DIR --start_service --runtests \
#                        --bench_args ${io_size},1,0.99,${num_threads},--gtest_filter=FamPutGet.${op_type} &>> log_${op_type}_${access_pattern}.txt
#                else
#                    $BUILD_DIR/bin/openfam_adm @$TOOL_DIR/common-config-arg.txt --install_path $INSTALL_DIR \
#                        --model memory_server --cisinterface rpc --memserverinterface rpc --metaserverinterface rpc \
#                        --create_config_files --config_file_path $CONFIG_OUT_DIR --start_service --runtests \
#                        --bench_args ${io_size},0,0,${num_threads},--gtest_filter=FamPutGet.${op_type} &>> log_${op_type}_${access_pattern}.txt
#                fi
#
#                sleep 5
#                $BUILD_DIR/bin/openfam_adm --stop_service --clean 
#                sleep 5
#            done
#        done
#    done
#done

####### Caching page access
##for cache_page_size in 64 128 256 512 1024 2048 4096 8192 16384 32768 65536 131072 262144 524288 1048576 2097152
#for cache_page_size in 4096 65536
for cache_page_size in 65536
do
    for op_type in BlockingFamGetSingleRegionDataItem NonBlockingFamGetSingleRegionDataItem
    do
        #for access_pattern in Sequential UnifRand Zipf05 Zipf099
        for access_pattern in UnifRand
        do
            for num_threads in 1 2 4 8
            do
                #for cache_ratio in 1.0 0.9 0.8 0.7 0.6 0.5 0.4 0.3 0.2 0.1
                for cache_ratio in 0.9 0.5 0.1
                do
                    printf "${op_type} ${access_pattern} ${num_threads} ${cache_ratio}\n" &>> log_${op_type}_${access_pattern}_${cache_ratio}_${cache_page_size}.txt
                    for io_size in 64 128 256 512 1024 2048 4096 8192 16384 32768 65536 131072 262144 524288 1048576 2097152
                    do
                        #io_size=${cache_page_size}
                        #cache_page_size=${io_size}
                        #cache_page_size=4096
                        echo "========s======================================================"
                        echo "${op_type}, ${access_pattern}, ${io_size}, ${num_threads}, ${cache_ratio}, ${cache_page_size}"
                        echo "==============================================================="
    
                        if [[ $access_pattern = "UnifRand" ]]
                        then
                            $BUILD_DIR/bin/openfam_adm @$TOOL_DIR/common-config-arg.txt --install_path $INSTALL_DIR \
                                --model memory_server --cisinterface rpc --memserverinterface rpc --metaserverinterface rpc \
                                --create_config_files --config_file_path $CONFIG_OUT_DIR --start_service --runtests \
                                --bench_args ${io_size},1,0,${num_threads},${cache_ratio},${cache_page_size},--gtest_filter=FamPutGet.${op_type} &>> log_${op_type}_${access_pattern}_${cache_ratio}_${cache_page_size}.txt
                        elif [[ $access_pattern = "Zipf05" ]]
                        then
                            $BUILD_DIR/bin/openfam_adm @$TOOL_DIR/common-config-arg.txt --install_path $INSTALL_DIR \
                                --model memory_server --cisinterface rpc --memserverinterface rpc --metaserverinterface rpc \
                                --create_config_files --config_file_path $CONFIG_OUT_DIR --start_service --runtests \
                                --bench_args ${io_size},1,0.5,${num_threads},${cache_ratio},${cache_page_size},--gtest_filter=FamPutGet.${op_type} &>> log_${op_type}_${access_pattern}_${cache_ratio}_${cache_page_size}.txt
                        elif [[ $access_pattern = "Zipf099" ]]
                        then
                            $BUILD_DIR/bin/openfam_adm @$TOOL_DIR/common-config-arg.txt --install_path $INSTALL_DIR \
                                --model memory_server --cisinterface rpc --memserverinterface rpc --metaserverinterface rpc \
                                --create_config_files --config_file_path $CONFIG_OUT_DIR --start_service --runtests \
                                --bench_args ${io_size},1,0.99,${num_threads},${cache_ratio},${cache_page_size},--gtest_filter=FamPutGet.${op_type} &>> log_${op_type}_${access_pattern}_${cache_ratio}_${cache_page_size}.txt
                        else
                            $BUILD_DIR/bin/openfam_adm @$TOOL_DIR/common-config-arg.txt --install_path $INSTALL_DIR \
                                --model memory_server --cisinterface rpc --memserverinterface rpc --metaserverinterface rpc \
                                --create_config_files --config_file_path $CONFIG_OUT_DIR --start_service --runtests \
                                --bench_args ${io_size},0,0,${num_threads},${cache_ratio},${cache_page_size},--gtest_filter=FamPutGet.${op_type} &>> log_${op_type}_${access_pattern}_${cache_ratio}_${cache_page_size}.txt
                        fi
    
                        sleep 5
                        $BUILD_DIR/bin/openfam_adm --stop_service --clean 
                        sleep 5
                    done
                done
            done
        done
    done
done

####### Caching byte access
#for op_type in BlockingFamGetSingleRegionDataItem NonBlockingFamGetSingleRegionDataItem
for op_type in BlockingFamGetSingleRegionDataItem
do
    #for access_pattern in Sequential UnifRand Zipf05 Zipf099
    for access_pattern in UnifRand
    do
        for num_threads in 1 2 4 8
        do
            #for cache_ratio in 1.0 0.9 0.8 0.7 0.6 0.5 0.4 0.3 0.2 0.1
            for cache_ratio in 0.9 0.5 0.1
            do
                printf "${op_type} ${access_pattern} ${num_threads} ${cache_ratio}\n" &>> log_${op_type}_${access_pattern}_${cache_ratio}.txt
                for io_size in 64 128 256 512 1024 2048 4096 8192 16384 32768 65536 131072 262144 524288 1048576 2097152
                do
                    cache_page_size=${io_size}
                    #cache_page_size=4096

                    echo "========s======================================================"
                    echo "${op_type}, ${access_pattern}, ${io_size}, ${num_threads}, ${cache_ratio}, ${cache_page_size}"
                    echo "==============================================================="

                    if [[ $access_pattern = "UnifRand" ]]
                    then
                        $BUILD_DIR/bin/openfam_adm @$TOOL_DIR/common-config-arg.txt --install_path $INSTALL_DIR \
                            --model memory_server --cisinterface rpc --memserverinterface rpc --metaserverinterface rpc \
                            --create_config_files --config_file_path $CONFIG_OUT_DIR --start_service --runtests \
                            --bench_args ${io_size},1,0,${num_threads},${cache_ratio},${cache_page_size},--gtest_filter=FamPutGet.${op_type} &>> log_${op_type}_${access_pattern}_${cache_ratio}.txt
                    elif [[ $access_pattern = "Zipf05" ]]
                    then
                        $BUILD_DIR/bin/openfam_adm @$TOOL_DIR/common-config-arg.txt --install_path $INSTALL_DIR \
                            --model memory_server --cisinterface rpc --memserverinterface rpc --metaserverinterface rpc \
                            --create_config_files --config_file_path $CONFIG_OUT_DIR --start_service --runtests \
                            --bench_args ${io_size},1,0.5,${num_threads},${cache_ratio},${cache_page_size},--gtest_filter=FamPutGet.${op_type} &>> log_${op_type}_${access_pattern}_${cache_ratio}.txt
                    elif [[ $access_pattern = "Zipf099" ]]
                    then
                        $BUILD_DIR/bin/openfam_adm @$TOOL_DIR/common-config-arg.txt --install_path $INSTALL_DIR \
                            --model memory_server --cisinterface rpc --memserverinterface rpc --metaserverinterface rpc \
                            --create_config_files --config_file_path $CONFIG_OUT_DIR --start_service --runtests \
                            --bench_args ${io_size},1,0.99,${num_threads},${cache_ratio},${cache_page_size},--gtest_filter=FamPutGet.${op_type} &>> log_${op_type}_${access_pattern}_${cache_ratio}.txt
                    else
                        $BUILD_DIR/bin/openfam_adm @$TOOL_DIR/common-config-arg.txt --install_path $INSTALL_DIR \
                            --model memory_server --cisinterface rpc --memserverinterface rpc --metaserverinterface rpc \
                            --create_config_files --config_file_path $CONFIG_OUT_DIR --start_service --runtests \
                            --bench_args ${io_size},0,0,${num_threads},${cache_ratio},${cache_page_size},--gtest_filter=FamPutGet.${op_type} &>> log_${op_type}_${access_pattern}_${cache_ratio}.txt
                    fi

                    sleep 5
                    $BUILD_DIR/bin/openfam_adm --stop_service --clean 
                    sleep 5
                done
            done
        done
    done
done

#$BUILD_DIR/bin/openfam_adm @$TOOL_DIR/common-config-arg.txt --install_path $INSTALL_DIR \
#    --model memory_server --cisinterface rpc --memserverinterface rpc --metaserverinterface rpc \
#    --create_config_files --config_file_path $CONFIG_OUT_DIR --start_service --runtests \
#    --bench_args 64,1,0,1,0.1,4096,--gtest_filter=FamPutGet.Blocking
#
#sleep 5
#$BUILD_DIR/bin/openfam_adm --stop_service --clean 
#sleep 5

cd $CURRENTDIR
