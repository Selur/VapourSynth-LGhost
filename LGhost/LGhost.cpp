// VapourSynth port by HolyWu
//
// LGhost.dll v0.3.01 Copyright(C) 2002, 2003 minamina
// Avisynth Plugin - Ghost Reduction (YUY2 and YV12 Only, Luminance Ghost Only)
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include <cstdlib>
#include <cstring>

#include <algorithm>
#include <string>

#include "LGhost.h"

using namespace std::literals;

#ifdef LGHOST_SIMD
template<typename pixel_t> extern void filter_sse2(const VSFrame * src, VSFrame * dst, const LGhostData * const VS_RESTRICT d, const VSAPI * vsapi, void * lineBuffer) noexcept;
#endif
#ifdef LGHOST_X86
template<typename pixel_t> extern void filter_avx2(const VSFrame * src, VSFrame * dst, const LGhostData * const VS_RESTRICT d, const VSAPI * vsapi, void * lineBuffer) noexcept;
template<typename pixel_t> extern void filter_avx512(const VSFrame * src, VSFrame * dst, const LGhostData * const VS_RESTRICT d, const VSAPI * vsapi, void * lineBuffer) noexcept;
#endif

template<typename pixel_t>
static void filter_c(const VSFrame * src, VSFrame * dst, const LGhostData * const VS_RESTRICT d, const VSAPI * vsapi, void * lineBuffer) noexcept {
    using var_t = std::conditional_t<std::is_integral_v<pixel_t>, int, float>;

    var_t * buffer = reinterpret_cast<var_t *>(lineBuffer);

    for (int plane = 0; plane < d->vi->format.numPlanes; plane++) {
        if (d->process[plane]) {
            const int width = vsapi->getFrameWidth(src, plane);
            const int height = vsapi->getFrameHeight(src, plane);
            const int stride = vsapi->getStride(src, plane) / sizeof(pixel_t);
            const pixel_t * srcp = reinterpret_cast<const pixel_t *>(vsapi->getReadPtr(src, plane));
            pixel_t * VS_RESTRICT dstp = reinterpret_cast<pixel_t *>(vsapi->getWritePtr(dst, plane));

            for (int y = 0; y < height; y++) {
                memset(buffer, 0, width * sizeof(var_t));

                for (auto && it : d->options[plane][0])
                    for (int x = it.startX; x < it.endX; x++)
                        buffer[x] += (srcp[x - it.shift + 1] - srcp[x - it.shift]) * it.intensity;

                for (auto && it : d->options[plane][1])
                    for (int x = it.startX; x < it.endX; x++)
                        buffer[x] += srcp[x - it.shift] * it.intensity;

                for (auto && it : d->options[plane][2])
                    for (int x = it.startX; x < it.endX; x++)
                        if (const var_t tempEdge = srcp[x - it.shift + 1] - srcp[x - it.shift]; tempEdge > 0)
                            buffer[x] += tempEdge * it.intensity;

                for (auto && it : d->options[plane][3])
                    for (int x = it.startX; x < it.endX; x++)
                        if (const var_t tempEdge = srcp[x - it.shift + 1] - srcp[x - it.shift]; tempEdge < 0)
                            buffer[x] += tempEdge * it.intensity;

                for (int x = 0; x < width; x++) {
                    if constexpr (std::is_integral_v<pixel_t>)
                        dstp[x] = std::clamp(srcp[x] + (buffer[x] >> 7), 0, d->peak);
                    else
                        dstp[x] = srcp[x] + (buffer[x] * (1.0f / 128.0f));
                }

                srcp += stride;
                dstp += stride;
            }
        }
    }
}

static const VSFrame * VS_CC lghostGetFrame(int n, int activationReason, void * instanceData, void ** frameData, VSFrameContext * frameCtx, VSCore * core, const VSAPI * vsapi) {
    LGhostData * d = static_cast<LGhostData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        // One line of int or float per call; the SIMD paths read up to 15 elements past the width.
        float * buffer = vsh::vsh_aligned_malloc<float>((d->vi->width + 15) * sizeof(float), 64);
        if (!buffer) {
            vsapi->setFilterError("LGhost: malloc failure (buffer)", frameCtx);
            return nullptr;
        }

        const VSFrame * src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSFrame * fr[] = { d->process[0] ? nullptr : src, d->process[1] ? nullptr : src, d->process[2] ? nullptr : src };
        const int pl[] = { 0, 1, 2 };
        VSFrame * dst = vsapi->newVideoFrame2(&d->vi->format, d->vi->width, d->vi->height, fr, pl, src, core);

        d->filter(src, dst, d, vsapi, buffer);

        vsh::vsh_aligned_free(buffer);
        vsapi->freeFrame(src);
        return dst;
    }

    return nullptr;
}

static void VS_CC lghostFree(void * instanceData, VSCore * core, const VSAPI * vsapi) {
    LGhostData * d = static_cast<LGhostData *>(instanceData);
    vsapi->freeNode(d->node);
    delete d;
}

