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

#include "xvc_test/yuv_helper.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>

#include "xvc_common_lib/utils.h"

namespace xvc_test {

std::array<uint16_t, xvc_test::TestYuvPic::kInternalBufferSize>
xvc_test::TestYuvPic::kTestSamples = { {
  687,686,687,688,688,687,686,686,685,682,680,680,680,680,560,508,515,513,589,693,682,653,643,621,628,674,679,679,680,681,680,680,679,680,681,683,683,684,683,705,  // NOLINT
  687,687,686,686,686,685,683,681,680,679,679,680,682,667,388,320,386,426,509,676,613,354,366,426,469,624,677,671,670,670,670,669,669,669,669,670,671,671,670,687,  // NOLINT
  692,689,687,685,684,686,685,683,682,680,678,679,679,684,411,332,381,419,489,666,582,317,335,419,448,579,676,668,669,668,667,667,667,668,668,669,669,668,669,678,  // NOLINT
  681,681,679,680,679,679,679,678,676,675,674,672,669,713,456,360,416,447,499,659,628,330,344,417,451,581,671,664,665,664,665,666,665,667,667,668,669,668,668,667,  // NOLINT
  669,668,667,666,666,665,665,665,663,663,662,660,656,706,445,342,407,461,502,646,648,342,348,422,451,579,670,664,665,665,667,670,671,670,669,669,668,667,668,659,  // NOLINT
  683,682,680,678,677,677,675,674,673,671,670,668,669,680,410,349,393,430,497,660,631,336,350,422,447,568,666,658,657,657,658,659,663,670,675,674,673,672,675,662,  // NOLINT
  686,685,685,684,682,682,682,682,681,679,678,676,686,655,373,365,404,435,475,634,578,379,397,441,470,558,667,663,662,662,663,661,660,662,664,666,666,665,668,654,  // NOLINT
  680,680,679,678,677,677,676,678,676,675,674,674,686,648,357,357,396,418,449,619,587,371,439,529,524,538,658,667,665,666,666,665,664,665,664,664,664,664,667,653,  // NOLINT
  679,678,678,676,675,675,674,674,672,671,670,669,679,626,357,345,373,387,444,606,526,312,392,530,536,541,653,662,662,663,662,660,660,660,662,664,664,663,666,651,  // NOLINT
  684,683,682,681,680,678,678,676,674,674,673,671,683,543,354,348,351,367,424,562,471,317,388,535,544,543,651,663,660,660,659,660,661,657,657,659,659,657,662,648,  // NOLINT
  692,689,689,690,689,688,689,683,680,679,679,679,675,464,354,362,372,384,428,501,421,316,390,539,553,522,622,673,668,662,664,666,662,661,661,661,660,659,658,617,  // NOLINT
  637,694,694,674,674,670,670,684,694,689,688,688,669,439,344,342,371,381,425,475,400,314,383,536,539,492,583,592,572,642,647,635,658,674,676,673,668,667,655,565,  // NOLINT
  607,710,634,497,512,500,503,573,626,685,657,693,666,426,337,336,359,354,405,446,424,550,600,591,478,475,477,561,579,565,582,592,592,590,602,630,667,673,655,539,  // NOLINT
  605,691,604,436,407,381,409,407,415,504,574,676,652,418,343,334,385,423,435,469,515,680,693,611,528,495,449,572,600,530,537,559,597,585,573,589,635,661,657,535,  // NOLINT
  601,656,591,450,470,445,322,374,419,478,558,626,649,409,382,453,550,590,540,461,614,671,631,641,626,573,572,583,601,490,488,566,597,581,578,598,572,591,605,549,  // NOLINT
  629,639,646,660,670,617,552,563,573,600,609,617,650,583,548,554,580,532,502,427,632,666,561,642,690,588,635,589,602,461,475,578,581,547,582,604,677,642,602,549,  // NOLINT
  552,555,581,586,582,586,582,539,549,541,540,552,518,543,555,548,530,563,524,426,630,649,559,646,704,583,619,579,606,473,487,580,590,546,525,542,663,667,596,546,  // NOLINT
  603,648,655,660,649,629,466,260,355,425,412,402,329,369,499,496,431,413,423,429,594,611,569,638,704,595,625,594,606,459,465,558,564,484,508,540,559,591,607,533,  // NOLINT
  641,669,656,658,664,663,562,527,521,410,425,527,534,533,541,494,427,440,438,509,555,558,573,567,626,607,625,609,626,584,571,587,570,565,577,584,545,538,541,373,  // NOLINT
  641,662,648,652,659,668,494,406,431,406,368,423,412,432,494,509,429,520,597,601,629,627,614,609,611,614,594,571,607,656,657,647,660,660,632,635,560,517,520,309,  // NOLINT
  648,669,654,658,666,673,495,403,427,432,388,388,362,355,495,503,531,620,651,567,550,550,589,596,613,619,597,596,584,589,647,666,662,656,640,639,627,605,544,342,  // NOLINT
  644,648,653,665,671,677,564,495,505,388,441,485,478,484,503,482,579,654,584,521,541,454,494,550,555,620,613,633,645,588,648,676,674,677,658,552,551,553,588,459,  // NOLINT
  628,589,593,654,663,671,407,214,309,399,418,237,231,238,441,518,563,548,459,513,664,668,671,638,621,641,650,661,658,649,661,663,664,684,561,427,486,564,631,457,  // NOLINT
  648,663,655,668,674,674,547,488,506,399,458,507,507,516,547,553,581,521,449,478,562,627,604,539,536,605,655,659,661,661,659,660,667,585,457,411,566,553,552,456,  // NOLINT
  658,674,680,679,681,686,526,439,445,453,402,418,422,414,463,435,409,415,514,469,422,604,582,548,590,600,644,654,654,652,658,640,548,385,365,443,541,535,550,474,  // NOLINT
  665,669,673,677,665,685,487,348,399,416,380,382,390,398,494,576,400,416,605,401,245,442,601,473,433,596,645,619,614,635,613,529,447,418,342,329,350,344,360,448,  // NOLINT
  646,650,655,666,658,664,591,544,543,419,446,554,546,546,593,546,361,395,533,347,270,326,339,481,488,516,433,419,490,515,417,500,475,445,344,378,407,402,412,448,  // NOLINT
  672,687,686,688,691,698,488,365,403,474,396,358,351,361,529,530,344,379,484,334,322,521,539,716,705,618,514,550,584,480,420,508,541,511,316,602,703,633,545,488,  // NOLINT
  675,690,690,692,693,703,528,435,473,453,445,473,483,497,586,515,343,382,489,351,341,518,453,622,681,622,574,454,424,434,500,506,495,492,368,549,634,566,531,497,  // NOLINT
  676,666,659,687,685,690,573,506,510,408,420,476,470,481,558,509,339,341,403,344,419,422,278,349,528,466,449,406,363,336,401,428,429,535,483,518,522,493,443,487,  // NOLINT
  670,678,675,682,686,699,459,329,382,426,380,336,356,377,485,418,315,357,365,320,444,456,330,351,451,535,533,526,550,547,547,549,543,645,500,428,460,450,415,484,  // NOLINT
  685,697,698,698,701,704,594,544,548,446,470,553,556,564,525,360,459,523,552,512,541,573,552,556,518,592,620,535,531,593,583,552,645,727,498,377,416,413,462,493,  // NOLINT
  689,702,700,702,703,711,531,415,432,481,402,376,369,356,429,540,643,643,633,553,614,631,556,438,538,555,563,504,462,472,526,545,581,620,575,517,550,532,565,508,  // NOLINT
  681,696,691,689,694,702,509,404,440,488,421,455,465,486,535,588,583,609,500,330,474,541,567,482,562,605,604,491,463,490,533,616,599,602,541,475,483,520,529,480,  // NOLINT
  688,707,705,706,708,708,606,554,543,488,471,531,515,514,589,587,562,590,519,483,596,576,523,517,520,557,597,520,506,492,507,584,565,542,556,510,440,494,613,517,  // NOLINT
  703,713,711,713,714,722,485,344,387,432,379,362,355,378,520,570,485,490,532,516,560,563,499,495,463,466,563,587,552,524,512,581,541,526,475,421,368,363,475,497,  // NOLINT
  702,717,715,713,714,717,562,485,517,409,450,536,539,560,559,472,415,517,544,513,463,563,539,530,541,487,596,684,638,565,592,688,575,417,415,468,478,400,392,484,  // NOLINT
  695,700,698,700,708,704,558,477,478,414,409,436,421,427,507,522,505,588,590,598,573,568,539,509,481,426,446,507,578,593,572,511,479,510,524,516,471,404,448,521,  // NOLINT
  719,721,719,721,723,733,450,316,402,461,396,398,414,438,543,571,586,573,589,607,658,615,408,326,320,406,374,330,488,358,394,480,470,507,498,493,515,510,532,505,  // NOLINT
  717,727,723,725,726,732,602,549,557,500,476,556,549,553,525,458,539,535,548,586,662,653,530,375,403,456,578,597,552,480,461,509,523,500,441,487,513,507,531,500,  // NOLINT
  532,533,534,534,535,536,535,533,529,530,534,531,532,537,538,538,538,538,538,540,530,531,532,532,534,535,529,527,527,527,526,532,526,538,538,538,539,538,538,541,  // NOLINT
  528,529,530,530,531,533,526,527,527,526,524,533,525,535,535,535,535,535,534,539,525,525,525,526,526,528,518,529,530,519,521,535,528,530,532,532,532,532,532,537,  // NOLINT
  525,525,526,526,527,528,521,532,534,522,528,535,529,529,531,531,531,531,531,535,520,521,521,521,521,523,523,537,535,526,536,535,531,530,528,523,525,526,527,533,  // NOLINT
  516,522,533,532,524,518,524,539,531,527,533,536,542,541,537,532,524,521,522,531,523,522,530,537,533,526,528,537,525,527,528,536,538,538,539,540,530,523,510,527,  // NOLINT
  526,524,523,530,532,533,528,532,535,528,521,531,534,536,540,540,531,528,511,530,520,516,517,531,534,531,518,531,533,529,524,527,532,535,532,530,531,531,533,535,  // NOLINT
  519,516,517,531,535,534,528,531,528,529,535,536,536,533,533,530,529,530,529,535,519,516,516,531,535,536,536,533,532,506,528,535,536,533,532,532,533,540,531,537,  // NOLINT
  518,515,515,530,533,535,535,533,533,529,529,534,533,534,535,536,539,538,531,533,518,515,515,530,534,535,531,533,535,531,527,530,535,524,523,529,534,537,541,548,  // NOLINT
  517,515,515,530,534,535,532,534,520,512,533,531,536,529,527,527,527,538,544,546,517,513,515,530,534,536,533,524,526,532,535,533,538,539,528,532,534,537,530,528,  // NOLINT
  516,514,514,529,534,537,533,527,532,535,534,537,532,531,529,528,536,537,529,531,515,513,514,529,533,534,531,532,536,532,536,533,534,532,533,528,532,522,508,529,  // NOLINT
  516,513,513,529,532,533,532,532,532,531,535,533,536,533,534,531,518,529,531,533,515,512,513,526,534,536,534,528,524,529,534,531,528,530,527,524,526,534,529,530,  // NOLINT
  516,516,515,514,513,512,515,528,531,520,520,525,519,509,509,509,509,509,508,508,520,518,517,516,515,514,522,539,535,525,539,535,527,510,511,510,510,510,510,509,  // NOLINT
  523,522,521,521,520,519,525,540,535,527,543,535,528,514,515,515,514,514,514,512,526,526,526,525,525,523,536,539,534,535,537,521,520,520,520,520,519,519,518,516,  // NOLINT
  526,526,525,525,524,521,534,536,532,536,531,514,515,521,521,520,520,520,519,519,529,529,528,528,528,526,534,531,533,537,525,512,516,520,521,528,525,523,523,519,  // NOLINT
  528,531,521,518,524,532,534,532,534,534,519,510,511,508,510,517,520,527,528,521,524,528,521,514,517,520,521,520,522,520,514,504,506,505,507,505,512,523,538,522,  // NOLINT
  514,515,516,514,512,512,517,514,517,517,514,505,505,506,507,506,513,515,535,519,515,518,518,510,512,514,525,516,518,513,513,510,510,510,510,510,510,509,510,532,  // NOLINT
  516,518,517,509,512,511,518,515,514,512,510,512,510,511,508,510,510,510,514,521,516,517,517,510,511,506,507,511,512,530,511,508,508,508,508,507,508,510,514,505,  // NOLINT
  516,517,517,509,509,509,507,511,517,517,512,507,507,506,507,507,515,523,523,513,515,517,517,509,509,508,508,510,516,512,513,507,506,517,530,526,517,518,506,497,  // NOLINT
  515,517,517,508,511,508,508,510,521,522,508,507,504,524,529,528,520,507,500,501,515,517,516,508,511,509,506,514,520,523,514,512,504,506,516,512,504,510,516,514,  // NOLINT
  514,516,516,508,507,505,504,509,512,513,516,516,511,509,514,511,503,511,516,510,514,516,516,509,510,506,507,511,511,513,509,512,511,510,509,510,511,521,518,510,  // NOLINT
  515,516,516,508,513,507,505,513,513,511,507,510,510,509,508,509,509,524,520,513,515,517,516,509,508,504,505,512,513,509,504,512,512,511,509,510,510,524,517,512,  // NOLINT
} };

constexpr float GetRndFracX(int i) {
  return (i % 3) / 3.0f;
}
constexpr float GetRndFracY(int i) {
  return (i % 7) / 7.0f;
}

TestYuvPic::TestYuvPic(int width, int height, int bitdepth, int dx, int dy,
                       xvc::ChromaFormat chroma_fmt)
  : width_(width),
  height_(height),
  bitdepth_(bitdepth),
  chroma_fmt_(chroma_fmt) {
  assert(dx + width <= kInternalPicSize);
  assert(dy + height <= kInternalPicSize);
  const int down_shift = std::max(0, kInternalBitdepth - bitdepth);
  const int up_shift = std::max(0, bitdepth - kInternalBitdepth);
  const Sample max_val = (1 << bitdepth) - 1;
  const int total_samples =
    xvc::util::GetTotalNumSamples(width, height, chroma_fmt);
  samples_.resize(total_samples);
  const float fx = GetRndFracX(dx);
  const float fy = GetRndFracY(dy);
  const float total_w = 2 + fx + fy;
  const float w[3] = { 2 / total_w, fx / total_w, fy / total_w };

  const ptrdiff_t strideY = kInternalPicSize;
  const uint16_t *srcY = &kTestSamples[dy * strideY + dx];
  int i = 0;
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      int tmp = static_cast<int>(srcY[x] * w[0] + srcY[x + 1] * w[1] +
                                 srcY[strideY] * w[2] + 0.5);
      samples_[i++] =
        xvc::util::ClipBD((tmp >> down_shift) << up_shift, max_val);
    }
    srcY += strideY;
  }
  stride_[0] = width;

  if (chroma_fmt == xvc::ChromaFormat::k420) {
    const ptrdiff_t strideUV = kInternalPicSize >> 1;
    const uint16_t *srcUV = &kTestSamples[kInternalPicSize * kInternalPicSize +
      (dy >> 1) * strideUV + (dx >> 1)];
    for (int c = 1; c < 3; c++) {
      for (int y = 0; y < height >> 1; y++) {
        for (int x = 0; x < width >> 1; x++) {
          int16_t tmp = static_cast<int16_t>(
            srcUV[x] * w[0] + srcUV[x + 1] * w[1] + srcUV[strideUV] * w[2]);
          samples_[i++] = static_cast<Sample>((tmp >> down_shift) << up_shift);
        }
        srcUV += strideUV;
      }
    }
    stride_[1] = width >> 1;
    stride_[2] = width >> 1;
  } else {
    assert(0);
  }
  if (bitdepth == 8) {
    bytes_.resize(total_samples);
    for (int j = 0; j < static_cast<int>(samples_.size()); j++) {
      bytes_[j] = static_cast<uint8_t>(samples_[j]);
    }
  } else {
    bytes_.resize(total_samples * 2);
    for (int j = 0; j < static_cast<int>(samples_.size()); j++) {
      bytes_[j * 2 + 0] = static_cast<uint8_t>(samples_[j] & 0xff);
#if XVC_HIGH_BITDEPTH
      bytes_[j * 2 + 1] = static_cast<uint8_t>(samples_[j] >> 8);
#endif
    }
  }
}

