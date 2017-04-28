/******************************************************************************
* Copyright (C) 2017, Divideon. All rights reserved.
* No part of this code may be reproduced in any form
* without the written permission of the copyright holder.
******************************************************************************/

#include "xvc_enc_lib/cu_encoder.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <limits>
#include <utility>

#include "xvc_common_lib/restrictions.h"
#include "xvc_common_lib/utils.h"
#include "xvc_enc_lib/sample_metric.h"

namespace xvc {

struct CuEncoder::RdoCost {
  RdoCost() = default;
  explicit RdoCost(Cost c) : cost(c), dist(0) {}
  RdoCost(Cost c, Distortion d) : cost(c), dist(d) {}
  bool operator<(const RdoCost &other) { return cost < other.cost; }
  Cost cost;
  Distortion dist;
};

CuEncoder::CuEncoder(const YuvPicture &orig_pic,
                     YuvPicture *rec_pic, PictureData *pic_data,
                     const EncoderSettings &encoder_settings)
  : TransformEncoder(rec_pic->GetBitdepth(), pic_data->GetMaxNumComponents(),
                     orig_pic),
  orig_pic_(orig_pic),
  encoder_settings_(encoder_settings),
  rec_pic_(*rec_pic),
  pic_data_(*pic_data),
  inter_search_(rec_pic->GetBitdepth(), pic_data->GetMaxNumComponents(),
                orig_pic, *pic_data->GetRefPicLists(), encoder_settings),
  intra_search_(rec_pic->GetBitdepth(), *pic_data, orig_pic, encoder_settings),
  cu_writer_(pic_data_, &intra_search_) {
  for (int tree_idx = 0; tree_idx < constants::kMaxNumCuTrees; tree_idx++) {
    const CuTree cu_tree = static_cast<CuTree>(tree_idx);
    const int max_depth = static_cast<int>(rdo_temp_cu_[tree_idx].size());
    for (int depth = 0; depth < max_depth; depth++) {
      rdo_temp_cu_[tree_idx][depth] =
        pic_data_.CreateCu(cu_tree, depth, -1, -1, 0, 0);
      cu_cache_[tree_idx][depth].first = false;
      cu_cache_[tree_idx][depth].second =
        pic_data_.CreateCu(cu_tree, depth, -1, -1, 0, 0);
    }
  }
}

CuEncoder::~CuEncoder() {
  for (int tree_idx = 0; tree_idx < constants::kMaxNumCuTrees; tree_idx++) {
    const int max_depth = static_cast<int>(rdo_temp_cu_[tree_idx].size());
    for (int depth = 0; depth < max_depth; depth++) {
      pic_data_.ReleaseCu(rdo_temp_cu_[tree_idx][depth]);
      pic_data_.ReleaseCu(cu_cache_[tree_idx][depth].second);
    }
  }
}

void CuEncoder::EncodeCtu(int rsaddr, SyntaxWriter *bitstream_writer) {
  RdoSyntaxWriter rdo_writer(*bitstream_writer);
  CodingUnit *ctu = pic_data_.GetCtu(CuTree::Primary, rsaddr);
  CompressCu(&ctu, 0, SplitRestriction::kNone, &rdo_writer);
  pic_data_.SetCtu(CuTree::Primary, rsaddr, ctu);
  if (pic_data_.HasSecondaryCuTree()) {
    RdoSyntaxWriter rdo_writer2(*bitstream_writer);
    CodingUnit *ctu2 = pic_data_.GetCtu(CuTree::Secondary, rsaddr);
    CompressCu(&ctu2, 0, SplitRestriction::kNone, &rdo_writer2);
    pic_data_.SetCtu(CuTree::Secondary, rsaddr, ctu2);
  }

  WriteCtu(rsaddr, bitstream_writer);
}

Distortion CuEncoder::CompressCu(CodingUnit **best_cu, int rdo_depth,
                                 SplitRestriction split_restiction,
                                 RdoSyntaxWriter *writer) {
  const QP &qp = *pic_data_.GetPicQp();
  const int kMaxTrSize =
    !Restrictions::Get().disable_ext_transform_size_64 ? 64 : 32;
  CodingUnit *cu = *best_cu;  // Invariant: cu always points to *best_cu
  const int cu_tree = static_cast<int>(cu->GetCuTree());
  const int depth = cu->GetDepth();
  const bool do_quad_split = cu->GetBinaryDepth() == 0 &&
    depth < pic_data_.GetMaxDepth(cu->GetCuTree());
  const bool can_binary_split = !Restrictions::Get().disable_ext &&
    cu->IsBinarySplitValid() && cu->IsFullyWithinPicture() &&
    cu->GetWidth(YuvComponent::kY) <= kMaxTrSize &&
    cu->GetHeight(YuvComponent::kY) <= kMaxTrSize;
  const bool do_hor_split = can_binary_split &&
    split_restiction != SplitRestriction::kNoHorizontal &&
    cu->GetHeight(YuvComponent::kY) > constants::kMinBinarySplitSize;
  const bool do_ver_split = can_binary_split &&
    split_restiction != SplitRestriction::kNoVertical &&
    cu->GetWidth(YuvComponent::kY) > constants::kMinBinarySplitSize;
  const bool do_full = cu->IsFullyWithinPicture() &&
    cu->GetWidth(YuvComponent::kY) <= kMaxTrSize &&
    cu->GetHeight(YuvComponent::kY) <= kMaxTrSize;
  const bool do_split_any = do_quad_split || do_hor_split || do_ver_split;
  assert(do_full || do_split_any);

  if (!do_split_any) {
    return CompressNoSplit(best_cu, rdo_depth, split_restiction, writer);
  }
  RdoCost best_cost(std::numeric_limits<Cost>::max());
  CodingUnit::ReconstructionState *best_state = &temp_cu_state_[rdo_depth];
  RdoSyntaxWriter best_writer(*writer);
  CodingUnit **temp_cu = &rdo_temp_cu_[cu_tree][rdo_depth];
  (*temp_cu)->InitializeFrom(*cu);

  // Clear cache for CU re-use
  if (cu->GetBinaryDepth() == 0) {
    cu_cache_[cu_tree][cu->GetDepth() + 1].first = false;
  }

  // First eval without CU split
  if (do_full) {
    Bits start_bits = writer->GetNumWrittenBits();
    best_cost.dist =
      CompressNoSplit(best_cu, rdo_depth, split_restiction, &best_writer);
    cu = *best_cu;
    Bits full_bits = best_writer.GetNumWrittenBits() - start_bits;
    best_cost.cost =
      best_cost.dist + static_cast<Cost>(full_bits * qp.GetLambda() + 0.5);
    cu->SaveStateTo(best_state, rec_pic_);
  }

  // Horizontal split
  if (do_hor_split) {
    RdoSyntaxWriter splitcu_writer(*writer);
    RdoCost split_cost =
      CompressSplitCu(*temp_cu, rdo_depth, qp, SplitType::kHorizontal,
                      split_restiction, &splitcu_writer);
    if (split_cost.cost < best_cost.cost) {
      std::swap(*best_cu, *temp_cu);
      cu = *best_cu;
      if (!do_quad_split && !do_ver_split) {
        // No more split evaluations
        *writer = splitcu_writer;
        return split_cost.dist;
      }
      best_cost = split_cost;
      best_writer = splitcu_writer;
      cu->SaveStateTo(best_state, rec_pic_);
    } else {
      // Restore (previous) best state
      cu->LoadStateFrom(*best_state, &rec_pic_);
      pic_data_.MarkUsedInPic(cu);
    }
  }

  // Vertical split
  if (do_ver_split) {
    RdoSyntaxWriter splitcu_writer(*writer);
    RdoCost split_cost =
      CompressSplitCu(*temp_cu, rdo_depth, qp, SplitType::kVertical,
                      split_restiction, &splitcu_writer);
    if (split_cost.cost < best_cost.cost) {
      std::swap(*best_cu, *temp_cu);
      cu = *best_cu;
      if (!do_quad_split) {
        // No more split evaluations
        *writer = splitcu_writer;
        return split_cost.dist;
      }
      best_cost = split_cost;
      best_writer = splitcu_writer;
      cu->SaveStateTo(best_state, rec_pic_);
    } else {
      // Restore (previous) best state
      cu->LoadStateFrom(*best_state, &rec_pic_);
      pic_data_.MarkUsedInPic(cu);
    }
  }

  // Quad split
  if (do_quad_split) {
    RdoSyntaxWriter splitcu_writer(*writer);
    RdoCost split_cost =
      CompressSplitCu(*temp_cu, rdo_depth, qp, SplitType::kQuad,
                      split_restiction, &splitcu_writer);
    if (split_cost.cost < best_cost.cost) {
      std::swap(*best_cu, *temp_cu);
      cu = *best_cu;
      // No more split evaluations
      *writer = splitcu_writer;
      return split_cost.dist;
    } else {
      // Restore (previous) best state
      cu->LoadStateFrom(*best_state, &rec_pic_);
      pic_data_.MarkUsedInPic(cu);
    }
  }

  *writer = best_writer;
  return best_cost.dist;
}

CuEncoder::RdoCost
CuEncoder::CompressSplitCu(CodingUnit *cu, int rdo_depth, const QP &qp,
                           SplitType split_type,
                           SplitRestriction split_restriction,
                           RdoSyntaxWriter *rdo_writer) {
  if (cu->GetSplit() != SplitType::kNone) {
    cu->UnSplit();
  }
  cu->Split(split_type);
  pic_data_.ClearMarkCuInPic(cu);
  Distortion dist = 0;
  Bits start_bits = rdo_writer->GetNumWrittenBits();
  SplitRestriction sub_split_restriction = SplitRestriction::kNone;
  for (auto &sub_cu : cu->GetSubCu()) {
    if (sub_cu) {
      dist +=
        CompressCu(&sub_cu, rdo_depth + 1, sub_split_restriction, rdo_writer);
      sub_split_restriction = sub_cu->DeriveSiblingSplitRestriction(split_type);
    }
  }
  cu_writer_.WriteSplit(*cu, split_restriction, rdo_writer);
  Bits bits = rdo_writer->GetNumWrittenBits() - start_bits;
  Cost cost = dist + static_cast<Cost>(bits * qp.GetLambda() + 0.5);
  return RdoCost(cost, dist);
}

Distortion CuEncoder::CompressNoSplit(CodingUnit **best_cu, int rdo_depth,
                                      SplitRestriction split_restriction,
                                      RdoSyntaxWriter *writer) {
  const QP &qp = *pic_data_.GetPicQp();
  CodingUnit::ReconstructionState *best_state =
    &temp_cu_state_[rdo_depth + 1];
  CodingUnit *cu = *best_cu;
  if (cu->GetSplit() != SplitType::kNone) {
    cu->UnSplit();
  }
  cu->SetQp(qp);
  const int cu_tree = static_cast<int>(cu->GetCuTree());
  const int cache_depth = util::SizeToLog2(constants::kCtuSize) -
    util::SizeToLog2(cu->GetWidth(YuvComponent::kY));
  RdoCost best_cost(std::numeric_limits<Cost>::max());

  if (encoder_settings_.skip_mode_decision_for_identical_cu &&
      cu_cache_[cu_tree][cache_depth].first &&
      *cu_cache_[cu_tree][cache_depth].second == *cu) {
    // Use cached CU
    *cu = *cu_cache_[cu_tree][cache_depth].second;
    best_cost.cost = 0;
    best_cost.dist = CompressFast(cu, qp, *writer);
  } else if (pic_data_.IsIntraPic()) {
    // Intra pic
    best_cost = CompressIntra(cu, qp, *writer);
  } else {
    // Inter pic
    CodingUnit *temp_cu = rdo_temp_cu_[cu_tree][rdo_depth + 1];
    temp_cu->InitializeFrom(*cu);
    if (temp_cu->GetSplit() != SplitType::kNone) {
      temp_cu->UnSplit();
    }

    RdoCost cost;
    if (!Restrictions::Get().disable_inter_merge_mode) {
      cost = CompressMerge(temp_cu, qp, *writer);
      if (cost < best_cost) {
        best_cost = cost;
        temp_cu->SaveStateTo(best_state, rec_pic_);
        std::swap(cu, temp_cu);
      }
    }

    cost = CompressInter(temp_cu, qp, *writer);
    if (cost < best_cost) {
      best_cost = cost;
      temp_cu->SaveStateTo(best_state, rec_pic_);
      std::swap(cu, temp_cu);
    }

    if (encoder_settings_.always_evaluate_intra_in_inter
        || cu->GetHasAnyCbf()) {
      cost = CompressIntra(temp_cu, qp, *writer);
      if (cost < best_cost) {
        best_cost = cost;
        temp_cu->SaveStateTo(best_state, rec_pic_);
        std::swap(cu, temp_cu);
      }
    }

    *best_cu = cu;
    rdo_temp_cu_[cu_tree][rdo_depth + 1] = temp_cu;
    cu->LoadStateFrom(*best_state, &rec_pic_);
  }
  cu->SetRootCbf(cu->GetHasAnyCbf());
  pic_data_.MarkUsedInPic(cu);

  // Cache prediction data for this CU
  bool cache_candidate = cu->GetBinaryDepth() == 2 &&
    cu->GetWidth(YuvComponent::kY) == cu->GetHeight(YuvComponent::kY);
  if (cache_candidate) {
    int parent_mask = (cu->GetWidth(YuvComponent::kY) << 1) - 1;
    if (((cu->GetPosX(YuvComponent::kY) & parent_mask) == 0) &&
      ((cu->GetPosY(YuvComponent::kY) & parent_mask) == 0)) {
      cu_cache_[cu_tree][cache_depth].first = true;
      *cu_cache_[cu_tree][cache_depth].second = *cu;
    }
  }

  for (YuvComponent comp : pic_data_.GetComponents(cu->GetCuTree())) {
    cu_writer_.WriteComponent(*cu, comp, writer);
  }
  cu_writer_.WriteSplit(*cu, split_restriction, writer);
  return best_cost.dist;
}

Distortion CuEncoder::CompressFast(CodingUnit *cu, const QP &qp,
                                   const SyntaxWriter &writer) {
  assert(cu->GetSplit() == SplitType::kNone);
  Distortion dist = 0;
  if (cu->IsIntra()) {
    for (YuvComponent comp : pic_data_.GetComponents(cu->GetCuTree())) {
      // TODO(PH) Add fast method without cbf evaluation
      dist += intra_search_.CompressIntra(cu, comp, qp, this, &rec_pic_);
    }
  } else {
    for (YuvComponent comp : pic_data_.GetComponents(cu->GetCuTree())) {
      dist += inter_search_.CompressInterFast(cu, comp, qp, this, &rec_pic_);
    }
  }
  return dist;
}

CuEncoder::RdoCost
CuEncoder::CompressIntra(CodingUnit *cu, const QP &qp,
                         const SyntaxWriter &writer) {
  cu->SetPredMode(PredictionMode::kIntra);
  cu->SetSkipFlag(false);
  Distortion dist = 0;
  for (YuvComponent comp : pic_data_.GetComponents(cu->GetCuTree())) {
    if (util::IsLuma(comp)) {
      IntraMode best_mode =
        intra_search_.SearchIntraLuma(cu, comp, qp, writer, this, &rec_pic_);
      cu->SetIntraModeLuma(best_mode);
    } else if (util::IsFirstChroma(comp)) {
      IntraChromaMode chroma_mode =
        intra_search_.SearchIntraChroma(cu, qp, writer, this, &rec_pic_);
      cu->SetIntraModeChroma(chroma_mode);
    }
    dist += intra_search_.CompressIntra(cu, comp, qp, this, &rec_pic_);
  }
  return GetCuCostWithoutSplit(*cu, qp, writer, dist);
}

CuEncoder::RdoCost
CuEncoder::CompressInter(CodingUnit *cu, const QP &qp,
                         const SyntaxWriter &bitstream_writer) {
  Distortion dist =
    inter_search_.CompressInter(cu, qp, bitstream_writer, this, &rec_pic_);
  return GetCuCostWithoutSplit(*cu, qp, bitstream_writer, dist);
}

CuEncoder::RdoCost
CuEncoder::CompressMerge(CodingUnit *cu, const QP &qp,
                         const SyntaxWriter &bitstream_writer) {
  std::array<bool,
    constants::kNumInterMergeCandidates> skip_evaluated = { false };
  InterMergeCandidateList merge_list = inter_search_.GetMergeCandidates(*cu);
  RdoCost best_cost(std::numeric_limits<Cost>::max());
  CodingUnit::TransformState &best_transform_state = rd_transform_state_;
  int best_merge_idx = -1;

  int num_merge_cand = Restrictions::Get().disable_inter_merge_candidates ?
    1 : static_cast<int>(merge_list.size());
  for (int skip_eval_idx = 0; skip_eval_idx < 2; skip_eval_idx++) {
    bool force_skip = skip_eval_idx != 0;
    for (int merge_idx = 0; merge_idx < num_merge_cand; merge_idx++) {
      if (skip_evaluated[merge_idx]) {
        continue;
      }
      Distortion dist =
        inter_search_.CompressMergeCand(cu, qp, bitstream_writer, merge_list,
                                        merge_idx, force_skip, this, &rec_pic_);
      RdoCost cost = GetCuCostWithoutSplit(*cu, qp, bitstream_writer, dist);
      if (!cu->GetHasAnyCbf()) {
        skip_evaluated[merge_idx] = true;
      }
      if (cost.cost < best_cost.cost) {
        best_cost = cost;
        best_merge_idx = merge_idx;
        cu->SaveStateTo(&best_transform_state, rec_pic_);
        if (!cu->GetHasAnyCbf() && !force_skip) {
          // Encoder optimization, assume skip is always best
          break;
        }
      }
    }
  }
  cu->SetMergeIdx(best_merge_idx);
  inter_search_.ApplyMerge(cu, merge_list, best_merge_idx);
  cu->LoadStateFrom(best_transform_state, &rec_pic_);
  cu->SetSkipFlag(!cu->GetRootCbf());
  return best_cost;
}

CuEncoder::RdoCost
CuEncoder::GetCuCostWithoutSplit(const CodingUnit &cu, const QP &qp,
                                 const SyntaxWriter &bitstream_writer,
                                 Distortion ssd) {
  RdoSyntaxWriter rdo_writer(bitstream_writer, 0);
  for (YuvComponent comp : pic_data_.GetComponents(cu.GetCuTree())) {
    cu_writer_.WriteComponent(cu, comp, &rdo_writer);
  }
  Bits bits = rdo_writer.GetNumWrittenBits();
  Cost cost = ssd + static_cast<Cost>(bits * qp.GetLambda() + 0.5);
  return RdoCost(cost, ssd);
}

void CuEncoder::WriteCtu(int rsaddr, SyntaxWriter *writer) {
  writer->ResetBitCounting();
  CodingUnit *ctu = pic_data_.GetCtu(CuTree::Primary, rsaddr);
  cu_writer_.WriteCtu(*ctu, writer);
  if (pic_data_.HasSecondaryCuTree()) {
    CodingUnit *ctu2 = pic_data_.GetCtu(CuTree::Secondary, rsaddr);
    cu_writer_.WriteCtu(*ctu2, writer);
  }
#if HM_STRICT
  writer->WriteEndOfSlice(false);
#endif
}

}   // namespace xvc
