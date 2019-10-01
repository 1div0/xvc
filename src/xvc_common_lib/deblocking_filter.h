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

#ifndef XVC_COMMON_LIB_DEBLOCKING_FILTER_H_
#define XVC_COMMON_LIB_DEBLOCKING_FILTER_H_

#include "xvc_common_lib/coding_unit.h"
#include "xvc_common_lib/picture_data.h"
#include "xvc_common_lib/yuv_pic.h"

namespace xvc {

enum class Direction {
  kVertical,
  kHorizontal,
};

class DeblockingFilter {
public:
  DeblockingFilter(PictureData *pic_data, YuvPicture *rec_pic,
                   int beta_offset, int tc_offset);
  void DeblockPicture();

private:
  // Controls at what level filter decisions are made at
  static const int kSubblockSize = 8;
  static const int kSubblockSizeExt = 4;
  static const int kChromaFilterResolution = 8;
  // Number of samples to filter in parallel
  static const int kFilterGroupSize = 4;

  void DeblockCtu(int rsaddr, CuTree cu_tree, Direction dir,
                  int subblock_size);
  int GetBoundaryStrength(const CodingUnit &cu_p, const CodingUnit &cu_q,
                          int pos_x, int pos_y, Direction dir);
  void FilterEdgeLuma(int x, int y, Direction dir, int subblock_size,
                      int boundary_strength, int qp);
  bool CheckStrongFilter(Sample *src, int beta, int tc, ptrdiff_t offset);
  void FilterLumaWeak(Sample *src_ptr, ptrdiff_t step_size,
                      ptrdiff_t offset, int tc, bool filter_p1,
                      bool filter_q1);
  void FilterLumaStrong(Sample *src_ptr, ptrdiff_t step_size,
                        ptrdiff_t offset, int tc);
  void FilterEdgeChroma(int x, int y, int scale_x, int scale_y, Direction dir,
                        int subblock_size, int boundary_strength, int qp);
  template<int FilterGroupSize>
  void FilterChroma(Sample *src_ptr, ptrdiff_t step_size,
                    ptrdiff_t offset, int tc2);

  const Restrictions &restrictions_;
  PictureData *pic_data_;
  YuvPicture *rec_pic_;
  int beta_offset_ = 0;
  int tc_offset_ = 0;
};

}   // namespace xvc

#endif  // XVC_COMMON_LIB_DEBLOCKING_FILTER_H_
