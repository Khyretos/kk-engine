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

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <algorithm>
#include "FEMFXVectorMath.h"

#ifdef _MSC_VER
#define FM_FORCE_INLINE __forceinline
#define FM_RESTRICT __restrict
#define FM_ALIGN_OF(x) __alignof(x)
#define FM_ALIGN(x) __declspec(align(x))
#define FM_ALIGN_END(x)
#define FM_THREAD_LOCAL_STORAGE __declspec(thread)
#else
// Portability for GCC/Clang (Linux) — see README "GPU profiler
// (VulkanProfiler) integration" for the documentation style precedent
// on found-and-fixed upstream portability issues; this is the FEMFX
// equivalent. alignof/alignas/thread_local are the standard C++11
// equivalents of the three MSVC-specific constructs above. Checking
// _MSC_VER (the real compiler) rather than WIN32 (a platform define
// premake's own build script sets unconditionally, even for this
// Linux build) — see soa_float.h/sse_mathfun.h for AMD's own correct
// use of this same _MSC_VER pattern elsewhere in this codebase.
//
// FM_FORCE_INLINE specifically needs an #undef first: FEMFXVectorMath.h
// (included just above) already defines it via its own #ifndef guard,
// so redefining it here unconditionally produced a real "redefined"
// warning on nearly every FEMFX translation unit — found by a user
// building on real hardware who, reasonably, wanted a clean build
// rather than pages of warnings. Both paths resolve to the same
// effective value either way (this file's own -D__forceinline=inline
// compile definition makes them equivalent), so this changes zero
// behavior, only removes the warning.
#undef FM_FORCE_INLINE
#define FM_FORCE_INLINE inline
#define FM_RESTRICT __restrict__
#define FM_ALIGN_OF(x) alignof(x)
#define FM_ALIGN(x) alignas(x)
#define FM_ALIGN_END(x)
#define FM_THREAD_LOCAL_STORAGE thread_local
#endif

#ifdef _DEBUG
#include <assert.h>
#define FM_ASSERT(x) assert(x)
#else
#define FM_ASSERT(x)
#endif
#define FM_STATIC_ASSERT(x) static_assert(x, #x)

#define FM_DELETE(ptr) if (ptr) { delete (ptr); (ptr) = NULL; }
#define FM_DELETE_ARRAY(ptr) if (ptr) { delete [] (ptr); (ptr) = NULL; }

#define FM_PI 3.14159265358979323846f

namespace AMD
{
    typedef uint32_t uint;

    // Integer types in struct matching size/alignment of cache block
    struct FmAtomicUint
    {
        FM_ALIGN(64) uint32_t val FM_ALIGN_END(64);
        FmAtomicUint() {}
        FmAtomicUint(uint32_t inVal) { val = inVal; }
    };

    struct FmAtomicUint64
    {
        FM_ALIGN(64) uint64_t val FM_ALIGN_END(64);
        FmAtomicUint64() {}
        FmAtomicUint64(uint64_t inVal) { val = inVal; }
    };

    // Min/max
    static FM_FORCE_INLINE float FmMinFloat(float a, float b) { return std::min(a, b); }
    static FM_FORCE_INLINE float FmMaxFloat(float a, float b) { return std::max(a, b); }
    static FM_FORCE_INLINE int32_t FmMinInt(int32_t a, int32_t b) { return std::min(a, b); }
    static FM_FORCE_INLINE int32_t FmMaxInt(int32_t a, int32_t b) { return std::max(a, b); }
    static FM_FORCE_INLINE uint32_t FmMinUint(uint32_t a, uint32_t b) { return std::min(a, b); }
    static FM_FORCE_INLINE uint32_t FmMaxUint(uint32_t a, uint32_t b) { return std::max(a, b); }
}
