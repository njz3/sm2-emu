//  ____  __  __  ____         _____ __  __ _   _
// / ___||  \/  ||___ \       | ____|  \/  | | | |
// \___ \| |\/| |  __) |_____ |  _| | |\/| | | | |
//  ___) | |  | | / __/|_____|| |___| |  | | |_| |
// |____/|_|  |_||_____|      |_____|_|  |_|\___/
//
// A Sega Model 2 arcade emulator.
// Copyright (c) 2025+ Daniel Martin (dmanlfc)
// SPDX-License-Identifier: GPL-3.0-or-later AND BSD-3-Clause
//
// This header must not be removed. sm2-emu is licensed under the GNU General
// Public License, version 3 or later, with additional terms under section 7 of
// that licence. See LICENSE and NOTICE.
//
// The texture-header decoding reproduced here follows MAME's
// src/mame/sega/model2_v.cpp and model2rd.ipp (BSD-3-Clause, copyright-holders
// R. Belmont, Olivier Galibert, ElSemi, Angelo Salese, Matthew Daniels).
#include "render/geometry.h"

#include "core/log.h"
#include "hw/geometrizer.h"
#include "hw/model2_machine_base.h"
#include "hw/model2_video.h"
#include "render/texture_replace.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace sm2::render {
namespace {

/// Texture header bits that select the pixel path. MAME reads bits 14 and 13
/// together as a renderer index: bit 14 chooses textured over solid and bit 13
/// chooses translucent over opaque.
constexpr u32 kHeaderTextured    = 1u << 14;
constexpr u32 kHeaderTranslucent = 1u << 13;
constexpr u32 kHeaderChecker     = 1u << 15;

}  // namespace

u32 deepest_mip_level(u32 width, u32 height)
{
    u32 smaller = std::min(width, height);
    u32 level   = 0;
    while (smaller > 2 && level < 15) {
        smaller >>= 1;
        ++level;
    }
    return level;
}

PolyParams describe_polygon(const hw::RenderPolygon& poly, const hw::Model2Video& video)
{
    PolyParams params{};

    // Ten bits of colour base against the 3D half of palette RAM. The
    // components rather than a resolved colour, because the shade is a
    // per-pixel matter.
    params.colour     = video.polygon_colour_components((poly.texheader[3] >> 6) & 0x3ff);
    params.luma_base  = static_cast<u32>(poly.texheader[1] & 0xff) << 7;
    params.luma_scale = poly.luma;
    params.tex_lod    = poly.texlod;

    if ((poly.texheader[0] & kHeaderChecker) != 0) {
        params.flags |= kFlagChecker;
    }
    if ((poly.texheader[0] & kHeaderTranslucent) != 0) {
        params.flags |= kFlagTranslucent;
    }
    if ((poly.texheader[0] & kHeaderTextured) == 0) {
        return params;
    }
    params.flags |= kFlagTextured;

    // Dimensions are powers of two from 32 up, three bits each.
    params.tex_width  = 32u << (poly.texheader[0] & 0x7);
    params.tex_height = 32u << ((poly.texheader[0] >> 3) & 0x7);
    params.tex_x      = 32u * (poly.texheader[2] & 0x3f);
    params.tex_y      = 32u * ((poly.texheader[2] >> 6) & 0x1f);

    const bool mirror_x = ((poly.texheader[0] >> 8) & 1) != 0;
    const bool mirror_y = ((poly.texheader[0] >> 9) & 1) != 0;
    if (mirror_x) {
        params.flags |= kFlagMirrorX;
    }
    if (mirror_y) {
        params.flags |= kFlagMirrorY;
    }
    // Smooth wrapping and mirroring are mutually exclusive; MAME masks the one
    // out when the other is set rather than letting both apply.
    if (((poly.texheader[0] >> 6) & 1) != 0 && !mirror_x) {
        params.flags |= kFlagWrapX;
    }
    if (((poly.texheader[0] >> 7) & 1) != 0 && !mirror_y) {
        params.flags |= kFlagWrapY;
    }
    if ((poly.texheader[2] & 0x1000) != 0) {
        params.flags |= kFlagSheet;
    }
    if (((poly.texheader[0] >> 12) & 1) != 0) {
        params.flags |= kFlagMicro;
    }

    params.micro_min_lod = (poly.texheader[0] >> 10) & 3;
    params.micro_x       = ((poly.texheader[2] >> 13) & 1) * 128;
    params.micro_y       = ((poly.texheader[2] >> 14) & 3) * 128;

    params.flags |= deepest_mip_level(params.tex_width, params.tex_height) << kMaxLevelShift;
    return params;
}

