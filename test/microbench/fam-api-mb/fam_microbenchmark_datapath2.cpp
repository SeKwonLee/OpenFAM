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
uint64_t gItemSize = 8192 * 1024 * 1024ULL;
uint64_t gDataSize = 256;

int isRand = 0;
double zipf = 0.0;
uint64_t numThreads = 1;
fam_context **ctx;

#define LOCAL_BUFFER_SIZE   2*1024*1024UL   // 2MB

#ifdef ENABLE_LOCAL_CACHE
#include "cache.hpp"
#include "lru_cache_policy.hpp"

void *gCacheBuf = NULL;

template <typename Key, typename Value>
using lru_cache_t = typename caches::fixed_sized_cache<Key, Value, caches::LRUCachePolicy>;
double cache_ratio = 0.0;
uint64_t cache_page_size = 4096;
uint64_t num_cache_hit = 0;
uint64_t num_cache_miss = 0;
#endif

typedef struct {
    Fam_Descriptor *item;
    uint64_t tid;
    uint64_t num_ops;
    uint64_t *indexes;
#ifdef ENABLE_LOCAL_CACHE
    lru_cache_t<uint64_t, void *> *cache;
    void *cache_buf;
#endif
} ValueInfo;

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

void *func_blocking_put_single_region_item(void *arg) {
    ValueInfo *it = (ValueInfo *)arg;
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
    uint64_t offset = 0;

    void *LocalBuf = (void *)((uint64_t)gLocalBuf + (it->tid * LOCAL_BUFFER_SIZE));

    for (uint64_t i = 0; i < it->num_ops; i++) {
        offset = it->indexes[i] * gDataSize;
        ctx[it->tid]->fam_get_blocking(LocalBuf, it->item, offset, gDataSize);
    }

    pthread_exit(NULL);
}

#ifdef ENABLE_LOCAL_CACHE
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

void *func_blocking_cache_get_single_region_item_warmup(void *arg) {
    ValueInfo *it = (ValueInfo *)arg;
    uint64_t byte_index = 0, start_byte_offset = 0, end_byte_offset = 0, 
             start_page_index = 0, end_page_index = 0, start_page_local_offset = 0;

    void *LocalBuf = (void *)((uint64_t)gLocalBuf + (it->tid * LOCAL_BUFFER_SIZE));

    // Generate random indexes
    uint64_t total_indexes = gItemSize / gDataSize;
    uint64_t max_indexes_per_thread = (gItemSize / gDataSize) / numThreads;
    std::srand(unsigned(std::time(0)));
    std::vector<uint64_t> indexes;
    indexes.reserve(total_indexes);
    for (uint64_t i = 0; i < total_indexes; i++) {
        indexes[i] = i;
    }
    std::random_shuffle(indexes.begin(), indexes.end());

    for (uint64_t i = 0; i < it->num_ops; i++) {
        byte_index = indexes[i];
        if (byte_index % numThreads == it->tid) {
            start_byte_offset = (byte_index % max_indexes_per_thread) * gDataSize;
            end_byte_offset = start_byte_offset + gDataSize;

            start_page_index = offset_to_start_page_index(start_byte_offset);
            end_page_index = offset_to_end_page_index(end_byte_offset);

            start_page_local_offset = page_index_to_page_local_offset(start_byte_offset, start_page_index);

            uint64_t size_to_read = gDataSize, read_size = 0;
            for (uint64_t index = start_page_index; index <= end_page_index; index++) {
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
                } else {
                    ctx[it->tid]->fam_get_blocking((void *)((uint64_t)it->cache_buf + (index * cache_page_size)),
                            it->item, index * cache_page_size, cache_page_size);
                    it->cache->Put(index, (void *)((uint64_t)it->cache_buf + (index * cache_page_size)));
                    if (index == start_page_index) {
                        read_size = size_to_read > (cache_page_size - start_page_local_offset) ?
                            (cache_page_size - start_page_local_offset) : gDataSize;
                        memcpy(LocalBuf, (void *)((uint64_t)it->cache_buf + (index * cache_page_size) 
                                    + start_page_local_offset), read_size);
                    } else {
                        read_size = size_to_read > cache_page_size ? cache_page_size : size_to_read;
                        memcpy((void *)((uint64_t)LocalBuf + (gDataSize - size_to_read)),
                                (void *)((uint64_t)it->cache_buf + (index * cache_page_size)), read_size);
                    }
                }

                size_to_read -= read_size;
            }
        }
    }

    pthread_exit(NULL);
}

