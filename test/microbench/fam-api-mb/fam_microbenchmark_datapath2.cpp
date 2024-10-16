/*
 * fam_microbenchmark_datapath2.cpp
 * Copyright (c) 2019 Hewlett Packard Enterprise Development, LP. All rights
 * reserved. Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 *    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * See https://spdx.org/licenses/BSD-3-Clause
 *
 */

#include <fam/fam_exception.h>
#include <gtest/gtest.h>
#include <iostream>
#include <random>
#include <algorithm>
#include <ctime>
#include <chrono>

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <sys/mman.h>
#include <numa.h>

#include "cis/fam_cis_client.h"
#include "cis/fam_cis_direct.h"
#include "common/fam_libfabric.h"
#include <fam/fam.h>

#include "common/fam_test_config.h"
#define ALL_PERM 0777
#define BIG_REGION_SIZE 34359738368
using namespace std;
using namespace openfam;

int *myPE;
Fam_CIS *cis;
fam *my_fam;
Fam_Options fam_opts;
Fam_Descriptor *item;
Fam_Region_Descriptor *desc;
mode_t test_perm_mode;
size_t test_item_size;

int64_t *gLocalBuf = NULL;
uint64_t gItemSize = 8 * 1024 * 1024 * 1024ULL;
uint64_t gDataSize = 256;

int isRand = 0;
double zipf = 0.0;
uint64_t numThreads = 1;
uint64_t max_num_outstanding_requests = 1;

fam_context **ctx;

#define LOCAL_BUFFER_SIZE   2*1024*1024UL   // 2MB

#include "cache.hpp"
#include "lru_cache_policy.hpp"

void *gCacheBuf = NULL;

template <typename Key, typename Value>
using lru_cache_t = typename caches::fixed_sized_cache<Key, Value, caches::LRUCachePolicy>;
double cache_ratio = 0.0;
uint64_t cache_page_size = 4096;

std::vector<std::vector<uint64_t>> op_latency_maps;

struct ValueInfo {
    Fam_Descriptor *item;
    uint64_t tid;
    uint64_t num_ops;
    std::vector<uint64_t> indexes;
    lru_cache_t<uint64_t, void *> *cache;
    void *cache_buf;
    uint64_t num_cache_hit;
    uint64_t num_cache_miss;
};

#ifdef MEMSERVER_PROFILE
#define RESET_PROFILE()                                                        \
    {                                                                          \
        cis->reset_profile();                                                  \
        my_fam->fam_barrier_all();                                             \
    }

#define GENERATE_PROFILE()                                                     \
    {                                                                          \
        if (*myPE == 0)                                                        \
            cis->dump_profile();                                               \
        my_fam->fam_barrier_all();                                             \
    }
#else
#define RESET_PROFILE()
#define GENERATE_PROFILE()
#endif

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

void pinThreadToCore(int core_id) {
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(core_id, &cpuset);

	int result = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
	if (result != 0) {
		fprintf(stderr, "Error setting thread affinity: %d\n", result);
	}
}

void *func_blocking_put_single_region_item(void *arg) {
    ValueInfo *it = (ValueInfo *)arg;
    pinThreadToCore((int)it->tid);

    uint64_t offset = 0;
    void *LocalBuf = (void *)((uint64_t)gLocalBuf + (it->tid * LOCAL_BUFFER_SIZE));

    for (uint64_t i = 0; i < it->num_ops; i++) {
        offset = it->indexes[i] * gDataSize;
        ctx[it->tid]->fam_put_blocking(LocalBuf, it->item, offset, gDataSize);
    }

    pthread_exit(NULL);
}

void *func_blocking_get_single_region_item(void *arg) {
    ValueInfo *it = (ValueInfo *)arg;
    pinThreadToCore((int)it->tid);

    uint64_t offset = 0;
    void *LocalBuf = (void *)((uint64_t)gLocalBuf + (it->tid * LOCAL_BUFFER_SIZE));

    for (uint64_t i = 0; i < it->num_ops; i++) {
        offset = it->indexes[i] * gDataSize;
        ctx[it->tid]->fam_get_blocking(LocalBuf, it->item, offset, gDataSize);
    }

    pthread_exit(NULL);
}

void *func_non_blocking_get_single_region_item(void *arg) {
    ValueInfo *it = (ValueInfo *)arg;
    pinThreadToCore((int)it->tid);

    uint64_t offset = 0;
    void *LocalBuf = (void *)((uint64_t)gLocalBuf + (it->tid * LOCAL_BUFFER_SIZE));

    for (uint64_t i = 0; i < it->num_ops; i++) {
        offset = it->indexes[i] * gDataSize;
        ctx[it->tid]->fam_get_nonblocking(LocalBuf, it->item, offset, gDataSize);
    }
    ctx[it->tid]->fam_quiet();

    pthread_exit(NULL);
}

uint64_t offset_to_start_page_index(uint64_t byte_offset) {
    return (uint64_t)(byte_offset / cache_page_size);
}

uint64_t offset_to_end_page_index(uint64_t byte_offset) {
    if (byte_offset % cache_page_size == 0) {
        return (uint64_t)(byte_offset / cache_page_size) - 1;
    } else {
        return (uint64_t)(byte_offset / cache_page_size);
    }
}

uint64_t page_index_to_page_offset(uint64_t page_index) {
    return (page_index * cache_page_size);
}

uint64_t page_index_to_page_local_offset(uint64_t byte_offset, uint64_t page_index) {
    return (byte_offset - (page_index * cache_page_size));
}

void *func_cache_get_single_region_item_warmup(void *arg) {
    ValueInfo *it = (ValueInfo *)arg;
    pinThreadToCore((int)it->tid);

    uint64_t max_io_index = *std::max_element(it->indexes.begin(), it->indexes.end());
    uint64_t min_io_index = *std::min_element(it->indexes.begin(), it->indexes.end());

    uint64_t start_page_index, end_page_index;
    start_page_index = offset_to_start_page_index(min_io_index * gDataSize);
    end_page_index = offset_to_end_page_index((max_io_index * gDataSize) + gDataSize);

    std::vector<uint64_t> indexes;
    for (uint64_t idx = start_page_index; idx <= end_page_index; idx++) {
        indexes.push_back(idx);
    }

    // Shuffle indexes
    std::srand(unsigned(std::time(0)));
    std::random_shuffle(indexes.begin(), indexes.end());

    uint64_t index, thread_local_page_start_addr;
    for (uint64_t i = 0; i < indexes.size(); i++) {
        index = indexes[i];
#ifdef ENABLE_FULL_CACHE_MISS_SIMULATION
        index += (gItemSize / cache_page_size);
#endif
        thread_local_page_start_addr = (uint64_t)it->cache_buf + (index * cache_page_size);
        it->cache->Put(index, (void *)(thread_local_page_start_addr));
    }

    pthread_exit(NULL);
}