const xvc::Sample* TestYuvPic::GetSamplePtr(xvc::YuvComponent comp) const {
  assert(chroma_fmt_ == xvc::ChromaFormat::k420);
  const Sample *sample_ptr = &samples_[0];
  if (comp == xvc::YuvComponent::kU) {
    sample_ptr += height_ * stride_[0];
  } else if (comp == xvc::YuvComponent::kV) {
    sample_ptr += height_ * stride_[0] + (height_ >> 1) * stride_[1];
  }
  return sample_ptr;
}

xvc::Sample TestYuvPic::GetAverageSample() {
  int sum = std::accumulate(samples_.begin(), samples_.end(), 0);
  return static_cast<xvc::Sample>(sum / samples_.size());
}

double TestYuvPic::CalcPsnr(const char *pic_bytes) const {
  int ssd = 0;
  if (bitdepth_ == 8) {
    auto pic8 = reinterpret_cast<const uint8_t*>(pic_bytes);
    for (int i = 0; i < width_*height_; i++) {
      int tmp = samples_[i] - pic8[i];
      ssd += tmp*tmp;
    }
  } else {
    auto pic16 = reinterpret_cast<const uint16_t*>(pic_bytes);
    for (int i = 0; i < width_*height_; i++) {
      int tmp = samples_[i] - pic16[i];
      ssd += tmp*tmp;
    }
  }
  const int64_t max_val = (1 << bitdepth_) - 1;
  const int64_t max_sum = max_val * max_val * width_ * height_;
  return ssd > 0 ? 10.0 * std::log10(max_sum / ssd) : 1000;
}

