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

#include "xvc_dec_lib/decoder.h"

#include <cassert>
#include <limits>

#include "xvc_common_lib/reference_list_sorter.h"
#include "xvc_common_lib/restrictions.h"
#include "xvc_common_lib/segment_header.h"
#include "xvc_common_lib/utils.h"
#include "xvc_dec_lib/segment_header_reader.h"
#include "xvc_dec_lib/thread_decoder.h"

namespace xvc {

constexpr size_t Decoder::kInvalidNal;

Decoder::Decoder(int num_threads)
  : curr_segment_header_(std::make_shared<SegmentHeader>()),
  prev_segment_header_(std::make_shared<SegmentHeader>()),
  simd_(SimdCpu::GetRuntimeCapabilities()) {
  if (num_threads != 0) {
    thread_decoder_ =
      std::unique_ptr<ThreadDecoder>(new ThreadDecoder(num_threads));
  }
}

Decoder::~Decoder() {
  if (thread_decoder_) {
    thread_decoder_->StopAll();
  }
}

size_t Decoder::DecodeNal(const uint8_t *nal_unit, size_t nal_unit_size,
                          int64_t user_data) {
  // Nal header parsing
  BitReader bit_reader(nal_unit, nal_unit_size);
  NalUnitType nal_unit_type;
  if (!ParseNalUnitHeader(&bit_reader, &nal_unit_type, accept_xvc_bit_zero_)) {
    return kInvalidNal;
  }

  // Segment header parsing
  if (nal_unit_type == NalUnitType::kSegmentHeader) {
    return DecodeSegmentHeaderNal(&bit_reader);
  }
  if (state_ == State::kNoSegmentHeader ||
      state_ == State::kDecoderVersionTooLow ||
      state_ == State::kBitstreamBitdepthTooHigh ||
      state_ == State::kBitstreamVersionTooLow) {
    // Do not decode anything else than a segment header if
    // no segment header has been decoded or if the xvc version
    // of the decoder is identified to be too low or if the
    // bitstream bitdepth is too high.
    return kInvalidNal;
  }
  if (nal_unit_type >= NalUnitType::kIntraPicture &&
      nal_unit_type <= NalUnitType::kReservedPictureType10) {
    return DecodePictureNal(nal_unit, nal_unit_size, user_data, &bit_reader);
  }
  return kInvalidNal;   // unknown nal type
}

bool Decoder::ParseNalUnitHeader(BitReader *bit_reader,
                                 NalUnitType *nal_unit_type,
                                 bool accept_xvc_bit_zero) {
  uint8_t header = bit_reader->ReadByte();
  // Check the xvc_bit_one to see if the Nal Unit shall be ignored.
  int xvc_bit_one = ((header >> 7) & 1);
  if (xvc_bit_one == 0) {
    if (accept_xvc_bit_zero &&
      (NalUnitType((header >> 1) & 31) == NalUnitType::kIntraAccessPicture ||
       NalUnitType((header >> 1) & 31) == NalUnitType::kPredictedPicture ||
       NalUnitType((header >> 1) & 31) == NalUnitType::kBipredictedPicture ||
       NalUnitType((header >> 1) & 31) == NalUnitType::kSegmentHeader)) {
      // The above NAL unit types of xvc version 1 bitstreams are allowed
      // to not have xvc_bit_one set to 1.
    } else if (header == constants::kEncapsulationCode) {
      // Nal units with an encapsulation code consisting of two bytes
      bit_reader->ReadByte();
      header = bit_reader->ReadByte();
    } else {
      return false;
    }
  }
  // Check the nal_rfe to see if the Nal Unit shall be ignored.
  int nal_rfe = ((header >> 6) & 1);
  if (nal_rfe == 1) {
    return false;
  }
  *nal_unit_type = NalUnitType((header >> 1) & 31);
  return true;
}

void Decoder::DecodeAllBufferedNals() {
  for (auto &&nal : nal_buffer_) {
    DecodeOneBufferedNal(std::move(nal.first), nal.second);
  }
  if (thread_decoder_) {
    thread_decoder_->WaitAll([this](std::shared_ptr<PictureDecoder> pic_dec,
                                    bool success, const PicDecList &deps) {
      OnPictureDecoded(pic_dec, success, deps);
    });
  }
  nal_buffer_.clear();
}

size_t Decoder::DecodeSegmentHeaderNal(BitReader *bit_reader) {
  // If there are old nal units buffered that are not tail pictures,
  // they are discarded before decoding the new segment.
  if (nal_buffer_.size() > static_cast<size_t>(num_tail_pics_)) {
    // When all intra we may have some buffered nals that can be decoded
    while (!nal_buffer_.empty() && num_pics_in_buffer_ < pic_buffering_num_) {
      auto &&nal = nal_buffer_.front();
      DecodeOneBufferedNal(std::move(nal.first), nal.second);
      nal_buffer_.pop_front();
    }
    num_pics_in_buffer_ -= static_cast<uint32_t>(nal_buffer_.size());
    nal_buffer_.clear();
    num_tail_pics_ = 0;
  }
  prev_segment_header_ = curr_segment_header_;
  curr_segment_header_ = std::make_shared<SegmentHeader>();
  soc_++;
  state_ =
    SegmentHeaderReader::Read(curr_segment_header_.get(), bit_reader, soc_,
                              &accept_xvc_bit_zero_);
  if (doc_ == 0 && curr_segment_header_->leading_pictures > 0) {
    doc_++;
  }
  if (state_ != State::kSegmentHeaderDecoded) {
    return kInvalidNal;
  }
  sub_gop_length_ = curr_segment_header_->max_sub_gop_length;
  if (sub_gop_length_ + 1 > sliding_window_length_) {
    sliding_window_length_ = additional_decoder_buffers_ +
      sub_gop_length_ + 1 + (thread_decoder_ ? 1 : 0);
  }
  pic_buffering_num_ =
    sliding_window_length_ + curr_segment_header_->num_ref_pics;

  if (output_pic_format_.width == 0) {
    output_pic_format_.width = curr_segment_header_->GetOutputWidth();
  }
  if (output_pic_format_.height == 0) {
    output_pic_format_.height = curr_segment_header_->GetOutputHeight();
  }
  if (output_pic_format_.chroma_format == ChromaFormat::kUndefined) {
    output_pic_format_.chroma_format = curr_segment_header_->chroma_format;
  }
  if (output_pic_format_.color_matrix == ColorMatrix::kUndefined) {
    output_pic_format_.color_matrix = curr_segment_header_->color_matrix;
  }
  if (output_pic_format_.bitdepth == 0) {
    output_pic_format_.bitdepth = curr_segment_header_->internal_bitdepth;
  }
  max_tid_ = SegmentHeader::GetFramerateMaxTid(
    decoder_ticks_, curr_segment_header_->bitstream_ticks, sub_gop_length_);
  return bit_reader->GetPosition();
}

size_t Decoder::DecodePictureNal(const uint8_t * nal_unit, size_t nal_unit_size,
                                 int64_t user_data, BitReader *bit_reader) {
  // All picture types are decoded using the same process.
  // First, the buffer flag is checked to see if the picture
  // should be decoded or buffered.
  int buffer_flag = bit_reader->ReadBit();
  int tid = bit_reader->ReadBits(3);
  int new_desired_max_tid = SegmentHeader::GetFramerateMaxTid(
    decoder_ticks_, curr_segment_header_->bitstream_ticks,
    curr_segment_header_->max_sub_gop_length);
  if (new_desired_max_tid < max_tid_ || tid == 0) {
    // Number of temporal layers can always be decreased,
    // but only increased at temporal layer 0 pictures.
    max_tid_ = new_desired_max_tid;
  }
  if (tid > max_tid_) {
    // Ignore (drop) picture if it belongs to a temporal layer that
    // should not be decoded.
    return nal_unit_size;
  }

  enforce_sliding_window_ = true;
  num_pics_in_buffer_++;

  NalUnitPtr nal_element(new std::vector<uint8_t>(&nal_unit[0], &nal_unit[0]
                                                  + nal_unit_size));
  if (buffer_flag == 0 && num_tail_pics_ > 0) {
    nal_buffer_.push_front({ std::move(nal_element), user_data });
  } else {
    nal_buffer_.push_back({ std::move(nal_element), user_data });
  }
  if (state_ == State::kSegmentHeaderDecoded) {
    state_ = State::kPicDecoded;
  }
  if (buffer_flag) {
    num_tail_pics_++;
    return nal_unit_size;
  }
  while (nal_buffer_.size() > 0 &&
         num_pics_in_buffer_ - nal_buffer_.size() + 1 < pic_buffering_num_) {
    auto &&nal = nal_buffer_.front();
    DecodeOneBufferedNal(std::move(nal.first), nal.second);
    nal_buffer_.pop_front();
  }
  return nal_unit_size;
}

void
Decoder::DecodeOneBufferedNal(NalUnitPtr &&nal, int64_t user_data) {
  BitReader pic_bit_reader(&(*nal)[0], nal->size());
  std::shared_ptr<SegmentHeader> segment_header = curr_segment_header_;
  std::shared_ptr<SegmentHeader> prev_segment_header = prev_segment_header_;

  int header = pic_bit_reader.ReadByte();
  int xvc_bit_one = ((header >> 7) & 1);
  if (xvc_bit_one == 0 && !accept_xvc_bit_zero_) {
    // Read two more bytes if encapsulation is used.
    pic_bit_reader.ReadBits(16);
  }

  // Special handling for tail pictures
  int buffer_flag = pic_bit_reader.ReadBits(1);
  pic_bit_reader.Rewind(9);
  if (buffer_flag) {
    segment_header = prev_segment_header_;
    num_tail_pics_--;
  }

  // Parse picture header to determine poc
  PictureDecoder::PicNalHeader pic_header =
    PictureDecoder::DecodeHeader(*segment_header, &pic_bit_reader,
                                 &sub_gop_end_poc_, &sub_gop_start_poc_,
                                 &sub_gop_length_,
                                 prev_segment_header_->max_sub_gop_length,
                                 doc_, soc_, num_tail_pics_);
  doc_ = pic_header.doc + 1;

  // Reload restriction flags for current thread if segment has changed
  thread_local SegmentNum loaded_restrictions_soc = static_cast<SegmentNum>(-1);
  if (loaded_restrictions_soc != segment_header->soc) {
    Restrictions::GetRW() = segment_header->restrictions;
  }

  // Determine dependencies for reference picture based on poc and tid
  const bool is_intra_nal =
    pic_header.nal_unit_type == NalUnitType::kIntraPicture ||
    pic_header.nal_unit_type == NalUnitType::kIntraAccessPicture;
  ReferenceListSorter<PictureDecoder>
    ref_list_sorter(*segment_header, prev_segment_header_->open_gop);
  ReferencePictureLists ref_pic_list;
  auto inter_dependencies =
    ref_list_sorter.Prepare(pic_header.poc, pic_header.tid, is_intra_nal,
                            pic_decoders_, &ref_pic_list,
                            segment_header->leading_pictures);

  // Bump ref count before finding an available picture decoder
  for (auto &pic_dep : inter_dependencies) {
    pic_dep->AddReferenceCount(1);
  }

  // Find an available decoder to use for this nal
  std::shared_ptr<PictureDecoder> pic_dec;
  if (thread_decoder_) {
    while (!(pic_dec = GetFreePictureDecoder(*segment_header))) {
      thread_decoder_->WaitOne([this](std::shared_ptr<PictureDecoder> pic,
                                      bool success, const PicDecList &deps) {
        OnPictureDecoded(pic, success, deps);
      });
    }
  } else {
    pic_dec = GetFreePictureDecoder(*segment_header);
    assert(pic_dec);
  }

  // Setup poc and output status on main thread
  pic_dec->Init(*segment_header, pic_header, std::move(ref_pic_list),
                output_pic_format_, user_data);

  // Special handling of inter dependency ref counting for lowest layer
  if (pic_header.tid == 0) {
    // This picture might be used by later lowest temporal layer pictures.
    // Force an extra ref until we are sure it is no longer referenced
    pic_dec->AddReferenceCount(1);
    zero_tid_pic_dec_.push_back(pic_dec);
    // Restrict number of lowest layer pictures that must be in picture buffer
    while (static_cast<int>(zero_tid_pic_dec_.size()) >
           segment_header->num_ref_pics + 1) {
      auto &pic = zero_tid_pic_dec_.front();
      pic->RemoveReferenceCount(1);
      zero_tid_pic_dec_.pop_front();
    }
  }

  if (thread_decoder_) {
    thread_decoder_->DecodeAsync(std::move(segment_header),
                                 std::move(prev_segment_header),
                                 std::move(pic_dec),
                                 std::move(inter_dependencies), std::move(nal),
                                 pic_bit_reader.GetPosition());
    if (state_ == State::kSegmentHeaderDecoded) {
      state_ = State::kPicDecoded;
    }
  } else {
    // Synchronous decode
    bool success = pic_dec->Decode(*segment_header, *prev_segment_header,
                                   &pic_bit_reader, true);
    OnPictureDecoded(pic_dec, success, inter_dependencies);
  }
}

void Decoder::FlushBufferedNalUnits() {
  // Remove restriction of minimum picture buffer size
  // so that we can output any remaining pictures
  enforce_sliding_window_ = false;
  // Preparing to start a new segment
  soc_++;
  prev_segment_header_ = curr_segment_header_;
  // Check if there are buffered nal units.
  if (nal_buffer_.size() > 0) {
    if (curr_segment_header_->open_gop &&
        curr_segment_header_->num_ref_pics > 0) {
      // throw away buffered nals that are impossible to decode without the
      // next random acess picture available as reference
      num_pics_in_buffer_ -= static_cast<uint32_t>(nal_buffer_.size());
      nal_buffer_.clear();
    } else {
      if (curr_segment_header_->num_ref_pics == 0) {
        soc_--;
      } else if (sub_gop_length_ > 1) {
        // Step over the missing key picture and then decode the buffered nals
        doc_++;
        sub_gop_start_poc_ = sub_gop_end_poc_;
        sub_gop_end_poc_ += sub_gop_length_;
      }
      DecodeAllBufferedNals();
    }
  }
  // After flush a new segment must be parsed
  // to avoid decoding new nals with old or invalid reference pictures
  state_ = State::kNoSegmentHeader;
}

bool Decoder::GetDecodedPicture(xvc_decoded_picture *output_pic) {
  // Prevent outputing pictures if non are available
  // otherwise reference pictures might be corrupted
  if (!HasPictureReadyForOutput()) {
    output_pic->size = 0;
    output_pic->bytes = nullptr;
    for (int c = 0; c < constants::kMaxYuvComponents; c++) {
      output_pic->planes[c] = nullptr;
      output_pic->stride[c] = 0;
    }
    return false;
  }

  // Find the picture with lowest poc that has not been output.
  std::shared_ptr<PictureDecoder> pic_dec;
  PicNum lowest_poc = std::numeric_limits<PicNum>::max();
  for (auto &pic : pic_decoders_) {
    auto pd = pic->GetPicData();
    if (pic->GetOutputStatus() != OutputStatus::kHasBeenOutput &&
        pd->GetPoc() < lowest_poc) {
      pic_dec = pic;
      lowest_poc = pd->GetPoc();
    }
  }
  if (!pic_dec) {
    output_pic->size = 0;
    output_pic->bytes = nullptr;
    for (int c = 0; c < constants::kMaxYuvComponents; c++) {
      output_pic->planes[c] = nullptr;
      output_pic->stride[c] = 0;
    }
    return false;
  }

  // Wait for picture to finish decoding
  if (thread_decoder_) {
    thread_decoder_->WaitForPicture(
      pic_dec, [this](std::shared_ptr<PictureDecoder> pic, bool success,
                      const PicDecList &deps) {
      OnPictureDecoded(pic, success, deps);
    });
  }

  pic_dec->SetOutputStatus(OutputStatus::kHasBeenOutput);
  SetOutputStats(pic_dec, output_pic);
  const int sample_size = output_pic_format_.bitdepth == 8 ? 1 : 2;
  // TODO(PH) Potential dangerous race-condition, the output_pic->bytes will
  // be modified concurrently when pic_dec is assigned a new nal to decode,
  // so we don't do that until next call to 'decoder_decode_nal'
  const std::vector<uint8_t> &output_pic_bytes =
    pic_dec->GetOutputPictureBytes();
  output_pic->size = output_pic_bytes.size();
  output_pic->bytes = output_pic_bytes.empty() ? nullptr :
    reinterpret_cast<const char *>(&output_pic_bytes[0]);
  output_pic->planes[0] = output_pic->bytes;
  output_pic->stride[0] = output_pic_format_.width * sample_size;
  output_pic->planes[1] =
    output_pic->planes[0] + output_pic->stride[0] * output_pic_format_.height;
  output_pic->stride[1] =
    util::ScaleChromaX(output_pic_format_.width,
                       output_pic_format_.chroma_format) * sample_size;
  output_pic->planes[2] = output_pic->planes[1] + output_pic->stride[1] *
    util::ScaleChromaY(output_pic_format_.height,
                       output_pic_format_.chroma_format);
  output_pic->stride[2] = output_pic->stride[1];

  // Decrease counter for how many decoded pictures are buffered.
  num_pics_in_buffer_--;
  return true;
}

std::shared_ptr<PictureDecoder>
Decoder::GetFreePictureDecoder(const SegmentHeader &segment) {
  if (pic_decoders_.size() < pic_buffering_num_) {
    auto pic =
      std::make_shared<PictureDecoder>(simd_, segment.GetInternalPicFormat(),
                                       segment.GetCropWidth(),
                                       segment.GetCropHeight());
    pic_decoders_.push_back(pic);
    return pic;
  }

  PicNum best_poc = std::numeric_limits<PicNum>::max();
  auto pic_dec_it = pic_decoders_.end();
  for (auto it = pic_decoders_.begin(); it != pic_decoders_.end(); ++it) {
    // A picture decoder has two independent variables that indicates usage
    // 1. output status - if decoded samples has been sent to application
    // 2. ref count - if picture is no longer referenced by any other pictures
    if ((*it)->IsReferenced() ||
      (*it)->GetOutputStatus() != OutputStatus::kHasBeenOutput) {
      continue;
    }
    if ((*it)->GetPicData()->GetPoc() < best_poc) {
      best_poc = (*it)->GetPicData()->GetPoc();
      pic_dec_it = it;
    }
  }
  if (pic_dec_it == pic_decoders_.end()) {
    return std::shared_ptr<PictureDecoder>();
  }

  // Replace the PictureDecoder if the picture format has changed.
  auto pic_data = (*pic_dec_it)->GetPicData();
  if (segment.GetInternalWidth() !=
      pic_data->GetPictureWidth(YuvComponent::kY) ||
      segment.GetInternalHeight() !=
      pic_data->GetPictureHeight(YuvComponent::kY) ||
      segment.chroma_format != pic_data->GetChromaFormat() ||
      segment.internal_bitdepth != pic_data->GetBitdepth()) {
    pic_dec_it->reset(new PictureDecoder(simd_, segment.GetInternalPicFormat(),
                                         segment.GetCropWidth(),
                                         segment.GetCropHeight()));
  }
  return *pic_dec_it;
}

void Decoder::OnPictureDecoded(std::shared_ptr<PictureDecoder> pic_dec,
                               bool success, const PicDecList &inter_deps) {
  pic_dec->SetOutputStatus(OutputStatus::kHasNotBeenOutput);
  pic_dec->SetIsConforming(success);
  for (auto &pic_dep : inter_deps) {
    pic_dep->RemoveReferenceCount(1);
  }
  if (success) {
    if (state_ != State::kChecksumMismatch) {
      state_ = State::kPicDecoded;
    }
  } else {
    state_ = State::kChecksumMismatch;
    num_corrupted_pics_++;
  }
}

void Decoder::SetOutputStats(std::shared_ptr<PictureDecoder> pic_dec,
                             xvc_decoded_picture *output_pic) {
  const int poc_offset = (curr_segment_header_->leading_pictures != 0 ? -1 : 0);
  auto pic_data = pic_dec->GetPicData();
  output_pic->user_data = pic_dec->GetNalUserData();
  output_pic->stats.width = output_pic_format_.width;
  output_pic->stats.height = output_pic_format_.height;
  output_pic->stats.bitdepth = output_pic_format_.bitdepth;
  output_pic->stats.chroma_format =
    xvc_dec_chroma_format(output_pic_format_.chroma_format);
  output_pic->stats.color_matrix =
    xvc_dec_color_matrix(output_pic_format_.color_matrix);
  output_pic->stats.bitstream_bitdepth = pic_data->GetBitdepth();
  output_pic->stats.framerate =
    SegmentHeader::GetFramerate(max_tid_, curr_segment_header_->bitstream_ticks,
                                curr_segment_header_->max_sub_gop_length);
  output_pic->stats.bitstream_framerate =
    SegmentHeader::GetFramerate(0, curr_segment_header_->bitstream_ticks, 1);
  output_pic->stats.nal_unit_type =
    static_cast<uint32_t>(pic_data->GetNalType());
  output_pic->stats.profile =
    Restrictions::Get().CheckBaselineCompatibility() ? 1 : 0;

  // Expose the 32 least significant bits of poc and doc.
  output_pic->stats.poc =
    static_cast<uint32_t>(pic_data->GetPoc() + poc_offset);
  output_pic->stats.doc =
    static_cast<uint32_t>(pic_data->GetDoc() + poc_offset);
  output_pic->stats.soc = static_cast<uint32_t>(pic_data->GetSoc());
  output_pic->stats.tid = pic_data->GetTid();
  output_pic->stats.qp = pic_data->GetPicQp()->GetQpRaw(YuvComponent::kY);
  output_pic->stats.conforming = pic_dec->GetIsConforming() ? 1 : 0;

  // Expose the first five reference pictures in L0 and L1.
  ReferencePictureLists* rpl = pic_data->GetRefPicLists();
  int length = sizeof(output_pic->stats.l0) / sizeof(output_pic->stats.l0[0]);
  for (int i = 0; i < length; i++) {
    if (i < rpl->GetNumRefPics(RefPicList::kL0)) {
      output_pic->stats.l0[i] =
        static_cast<int32_t>(rpl->GetRefPoc(RefPicList::kL0, i) + poc_offset);
    } else {
      output_pic->stats.l0[i] = -1;
    }
    if (i < rpl->GetNumRefPics(RefPicList::kL1)) {
      output_pic->stats.l1[i] =
        static_cast<int32_t>(rpl->GetRefPoc(RefPicList::kL1, i) + poc_offset);
    } else {
      output_pic->stats.l1[i] = -1;
    }
  }
}

}   // namespace xvc
