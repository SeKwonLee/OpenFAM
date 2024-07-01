#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <emmintrin.h>
#include <numa.h>

#include <chrono>

#define CACHE_LINE_SIZE     64
#define LARGEST_BUF_SIZE    2*1024*1024UL

static inline void mfence() {                                                                                
    asm volatile("sfence":::"memory");                                                                       
}                                                                                                            

static inline void clflush(char *data, int len, bool front, bool back)                                       
{                                                                                                            
    volatile char *ptr = (char *)((unsigned long)data &~(CACHE_LINE_SIZE-1));                                
    if (front)                                                                                               
        mfence();                                                                                            
    for(; ptr<data+len; ptr+=CACHE_LINE_SIZE){                                                               
        asm volatile("clflush %0" : "+m" (*(volatile char *)ptr));                                           
    }                                                                                                        
    if (back)                                                                                                
        mfence();                                                                                            
}

int main(int argc, char **argv) {
    if (argc != 4) {
        printf("Usage: ./test <total memory size (MB)> <access size> <numa_alloc>\n");
        return 0;
    }

    uint64_t total_memory_size = (uint64_t)atoi(argv[1]) * 1024UL;
    uint64_t access_size = (uint64_t)atoi(argv[2]);     // in bytes
    int do_numa_alloc = atoi(argv[3]);

    //void *arr1 = calloc(1, total_memory_size);
    //void *arr2 = calloc(1, total_memory_size);

    // Bind the current process to the specified NUMA node
    int node = 0;
    ret = numa_run_on_node(node);
    if (ret != 0) {
        fprintf(stderr, "Failed to bind to NUMA node %d\n", node);
        return 1;
    }

    // Check which node the process is running on
    int current_node = numa_node_of_cpu(sched_getcpu());
    printf("Process is running on NUMA node %d\n", current_node);

    if (do_numa_alloc) {
        void *arr1 = numa_alloc_onnode(LARGEST_BUF_SIZE, 0);
        memset(arr1, 0, LARGEST_BUF_SIZE);
        void *arr2 = numa_alloc_onnode(total_memory_size, 1);
        memset(arr2, 0, total_memory_size);
    } else {
        void *arr1 = numa_alloc_onnode(LARGEST_BUF_SIZE, 0);
        memset(arr1, 0, LARGEST_BUF_SIZE);
        void *arr2 = numa_alloc_onnode(total_memory_size, 0);
        memset(arr2, 0, total_memory_size);
    }

    clflush((char *)arr1, LARGEST_BUF_SIZE, true, true);
    clflush((char *)arr2, total_memory_size, true, true);

    auto starttime = std::chrono::system_clock::now();
    memcpy(arr1, arr2, total_memory_size);
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now() - starttime);
    printf("Throughput (A to B): %f GB/sec\n", ((double)total_memory_size / (1024 * 1024 * 1024)) / ((double)duration.count() / 1000000.0));

    clflush((char *)arr1, LARGEST_BUF_SIZE, true, true);
    clflush((char *)arr2, total_memory_size, true, true);

    starttime = std::chrono::system_clock::now();
    memcpy(arr2, arr1, total_memory_size);
    duration = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now() - starttime);
    printf("Throughput (B to A): %f GB/sec\n", ((double)total_memory_size / (1024 * 1024 * 1024)) / ((double)duration.count() / 1000000.0));

    return 0;
}