void *func_blocking_cache_get_single_region_item(void *arg) {
    ValueInfo *it = (ValueInfo *)arg;
    pinThreadToCore((int)it->tid);

    uint64_t byte_index = 0, start_byte_offset = 0, end_byte_offset = 0, 
             start_page_index = 0, end_page_index = 0, start_page_local_offset = 0;

    void *LocalBuf = (void *)((uint64_t)gLocalBuf + (it->tid * LOCAL_BUFFER_SIZE));

    for (uint64_t i = 0; i < it->num_ops; i++) {
#ifdef ENABLE_LATENCY_CHECK
        auto op_start = std::chrono::system_clock::now();
#endif
        byte_index = it->indexes[i];

        start_byte_offset = byte_index * gDataSize;
        end_byte_offset = start_byte_offset + gDataSize;

        start_page_index = offset_to_start_page_index(start_byte_offset);
        end_page_index = offset_to_end_page_index(end_byte_offset);

        start_page_local_offset = page_index_to_page_local_offset(start_byte_offset, start_page_index);

        uint64_t size_to_read = gDataSize, read_size = 0, thread_local_page_start_addr = 0;
        for (uint64_t index = start_page_index; index <= end_page_index; index++) {
#ifdef ENABLE_LOCAL_CACHE
            const auto cache_value = it->cache->TryGet(index);
            if (cache_value.second == true) {
                if (index == start_page_index) {
                    read_size = size_to_read > (cache_page_size - start_page_local_offset) ?
                        (cache_page_size - start_page_local_offset) : gDataSize;
                    memcpy(LocalBuf, (void *)((uint64_t)*cache_value.first.get() 
                                + start_page_local_offset), read_size);
                } else {
                    read_size = size_to_read > cache_page_size ? cache_page_size : size_to_read;
                    memcpy((void *)((uint64_t)LocalBuf + (gDataSize - size_to_read)),
                            *cache_value.first.get(), read_size);
                }
                it->num_cache_hit++;
            } else {
                thread_local_page_start_addr = (uint64_t)it->cache_buf + (index * cache_page_size);
                ctx[it->tid]->fam_get_blocking((void *)(thread_local_page_start_addr),
                        it->item, index * cache_page_size, cache_page_size);
                //it->cache->Put(index, (void *)(thread_local_page_start_addr));
                if (index == start_page_index) {
                    read_size = size_to_read > (cache_page_size - start_page_local_offset) ?
                        (cache_page_size - start_page_local_offset) : gDataSize;
                    memcpy(LocalBuf, (void *)(thread_local_page_start_addr + start_page_local_offset), read_size);
                } else {
                    read_size = size_to_read > cache_page_size ? cache_page_size : size_to_read;
                    memcpy((void *)((uint64_t)LocalBuf + (gDataSize - size_to_read)),
                            (void *)(thread_local_page_start_addr), read_size);
                }
                it->num_cache_miss++;
            }
#else
            thread_local_page_start_addr = (uint64_t)it->cache_buf + (index * cache_page_size);
            ctx[it->tid]->fam_get_blocking((void *)(thread_local_page_start_addr),
                    it->item, index * cache_page_size, cache_page_size);
            if (index == start_page_index) {
                read_size = size_to_read > (cache_page_size - start_page_local_offset) ?
                    (cache_page_size - start_page_local_offset) : gDataSize;
                memcpy(LocalBuf, (void *)(thread_local_page_start_addr + start_page_local_offset), read_size);
            } else {
                read_size = size_to_read > cache_page_size ? cache_page_size : size_to_read;
                memcpy((void *)((uint64_t)LocalBuf + (gDataSize - size_to_read)),
                        (void *)(thread_local_page_start_addr), read_size);
            }
#endif
            size_to_read -= read_size;
        }
#ifdef ENABLE_LATENCY_CHECK
        auto op_end = std::chrono::system_clock::now();
        uint64_t op_elapsed_time = std::chrono::duration_cast<std::chrono::nanoseconds>(op_end - op_start).count();
        op_latency_maps[it->tid].push_back(op_elapsed_time);
#endif
    }

    pthread_exit(NULL);
}

typedef struct PostProcessInfo {
    uint64_t index;
    void *index_src;
    void *copy_src;
    void *dest;
    uint64_t read_size;
} PostProcessInfo;

void *func_non_blocking_cache_get_single_region_item(void *arg) {
    ValueInfo *it = (ValueInfo *)arg;
    pinThreadToCore((int)it->tid);

    uint64_t byte_index = 0, start_byte_offset = 0, end_byte_offset = 0, 
             start_page_index = 0, end_page_index = 0, start_page_local_offset = 0;

    void *LocalBuf = (void *)((uint64_t)gLocalBuf + (it->tid * LOCAL_BUFFER_SIZE * (max_num_outstanding_requests + 1)));

    uint64_t num_outstanding_requests = 0, num_outstanding_ios = 0;
    std::vector<std::pair<std::shared_ptr<void *>, bool>> cache_values;

    std::vector<PostProcessInfo> PostProcessInfoArray;
    if (gDataSize < cache_page_size) {
        PostProcessInfoArray.reserve(max_num_outstanding_requests);
    } else {
        PostProcessInfoArray.reserve((size_t)(gDataSize/cache_page_size) * max_num_outstanding_requests);
    }

#ifdef ENABLE_LATENCY_CHECK
    auto op_start = std::chrono::system_clock::now();
    auto op_end = std::chrono::system_clock::now();
    uint64_t op_elapsed_time = 0;
    uint64_t op_count = 0;
#endif

    for (uint64_t i = 0; i < it->num_ops; i++) {
#ifdef ENABLE_LATENCY_CHECK
        op_count++;
#endif
        byte_index = it->indexes[i];

        start_byte_offset = byte_index * gDataSize;
        end_byte_offset = start_byte_offset + gDataSize;

        start_page_index = offset_to_start_page_index(start_byte_offset);
        end_page_index = offset_to_end_page_index(end_byte_offset);

        start_page_local_offset = page_index_to_page_local_offset(start_byte_offset, start_page_index);

        uint64_t size_to_read = gDataSize, read_size = 0;
        uint64_t thread_local_page_start_addr = 0, thread_local_buffer_addr = 0;
#ifdef ENABLE_LOCAL_CACHE
        for (uint64_t index = start_page_index; index <= end_page_index; index++) {
            const auto cache_value = it->cache->TryGet(index);
            cache_values.push_back(cache_value);
        }

        bool target = false;
        auto iter = std::find_if(cache_values.begin(), cache_values.end(), [target](const std::pair<std::shared_ptr<void *>, bool>& p) {
            return p.second == target;
        });

        if (iter != cache_values.end()) {
            for (uint64_t index = start_page_index; index <= end_page_index; index++) {
                thread_local_buffer_addr = (uint64_t)LocalBuf + ((num_outstanding_requests + 1) 
                    * LOCAL_BUFFER_SIZE) + ((index - start_page_index) * cache_page_size);
                thread_local_page_start_addr = (uint64_t)it->cache_buf + (index * cache_page_size);

                if (cache_values[index - start_page_index].second == true) {
                    if (index == start_page_index) {
                        read_size = size_to_read > (cache_page_size - start_page_local_offset) ?
                            (cache_page_size - start_page_local_offset) : gDataSize;
                        memcpy((void *)(thread_local_buffer_addr + start_page_local_offset),
                            (void *)((uint64_t)*cache_values[index - start_page_index].first.get() + start_page_local_offset), read_size);
                    } else {
                        read_size = size_to_read > cache_page_size ? cache_page_size : size_to_read;
                        memcpy((void *)(thread_local_buffer_addr), *cache_values[index - start_page_index].first.get(), read_size);
                    }
                    it->num_cache_hit++;
                } else {
                    PostProcessInfo ppi;
                    ppi.index = index;
                    ppi.index_src = (void *)(thread_local_page_start_addr);

                    ctx[it->tid]->fam_get_nonblocking((void *)(thread_local_buffer_addr),
                            it->item, page_index_to_page_offset(index), cache_page_size);

                    if (index == start_page_index) {
                        read_size = size_to_read > (cache_page_size - start_page_local_offset) ?
                            (cache_page_size - start_page_local_offset) : gDataSize;
                        ppi.copy_src = (void *)(thread_local_buffer_addr + start_page_local_offset);
                        ppi.dest = (void *)(thread_local_page_start_addr + start_page_local_offset);
                    } else {
                        read_size = size_to_read > cache_page_size ? cache_page_size : size_to_read;
                        ppi.copy_src = (void *)(thread_local_buffer_addr);
                        ppi.dest = (void *)(thread_local_page_start_addr);
                    }
                    ppi.read_size = read_size;
                    //PostProcessInfoArray.push_back(ppi);
                    num_outstanding_ios++;
                    it->num_cache_miss++;
                }
                size_to_read -= read_size;
            }
            num_outstanding_requests++;
        } else {
            for (uint64_t index = start_page_index; index <= end_page_index; index++) {
                thread_local_buffer_addr = (uint64_t)LocalBuf + ((index - start_page_index) * cache_page_size);
                thread_local_page_start_addr = (uint64_t)it->cache_buf + (index * cache_page_size);

                if (index == start_page_index) {
                    read_size = size_to_read > (cache_page_size - start_page_local_offset) ?
                        (cache_page_size - start_page_local_offset) : gDataSize;
                    memcpy((void *)(thread_local_buffer_addr + start_page_local_offset),
                        (void *)((uint64_t)*cache_values[index - start_page_index].first.get() + start_page_local_offset), read_size);
                } else {
                    read_size = size_to_read > cache_page_size ? cache_page_size : size_to_read;
                    memcpy((void *)(thread_local_buffer_addr), *cache_values[index - start_page_index].first.get(), read_size);
                }
                it->num_cache_hit++;

                size_to_read -= read_size;
            }
        }
        cache_values.clear();
#else
        thread_local_buffer_addr = (uint64_t)LocalBuf + (((i % max_num_outstanding_requests) + 1) * LOCAL_BUFFER_SIZE);
        read_size = size_to_read;

        ctx[it->tid]->fam_get_nonblocking((void *)(thread_local_buffer_addr),
                it->item, start_byte_offset, read_size);
        num_outstanding_ios++;
        num_outstanding_requests++;
#endif

        if (num_outstanding_requests == max_num_outstanding_requests) {
            if (num_outstanding_ios > 0) {
                ctx[it->tid]->fam_quiet();
                num_outstanding_ios = 0;
//#ifdef ENABLE_LOCAL_CACHE
//                for (auto &info : PostProcessInfoArray) {
//                    it->cache->Put(info.index, info.index_src);
//                    memcpy(info.dest, info.copy_src, info.read_size);
//                }
//                PostProcessInfoArray.clear();
//#endif
            }
            num_outstanding_requests = 0;

#ifdef ENABLE_LATENCY_CHECK
            op_end = std::chrono::system_clock::now();
            op_elapsed_time = std::chrono::duration_cast<std::chrono::nanoseconds>(op_end - op_start).count();
            op_latency_maps[it->tid].push_back(op_elapsed_time / op_count);
            op_start = std::chrono::system_clock::now();
            op_count = 0;
#endif
        }
    }

    if (num_outstanding_ios > 0) {
        ctx[it->tid]->fam_quiet();
//#ifdef ENABLE_LOCAL_CACHE
//        for (auto &info : PostProcessInfoArray) {
//            it->cache->Put(info.index, info.index_src);
//            memcpy(info.dest, info.copy_src, info.read_size);
//        }
//        PostProcessInfoArray.clear();
//#endif
    }

#ifdef ENABLE_LATENCY_CHECK
    if (op_count > 0) {
        op_end = std::chrono::system_clock::now();
        op_elapsed_time = std::chrono::duration_cast<std::chrono::nanoseconds>(op_end - op_start).count();
        op_latency_maps[it->tid].push_back(op_elapsed_time / op_count);
    }
#endif

    pthread_exit(NULL);
}

