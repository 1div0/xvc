/******************************************************************************
* Copyright (C) 2017, Divideon.
*
* Redistribution and use in source and binary form, with or without
* modifications is permitted only under the terms and conditions set forward
* in the xvc License Agreement. For commercial redistribution and use, you are
* required to send a signed copy of the xvc License Agreement to Divideon.
*
* Redistribution and use in source and binary form is permitted free of charge
* for non-commercial purposes. See definition of non-commercial in the xvc
* License Agreement.
*
* All redistribution of source code must retain this copyright notice
* unmodified.
*
* The xvc License Agreement is available at https://xvc.io/license/.
******************************************************************************/

#ifndef XVC_COMMON_LIB_CU_TYPES_H_
#define XVC_COMMON_LIB_CU_TYPES_H_

namespace xvc {

enum class SplitType {
  kNone,
  kQuad,
  kHorizontal,
  kVertical,
};

enum class SplitRestriction {
  kNone,
  kNoHorizontal,
  kNoVertical,
};

enum class PredictionMode {
  kIntra = 0,
  kInter = 1,
};

enum class PartitionType {
  kSize2Nx2N,
  kSize2NxN,
  kSizeNx2N,
  kSizeNxN,
  kSize2NxnU,
  kSize2NxnD,
  kSizenLx2N,
  kSizenRx2N,
  kSizeNone = 15,
};

enum class TransformType {
  kDefault,
  kDCT2,
  kDCT5,
  kDCT8,
  kDST1,
  kDST7,
  kTotalNumber,
};
static const int kNbrTransformTypes =
static_cast<int>(TransformType::kTotalNumber);

enum IntraMode {
  kLmChroma = -2,
  kInvalid = -1,
  kPlanar = 0,
  kDc = 1,
};
static const int kNbrIntraModes = 35;
static const int kNbrIntraModesExt = 67;

enum class IntraAngle {
  kPlanar = 0,
  kDc = 1,
  kFirst = 2,
  kHorizontal = 10,
  kDiagonal = 18,
  kVertical = 26,
};
static const int kNbrIntraDirs = kNbrIntraModes;

// chroma modes maps directly to intra modes except for kDmChroma
enum class IntraChromaMode {
  kLmChroma = -2,
  kDmChroma = -1,
  kPlanar = 0,
  kDc = 1,
  kInvalid = 99,
};
const int kMaxNumIntraChromaModes = 6;

enum class IntraChromaAngle {
  kDmChroma = -1,
  kPlanar = 0,
  kDc = 1,
  kHorizontal = 10,
  kVertical = 26,
  kVerticalPlus8 = 34,
  kInvalid = 99,
};

enum class InterDir {
  kL0 = 0,
  kL1 = 1,
  kBi = 2,
};

struct MotionVector {
  MotionVector() = default;
  MotionVector(int mv_x, int mv_y) : x(mv_x), y(mv_y) {}
  bool operator==(const MotionVector &other) const {
    return x == other.x && y == other.y;
  }
  bool operator!=(const MotionVector &other) const {
    return !(*this == other);
  }
  int x = 0;
  int y = 0;
};

}   // namespace xvc

#endif  // XVC_COMMON_LIB_CU_TYPES_H_