TriangulatedFrame triangulate(const hw::Model2MachineBase* machine,
                              const hw::Model2Video&       video,
                              bool*                        warned,
                              TextureReplacements*         replacements,
                              bool                         blend_translucency)
{
    TriangulatedFrame frame;
    if (machine == nullptr) {
        return frame;
    }
    if (replacements != nullptr && replacements->empty()) {
        replacements = nullptr;
    }
    if (replacements != nullptr) {
        replacements->begin_frame(*machine, video);
    }

    // Blended polygons are held back and appended after everything else in
    // reverse, so each blends over what is behind it.
    struct Deferred {
        ScissorRect scissor;
        u32         first_vertex = 0;
        u32         vertex_count = 0;
    };
    std::vector<Vertex>   deferred_vertices;
    std::vector<Deferred> deferred;

    for (const hw::RenderPolygon& poly : machine->render_list().polygons) {
        // Untextured and translucent is the one combination the hardware
        // draws nothing for: `draw_scanline_solid` in model2rd.ipp begins
        // with `if (Translucent) return;` because with no texel there is
        // nothing to alpha-test. The *textured* translucent path does draw —
        // it alpha-tests each filtered texel and skips the ones below half —
        // so it must not be culled here. Virtua Fighter 2 draws its sky with
        // 181 textured translucent polygons, and culling them leaves a flat
        // band where the sky should be.
        if ((poly.texheader[0] & kHeaderTranslucent) != 0
            && (poly.texheader[0] & kHeaderTextured) == 0) {
            ++frame.blank_polygons;
            continue;
        }
        if (poly.num_vertices < 3) {
            continue;
        }

        const ScissorRect scissor{
            poly.scissor[0], poly.scissor[1],
            static_cast<u32>(poly.scissor[2] - poly.scissor[0]),
            static_cast<u32>(poly.scissor[3] - poly.scissor[1])};
        if (scissor.width == 0 || scissor.height == 0) {
            continue;
        }

        const u32 triangles = static_cast<u32>(poly.num_vertices) - 2u;
        if (frame.vertices.size() + deferred_vertices.size() + triangles * 3u > kMaxVertices
            || frame.polygons.size() >= kMaxPolygons) {
            if (warned != nullptr && !*warned) {
                *warned = true;
                SM2_WARN("3d: more geometry than the buffers hold (%u vertices, %u "
                         "polygons); the rest of this frame is dropped",
                         kMaxVertices, kMaxPolygons);
            }
            break;
        }

        const u32        index  = static_cast<u32>(frame.polygons.size());
        PolyParams params = describe_polygon(poly, video);
        if (replacements != nullptr && (params.flags & kFlagTextured) != 0) {
            replacements->resolve(*machine, video, poly, &params);
        }
        const bool blended = blend_translucency && (params.flags & kFlagChecker) != 0;
        if (blended) {
            params.flags = (params.flags & ~kFlagChecker) | kFlagBlended;
        }
        frame.polygons.push_back(params);

        // The vertex ring for the fragment shader's edge walk (see PolyGeom).
        // No 1/8 on u/v here: the shader's int(uv*32) carries the eighths the
        // software renderer folds into its uoz*z*256, so texelUV stays the raw
        // p[1]/p[2] the fan path produced.
        PolyGeom geom{};
        geom.num_vertices = poly.num_vertices;
        for (u32 corner = 0; corner < poly.num_vertices; ++corner) {
            const hw::PolyVertex& pv = poly.v[corner];
            const float           oz = 1.0F / (pv.p[0] + std::numeric_limits<float>::min());
            geom.x[corner]          = pv.x;
            geom.y[corner]          = pv.y;
            geom.one_over_z[corner] = oz;
            geom.u_over_z[corner]   = pv.p[1] * oz;
            geom.v_over_z[corner]   = pv.p[2] * oz;
        }
        frame.geometry.push_back(geom);

        // A fan. The clipper produces convex polygons, so fanning from the
        // first vertex cannot fold over itself.
        const auto emit_fan = [&](std::vector<Vertex>& out) {
            const auto emit = [&](u32 corner) {
                const hw::PolyVertex& v = poly.v[corner];
                out.push_back(Vertex{v.x, v.y, v.p[1], v.p[2], v.p[0], index});
            };
            for (u32 corner = 1; corner + 1 < poly.num_vertices; ++corner) {
                emit(0);
                emit(corner);
                emit(corner + 1);
            }
        };

        if (blended) {
            const u32 first = static_cast<u32>(deferred_vertices.size());
            emit_fan(deferred_vertices);
            deferred.push_back(
                Deferred{scissor, first, static_cast<u32>(deferred_vertices.size()) - first});
            ++frame.drawn_polygons;
            continue;
        }

        const bool can_early = (params.flags & (kFlagChecker | kFlagTranslucent)) == 0;

        // A new batch starts on a scissor or can_early change, so each batch is
        // a contiguous same-capability run in list order -- which keeps the
        // order-based fill mask's global draw order exact.
        if (frame.batches.empty() || frame.batches.back().scissor.x != scissor.x
            || frame.batches.back().scissor.y != scissor.y
            || frame.batches.back().scissor.width != scissor.width
            || frame.batches.back().scissor.height != scissor.height
            || frame.batches.back().early != can_early) {
            frame.batches.push_back(
                Batch{scissor, static_cast<u32>(frame.vertices.size()), 0, can_early});
        }

        emit_fan(frame.vertices);

        frame.batches.back().vertex_count =
            static_cast<u32>(frame.vertices.size()) - frame.batches.back().first_vertex;
        ++frame.drawn_polygons;
    }

    for (auto it = deferred.rbegin(); it != deferred.rend(); ++it) {
        const ScissorRect& scissor = it->scissor;
        if (frame.batches.empty() || !frame.batches.back().blended
            || frame.batches.back().scissor.x != scissor.x
            || frame.batches.back().scissor.y != scissor.y
            || frame.batches.back().scissor.width != scissor.width
            || frame.batches.back().scissor.height != scissor.height) {
            frame.batches.push_back(
                Batch{scissor, static_cast<u32>(frame.vertices.size()), 0, false, true});
        }
        const auto first = deferred_vertices.begin() + static_cast<std::ptrdiff_t>(it->first_vertex);
        frame.vertices.insert(frame.vertices.end(), first,
                              first + static_cast<std::ptrdiff_t>(it->vertex_count));
        frame.batches.back().vertex_count += it->vertex_count;
    }

    return frame;
}