double TestYuvPic::CalcPsnr(xvc::YuvComponent comp,
                            const xvc::YuvPicture &ref_pic) {
  const int ref_width = ref_pic.GetWidth(comp);
  const int ref_height = ref_pic.GetHeight(comp);
  const int orig_width =
    xvc::util::ScaleSizeX(width_, ref_pic.GetChromaFormat(), comp);
  const int orig_height =
    xvc::util::ScaleSizeX(height_, ref_pic.GetChromaFormat(), comp);
  EXPECT_LE(orig_width, ref_width);
  EXPECT_LE(orig_height, ref_height);
  const int max_bitdepth = std::max(bitdepth_, ref_pic.GetBitdepth());
  const int orig_upshift = max_bitdepth - bitdepth_;
  const int ref_upshift = max_bitdepth - ref_pic.GetBitdepth();
  const xvc::SampleBufferConst ref_buffer = ref_pic.GetSampleBuffer(comp, 0, 0);
  const Sample *ref_ptr = ref_buffer.GetDataPtr();
  const Sample *orig_ptr = GetSamplePtr(comp);
  const ptrdiff_t orig_stride = stride_[static_cast<int>(comp)];
  // TODO(PH) Hack to scale orig image if smaller than reference
  const double orig_scale_x = 1.0 * orig_width / ref_width;
  const double orig_scale_y = 1.0 * orig_height / ref_height;
  // If orig is smaller size than ref picture clip to closest sample
  int ssd = 0;
  for (int y = 0; y < ref_height; y++) {
    for (int x = 0; x < ref_width; x++) {
      auto orig_x = std::min(::lround(orig_scale_x * x), orig_width - 1l);
      auto orig_y = std::min(::lround(orig_scale_y * y), orig_height - 1l);
      Sample orig = orig_ptr[orig_y * orig_stride + orig_x] << orig_upshift;
      Sample ref = ref_ptr[x] << ref_upshift;
      int err = orig - ref;
      ssd += err*err;
    }
    ref_ptr += ref_buffer.GetStride();
  }
  const int64_t max_val = (1 << max_bitdepth) - 1;
  const int64_t max_sum = max_val * max_val * width_ * height_;
  return ssd > 0 ? 10.0 * std::log10(max_sum / ssd) : 1000;
}

