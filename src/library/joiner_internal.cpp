/*
 *    Copyright (c) 2019, The OpenThread Commissioner Authors.
 *    All rights reserved.
 *
 *    Redistribution and use in source and binary forms, with or without
 *    modification, are permitted provided that the following conditions are met:
 *    1. Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *    2. Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *    3. Neither the name of the copyright holder nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *    ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *    LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *    ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *    POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file
 *   This file implements internal joiner-specific functions.
 */

#include "library/joiner_internal.hpp"

#include "common/utils.hpp"

namespace ot {

namespace commissioner {

ByteArray ComputeJoinerId(uint64_t aEui64)
{
    Sha256    sha256;
    uint8_t   hash[Sha256::kHashSize];
    ByteArray eui64;

    utils::Encode(eui64, aEui64);

    sha256.Start();
    sha256.Update(&eui64[0], eui64.size());
    sha256.Finish(hash);

    static_assert(sizeof(hash) >= kJoinerIdLength, "wrong crypto::Sha256::kHashSize value");

    ByteArray joinerId{hash, hash + kJoinerIdLength};
    joinerId[0] |= kLocalExternalAddrMask;
    return joinerId;
}

ByteArray ComputeJoinerId(const JoinerDiscerner &aDiscerner)
{
    static_assert(sizeof(aDiscerner.mData) == kJoinerIdLength, "invalid discerner length");
    return utils::Hex(aDiscerner.mData);
}

} // namespace commissioner

} // namespace ot
