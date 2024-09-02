#!/bin/bash
CURRENTDIR=`pwd`
BUILD_DIR=$CURRENTDIR/build
INSTALL_DIR=$BUILD_DIR
CONFIG_OUT_DIR=$BUILD_DIR/config
export OPENFAM_ROOT=$CONFIG_OUT_DIR

cd $BUILD_DIR

cmake .. -DLIBFABRIC_PATH=/opt/cray/libfabric/1.15.2.0 -DENABLE_LOCAL_CACHE=OFF; make -j; make install
if [[ $? > 0 ]]
then
        echo "OpenFAM build with memoryserver version failed.. exit..."
        exit 1
fi

sleep 5

####### Direct page access
cache_type="direct_page"
##for cache_page_size in 64 128 256 512 1024 2048 4096 8192 16384 32768 65536 131072 262144 524288 1048576 2097152
for cache_page_size in 4096 65536
do
    #for op_type in BlockingFamGetSingleRegionDataItem NonBlockingFamGetSingleRegionDataItem
    for op_type in NonBlockingFamGetSingleRegionDataItem
    do
        #for access_pattern in Sequential UnifRand Zipf05 Zipf099
        for access_pattern in UnifRand
        do
            for num_threads in 1 2 4 8
            do
                #for cache_ratio in 1.0 0.9 0.8 0.7 0.6 0.5 0.4 0.3 0.2 0.1
                for cache_ratio in 0.0
                do
                    printf "${cache_type} ${op_type} ${access_pattern} ${num_threads} ${cache_ratio}\n" \
                        &>> log_${cache_type}_${op_type}_${access_pattern}_${cache_ratio}_${cache_page_size}.txt
                    for io_size in 64 128 256 512 1024 2048 4096 8192 16384 32768 65536 131072 262144 524288 1048576 2097152
                    do
                        #io_size=${cache_page_size}
                        #cache_page_size=${io_size}
                        #cache_page_size=4096
                        echo "========s======================================================"
                        echo "${cache_type}, ${op_type}, ${access_pattern}, ${io_size}, ${num_threads}, ${cache_ratio}, ${cache_page_size}"
                        echo "==============================================================="

                        $BUILD_DIR/bin/openfam_adm --start_service --config_file_path=$CONFIG_OUT_DIR --install_path $INSTALL_DIR
                        sleep 5
    
                        if [[ $access_pattern = "UnifRand" ]]
                        then
                            srun --nodelist=gtfama6 -p gtfamA numactl --membind=0 \
                                $BUILD_DIR/test/microbench/fam-api-mb/fam_microbenchmark_datapath2 \
                                ${io_size} 1 0 ${num_threads} ${cache_ratio} ${cache_page_size} \
                                --gtest_filter=FamPutGet.${op_type} &>> \
                                log_${cache_type}_${op_type}_${access_pattern}_${cache_ratio}_${cache_page_size}.txt
                        elif [[ $access_pattern = "Zipf05" ]]
                        then
                            srun --nodelist=gtfama6 -p gtfamA numactl --membind=0 \
                                $BUILD_DIR/test/microbench/fam-api-mb/fam_microbenchmark_datapath2 \
                                ${io_size} 1 0.5 ${num_threads} ${cache_ratio} ${cache_page_size} \
                                --gtest_filter=FamPutGet.${op_type} &>> \
                                log_${cache_type}_${op_type}_${access_pattern}_${cache_ratio}_${cache_page_size}.txt
                        elif [[ $access_pattern = "Zipf099" ]]
                        then
                            srun --nodelist=gtfama6 -p gtfamA numactl --membind=0 \
                                $BUILD_DIR/test/microbench/fam-api-mb/fam_microbenchmark_datapath2 \
                                ${io_size} 1 0.99 ${num_threads} ${cache_ratio} ${cache_page_size} \
                                --gtest_filter=FamPutGet.${op_type} &>> \
                                log_${cache_type}_${op_type}_${access_pattern}_${cache_ratio}_${cache_page_size}.txt
                        else
                            srun --nodelist=gtfama6 -p gtfamA numactl --membind=0 \
                                $BUILD_DIR/test/microbench/fam-api-mb/fam_microbenchmark_datapath2 \
                                ${io_size} 0 0 ${num_threads} ${cache_ratio} ${cache_page_size} \
                                --gtest_filter=FamPutGet.${op_type} &>> \
                                log_${cache_type}_${op_type}_${access_pattern}_${cache_ratio}_${cache_page_size}.txt
                        fi

                        sleep 5
                        $BUILD_DIR/bin/openfam_adm --stop_service --clean --config_file_path=$CONFIG_OUT_DIR --install_path $INSTALL_DIR
                        sleep 5
                    done
                done
            done
        done
    done