void *func_blocking_cache_get_single_region_item(void *arg) {
    ValueInfo *it = (ValueInfo *)arg;
    uint64_t byte_index = 0, start_byte_offset = 0, end_byte_offset = 0, 
             start_page_index = 0, end_page_index = 0, start_page_local_offset = 0;

    void *LocalBuf = (void *)((uint64_t)gLocalBuf + (it->tid * LOCAL_BUFFER_SIZE));

    for (uint64_t i = 0; i < it->num_ops; i++) {
        byte_index = it->indexes[i];

        start_byte_offset = byte_index * gDataSize;
        end_byte_offset = start_byte_offset + gDataSize;

        start_page_index = offset_to_start_page_index(start_byte_offset);
        end_page_index = offset_to_end_page_index(end_byte_offset);

        start_page_local_offset = page_index_to_page_local_offset(start_byte_offset, start_page_index);

        uint64_t size_to_read = gDataSize, read_size = 0;
        for (uint64_t index = start_page_index; index <= end_page_index; index++) {
#if 1
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
                num_cache_hit++;
            } else {
                ctx[it->tid]->fam_get_blocking((void *)((uint64_t)it->cache_buf + (index * cache_page_size)),
                        it->item, index * cache_page_size, cache_page_size);
                it->cache->Put(index, (void *)((uint64_t)it->cache_buf + (index * cache_page_size)));
                if (index == start_page_index) {
                    read_size = size_to_read > (cache_page_size - start_page_local_offset) ?
                        (cache_page_size - start_page_local_offset) : gDataSize;
                    memcpy(LocalBuf, (void *)((uint64_t)it->cache_buf + (index * cache_page_size) 
                                + start_page_local_offset), read_size);
                } else {
                    read_size = size_to_read > cache_page_size ? cache_page_size : size_to_read;
                    memcpy((void *)((uint64_t)LocalBuf + (gDataSize - size_to_read)),
                            (void *)((uint64_t)it->cache_buf + (index * cache_page_size)), read_size);
                }
                num_cache_miss++;
            }
#else
            if (index == start_page_index) {
                read_size = size_to_read > (cache_page_size - start_page_local_offset) ?
                    (cache_page_size - start_page_local_offset) : gDataSize;
                memcpy(LocalBuf, (void *)((uint64_t)it->cache_buf + (index * cache_page_size) 
                            + start_page_local_offset), read_size);
            } else {
                read_size = size_to_read > cache_page_size ? cache_page_size : size_to_read;
                memcpy((void *)((uint64_t)LocalBuf + (gDataSize - size_to_read)),
                        (void *)((uint64_t)it->cache_buf + (index * cache_page_size)), read_size);
            }
#endif
            size_to_read -= read_size;
        }
    }

    pthread_exit(NULL);
}

typedef struct PostProcessInfo {
    uint64_t index;
    void *src;
    void *dest;
    uint64_t read_size;
} PostProcessInfo;