void *func_blocking_put_multiple_region_item(void *arg) {
    ValueInfo *it = (ValueInfo *)arg;
    pinThreadToCore((int)it->tid);

    uint64_t offset = 0, index = 0;
    uint64_t num_ios = gItemSize / gDataSize / numThreads;
    
    std::default_random_engine generator;
    std::uniform_int_distribution<uint64_t> distribution(0, num_ios - 1);

    if (isRand) {
        for (uint64_t i = 0; i < num_ios; i++) {
            index = distribution(generator);
            offset = index * gDataSize;
            my_fam->fam_put_blocking(gLocalBuf, it->item, offset, gDataSize);
        }
    } else {
        for (uint64_t i = 0; i < num_ios; i++) {
            offset = i * gDataSize;
            my_fam->fam_put_blocking(gLocalBuf, it->item, offset, gDataSize);
        }
    }

    pthread_exit(NULL);
}

void *func_blocking_get_multiple_region_item(void *arg) {
    ValueInfo *it = (ValueInfo *)arg;
    pinThreadToCore((int)it->tid);

    uint64_t offset = 0, index = 0;
    uint64_t num_ios = gItemSize / gDataSize / numThreads;

    std::default_random_engine generator;
    std::uniform_int_distribution<uint64_t> distribution(0, num_ios - 1);

    if (isRand) {
        for (uint64_t i = 0; i < num_ios; i++) {
            index = distribution(generator);
            offset = index * gDataSize;
            my_fam->fam_get_blocking(gLocalBuf, it->item, offset, gDataSize);
        }
    } else {
        for (uint64_t i = 0; i < num_ios; i++) {
            offset = i * gDataSize;
            my_fam->fam_get_blocking(gLocalBuf, it->item, offset, gDataSize);
        }
    }

    pthread_exit(NULL);
}

