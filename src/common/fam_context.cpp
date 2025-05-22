/*
 * fam_context.cpp
 * Copyright (c) 2019, 2022-2023 Hewlett Packard Enterprise Development, LP. All
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

#include <iostream>
#include <sstream>
#include <string.h>
#include <vector>

#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>

#include "common/fam_context.h"
#include "common/fam_libfabric.h"
#include "common/fam_options.h"
#include "common/fam_local_buf_reg_helper.h"

namespace openfam {

Fam_Context::Fam_Context(Fam_Thread_Model famTM)
    : numTxOps(0), numRxOps(0), isNVMM(true) {
    numLastRxFailCnt = 0;
    numLastTxFailCnt = 0;
    // Initialize ctxRWLock
    famThreadModel = famTM;
    if (famThreadModel == FAM_THREAD_MULTIPLE)
        pthread_rwlock_init(&ctxRWLock, NULL);
}

Fam_Context::Fam_Context(struct fi_info *fi, struct fid_domain *domain,
                         Fam_Thread_Model famTM) {
    std::ostringstream message;
    numTxOps = numRxOps = 0;
    isNVMM = false;
    numLastRxFailCnt = 0;
    numLastTxFailCnt = 0;

    fi->caps = FI_RMA | FI_WRITE | FI_READ | FI_ATOMIC | FI_REMOTE_WRITE |
               FI_REMOTE_READ;
    fi->tx_attr->op_flags = FI_DELIVERY_COMPLETE;
    fi->mode = 0;
    fi->tx_attr->mode = 0;
    fi->rx_attr->mode = 0;

    // Initialize ctxRWLock
    famThreadModel = famTM;
    if (famThreadModel == FAM_THREAD_MULTIPLE)
        pthread_rwlock_init(&ctxRWLock, NULL);

    int ret = fi_endpoint(domain, fi, &ep, NULL);
    if (ret < 0) {
        message << "Fam libfabric fi_endpoint failed: " << fabric_strerror(ret);
        THROW_ERR_MSG(Fam_Datapath_Exception, message.str().c_str());
    }

    struct fi_cq_attr cq_attr;
    memset(&cq_attr, 0, sizeof(cq_attr));
    cq_attr.format = FI_CQ_FORMAT_DATA;
    if ((strncmp(fi->fabric_attr->prov_name, "cxi", 3) != 0)) {
        cq_attr.wait_obj = FI_WAIT_UNSPEC;
    }
    cq_attr.wait_cond = FI_CQ_COND_NONE;

    /* Set the cq size unless a default is in play. */
    if (!getenv("FI_CXI_DEFAULT_CQ_SIZE"))
        cq_attr.size = fi->tx_attr->size;
    ret = fi_cq_open(domain, &cq_attr, &txcq, &txcq);
    if (ret < 0) {
        message << "Fam libfabric fi_cq_open failed: " << fabric_strerror(ret);
        THROW_ERR_MSG(Fam_Datapath_Exception, message.str().c_str());
    }
    cq_attr.size = fi->rx_attr->size;
    ret = fi_cq_open(domain, &cq_attr, &rxcq, &rxcq);
    if (ret < 0) {
        message << "Fam libfabric fi_cq_open failed: " << fabric_strerror(ret);
        THROW_ERR_MSG(Fam_Datapath_Exception, message.str().c_str());
    }

    ret = fi_ep_bind(ep, &txcq->fid, FI_TRANSMIT | FI_SELECTIVE_COMPLETION);
    if (ret < 0) {
        message << "Fam libfabric fi_ep_bind failed: " << fabric_strerror(ret);
        THROW_ERR_MSG(Fam_Datapath_Exception, message.str().c_str());
    }

    ret = initialize_cntr(domain, &txCntr);
    if (ret < 0) {
        // print_fierr("initialize_cntr", ret);
        // return -1;
        // throws exception in initialize_cntr
    }

    ret = fi_ep_bind(ep, &txCntr->fid, FI_WRITE | FI_SEND | FI_REMOTE_WRITE);
    if (ret < 0) {
        message << "Fam libfabric fi_ep_bind failed: " << fabric_strerror(ret);
        THROW_ERR_MSG(Fam_Datapath_Exception, message.str().c_str());
    }

    ret = fi_ep_bind(ep, &rxcq->fid, FI_RECV);
    if (ret < 0) {
        message << "Fam libfabric fi_ep_bind failed: " << fabric_strerror(ret);
        THROW_ERR_MSG(Fam_Datapath_Exception, message.str().c_str());
    }

    ret = initialize_cntr(domain, &rxCntr);
    if (ret < 0) {
        // print_fierr("initialize_cntr", ret);
        // return -1;
        // throws exception in initialize_cntr
    }

    ret = fi_ep_bind(ep, &rxCntr->fid, FI_READ | FI_RECV | FI_REMOTE_READ);
    if (ret < 0) {
        message << "Fam libfabric fi_ep_bind failed: " << fabric_strerror(ret);
        THROW_ERR_MSG(Fam_Datapath_Exception, message.str().c_str());
    }
}