void *func_non_blocking_cache_get_single_region_item_warmup(void *arg) {
    ValueInfo *it = (ValueInfo *)arg;
    uint64_t byte_index = 0, start_byte_offset = 0, end_byte_offset = 0, 
             start_page_index = 0, end_page_index = 0, start_page_local_offset = 0;

    void *LocalBuf = (void *)((uint64_t)gLocalBuf + (it->tid * LOCAL_BUFFER_SIZE));

    std::vector<PostProcessInfo> PostProcessInfoArray;
    if (gDataSize < cache_page_size) {
        PostProcessInfoArray.reserve(1);
    } else {
        PostProcessInfoArray.reserve((size_t)(gDataSize/cache_page_size));
    }

    // Generate random indexes
    std::srand(unsigned(std::time(0)));
    std::vector<uint64_t> indexes;
    indexes.reserve(it->num_ops);
    for (uint64_t i = 0; i < it->num_ops; i++) {
        indexes[i] = i;
    }
    std::random_shuffle(indexes.begin(), indexes.end());

    for (uint64_t i = 0; i < it->num_ops; i++) {
        byte_index = indexes[i];

        start_byte_offset = byte_index * gDataSize;
        end_byte_offset = start_byte_offset + gDataSize;

        start_page_index = offset_to_start_page_index(start_byte_offset);
        end_page_index = offset_to_end_page_index(end_byte_offset);

        start_page_local_offset = page_index_to_page_local_offset(start_byte_offset, start_page_index);

        uint64_t size_to_read = gDataSize, read_size = 0;
        for (uint64_t index = start_page_index; index <= end_page_index; index++) {
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
            } else {
                PostProcessInfo ppi;
                ppi.index = index;
                ppi.src = (void *)((uint64_t)it->cache_buf + (index * cache_page_size));
                ctx[it->tid]->fam_get_nonblocking((void *)((uint64_t)it->cache_buf + (index * cache_page_size)),
                        it->item, index * cache_page_size, cache_page_size);
                if (index == start_page_index) {
                    read_size = size_to_read > (cache_page_size - start_page_local_offset) ?
                        (cache_page_size - start_page_local_offset) : gDataSize;
                    ppi.dest = LocalBuf;
                } else {
                    read_size = size_to_read > cache_page_size ? cache_page_size : size_to_read;
                    ppi.dest = (void *)((uint64_t)LocalBuf + (gDataSize - size_to_read));
                }
                ppi.read_size = read_size;
                PostProcessInfoArray.push_back(ppi);
            }

            size_to_read -= read_size;
        }

        if (PostProcessInfoArray.size() > 0) {
            ctx[it->tid]->fam_quiet();
            for (auto & info : PostProcessInfoArray) {
                it->cache->Put(info.index, info.src);
                memcpy(info.dest, info.src, info.read_size);
            }
            PostProcessInfoArray.clear();
        }
    }

    pthread_exit(NULL);
}

void *func_non_blocking_cache_get_single_region_item(void *arg) {
    ValueInfo *it = (ValueInfo *)arg;
    uint64_t byte_index = 0, start_byte_offset = 0, end_byte_offset = 0, 
             start_page_index = 0, end_page_index = 0, start_page_local_offset = 0;

    void *LocalBuf = (void *)((uint64_t)gLocalBuf + (it->tid * LOCAL_BUFFER_SIZE));

    std::vector<PostProcessInfo> PostProcessInfoArray;
    if (gDataSize < cache_page_size) {
        PostProcessInfoArray.reserve(1);
    } else {
        PostProcessInfoArray.reserve((size_t)(gDataSize/cache_page_size));
    }

    for (uint64_t i = 0; i < it->num_ops; i++) {
        byte_index = it->indexes[i];

        start_byte_offset = byte_index * gDataSize;
        end_byte_offset = start_byte_offset + gDataSize;

        start_page_index = offset_to_start_page_index(start_byte_offset);
        end_page_index = offset_to_end_page_index(end_byte_offset);

        start_page_local_offset = page_index_to_page_local_offset(start_byte_offset, start_page_index);

        uint64_t size_to_read = gDataSize, read_size = 0;
        for (uint64_t index = start_page_index; index <= end_page_index; index++) {
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
                num_cache_hit++;
            } else {
                PostProcessInfo ppi;
                ppi.index = index;
                ppi.src = (void *)((uint64_t)it->cache_buf + (index * cache_page_size));
                ctx[it->tid]->fam_get_nonblocking((void *)((uint64_t)it->cache_buf + (index * cache_page_size)),
                        it->item, index * cache_page_size, cache_page_size);
                if (index == start_page_index) {
                    read_size = size_to_read > (cache_page_size - start_page_local_offset) ?
                        (cache_page_size - start_page_local_offset) : gDataSize;
                    ppi.dest = LocalBuf;
                } else {
                    read_size = size_to_read > cache_page_size ? cache_page_size : size_to_read;
                    ppi.dest = (void *)((uint64_t)LocalBuf + (gDataSize - size_to_read));
                }
                ppi.read_size = read_size;
                PostProcessInfoArray.push_back(ppi);
                num_cache_miss++;
            }

            size_to_read -= read_size;
        }

        if (PostProcessInfoArray.size() > 0) {
            ctx[it->tid]->fam_quiet();
            for (auto & info : PostProcessInfoArray) {
                it->cache->Put(info.index, info.src);
                memcpy(info.dest, info.src, info.read_size);
            }
            PostProcessInfoArray.clear();
        }
    }

    pthread_exit(NULL);
}
#endif

