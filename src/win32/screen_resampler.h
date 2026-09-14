// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <new>
#include <vector>
#if defined(_M_X64) || defined(__SSE2__)
#include <emmintrin.h>
#define M88_RESAMPLE_SSE2 1
#endif

namespace M88V {
// Separable opaque-RGB filtering. Sampling uses pixel centers and mirrored
// edges, including the edge pixel twice, like a reflected image tile.
class ScreenResampler {
    struct Tap { int index; float weight; };
    struct Span { size_t first; size_t count; };
    struct Axis { std::vector<Span> spans; std::vector<Tap> taps; };
    struct alignas(16) Color { float channel[4]; };
    Axis horizontal,vertical;
    std::vector<Color> scratch;
    std::vector<uint32_t> output;
    int oldWidth=0,oldHeight=0,oldOutputWidth=0,oldOutputHeight=0,oldFilter=0;

    static double Kernel(double x,int filter) {
        x=std::abs(x);
        if(filter==1) return x<1 ? 1-x : 0;
        // Catmull-Rom cubic convolution (a = -0.5).
        if(x<1) return ((1.5*x-2.5)*x)*x+1;
        if(x<2) return ((-0.5*x+2.5)*x-4)*x+2;
        return 0;
    }
    static int Reflect(int64_t index,int size) {
        const int64_t period=int64_t(size)*2;
        index%=period;
        if(index<0) index+=period;
        return int(index<size ? index : period-index-1);
    }
    static void BuildAxis(Axis& axis,int source,int destination,int filter) {
        axis.spans.resize(destination); axis.taps.clear();
        const double scale=double(destination)/source;
        const double reduction=std::min(1.0,scale);
        const double radius=(filter==1 ? 1.0 : 2.0)/reduction;
        for(int x=0;x<destination;++x) {
            const double center=(x+0.5)/scale-0.5;
            const int64_t first=int64_t(std::ceil(center-radius));
            const int64_t last=int64_t(std::floor(center+radius));
            const size_t begin=axis.taps.size();
            double sum=0;
            for(int64_t i=first;i<=last;++i) {
                const double weight=Kernel((i-center)*reduction,filter);
                if(weight==0) continue;
                axis.taps.push_back({Reflect(i,source),float(weight)});
                sum+=weight;
            }
            // Normalization preserves uniform colors at every edge and scale.
            for(size_t i=begin;i<axis.taps.size();++i) axis.taps[i].weight=float(axis.taps[i].weight/sum);
            axis.spans[x]={begin,axis.taps.size()-begin};
        }
    }
public:
    bool Render(const uint32_t* source,int width,int height,int outputWidth,int outputHeight,int filter) {
        if(!source || width<=0 || height<=0 || outputWidth<=0 || outputHeight<=0 || (filter!=1 && filter!=2)) return false;
        if(size_t(outputWidth)>scratch.max_size()/size_t(height) ||
            size_t(outputWidth)>output.max_size()/size_t(outputHeight)) return false;
        try {
            if(width!=oldWidth || height!=oldHeight || outputWidth!=oldOutputWidth ||
                outputHeight!=oldOutputHeight || filter!=oldFilter) {
                oldWidth=0; // A failed allocation must force a complete retry.
                BuildAxis(horizontal,width,outputWidth,filter);
                BuildAxis(vertical,height,outputHeight,filter);
                scratch.resize(size_t(outputWidth)*height);
                output.resize(size_t(outputWidth)*outputHeight);
                oldWidth=width; oldHeight=height;
                oldOutputWidth=outputWidth; oldOutputHeight=outputHeight; oldFilter=filter;
            }
        } catch(const std::bad_alloc&) { return false; }

        for(int y=0;y<height;++y) {
            const uint32_t* row=source+size_t(y)*width;
            Color* target=scratch.data()+size_t(y)*outputWidth;
            for(int x=0;x<outputWidth;++x) {
                const Span span=horizontal.spans[x];
#if M88_RESAMPLE_SSE2
                __m128 value=_mm_setzero_ps();
                for(size_t i=span.first;i<span.first+span.count;++i) {
                    const Tap tap=horizontal.taps[i];
                    __m128i pixel=_mm_cvtsi32_si128(int(row[tap.index]));
                    pixel=_mm_unpacklo_epi8(pixel,_mm_setzero_si128());
                    pixel=_mm_unpacklo_epi16(pixel,_mm_setzero_si128());
                    value=_mm_add_ps(value,_mm_mul_ps(_mm_cvtepi32_ps(pixel),_mm_set1_ps(tap.weight)));
                }
                _mm_store_ps(target[x].channel,value);
#else
                Color value{};
                for(size_t i=span.first;i<span.first+span.count;++i) {
                    const Tap tap=horizontal.taps[i]; const uint32_t pixel=row[tap.index];
                    for(int c=0;c<3;++c) value.channel[c]+=float((pixel>>(c*8))&255)*tap.weight;
                }
                target[x]=value;
#endif
            }
        }
        for(int y=0;y<outputHeight;++y) {
            const Span span=vertical.spans[y];
            uint32_t* target=output.data()+size_t(y)*outputWidth;
            for(int x=0;x<outputWidth;++x) {
#if M88_RESAMPLE_SSE2
                __m128 value=_mm_setzero_ps();
                for(size_t i=span.first;i<span.first+span.count;++i) {
                    const Tap tap=vertical.taps[i];
                    const Color& pixel=scratch[size_t(tap.index)*outputWidth+x];
                    value=_mm_add_ps(value,_mm_mul_ps(_mm_load_ps(pixel.channel),_mm_set1_ps(tap.weight)));
                }
                value=_mm_min_ps(_mm_set1_ps(255),_mm_max_ps(_mm_setzero_ps(),value));
                __m128i packed=_mm_cvtps_epi32(value);
                packed=_mm_packs_epi32(packed,_mm_setzero_si128());
                packed=_mm_packus_epi16(packed,_mm_setzero_si128());
                target[x]=uint32_t(_mm_cvtsi128_si32(packed))|0xff000000u;
#else
                Color value{};
                for(size_t i=span.first;i<span.first+span.count;++i) {
                    const Tap tap=vertical.taps[i];
                    const Color& pixel=scratch[size_t(tap.index)*outputWidth+x];
                    for(int c=0;c<3;++c) value.channel[c]+=pixel.channel[c]*tap.weight;
                }
                uint32_t packed=0xff000000u;
                for(int c=0;c<3;++c) packed|=uint32_t(std::clamp(int(std::lround(value.channel[c])),0,255))<<(c*8);
                target[x]=packed;
#endif
            }
        }
        return true;
    }
    const uint32_t* Pixels() const { return output.data(); }
};
}
#undef M88_RESAMPLE_SSE2
