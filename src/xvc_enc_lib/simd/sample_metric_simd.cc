/******************************************************************************
* Copyright (C) 2018, Divideon.
*
* This library is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public
* License as published by the Free Software Foundation; either
* version 2.1 of the License, or (at your option) any later version.
*
* This library is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with this library; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*
* This library is also available under a commercial license.
* Please visit https://xvc.io/license/ for more information.
******************************************************************************/

#include "xvc_enc_lib/simd/sample_metric_simd.h"

#ifdef XVC_ARCH_X86
#if __GNUC__  == 4 && __GNUC_MINOR__  <= 8 && not defined(__AVX2__)
#define USE_AVX2 0  // gcc 4.8 requires -mavx2 before defining __m256i
#else
#define USE_AVX2 1
#endif
#endif  // XVC_ARCH_X86

#ifdef XVC_ARCH_X86
#include <immintrin.h>    // AVX2
#endif  // XVC_ARCH_X86

#include <type_traits>

#include "xvc_enc_lib/encoder_simd_functions.h"
#include "xvc_enc_lib/sample_metric.h"

#ifdef _MSC_VER
#define __attribute__(SPEC)
#endif  // _MSC_VER

#ifdef XVC_ARCH_X86
#define CAST_M128i_CONST(VAL) reinterpret_cast<const __m128i*>((VAL))
#define CAST_M256i_CONST(VAL) reinterpret_cast<const __m256i*>((VAL))
#endif  // XVC_ARCH_X86

static const std::array<int16_t, 16> kOnes16bit alignas(32) = { {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
} };