void TestYuvPic::FillLargerBuffer(int out_width, int out_height,
                                  xvc::SampleBuffer *out) {
  assert(width_ <= out_width);
  assert(height_ <= out_height);
  xvc::SampleBuffer src_buffer(&samples_[0], width_);
  // Upper left corner uses original data directly
  out->CopyFrom(width_, height_, src_buffer);
  // Pad right side
  xvc::Sample *src_ptr = src_buffer.GetDataPtr();
  xvc::Sample *out_ptr = out->GetDataPtr();
  for (int y = 0; y < height_; y++) {
    for (int x = width_; x < out_width; x += width_) {
      std::memcpy(out_ptr + x,
                  src_ptr,
                  std::min(width_, out_width - x) * sizeof(Sample));
      src_ptr += src_buffer.GetStride();
      out_ptr += out->GetStride();
    }
  }
  // Pad bottom from already padded data
  out_ptr = out->GetDataPtr();
  for (int y = height_; y < out_height; y += height_) {
    int remaining_y = std::min(height_, out_height - height_);
    for (int y2 = 0; y2 < remaining_y; y2++) {
      std::memcpy(out_ptr + (y + y2) * out->GetStride(),
                  out_ptr + y2 * out->GetStride(),
                  out_width * sizeof(Sample));
    }
  }
}