// Test case -  Blocking put test by multiple threads on same region and data item.
TEST(FamPutGet, BlockingFamPutSingleRegionDataItem) {
    uint64_t num_ios = gItemSize / gDataSize;
    pthread_t *threads = (pthread_t *)malloc(sizeof(pthread_t) * numThreads);
    int rc;

    Fam_Region_Descriptor **descs = (Fam_Region_Descriptor **)malloc(sizeof(Fam_Region_Descriptor *) * numThreads);
    Fam_Descriptor **items = (Fam_Descriptor **)malloc(sizeof(Fam_Descriptor *) * numThreads);

    ValueInfo *infos = new ValueInfo[numThreads];

    std::string regionPrefix("region");
    std::string itemPrefix("item");
    EXPECT_NO_THROW(desc = my_fam->fam_create_region(regionPrefix.c_str(),
                BIG_REGION_SIZE, 0777, NULL));
    EXPECT_NE((void *)NULL, desc);

    EXPECT_NO_THROW(item = my_fam->fam_allocate(itemPrefix.c_str(), gItemSize, 0777, desc));
    EXPECT_NE((void *)NULL, item);

    ctx = (fam_context **) malloc(sizeof(fam_context *) * numThreads);
    for (uint64_t i = 0; i < numThreads; i++) {
        ctx[i] = my_fam->fam_context_open();
    }

    for (uint64_t i = 0; i < numThreads; i++) {
        items[i] = item;
    }

    // Warm-up
    //int64_t *testBuf = (int64_t *)malloc(gItemSize);
    //memset(testBuf, 1, gItemSize);
    //my_fam->fam_put_blocking(testBuf, item, 0, gItemSize);
    //free(testBuf);
    //my_fam->fam_barrier_all();

    uint64_t num_indexes = num_ios;
    double base = 0;
    unsigned seed = 0;

    double *sum_probs = (double *) calloc(1, sizeof(double) * num_indexes);

    if (zipf > 0) {
        base = get_base((unsigned)num_indexes, zipf);
        sum_probs[0] = 0;

        for (unsigned i = 1; i <= num_indexes; i++) {
            sum_probs[i] = sum_probs[i - 1] + base / pow((double)i, zipf);
        }
    }

    for (uint64_t i = 0; i < numThreads; i++) {
        infos[i].item = items[i];
        infos[i].tid = i;
        infos[i].num_ops = num_indexes / numThreads;
        infos[i].indexes.reserve(infos[i].num_ops);

        if (isRand) {
            if (zipf > 0) {
                seed = (unsigned)time(NULL);
                seed += (unsigned)infos[i].tid;
                for (uint64_t j = 0; j < infos[i].num_ops; j++) {
                    infos[i].indexes[j] = (uint64_t) sample((int)num_ios, seed, base, sum_probs);
                    infos[i].indexes[j] -= 1;
                }
            } else {
                uint64_t start_idx = infos[i].num_ops * infos[i].tid;
                uint64_t end_idx = start_idx + infos[i].num_ops - 1;

                std::default_random_engine generator;
                std::uniform_int_distribution<uint64_t> distribution(start_idx, end_idx);
                for (uint64_t j = 0; j < infos[i].num_ops; j++) {
                    infos[i].indexes[j] = distribution(generator);
                }
            }
        } else {
            uint64_t start_idx = infos[i].num_ops * infos[i].tid;
            for (uint64_t j = 0; j < infos[i].num_ops; j++) {
                infos[i].indexes[j] = start_idx + j;
            }
        }
    }

    auto starttime = std::chrono::system_clock::now();
    for (uint64_t i = 0; i < numThreads; i++) {
        if ((rc = pthread_create(&threads[i], NULL, 
                        func_blocking_put_single_region_item, &infos[i]))) {
            fprintf(stderr, "error: pthread_create, rc: %d\n", rc);
            exit(1);
        }
    }

    for (uint64_t i = 0; i < numThreads; i++) {
        pthread_join(threads[i], NULL);
    }
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now() - starttime);
    printf("Elapsed time (us): %lu us\n", duration.count());
    printf("Elapsed time (ms): %.f ms\n", (double)duration.count() / 1000.0);
    printf("Elapsed time (sec): %.f sec\n", (double)duration.count() / 1000000.0);
    printf("Throughput: %f Mops/sec\n", ((double)num_ios * 1.0) / (double)duration.count());
    printf("Throughput: %f GB/sec\n", ((double)gItemSize / (1024 * 1024 * 1024)) / ((double)duration.count() / 1000000.0));

    // Deallocating data items
    EXPECT_NO_THROW(my_fam->fam_deallocate(item));
    free(items);

    // Destroying the region
    EXPECT_NO_THROW(my_fam->fam_destroy_region(desc));
    free(descs);

    for (uint64_t i = 0; i < numThreads; i++) {
        my_fam->fam_context_close(ctx[i]);
    }
    free(ctx);

    free(sum_probs);
    delete[] infos;
    free(threads);
}

// Test case -  Blocking get test by multiple threads on same region and data item.
TEST(FamPutGet, BlockingFamGetSingleRegionDataItem) {
    uint64_t num_ios = gItemSize / gDataSize;
    pthread_t *threads = (pthread_t *)malloc(sizeof(pthread_t) * numThreads);
    int rc;

    Fam_Region_Descriptor **descs = (Fam_Region_Descriptor **)malloc(sizeof(Fam_Region_Descriptor *) * numThreads);
    Fam_Descriptor **items = (Fam_Descriptor **)malloc(sizeof(Fam_Descriptor *) * numThreads);

    ValueInfo *infos = new ValueInfo[numThreads];

#ifdef ENABLE_LOCAL_CACHE
    size_t cache_size = (size_t)((double)(((double)gItemSize / (double)cache_page_size) 
                / (double)numThreads) * cache_ratio);
    lru_cache_t<uint64_t, void *> **caches = (lru_cache_t<uint64_t, void *> **)malloc(sizeof(lru_cache_t<uint64_t, void *> *) * numThreads);
    for (uint64_t i = 0; i < numThreads; i++) {
        caches[i] = new lru_cache_t<uint64_t, void *>(cache_size);
        infos[i].cache = caches[i];
        infos[i].cache_buf = gCacheBuf;
        infos[i].num_cache_hit = 0;
        infos[i].num_cache_miss = 0;
    }
#else
    for (uint64_t i = 0; i < numThreads; i++) {
        infos[i].cache_buf = gCacheBuf;
        infos[i].num_cache_hit = 0;
        infos[i].num_cache_miss = 0;
    }
#endif

    std::string regionPrefix("region");
    std::string itemPrefix("item");
    EXPECT_NO_THROW(desc = my_fam->fam_create_region(regionPrefix.c_str(),
                BIG_REGION_SIZE, 0777, NULL));
    EXPECT_NE((void *)NULL, desc);

    EXPECT_NO_THROW(item = my_fam->fam_allocate(itemPrefix.c_str(), gItemSize, 0777, desc));
    EXPECT_NE((void *)NULL, item);

    ctx = (fam_context **) malloc(sizeof(fam_context *) * numThreads);
    for (uint64_t i = 0; i < numThreads; i++) {
        ctx[i] = my_fam->fam_context_open();
    }

    for (uint64_t i = 0; i < numThreads; i++) {
        items[i] = item;
    }

    // Warm-up
    //int64_t *testBuf = (int64_t *)malloc(gItemSize);
    //memset(testBuf, 1, gItemSize);
    //my_fam->fam_get_blocking(testBuf, item, 0, gItemSize);
    //free(testBuf);
    //my_fam->fam_barrier_all();

    uint64_t num_indexes = num_ios;
    std::vector<uint64_t> uniform_random_indexes;
    double base = 0;
    unsigned seed = 0;

    double *sum_probs = (double *) calloc(1, sizeof(double) * num_indexes);

    if (zipf > 0) {
        base = get_base((unsigned)num_indexes, zipf);
        sum_probs[0] = 0;

        seed = (unsigned)time(NULL);

        for (unsigned i = 1; i <= num_indexes; i++) {
            sum_probs[i] = sum_probs[i - 1] + base / pow((double)i, zipf);
        }
    } else {
        // Generate random indexes
        std::srand(unsigned(std::time(0)));
        for (uint64_t i = 0; i < num_indexes; i++) {
            uniform_random_indexes.push_back(i);
        }
        std::random_shuffle(uniform_random_indexes.begin(), uniform_random_indexes.end());
    }

    for (uint64_t i = 0; i < numThreads; i++) {
        infos[i].item = items[i];
        infos[i].tid = i;
        infos[i].num_ops = 0;
    }

    uint64_t sample_index = 0;
    for (uint64_t i = 0; i < num_indexes; i++) {
        if (isRand) {
            if (zipf > 0) {
                sample_index = (uint64_t) sample((int)num_indexes, seed, base, sum_probs);
                sample_index -= 1;
            } else {
                // Uniform random
                sample_index = uniform_random_indexes.back();
                uniform_random_indexes.pop_back();
            }
        } else {
            // Sequential
            sample_index = i;
        }

        infos[sample_index / (num_indexes / numThreads)].indexes.push_back(sample_index);
        infos[sample_index / (num_indexes / numThreads)].num_ops++;
    }

#ifdef ENABLE_LATENCY_CHECK
    for (uint64_t i = 0; i < numThreads; i++)
        op_latency_maps.push_back(std::vector<uint64_t>());
    for (uint64_t i = 0; i < numThreads; i++)
        op_latency_maps[i].reserve(infos[i].num_ops);
#endif

#ifdef ENABLE_LOCAL_CACHE
    for (uint64_t i = 0; i < numThreads; i++) {
        if ((rc = pthread_create(&threads[i], NULL, 
                        func_cache_get_single_region_item_warmup, &infos[i]))) {
            fprintf(stderr, "error: pthread_create, rc: %d\n", rc);
            exit(1);
        }
    }

    for (uint64_t i = 0; i < numThreads; i++) {
        pthread_join(threads[i], NULL);
    }
#endif

    auto starttime = std::chrono::system_clock::now();
    for (uint64_t i = 0; i < numThreads; i++) {
        if ((rc = pthread_create(&threads[i], NULL, 
                        func_blocking_cache_get_single_region_item, &infos[i]))) {
            fprintf(stderr, "error: pthread_create, rc: %d\n", rc);
            exit(1);
        }
    }

    for (uint64_t i = 0; i < numThreads; i++) {
        pthread_join(threads[i], NULL);
    }
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now() - starttime);
    printf("Elapsed time (us): %lu us\n", duration.count());
    printf("Elapsed time (ms): %.f ms\n", (double)duration.count() / 1000.0);
    printf("Elapsed time (sec): %.f sec\n", (double)duration.count() / 1000000.0);
    printf("Throughput: %f Mops/sec\n", ((double)num_ios * 1.0) / (double)duration.count());
    printf("Throughput: %f GB/sec\n", ((double)gItemSize / (1024 * 1024 * 1024)) / ((double)duration.count() / 1000000.0));
