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

#include <algorithm>
#include <iterator>
#include <vector>

#include "googletest/include/gtest/gtest.h"

#include "xvc_test/decoder_helper.h"
#include "xvc_test/encoder_helper.h"
#include "xvc_test/yuv_helper.h"

using xvc_test::NalUnit;

namespace {

static const int kSubGopLength = 4;
static const int kSegmentLength = 8;
static const int qp = 32;

class DecoderScalabilityTest : public ::testing::TestWithParam<bool>,
  public ::xvc_test::EncoderHelper, public ::xvc_test::DecoderHelper {
protected:
  void SetUp() override {
    EncoderHelper::Init();
    DecoderHelper::Init();
  }

  std::vector<NalUnit> EncodeBitstream(int width, int height,
                                       int internal_bitdepth, int frames) {
    const int input_bitdepth = 8;
    xvc::EncoderSettings encoder_settings = GetDefaultEncoderSettings();
    encoder_settings.leading_pictures = GetParam() ? 1 : 0;
    SetupEncoder(encoder_settings, width, height, internal_bitdepth, qp);
    encoder_->SetSubGopLength(kSubGopLength);
    encoder_->SetSegmentLength(kSegmentLength);
    encoder_->SetClosedGopInterval(1000);   // force open gop
    encoded_nal_units_.clear();
    for (int i = 0; i < frames; i++) {
      auto orig_pic = xvc_test::TestYuvPic(width, height, input_bitdepth, i, i);
      EncodeOneFrame(orig_pic.GetBytes(), orig_pic.GetBitdepth());
    }
    EncoderFlush();
    return encoded_nal_units_;
  }

  int DecodeBitstream(int width, int height) {
    ResetBitstreamPosition();
    DecodeSegmentHeaderSuccess(GetNextNalToDecode());
    int decoded_pictures = 0;
    while (HasMoreNals()) {
      auto &nal = GetNextNalToDecode();
      EXPECT_EQ(nal.size(), decoder_->DecodeNal(&nal[0], nal.size()));
      if (decoder_->GetDecodedPicture(&last_decoded_picture_)) {
        decoded_pictures++;
      }
    }
    while (DecoderFlushAndGet()) {
      decoded_pictures++;
    }
    return decoded_pictures;
  }
};

TEST_P(DecoderScalabilityTest, ReferencePicDownscaling) {
  auto is_segment_header = [](const NalUnit &nal) {
    xvc::BitReader bit_reader(&nal[0], nal.size());
    xvc::NalUnitType nal_type;
    EXPECT_TRUE(xvc::Decoder::ParseNalUnitHeader(&bit_reader, &nal_type, true));
    return nal_type == xvc::NalUnitType::kSegmentHeader;
  };
  std::vector<NalUnit> bitstream1 = EncodeBitstream(16, 16, 8,
                                                    1 + 2 * kSegmentLength);
  std::vector<NalUnit> bitstream2 = EncodeBitstream(24, 24, 8,
                                                    1 + 2 * kSegmentLength);
  encoded_nal_units_.clear();

  // Copy first segment from bitstream 1
  auto it1 = std::find_if(bitstream1.begin() + 1, bitstream1.end(),
                          is_segment_header);
  std::copy(bitstream1.begin(), it1, std::back_inserter(encoded_nal_units_));

  // Copy second segment from bitstream 2
  auto it2 = std::find_if(bitstream2.begin() + 1, bitstream2.end(),
                          is_segment_header);
  std::copy(it2, bitstream2.end(), std::back_inserter(encoded_nal_units_));

  // Decode merged bitstream
  int decoded_pics = DecodeBitstream(16, 16);
  int expected_decoded_pics = 1 + 2 * kSegmentLength;
  // Half of pictures from 1st bitstream will be corrupted
  /// due to different reference picture
  int expected_corrupted_pics = kSubGopLength / 2;
  EXPECT_EQ(decoded_pics, expected_decoded_pics);
  // Number of corrupted pictures varies depending on qp and rdoq
  EXPECT_LE(decoder_->GetNumCorruptedPics(), expected_corrupted_pics + 1);
  EXPECT_GT(decoder_->GetNumCorruptedPics(), 0);
}

INSTANTIATE_TEST_CASE_P(LeadingPictures, DecoderScalabilityTest,
                        ::testing::Bool());

}   // namespace