namespace xvc {
namespace simd {

#ifdef XVC_ARCH_X86
#if XVC_HIGH_BITDEPTH
template<typename SampleT1>
__attribute__((target("sse2")))
static int ComputeSad_8x2_sse2(int width, int height,
                               const SampleT1 *src1, ptrdiff_t stride1,
                               const Sample *src2, ptrdiff_t stride2) {
  static_assert(std::is_same<Sample, uint16_t>::value, "assume high bitdepth");
  auto sad_8x1_epi16 = [](const SampleT1 *ptr1, const Sample *ptr2)
    __attribute__((target("sse2"))) {
    __m128i src1_row0 = _mm_loadu_si128(CAST_M128i_CONST(ptr1));
    __m128i src2_row0 = _mm_loadu_si128(CAST_M128i_CONST(ptr2));
    __m128i row0 = _mm_sub_epi16(src1_row0, src2_row0);
    __m128i neg0 = _mm_sub_epi16(_mm_setzero_si128(), row0);
    return _mm_max_epi16(row0, neg0);
  };  // NOLINT
  auto sad_8x2_epi32 = [&](const SampleT1 *ptr1, const Sample *ptr2)
    __attribute__((target("sse2"))) {
    __m128i abs0 = sad_8x1_epi16(ptr1, ptr2);
    __m128i abs1 = sad_8x1_epi16(ptr1 + stride1, ptr2 + stride2);
    __m128i sum01_epi16 = _mm_add_epi16(abs0, abs1);
    __m128i ones_epi16 = _mm_load_si128(CAST_M128i_CONST(&kOnes16bit[0]));
    return _mm_madd_epi16(sum01_epi16, ones_epi16);
  };  // NOLINT
  __m128i sum = _mm_setzero_si128();
  for (int y = 0; y < height; y += 2) {
    for (int x = 0; x < width; x += 8) {
      __m128i sum_epi32 = sad_8x2_epi32(src1 + x, src2 + x);
      sum = _mm_add_epi32(sum, sum_epi32);
    }
    src1 += stride1 * 2;
    src2 += stride2 * 2;
  }
  __m128i sum64_hi = _mm_shuffle_epi32(sum, _MM_SHUFFLE(1, 0, 3, 2));
  __m128i sum32 = _mm_add_epi32(sum, sum64_hi);
  __m128i sum32_hi = _mm_shufflelo_epi16(sum32, _MM_SHUFFLE(1, 0, 3, 2));
  __m128i out = _mm_add_epi32(sum32, sum32_hi);
  return _mm_cvtsi128_si32(out);
}

template<typename SampleT1, typename Sample>
__attribute__((target("sse2")))
static uint64_t ComputeSsd_8x2_sse2(int width, int height,
                                    const SampleT1 *src1, ptrdiff_t stride1,
                                    const Sample *src2, ptrdiff_t stride2) {
  static_assert((std::is_same<Sample, uint16_t>::value) ||
    (std::is_same<Sample, int16_t>::value), "assume high bitdepth");
  __m128i sum = _mm_setzero_si128();
  __m128i zero_vec = _mm_setzero_si128();
  uint64_t result = 0;
  for (int y = 0; y < height; y += 2) {
    for (int x = 0; x < width; x += 8) {
      __m128i src1_row0 = _mm_loadu_si128(CAST_M128i_CONST(&src1[x]));
      __m128i src1_row1 = _mm_loadu_si128(CAST_M128i_CONST(&src1[x + stride1]));
      __m128i src2_row0 = _mm_loadu_si128(CAST_M128i_CONST(&src2[x]));
      __m128i src2_row1 = _mm_loadu_si128(CAST_M128i_CONST(&src2[x + stride2]));
      __m128i diff_row0 = _mm_sub_epi16(src1_row0, src2_row0);
      __m128i diff_row1 = _mm_sub_epi16(src1_row1, src2_row1);
      __m128i ssd0 = _mm_madd_epi16(diff_row0, diff_row0);
      __m128i ssd1 = _mm_madd_epi16(diff_row1, diff_row1);
      __m128i ssd_01 = _mm_add_epi32(ssd0, ssd1);
      __m128i ssd_l1 = _mm_unpacklo_epi32(ssd_01, zero_vec);
      __m128i ssd_h1 = _mm_unpackhi_epi32(ssd_01, zero_vec);
      sum = _mm_add_epi64(sum, ssd_l1);
      sum = _mm_add_epi64(sum, ssd_h1);
    }
    src1 += stride1 * 2;
    src2 += stride2 * 2;
  }
  __m128i sum64_hi = _mm_shuffle_epi32(sum, _MM_SHUFFLE(1, 0, 3, 2));
  __m128i out = _mm_add_epi64(sum, sum64_hi);
  _mm_storel_epi64(reinterpret_cast<__m128i*>(&result), out);
  return result;
}
#endif
#endif  // XVC_ARCH_X86

#ifdef XVC_ARCH_X86
#if USE_AVX2 && XVC_HIGH_BITDEPTH
template<typename SampleT1>
__attribute__((target("avx2")))
static int ComputeSad_16x2_avx2(int width, int height,
                                const SampleT1 *src1, ptrdiff_t stride1,
                                const Sample *src2, ptrdiff_t stride2) {
  static_assert(std::is_same<Sample, uint16_t>::value, "assume high bitdepth");
  auto sad_8x1_epi16 = [&](const SampleT1 *ptr1, const Sample *ptr2)
    __attribute__((target("avx2"))) {
    __m256i  src1_row0 = _mm256_lddqu_si256(CAST_M256i_CONST(ptr1));
    __m256i  src2_row0 = _mm256_lddqu_si256(CAST_M256i_CONST(ptr2));
    __m256i  row0 = _mm256_sub_epi16(src1_row0, src2_row0);
    return _mm256_abs_epi16(row0);
  };  // NOLINT
  auto sad_8x2_epi32 = [&](const SampleT1 *ptr1, const Sample *ptr2)
    __attribute__((target("avx2"))) {
    __m256i  abs0 = sad_8x1_epi16(ptr1, ptr2);
    __m256i  abs1 = sad_8x1_epi16(ptr1 + stride1, ptr2 + stride2);
    __m256i  sum01_epi16 = _mm256_add_epi16(abs0, abs1);
    __m256i ones_epi16 = _mm256_load_si256(CAST_M256i_CONST(&kOnes16bit[0]));
    return _mm256_madd_epi16(sum01_epi16, ones_epi16);
  };  // NOLINT
  __m256i  sum = _mm256_setzero_si256();
  for (int y = 0; y < height; y += 2) {
    for (int x = 0; x < width; x += 16) {
      __m256i  sum_epi32 = sad_8x2_epi32(src1 + x, src2 + x);
      sum = _mm256_add_epi32(sum, sum_epi32);
    }
    src1 += stride1 * 2;
    src2 += stride2 * 2;
  }
  __m256i vzero = _mm256_setzero_si256();
  sum = _mm256_hadd_epi32(sum, vzero);
  sum = _mm256_hadd_epi32(sum, vzero);
  __m256i sum_hi = _mm256_permute2x128_si256(sum, sum, _MM_SHUFFLE(0, 1, 0, 1));
  return _mm_cvtsi128_si32(_mm256_castsi256_si128(sum)) +
    _mm_cvtsi128_si32(_mm256_castsi256_si128(sum_hi));
}

template<typename SampleT1, typename Sample>
__attribute__((target("avx2")))
static uint64_t ComputeSsd_16x2_avx2(int width, int height,
                                     const SampleT1 *src1, ptrdiff_t stride1,
                                     const Sample *src2, ptrdiff_t stride2) {
  static_assert((std::is_same<Sample, uint16_t>::value) ||
    (std::is_same<Sample, int16_t>::value), "assume high bitdepth");
  __m256i sum = _mm256_setzero_si256();
  __m256i zero_vec = _mm256_setzero_si256();
  uint64_t result = 0;
  for (int y = 0; y < height; y += 2) {
    for (int x = 0; x < width; x += 16) {
      __m256i src1_row0 = _mm256_lddqu_si256(CAST_M256i_CONST(&src1[x]));
      __m256i src1_row1 =
        _mm256_lddqu_si256(CAST_M256i_CONST(&src1[x + stride1]));
      __m256i src2_row0 = _mm256_lddqu_si256(CAST_M256i_CONST(&src2[x]));
      __m256i src2_row1 =
        _mm256_lddqu_si256(CAST_M256i_CONST(&src2[x + stride2]));
      __m256i diff_row0 = _mm256_sub_epi16(src1_row0, src2_row0);
      __m256i diff_row1 = _mm256_sub_epi16(src1_row1, src2_row1);
      __m256i ssd0 = _mm256_madd_epi16(diff_row0, diff_row0);
      __m256i ssd1 = _mm256_madd_epi16(diff_row1, diff_row1);
      __m256i ssd_01 = _mm256_add_epi32(ssd0, ssd1);
      __m256i ssd_l1 = _mm256_unpacklo_epi32(ssd_01, zero_vec);
      __m256i ssd_h1 = _mm256_unpackhi_epi32(ssd_01, zero_vec);
      sum = _mm256_add_epi64(ssd_l1, sum);
      sum = _mm256_add_epi64(ssd_h1, sum);
    }
    src1 += stride1 * 2;
    src2 += stride2 * 2;
  }
  __m256i sum64_hi = _mm256_shuffle_epi32(sum, _MM_SHUFFLE(1, 0, 3, 2));
  __m256i out_imm = _mm256_add_epi64(sum, sum64_hi);
  __m256i out_imm1 = _mm256_permute4x64_epi64(out_imm, _MM_SHUFFLE(1, 0, 3, 2));
  __m256i out1 = _mm256_add_epi64(out_imm, out_imm1);
  __m128i out = _mm256_extracti128_si256(out1, 0);
  _mm_storel_epi64(reinterpret_cast<__m128i*>(&result), out);
  return result;
}
#endif
#endif  // XVC_ARCH_X86

#ifdef XVC_ARCH_X86
void SampleMetricSimd::Register(const std::set<CpuCapability> &caps,
                                int internal_bitdepth,
                                xvc::EncoderSimdFunctions *simd_functions) {
#if XVC_HIGH_BITDEPTH
  SampleMetric::SimdFunc &sm = simd_functions->sample_metric;
  // TODO(PH) Check for 16-bit samples and bitdepth <= 12
  if (caps.find(CpuCapability::kSse2) != caps.end()) {
    sm.sad_sample_sample[3] = &ComputeSad_8x2_sse2<Sample>;   // 8
    sm.sad_sample_sample[4] = &ComputeSad_8x2_sse2<Sample>;   // 16
    sm.sad_sample_sample[5] = &ComputeSad_8x2_sse2<Sample>;   // 32
    sm.sad_sample_sample[6] = &ComputeSad_8x2_sse2<Sample>;   // 64
    sm.sad_short_sample[3] = &ComputeSad_8x2_sse2<int16_t>;   // 8
    sm.sad_short_sample[4] = &ComputeSad_8x2_sse2<int16_t>;   // 16
    sm.sad_short_sample[5] = &ComputeSad_8x2_sse2<int16_t>;   // 32
    sm.sad_short_sample[6] = &ComputeSad_8x2_sse2<int16_t>;   // 64

    sm.ssd_sample_sample[3] = &ComputeSsd_8x2_sse2<Sample, Sample>;   // 8
    sm.ssd_sample_sample[4] = &ComputeSsd_8x2_sse2<Sample, Sample>;   // 16
    sm.ssd_sample_sample[5] = &ComputeSsd_8x2_sse2<Sample, Sample>;   // 32
    sm.ssd_sample_sample[6] = &ComputeSsd_8x2_sse2<Sample, Sample>;   // 64
    sm.ssd_short_sample[3] = &ComputeSsd_8x2_sse2<int16_t, Sample>;   // 8
    sm.ssd_short_sample[4] = &ComputeSsd_8x2_sse2<int16_t, Sample>;   // 16
    sm.ssd_short_sample[5] = &ComputeSsd_8x2_sse2<int16_t, Sample>;   // 32
    sm.ssd_short_sample[6] = &ComputeSsd_8x2_sse2<int16_t, Sample>;   // 64
    sm.ssd_short_short[3] = &ComputeSsd_8x2_sse2<int16_t, int16_t>;   // 8
    sm.ssd_short_short[4] = &ComputeSsd_8x2_sse2<int16_t, int16_t>;   // 16
    sm.ssd_short_short[5] = &ComputeSsd_8x2_sse2<int16_t, int16_t>;   // 32
    sm.ssd_short_short[6] = &ComputeSsd_8x2_sse2<int16_t, int16_t>;   // 64
  }
#if USE_AVX2
  if (caps.find(CpuCapability::kAvx2) != caps.end()) {
    sm.sad_sample_sample[4] = &ComputeSad_16x2_avx2<Sample>;   // 16
    sm.sad_sample_sample[5] = &ComputeSad_16x2_avx2<Sample>;   // 32
    sm.sad_sample_sample[6] = &ComputeSad_16x2_avx2<Sample>;   // 64
    sm.ssd_sample_sample[4] = &ComputeSsd_16x2_avx2<Sample, Sample>;   // 16
    sm.ssd_sample_sample[5] = &ComputeSsd_16x2_avx2<Sample, Sample>;   // 32
    sm.ssd_sample_sample[6] = &ComputeSsd_16x2_avx2<Sample, Sample>;   // 64
    sm.ssd_short_short[4] = &ComputeSsd_16x2_avx2<int16_t, int16_t>;   // 16
    sm.ssd_short_short[5] = &ComputeSsd_16x2_avx2<int16_t, int16_t>;   // 32
    sm.ssd_short_short[6] = &ComputeSsd_16x2_avx2<int16_t, int16_t>;   // 64
  }
#endif
#endif
}
#endif  // XVC_ARCH_X86

#ifdef XVC_ARCH_ARM
void SampleMetricSimd::Register(const std::set<CpuCapability> &caps,
                                int internal_bitdepth,
                                xvc::EncoderSimdFunctions *simd_functions) {
#ifdef XVC_HAVE_NEON
#endif  // XVC_HAVE_NEON
}
#endif  // XVC_ARCH_ARM

#ifdef XVC_ARCH_MIPS
void SampleMetricSimd::Register(const std::set<CpuCapability> &caps,
                                int internal_bitdepth,
                                xvc::EncoderSimdFunctions *simd_functions) {
}
#endif  // XVC_ARCH_MIPS

}   // namespace simd
}   // namespace xvc
