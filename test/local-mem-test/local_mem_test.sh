#!/bin/bash

# Usage: ./test <total memory size (MB)> <access size (Bytes)> <isRand: 0 or 1> <zipf: 0 - Uniform> <num threads> <numa_alloc: 0 or 1>

pattern="zipf-0.99"
for numThreads in 1 2 4 8
do
    for access_size in 64 128 256 512 1024 2048 4096 8192 16384 32768 65536 131072 262144 524288 1048576 2097152
    do
        sudo ./memcpy_test 8192 ${access_size} 1 0.99 ${numThreads} 0 1 /mnt/hugetlbfs/ &>> log-huge-${pattern}.txt
        #numactl --cpunodebind=0 ./memcpy_test 8192 ${access_size} 1 0.99 ${numThreads} 0 &>> log-${pattern}.txt
        sudo rm /mnt/hugetlbfs/*
    done
    printf "\n\n" &>> log-huge-${pattern}.txt
done

pattern="uniform"
for numThreads in 1 2 4 8
do
    for access_size in 64 128 256 512 1024 2048 4096 8192 16384 32768 65536 131072 262144 524288 1048576 2097152
    do
        sudo ./memcpy_test 8192 ${access_size} 1 0 ${numThreads} 0 1 /mnt/hugetlbfs/ &>> log-huge-${pattern}.txt
        #numactl --cpunodebind=0 ./memcpy_test 8192 ${access_size} 1 0 ${numThreads} 0 &>> log-${pattern}.txt
        sudo rm /mnt/hugetlbfs/*
    done
    printf "\n\n" &>> log-huge-${pattern}.txt
done
