#include "LGhost.h"
#ifdef LGHOST_X86

template<typename pixel_t>
void filter_avx512(const VSFrame * src, VSFrame * dst, const LGhostData * const VS_RESTRICT d, const VSAPI * vsapi, void * lineBuffer) noexcept {
    using var_t = std::conditional_t<std::is_integral_v<pixel_t>, int, float>;
    using vec_t = std::conditional_t<std::is_integral_v<pixel_t>, Vec16i, Vec16f>;

    var_t * _buffer = reinterpret_cast<var_t *>(lineBuffer);

    auto load = [](const pixel_t * srcp) noexcept {
        if constexpr (std::is_same_v<pixel_t, uint8_t>)
            return vec_t(_mm512_cvtepu8_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i *>(srcp))));
        else if constexpr (std::is_same_v<pixel_t, uint16_t>)
            return vec_t(_mm512_cvtepu16_epi32(_mm256_loadu_si256(reinterpret_cast<const __m256i *>(srcp))));
        else
            return vec_t().load(srcp);
    };

    auto store = [&](const vec_t srcp, const vec_t buffer, pixel_t * dstp) noexcept {
        // Integer: clamp at 0, then narrow with unsigned saturation (gives the same as signed and then unsigned saturation).
        if constexpr (std::is_same_v<pixel_t, uint8_t>) {
            const __m512i sum = _mm512_max_epi32(srcp + (buffer >> 7), _mm512_setzero_si512());
            _mm_stream_si128(reinterpret_cast<__m128i *>(dstp), _mm512_cvtusepi32_epi8(sum));
        } else if constexpr (std::is_same_v<pixel_t, uint16_t>) {
            const __m512i sum = _mm512_max_epi32(srcp + (buffer >> 7), _mm512_setzero_si512());
            const __m256i words = _mm512_cvtusepi32_epi16(sum);
            _mm256_stream_si256(reinterpret_cast<__m256i *>(dstp), _mm256_min_epu16(words, _mm256_set1_epi16(static_cast<short>(d->peak))));
        } else {
            const auto result = mul_add(buffer, 1.0f / 128.0f, srcp);
            result.store_nt(dstp);
        }
    };

    for (int plane = 0; plane < d->vi->format.numPlanes; plane++) {
        if (d->process[plane]) {
            const int width = vsapi->getFrameWidth(src, plane);
            const int height = vsapi->getFrameHeight(src, plane);
            const int stride = vsapi->getStride(src, plane) / sizeof(pixel_t);
            const pixel_t * _srcp = reinterpret_cast<const pixel_t *>(vsapi->getReadPtr(src, plane));
            pixel_t * dstp = reinterpret_cast<pixel_t *>(vsapi->getWritePtr(dst, plane));


            for (int y = 0; y < height; y++) {
                memset(_buffer, 0, width * sizeof(var_t));

                for (auto && it : d->options[plane][0]) {
                    const vec_t intensity = static_cast<var_t>(it.intensity);
                    int x = 0;

                    if (it.endX - it.startX >= vec_t().size()) {
                        for (x = it.startX; x + vec_t().size() <= it.endX; x += vec_t().size()) {
                            const vec_t buffer = vec_t().load(_buffer + x);
                            const vec_t result = buffer + (load(_srcp + x - it.shift + 1) - load(_srcp + x - it.shift)) * intensity;
                            result.store(_buffer + x);
                        }
                    }

                    for (; x < it.endX; x++)
                        _buffer[x] += (_srcp[x - it.shift + 1] - _srcp[x - it.shift]) * it.intensity;
                }

                for (auto && it : d->options[plane][1]) {
                    const vec_t intensity = static_cast<var_t>(it.intensity);
                    int x = 0;

                    if (it.endX - it.startX >= vec_t().size()) {
                        for (x = it.startX; x + vec_t().size() <= it.endX; x += vec_t().size()) {
                            const vec_t buffer = vec_t().load(_buffer + x);
                            const vec_t result = buffer + load(_srcp + x - it.shift) * intensity;
                            result.store(_buffer + x);
                        }
                    }

                    for (; x < it.endX; x++)
                        _buffer[x] += _srcp[x - it.shift] * it.intensity;
                }

                for (auto && it : d->options[plane][2]) {
                    const vec_t intensity = static_cast<var_t>(it.intensity);
                    int x = 0;

                    if (it.endX - it.startX >= vec_t().size()) {
                        for (x = it.startX; x + vec_t().size() <= it.endX; x += vec_t().size()) {
                            const vec_t buffer = vec_t().load(_buffer + x);
                            const vec_t tempEdge = load(_srcp + x - it.shift + 1) - load(_srcp + x - it.shift);
                            const vec_t result = select(tempEdge > 0, buffer + tempEdge * intensity, buffer);
                            result.store(_buffer + x);
                        }
                    }

                    for (; x < it.endX; x++)
                        if (const var_t tempEdge = _srcp[x - it.shift + 1] - _srcp[x - it.shift]; tempEdge > 0)
                            _buffer[x] += tempEdge * it.intensity;
                }

                for (auto && it : d->options[plane][3]) {
                    const vec_t intensity = static_cast<var_t>(it.intensity);
                    int x = 0;

                    if (it.endX - it.startX >= vec_t().size()) {
                        for (x = it.startX; x + vec_t().size() <= it.endX; x += vec_t().size()) {
                            const vec_t buffer = vec_t().load(_buffer + x);
                            const vec_t tempEdge = load(_srcp + x - it.shift + 1) - load(_srcp + x - it.shift);
                            const vec_t result = select(tempEdge < 0, buffer + tempEdge * intensity, buffer);
                            result.store(_buffer + x);
                        }
                    }

                    for (; x < it.endX; x++)
                        if (const var_t tempEdge = _srcp[x - it.shift + 1] - _srcp[x - it.shift]; tempEdge < 0)
                            _buffer[x] += tempEdge * it.intensity;
                }

                for (int x = 0; x < width; x += vec_t().size()) {
                    const vec_t srcp = load(_srcp + x);
                    const vec_t buffer = vec_t().load_a(_buffer + x);
                    store(srcp, buffer, dstp + x);
                }

                _srcp += stride;
                dstp += stride;
            }
        }
    }
}

template void filter_avx512<uint8_t>(const VSFrame * src, VSFrame * dst, const LGhostData * const VS_RESTRICT d, const VSAPI * vsapi, void * lineBuffer) noexcept;
template void filter_avx512<uint16_t>(const VSFrame * src, VSFrame * dst, const LGhostData * const VS_RESTRICT d, const VSAPI * vsapi, void * lineBuffer) noexcept;
template void filter_avx512<float>(const VSFrame * src, VSFrame * dst, const LGhostData * const VS_RESTRICT d, const VSAPI * vsapi, void * lineBuffer) noexcept;
#endif
