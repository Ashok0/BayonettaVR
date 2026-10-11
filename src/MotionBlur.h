#pragma once
#include "DepthOfField.h"
namespace bvr {
// Steam shader.dat: modelshaderblur, blurcm4, blurcm8, blurcme, blurcmx/y.
// These reproject scene color using g_InvOldWVP/g_InvNewWVP in the pixel
// shader, independently of the HMD-adjusted vertex camera. cmx/y are identical.
// Do not match menu blur, bloom, DOF, or the motion-vector generation shaders.
inline bool IsMotionBlurShader(uint32_t crc,size_t size) {
    return (crc==0xcde775fa&&size==1288)||(crc==0x27b1ec05&&size==1492)||
           (crc==0x7c993bc7&&size==1740)||(crc==0x9fcfac0f&&size==1744)||
           (crc==0x24ea099a&&size==1832);
}
}
