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

#ifndef XVC_COMMON_LIB_INTER_PREDICTION_H_
#define XVC_COMMON_LIB_INTER_PREDICTION_H_

#include <array>

#include "xvc_common_lib/common.h"
#include "xvc_common_lib/coding_unit.h"
#include "xvc_common_lib/sample_buffer.h"

namespace xvc {

typedef std::array<MotionVector,
  constants::kNumInterMvPredictors> InterPredictorList;
typedef std::array<MotionVector3,
  constants::kNumInterMvPredictors> AffinePredictorList;

struct MergeCandidate {
  InterDir inter_dir = InterDir::kL0;
  std::array<MotionVector, static_cast<int>(RefPicList::kTotalNumber)> mv;
  std::array<int, static_cast<int>(RefPicList::kTotalNumber)> ref_idx = { {0} };
  bool use_lic = false;
};

struct AffineMergeCandidate {
  InterDir inter_dir = InterDir::kL0;
  std::array<MotionVector3, static_cast<int>(RefPicList::kTotalNumber)> mv;
  std::array<int, static_cast<int>(RefPicList::kTotalNumber)> ref_idx = { {0} };
};

struct InterMergeCandidateList
  : public std::array<MergeCandidate, constants::kNumInterMergeCandidates> {
  int num = 0;
};

class InterPrediction {
public:
  static const int kNumTapsLuma = 8;
  static const int kNumTapsChroma = 4;
  static const int kInternalPrecision = 14;
  static const int kFilterPrecision = 6;
  static const int kInternalOffset = 1 << (kInternalPrecision - 1);
  static const int kMergeLevelShift = 2;
  struct SimdFunc;

  InterPrediction(const SimdFunc &simd, const YuvPicture &rec_pic,
                  int bitdepth);
  InterPredictorList GetMvpList(const CodingUnit &cu, RefPicList ref_list,
                                int ref_idx);
  AffinePredictorList GetMvpListAffine(const CodingUnit &cu,
                                       RefPicList ref_list, int ref_idx,
                                       int max_num_mvp);
  InterMergeCandidateList GetMergeCandidates(const CodingUnit &cu,
                                             int merge_cand_idx = -1);
  AffineMergeCandidate GetAffineMergeCand(const CodingUnit &cu);
  MotionVector3 DeriveMvAffine(const CodingUnit &cu, const YuvPicture &ref_pic,
                               const MotionVector &mv1,
                               const MotionVector &mv2);
  void CalculateMV(CodingUnit *cu);
  void ApplyMergeCand(CodingUnit *cu, const MergeCandidate &merge_cand);
  void ApplyMergeCand(CodingUnit *cu, const AffineMergeCandidate &merge_cand);
  void MotionCompensation(const CodingUnit &cu, YuvComponent comp,
                          SampleBuffer *pred_buffer);
  void MotionCompensationMv(const CodingUnit &cu, YuvComponent comp,
                            const YuvPicture &ref_pic, const MotionVector &mv,
                            bool post_filter,
                            SampleBuffer *pred_buffer);
  void MotionCompensationMv(const CodingUnit &cu, YuvComponent comp,
                            const YuvPicture &ref_pic, const MotionVector3 &mv,
                            bool post_filter,
                            SampleBuffer *pred_buffer);
  void ClipMv(const CodingUnit &cu, const YuvPicture &ref_pic,
              MotionVector *mv) const;
  void ClipMv(const CodingUnit &cu, const YuvPicture &ref_pic,
              MotionVector3 *mv) const;
  void DetermineMinMaxMv(const CodingUnit &cu, const YuvPicture &ref_pic,
                         const MotionVector &center, int search_range,
                         MvFullpel *mv_min, MvFullpel *mv_max) const;
  template<typename SrcT, bool Clip>
  static int GetFilterShift(int bitdepth);
  template<typename SrcT, bool Clip>
  static int GetFilterOffset(int shift);

private:
  static const int kBufSize = constants::kMaxBlockSize *
    (constants::kMaxBlockSize + kNumTapsLuma - 1);
  void ScaleMv(PicNum poc_current1, PicNum poc_ref1, PicNum poc_current2,
               PicNum poc_ref2, MotionVector *out);
  struct LicParams;

