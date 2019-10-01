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

#ifndef XVC_COMMON_LIB_REFERENCE_PICTURE_LISTS_H_
#define XVC_COMMON_LIB_REFERENCE_PICTURE_LISTS_H_

#include <memory>
#include <vector>

#include "xvc_common_lib/cu_types.h"
#include "xvc_common_lib/quantize.h"
#include "xvc_common_lib/yuv_pic.h"

namespace xvc {

enum class RefPicList {
  kL0 = 0,
  kL1 = 1,
  kTotalNumber = 2
};

class CodingUnit;
class PictureData;

class ReferencePictureLists {
public:
  static bool IsRefPicListUsed(RefPicList ref_pic_list, InterDir inter_dir);
  static RefPicList Inverse(RefPicList ref_list) {
    return ref_list == RefPicList::kL0 ? RefPicList::kL1 : RefPicList::kL0;
  }

  int GetNumRefPics(RefPicList list) const {
    return (list == RefPicList::kL0) ?
      static_cast<int>(l0_.size()) : static_cast<int>(l1_.size());
  }
  const YuvPicture* GetRefPic(RefPicList ref_list, int ref_idx) const {
    return ref_list == RefPicList::kL0 ?
      l0_[ref_idx].ref_pic.get() : l1_[ref_idx].ref_pic.get();
  }
  const YuvPicture* GetRefOrigPic(RefPicList ref_list, int ref_idx) const {
    return ref_list == RefPicList::kL0 ?
      l0_[ref_idx].orig_pic.get() : l1_[ref_idx].orig_pic.get();
  }
  PicNum GetRefPoc(RefPicList ref_list, int ref_idx) const {
    return (ref_list == RefPicList::kL0) ? l0_[ref_idx].poc : l1_[ref_idx].poc;
  }
  bool HasRefPoc(RefPicList ref_list, PicNum poc) const;
  bool HasOnlyBackReferences() const { return only_back_references_; }
  PicturePredictionType GetRefPicType(RefPicList ref_list, int ref_idx) const;
  int GetRefPicTid(RefPicList ref_list, int ref_idx) const;
  const CodingUnit* GetCodingUnitAt(RefPicList ref_list, int ref_idx,
                                    CuTree cu_tree, int posx, int posy) const;
  void SetRefPic(RefPicList ref_list, int index, PicNum ref_poc,
                 const std::shared_ptr<const PictureData> &pic_data,
                 const std::shared_ptr<const YuvPicture> &ref_pic,
                 const std::shared_ptr<const YuvPicture> &orig_pic);
  std::vector<int> GetSamePocMappingFor(RefPicList ref_list) const;
  void ZeroOutReferences();
  void Reset(PicNum current_poc);

private:
  struct RefEntry {
    PicNum poc;
    std::shared_ptr<const YuvPicture> ref_pic;
    std::shared_ptr<const YuvPicture> orig_pic;
    std::shared_ptr<const PictureData> data;
  };
  std::vector<RefEntry> l0_;
  std::vector<RefEntry> l1_;
  PicNum current_poc_ = static_cast<PicNum>(-1);
  bool only_back_references_ = true;
};

}   // namespace xvc

#endif  // XVC_COMMON_LIB_REFERENCE_PICTURE_LISTS_H_