Fam_Context::~Fam_Context() {
    if (!isNVMM) {
        fi_close(&ep->fid);
        fi_close(&txcq->fid);
        fi_close(&rxcq->fid);
        fi_close(&txCntr->fid);
        fi_close(&rxCntr->fid);
    }
    pthread_rwlock_destroy(&ctxRWLock);
}

int Fam_Context::initialize_cntr(struct fid_domain *domain,
                                 struct fid_cntr **cntr) {
    int ret = 0;
    struct fi_cntr_attr cntrAttr;
    std::ostringstream message;

    memset(&cntrAttr, 0, sizeof(cntrAttr));
    cntrAttr.events = FI_CNTR_EVENTS_COMP;
    cntrAttr.wait_obj = FI_WAIT_UNSPEC;

    ret = fi_cntr_open(domain, &cntrAttr, cntr, cntr);
    if (ret < 0) {
        message << "Fam libfabric fi_cntr_open failed: "
                << fabric_strerror(ret);
        THROW_ERR_MSG(Fam_Datapath_Exception, message.str().c_str());
    }

    return ret;
}

void Fam_Context::register_heap(void *base, size_t len,
                                struct fid_domain *domain, size_t iov_limit) {
    std::ostringstream message;

    std::lock_guard<std::mutex> lock(globalBufMapMutex);

    size_t start = (size_t)base, end = (size_t)base + len;

    // Get the pointer to the current map
    // Be careful not to add a reference.
    auto mapIndex = currentBufMapIndex;
    auto sharedMapPtr = wait_for_map(mapIndex);
    auto mapPtr = sharedMapPtr.get();

    auto result = test_overlap(mapPtr, start, end);
    if (result.first != 0) {
        if (result.first == -1) {
            message << "Fam_Context register_heap() failed: Overlapping range [" << start << ", " << end << ") overlaps with ["
                    << result.second->start_ << ", " << result.second->end_ << ")";
            THROW_ERR_MSG(Fam_Datapath_Exception, message.str().c_str());
        } else if (result.first == -2) {
            message << "Fam_Context register_heap() failed: Invalid memory range, start must be less than end.";
            THROW_ERR_MSG(Fam_Datapath_Exception, message.str().c_str());
        }
    }
    
    auto buffer = create_new_buffer(base, len, domain, iov_limit);
    mapPtr->operator[](start) = buffer;
    mapIndex = publish_maps(mapIndex);
    for (size_t i = 1; i < numGlobalBufMaps; ++i) {
        sharedMapPtr = wait_for_map(mapIndex);
        mapPtr = sharedMapPtr.get();
        mapPtr->operator[](start) = buffer;
        mapIndex = (mapIndex + 1) % numGlobalBufMaps;
    }

    // Sanity check: all maps should have the same size
    auto map_size = globalBufMaps[0].get()->size();
    for (size_t i = 1; i < numGlobalBufMaps; ++i) {
        if (globalBufMaps[i].get()->size() != map_size) {
            message << "Fam_Context register_heap() failed: Map sizes do not match after insertion.";
            THROW_ERR_MSG(Fam_Datapath_Exception, message.str().c_str());
        }
    }
}

void Fam_Context::deregister_heap(void *base, size_t len) {
    std::ostringstream message;

    std::unique_lock<std::mutex> lock(globalBufMapMutex);

    size_t start = (size_t)base, end = (size_t)base + len;

    // Update first map and publish it.
    // Be careful not to add a reference,
    auto mapIndex = currentBufMapIndex;
    auto sharedMapPtr = wait_for_map(mapIndex);
    auto mapPtr = sharedMapPtr.get();
    auto search = mapPtr->find(start);
    if (search != mapPtr->end()) {
        if (search->second->start_ == start && search->second->end_ == end) {
            auto buffer = search->second;
            mapPtr->erase(buffer->start_);
            mapIndex = publish_maps(mapIndex);

            // Update the other maps
            for (size_t i = 1; i < numGlobalBufMaps; ++i) {
                sharedMapPtr = wait_for_map(mapIndex);
                mapPtr = sharedMapPtr.get();
                mapPtr->erase(buffer->start_);
                mapIndex = (mapIndex + 1) % numGlobalBufMaps;
            }

            // Sanity check: all maps should have the same size
            auto map_size = globalBufMaps[0].get()->size();
            for (size_t i = 1; i < numGlobalBufMaps; ++i) {
                if (globalBufMaps[i].get()->size() != map_size) {
                    message << "Fam_Context deregister_heap() failed: Map sizes do not match after deletion.";
                    THROW_ERR_MSG(Fam_Datapath_Exception, message.str().c_str());
                }
            }
        
            //  We're the only reference left.
            buffer.reset();
        }
    }
}

void **Fam_Context::get_mr_descs(const void *local_addr, size_t local_size) {
    void **ret = nullptr;
    size_t start = (size_t)local_addr, end = (size_t)local_addr + local_size;
    
#if 0
    MapPtr mapPtr = sequencedMapPtr.get();
#else
    SharedMapPtr sharedMapPtr = publishedMapPtr.load();
    MapPtr mapPtr = sharedMapPtr.get();
#endif
    
    auto result = test_overlap(mapPtr, start, end);
    if (result.first == 1) {
        ret = result.second->mr_descs_;
    }

    return ret;
}

} // namespace openfam
