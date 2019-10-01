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

#include "xvc_enc_lib/encoder_simd_functions.h"

#if defined(XVC_ARCH_ARM) || defined(XVC_ARCH_X86) || defined(XVC_ARCH_MIPS)
#include "xvc_enc_lib/simd/sample_metric_simd.h"
#endif

namespace xvc {

EncoderSimdFunctions::EncoderSimdFunctions(const std::set<CpuCapability> &caps,
                                           int internal_bitdepth)
  : SimdFunctions(caps),
  sample_metric() {
#if defined(XVC_ARCH_ARM) || defined(XVC_ARCH_X86) || defined(XVC_ARCH_MIPS)
  simd::SampleMetricSimd::Register(caps, internal_bitdepth, this);
#endif
}

}   // namespace xvc
