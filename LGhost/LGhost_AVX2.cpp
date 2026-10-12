#include "LGhost.h"
#ifdef LGHOST_X86

template<typename pixel_t>
void filter_avx2(const VSFrame * src, VSFrame * dst, const LGhostData * const VS_RESTRICT d, const VSAPI * vsapi, void * lineBuffer) noexcept {
    using var_t = std::conditional_t<std::is_integral_v<pixel_t>, int, float>;
    using vec_t = std::conditional_t<std::is_integral_v<pixel_t>, Vec8i, Vec8f>;

    var_t * _buffer = reinterpret_cast<var_t *>(lineBuffer);

    auto load = [](const pixel_t * srcp) noexcept {
        if constexpr (std::is_same_v<pixel_t, uint8_t>)
            return vec_t(_mm256_cvtepu8_epi32(_mm_loadl_epi64(reinterpret_cast<const __m128i *>(srcp))));
        else if constexpr (std::is_same_v<pixel_t, uint16_t>)
            return vec_t(_mm256_cvtepu16_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i *>(srcp))));
        else
            return vec_t().load(srcp);
    };

    auto store = [&](const vec_t srcp, const vec_t buffer, pixel_t * dstp) noexcept {
        // Integer: narrow both 128 bit halves in order, 8 bit through int16 with signed and then unsigned saturation.
        if constexpr (std::is_same_v<pixel_t, uint8_t>) {
            const __m256i sum = srcp + (buffer >> 7);
            const __m128i words = _mm_packs_epi32(_mm256_castsi256_si128(sum), _mm256_extracti128_si256(sum, 1));
            _mm_storel_epi64(reinterpret_cast<__m128i *>(dstp), _mm_packus_epi16(words, words));
        } else if constexpr (std::is_same_v<pixel_t, uint16_t>) {
            const __m256i sum = srcp + (buffer >> 7);
            const __m128i words = _mm_packus_epi32(_mm256_castsi256_si128(sum), _mm256_extracti128_si256(sum, 1));
            _mm_stream_si128(reinterpret_cast<__m128i *>(dstp), _mm_min_epu16(words, _mm_set1_epi16(static_cast<short>(d->peak))));
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

template void filter_avx2<uint8_t>(const VSFrame * src, VSFrame * dst, const LGhostData * const VS_RESTRICT d, const VSAPI * vsapi, void * lineBuffer) noexcept;
template void filter_avx2<uint16_t>(const VSFrame * src, VSFrame * dst, const LGhostData * const VS_RESTRICT d, const VSAPI * vsapi, void * lineBuffer) noexcept;
template void filter_avx2<float>(const VSFrame * src, VSFrame * dst, const LGhostData * const VS_RESTRICT d, const VSAPI * vsapi, void * lineBuffer) noexcept;
#endif