void *func_blocking_put_multiple_region_item(void *arg) {
    ValueInfo *it = (ValueInfo *)arg;
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

    ValueInfo *infos = (ValueInfo *)malloc(sizeof(ValueInfo) * numThreads);

    std::string regionPrefix("region");
    std::string itemPrefix("item");
    EXPECT_NO_THROW(desc = my_fam->fam_create_region(regionPrefix.c_str(),
                BIG_REGION_SIZE, 0777, NULL));
    EXPECT_NE((void *)NULL, desc);

    EXPECT_NO_THROW(item = my_fam->fam_allocate(itemPrefix.c_str(), gItemSize, 0777, desc));
    EXPECT_NE((void *)NULL, item);

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
        infos[i].indexes = (uint64_t *) calloc(1, sizeof(uint64_t) * infos[i].num_ops);

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

    free(sum_probs);
    for (uint64_t i = 0; i < numThreads; i++) {
        free(infos[i].indexes);
    }
    free(infos);
    free(threads);
}

// Test case -  Blocking get test by multiple threads on same region and data item.
TEST(FamPutGet, BlockingFamGetSingleRegionDataItem) {
    uint64_t num_ios = gItemSize / gDataSize;
    pthread_t *threads = (pthread_t *)malloc(sizeof(pthread_t) * numThreads);
    int rc;

    Fam_Region_Descriptor **descs = (Fam_Region_Descriptor **)malloc(sizeof(Fam_Region_Descriptor *) * numThreads);
    Fam_Descriptor **items = (Fam_Descriptor **)malloc(sizeof(Fam_Descriptor *) * numThreads);

    ValueInfo *infos = (ValueInfo *)malloc(sizeof(ValueInfo) * numThreads);

#ifdef ENABLE_LOCAL_CACHE
    size_t cache_size = (size_t)((double)(((double)gItemSize / (double)cache_page_size) 
                / (double)numThreads) * cache_ratio);
    lru_cache_t<uint64_t, void *> **caches = (lru_cache_t<uint64_t, void *> **)malloc(sizeof(lru_cache_t<uint64_t, void *> *) * numThreads);
    for (uint64_t i = 0; i < numThreads; i++) {
        caches[i] = new lru_cache_t<uint64_t, void *>(cache_size);
        infos[i].cache = caches[i];
        infos[i].cache_buf = gCacheBuf;
        //infos[i].cache_buf = gCacheBuf + ((gItemSize / numThreads) * i);
    }
#endif

    std::string regionPrefix("region");
    std::string itemPrefix("item");
    EXPECT_NO_THROW(desc = my_fam->fam_create_region(regionPrefix.c_str(),
                BIG_REGION_SIZE, 0777, NULL));
    EXPECT_NE((void *)NULL, desc);

    EXPECT_NO_THROW(item = my_fam->fam_allocate(itemPrefix.c_str(), gItemSize, 0777, desc));
    EXPECT_NE((void *)NULL, item);

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
        infos[i].num_ops = num_indexes / numThreads;
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

#ifdef ENABLE_LOCAL_CACHE
    for (uint64_t i = 0; i < numThreads; i++) {
        if ((rc = pthread_create(&threads[i], NULL, 
                        func_blocking_cache_get_single_region_item_warmup, &infos[i]))) {
            fprintf(stderr, "error: pthread_create, rc: %d\n", rc);
            exit(1);
        }
    }

    for (uint64_t i = 0; i < numThreads; i++) {
        pthread_join(threads[i], NULL);
    }
#endif

    auto starttime = std::chrono::system_clock::now();
#ifdef ENABLE_LOCAL_CACHE
    for (uint64_t i = 0; i < numThreads; i++) {
        if ((rc = pthread_create(&threads[i], NULL, 
                        func_blocking_cache_get_single_region_item, &infos[i]))) {
            fprintf(stderr, "error: pthread_create, rc: %d\n", rc);
            exit(1);
        }
    }
#else
    for (uint64_t i = 0; i < numThreads; i++) {
        if ((rc = pthread_create(&threads[i], NULL, 
                        func_blocking_get_single_region_item, &infos[i]))) {
            fprintf(stderr, "error: pthread_create, rc: %d\n", rc);
            exit(1);
        }
    }
#endif

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
    printf("Cache hit ratio = %f \n", (double)((double)num_cache_hit / (double)num_ios) * 100.0);
    printf("Cache hit count = %lu\n", num_cache_hit);
    printf("Cache miss count = %lu\n", num_cache_miss);
#endif

    // Deallocating data items
    EXPECT_NO_THROW(my_fam->fam_deallocate(item));
    free(items);

    // Destroying the region
    EXPECT_NO_THROW(my_fam->fam_destroy_region(desc));
    free(descs);

    free(sum_probs);
    for (uint64_t i = 0; i < numThreads; i++) {
        free(infos[i].indexes);
#ifdef ENABLE_LOCAL_CACHE
        delete infos[i].cache;
#endif
    }
#ifdef ENABLE_LOCAL_CACHE
    free(caches);
#endif
    free(infos);
    free(threads);
}

