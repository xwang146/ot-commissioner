/*
 *    Copyright (c) 2019, The OpenThread Authors.
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
 *   This file includes definition of random generator.
 *
 */

#ifndef OT_COMM_LIBRARY_OPENTHREAD_RANDOM_HPP_
#define OT_COMM_LIBRARY_OPENTHREAD_RANDOM_HPP_

#include <stdint.h>

#include <commissioner/error.hpp>

namespace ot {

namespace commissioner {

namespace random {

namespace non_crypto {

/**
 * This function generates and returns a random `uint32_t` value.
 *
 * @returns    A random `uint32_t` value.
 *
 */
uint32_t GetUint32(void);

/**
 * This function generates and returns a random byte.
 *
 * @returns A random `uint8_t` value.
 *
 */
inline uint8_t GetUint8(void)
{
    return static_cast<uint8_t>(GetUint32() & 0xff);
}

/**
 * This function generates and returns a random `uint16_t` value.
 *
 * @returns A random `uint16_t` value.
 *
 */
inline uint16_t GetUint16(void)
{
    return static_cast<uint16_t>(GetUint32() & 0xffff);
}

/**
 * This function generates and returns a random `uint8_t` value within a given range `[aMin, aMax)`.
 *
 * @param[in]  aMin  A minimum value (this value can be included in returned random result).
 * @param[in]  aMax  A maximum value (this value is excluded from returned random result).
 *
 * @returns    A random `uint8_t` value in the given range (i.e., aMin <= random value < aMax).
 */
inline uint8_t GetUint8InRange(uint8_t aMin, uint8_t aMax)
{
    return (aMin + (GetUint8() % (aMax - aMin)));
}

/**
 * This function generates and returns a random `uint16_t` value within a given range `[aMin,
 * aMax)`.
 *
 * @note The returned random value can include the @p aMin value but excludes the @p aMax.
 *
 * @param[in]  aMin  A minimum value (this value can be included in returned random result).
 * @param[in]  aMax  A maximum value (this value is excluded from returned random result).
 *
 * @returns    A random `uint16_t` value in the given range (i.e., aMin <= random value < aMax).
 */
inline uint16_t GetUint16InRange(uint16_t aMin, uint16_t aMax)
{
    return (aMin + (GetUint16() % (aMax - aMin)));
}

/**
 * This function generates and returns a random `uint32_t` value within a given range `[aMin,
 * aMax)`.
 *
 * @note The returned random value can include the @p aMin value but excludes the @p aMax.
 *
 * @param[in]  aMin  A minimum value (this value can be included in returned random result).
 * @param[in]  aMax  A maximum value (this value is excluded from returned random result).
 *
 * @returns    A random `uint32_t` value in the given range (i.e., aMin <= random value < aMax).
 *
 */
inline uint32_t GetUint32InRange(uint32_t aMin, uint32_t aMax)
{
    return (aMin + (GetUint32() % (aMax - aMin)));
}

/**
 * This function fills a given buffer with random bytes.
 *
 * @param[out] aBuffer  A pointer to a buffer to fill with the random bytes.
 * @param[in]  aSize    Size of buffer (number of bytes to fill).
 *
 */
inline void FillBuffer(uint8_t *aBuffer, uint16_t aSize)
{
    while (aSize-- != 0)
    {
        *aBuffer++ = GetUint8();
    }
}

/**
 * This function adds a random jitter within a given range to a given value.
 *
 * @param[in]  aValue     A value to which the random jitter is added.
 * @param[in]  aJitter    Maximum jitter. Random jitter is selected from the range `[-aJitter,
 * aJitter]`.
 *
 * @returns    The given value with an added random jitter.
 *
 */
inline uint32_t AddJitter(uint32_t aValue, uint16_t aJitter)
{
    aJitter = (aJitter <= aValue) ? aJitter : static_cast<uint16_t>(aValue);

    return aValue + GetUint32InRange(0, 2 * aJitter + 1) - aJitter;
}

/**
 * This function seeds a software random generator.
 *
 * @param[in]  aSeed  Seed value.
 *
 */
void Seed(uint32_t aSeed);

} // namespace non_crypto

namespace crypto {

// Removed since not used by commissioner.

} // namespace crypto

} // namespace random

} // namespace commissioner

} // namespace ot

#endif // OT_COMM_LIBRARY_OPENTHREAD_RANDOM_HPP_
