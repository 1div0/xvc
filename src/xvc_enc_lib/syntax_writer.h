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

#ifndef XVC_ENC_LIB_SYNTAX_WRITER_H_
#define XVC_ENC_LIB_SYNTAX_WRITER_H_

#include "xvc_common_lib/cabac.h"
#include "xvc_common_lib/coding_unit.h"
#include "xvc_common_lib/intra_prediction.h"
#include "xvc_common_lib/picture_types.h"
#include "xvc_common_lib/quantize.h"
#include "xvc_common_lib/transform.h"
#include "xvc_enc_lib/entropy_encoder.h"

namespace xvc {

using Contexts = CabacContexts<ContextModel>;

class SyntaxWriter {
public:
  SyntaxWriter(const Qp &qp, PicturePredictionType pic_type,
               BitWriter *bit_writer);
  SyntaxWriter(const Contexts &contexts, EntropyEncoder &&entropyenc);
  const Contexts& GetContexts() const { return ctx_; }
  Bits GetNumWrittenBits() const {
    return encoder_.GetNumWrittenBits();
  }
  Bits GetFractionalBits() const {
    return encoder_.GetFractionalBits();
  }
  void ResetBitCounting() { encoder_.ResetBitCounting(); }
  void Finish();

  void WriteAffineFlag(const CodingUnit &cu, bool is_merge, bool use_affine);
  void WriteCbf(const CodingUnit &cu, YuvComponent comp, bool cbf);
  int WriteCoefficients(const CodingUnit &cu, YuvComponent comp,
                        const Coeff *coeff, ptrdiff_t coeff_stride);
  void WriteEndOfSlice(bool end_of_slice);
  void WriteInterDir(const CodingUnit &cu, InterDir inter_dir);
  void WriteInterFullpelMvFlag(const CodingUnit &cu, bool fullpel_mv_only);
  void WriteInterMvd(const MvDelta &mvd);
  void WriteInterMvpIdx(const CodingUnit &cu, int mvp_idx);
  void WriteInterRefIdx(int ref_idx, int num_refs_available);
  void WriteIntraMode(IntraMode intra_mode, const IntraPredictorLuma &mpm);
  void WriteIntraChromaMode(IntraChromaMode chroma_mode,
                            IntraPredictorChroma chroma_preds);
  void WriteLicFlag(bool use_lic);
  void WriteMergeFlag(bool merge);
  void WriteMergeIdx(int merge_idx);
  void WritePartitionType(const CodingUnit &cu, PartitionType type);
  void WritePredMode(PredictionMode pred_mode);
  void WriteQp(int qp_value, int predicted_qp, int aqp_mode);
  void WriteRootCbf(bool root_cbf);
  void WriteSkipFlag(const CodingUnit &cu, bool flag);
  void WriteSplitBinary(const CodingUnit &cu,
                        SplitRestriction split_restriction, SplitType split);
  void WriteSplitQuad(const CodingUnit &cu, int max_depth, SplitType split);
  void WriteTransformSkip(const CodingUnit &cu, YuvComponent comp,
                          bool tx_skip);
  void WriteTransformSelectEnable(const CodingUnit &cu, bool enable);
  void WriteTransformSelectIdx(const CodingUnit &cu, int type_idx);

private:
  template<int SubBlockShift>
  int WriteCoeffSubblock(const CodingUnit &cu, YuvComponent comp,
                         const Coeff *coeff, ptrdiff_t coeff_stride);
  void WriteCoeffLastPos(int width, int height, YuvComponent comp,
                         ScanOrder scan_order, int last_pos_x,
                         int last_pos_y);
  void WriteCoeffRemainExpGolomb(uint32_t abs_level, uint32_t golomb_rice_k);
  void WriteExpGolomb(uint32_t abs_level, uint32_t golomb_rice_k);
  void WriteUnaryMaxSymbol(uint32_t symbol, uint32_t max_val,
                           ContextModel *ctx_start, ContextModel *ctx_rest);

  Contexts ctx_;
  EntropyEncoder encoder_;
  friend class RdoSyntaxWriter;
};

class RdoSyntaxWriter : public SyntaxWriter {
public:
  explicit RdoSyntaxWriter(const SyntaxWriter &writer);
  explicit RdoSyntaxWriter(const RdoSyntaxWriter &writer);
  RdoSyntaxWriter(const SyntaxWriter &writer, uint32_t bits_written);
  RdoSyntaxWriter(const SyntaxWriter &writer, uint32_t bits_written,
                  uint32_t frac_bits);
  RdoSyntaxWriter& operator=(const RdoSyntaxWriter &writer);
};

}   // namespace xvc

#endif  // XVC_ENC_LIB_SYNTAX_WRITER_H_
