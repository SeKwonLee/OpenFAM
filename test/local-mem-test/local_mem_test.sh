#!/bin/bash

# Usage: ./test <total memory size (MB)> <access size (Bytes)> <isRand: 0 or 1> <zipf: 0 - Uniform> <num threads> <numa_alloc: 0 or 1>

pattern="zipf-0.99"
for numThreads in 1 2 4 8
do
    for access_size in 64 128 256 512 1024 2048 4096 8192 16384 32768 65536 131072 262144 524288 1048576 2097152
    do
        ./memcpy_test 8192 ${access_size} 1 0.99 ${numThreads} 0 &>> log-single-numa-${pattern}.txt
        #numactl --cpunodebind=0 ./memcpy_test 8192 ${access_size} 1 0.99 ${numThreads} 0 &>> log-${pattern}.txt
    done
    printf "\n\n" &>> log-single-numa-${pattern}.txt
done

pattern="uniform"
for numThreads in 1 2 4 8
do
    for access_size in 64 128 256 512 1024 2048 4096 8192 16384 32768 65536 131072 262144 524288 1048576 2097152
    do
        ./memcpy_test 8192 ${access_size} 1 0 ${numThreads} 0 &>> log-single-numa-${pattern}.txt
        #numactl --cpunodebind=0 ./memcpy_test 8192 ${access_size} 1 0 ${numThreads} 0 &>> log-${pattern}.txt
    done
    printf "\n\n" &>> log-single-numa-${pattern}.txt
done


pattern="zipf-0.99"
for numThreads in 1 2 4 8
do
    for access_size in 64 128 256 512 1024 2048 4096 8192 16384 32768 65536 131072 262144 524288 1048576 2097152
    do
        ./memcpy_test_interleave 8192 ${access_size} 1 0.99 ${numThreads} 0 &>> log-multi-numa-cpu-mem-interleave-${pattern}.txt
    done
    printf "\n\n" &>> log-multi-numa-cpu-mem-interleave-${pattern}.txt
done

pattern="uniform"
for numThreads in 1 2 4 8
do
    for access_size in 64 128 256 512 1024 2048 4096 8192 16384 32768 65536 131072 262144 524288 1048576 2097152
    do
        ./memcpy_test_interleave 8192 ${access_size} 1 0 ${numThreads} 0 &>> log-multi-numa-cpu-mem-interleave-${pattern}.txt
    done
    printf "\n\n" &>> log-multi-numa-cpu-mem-interleave-${pattern}.txt
done


pattern="zipf-0.99"
for numThreads in 1 2 4 8
do
    for access_size in 64 128 256 512 1024 2048 4096 8192 16384 32768 65536 131072 262144 524288 1048576 2097152
    do
        ./memcpy_test_interleave 8192 ${access_size} 1 0.99 ${numThreads} 1 &>> log-multi-numa-cpu-interleave-${pattern}.txt
    done
    printf "\n\n" &>> log-multi-numa-cpu-interleave-${pattern}.txt
done

pattern="uniform"
for numThreads in 1 2 4 8
do
    for access_size in 64 128 256 512 1024 2048 4096 8192 16384 32768 65536 131072 262144 524288 1048576 2097152
    do
        ./memcpy_test_interleave 8192 ${access_size} 1 0 ${numThreads} 1 &>> log-multi-numa-cpu-interleave-${pattern}.txt
    done
    printf "\n\n" &>> log-multi-numa-cpu-interleave-${pattern}.txt
done