// Test case -  NonBlocking get test by multiple threads on same region and data item.
TEST(FamPutGet, NonBlockingFamGetSingleRegionDataItem) {
    uint64_t num_ios = gItemSize / gDataSize;
    pthread_t *threads = (pthread_t *)malloc(sizeof(pthread_t) * numThreads);
    int rc;

    Fam_Region_Descriptor **descs = (Fam_Region_Descriptor **)malloc(sizeof(Fam_Region_Descriptor *) * numThreads);
    Fam_Descriptor **items = (Fam_Descriptor **)malloc(sizeof(Fam_Descriptor *) * numThreads);

    ValueInfo *infos = (ValueInfo *)malloc(sizeof(ValueInfo) * numThreads);

#ifdef ENABLE_LOCAL_CACHE
    size_t cache_size = (size_t)((double)(((double)gItemSize / (double)cache_page_size) / (double)numThreads) * cache_ratio);
    lru_cache_t<uint64_t, void *> **caches = (lru_cache_t<uint64_t, void *> **)malloc(sizeof(lru_cache_t<uint64_t, void *> *) * numThreads);
    for (uint64_t i = 0; i < numThreads; i++) {
        caches[i] = new lru_cache_t<uint64_t, void *>(cache_size);
        infos[i].cache = caches[i];
        infos[i].cache_buf = gCacheBuf;
        //infos[i].cache_buf = gCacheBuf + ((gItemSize / numThreads) * i);
    }
#endif

    std::string regionPrefix("region");
    std::string itemPrefix("item");
    EXPECT_NO_THROW(desc = my_fam->fam_create_region(regionPrefix.c_str(),
                BIG_REGION_SIZE, 0777, NULL));
    EXPECT_NE((void *)NULL, desc);

    EXPECT_NO_THROW(item = my_fam->fam_allocate(itemPrefix.c_str(), gItemSize, 0777, desc));
    EXPECT_NE((void *)NULL, item);

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
        infos[i].indexes = (uint64_t *) calloc(1, sizeof(uint64_t) * infos[i].num_ops);

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

#ifdef ENABLE_LOCAL_CACHE
    for (uint64_t i = 0; i < numThreads; i++) {
        if ((rc = pthread_create(&threads[i], NULL, 
                        func_non_blocking_cache_get_single_region_item_warmup, &infos[i]))) {
            fprintf(stderr, "error: pthread_create, rc: %d\n", rc);
            exit(1);
        }
    }

    for (uint64_t i = 0; i < numThreads; i++) {
        pthread_join(threads[i], NULL);
    }
#endif

    auto starttime = std::chrono::system_clock::now();
#ifdef ENABLE_LOCAL_CACHE
    for (uint64_t i = 0; i < numThreads; i++) {
        if ((rc = pthread_create(&threads[i], NULL, 
                        func_non_blocking_cache_get_single_region_item, &infos[i]))) {
            fprintf(stderr, "error: pthread_create, rc: %d\n", rc);
            exit(1);
        }
    }
#else
    for (uint64_t i = 0; i < numThreads; i++) {
        if ((rc = pthread_create(&threads[i], NULL, 
                        func_blocking_get_single_region_item, &infos[i]))) {
            fprintf(stderr, "error: pthread_create, rc: %d\n", rc);
            exit(1);
        }
    }
#endif

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
    printf("Cache hit ratio = %f \n", (double)((double)num_cache_hit / (double)num_ios) * 100.0);
    printf("Cache hit count = %lu\n", num_cache_hit);
    printf("Cache miss count = %lu\n", num_cache_miss);
#endif

    // Deallocating data items
    EXPECT_NO_THROW(my_fam->fam_deallocate(item));
    free(items);

    // Destroying the region
    EXPECT_NO_THROW(my_fam->fam_destroy_region(desc));
    free(descs);

    free(sum_probs);
    for (uint64_t i = 0; i < numThreads; i++) {
        free(infos[i].indexes);
#ifdef ENABLE_LOCAL_CACHE
        delete infos[i].cache;
#endif
    }
#ifdef ENABLE_LOCAL_CACHE
    free(caches);
#endif
    free(infos);
    free(threads);
}