  bool GetMvpCand(const CodingUnit *cu, NeighborDir dir,
                  RefPicList ref_list, int ref_idx, PicNum ref_poc,
                  MotionVector *mv_list, int index);
  bool GetScaledMvpCand(const CodingUnit *cu, NeighborDir dir,
                        RefPicList ref_list, int ref_idx,
                        MotionVector *mv_list, int index);
  bool GetTemporalMvPredictor(const CodingUnit &cu, RefPicList ref_list,
                              int ref_idx, MotionVector *mv_out, bool *use_lic);
  template<typename PredBuffer>
  void MotionCompRefList(const CodingUnit &cu, YuvComponent comp,
                         RefPicList ref_list, bool post_filter,
                         PredBuffer *pred_buffer);
  template<typename PredBuffer>
  void MotionCompAffine(const CodingUnit &cu, YuvComponent comp,
                        const YuvPicture &ref_pic,
                        const MotionVector3 &mv,
                        PredBuffer *pred_buffer);
  void MotionCompUniPred(int width, int height, YuvComponent comp,
                         const SampleBufferConst &ref_buffer,
                         int frac_x, int frac_y,
                         SampleBuffer *pred_buffer);
  void MotionCompUniPred(int width, int height, YuvComponent comp,
                         const SampleBufferConst &ref_buffer,
                         int frac_x, int frac_y,
                         DataBuffer<int16_t> *pred_buffer);
  SampleBufferConst
    GetFullpelRef(const CodingUnit &cu, YuvComponent comp,
                  const YuvPicture &ref_pic, int mv_x, int mv_y,
                  int *frac_x, int *frac_y);
  void FilterLuma(int width, int height, int frac_x, int frac_y,
                  const Sample *ref, ptrdiff_t ref_stride,
                  Sample *pred, ptrdiff_t pred_stride);
  void FilterChroma(int width, int height, int frac_x, int frac_y,
                    const Sample *ref, ptrdiff_t ref_stride,
                    Sample *pred, ptrdiff_t pred_stride);
  void FilterCopyBipred(int width, int height,
                        const SampleBufferConst &ref_buffer,
                        DataBuffer<int16_t> *pred_buffer);
  void FilterLumaBipred(int width, int height, int frac_x, int frac_y,
                        const Sample *ref, ptrdiff_t ref_stride,
                        int16_t *pred, ptrdiff_t pred_stride);
  void FilterChromaBipred(int width, int height, int frac_x, int frac_y,
                          const Sample *ref, ptrdiff_t ref_stride,
                          int16_t *pred, ptrdiff_t pred_stride);
  void AddAvgBi(const CodingUnit &cu, YuvComponent comp,
                const DataBuffer<const int16_t> &src_l0_buffer,
                const DataBuffer<const int16_t> &src_l1_buffer,
                SampleBuffer *pred_buffer);
  void LocalIlluminationComp(const CodingUnit &cu, YuvComponent comp,
                             int mv_x, int mv_y, const YuvPicture &ref_pic,
                             SampleBuffer *pred_buffer);
  LicParams DeriveLicParams(const CodingUnit &cu, YuvComponent comp,
                            const MotionVector &mv_full,
                            const YuvPicture &ref_pic,
                            const SampleBufferConst &rec_buffer);
  MergeCandidate GetMergeCandidateFromCu(const CodingUnit &cu, MvCorner corner);

  const InterPrediction::SimdFunc &simd_;
  const Restrictions &restrictions_;
  const YuvPicture &rec_pic_;    // current picture, used for template matching
  std::array<int16_t, kBufSize> filter_buffer_;
  std::array<std::array<int16_t, constants::kMaxBlockSamples>, 2> bipred_temp_;
  int bitdepth_;
};

struct InterPrediction::SimdFunc {
  // 0: width <= 2, 1: width >= 4
  static const int kSize = 2;

  // 0: luma, 1: chroma
  static const int kLC = 2;

  SimdFunc();
  void(*add_avg[kSize])(int width, int height, int offset, int shift,
                        int bitdepth, const int16_t *src1, intptr_t stride1,
                        const int16_t *src2, intptr_t stride2,
                        Sample *dst, intptr_t dst_stride);
  void(*filter_copy_bipred[kSize])(int width, int height,
                                   int16_t offset, int shift,
                                   const Sample *ref, ptrdiff_t ref_stride,
                                   int16_t *pred, ptrdiff_t pred_stride);
  void(*filter_h_sample_sample[kLC])(int width, int height, int bitdepth,
                                     const int16_t *filter,
                                     const Sample *src, ptrdiff_t src_stride,
                                     Sample *dst, ptrdiff_t dst_stride);
  void(*filter_h_sample_short[kLC])(int width, int height, int bitdepth,
                                    const int16_t *filter,
                                    const Sample *src, ptrdiff_t src_stride,
                                    int16_t *dst, ptrdiff_t dst_stride);
  void(*filter_v_sample_sample[kLC])(int width, int height, int bitdepth,
                                     const int16_t *filter,
                                     const Sample *src, ptrdiff_t src_stride,
                                     Sample *dst, ptrdiff_t dst_stride);
  void(*filter_v_sample_short[kLC])(int width, int height, int bitdepth,
                                    const int16_t *filter,
                                    const Sample *src, ptrdiff_t src_stride,
                                    int16_t *dst, ptrdiff_t dst_stride);
  void(*filter_v_short_sample[kLC])(int width, int height, int bitdepth,
                                    const int16_t *filter,
                                    const int16_t *src, ptrdiff_t src_stride,
                                    Sample *dst, ptrdiff_t dst_stride);
  void(*filter_v_short_short[kLC])(int width, int height, int bitdepth,
                                   const int16_t *filter,
                                   const int16_t *src, ptrdiff_t src_stride,
                                   int16_t *dst, ptrdiff_t dst_stride);
};

template<>
inline int InterPrediction::GetFilterShift<int16_t, true>(int bitdepth) {
  const int head_room = InterPrediction::kInternalPrecision - bitdepth;
  return InterPrediction::kFilterPrecision + head_room;
}
template<>
inline int InterPrediction::GetFilterShift<int16_t, false>(int bitdepth) {
  return InterPrediction::kFilterPrecision;
}
template<>
inline int InterPrediction::GetFilterShift<Sample, false>(int bitdepth) {
  const int head_room = InterPrediction::kInternalPrecision - bitdepth;
  return InterPrediction::kFilterPrecision - head_room;
}
template<>
inline int InterPrediction::GetFilterShift<Sample, true>(int bitdepth) {
  return InterPrediction::kFilterPrecision;
}

template<>
inline int InterPrediction::GetFilterOffset<int16_t, true>(int shift) {
  return
    (InterPrediction::kInternalOffset << InterPrediction::kFilterPrecision) +
    (1 << (shift - 1));
}
template<>
inline int InterPrediction::GetFilterOffset<int16_t, false>(int shift) {
  return 0;
}
template<>
inline int InterPrediction::GetFilterOffset<Sample, false>(int shift) {
  return -(InterPrediction::kInternalOffset << shift);
}
template<>
inline int InterPrediction::GetFilterOffset<Sample, true>(int shift) {
  return 1 << (shift - 1);
}

}   // namespace xvc

#endif  // XVC_COMMON_LIB_INTER_PREDICTION_H_
