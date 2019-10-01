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

#include "xvc_common_lib/coding_unit.h"

#include <algorithm>
#include <cassert>
#include <cstring>

#include "xvc_common_lib/picture_data.h"
#include "xvc_common_lib/sample_buffer.h"
#include "xvc_common_lib/quantize.h"
#include "xvc_common_lib/utils.h"

namespace xvc {

CodingUnit::TransformState::TransformState()
  : root_cbf(false),
  cbf({ { false, false, false } }),
  transform_skip({ { false, false, false } }),
  transform_type({ { { { TransformType::kDefault, TransformType::kDefault } },
  { { TransformType::kDefault, TransformType::kDefault } } } }),
  transform_select_idx(-1) {
}

CodingUnit::CodingUnit(PictureData *pic_data, CoeffCtuBuffer *ctu_coeff,
                       CuTree cu_tree, int depth, int pic_x, int pic_y,
                       int width, int height)
  : pic_data_(pic_data),
  ctu_coeff_(ctu_coeff),
  cu_tree_(cu_tree),
  pos_x_(pic_x),
  pos_y_(pic_y),
  width_(width),
  height_(height),
  depth_(depth),
  split_state_(SplitType::kNone),
  pred_mode_(PredictionMode::kIntra),
  sub_cu_list_({ { nullptr, nullptr, nullptr, nullptr } }),
  qp_(pic_data->GetPicQp()),
  tx_(),
  intra_(),
  inter_() {
}

CodingUnit& CodingUnit::operator=(const CodingUnit &cu) {
  assert(cu_tree_ == cu.cu_tree_);
  pos_x_ = cu.pos_x_;
  pos_y_ = cu.pos_y_;
  width_ = cu.width_;
  height_ = cu.height_;
  depth_ = cu.depth_;
  assert(split_state_ == SplitType::kNone &&
         cu.split_state_ == SplitType::kNone);
  pred_mode_ = cu.pred_mode_;
  qp_ = cu.qp_;
  tx_ = cu.tx_;
  intra_ = cu.intra_;
  inter_ = cu.inter_;
  return *this;
}

void CodingUnit::ResetPredictionState() {
  tx_ = TransformState();
  intra_ = IntraState();
  inter_ = InterState();
}

void CodingUnit::CopyPositionAndSizeFrom(const CodingUnit &cu) {
  assert(cu_tree_ == cu.cu_tree_);
  pos_x_ = cu.pos_x_;
  pos_y_ = cu.pos_y_;
  width_ = cu.width_;
  height_ = cu.height_;
  depth_ = cu.depth_;
  qp_ = cu.qp_;
}

void CodingUnit::CopyPredictionDataFrom(const CodingUnit &cu) {
  pred_mode_ = cu.pred_mode_;
  qp_ = cu.qp_;
  tx_ = cu.tx_;
  intra_ = cu.intra_;
  inter_ = cu.inter_;
}

int CodingUnit::GetBinaryDepth() const {
  int quad_size_log2 = util::SizeToLog2(constants::kCtuSize >> depth_);
  return (quad_size_log2 - util::SizeToLog2(width_)) +
    (quad_size_log2 - util::SizeToLog2(height_));
}

bool CodingUnit::IsBinarySplitValid() const {
  int max_split_depth = pic_data_->GetMaxBinarySplitDepth(cu_tree_);
  int max_split_size = pic_data_->GetMaxBinarySplitSize(cu_tree_);
  return GetBinaryDepth() < max_split_depth &&
    width_ <= max_split_size &&
    height_ <= max_split_size &&
    (width_ > constants::kMinBinarySplitSize ||
     height_ > constants::kMinBinarySplitSize);
}

const Qp& CodingUnit::GetQp() const {
  return *qp_;
}

void CodingUnit::SetQp(const Qp &qp) {
  qp_ = &qp;
}

void CodingUnit::SetQp(int qp_value) {
  qp_ = pic_data_->GetQp(qp_value);
}

int CodingUnit::GetQp(YuvComponent comp) const {
  return qp_->GetQpRaw(comp);
}

int CodingUnit::GetPredictedQp() const {
  const CodingUnit *tmp;
  int qp = pic_data_->GetPicQp()->GetQpRaw(YuvComponent::kY);
  if ((tmp = GetCodingUnitLeft()) != nullptr) {
    qp = tmp->GetQp().GetQpRaw(YuvComponent::kY);
  } else if ((tmp = GetCodingUnitAbove()) != nullptr) {
    qp = tmp->GetQp().GetQpRaw(YuvComponent::kY);
  }
  return qp;
}

SplitRestriction
CodingUnit::DeriveSiblingSplitRestriction(SplitType parent_split) const {
  if (pic_data_->GetPredictionType() == PicturePredictionType::kIntra) {
    return SplitRestriction::kNone;
  }
  if (parent_split == SplitType::kVertical &&
      split_state_ == SplitType::kHorizontal) {
    // This case is like quad split although with different coding order
    return width_ >= constants::kMinCuSize && GetBinaryDepth() == 1 ?
      SplitRestriction::kNoHorizontal : SplitRestriction::kNone;
  } else if (parent_split == SplitType::kHorizontal &&
             split_state_ == SplitType::kVertical) {
    // This case is almost identical to quad split
    return SplitRestriction::kNoVertical;
  }
  return SplitRestriction::kNone;
}

PicNum CodingUnit::GetRefPoc(RefPicList ref_list) const {
  if (!HasMv(ref_list)) {
    return static_cast<PicNum>(-1);
  }
  int ref_idx = GetRefIdx(ref_list);
  return pic_data_->GetRefPicLists()->GetRefPoc(ref_list, ref_idx);
}

bool CodingUnit::IsFullyWithinPicture() const {
  return pos_x_ + width_ <= pic_data_->GetPictureWidth(YuvComponent::kY) &&
    pos_y_ + height_ <= pic_data_->GetPictureHeight(YuvComponent::kY);
}

const CodingUnit* CodingUnit::GetCodingUnit(NeighborDir dir,
                                            MvCorner *mv_corner) const {
  const CodingUnit *cu = nullptr;
  int x = pos_x_;
  int y = pos_y_;
  switch (dir) {
    case NeighborDir::kAboveLeft:
      cu = GetCodingUnitAboveLeft();
      x = pos_x_ - constants::kMinBlockSize;
      y = pos_y_ - constants::kMinBlockSize;
      break;
    case NeighborDir::kAbove:
      cu = GetCodingUnitAbove();
      x = pos_x_;
      y = pos_y_ - constants::kMinBlockSize;
      break;
    case NeighborDir::kAboveCorner:
      cu = GetCodingUnitAboveCorner();
      x = pos_x_ + width_ - constants::kMinBlockSize;
      y = pos_y_ - constants::kMinBlockSize;
      break;
    case NeighborDir::kAboveRight:
      cu = GetCodingUnitAboveRight();
      x = pos_x_ + width_;
      y = pos_y_ - constants::kMinBlockSize;
      break;
    case NeighborDir::kLeft:
      cu = GetCodingUnitLeft();
      x = pos_x_ - constants::kMinBlockSize;
      y = pos_y_;
      break;
    case NeighborDir::kLeftCorner:
      cu = GetCodingUnitLeftCorner();
      x = pos_x_ - constants::kMinBlockSize;
      y = pos_y_ + height_ - constants::kMinBlockSize;
      break;
    case NeighborDir::kLeftBelow:
      cu = GetCodingUnitLeftBelow();
      x = pos_x_ - constants::kMinBlockSize;
      y = pos_y_ + height_;
      break;
  }
  if (cu) {
    *mv_corner = cu->GetMvCorner(x, y);
  }
  return cu;
}

const CodingUnit* CodingUnit::GetCodingUnitAbove() const {
  int posx = pos_x_;
  int posy = pos_y_;
  if (posy == 0) {
    return nullptr;
  }
  return pic_data_->GetCuAt(cu_tree_, posx, posy - constants::kMinBlockSize);
}

const CodingUnit* CodingUnit::GetCodingUnitAboveIfSameCtu() const {
  int posx = pos_x_;
  int posy = pos_y_;
  if ((posy % constants::kCtuSize) == 0) {
    return nullptr;
  }
  return pic_data_->GetCuAt(cu_tree_, posx, posy - constants::kMinBlockSize);
}

const CodingUnit* CodingUnit::GetCodingUnitAboveLeft() const {
  int posx = pos_x_;
  int posy = pos_y_;
  if (posx == 0 || posy == 0) {
    return nullptr;
  }
  return pic_data_->GetCuAt(cu_tree_, posx - constants::kMinBlockSize,
                            posy - constants::kMinBlockSize);
}

const CodingUnit* CodingUnit::GetCodingUnitAboveCorner() const {
  int right = pos_x_ + width_;
  int posy = pos_y_;
  if (posy == 0) {
    return nullptr;
  }
  return pic_data_->GetCuAt(cu_tree_, right - constants::kMinBlockSize,
                            posy - constants::kMinBlockSize);
}

const CodingUnit* CodingUnit::GetCodingUnitAboveRight() const {
  int right = pos_x_ + width_;
  int posy = pos_y_;
  if (posy == 0) {
    return nullptr;
  }
  // Padding in table will guard for y going out-of-bounds
  return pic_data_->GetCuAt(cu_tree_, right, posy - constants::kMinBlockSize);
}

const CodingUnit* CodingUnit::GetCodingUnitLeft() const {
  int posx = pos_x_;
  int posy = pos_y_;
  if (posx == 0) {
    return nullptr;
  }
  return pic_data_->GetCuAt(cu_tree_, posx - constants::kMinBlockSize, posy);
}

const CodingUnit* CodingUnit::GetCodingUnitLeftCorner() const {
  int posx = pos_x_;
  int bottom = pos_y_ + height_;
  if (posx == 0) {
    return nullptr;
  }
  return pic_data_->GetCuAt(cu_tree_, posx - constants::kMinBlockSize,
                            bottom - constants::kMinBlockSize);
}

const CodingUnit* CodingUnit::GetCodingUnitLeftBelow() const {
  int posx = pos_x_;
  int bottom = pos_y_ + height_;
  if (posx == 0) {
    return nullptr;
  }
  // Padding in table will guard for y going out-of-bounds
  return pic_data_->GetCuAt(cu_tree_, posx - constants::kMinBlockSize, bottom);
}

int CodingUnit::GetCuSizeAboveRight(YuvComponent comp) const {
  const int chroma_shift =
    std::max(pic_data_->GetChromaShiftX(), pic_data_->GetChromaShiftY());
  int posx = pos_x_ + width_;
  int posy = pos_y_ - constants::kMinBlockSize;
  if (posy < 0) {
    return 0;
  }
  posx -= constants::kMinBlockSize;
  for (int i = height_; i >= 0; i -= constants::kMinBlockSize) {
    if (pic_data_->GetCuAt(cu_tree_, posx + i, posy)) {
      return util::IsLuma(comp) ? i : (i >> chroma_shift);
    }
  }
  return 0;
}

int CodingUnit::GetCuSizeBelowLeft(YuvComponent comp) const {
  const int chroma_shift =
    std::max(pic_data_->GetChromaShiftX(), pic_data_->GetChromaShiftY());
  int posx = pos_x_ - constants::kMinBlockSize;
  int posy = pos_y_ + height_;
  if (posx < 0) {
    return 0;
  }
  posy -= constants::kMinBlockSize;
  for (int i = width_; i >= 0; i -= constants::kMinBlockSize) {
    if (pic_data_->GetCuAt(cu_tree_, posx, posy + i)) {
      return util::IsLuma(comp) ? i : (i >> chroma_shift);
    }
  }
  return 0;
}

void CodingUnit::ClearCbf(YuvComponent comp) {
  CoeffBuffer cu_coeff = GetCoeff(comp);
  const int width = GetWidth(comp);
  const int height = GetHeight(comp);
  tx_.cbf[static_cast<int>(comp)] = false;
  if (Restrictions::Get().disable_transform_cbf) {
    // when not signaling cbf this can only be false when root cbf is false
    tx_.cbf[static_cast<int>(comp)] = tx_.root_cbf;
  }
  tx_.transform_skip[static_cast<int>(comp)] = false;
  SetTransformFromSelectIdx(comp, -1);
  cu_coeff.ZeroOut(width, height);
}

void CodingUnit::SetTransformType(YuvComponent comp, TransformType tx1,
                                  TransformType tx2) {
  int plane = util::IsLuma(comp) ? 0 : 1;
  tx_.transform_type[plane][0] = tx1;
  tx_.transform_type[plane][1] = tx2;
}

void CodingUnit::SetTransformFromSelectIdx(YuvComponent comp, int select_idx) {
  static const std::array<std::array<TransformType, 2>, 3> kIntraTxMap = { {
    { TransformType::kDst7, TransformType::kDct8 },
    { TransformType::kDst7, TransformType::kDst1 },
    { TransformType::kDst7, TransformType::kDct5 },
  } };
  static const std::array<TransformType, 2> kInterTxMap = { {
    TransformType::kDct8, TransformType::kDst7
  } };
  static const std::array<int8_t, kNbrIntraModes> kIntraVerticalMap = { {
    2, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 2,
    2, 2, 2, 2, 1, 0, 1, 0, 1, 0
  } };
  static const std::array<int8_t, kNbrIntraModes> kIntraHorisontalMap = { {
    2, 1, 0, 1, 0, 1, 0, 1, 2, 2, 2, 2, 2, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0,
    0, 0, 0, 0, 1, 0, 1, 0, 1, 0
  } };
  static const std::array<int8_t, kNbrIntraModesExt> kIntraExtVerticalMap = {
    2, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0,
    1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0
  };
  static const std::array<int8_t, kNbrIntraModesExt> kIntraExtHorisontalMap = {
    2, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0,
    1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0
  };
  static const int kLuma = static_cast<int>(YuvComponent::kY);
  static const int kChroma = static_cast<int>(YuvComponent::kU);

  if (!util::IsLuma(comp)) {
    return;
  }
  assert(select_idx < constants::kMaxTransformSelectIdx);
  tx_.transform_select_idx = select_idx;
  if (Restrictions::Get().disable_ext2_transform_select) {
    tx_.transform_type[kLuma][0] = TransformType::kDefault;
    tx_.transform_type[kLuma][1] = TransformType::kDefault;
    tx_.transform_type[kChroma][0] = TransformType::kDefault;
    tx_.transform_type[kChroma][1] = TransformType::kDefault;
  } else if (select_idx < 0) {
    // No adaptive transform type used
    tx_.transform_type[kLuma][0] = TransformType::kDct2;
    tx_.transform_type[kLuma][1] = TransformType::kDct2;
    tx_.transform_type[kChroma][0] = TransformType::kDct2;
    tx_.transform_type[kChroma][1] = TransformType::kDct2;
  } else {
    if (IsIntra()) {
      int intra_mode = static_cast<int>(intra_.mode_luma);
      if (!Restrictions::Get().disable_ext2_intra_67_modes) {
        tx_.transform_type[kLuma][0] =
          kIntraTxMap[kIntraExtVerticalMap[intra_mode]][select_idx >> 1];
        tx_.transform_type[kLuma][1] =
          kIntraTxMap[kIntraExtHorisontalMap[intra_mode]][select_idx & 1];
      } else {
        tx_.transform_type[kLuma][0] =
          kIntraTxMap[kIntraVerticalMap[intra_mode]][select_idx >> 1];
        tx_.transform_type[kLuma][1] =
          kIntraTxMap[kIntraHorisontalMap[intra_mode]][select_idx & 1];
      }
    } else {
      tx_.transform_type[kLuma][0] = kInterTxMap[select_idx >> 1];
      tx_.transform_type[kLuma][1] = kInterTxMap[select_idx & 1];
    }
    tx_.transform_type[kChroma][0] = TransformType::kDct2;
    tx_.transform_type[kChroma][1] = TransformType::kDct2;
  }
}

IntraMode CodingUnit::GetIntraMode(YuvComponent comp) const {
  if (util::IsLuma(comp)) {
    assert(cu_tree_ == CuTree::Primary);
    return intra_.mode_luma;
  }
  if (intra_.mode_chroma == IntraChromaMode::kDmChroma) {
    if (cu_tree_ == CuTree::Primary) {
      return intra_.mode_luma;
    }
    const CodingUnit *luma_cu = pic_data_->GetLumaCu(this);
    return luma_cu->intra_.mode_luma;
  }
  assert(static_cast<int>(intra_.mode_chroma) <
         static_cast<int>(IntraChromaMode::kInvalid));
  return static_cast<IntraMode>(intra_.mode_chroma);
}

bool CodingUnit::HasZeroMvd() const {
  if (inter_.inter_dir == InterDir::kBi) {
    return inter_.mvd[0][0].IsZero() && inter_.mvd[1][0].IsZero();
  } else if (inter_.inter_dir == InterDir::kL0) {
    return inter_.mvd[0][0].IsZero();
  } else {
    return inter_.mvd[1][0].IsZero();
  }
}

bool CodingUnit::CanAffineMerge() const {
  if (width_ * height_ < 64) {
    return false;
  }
  const CodingUnit *tmp;
  return
    ((tmp = GetCodingUnitLeftCorner()) != nullptr && tmp->GetUseAffine()) ||
    ((tmp = GetCodingUnitAboveCorner()) != nullptr && tmp->GetUseAffine()) ||
    ((tmp = GetCodingUnitAboveRight()) != nullptr && tmp->GetUseAffine()) ||
    ((tmp = GetCodingUnitLeftBelow()) != nullptr && tmp->GetUseAffine()) ||
    ((tmp = GetCodingUnitAboveLeft()) != nullptr && tmp->GetUseAffine());
}

void CodingUnit::Split(SplitType split_type) {
  assert(split_type != SplitType::kNone);
  assert(split_state_ == SplitType::kNone);
  assert(!sub_cu_list_[0]);
  split_state_ = split_type;
  int sub_width = width_ >> 1;
  int sub_height = height_ >> 1;
  int quad_subdepth = depth_ + 1;
  switch (split_type) {
    case SplitType::kQuad:
      sub_cu_list_[0] =
        pic_data_->CreateCu(cu_tree_, quad_subdepth, pos_x_ + sub_width * 0,
                            pos_y_ + sub_height * 0, sub_width, sub_height);
      sub_cu_list_[1] =
        pic_data_->CreateCu(cu_tree_, quad_subdepth, pos_x_ + sub_width * 1,
                            pos_y_ + sub_height * 0, sub_width, sub_height);
      sub_cu_list_[2] =
        pic_data_->CreateCu(cu_tree_, quad_subdepth, pos_x_ + sub_width * 0,
                            pos_y_ + sub_height * 1, sub_width, sub_height);
      sub_cu_list_[3] =
        pic_data_->CreateCu(cu_tree_, quad_subdepth, pos_x_ + sub_width * 1,
                            pos_y_ + sub_height * 1, sub_width, sub_height);
      break;

    case SplitType::kHorizontal:
      sub_cu_list_[0] =
        pic_data_->CreateCu(cu_tree_, depth_, pos_x_,
                            pos_y_ + sub_height * 0, width_, sub_height);
      sub_cu_list_[1] =
        pic_data_->CreateCu(cu_tree_, depth_, pos_x_,
                            pos_y_ + sub_height * 1, width_, sub_height);
      sub_cu_list_[2] = nullptr;
      sub_cu_list_[3] = nullptr;
      break;

    case SplitType::kVertical:
      sub_cu_list_[0] =
        pic_data_->CreateCu(cu_tree_, depth_, pos_x_ + sub_width * 0,
                            pos_y_, sub_width, height_);
      sub_cu_list_[1] =
        pic_data_->CreateCu(cu_tree_, depth_, pos_x_ + sub_width * 1,
                            pos_y_, sub_width, height_);
      sub_cu_list_[2] = nullptr;
      sub_cu_list_[3] = nullptr;
      break;

    case SplitType::kNone:
    default:
      assert(0);
      break;
  }
}

void CodingUnit::UnSplit() {
  assert(split_state_ != SplitType::kNone);
  assert(sub_cu_list_[0]);
  for (int i = 0; i < static_cast<int>(sub_cu_list_.size()); i++) {
    if (sub_cu_list_[i]) {
      pic_data_->ReleaseCu(sub_cu_list_[i]);
    }
  }
  sub_cu_list_.fill(nullptr);
  split_state_ = SplitType::kNone;
}

void CodingUnit::SaveStateTo(ReconstructionState *dst_state,
                             const YuvPicture &rec_pic,
                             YuvComponent comp) const {
  const int c = static_cast<int>(comp);
  int posx = GetPosX(comp);
  int posy = GetPosY(comp);
  // Reco
  SampleBufferConst reco_src = rec_pic.GetSampleBuffer(comp, posx, posy);
  SampleBuffer reco_dst(&dst_state->reco[c][0], GetWidth(comp));
  reco_dst.CopyFrom(GetWidth(comp), GetHeight(comp), reco_src);
  // Coeff
  CoeffBufferConst coeff_src =
    ctu_coeff_->GetBuffer(comp, GetPosX(comp), GetPosY(comp));
  CoeffBuffer coeff_dst(&dst_state->coeff[c][0], GetWidth(comp));
  coeff_dst.CopyFrom(GetWidth(comp), GetHeight(comp), coeff_src);
}

void CodingUnit::SaveStateTo(ReconstructionState *dst_state,
                             const YuvPicture &rec_pic) const {
  for (YuvComponent comp : pic_data_->GetComponents(cu_tree_)) {
    SaveStateTo(dst_state, rec_pic, comp);
  }
}

void CodingUnit::SaveStateTo(ResidualState *dst_state,
                             const YuvPicture &rec_pic,
                             YuvComponent comp) const {
  const int plane = util::CompToPlane(comp);
  SaveStateTo(&dst_state->reco, rec_pic, comp);
  dst_state->tx.cbf[comp] = tx_.cbf[comp];
  dst_state->tx.transform_skip[comp] = tx_.transform_skip[comp];
  dst_state->tx.transform_type[plane] = tx_.transform_type[plane];
  if (util::IsLuma(comp)) {
    dst_state->tx.transform_select_idx = tx_.transform_select_idx;
  }
}

void CodingUnit::SaveStateTo(ResidualState *dst_state,
                             const YuvPicture &rec_pic) const {
  for (YuvComponent comp : pic_data_->GetComponents(cu_tree_)) {
    SaveStateTo(&dst_state->reco, rec_pic, comp);
  }
  dst_state->tx = tx_;
}

void CodingUnit::SaveStateTo(TransformState *state) const {
  *state = tx_;
}

void CodingUnit::SaveStateTo(InterState *state) const {
  *state = inter_;
}

void CodingUnit::LoadStateFrom(const ReconstructionState &src_state,
                               YuvPicture *rec_pic, YuvComponent comp) {
  const int c = static_cast<int>(comp);
  int posx = GetPosX(comp);
  int posy = GetPosY(comp);
  // Reco
  SampleBufferConst reco_src(&src_state.reco[c][0], GetWidth(comp));
  SampleBuffer reco_dst = rec_pic->GetSampleBuffer(comp, posx, posy);
  reco_dst.CopyFrom(GetWidth(comp), GetHeight(comp), reco_src);
  // Coeff
  CoeffBufferConst coeff_src(&src_state.coeff[c][0], GetWidth(comp));
  CoeffBuffer coeff_dst =
    ctu_coeff_->GetBuffer(comp, GetPosX(comp), GetPosY(comp));
  coeff_dst.CopyFrom(GetWidth(comp), GetHeight(comp), coeff_src);
}

void CodingUnit::LoadStateFrom(const ReconstructionState &src_state,
                               YuvPicture *rec_pic) {
  for (YuvComponent comp : pic_data_->GetComponents(cu_tree_)) {
    LoadStateFrom(src_state, rec_pic, comp);
  }
}

void CodingUnit::LoadStateFrom(const ResidualState &src_state,
                               YuvPicture *rec_pic, YuvComponent comp) {
  const int plane = util::CompToPlane(comp);
  LoadStateFrom(src_state.reco, rec_pic, comp);
  tx_.cbf[comp] = src_state.tx.cbf[comp];
  tx_.transform_skip[comp] = src_state.tx.transform_skip[comp];
  tx_.transform_type[plane] = src_state.tx.transform_type[plane];
  if (util::IsLuma(comp)) {
    tx_.transform_select_idx = src_state.tx.transform_select_idx;
  }
}

void CodingUnit::LoadStateFrom(const ResidualState &src_state,
                               YuvPicture *rec_pic) {
  for (YuvComponent comp : pic_data_->GetComponents(cu_tree_)) {
    LoadStateFrom(src_state.reco, rec_pic, comp);
  }
  tx_ = src_state.tx;
}

void CodingUnit::LoadStateFrom(const TransformState &state) {
  tx_ = state;
}

void CodingUnit::LoadStateFrom(const InterState &state) {
  inter_ = state;
}

void CodingUnit::LoadStateFrom(const InterState &state, RefPicList ref_list) {
  const int ref_list_idx = static_cast<int>(ref_list);
  inter_.mv[ref_list_idx] = state.mv[ref_list_idx];
  inter_.ref_idx[ref_list_idx] = state.ref_idx[ref_list_idx];
  inter_.mvd[ref_list_idx] = state.mvd[ref_list_idx];
  inter_.mvp_idx[ref_list_idx] = state.mvp_idx[ref_list_idx];
}

}   // namespace xvc