Letterbox compute_letterbox(u32           window_width,
                            u32           window_height,
                            AspectMode    aspect,
                            ScalingMethod method,
                            PictureArea   area)
{
    const float width  = static_cast<float>(window_width) * area.width;
    const float height = static_cast<float>(window_height) * area.height;

    float target_width  = width;
    float target_height = height;

    if (aspect != AspectMode::Stretch) {
        // Fit the largest rectangle of the chosen aspect inside the window.
        const float ratio = aspect == AspectMode::SquarePixel
                                ? static_cast<float>(kNativeWidth)
                                      / static_cast<float>(kNativeHeight)
                            : aspect == AspectMode::Widescreen ? 16.0F / 9.0F
                                                               : kDisplayAspect;
        target_width  = width;
        target_height = width / ratio;
        if (target_height > height) {
            target_height = height;
            target_width  = height * ratio;
        }
    }

    // Integer snapping applies to the shaped fits only. Stretch means "fill the
    // window", so it is left at the full window size regardless of method --
    // snapping it would defeat the point and leave bars.
    if (method == ScalingMethod::Integer && aspect != AspectMode::Stretch) {
        // Aspect-aware: snap the height to a whole native multiple so the
        // scanlines are pixel-perfect, then set the width from the aspect
        // rectangle already fitted above. A non-integer horizontal scale is
        // accepted so the chosen shape (4:3 or square) is honoured -- the
        // vertical is what the eye reads as "clean scanlines".
        const float fh     = static_cast<float>(kNativeHeight);
        u32         factor = static_cast<u32>(target_height / fh);
        factor             = std::max(factor, 1u);
        const float scanline_height = fh * static_cast<float>(factor);
        target_width  *= scanline_height / target_height;
        target_height  = scanline_height;
    }

    Letterbox box{};
    box.x      = static_cast<float>(window_width) * area.x + (width - target_width) * 0.5F;
    box.y      = static_cast<float>(window_height) * area.y + (height - target_height) * 0.5F;
    box.width  = target_width;
    box.height = target_height;
    return box;
}

