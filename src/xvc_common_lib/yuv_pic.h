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

#ifndef XVC_COMMON_LIB_YUV_PIC_H_
#define XVC_COMMON_LIB_YUV_PIC_H_

#include <vector>

#include "xvc_common_lib/common.h"
#include "xvc_common_lib/sample_buffer.h"

namespace xvc {

struct PictureFormat {
  PictureFormat() = default;
  PictureFormat(int _width, int _height, int _bitdepth,
               ChromaFormat _chroma_format, ColorMatrix _color_matrix,
               bool _dither)
    : width(_width),
    height(_height),
    bitdepth(_bitdepth),
    chroma_format(_chroma_format),
    color_matrix(_color_matrix),
    dither(_dither) {
  }
  int width = 0;
  int height = 0;
  int bitdepth = 0;
  ChromaFormat chroma_format = ChromaFormat::kUndefined;
  ColorMatrix color_matrix = ColorMatrix::kUndefined;
  bool dither = false;
};

class YuvPicture {
public:
  YuvPicture(const PictureFormat &pic_fmt, bool padding,
             int crop_width, int crop_height)
    : YuvPicture(pic_fmt.chroma_format, pic_fmt.width, pic_fmt.height,
                 pic_fmt.bitdepth, padding, crop_width, crop_height) {
  }
  YuvPicture(ChromaFormat chroma_format, int width, int height, int bitdepth,
             bool padding, int crop_width, int crop_height);

  int GetWidth(YuvComponent comp) const { return width_[comp]; }
  int GetHeight(YuvComponent comp) const { return height_[comp]; }
  ptrdiff_t GetStride(YuvComponent comp) const { return stride_[comp]; }
  int GetTotalHeight(YuvComponent comp) const { return total_height_[comp]; }
  int GetSizeShiftX(YuvComponent comp) const { return shiftx_[comp]; }
  int GetSizeShiftY(YuvComponent comp) const { return shifty_[comp]; }
  int GetBitdepth() const { return bitdepth_; }
  size_t GetTotalSamples() const { return sample_buffer_.size(); }
  ChromaFormat GetChromaFormat() const { return chroma_format_; }
  int GetCropWidth() const { return crop_width_; }
  int GetCropHeight() const { return crop_height_; }
  int GetDisplayWidth(YuvComponent comp) const;
  int GetDisplayHeight(YuvComponent comp) const;

  Sample* GetSamplePtr(YuvComponent comp, int x, int y) {
    return comp_pel_[comp] + y * GetStride(comp) + x;
  }
  const Sample* GetSamplePtr(YuvComponent comp, int x, int y) const {
    return comp_pel_[comp] + y * GetStride(comp) + x;
  }
  SampleBuffer GetSampleBuffer(YuvComponent comp, int x, int y) {
    return SampleBuffer(GetSamplePtr(comp, x, y), GetStride(comp));
  }
  SampleBufferConst GetSampleBuffer(YuvComponent comp, int x, int y) const {
    return SampleBufferConst(GetSamplePtr(comp, x, y), GetStride(comp));
  }
  void CopyToSameBitdepth(std::vector<uint8_t> *pic_bytes) const;
  void PadBorder();

private:
  ChromaFormat chroma_format_;
  int width_[constants::kMaxYuvComponents];
  int height_[constants::kMaxYuvComponents];
  ptrdiff_t stride_[constants::kMaxYuvComponents];
  int total_height_[constants::kMaxYuvComponents];
  int shiftx_[constants::kMaxYuvComponents];
  int shifty_[constants::kMaxYuvComponents];
  int bitdepth_;
  int crop_width_;
  int crop_height_;
  std::vector<Sample> sample_buffer_;
  Sample *comp_pel_[constants::kMaxYuvComponents];
};

}   // namespace xvc

#endif  // XVC_COMMON_LIB_YUV_PIC_H_
