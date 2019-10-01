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

#include <array>
#include <cassert>
#include <memory>

#include "googletest/include/gtest/gtest.h"

#include "xvc_common_lib/coding_unit.h"
#include "xvc_common_lib/picture_data.h"
#include "xvc_common_lib/quantize.h"
#include "xvc_common_lib/restrictions.h"
#include "xvc_dec_lib/syntax_reader.h"
#include "xvc_enc_lib/bit_writer.h"
#include "xvc_enc_lib/syntax_writer.h"

namespace {

class ResidualCoding : public ::testing::TestWithParam<int> {
protected:
  void SetUp() override {
    double lambda = 0;
    qp_.reset(new xvc::Qp(32, chroma_format, bitdepth, lambda));
    enc_coeff.fill(0);
    dec_coeff.fill(0xff);
  }

  void Encode(std::vector<uint8_t> *bitstream) {
    xvc::PictureData pic_data(chroma_format, width_, height_, bitdepth);
    xvc::CodingUnit *cu = pic_data.CreateCu(cu_tree, 0, 0, 0, width_, height_);
    cu->SetPredMode(xvc::PredictionMode::kInter);  // for diag scan order
    xvc::BitWriter bit_writer;
    xvc::SyntaxWriter writer(*qp_, pic_type, &bit_writer);
    writer.WriteCoefficients(*cu, comp, &enc_coeff[0], coeff_stride);
    writer.Finish();
    *bitstream = *bit_writer.GetBytes();
    pic_data.ReleaseCu(cu);
  }

  void Decode(const std::vector<uint8_t> &bitstream) {
    xvc::PictureData pic_data(chroma_format, width_, height_, bitdepth);
    xvc::CodingUnit *cu = pic_data.CreateCu(cu_tree, 0, 0, 0, width_, height_);
    cu->SetPredMode(xvc::PredictionMode::kInter);  // for diag scan order
    xvc::BitReader bit_reader(&bitstream[0], bitstream.size());
    std::unique_ptr<xvc::SyntaxReader> syntax_reader =
      xvc::SyntaxReader::Create(*qp_, pic_type, &bit_reader);
    dec_coeff.fill(0);  // caller responsiblility
    syntax_reader->ReadCoefficients(*cu, comp, &dec_coeff[0], coeff_stride);
    ASSERT_EQ(true, syntax_reader->Finish());
    pic_data.ReleaseCu(cu);
  }

  void EncodeDecodeVerify() {
    assert(width_ <= kMaxWidth);
    assert(height_ <= kMaxHeight);
    std::vector<uint8_t> bitstream;
    Encode(&bitstream);
    Decode(bitstream);
    xvc::Coeff *enc = &enc_coeff[0];
    xvc::Coeff *dec = &dec_coeff[0];
    for (int y = 0; y < height_; y++) {
      for (int x = 0; x < width_; x++) {
        ASSERT_EQ(enc[x], dec[x]) << "at y=" << y << " x=" << x;
      }
      for (int x = width_; x < coeff_stride; x++) {
        ASSERT_EQ(0, dec[x]) << "padding at y=" << y << " x=" << x;
      }
      enc += coeff_stride;
      dec += coeff_stride;
    }
  }

  constexpr static int kMaxWidth = 16;
  constexpr static int kMaxHeight = 16;
  constexpr static ptrdiff_t coeff_stride = kMaxWidth << 1;
  const xvc::CuTree cu_tree = xvc::CuTree::Primary;
  const xvc::YuvComponent comp = xvc::YuvComponent::kY;
  const int bitdepth = 8;
  const xvc::PicturePredictionType pic_type = xvc::PicturePredictionType::kBi;
  const xvc::ChromaFormat chroma_format = xvc::ChromaFormat::k420;
  const xvc::PredictionMode pred_mode = xvc::PredictionMode::kInter;
  std::array<xvc::Coeff, coeff_stride * kMaxHeight> enc_coeff;
  std::array<xvc::Coeff, coeff_stride * kMaxHeight> dec_coeff;
  std::unique_ptr<xvc::Qp> qp_;
  int width_ = kMaxWidth;
  int height_ = kMaxHeight;
};

TEST_P(ResidualCoding, DcOnly) {
  enc_coeff[0] = GetParam();
  EncodeDecodeVerify();
}

TEST_P(ResidualCoding, AcOnly) {
  enc_coeff[1] = GetParam();
  EncodeDecodeVerify();
}

TEST_P(ResidualCoding, AllCoeff) {
  enc_coeff.fill(GetParam());
  EncodeDecodeVerify();
}

TEST_P(ResidualCoding, LastOnly) {
  int pos = (height_ - 1) * coeff_stride + width_ - 1;
  enc_coeff[pos] = GetParam();
  EncodeDecodeVerify();
}

TEST_P(ResidualCoding, 4x4DcOnly) {
  width_ = 4;
  height_ = 4;
  enc_coeff[0] = GetParam();
  EncodeDecodeVerify();
}

TEST_P(ResidualCoding, 4x4AcOnly) {
  width_ = 4;
  height_ = 4;
  enc_coeff[3 * coeff_stride + 3] = GetParam();
  EncodeDecodeVerify();
}

TEST_F(ResidualCoding, AllZero) {
  if (!xvc::Restrictions::Get().disable_transform_cbf) {
    // this functionality is not used when cbf is available
    return;
  }
  enc_coeff.fill(0);
  EncodeDecodeVerify();
}

INSTANTIATE_TEST_CASE_P(CoeffValues, ResidualCoding,
                        ::testing::Values(1, 2, 3, 255));

}   // namespace