#ifdef ENABLE_LOCAL_CACHE
    uint64_t num_cache_hit = 0, num_cache_miss = 0;
    for (uint64_t i = 0; i < numThreads; i++) {
        num_cache_hit += infos[i].num_cache_hit;
        num_cache_miss += infos[i].num_cache_miss;
    }
    printf("Cache hit ratio = %f \n", (double)((double)num_cache_hit / (double)(num_cache_hit + num_cache_miss)) * 100.0);
    printf("Cache hit count = %lu\n", num_cache_hit);
    printf("Cache miss count = %lu\n", num_cache_miss);
#endif

#ifdef ENABLE_LATENCY_CHECK
    std::vector<uint64_t> combined_latency_map;
    combined_latency_map.reserve(num_ios);
    for (uint64_t i = 0; i < numThreads; i++) {
        combined_latency_map.insert(combined_latency_map.end(), op_latency_maps[i].begin(), op_latency_maps[i].end());
    }
    std::sort(combined_latency_map.begin(), combined_latency_map.end());

    uint64_t average_latency = std::accumulate(combined_latency_map.begin(), combined_latency_map.end(), 0UL) / combined_latency_map.size();
    uint64_t min_latency = combined_latency_map.front();
    uint64_t max_latency = combined_latency_map.back();
    uint64_t median_latency = combined_latency_map[trunc((double)combined_latency_map.size() * 0.50)];
    uint64_t tail_latency_90 = combined_latency_map[trunc((double)combined_latency_map.size() * 0.90)];
    uint64_t tail_latency_95 = combined_latency_map[trunc((double)combined_latency_map.size() * 0.95)];
    uint64_t tail_latency_99 = combined_latency_map[trunc((double)combined_latency_map.size() * 0.99)];

    printf("Average latency = %lu ns\n", average_latency);
    printf("Min latency = %lu ns\n", min_latency);
    printf("Max latency = %lu ns\n", max_latency);
    printf("50th median latency = %lu ns\n", median_latency);                                         
    printf("90th tail latency = %lu ns\n", tail_latency_90);                                          
    printf("95th tail latency = %lu ns\n", tail_latency_95);                                          
    printf("99th tail latency = %lu ns\n", tail_latency_99);
#endif

    // Deallocating data items
    EXPECT_NO_THROW(my_fam->fam_deallocate(item));
    free(items);

    // Destroying the region
    EXPECT_NO_THROW(my_fam->fam_destroy_region(desc));
    free(descs);

    for (uint64_t i = 0; i < numThreads; i++) {
        my_fam->fam_context_close(ctx[i]);
    }
    free(ctx);

    free(sum_probs);
#ifdef ENABLE_LOCAL_CACHE
    for (uint64_t i = 0; i < numThreads; i++) {
        delete infos[i].cache;
    }
    free(caches);
#endif
    delete[] infos;
    free(threads);
}