// Test case -  Blocking put test by multiple threads on multiple regions and data items.
TEST(FamPutGet, BlockingFamPutMultipleRegionDataItem) {
    uint64_t num_ios = gItemSize / gDataSize, i;
    pthread_t *threads = (pthread_t *)malloc(sizeof(pthread_t) * numThreads);
    int rc;

    Fam_Region_Descriptor **descs = (Fam_Region_Descriptor **)malloc(sizeof(Fam_Region_Descriptor *) * numThreads);
    Fam_Descriptor **items = (Fam_Descriptor **)malloc(sizeof(Fam_Descriptor *) * numThreads);

    ValueInfo *infos = (ValueInfo *)malloc(sizeof(ValueInfo) * numThreads);

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

    free(infos);
    free(threads);
}

// Test case -  Blocking get test by multiple threads on multiple regions and data items.
TEST(FamPutGet, BlockingFamGetMultipleRegionDataItem) {
    uint64_t num_ios = gItemSize / gDataSize, i;
    pthread_t *threads = (pthread_t *)malloc(sizeof(pthread_t) * numThreads);
    int rc;

    Fam_Region_Descriptor **descs = (Fam_Region_Descriptor **)malloc(sizeof(Fam_Region_Descriptor *) * numThreads);
    Fam_Descriptor **items = (Fam_Descriptor **)malloc(sizeof(Fam_Descriptor *) * numThreads);

    ValueInfo *infos = (ValueInfo *)malloc(sizeof(ValueInfo) * numThreads);

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

    free(infos);
    free(threads);
}

// Test case -  Blocking put test by multiple threads on a single region and multiple data items.
TEST(FamPutGet, BlockingFamPutSingleRegionMultipleDataItem) {
    uint64_t num_ios = gItemSize / gDataSize, i;
    pthread_t *threads = (pthread_t *)malloc(sizeof(pthread_t) * numThreads);
    int rc;

    Fam_Region_Descriptor **descs = (Fam_Region_Descriptor **)malloc(sizeof(Fam_Region_Descriptor *) * numThreads);
    Fam_Descriptor **items = (Fam_Descriptor **)malloc(sizeof(Fam_Descriptor *) * numThreads);

    ValueInfo *infos = (ValueInfo *)malloc(sizeof(ValueInfo) * numThreads);

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

    free(infos);
    free(threads);
}