done


####### Direct byte access
cache_type="direct_byte"
#for op_type in BlockingFamGetSingleRegionDataItem NonBlockingFamGetSingleRegionDataItem
for op_type in BlockingFamGetSingleRegionDataItem
do
    #for access_pattern in Sequential UnifRand Zipf05 Zipf099
    for access_pattern in UnifRand
    do
        for num_threads in 1 2 4 8
        do
            #for cache_ratio in 1.0 0.9 0.8 0.7 0.6 0.5 0.4 0.3 0.2 0.1
            for cache_ratio in 0.0
            do
                printf "${cache_type} ${op_type} ${access_pattern} ${num_threads} ${cache_ratio}\n" &>> log_${cache_type}_${op_type}_${access_pattern}_${cache_ratio}.txt
                for io_size in 64 128 256 512 1024 2048 4096 8192 16384 32768 65536 131072 262144 524288 1048576 2097152
                do
                    cache_page_size=${io_size}
                    #cache_page_size=4096

                    echo "========s======================================================"
                    echo "${cache_type}, ${op_type}, ${access_pattern}, ${io_size}, ${num_threads}, ${cache_ratio}, ${cache_page_size}"
                    echo "==============================================================="

                    $BUILD_DIR/bin/openfam_adm --start_service --config_file_path=$CONFIG_OUT_DIR --install_path $INSTALL_DIR
                    sleep 5
    
                    if [[ $access_pattern = "UnifRand" ]]
                    then
                        srun --nodelist=gtfama6 -p gtfamA numactl --membind=0 \
                            $BUILD_DIR/test/microbench/fam-api-mb/fam_microbenchmark_datapath2 \
                            ${io_size} 1 0 ${num_threads} ${cache_ratio} ${cache_page_size} \
                            --gtest_filter=FamPutGet.${op_type} &>> \
                            log_${cache_type}_${op_type}_${access_pattern}_${cache_ratio}.txt
                    elif [[ $access_pattern = "Zipf05" ]]
                    then
                        srun --nodelist=gtfama6 -p gtfamA numactl --membind=0 \
                            $BUILD_DIR/test/microbench/fam-api-mb/fam_microbenchmark_datapath2 \
                            ${io_size} 1 0.5 ${num_threads} ${cache_ratio} ${cache_page_size} \
                            --gtest_filter=FamPutGet.${op_type} &>> \
                            log_${cache_type}_${op_type}_${access_pattern}_${cache_ratio}.txt
                    elif [[ $access_pattern = "Zipf099" ]]
                    then
                        srun --nodelist=gtfama6 -p gtfamA numactl --membind=0 \
                            $BUILD_DIR/test/microbench/fam-api-mb/fam_microbenchmark_datapath2 \
                            ${io_size} 1 0.99 ${num_threads} ${cache_ratio} ${cache_page_size} \
                            --gtest_filter=FamPutGet.${op_type} &>> \
                            log_${cache_type}_${op_type}_${access_pattern}_${cache_ratio}.txt
                    else
                        srun --nodelist=gtfama6 -p gtfamA numactl --membind=0 \
                            $BUILD_DIR/test/microbench/fam-api-mb/fam_microbenchmark_datapath2 \
                            ${io_size} 0 0 ${num_threads} ${cache_ratio} ${cache_page_size} \
                            --gtest_filter=FamPutGet.${op_type} &>> \
                            log_${cache_type}_${op_type}_${access_pattern}_${cache_ratio}.txt
                    fi

                    sleep 5
                    $BUILD_DIR/bin/openfam_adm --stop_service --clean --config_file_path=$CONFIG_OUT_DIR --install_path $INSTALL_DIR
                    sleep 5
                done
            done
        done
    done
