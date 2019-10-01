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

#ifndef XVC_COMMON_LIB_INTRA_PREDICTION_H_
#define XVC_COMMON_LIB_INTRA_PREDICTION_H_

#include <array>

#include "xvc_common_lib/coding_unit.h"
#include "xvc_common_lib/common.h"

namespace xvc {

struct IntraPredictorLuma : std::array<IntraMode, constants::kNumIntraMpmExt> {
  int num_neighbor_modes = 0;
};
using IntraPredictorChroma = std::array<IntraChromaMode,
  kMaxNumIntraChromaModes>;

class IntraPrediction {
public:
  static const ptrdiff_t kRefSampleStride_ = constants::kMaxBlockSize * 2 + 1;
  struct RefState {
    std::array<Sample, kRefSampleStride_ * 2> ref_samples;
    std::array<Sample, kRefSampleStride_ * 2> ref_filtered;
  };

  explicit IntraPrediction(int bitdepth);
  void Predict(IntraMode intra_mode, const CodingUnit &cu, YuvComponent comp,
               const RefState &ref_state, const YuvPicture &rec_pic,
               SampleBuffer *output_buffer);
  void FillReferenceState(const CodingUnit &cu, YuvComponent comp,
                          const YuvPicture &rec_pic, RefState *ref_state);
  IntraPredictorLuma GetPredictorLuma(const CodingUnit &cu) const;
  IntraPredictorChroma GetPredictorsChroma(IntraMode luma_mode) const;
  static IntraMode Convert(IntraAngle angle);
  static IntraChromaMode Convert(IntraChromaAngle chroma_angle);

private:
  static const int kDownscaleLumaPadding = sizeof(int) / (1 * sizeof(Sample));
  struct NeighborState;
  struct LmParams;
  void FillPredictorLumaDefault(const CodingUnit &cu,
                                IntraPredictorLuma *predictors) const;
  bool UseFilteredRefSamples(const CodingUnit &cu, IntraMode intra_mode);
  void PredIntraDC(int width, int height, bool dc_filter,
                   const Sample *ref_samples, ptrdiff_t ref_stride,
                   Sample *output_buffer, ptrdiff_t output_stride);
  void PlanarPred(int width, int height,
                  const Sample *ref_samples, ptrdiff_t ref_stride,
                  Sample *output_buffer, ptrdiff_t output_stride);
  void AngularPred(int width, int height, IntraMode mode, bool filter,
                   const Sample *ref_samples, ptrdiff_t ref_stride,
                   Sample *output_buffer, ptrdiff_t output_stride);
  void PredLmChroma(const CodingUnit &cu, YuvComponent comp,
                    const YuvPicture &rec_pic, SampleBuffer *output_buffer);
  LmParams DeriveLmParams(const CodingUnit &cu, YuvComponent comp,
                          const SampleBufferConst &comp_src,
                          const SampleBufferConst &luma_src);
  NeighborState DetermineNeighbors(const CodingUnit &cu, YuvComponent comp);
  void ComputeRefSamples(int width, int height,
                         const NeighborState &neighbors,
                         const Sample *input, ptrdiff_t input_stride,
                         Sample *output, ptrdiff_t output_stride);
  void FilterRefSamples(int width, int height, const Sample *src_ref,
                        Sample *dst_ref, ptrdiff_t stride);
  void RescaleLuma(const CodingUnit &cu, int src_width, int src_height,
                     const SampleBufferConst &src_buffer,
                     int out_width, int out_height, SampleBuffer *out_buffer);

  const Restrictions restrictions_;
  int bitdepth_;
  SampleBufferStorage temp_pred_buffer_;
};

}   // namespace xvc

#endif  // XVC_COMMON_LIB_INTRA_PREDICTION_H_
