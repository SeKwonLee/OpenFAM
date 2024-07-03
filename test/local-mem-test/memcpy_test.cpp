#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <emmintrin.h>
#include <numa.h>
#include <sched.h>
#include <math.h>
#include <assert.h>
#include <unistd.h>

#include <chrono>
#include <random>
#include <thread>
#include <algorithm>

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

double get_base(unsigned N, double skew) {                                                                   
    double base = 0;                                                                                         
    for (unsigned k = 1; k <= N; k++) {
        base += pow(k, -1 * skew);                                                                           
    }                                                                                                        
    return (1 / base);                                                                                       
}

int sample(int n, unsigned &seed, double base,
        double *sum_probs) {
    double z;           // Uniform random number (0 < z < 1)
    int zipf_value = 0; // Computed exponential value to be returned
    int low, high, mid; // Binary-search bounds

    // Pull a uniform random number (0 < z < 1)
    do {
        z = rand_r(&seed) / static_cast<double>(RAND_MAX);
    } while ((z == 0) || (z == 1));

    // Map z to the value
    low = 1, high = n;

    do {
        mid = (int)floor((low + high) / 2);
        if (sum_probs[mid] >= z && sum_probs[mid - 1] < z) {
            zipf_value = mid;
            break;
        } else if (sum_probs[mid] >= z) {
            high = mid - 1;
        } else {
            low = mid + 1;
        }
    } while (low <= high);

    // Assert that zipf_value is between 1 and N
    assert((zipf_value >= 1) && (zipf_value <= n));

    return zipf_value;
}


typedef struct {
    uint64_t tid;
    uint64_t num_ops;
    uint64_t *indexes;
} ThreadInfo;

void pinThreadToNode(int nodeId) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);

    // Get the number of CPUs in the system
    int numCpus = sysconf(_SC_NPROCESSORS_ONLN);

    // Add all CPUs of the given NUMA node to the CPU set
    for (int i = 0; i < numCpus; ++i) {
        if (numa_node_of_cpu(i) == nodeId) {
            CPU_SET(i, &cpuset);
        }
    }

    // Set the affinity for the current thread
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
}

int main(int argc, char **argv) {
    if (argc != 7) {
        printf("Usage: ./test <total memory size (MB)> <access size (Bytes)> <isRand: 0 or 1> <zipf: 0 - Uniform> <num threads> <numa_alloc: 0 or 1>\n");
        return 0;
    }

    uint64_t total_memory_size = std::stoul(std::string(argv[1])) * 1024UL * 1024UL;
    uint64_t access_size = std::stoul(std::string(argv[2]));     // in bytes
    int isRand = atoi(argv[3]);
    double zipf = std::stod(argv[4]);
    uint64_t num_threads = std::stoul(std::string(argv[5]));
    int do_numa_alloc = atoi(argv[6]);

    void *dest_buf = NULL;
    void *src_buf = NULL;
    if (do_numa_alloc) {
        dest_buf = numa_alloc_onnode(LARGEST_BUF_SIZE, 0);
        memset(dest_buf, 0, LARGEST_BUF_SIZE);
        src_buf = numa_alloc_onnode(total_memory_size, 1);
        memset(src_buf, 1, total_memory_size);
    } else {
        dest_buf = numa_alloc_onnode(LARGEST_BUF_SIZE, 0);
        memset(dest_buf, 0, LARGEST_BUF_SIZE);
        src_buf = numa_alloc_onnode(total_memory_size, 0);
        memset(src_buf, 1, total_memory_size);
    }

    clflush((char *)dest_buf, LARGEST_BUF_SIZE, true, true);
    clflush((char *)src_buf, total_memory_size, true, true);

    uint64_t num_ios = total_memory_size / access_size;
    uint64_t num_indexes = num_ios;

    double base = 0;
    unsigned seed = 0;

    std::vector<uint64_t> uniform_random_indexes;

    double *sum_probs = (double *) calloc(1, sizeof(double) * num_indexes);

    if (zipf > 0) {
        base = get_base((unsigned)num_indexes, zipf);
        sum_probs[0] = 0;

        seed = (unsigned)time(NULL);

        for (unsigned i = 1; i <= num_indexes; i++) {
            sum_probs[i] = sum_probs[i - 1] + base / pow((double)i, zipf);
        }
    } else if (isRand) {
        // Generate random indexes
        std::srand(unsigned(std::time(0)));
        for (uint64_t i = 0; i < num_indexes; i++) {
            uniform_random_indexes.push_back(i);
        }
        std::random_shuffle(uniform_random_indexes.begin(), uniform_random_indexes.end());
    }

    ThreadInfo *infos = (ThreadInfo *)malloc(sizeof(ThreadInfo) * num_threads); 

    for (uint64_t i = 0; i < num_threads; i++) {
        infos[i].tid = i;
        infos[i].num_ops = num_indexes / num_threads;
        infos[i].indexes = (uint64_t *) calloc(1, sizeof(uint64_t) * infos[i].num_ops);

        if (isRand) {
            if (zipf > 0) {
                for (uint64_t j = 0; j < infos[i].num_ops; j++) {
                    infos[i].indexes[j] = (uint64_t) sample((int)num_ios, seed, base, sum_probs);
                    infos[i].indexes[j] -= 1;
                }
            } else {
                // Uniform random
                for (uint64_t j = 0; j < infos[i].num_ops; j++) {
                    infos[i].indexes[j] = uniform_random_indexes.back();
                    uniform_random_indexes.pop_back();
                }
            }
        } else {
            // Sequential
            uint64_t start_idx = infos[i].num_ops * infos[i].tid;
            for (uint64_t j = 0; j < infos[i].num_ops; j++) {
                infos[i].indexes[j] = start_idx + j;
            }
        }
    }

    auto memcpy_bench_func = [&](int tid) {
        pinThreadToNode(0);
        uint64_t index = 0;
        for (uint64_t i = 0; i < infos[tid].num_ops; i++) {
            index = infos[tid].indexes[i];
            memcpy(dest_buf, (char *)((uint64_t)src_buf + (index * access_size)), access_size);
        }
    };

    std::vector<std::thread> thread_group;

    auto starttime = std::chrono::system_clock::now();
    for (int i = 0; i < num_threads; i++)
        thread_group.emplace_back(memcpy_bench_func, i);

    for (int i = 0; i < num_threads; i++) {
        if (thread_group[i].joinable())
            thread_group[i].join();
    }
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now() - starttime);
    printf("Throughput: %f GB/sec\n", ((double)total_memory_size / (1024 * 1024 * 1024)) / ((double)duration.count() / 1000000.0));

    numa_free(dest_buf, LARGEST_BUF_SIZE);
    numa_free(src_buf, total_memory_size);
    free(sum_probs);

    for (int i = 0; i < num_threads; i++) {
        free(infos[i].indexes);
    }
    free(infos);
    
    return 0;
}