Letterbox compute_game_rect(u32 window_width, u32 window_height, AspectMode aspect,
                            ScalingMethod method, PictureArea area)
{
    Letterbox box = compute_letterbox(window_width, window_height, aspect, method, area);
    if (aspect == AspectMode::Widescreen) {
        const float inner = box.width * static_cast<float>(kNativeWidth)
                          / static_cast<float>(frame_width(kWideColumns));
        box.x += (box.width - inner) * 0.5F;
        box.width = inner;
    }
    return box;
}


namespace {

constexpr float kHalfPi = 1.5707963F;

/// How much of the picture a point `x` into an edge band shows: the band is
/// `band` wide and `length` of picture is left on its side (see FrameMap).
[[nodiscard]] float bend(float x, float length, float band, float radius)
{
    if (band <= 0.0F) {
        return 0.0F;
    }
    if (length <= band) {
        return length * x / band;
    }
    const float t = std::min(length / std::max(radius, 1e-4F), kHalfPi);
    return length * std::asin(std::clamp(std::sin(t) * x / band, -1.0F, 1.0F)) / t;
}

/// bend()'s inverse: where in the band `s` of the picture shows.
[[nodiscard]] float unbend(float s, float length, float band, float radius)
{
    if (band <= 0.0F || length <= 0.0F) {
        return 0.0F;
    }
    if (length <= band) {
        return s * band / length;
    }
    const float t = std::min(length / std::max(radius, 1e-4F), kHalfPi);
    return band * std::sin(t * std::clamp(s / length, 0.0F, 1.0F)) / std::sin(t);
}

}  // namespace

float FrameMap::to_picture(float f) const
{
    if (axis == 0) {
        return f;
    }
    float g = offset + f;
    if (barrel) {
        const float band = (1.0F - centre) * 0.5F;
        if (f < band) {
            const float left = offset + band;
            g = left - bend(band - f, left, band, radius);
        } else if (f > 1.0F - band) {
            const float right = extent - offset - 1.0F + band;
            g = offset + 1.0F - band + bend(f - (1.0F - band), right, band, radius);
        }
    }
    return g / extent;
}

float FrameMap::to_frame(float p) const
{
    if (axis == 0) {
        return p;
    }
    const float g = p * extent;
    if (barrel) {
        const float band  = (1.0F - centre) * 0.5F;
        const float left  = offset + band;
        const float right = offset + 1.0F - band;
        if (g < left) {
            return band - unbend(left - g, left, band, radius);
        }
        if (g > right) {
            return 1.0F - band + unbend(g - right, extent - right, band, radius);
        }
    }
    return g - offset;
}

void FrameMap::window_to_game(float x, float y, float* u, float* v) const
{
    float pu = picture.width > 0.0F ? (x - picture.x) / picture.width : 0.5F;
    float pv = picture.height > 0.0F ? (y - picture.y) / picture.height : 0.5F;
    if (axis == 1 && frame.width > 0.0F) {
        pu = to_picture((x - frame.x) / frame.width);
    } else if (axis == 2 && frame.height > 0.0F) {
        pv = to_picture((y - frame.y) / frame.height);
    }
    *u = side > 0.0F ? (pu - side) / (1.0F - 2.0F * side) : pu;
    *v = pv;
}

void FrameMap::game_to_window(float u, float v, float* x, float* y) const
{
    const float pu = side > 0.0F ? side + u * (1.0F - 2.0F * side) : u;
    *x = picture.x + pu * picture.width;
    *y = picture.y + v * picture.height;
    if (axis == 1) {
        *x = frame.x + to_frame(pu) * frame.width;
    } else if (axis == 2) {
        *y = frame.y + to_frame(v) * frame.height;
    }
}

