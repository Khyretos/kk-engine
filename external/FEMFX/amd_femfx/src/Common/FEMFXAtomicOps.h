/*
MIT License

Copyright (c) 2019 Advanced Micro Devices, Inc.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

//---------------------------------------------------------------------------------------
// API for platform atomic operations
//---------------------------------------------------------------------------------------

#pragma once

#include "FEMFXCommon.h"

#ifdef _MSC_VER
#include <Windows.h>
#endif

namespace AMD
{
    // Atomic operation interface

#ifdef _MSC_VER
    // Atomically increment and return new value
    static FM_FORCE_INLINE uint FmAtomicIncrement(uint* pValue)
    {
        return InterlockedIncrement((volatile unsigned long *)pValue);
    }

    // Atomically decrement and return new value
    static FM_FORCE_INLINE uint FmAtomicDecrement(uint* pValue)
    {
        return InterlockedDecrement((volatile unsigned long *)pValue);
    }

    // Atomically add to *pValue and return result
    static FM_FORCE_INLINE uint FmAtomicAdd(uint* pValue, uint addedValue)
    {
        return (uint)InterlockedAdd((volatile LONG *)pValue, (LONG)addedValue);
    }

    // Atomically subtract from *pValue and return result
    static FM_FORCE_INLINE uint FmAtomicSub(uint* pValue, uint subtractedValue)
    {
        return (uint)InterlockedAdd((volatile LONG *)pValue, -(LONG)subtractedValue);
    }

    // Atomically OR with *pValue and return result
    static FM_FORCE_INLINE uint FmAtomicOr(uint* pValue, uint orValue)
    {
        return (uint)InterlockedOr((volatile LONG *)pValue, (LONG)orValue);
    }

    // Atomically replace *pValue with newValue if currently equal to compareValue, and return the initial value
    static FM_FORCE_INLINE uint FmAtomicCompareExchange(uint* pValue, uint newValue, uint compareValue)
    {
        return InterlockedCompareExchange((volatile unsigned long *)pValue, newValue, compareValue);
    }

    // Atomically read current *pValue
    static FM_FORCE_INLINE uint FmAtomicRead(uint* pValue)
    {
        uint value = *(volatile unsigned long*)pValue;

        // Ensure subsequent loads ordered after read of value
        _mm_lfence();

        return value;
    }

    // Atomically write to *pValue
    static FM_FORCE_INLINE uint FmAtomicWrite(uint* pValue, uint newValue)
    {
        return InterlockedExchange((volatile unsigned long *)pValue, newValue);
    }
#else
    // Portability for GCC/Clang (Linux), using the standard __atomic_*
    // builtins. Each function's return value is matched to the exact
    // semantic MSVC's Interlocked* documents above it returns — e.g.
    // FmAtomicWrite/FmAtomicCompareExchange both return the value that
    // was there BEFORE the operation, not the new value or a bool — see
    // README "GPU profiler (VulkanProfiler) integration" for the
    // documentation-style precedent on found-and-fixed upstream
    // portability issues; this is the FEMFX equivalent, for a genuinely
    // platform-specific API rather than an MSVC-only spelling of a
    // portable concept.

    // Atomically increment and return new value
    static FM_FORCE_INLINE uint FmAtomicIncrement(uint* pValue)
    {
        return __atomic_add_fetch(pValue, 1u, __ATOMIC_SEQ_CST);
    }

    // Atomically decrement and return new value
    static FM_FORCE_INLINE uint FmAtomicDecrement(uint* pValue)
    {
        return __atomic_sub_fetch(pValue, 1u, __ATOMIC_SEQ_CST);
    }

    // Atomically add to *pValue and return result
    static FM_FORCE_INLINE uint FmAtomicAdd(uint* pValue, uint addedValue)
    {
        return __atomic_add_fetch(pValue, addedValue, __ATOMIC_SEQ_CST);
    }

    // Atomically subtract from *pValue and return result
    static FM_FORCE_INLINE uint FmAtomicSub(uint* pValue, uint subtractedValue)
    {
        return __atomic_sub_fetch(pValue, subtractedValue, __ATOMIC_SEQ_CST);
    }

    // Atomically OR with *pValue and return result
    static FM_FORCE_INLINE uint FmAtomicOr(uint* pValue, uint orValue)
    {
        return __atomic_or_fetch(pValue, orValue, __ATOMIC_SEQ_CST);
    }

    // Atomically replace *pValue with newValue if currently equal to compareValue, and return the initial value
    static FM_FORCE_INLINE uint FmAtomicCompareExchange(uint* pValue, uint newValue, uint compareValue)
    {
        uint expected = compareValue;
        // On success, 'expected' is left untouched (== compareValue); on
        // failure, __atomic_compare_exchange_n writes the CURRENT value
        // into 'expected' — either way, that's exactly "the initial
        // value" MSVC's InterlockedCompareExchange documents returning.
        __atomic_compare_exchange_n(pValue, &expected, newValue, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
        return expected;
    }

    // Atomically read current *pValue
    static FM_FORCE_INLINE uint FmAtomicRead(uint* pValue)
    {
        return __atomic_load_n(pValue, __ATOMIC_SEQ_CST);
    }

    // Atomically write to *pValue, returning the value that was there before
    static FM_FORCE_INLINE uint FmAtomicWrite(uint* pValue, uint newValue)
    {
        return __atomic_exchange_n(pValue, newValue, __ATOMIC_SEQ_CST);
    }
#endif

    // Atomically compute max
    void FmAtomicMax(uint* pValue, uint newValue);
}