done



cmake .. -DLIBFABRIC_PATH=/opt/cray/libfabric/1.15.2.0 -DENABLE_LOCAL_CACHE=ON; make -j; make install
if [[ $? > 0 ]]
then
        echo "OpenFAM build with memoryserver version failed.. exit..."
        exit 1
fi

sleep 5

####### Caching page access
cache_type="cache_page"
##for cache_page_size in 64 128 256 512 1024 2048 4096 8192 16384 32768 65536 131072 262144 524288 1048576 2097152
for cache_page_size in 4096 65536
do
    #for op_type in BlockingFamGetSingleRegionDataItem NonBlockingFamGetSingleRegionDataItem
    for op_type in NonBlockingFamGetSingleRegionDataItem
    do
        #for access_pattern in Sequential UnifRand Zipf05 Zipf099
        for access_pattern in UnifRand
        do
            for num_threads in 1 2 4 8
            do
                #for cache_ratio in 1.0 0.9 0.8 0.7 0.6 0.5 0.4 0.3 0.2 0.1
                for cache_ratio in 1.0 0.9 0.5 0.1
                do
                    printf "${cache_type} ${op_type} ${access_pattern} ${num_threads} ${cache_ratio}\n" \
                        &>> log_${cache_type}_${op_type}_${access_pattern}_${cache_ratio}_${cache_page_size}.txt
                    for io_size in 64 128 256 512 1024 2048 4096 8192 16384 32768 65536 131072 262144 524288 1048576 2097152
                    do
                        #io_size=${cache_page_size}
                        #cache_page_size=${io_size}
                        #cache_page_size=4096
                        echo "========s======================================================"
                        echo "${cache_type}, ${op_type}, ${access_pattern}, ${io_size}, ${num_threads}, ${cache_ratio}, ${cache_page_size}"
                        echo "==============================================================="

                        $BUILD_DIR/bin/openfam_adm --start_service --config_file_path=$CONFIG_OUT_DIR --install_path $INSTALL_DIR
                        sleep 5
    
                        if [[ $access_pattern = "UnifRand" ]]
                        then
                            srun --nodelist=gtfama6 -p gtfamA numactl --membind=0 \
                                $BUILD_DIR/test/microbench/fam-api-mb/fam_microbenchmark_datapath2 \
                                ${io_size} 1 0 ${num_threads} ${cache_ratio} ${cache_page_size} \
                                --gtest_filter=FamPutGet.${op_type} &>> \
                                log_${cache_type}_${op_type}_${access_pattern}_${cache_ratio}_${cache_page_size}.txt
                        elif [[ $access_pattern = "Zipf05" ]]
                        then
                            srun --nodelist=gtfama6 -p gtfamA numactl --membind=0 \
                                $BUILD_DIR/test/microbench/fam-api-mb/fam_microbenchmark_datapath2 \
                                ${io_size} 1 0.5 ${num_threads} ${cache_ratio} ${cache_page_size} \
                                --gtest_filter=FamPutGet.${op_type} &>> \
                                log_${cache_type}_${op_type}_${access_pattern}_${cache_ratio}_${cache_page_size}.txt
                        elif [[ $access_pattern = "Zipf099" ]]
                        then
                            srun --nodelist=gtfama6 -p gtfamA numactl --membind=0 \
                                $BUILD_DIR/test/microbench/fam-api-mb/fam_microbenchmark_datapath2 \
                                ${io_size} 1 0.99 ${num_threads} ${cache_ratio} ${cache_page_size} \
                                --gtest_filter=FamPutGet.${op_type} &>> \
                                log_${cache_type}_${op_type}_${access_pattern}_${cache_ratio}_${cache_page_size}.txt
                        else
                            srun --nodelist=gtfama6 -p gtfamA numactl --membind=0 \
                                $BUILD_DIR/test/microbench/fam-api-mb/fam_microbenchmark_datapath2 \
                                ${io_size} 0 0 ${num_threads} ${cache_ratio} ${cache_page_size} \
                                --gtest_filter=FamPutGet.${op_type} &>> \
                                log_${cache_type}_${op_type}_${access_pattern}_${cache_ratio}_${cache_page_size}.txt
                        fi

                        sleep 5
                        $BUILD_DIR/bin/openfam_adm --stop_service --clean --config_file_path=$CONFIG_OUT_DIR --install_path $INSTALL_DIR
                        sleep 5
                    done
                done
            done
        done
    done
