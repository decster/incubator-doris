// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#pragma once

#include "olap/memory/column_reader.h"
#include "util/hash_util.hpp"

namespace doris {
namespace memory {

// This method needs to be shared by reader/writer, so extract it as template function
template <class Reader, class T, bool Nullable, class ST>
inline const void* TypedColumnGet(const Reader& reader, const uint32_t rid) {
    for (ssize_t i = reader._deltas.size() - 1; i >= 0; i--) {
        ColumnDelta* pdelta = reader._deltas[i];
        uint32_t pos = pdelta->find_idx(rid);
        if (pos != DeltaIndex::npos) {
            if (Nullable) {
                bool isnull = pdelta->nulls() && pdelta->nulls().as<bool>()[pos];
                if (isnull) {
                    return nullptr;
                } else {
                    return &(pdelta->data().template as<ST>()[pos]);
                }
            } else {
                return &(pdelta->data().template as<ST>()[pos]);
            }
        }
    }
    uint32_t bid = rid >> 16;
    DCHECK(bid < reader._base->size());
    uint32_t idx = rid & 0xffff;
    DCHECK(idx * sizeof(ST) < (*reader._base)[bid]->data().bsize());
    if (Nullable) {
        bool isnull = (*reader._base)[bid]->is_null(idx);
        if (isnull) {
            return nullptr;
        } else {
            return &((*reader._base)[bid]->data().template as<ST>()[idx]);
        }
    } else {
        return &((*reader._base)[bid]->data().template as<ST>()[idx]);
    }
}

template <class T, class ST>
inline uint64_t TypedColumnHashcode(const void* rhs, size_t rhs_idx) {
    if (std::is_same<T, ST>::value) {
        const T* prhs = ((const T*)rhs) + rhs_idx;
        return HashUtil::fnv_hash64(prhs, sizeof(T), 0);
    } else {
        // TODO: support other type's hash
        return 0;
    }
}

template <class Reader, class T, bool Nullable, class ST>
bool TypedColumnEquals(const Reader& reader, const uint32_t rid, const void* rhs, size_t rhs_idx) {
    const T& rhs_value = ((const T*)rhs)[rhs_idx];
    for (ssize_t i = reader._deltas.size() - 1; i >= 0; i--) {
        ColumnDelta* pdelta = reader._deltas[i];
        uint32_t pos = pdelta->find_idx(rid);
        if (pos != DeltaIndex::npos) {
            if (Nullable) {
                CHECK(false) << "only used for key column";
                return false;
            } else {
                return (pdelta->data().template as<T>()[pos]) == rhs_value;
            }
        }
    }
    uint32_t bid = rid >> 16;
    DCHECK(bid < reader._base->size());
    uint32_t idx = rid & 0xffff;
    DCHECK(idx * sizeof(ST) < (*reader._base)[bid]->data().bsize());
    if (Nullable) {
        CHECK(false) << "only used for key column";
        return false;
    } else {
        DCHECK(rhs);
        return ((*reader._base)[bid]->data().template as<T>()[idx]) == rhs_value;
    }
}

// ColumnReader typed implementations
// currently only works for int8/int16/int32/int64/int128/float/double
// TODO: add string and other varlen type support
template <class T, bool Nullable = false, class ST = T>
class TypedColumnReader : public ColumnReader {
public:
    TypedColumnReader(Column* column, uint64_t version, uint64_t real_version,
                      vector<ColumnDelta*>&& deltas)
            : _column(column),
              _base(&_column->_base),
              _version(version),
              _real_version(real_version),
              _deltas(std::move(deltas)) {}

    const void* get(const uint32_t rid) const {
        return TypedColumnGet<TypedColumnReader<T, Nullable, ST>, T, Nullable, ST>(*this, rid);
    }

    Status get_block(size_t nrows, size_t block, ColumnBlockHolder* cbh) const {
        bool base_only = true;
        for (size_t i = 0; i < _deltas.size(); ++i) {
            if (_deltas[i]->contains_block(block)) {
                base_only = false;
                break;
            }
        }
        auto& page = (*_base)[block];
        if (base_only) {
            cbh->init(page.get(), false);
            return Status::OK();
        }
        if (!cbh->own() || cbh->get()->size() < nrows) {
            // need to create new column block
            cbh->release();
            cbh->init(new ColumnBlock(), true);
            cbh->get()->alloc(nrows, sizeof(T));
        }
        ColumnBlock& cb = *cbh->get();
        RETURN_IF_ERROR(page->copy_to(&cb, nrows, sizeof(ST)));
        for (auto delta : _deltas) {
            uint32_t start, end;
            delta->index()->block_range(block, &start, &end);
            if (end == start) {
                continue;
            }
            const uint16_t* poses = (const uint16_t*)delta->index()->data().data();
            const ST* data = delta->data().as<ST>();
            if (Nullable) {
                if (delta->nulls()) {
                    const bool* nulls = delta->nulls().as<bool>();
                    for (uint32_t i = start; i < end; i++) {
                        uint16_t pos = poses[i];
                        bool isnull = nulls[i];
                        if (isnull) {
                            cb.nulls().as<bool>()[pos] = true;
                        } else {
                            cb.nulls().as<bool>()[pos] = false;
                            cb.data().as<ST>()[pos] = data[i];
                        }
                    }
                } else {
                    for (uint32_t i = start; i < end; i++) {
                        uint16_t pos = poses[i];
                        cb.nulls().as<bool>()[pos] = true;
                        cb.data().as<ST>()[pos] = data[i];
                    }
                }
            } else {
                for (uint32_t i = start; i < end; i++) {
                    cb.data().as<ST>()[poses[i]] = data[i];
                }
            }
        }
        return Status::OK();
    }

    uint64_t hashcode(const void* rhs, size_t rhs_idx) const {
        return TypedColumnHashcode<T, ST>(rhs, rhs_idx);
    }

    bool equals(const uint32_t rid, const void* rhs, size_t rhs_idx) const {
        return TypedColumnEquals<TypedColumnReader<T, Nullable, ST>, T, Nullable, ST>(*this, rid,
                                                                                      rhs, rhs_idx);
    }

    string debug_string() const {
        return StringPrintf("%s version=%zu(real=%zu) ndelta=%zu", _column->debug_string().c_str(),
                            _version, _real_version, _deltas.size());
    }

private:
    template <class Reader, class T2, bool Nullable2, class ST2>
    friend const void* TypedColumnGet(const Reader& reader, const uint32_t rid);

    template <class T2, class ST2>
    friend bool TypedColumnHashcode(const void*, size_t);

    template <class Reader, class T2, bool Nullable2, class ST2>
    friend bool TypedColumnEquals(const Reader&, const uint32_t, const void*, size_t);

    scoped_refptr<Column> _column;
    vector<scoped_refptr<ColumnBlock>>* _base;
    uint64_t _version;
    uint64_t _real_version;
    vector<ColumnDelta*> _deltas;
};

} // namespace memory
} // namespace doris