::testing::AssertionResult TestYuvPic::SamePictureBytes(
  const uint8_t *pic1, size_t size1, const uint8_t *pic2, size_t size2) {
  EXPECT_EQ(size1, size2);
  for (size_t i = 0; i < size1; i++) {
    if (pic1[i] != pic2[i]) {
      return ::testing::AssertionFailure() <<
        (unsigned)pic1[i] << " vs " << (unsigned)pic2[i] << " at i=" << i;
    }
  }
  return ::testing::AssertionSuccess();
}

::testing::AssertionResult
TestYuvPic::SameSamples(int src_width, int src_height,
                        const xvc_test::TestYuvPic &orig_pic, int orig_upshift,
                        const xvc::YuvPicture &ref_pic, int ref_upshift) {
  xvc::ChromaFormat chroma_fmt = ref_pic.GetChromaFormat();
  for (int c = 0; c < xvc::constants::kMaxYuvComponents; c++) {
    const xvc::YuvComponent comp = static_cast<xvc::YuvComponent>(c);
    const int width = xvc::util::ScaleSizeX(src_width, chroma_fmt, comp);
    const int height = xvc::util::ScaleSizeY(src_height, chroma_fmt, comp);
    const int comp_crop_x = ref_pic.GetWidth(comp) - width;
    const int comp_crop_y = ref_pic.GetWidth(comp) - height;
    xvc::SampleBufferConst ref_buffer =
      ref_pic.GetSampleBuffer(comp, 0, 0);
    const xvc::Sample *orig_ptr = orig_pic.GetSamplePtr(comp);
    const xvc::Sample *ref_ptr = ref_buffer.GetDataPtr();
    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
        if ((orig_ptr[x] << orig_upshift) != (ref_ptr[x] << ref_upshift)) {
          return ::testing::AssertionFailure()
            << (orig_ptr[x] << orig_upshift)
            << " vs " << (ref_ptr[x] << ref_upshift)
            << " comp=" << comp << " y=" << y << " x=" << x;
        }
      }
      // Verify non-zero right padding
      for (int x2 = 0; x2 < comp_crop_x; x2++) {
        if (!ref_ptr[width + x2]) {
          return ::testing::AssertionFailure()
            << "zero padding comp=" << comp << " y=" << y << " x2=" << x2;
        }
      }
      orig_ptr += orig_pic.GetStride(comp);
      ref_ptr += ref_buffer.GetStride();
    }
    // Verfy non-zero bottom padding
    for (int y2 = 0; y2 < comp_crop_y; y2++) {
      for (int x2 = 0; x2 < width + comp_crop_x; x2++) {
        if (!ref_ptr[x2]) {
          return ::testing::AssertionFailure()
            << "zero padding comp=" << comp << " y2=" << y2 << " x2=" << x2;
        }
      }
      ref_ptr += ref_buffer.GetStride();
    }
  }
  return ::testing::AssertionSuccess();
}

