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

#include "olap/memory/buffer.h"
#include "olap/memory/common.h"

namespace doris {
namespace memory {

// ColumnBlock stores one block of data for a Column
class ColumnBlock : public RefCountedThreadSafe<ColumnBlock> {
    ColumnBlock() = default;

    size_t memory() const;

    Buffer& data() { return _data; }

    Buffer& nulls() { return _nulls; }

    Status alloc(size_t size, size_t esize);

    bool is_null(uint32_t idx) const { return _nulls && _nulls.as<bool>()[idx]; }

    Status set_null(uint32_t idx);

    Status set_not_null(uint32_t idx);

private:
    size_t _size = 0;
    Buffer _nulls;
    Buffer _data;
    DISALLOW_COPY_AND_ASSIGN(ColumnBlock);
};

} // namespace memory
} // namespace doris