FrameMap compute_frame_map(u32 window_width, u32 window_height, const PresentOptions& options)
{
    const bool  wide   = options.aspect_mode == AspectMode::Widescreen;
    const float raster = static_cast<float>(frame_width(wide ? kWideColumns : 0))
                       / static_cast<float>(kNativeHeight);
    // The picture's own shape: the monitor's, or its raster's for Original.
    float shape = wide ? 16.0F / 9.0F : kDisplayAspect;
    float frame_shape = shape;
    enum class Fill { Fit, Stretch, Crop } fill = Fill::Fit;
    switch (options.aspect_ratio) {
        case AspectRatio::Original:           frame_shape = shape = raster; break;
        case AspectRatio::FourThree:          frame_shape = 4.0F / 3.0F; break;
        case AspectRatio::SixteenNine:        frame_shape = 16.0F / 9.0F; break;
        case AspectRatio::FourThreeStretch:   frame_shape = 4.0F / 3.0F;  fill = Fill::Stretch; break;
        case AspectRatio::SixteenNineStretch: frame_shape = 16.0F / 9.0F; fill = Fill::Stretch; break;
        case AspectRatio::FourThreeCrop:      frame_shape = 4.0F / 3.0F;  fill = Fill::Crop; break;
        case AspectRatio::SixteenNineCrop:    frame_shape = 16.0F / 9.0F; fill = Fill::Crop; break;
    }

    FrameMap map;
    map.side = wide ? static_cast<float>(kWideColumns) / static_cast<float>(frame_width(kWideColumns))
                    : 0.0F;

    // The frame: the largest of its shape in the picture area, or all of the
    // area when it is stretched to the window.
    const PictureArea& area = options.picture_area;
    const float aw = static_cast<float>(window_width) * area.width;
    const float ah = static_cast<float>(window_height) * area.height;
    const float ax = static_cast<float>(window_width) * area.x;
    const float ay = static_cast<float>(window_height) * area.y;
    Letterbox frame{ax, ay, aw, ah};
    if (!options.window_stretch) {
        float fw = aw;
        float fh = aw / frame_shape;
        if (fh > ah) {
            fh = ah;
            fw = ah * frame_shape;
        }
        frame = Letterbox{ax + (aw - fw) * 0.5F, ay + (ah - fh) * 0.5F, fw, fh};
    }
    map.frame = frame;

    switch (fill) {
        case Fill::Fit: {
            // The whole picture inside, at its shape (in the frame's own shape,
            // so a frame stretched to the window stretches it too).
            float w = 1.0F;
            float h = 1.0F;
            if (shape > frame_shape) {
                h = frame_shape / shape;
            } else {
                w = shape / frame_shape;
            }
            Letterbox pic{frame.x + (1.0F - w) * 0.5F * frame.width,
                          frame.y + (1.0F - h) * 0.5F * frame.height, w * frame.width,
                          h * frame.height};
            // Integer: the picture's height a whole multiple of the raster's,
            // its width following, so the scanlines are pixel-perfect.
            if (options.scaling_method == ScalingMethod::Integer && !options.window_stretch) {
                const float native = static_cast<float>(kNativeHeight);
                const float factor = std::max(std::floor(pic.height / native), 1.0F);
                const float height = native * factor;
                const float width  = pic.width * height / pic.height;
                pic = Letterbox{pic.x + (pic.width - width) * 0.5F,
                                pic.y + (pic.height - height) * 0.5F, width, height};
            }
            map.view    = pic;
            map.picture = pic;
            break;
        }
        case Fill::Stretch:
            map.view    = frame;
            map.picture = frame;
            break;
        case Fill::Crop: {
            map.view    = frame;
            map.picture = frame;
            const float kept = (std::clamp(options.crop_position, -1.0F, 1.0F) + 1.0F) * 0.5F;
            if (shape > frame_shape * 1.0001F) {
                map.axis    = 1;
                map.extent  = shape / frame_shape;
                map.offset  = (map.extent - 1.0F) * kept;
                map.picture = Letterbox{frame.x - map.offset * frame.width, frame.y,
                                        map.extent * frame.width, frame.height};
            } else if (shape * 1.0001F < frame_shape) {
                map.axis    = 2;
                map.extent  = frame_shape / shape;
                map.offset  = (map.extent - 1.0F) * kept;
                map.picture = Letterbox{frame.x, frame.y - map.offset * frame.height,
                                        frame.width, map.extent * frame.height};
            }
            map.barrel = options.crop_barrel && map.axis != 0;
            map.centre = std::clamp(options.barrel_center, 0.0F, 1.0F);
            map.radius = std::max(options.barrel_radius, 0.01F);
            break;
        }
    }
    return map;
}

}  // namespace sm2::render