// Test case -  NonBlocking get test by multiple threads on same region and data item.
TEST(FamPutGet, NonBlockingFamGetSingleRegionDataItem) {
    uint64_t num_ios = gItemSize / gDataSize;
    pthread_t *threads = (pthread_t *)malloc(sizeof(pthread_t) * numThreads);
    int rc;

    Fam_Region_Descriptor **descs = (Fam_Region_Descriptor **)malloc(sizeof(Fam_Region_Descriptor *) * numThreads);
    Fam_Descriptor **items = (Fam_Descriptor **)malloc(sizeof(Fam_Descriptor *) * numThreads);

    ValueInfo *infos = new ValueInfo[numThreads];

#ifdef ENABLE_LOCAL_CACHE
    size_t cache_size = (size_t)((double)(((double)gItemSize / (double)cache_page_size) / (double)numThreads) * cache_ratio);
    lru_cache_t<uint64_t, void *> **caches = (lru_cache_t<uint64_t, void *> **)malloc(sizeof(lru_cache_t<uint64_t, void *> *) * numThreads);
    for (uint64_t i = 0; i < numThreads; i++) {
        caches[i] = new lru_cache_t<uint64_t, void *>(cache_size);
        infos[i].cache = caches[i];
        infos[i].cache_buf = gCacheBuf;
        infos[i].num_cache_hit = 0;
        infos[i].num_cache_miss = 0;
    }
#else
    for (uint64_t i = 0; i < numThreads; i++) {
        infos[i].cache_buf = gCacheBuf;
        infos[i].num_cache_hit = 0;
        infos[i].num_cache_miss = 0;
    }
#endif

    std::string regionPrefix("region");
    std::string itemPrefix("item");
    EXPECT_NO_THROW(desc = my_fam->fam_create_region(regionPrefix.c_str(),
                BIG_REGION_SIZE, 0777, NULL));
    EXPECT_NE((void *)NULL, desc);

    EXPECT_NO_THROW(item = my_fam->fam_allocate(itemPrefix.c_str(), gItemSize, 0777, desc));
    EXPECT_NE((void *)NULL, item);

    ctx = (fam_context **) malloc(sizeof(fam_context *) * numThreads);
    for (uint64_t i = 0; i < numThreads; i++) {
        ctx[i] = my_fam->fam_context_open();
    }

    for (uint64_t i = 0; i < numThreads; i++) {
        items[i] = item;
    }

    // Warm-up
    //int64_t *testBuf = (int64_t *)malloc(gItemSize);
    //memset(testBuf, 1, gItemSize);
    //my_fam->fam_get_blocking(testBuf, item, 0, gItemSize);
    //free(testBuf);
    //my_fam->fam_barrier_all();

    uint64_t num_indexes = num_ios;
    std::vector<uint64_t> uniform_random_indexes;
    double base = 0;
    unsigned seed = 0;

    double *sum_probs = (double *) calloc(1, sizeof(double) * num_indexes);

    if (zipf > 0) {
        base = get_base((unsigned)num_indexes, zipf);
        sum_probs[0] = 0;

        seed = (unsigned)time(NULL);

        for (unsigned i = 1; i <= num_indexes; i++) {
            sum_probs[i] = sum_probs[i - 1] + base / pow((double)i, zipf);
        }
    } else {
        // Generate random indexes
        std::srand(unsigned(std::time(0)));
        for (uint64_t i = 0; i < num_indexes; i++) {
            uniform_random_indexes.push_back(i);
        }
        std::random_shuffle(uniform_random_indexes.begin(), uniform_random_indexes.end());
    }

    for (uint64_t i = 0; i < numThreads; i++) {
        infos[i].item = items[i];
        infos[i].tid = i;
        infos[i].num_ops = 0;
    }

    uint64_t sample_index = 0;
    for (uint64_t i = 0; i < num_indexes; i++) {
        if (isRand) {
            if (zipf > 0) {
                sample_index = (uint64_t) sample((int)num_indexes, seed, base, sum_probs);
                sample_index -= 1;
            } else {
                // Uniform random
                sample_index = uniform_random_indexes.back();
                uniform_random_indexes.pop_back();
            }
        } else {
            // Sequential
            sample_index = i;
        }

        infos[sample_index / (num_indexes / numThreads)].indexes.push_back(sample_index);
        infos[sample_index / (num_indexes / numThreads)].num_ops++;
    }

#ifdef ENABLE_LATENCY_CHECK
    for (uint64_t i = 0; i < numThreads; i++)
        op_latency_maps.push_back(std::vector<uint64_t>());
    for (uint64_t i = 0; i < numThreads; i++)
        op_latency_maps[i].reserve(infos[i].num_ops);
#endif

#ifdef ENABLE_LOCAL_CACHE
    for (uint64_t i = 0; i < numThreads; i++) {
        if ((rc = pthread_create(&threads[i], NULL, 
                        func_cache_get_single_region_item_warmup, &infos[i]))) {
            fprintf(stderr, "error: pthread_create, rc: %d\n", rc);
            exit(1);
        }
    }

    for (uint64_t i = 0; i < numThreads; i++) {
        pthread_join(threads[i], NULL);
    }
#endif

    auto starttime = std::chrono::system_clock::now();
    for (uint64_t i = 0; i < numThreads; i++) {
        if ((rc = pthread_create(&threads[i], NULL, 
                        func_non_blocking_cache_get_single_region_item, &infos[i]))) {
            fprintf(stderr, "error: pthread_create, rc: %d\n", rc);
            exit(1);
        }
    }

    for (uint64_t i = 0; i < numThreads; i++) {
        pthread_join(threads[i], NULL);
    }
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now() - starttime);
    printf("Elapsed time (us): %lu us\n", duration.count());
    printf("Elapsed time (ms): %.f ms\n", (double)duration.count() / 1000.0);
    printf("Elapsed time (sec): %.f sec\n", (double)duration.count() / 1000000.0);
    printf("Throughput: %f Mops/sec\n", ((double)num_ios * 1.0) / (double)duration.count());
    printf("Throughput: %f GB/sec\n", ((double)gItemSize / (1024 * 1024 * 1024)) / ((double)duration.count() / 1000000.0));
#ifdef ENABLE_LOCAL_CACHE
    uint64_t num_cache_hit = 0, num_cache_miss = 0;
    for (uint64_t i = 0; i < numThreads; i++) {
        num_cache_hit += infos[i].num_cache_hit;
        num_cache_miss += infos[i].num_cache_miss;
    }
    printf("Cache hit ratio = %f \n", (double)((double)num_cache_hit / (double)(num_cache_hit + num_cache_miss)) * 100.0);
    printf("Cache hit count = %lu\n", num_cache_hit);
    printf("Cache miss count = %lu\n", num_cache_miss);
#endif

#ifdef ENABLE_LATENCY_CHECK
    std::vector<uint64_t> combined_latency_map;
    combined_latency_map.reserve(num_ios / max_num_outstanding_requests);
    for (uint64_t i = 0; i < numThreads; i++) {
        combined_latency_map.insert(combined_latency_map.end(), op_latency_maps[i].begin(), op_latency_maps[i].end());
    }
    std::sort(combined_latency_map.begin(), combined_latency_map.end());

    uint64_t average_latency = std::accumulate(combined_latency_map.begin(), combined_latency_map.end(), 0UL) / combined_latency_map.size();
    uint64_t min_latency = combined_latency_map.front();
    uint64_t max_latency = combined_latency_map.back();
    uint64_t median_latency = combined_latency_map[trunc((double)combined_latency_map.size() * 0.50)];
    uint64_t tail_latency_90 = combined_latency_map[trunc((double)combined_latency_map.size() * 0.90)];
    uint64_t tail_latency_95 = combined_latency_map[trunc((double)combined_latency_map.size() * 0.95)];
    uint64_t tail_latency_99 = combined_latency_map[trunc((double)combined_latency_map.size() * 0.99)];

    printf("Latency sample size = %lu\n", combined_latency_map.size());
    printf("Average latency = %lu ns\n", average_latency);
    printf("Min latency = %lu ns\n", min_latency);
    printf("Max latency = %lu ns\n", max_latency);
    printf("50th median latency = %lu ns\n", median_latency);                                         
    printf("90th tail latency = %lu ns\n", tail_latency_90);                                          
    printf("95th tail latency = %lu ns\n", tail_latency_95);                                          
    printf("99th tail latency = %lu ns\n", tail_latency_99);
#endif

    // Deallocating data items
    EXPECT_NO_THROW(my_fam->fam_deallocate(item));
    free(items);

    // Destroying the region
    EXPECT_NO_THROW(my_fam->fam_destroy_region(desc));
    free(descs);

    for (uint64_t i = 0; i < numThreads; i++) {
        my_fam->fam_context_close(ctx[i]);
    }
    free(ctx);

    free(sum_probs);
#ifdef ENABLE_LOCAL_CACHE
    for (uint64_t i = 0; i < numThreads; i++) {
        delete infos[i].cache;
    }
    free(caches);
#endif
    delete[] infos;
    free(threads);
}

