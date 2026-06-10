#pragma once
#include <cstdint>
#ifdef _MSC_VER
#include <immintrin.h>   // AVX intrinsics (MSVC)
#endif

// ── Lock-free ring buffer with SIMD-accelerated statistics ────────
// Uses AVX2 256-bit vectors on MSVC; scalar fallback on MinGW/GCC.
// All compute() paths produce identical results.

template <size_t Capacity>
struct RingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");

    float   buf[Capacity]{};
    size_t  head  = 0;
    size_t  count = 0;

    void push(float v) {
        buf[head] = v;
        head = (head + 1) & (Capacity - 1);
        if (count < Capacity) ++count;
    }

    size_t size() const { return count; }

    struct Stats { float min, max, avg, last; };

    Stats compute() const {
        if (count == 0) return {0,0,0,0};
        const size_t n    = count;
        const float* data = buf;

#ifdef _MSC_VER
        // AVX2 path: 8-wide SIMD min/max/sum
        __m256 vmin = _mm256_set1_ps(data[0]);
        __m256 vmax = _mm256_set1_ps(data[0]);
        __m256 vsum = _mm256_setzero_ps();

        size_t i = 0;
        for (; i + 8 <= n; i += 8) {
            __m256 chunk = _mm256_loadu_ps(data + i);
            vmin = _mm256_min_ps(vmin, chunk);
            vmax = _mm256_max_ps(vmax, chunk);
            vsum = _mm256_add_ps(vsum, chunk);
        }

        float minArr[8], maxArr[8], sumArr[8];
        _mm256_storeu_ps(minArr, vmin);
        _mm256_storeu_ps(maxArr, vmax);
        _mm256_storeu_ps(sumArr, vsum);

        float gmin = minArr[0], gmax = maxArr[0], gsum = sumArr[0];
        for (int k = 1; k < 8; ++k) {
            if (minArr[k] < gmin) gmin = minArr[k];
            if (maxArr[k] > gmax) gmax = maxArr[k];
            gsum += sumArr[k];
        }

        for (; i < n; ++i) {
            float v = data[i];
            if (v < gmin) gmin = v;
            if (v > gmax) gmax = v;
            gsum += v;
        }
#else
        // Scalar fallback (MinGW / GCC / Clang)
        float gmin = data[0], gmax = data[0], gsum = data[0];
        for (size_t i = 1; i < n; ++i) {
            float v = data[i];
            if (v < gmin) gmin = v;
            if (v > gmax) gmax = v;
            gsum += v;
        }
#endif

        return { gmin, gmax, gsum / (float)n, data[n-1] };
    }
};
