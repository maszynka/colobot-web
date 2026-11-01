/*
 * This file is part of the Colobot: Gold Edition source code
 * Copyright (C) 2001-2023, Daniel Roux, EPSITEC SA & TerranovaTeam
 * http://epsitec.ch; http://colobot.info; http://github.com/colobot
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see http://gnu.org/licenses
 */

#include "graphics/webgpu/wgpu_util.h"

#include "common/image.h"
#include "common/logger.h"

#include "graphics/core/material.h"
#include "graphics/core/transparency.h"

#include <SDL.h>

#include <algorithm>
#include <cstring>

namespace Gfx
{

bool TranslateBlend(TransparencyMode mode, wgpu::BlendState& out)
{
    wgpu::BlendComponent c;
    c.operation = wgpu::BlendOperation::Add;
    switch (mode)
    {
    case TransparencyMode::ALPHA:
        c.srcFactor = wgpu::BlendFactor::SrcAlpha;
        c.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
        break;
    case TransparencyMode::BLACK:
        c.srcFactor = wgpu::BlendFactor::One;
        c.dstFactor = wgpu::BlendFactor::OneMinusSrc;
        break;
    case TransparencyMode::WHITE:
        c.srcFactor = wgpu::BlendFactor::Dst;
        c.dstFactor = wgpu::BlendFactor::Zero;
        break;
    case TransparencyMode::NONE:
    default:
        return false;
    }
    out.color = c;
    out.alpha = c;
    return true;
}

wgpu::CullMode TranslateCull(CullFace mode)
{
    switch (mode)
    {
    case CullFace::BACK: return wgpu::CullMode::Back;
    case CullFace::FRONT: return wgpu::CullMode::Front;
    // BOTH: WebGPU has no front+back culling; triangles would vanish entirely in GL.
    // Callers that need this should skip the draw (see CWebGPUDevice::ShouldSkipTriangles()).
    case CullFace::BOTH: return wgpu::CullMode::None;
    case CullFace::NONE:
    default: return wgpu::CullMode::None;
    }
}

wgpu::ColorWriteMask TranslateColorMask(bool r, bool g, bool b, bool a)
{
    wgpu::ColorWriteMask m = wgpu::ColorWriteMask::None;
    if (r) m = m | wgpu::ColorWriteMask::Red;
    if (g) m = m | wgpu::ColorWriteMask::Green;
    if (b) m = m | wgpu::ColorWriteMask::Blue;
    if (a) m = m | wgpu::ColorWriteMask::Alpha;
    return m;
}

wgpu::PrimitiveTopology NativeTopology(PrimitiveType type)
{
    switch (type)
    {
    case PrimitiveType::LINES: return wgpu::PrimitiveTopology::LineList;
    case PrimitiveType::LINE_STRIP: return wgpu::PrimitiveTopology::LineStrip;
    case PrimitiveType::TRIANGLES: return wgpu::PrimitiveTopology::TriangleList;
    case PrimitiveType::TRIANGLE_STRIP: return wgpu::PrimitiveTopology::TriangleStrip;
    default: return wgpu::PrimitiveTopology::Undefined;
    }
}

wgpu::PrimitiveTopology ConvertedTopology(PrimitiveType type)
{
    switch (type)
    {
    case PrimitiveType::TRIANGLE_FAN:
    case PrimitiveType::POINTS:
        return wgpu::PrimitiveTopology::TriangleList;
    case PrimitiveType::LINE_LOOP:
        return wgpu::PrimitiveTopology::LineList;
    default:
        return NativeTopology(type);
    }
}

bool ConvertToRGBA(ImageData* data, TextureFormat format, RGBAImage& out)
{
    if (data == nullptr || data->surface == nullptr) return false;

    SDL_Surface* surface = data->surface;
    out.width = surface->w;
    out.height = surface->h;
    out.pixels.assign(static_cast<size_t>(out.width) * out.height * 4, 255);

    int bpp = surface->format->BytesPerPixel;

    // Explicit formats interpret raw bytes (same as GL upload with sourceFormat)
    int r = 0, g = 1, b = 2, a = -1;
    bool manual = true;
    switch (format)
    {
    case TextureFormat::RGB:  r = 0; g = 1; b = 2; a = -1; break;
    case TextureFormat::BGR:  r = 2; g = 1; b = 0; a = -1; break;
    case TextureFormat::RGBA: r = 0; g = 1; b = 2; a = 3; break;
    case TextureFormat::BGRA: r = 2; g = 1; b = 0; a = 3; break;
    case TextureFormat::AUTO:
    default:
        manual = false;
        break;
    }

    if (manual && bpp != (a >= 0 ? 4 : 3))
        manual = false; // mismatch: fall back to SDL conversion

    if (manual)
    {
        SDL_LockSurface(surface);
        const uint8_t* src = static_cast<const uint8_t*>(surface->pixels);
        for (int y = 0; y < out.height; ++y)
        {
            const uint8_t* row = src + static_cast<size_t>(y) * surface->pitch;
            uint8_t* dst = out.pixels.data() + static_cast<size_t>(y) * out.width * 4;
            for (int x = 0; x < out.width; ++x)
            {
                const uint8_t* p = row + x * bpp;
                dst[x * 4 + 0] = p[r];
                dst[x * 4 + 1] = p[g];
                dst[x * 4 + 2] = p[b];
                dst[x * 4 + 3] = a >= 0 ? p[a] : 255;
            }
        }
        SDL_UnlockSurface(surface);
        out.alpha = a >= 0;
        return true;
    }

    out.alpha = surface->format->Amask != 0;
    SDL_Surface* converted = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
    if (converted == nullptr)
    {
        GetLogger()->Error("WebGPU: SDL_ConvertSurfaceFormat failed: %%", SDL_GetError());
        return false;
    }
    SDL_LockSurface(converted);
    for (int y = 0; y < out.height; ++y)
    {
        std::memcpy(out.pixels.data() + static_cast<size_t>(y) * out.width * 4,
                    static_cast<const uint8_t*>(converted->pixels) + static_cast<size_t>(y) * converted->pitch,
                    static_cast<size_t>(out.width) * 4);
    }
    SDL_UnlockSurface(converted);
    SDL_FreeSurface(converted);
    return true;
}

RGBAImage DownsampleRGBA(const RGBAImage& src)
{
    RGBAImage dst;
    dst.width = std::max(1, src.width / 2);
    dst.height = std::max(1, src.height / 2);
    dst.alpha = src.alpha;
    dst.pixels.resize(static_cast<size_t>(dst.width) * dst.height * 4);
    for (int y = 0; y < dst.height; ++y)
    {
        int y0 = std::min(src.height - 1, y * 2), y1 = std::min(src.height - 1, y * 2 + 1);
        for (int x = 0; x < dst.width; ++x)
        {
            int x0 = std::min(src.width - 1, x * 2), x1 = std::min(src.width - 1, x * 2 + 1);
            for (int c = 0; c < 4; ++c)
            {
                auto at = [&](int xx, int yy) { return static_cast<int>(src.pixels[(static_cast<size_t>(yy) * src.width + xx) * 4 + c]); };
                int sum = at(x0, y0) + at(x1, y0) + at(x0, y1) + at(x1, y1);
                dst.pixels[(static_cast<size_t>(y) * dst.width + x) * 4 + c] = static_cast<uint8_t>((sum + 2) / 4);
            }
        }
    }
    return dst;
}

uint32_t MipLevelCount(int width, int height)
{
    uint32_t levels = 1;
    int size = std::max(width, height);
    while (size > 1)
    {
        size /= 2;
        ++levels;
    }
    return levels;
}

wgpu::ShaderModule CreateShaderModule(const wgpu::Device& device, const std::string& label,
                                      std::initializer_list<const char*> parts)
{
    std::string code;
    for (const char* part : parts)
    {
        code += part;
        code += "\n";
    }

    wgpu::ShaderSourceWGSL wgsl;
    wgsl.code = wgpu::StringView(std::string_view(code));

    wgpu::ShaderModuleDescriptor desc;
    desc.nextInChain = &wgsl;
    desc.label = wgpu::StringView(std::string_view(label));
    return device.CreateShaderModule(&desc);
}

} // namespace Gfx
