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
#include <chrono>

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <sys/mman.h>

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

typedef struct {
    Fam_Descriptor *item;
    uint64_t tid;
    uint64_t num_ops;
    uint64_t *indexes;
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

    for (uint64_t i = 0; i < it->num_ops; i++) {
        offset = it->indexes[i] * gDataSize;
        my_fam->fam_put_blocking(gLocalBuf, it->item, offset, gDataSize);
    }

    pthread_exit(NULL);
}

void *func_blocking_get_single_region_item(void *arg) {
    ValueInfo *it = (ValueInfo *)arg;
    uint64_t offset = 0;

    for (uint64_t i = 0; i < it->num_ops; i++) {
        offset = it->indexes[i] * gDataSize;
        my_fam->fam_put_blocking(gLocalBuf, it->item, offset, gDataSize);
    }

    pthread_exit(NULL);
}

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

// Test case 1 -  Blocking put test by multiple threads on same region and data item.
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

// Test case 2 -  Blocking get test by multiple threads on same region and data item.
TEST(FamPutGet, BlockingFamGetSingleRegionDataItem) {
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

    auto starttime = std::chrono::system_clock::now();
    for (uint64_t i = 0; i < numThreads; i++) {
        if ((rc = pthread_create(&threads[i], NULL, 
                        func_blocking_get_single_region_item, &infos[i]))) {
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

// Test case 3 -  Blocking put test by multiple threads on multiple regions and data items.
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

// Test case 4 -  Blocking get test by multiple threads on multiple regions and data items.
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

// Test case 5 -  Blocking put test by multiple threads on a single region and multiple data items.
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

// Test case 6 -  Blocking get test by multiple threads on a single region and multiple data items.
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

    if (argc == 5) {
        gDataSize = atoi(argv[1]);
        isRand = atoi(argv[2]);
        zipf = std::stod(argv[3]);
        numThreads = atoi(argv[4]);
    }

    std::cout << "gDataSize = " << gDataSize << std::endl;
    std::cout << "isRand = " << isRand << std::endl;
    std::cout << "zipf = " << zipf << std::endl;
    std::cout << "numThreads = " << numThreads << std::endl;

    my_fam = new fam();

    init_fam_options(&fam_opts);

    gLocalBuf = (int64_t *)malloc(gDataSize);
    fam_opts.local_buf_addr = gLocalBuf;
    fam_opts.local_buf_size = gDataSize;

    fam_opts.famThreadModel = strdup("FAM_THREAD_MULTIPLE");

    EXPECT_NO_THROW(my_fam->fam_initialize("default", &fam_opts));
    EXPECT_NO_THROW(myPE = (int *)my_fam->fam_get_option(strdup("PE_ID")));

    ret = RUN_ALL_TESTS();

    cout << "finished all testing" << endl;
    EXPECT_NO_THROW(my_fam->fam_finalize("default"));
    cout << "Finalize done : " << ret << endl;
    delete my_fam;
    return ret;
}