template<typename pixel_t>
static void selectFilter(LGhostData * d, const int opt) {
    d->filter = filter_c<pixel_t>;
#ifdef LGHOST_X86
    const int iset = instrset_detect();
    if ((opt == 0 && iset >= 10) || opt == 4)
        d->filter = filter_avx512<pixel_t>;
    else if ((opt == 0 && iset >= 8) || opt == 3)
        d->filter = filter_avx2<pixel_t>;
    else if ((opt == 0 && iset >= 2) || opt == 2)
        d->filter = filter_sse2<pixel_t>;
#elif defined(LGHOST_SIMD)
    // ARM64: NEON through sse2neon, there are no AVX2/AVX-512 paths.
    if (opt == 0 || opt == 2)
        d->filter = filter_sse2<pixel_t>;
#endif
}

static void VS_CC lghostCreate(const VSMap * in, VSMap * out, void * userData, VSCore * core, const VSAPI * vsapi) {
    std::unique_ptr<LGhostData> d = std::make_unique<LGhostData>();

    try {
        d->node = vsapi->mapGetNode(in, "clip", 0, nullptr);
        d->vi = vsapi->getVideoInfo(d->node);
        int err;

        if (!vsh::isConstantVideoFormat(d->vi) ||
            (d->vi->format.sampleType == stInteger && d->vi->format.bitsPerSample > 16) ||
            (d->vi->format.sampleType == stFloat && d->vi->format.bitsPerSample != 32))
            throw "only constant format 8-16 bit integer and 32 bit float input supported";

        const int64_t * modeArray = vsapi->mapGetIntArray(in, "mode", nullptr);
        const int64_t * shiftArray = vsapi->mapGetIntArray(in, "shift", nullptr);
        const int64_t * intensityArray = vsapi->mapGetIntArray(in, "intensity", nullptr);

        const int numMode = vsapi->mapNumElements(in, "mode");
        const int numShift = vsapi->mapNumElements(in, "shift");
        const int numIntensity = vsapi->mapNumElements(in, "intensity");

        {
            const int m = vsapi->mapNumElements(in, "planes");

            if (m <= 0) {
                for (int i = 0; i < 3; i++) {
                    d->process[i] = true;
                    if (i == 0 && d->vi->format.colorFamily != cfRGB)
                        break;
                }
            }

            for (int i = 0; i < m; i++) {
                const int n = vsh::int64ToIntS(vsapi->mapGetInt(in, "planes", i, nullptr));

                if (n < 0 || n >= d->vi->format.numPlanes)
                    throw "plane index out of range";

                if (d->process[n])
                    throw "plane specified twice";

                d->process[n] = true;
            }
        }

        const int opt = vsh::int64ToIntS(vsapi->mapGetInt(in, "opt", 0, &err));

        if (numMode != numShift || numMode != numIntensity)
            throw "number of elements in mode, shift and intensity must equal";

        for (int i = 0; i < numMode; i++) {
            const int mode = vsh::int64ToIntS(modeArray[i]);
            const int intensity = vsh::int64ToIntS(intensityArray[i]);

            if (mode < 1 || mode > 4)
                throw "mode must be 1, 2, 3, or 4";

            if (intensity == 0 || intensity < -128 || intensity > 127)
                throw "intensity must not be 0 and must be between -128 and 127 (inclusive)";

            for (int plane = 0; plane < d->vi->format.numPlanes; plane++) {
                if (d->process[plane]) {
                    int shift = vsh::int64ToIntS(shiftArray[i]);
                    int width = d->vi->width >> (plane ? d->vi->format.subSamplingW : 0);

                    if (std::abs(shift) >= width)
                        throw "abs(shift) must be less than plane's width";

                    if (mode != 2) {
                        if (shift == 0)
                            throw "shift must not be 0 for mode 1, 3, 4";

                        if (shift < 0) {
                            shift++;
                            width--;
                        }
                    }

                    d->options[plane][mode - 1].emplace_back(OptionData{ shift, intensity, std::max(shift, 0), std::min(width + shift, width) });
                }
            }
        }

        if (opt < 0 || opt > 4)
            throw "opt must be 0, 1, 2, 3, or 4";

        if (d->vi->format.bytesPerSample == 1)
            selectFilter<uint8_t>(d.get(), opt);
        else if (d->vi->format.bytesPerSample == 2)
            selectFilter<uint16_t>(d.get(), opt);
        else
            selectFilter<float>(d.get(), opt);

        if (d->vi->format.sampleType == stInteger)
            d->peak = (1 << d->vi->format.bitsPerSample) - 1;
    } catch (const char * error) {
        vsapi->mapSetError(out, ("LGhost: "s + error).c_str());
        vsapi->freeNode(d->node);
        return;
    }

    VSFilterDependency deps[] = { { d->node, rpStrictSpatial } };
    LGhostData * data = d.release();
    vsapi->createVideoFilter(out, "LGhost", data->vi, lghostGetFrame, lghostFree, fmParallel, deps, 1, data, core);
}

//////////////////////////////////////////
// Init

VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin * plugin, const VSPLUGINAPI * vspapi) {
    vspapi->configPlugin("com.holywu.lghost", "lghost", "Ghost Reduction", VS_MAKE_VERSION(1, 1), VAPOURSYNTH_API_VERSION, 0, plugin);
    vspapi->registerFunction("LGhost",
                             "clip:vnode;"
                             "mode:int[];"
                             "shift:int[];"
                             "intensity:int[];"
                             "planes:int[]:opt;"
                             "opt:int:opt;",
                             "clip:vnode;",
                             lghostCreate, nullptr, plugin);
}