// Test case -  Blocking put test by multiple threads on multiple regions and data items.
TEST(FamPutGet, BlockingFamPutMultipleRegionDataItem) {
    uint64_t num_ios = gItemSize / gDataSize, i;
    pthread_t *threads = (pthread_t *)malloc(sizeof(pthread_t) * numThreads);
    int rc;

    Fam_Region_Descriptor **descs = (Fam_Region_Descriptor **)malloc(sizeof(Fam_Region_Descriptor *) * numThreads);
    Fam_Descriptor **items = (Fam_Descriptor **)malloc(sizeof(Fam_Descriptor *) * numThreads);

    ValueInfo *infos = new ValueInfo[numThreads];

    std::string regionPrefix("region");
    std::string itemPrefix("item");
    for (i = 0; i < numThreads; i++) {
        EXPECT_NO_THROW(descs[i] = my_fam->fam_create_region((regionPrefix + std::to_string(i)).c_str(),
                    BIG_REGION_SIZE / numThreads, 0777, NULL));
        EXPECT_NE((void *)NULL, descs[i]);

        EXPECT_NO_THROW(items[i] = my_fam->fam_allocate((itemPrefix + std::to_string(i)).c_str(),
                    gItemSize / numThreads, 0777, descs[i]));
        EXPECT_NE((void *)NULL, items[i]);
    }

    // Warm-up
    //int64_t *testBuf = (int64_t *)malloc(gItemSize / numThreads);
    //memset(testBuf, 1, gItemSize / numThreads);
    //for (i = 0; i < numThreads; i++) {
    //    my_fam->fam_put_blocking(testBuf, items[i], 0, gItemSize / numThreads);
    //}
    //free(testBuf);
    //my_fam->fam_barrier_all();

    auto starttime = std::chrono::system_clock::now();
    for (i = 0; i < numThreads; i++) {
        infos[i].item = items[i];
        infos[i].tid = i;
        if ((rc = pthread_create(&threads[i], NULL, 
                        func_blocking_put_multiple_region_item, &infos[i]))) {
            fprintf(stderr, "error: pthread_create, rc: %d\n", rc);
            exit(1);
        }
    }

    for (i = 0; i < numThreads; i++) {
        pthread_join(threads[i], NULL);
    }
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now() - starttime);
    printf("Elapsed time (us): %lu us\n", duration.count());
    printf("Elapsed time (ms): %.f ms\n", (double)duration.count() / 1000.0);
    printf("Elapsed time (sec): %.f sec\n", (double)duration.count() / 1000000.0);
    printf("Throughput: %f Mops/sec\n", ((double)num_ios * 1.0) / (double)duration.count());
    printf("Throughput: %f GB/sec\n", ((double)gItemSize / (1024 * 1024 * 1024)) / ((double)duration.count() / 1000000.0));

    // Deallocating data items
    for (i = 0; i < numThreads; i++) {
        EXPECT_NO_THROW(my_fam->fam_deallocate(items[i]));
    }
    free(items);

    // Destroying the region
    for (i = 0; i < numThreads; i++) {
        EXPECT_NO_THROW(my_fam->fam_destroy_region(descs[i]));
    }
    free(descs);

    delete[] infos;
    free(threads);
}

// Test case -  Blocking get test by multiple threads on multiple regions and data items.
TEST(FamPutGet, BlockingFamGetMultipleRegionDataItem) {
    uint64_t num_ios = gItemSize / gDataSize, i;
    pthread_t *threads = (pthread_t *)malloc(sizeof(pthread_t) * numThreads);
    int rc;

    Fam_Region_Descriptor **descs = (Fam_Region_Descriptor **)malloc(sizeof(Fam_Region_Descriptor *) * numThreads);
    Fam_Descriptor **items = (Fam_Descriptor **)malloc(sizeof(Fam_Descriptor *) * numThreads);

    ValueInfo *infos = new ValueInfo[numThreads];

    std::string regionPrefix("region");
    std::string itemPrefix("item");
    for (i = 0; i < numThreads; i++) {
        EXPECT_NO_THROW(descs[i] = my_fam->fam_create_region((regionPrefix + std::to_string(i)).c_str(),
                    BIG_REGION_SIZE / numThreads, 0777, NULL));
        EXPECT_NE((void *)NULL, descs[i]);

        EXPECT_NO_THROW(items[i] = my_fam->fam_allocate((itemPrefix + std::to_string(i)).c_str(),
                    gItemSize / numThreads, 0777, descs[i]));
        EXPECT_NE((void *)NULL, items[i]);
    }

    // Warm-up
    //int64_t *testBuf = (int64_t *)malloc(gItemSize / numThreads);
    //memset(testBuf, 1, gItemSize / numThreads);
    //for (i = 0; i < numThreads; i++) {
    //    my_fam->fam_get_blocking(testBuf, items[i], 0, gItemSize / numThreads);
    //}
    //free(testBuf);
    //my_fam->fam_barrier_all();

    auto starttime = std::chrono::system_clock::now();
    for (i = 0; i < numThreads; i++) {
        infos[i].item = items[i];
        infos[i].tid = i;
        if ((rc = pthread_create(&threads[i], NULL, 
                        func_blocking_get_multiple_region_item, &infos[i]))) {
            fprintf(stderr, "error: pthread_create, rc: %d\n", rc);
            exit(1);
        }
    }

    for (i = 0; i < numThreads; i++) {
        pthread_join(threads[i], NULL);
    }
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now() - starttime);
    printf("Elapsed time (us): %lu us\n", duration.count());
    printf("Elapsed time (ms): %.f ms\n", (double)duration.count() / 1000.0);
    printf("Elapsed time (sec): %.f sec\n", (double)duration.count() / 1000000.0);
    printf("Throughput: %f Mops/sec\n", ((double)num_ios * 1.0) / (double)duration.count());
    printf("Throughput: %f GB/sec\n", ((double)gItemSize / (1024 * 1024 * 1024)) / ((double)duration.count() / 1000000.0));

    // Deallocating data items
    for (i = 0; i < numThreads; i++) {
        EXPECT_NO_THROW(my_fam->fam_deallocate(items[i]));
    }
    free(items);

    // Destroying the region
    for (i = 0; i < numThreads; i++) {
        EXPECT_NO_THROW(my_fam->fam_destroy_region(descs[i]));
    }
    free(descs);

    delete[] infos;
    free(threads);
}

// Test case -  Blocking put test by multiple threads on a single region and multiple data items.
TEST(FamPutGet, BlockingFamPutSingleRegionMultipleDataItem) {
    uint64_t num_ios = gItemSize / gDataSize, i;
    pthread_t *threads = (pthread_t *)malloc(sizeof(pthread_t) * numThreads);
    int rc;

    Fam_Region_Descriptor **descs = (Fam_Region_Descriptor **)malloc(sizeof(Fam_Region_Descriptor *) * numThreads);
    Fam_Descriptor **items = (Fam_Descriptor **)malloc(sizeof(Fam_Descriptor *) * numThreads);

    ValueInfo *infos = new ValueInfo[numThreads];

    std::string regionPrefix("region");
    std::string itemPrefix("item");

    Fam_Region_Attributes *regionAttributes = new Fam_Region_Attributes();
    regionAttributes->permissionLevel = REGION;

    EXPECT_NO_THROW(desc = my_fam->fam_create_region((regionPrefix).c_str(), 
                BIG_REGION_SIZE, 0777, regionAttributes));
    EXPECT_NE((void *)NULL, desc);

    for (i = 0; i < numThreads; i++) {
        descs[i] = desc;
        EXPECT_NO_THROW(items[i] = my_fam->fam_allocate((itemPrefix + std::to_string(i)).c_str(),
                    gItemSize / numThreads, 0777, descs[i]));
        EXPECT_NE((void *)NULL, items[i]);
    }

    // Warm-up
    //int64_t *testBuf = (int64_t *)malloc(gItemSize / numThreads);
    //memset(testBuf, 1, gItemSize / numThreads);
    //for (i = 0; i < numThreads; i++) {
    //    my_fam->fam_put_blocking(testBuf, items[i], 0, gItemSize / numThreads);
    //}
    //free(testBuf);
    //my_fam->fam_barrier_all();

    auto starttime = std::chrono::system_clock::now();
    for (i = 0; i < numThreads; i++) {
        infos[i].item = items[i];
        infos[i].tid = i;
        if ((rc = pthread_create(&threads[i], NULL, 
                        func_blocking_put_multiple_region_item, &infos[i]))) {
            fprintf(stderr, "error: pthread_create, rc: %d\n", rc);
            exit(1);
        }
    }

    for (i = 0; i < numThreads; i++) {
        pthread_join(threads[i], NULL);
    }
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now() - starttime);
    printf("Elapsed time (us): %lu us\n", duration.count());
    printf("Elapsed time (ms): %.f ms\n", (double)duration.count() / 1000.0);
    printf("Elapsed time (sec): %.f sec\n", (double)duration.count() / 1000000.0);
    printf("Throughput: %f Mops/sec\n", ((double)num_ios * 1.0) / (double)duration.count());
    printf("Throughput: %f GB/sec\n", ((double)gItemSize / (1024 * 1024 * 1024)) / ((double)duration.count() / 1000000.0));

    // Deallocating data items
    for (i = 0; i < numThreads; i++) {
        EXPECT_NO_THROW(my_fam->fam_deallocate(items[i]));
    }
    free(items);

    // Destroying the region
    EXPECT_NO_THROW(my_fam->fam_destroy_region(desc));
    free(descs);

    delete[] infos;
    free(threads);
}

