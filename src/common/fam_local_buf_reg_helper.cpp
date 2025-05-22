/*
 * fam_local_buf_reg_helper.cpp
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

#include <sstream>
#include <sched.h>

#include "common/fam_internal_exception.h"
#include "common/fam_libfabric.h"
#include "common/fam_local_buf_reg_helper.h"

namespace openfam {

std::mutex globalBufMapMutex;
size_t currentBufMapIndex = 0;
std::vector<SharedMapPtr> globalBufMaps;
size_t numGlobalBufMaps = 2;

AlignedAtomicMapPtr publishedMapPtr;
AlignedSequence publishedSequence;

thread_local SequencedMapPtr sequencedMapPtr;

std::shared_ptr<RegisteredBuffer> create_new_buffer(void *base, size_t len,
                                                  struct fid_domain *domain,
                                                  size_t iov_limit) {
    std::ostringstream message;
    int ret;

    struct fid_mr *new_mr = NULL;

    void **new_mr_descs = (void **)calloc(iov_limit, sizeof(*new_mr_descs));
    if (!new_mr_descs) {
        message << "Fam_Context register_heap() failed to allocate memory";
        THROW_ERR_MSG(Fam_Datapath_Exception, message.str().c_str());
    }

    ret = fi_mr_reg(domain, base, len, FI_READ | FI_WRITE, 0, 0, 0, &new_mr, 0);
    if (ret < 0) {
        message << "Fam libfabric fi_mr_reg failed: " << fabric_strerror(ret);
        THROW_ERR_MSG(Fam_Datapath_Exception, message.str().c_str());
    }

    for (size_t i = 0; i < iov_limit; i++)
        new_mr_descs[i] = fi_mr_desc(new_mr);

    return std::make_shared<RegisteredBuffer>(new_mr_descs, new_mr, (size_t)base, (size_t)base + len);
}

SharedMapPtr& wait_for_map(size_t mapIndex) {
    auto & globalBufMap = globalBufMaps[mapIndex];
    while (globalBufMap.use_count() > 1) {
        sched_yield();
    }
    return globalBufMap;
}

size_t publish_maps(size_t mapIndex) {
    // globalBufMapMutex must be locked.
    auto & globalBufMap = globalBufMaps[mapIndex];
#if __cplusplus >= 202002L
    publishedMapPtr.atomic_ptr_.store(globalMap);
#else
    publishedMapPtr.store(globalBufMap);
#endif
    currentBufMapIndex = (mapIndex + 1) % numGlobalBufMaps;
    return currentBufMapIndex; 
}

std::pair<int, RegisteredBuffer *>
test_overlap(MapPtr mapPtr, size_t start, size_t end) {
    if (start >= end) {
        // Invalid memory range: start must be less than end
        return std::make_pair(-2, nullptr);
    }

    if (!mapPtr->size()) {
        // Map is empty: nothing overlaps or contains
        return std::make_pair(0, nullptr);
    }

    auto lower_start = mapPtr->lower_bound(start);
    if (lower_start == mapPtr->end()) {
        // Back up to last valid entry.
        lower_start--;
    } else if (start < lower_start->first) {
        if (end > lower_start->first) {
            // Something must overlap.
            return std::make_pair(-1, lower_start->second.get());
        }
        if (lower_start == mapPtr->begin()) {
            // end before first entry: nothing overlaps or contains
            return std::make_pair(0, nullptr);
        }
        lower_start--;
    }

    // Now start >= lower_start->first
    auto regBuf = lower_start->second.get();
    auto regEnd = regBuf->end_;
    if (end <= regEnd) {
        // Contains
        return std::make_pair(1, regBuf);
    }
    if (start < regEnd) {
        // Overlaps
        return std::make_pair(-1, regBuf);
    }

    return std::make_pair(0, nullptr);
}

} // namespace openfam