::testing::AssertionResult
TestYuvPic::AllSampleEqualTo(int width, int height, int bitdepth,
                             const char * pic_bytes, size_t size,
                             xvc::Sample expected_sample) {
  size_t sample_size = bitdepth == 8 ? sizeof(uint8_t) : sizeof(uint16_t);
  EXPECT_EQ(sample_size * width * height * 3 / 2, size);
  size_t luma_samples = width * height;
  if (sample_size == sizeof(uint8_t)) {
    const uint8_t *bytes = reinterpret_cast<const uint8_t*>(pic_bytes);
    uint8_t expected = static_cast<uint8_t>(expected_sample);
    auto it =
      std::find_if(bytes, bytes + luma_samples,
                   [expected](const uint8_t &b) { return b != expected; });
    if (it == bytes + luma_samples) {
      return ::testing::AssertionSuccess();
    }
    return ::testing::AssertionFailure() << "found: " <<
      static_cast<int>(*it) << " expected: " << static_cast<int>(expected);
  } else {
    const uint16_t *bytes = reinterpret_cast<const uint16_t*>(pic_bytes);
    uint16_t expected = static_cast<uint16_t>(expected_sample);
    auto it =
      std::find_if(bytes, bytes + luma_samples,
                   [expected](const uint16_t &b) { return b != expected; });
    if (it == bytes + luma_samples) {
      return ::testing::AssertionSuccess();
    }
    return ::testing::AssertionFailure() << "found: " <<
      static_cast<int>(*it) << " expected: " << static_cast<int>(expected);
  }
}

}   // namespace xvc_test