// Test case -  Blocking get test by multiple threads on a single region and multiple data items.
TEST(FamPutGet, BlockingFamGetSingleRegionMultipleDataItem) {
    uint64_t num_ios = gItemSize / gDataSize, i;
    pthread_t *threads = (pthread_t *)malloc(sizeof(pthread_t) * numThreads);
    int rc;

    Fam_Region_Descriptor **descs = (Fam_Region_Descriptor **)malloc(sizeof(Fam_Region_Descriptor *) * numThreads);
    Fam_Descriptor **items = (Fam_Descriptor **)malloc(sizeof(Fam_Descriptor *) * numThreads);

    ValueInfo *infos = new ValueInfo[numThreads];

    std::string regionPrefix("region");
    std::string itemPrefix("item");

    Fam_Region_Attributes *regionAttributes = new Fam_Region_Attributes();
    regionAttributes->permissionLevel = REGION;

    EXPECT_NO_THROW(desc = my_fam->fam_create_region((regionPrefix).c_str(), 
                BIG_REGION_SIZE, 0777, regionAttributes));
    EXPECT_NE((void *)NULL, desc);

    for (i = 0; i < numThreads; i++) {
        descs[i] = desc;
        EXPECT_NO_THROW(items[i] = my_fam->fam_allocate((itemPrefix + std::to_string(i)).c_str(),
                    gItemSize / numThreads, 0777, descs[i]));
        EXPECT_NE((void *)NULL, items[i]);
    }

    // Warm-up
    //int64_t *testBuf = (int64_t *)malloc(gItemSize / numThreads);
    //memset(testBuf, 1, gItemSize / numThreads);
    //for (i = 0; i < numThreads; i++) {
    //    my_fam->fam_get_blocking(testBuf, items[i], 0, gItemSize / numThreads);
    //}
    //free(testBuf);
    //my_fam->fam_barrier_all();

    auto starttime = std::chrono::system_clock::now();
    for (i = 0; i < numThreads; i++) {
        infos[i].item = items[i];
        infos[i].tid = i;
        if ((rc = pthread_create(&threads[i], NULL, 
                        func_blocking_get_multiple_region_item, &infos[i]))) {
            fprintf(stderr, "error: pthread_create, rc: %d\n", rc);
            exit(1);
        }
    }

    for (i = 0; i < numThreads; i++) {
        pthread_join(threads[i], NULL);
    }
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now() - starttime);
    printf("Elapsed time (us): %lu us\n", duration.count());
    printf("Elapsed time (ms): %.f ms\n", (double)duration.count() / 1000.0);
    printf("Elapsed time (sec): %.f sec\n", (double)duration.count() / 1000000.0);
    printf("Throughput: %f Mops/sec\n", ((double)num_ios * 1.0) / (double)duration.count());
    printf("Throughput: %f GB/sec\n", ((double)gItemSize / (1024 * 1024 * 1024)) / ((double)duration.count() / 1000000.0));

    // Deallocating data items
    for (i = 0; i < numThreads; i++) {
        EXPECT_NO_THROW(my_fam->fam_deallocate(items[i]));
    }
    free(items);

    // Destroying the region
    EXPECT_NO_THROW(my_fam->fam_destroy_region(desc));
    free(descs);

    delete[] infos;
    free(threads);
}

int main(int argc, char **argv) {
    int ret;
    ::testing::InitGoogleTest(&argc, argv);

#ifdef ENABLE_LOCAL_CACHE
    printf("ENABLE_LOCAL_CACHE\n");
#else
    printf("DISABLE_LOCAL_CACHE\n");
#endif
    if (argc == 8) {
        gDataSize = atoi(argv[1]);
        isRand = atoi(argv[2]);
        zipf = std::stod(argv[3]);
        numThreads = atoi(argv[4]);
        max_num_outstanding_requests = atoi(argv[5]);
        cache_ratio = std::stod(argv[6]);
        cache_page_size = std::atoi(argv[7]);
        if (cache_page_size == 0) {
            cache_page_size = gDataSize;
        }
    } else {
        fprintf(stderr, "# required parameters doesn't match\n");
        exit(-1);
    }

    std::cout << "gDataSize = " << gDataSize << std::endl;
    std::cout << "isRand = " << isRand << std::endl;
    std::cout << "zipf = " << zipf << std::endl;
    std::cout << "numThreads = " << numThreads << std::endl;
    std::cout << "max_num_outstanding_requests = " << max_num_outstanding_requests << std::endl;
    std::cout << "cache_ratio = " << cache_ratio << std::endl;
    std::cout << "cache_page_size = " << cache_page_size << std::endl;

    my_fam = new fam();

    init_fam_options(&fam_opts);

    gLocalBuf = (int64_t *)malloc(LOCAL_BUFFER_SIZE * numThreads * (max_num_outstanding_requests + 1));
    //gLocalBuf = (int64_t *)numa_alloc_onnode(LOCAL_BUFFER_SIZE * numThreads, 1);
    memset(gLocalBuf, 2, LOCAL_BUFFER_SIZE * numThreads * (max_num_outstanding_requests + 1));
#ifndef ENABLE_LOCAL_MEMORY_HUGE_PAGE
    gCacheBuf = malloc(gItemSize);
    //gCacheBuf = numa_alloc_onnode(gItemSize, 0);
#else
    int fd = open("/mnt/hugetlbfs/gCacheBuf", O_CREAT | O_RDWR, 0755);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    if (ftruncate(fd, gItemSize) == -1) {
        perror("ftruncate");
        close(fd);
        return 1;
    }

    gCacheBuf = mmap(NULL, gItemSize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_HUGETLB | MAP_POPULATE, fd, 0);
    if (gCacheBuf == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }
#endif
    memset(gCacheBuf, 1, gItemSize);

    fam_opts.local_buf_addr = gLocalBuf;
    fam_opts.local_buf_size = LOCAL_BUFFER_SIZE * numThreads * (max_num_outstanding_requests + 1);

    fam_opts.famThreadModel = strdup("FAM_THREAD_MULTIPLE");

    EXPECT_NO_THROW(my_fam->fam_initialize("default", &fam_opts));
    EXPECT_NO_THROW(myPE = (int *)my_fam->fam_get_option(strdup("PE_ID")));

    ret = RUN_ALL_TESTS();

    cout << "finished all testing" << endl;
    EXPECT_NO_THROW(my_fam->fam_finalize("default"));
    cout << "Finalize done : " << ret << endl;
    delete my_fam;

    free(gLocalBuf);
#ifndef ENABLE_LOCAL_MEMORY_HUGE_PAGE
    free(gCacheBuf);
#else
    munmap(gCacheBuf, gItemSize);
    close(fd);
#endif

    return ret;
}
