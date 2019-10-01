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

#ifndef XVC_ENC_LIB_ENCODER_H_
#define XVC_ENC_LIB_ENCODER_H_

#include <deque>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <unordered_map>

#include "xvc_common_lib/common.h"
#include "xvc_common_lib/picture_data.h"
#include "xvc_common_lib/resample.h"
#include "xvc_common_lib/restrictions.h"
#include "xvc_common_lib/segment_header.h"
#include "xvc_enc_lib/bit_writer.h"
#include "xvc_enc_lib/picture_encoder.h"
#include "xvc_enc_lib/encoder_settings.h"
#include "xvc_enc_lib/encoder_simd_functions.h"

struct xvc_encoder {};

namespace xvc {

class ThreadEncoder;

class Encoder : public xvc_encoder {
public:
  using PicPlane = std::pair<const uint8_t *, ptrdiff_t>;
  using PicPlanes = std::array<PicPlane, constants::kMaxYuvComponents>;
  explicit Encoder(int internal_bitdepth, int num_threads = 0);
  ~Encoder();
  bool Encode(const uint8_t *pic_bytes, xvc_enc_pic_buffer *rec_pic,
              int64_t user_data = 0);
  bool Encode(const PicPlanes &planes,
              xvc_enc_pic_buffer *rec_pic, int64_t user_data = 0);
  bool Flush(xvc_enc_pic_buffer *rec_pic);
  std::vector<xvc_enc_nal_unit>& GetOutputNals() {
    return api_output_nals_;
  }
  const SegmentHeader* GetCurrentSegment() const {
    return segment_header_.get();
  }

  void SetCpuCapabilities(std::set<CpuCapability> capabilities) {
    simd_ = EncoderSimdFunctions(capabilities,
                                 segment_header_->internal_bitdepth);
  }
  void SetResolution(int width, int height) {
    segment_header_->SetWidth(width);
    segment_header_->SetHeight(height);
  }
  void SetChromaFormat(ChromaFormat chroma_fmt) {
    segment_header_->chroma_format = chroma_fmt;
  }
  void SetColorMatrix(ColorMatrix color_matrix) {
    segment_header_->color_matrix = color_matrix;
  }
  int GetNumRefPics() const { return segment_header_->num_ref_pics; }
  void SetNumRefPics(int num) { segment_header_->num_ref_pics = num; }
  void SetInputBitdepth(int bitdepth) { input_bitdepth_ = bitdepth; }
  void SetFramerate(double rate) { framerate_ = rate; }
  void SetSubGopLength(PicNum sub_gop_length) {
    assert(sub_gop_length > 0);
    segment_header_->max_sub_gop_length = sub_gop_length;
  }
  void SetLowDelay(bool low_delay) {
    segment_header_->low_delay = low_delay;
  }
  void SetSegmentLength(PicNum length) { segment_length_ = length; }
  void SetClosedGopInterval(PicNum interval) {
    assert(interval > 0);
    closed_gop_interval_ = interval;
  }
  void SetChromaQpOffsetTable(int table) {
    segment_header_->chroma_qp_offset_table = table;
  }
  void SetChromaQpOffsetU(int offset) {
    segment_header_->chroma_qp_offset_u = offset;
  }
  void SetChromaQpOffsetV(int offset) {
    segment_header_->chroma_qp_offset_v = offset;
  }
  void SetDeblockingMode(DeblockingMode deblocking_mode) {
    segment_header_->deblocking_mode = deblocking_mode;
  }
  void SetBetaOffset(int offset) { segment_header_->beta_offset = offset; }
  void SetTcOffset(int offset) { segment_header_->tc_offset = offset; }
  void SetQp(int qp) {
    segment_qp_ =
      util::Clip3(qp, constants::kMinAllowedQp, constants::kMaxAllowedQp);
  }
  void SetChecksumMode(Checksum::Mode mode) {
    segment_header_->checksum_mode = mode;
  }

  const EncoderSettings& GetEncoderSettings() { return encoder_settings_; }
  void SetEncoderSettings(const EncoderSettings &settings);

private:
  using NalBuffer = std::unique_ptr<std::vector<uint8_t>>;
  using PicEncList = std::vector<std::shared_ptr<const PictureEncoder>>;
  bool Encode(const uint8_t *pic_bytes, const PicPlanes *planes,
              xvc_enc_pic_buffer *rec_pic, int64_t user_data);
  void Initialize();
  void StartNewSegment();
  void EncodeOnePicture(std::shared_ptr<PictureEncoder> pic);
  void OnPictureEncoded(std::shared_ptr<PictureEncoder> pic_enc,
                        const PicEncList &inter_deps,
                        NalBuffer &&pic_nal_buffer);
  void PrepareOutputNals();
  void ReconstructNextPicture(xvc_enc_pic_buffer *rec_pic);
  std::shared_ptr<PictureEncoder>
    PrepareNewInputPicture(const SegmentHeader &segment, PicNum doc, PicNum poc,
                           int tid, bool is_access_picture,
                           const uint8_t *pic_bytes,
                           const PicPlanes *pic_planes, int64_t user_data);
  void DetermineBufferFlags(const PictureEncoder &pic_enc);
  void UpdateReferenceCounts(PicNum last_subgop_end_poc);
  std::shared_ptr<PictureEncoder> GetNewPictureEncoder();
  std::shared_ptr<PictureEncoder> RewriteLeadingPictures();
  xvc_enc_nal_unit WriteSegmentHeaderNal(const SegmentHeader &segment_header,
                                         BitWriter *bit_writer);
  void SetNalStats(const PictureData &pic_data, const PictureEncoder &pic_enc,
                   xvc_enc_nal_stats *nal_stats);

  bool initialized_ = false;
  int input_bitdepth_ = 8;
  double framerate_ = 0;
  std::shared_ptr<SegmentHeader> segment_header_;
  std::shared_ptr<SegmentHeader> prev_segment_header_;
  PicNum sub_gop_start_poc_ = 0;
  PicNum poc_ = 0;
  PicNum doc_ = 0;
  PicNum segment_length_ = 1;
  PicNum closed_gop_interval_ = std::numeric_limits<PicNum>::max();
  size_t pic_buffering_num_ = 1;
  int extra_num_buffered_subgops_ = 0;
  int segment_qp_ = std::numeric_limits<int>::max();
  EncoderSimdFunctions simd_;
  EncoderSettings encoder_settings_;
  Resampler input_resampler_;
  std::vector<std::shared_ptr<PictureEncoder>> pic_encoders_;
  std::vector<uint8_t> output_pic_bytes_;
  BitWriter segment_header_bit_writer_;
  std::vector<xvc_enc_nal_unit> api_output_nals_;
  std::vector<NalBuffer> avail_nal_buffers_;
  std::deque<PicNum> doc_bitstream_order_;
  std::unordered_map<PicNum,
    std::pair<NalBuffer, xvc_enc_nal_unit>> pending_out_nal_buffers_;
  PicNum last_rec_poc_ = static_cast<PicNum>(-1);
  std::unique_ptr<ThreadEncoder> thread_encoder_;
};

}   // namespace xvc

#endif  // XVC_ENC_LIB_ENCODER_H_
