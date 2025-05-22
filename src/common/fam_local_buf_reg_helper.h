/*
 * fam_local_buf_reg_helper.h
 * Copyright (c) 2025 Hewlett Packard Enterprise Development, LP. All
 * rights reserved. Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following conditions
 * are met:
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
#ifndef FAM_LOCAL_BUF_REG_HELPER_H
#define FAM_LOCAL_BUF_REG_HELPER_H

#include <map>
#include <memory>
#include <atomic>
#include <mutex>
#include <vector>
#include <cstdlib>

#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>

namespace openfam {

class RegisteredBuffer {
  public:
    RegisteredBuffer(void **mr_descs, struct fid_mr *mr, size_t start, size_t end) : mr_descs_(mr_descs), mr_(mr), start_(start), end_(end) {};

    ~RegisteredBuffer() {
        free(mr_descs_);
        fi_close(&mr_->fid);
    };

    void **mr_descs_;
    struct fid_mr *mr_;
    size_t start_, end_;
};

using MapType = std::map<size_t, std::shared_ptr<RegisteredBuffer>>;
using MapPtr = MapType *;
using SharedMapPtr = std::shared_ptr<MapType>;

#if __cplusplus >= 202002L
struct alignas(64) AlignedAtomicMapPtr {
    std::atomic<SharedMapPtr> atomic_ptr_;

    SharedMapPtr load() const {
        return atomic_ptr_.load(std::memory_order_acquire);
    }

    AlignedAtomicMapPtr() = default;
    AlignedAtomicMapPtr(const AlignedAtomicMapPtr&) = delete;
    AlignedAtomicMapPtr& operator=(const AlignedAtomicMapPtr&) = delete;
    AlignedAtomicMapPtr(AlignedAtomicMapPtr&& other): atomic_ptr_(other.atomic_ptr_.load()) {}
    AlignedAtomicMapPtr& operator=(AlignedAtomicMapPtr&&) = delete;
};
#else
struct alignas(64) AlignedAtomicMapPtr {
    SharedMapPtr atomic_ptr_;
    SharedMapPtr load() const { 
        return std::atomic_load_explicit(&atomic_ptr_, std::memory_order_acquire); 
    }
    void store(const SharedMapPtr& value) {
        std::atomic_store_explicit(&atomic_ptr_, value, std::memory_order_release);
    }
    AlignedAtomicMapPtr() = default;
    AlignedAtomicMapPtr(const AlignedAtomicMapPtr&) = delete;
    AlignedAtomicMapPtr& operator=(const AlignedAtomicMapPtr&) = delete;
    AlignedAtomicMapPtr(AlignedAtomicMapPtr&& other): atomic_ptr_(std::atomic_load(&other.atomic_ptr_)) {}
    AlignedAtomicMapPtr& operator=(AlignedAtomicMapPtr&&) = delete;
};
#endif

struct alignas(64) AlignedSequence {
    std::atomic<size_t> sequence_;
    void increment() { sequence_.fetch_add(1, std::memory_order_release); }
    size_t load() const { return sequence_.load(std::memory_order_acquire); }
};

extern std::mutex globalBufMapMutex;
extern size_t currentBufMapIndex;
extern std::vector<SharedMapPtr> globalBufMaps;
extern size_t numGlobalBufMaps;

extern AlignedAtomicMapPtr publishedMapPtr;
extern AlignedSequence publishedSequence;

extern thread_local SequencedMapPtr sequencedMapPtr;

class SequencedMapPtr {
public:
    SequencedMapPtr() : lastSeen_(0), sharedMapPtr_(nullptr), mapPtr_(nullptr) {}

    MapPtr get() {
        auto latestSequence = publishedSequence.load();
        if (lastSeen_ != latestSequence) {
            lastSeen_ = latestSequence;
            sharedMapPtr_ = publishedMapPtr.load();
            mapPtr_ = sharedMapPtr_.get();
        }
        return mapPtr_;
    }
    
private:
    size_t lastSeen_;
    SharedMapPtr sharedMapPtr_;
    MapPtr mapPtr_;
};

std::shared_ptr<RegisteredBuffer> create_new_buffer(void *base, size_t len,
                                                    struct fid_domain *domain,
                                                    size_t iov_limit);
SharedMapPtr& wait_for_map(size_t mapIndex);
size_t publish_maps(size_t mapIndex);
std::pair<int, RegisteredBuffer *> test_overlap(MapPtr mapPtr, size_t start, size_t end);

} // namespace openfam
#endif