done


####### Caching byte access
cache_type="cache_byte"
#for op_type in BlockingFamGetSingleRegionDataItem NonBlockingFamGetSingleRegionDataItem
for op_type in BlockingFamGetSingleRegionDataItem
do
    #for access_pattern in Sequential UnifRand Zipf05 Zipf099
    for access_pattern in UnifRand
    do
        for num_threads in 1 2 4 8
        do
            #for cache_ratio in 1.0 0.9 0.8 0.7 0.6 0.5 0.4 0.3 0.2 0.1
            for cache_ratio in 1.0 0.9 0.5 0.1
            do
                printf "${cache_type} ${op_type} ${access_pattern} ${num_threads} ${cache_ratio}\n" &>> log_${cache_type}_${op_type}_${access_pattern}_${cache_ratio}.txt
                for io_size in 64 128 256 512 1024 2048 4096 8192 16384 32768 65536 131072 262144 524288 1048576 2097152
                do
                    cache_page_size=${io_size}
                    #cache_page_size=4096

                    echo "========s======================================================"
                    echo "${cache_type}, ${op_type}, ${access_pattern}, ${io_size}, ${num_threads}, ${cache_ratio}, ${cache_page_size}"
                    echo "==============================================================="

                    $BUILD_DIR/bin/openfam_adm --start_service --config_file_path=$CONFIG_OUT_DIR --install_path $INSTALL_DIR
                    sleep 5
    
                    if [[ $access_pattern = "UnifRand" ]]
                    then
                        srun --nodelist=gtfama6 -p gtfamA numactl --membind=0 \
                            $BUILD_DIR/test/microbench/fam-api-mb/fam_microbenchmark_datapath2 \
                            ${io_size} 1 0 ${num_threads} ${cache_ratio} ${cache_page_size} \
                            --gtest_filter=FamPutGet.${op_type} &>> \
                            log_${cache_type}_${op_type}_${access_pattern}_${cache_ratio}.txt
                    elif [[ $access_pattern = "Zipf05" ]]
                    then
                        srun --nodelist=gtfama6 -p gtfamA numactl --membind=0 \
                            $BUILD_DIR/test/microbench/fam-api-mb/fam_microbenchmark_datapath2 \
                            ${io_size} 1 0.5 ${num_threads} ${cache_ratio} ${cache_page_size} \
                            --gtest_filter=FamPutGet.${op_type} &>> \
                            log_${cache_type}_${op_type}_${access_pattern}_${cache_ratio}.txt
                    elif [[ $access_pattern = "Zipf099" ]]
                    then
                        srun --nodelist=gtfama6 -p gtfamA numactl --membind=0 \
                            $BUILD_DIR/test/microbench/fam-api-mb/fam_microbenchmark_datapath2 \
                            ${io_size} 1 0.99 ${num_threads} ${cache_ratio} ${cache_page_size} \
                            --gtest_filter=FamPutGet.${op_type} &>> \
                            log_${cache_type}_${op_type}_${access_pattern}_${cache_ratio}.txt
                    else
                        srun --nodelist=gtfama6 -p gtfamA numactl --membind=0 \
                            $BUILD_DIR/test/microbench/fam-api-mb/fam_microbenchmark_datapath2 \
                            ${io_size} 0 0 ${num_threads} ${cache_ratio} ${cache_page_size} \
                            --gtest_filter=FamPutGet.${op_type} &>> \
                            log_${cache_type}_${op_type}_${access_pattern}_${cache_ratio}.txt
                    fi

                    sleep 5
                    $BUILD_DIR/bin/openfam_adm --stop_service --clean --config_file_path=$CONFIG_OUT_DIR --install_path $INSTALL_DIR
                    sleep 5
                done
            done
        done
    done
done


cd $CURRENTDIR