// Test case -  Blocking get test by multiple threads on a single region and multiple data items.
TEST(FamPutGet, BlockingFamGetSingleRegionMultipleDataItem) {
    uint64_t num_ios = gItemSize / gDataSize, i;
    pthread_t *threads = (pthread_t *)malloc(sizeof(pthread_t) * numThreads);
    int rc;

    Fam_Region_Descriptor **descs = (Fam_Region_Descriptor **)malloc(sizeof(Fam_Region_Descriptor *) * numThreads);
    Fam_Descriptor **items = (Fam_Descriptor **)malloc(sizeof(Fam_Descriptor *) * numThreads);

    ValueInfo *infos = (ValueInfo *)malloc(sizeof(ValueInfo) * numThreads);

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

    free(infos);
    free(threads);
}

int main(int argc, char **argv) {
    int ret;
    ::testing::InitGoogleTest(&argc, argv);

#ifdef ENABLE_LOCAL_CACHE
    if (argc == 7) {
        gDataSize = atoi(argv[1]);
        isRand = atoi(argv[2]);
        zipf = std::stod(argv[3]);
        numThreads = atoi(argv[4]);
        cache_ratio = std::stod(argv[5]);
        cache_page_size = std::atoi(argv[6]);
        if (cache_page_size == 0) {
            cache_page_size = gDataSize;
        }
    }
#else
    if (argc == 5) {
        gDataSize = atoi(argv[1]);
        isRand = atoi(argv[2]);
        zipf = std::stod(argv[3]);
        numThreads = atoi(argv[4]);
    }
#endif

    std::cout << "gDataSize = " << gDataSize << std::endl;
    std::cout << "isRand = " << isRand << std::endl;
    std::cout << "zipf = " << zipf << std::endl;
    std::cout << "numThreads = " << numThreads << std::endl;
#ifdef ENABLE_LOCAL_CACHE
    std::cout << "cache_ratio = " << cache_ratio << std::endl;
    std::cout << "cache_page_size = " << cache_page_size << std::endl;
#endif

    my_fam = new fam();

    init_fam_options(&fam_opts);

    gLocalBuf = (int64_t *)malloc(LOCAL_BUFFER_SIZE * numThreads);
    //gLocalBuf = (int64_t *)numa_alloc_onnode(LOCAL_BUFFER_SIZE * numThreads, 1);
    memset(gLocalBuf, 2, LOCAL_BUFFER_SIZE * numThreads);
#ifdef ENABLE_LOCAL_CACHE
#if 1
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
#endif

    fam_opts.local_buf_addr = gLocalBuf;
    fam_opts.local_buf_size = LOCAL_BUFFER_SIZE * numThreads;

    fam_opts.famThreadModel = strdup("FAM_THREAD_MULTIPLE");

    EXPECT_NO_THROW(my_fam->fam_initialize("default", &fam_opts));
    EXPECT_NO_THROW(myPE = (int *)my_fam->fam_get_option(strdup("PE_ID")));

    ctx = (fam_context **) malloc(sizeof(fam_context *) * numThreads);
    for (uint64_t i = 0; i < numThreads; i++) {
        ctx[i] = my_fam->fam_context_open();
    }

    ret = RUN_ALL_TESTS();

    for (uint64_t i = 0; i < numThreads; i++) {
        my_fam->fam_context_close(ctx[i]);
    }
    free(ctx);

    cout << "finished all testing" << endl;
    EXPECT_NO_THROW(my_fam->fam_finalize("default"));
    cout << "Finalize done : " << ret << endl;
    delete my_fam;
#ifdef ENABLE_LOCAL_CACHE
    free(gLocalBuf);
#if 1
    free(gCacheBuf);
#else
    munmap(gCacheBuf, gItemSize);
    close(fd);
#endif
#else
    free(gLocalBuf);
#endif
    return ret;
}
