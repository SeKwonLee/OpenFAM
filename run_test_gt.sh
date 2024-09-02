#!/bin/bash
CURRENTDIR=`pwd`
BUILD_DIR=$CURRENTDIR/build
INSTALL_DIR=$BUILD_DIR
CONFIG_OUT_DIR=$BUILD_DIR/config
export OPENFAM_ROOT=$CONFIG_OUT_DIR

$BUILD_DIR/bin/openfam_adm --start_service --config_file_path=$CONFIG_OUT_DIR --install_path $INSTALL_DIR

sleep 5
$BUILD_DIR/bin/openfam_adm --stop_service --clean --config_file_path=$CONFIG_OUT_DIR --install_path $INSTALL_DIR
sleep 5

#for op_type in BlockingFamGetSingleRegionDataItem BlockingFamPutSingleRegionDataItem BlockingFamGetMultipleRegionDataItem BlockingFamPutMultipleRegionDataItem
#do
#    for access_pattern in Sequential Random
#    do
#        for num_threads in 1 2 4 8
#        do
#            printf "${op_type} ${access_pattern} ${num_threads}\n" &>> log_${op_type}.txt
#            for io_size in 64 128 256 512 1024 2048 4096 8192 16384 32768 65536 131072 262144 524288 1048576 2097152
#            do
#                echo "==============================================================="
#                echo "${op_type}, ${access_pattern}, ${io_size}, ${num_threads}"
#                echo "==============================================================="
#
#                openfam_adm --start_service --config_file_path=$CONFIG_OUT_DIR
#
#                if [[ $access_pattern = "Random" ]]
#                then
#                    srun --nodelist=gtnode8 -p gtnodes numactl --cpunodebind=0 --membind=0 /home/leesek/projects/OpenFAM/test/microbench/fam-api-mb/fam_example ${io_size} 1 ${num_threads} --gtest_filter=FamPutGet.${op_type} &>> log_${op_type}.txt
#                else
#                    srun --nodelist=gtnode8 -p gtnodes numactl --cpunodebind=0 --membind=0 /home/leesek/projects/OpenFAM/test/microbench/fam-api-mb/fam_example ${io_size} 0 ${num_threads} --gtest_filter=FamPutGet.${op_type} &>> log_${op_type}.txt
#                fi
#
#                sleep 5
#                openfam_adm --stop_service --clean 
#                sleep 5
#            done
#        done
#    done
#done
