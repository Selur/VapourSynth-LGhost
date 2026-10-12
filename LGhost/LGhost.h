#pragma once

#include <memory>
#include <type_traits>
#include <vector>

#include <VapourSynth4.h>
#include <VSHelper4.h>

// The SSE2 path runs on x86 natively and on ARM64 through sse2neon (VCL2/instrset.h).
#if defined(LGHOST_X86) || defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
#define LGHOST_SIMD
#include "VCL2/vectorclass.h"
#endif

struct OptionData final {
    int shift, intensity, startX, endX;
};

struct LGhostData final {
    VSNode * node;
    const VSVideoInfo * vi;
    bool process[3];
    std::vector<OptionData> options[3][4];
    int peak;
    void (*filter)(const VSFrame * src, VSFrame * dst, const LGhostData * const VS_RESTRICT d, const VSAPI * vsapi, void * lineBuffer) noexcept;
};
