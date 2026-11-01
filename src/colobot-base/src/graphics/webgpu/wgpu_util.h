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

/**
 * \file graphics/webgpu/wgpu_util.h
 * \brief WebGPU helpers: enum translation, primitive conversion, image conversion (colobot-web WP4)
 */

#pragma once

#include "graphics/core/renderers.h"
#include "graphics/core/texture.h"

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <string>
#include <vector>

struct ImageData;

namespace Gfx
{

enum class CullFace : unsigned char;
enum class TransparencyMode : unsigned char;

//! Dynamic uniform offsets must be multiples of this (WebGPU minUniformBufferOffsetAlignment default)
constexpr uint32_t WGPU_UNIFORM_ALIGN = 256;

inline uint64_t AlignUp(uint64_t value, uint64_t align)
{
    return (value + align - 1) / align * align;
}

//! Fixed-function blend state equivalent of CGL33Device::SetTransparency(); nullopt-like: returns false for NONE
bool TranslateBlend(TransparencyMode mode, wgpu::BlendState& out);

//! Cull mode. Colobot faces are reversed (gl33 uses glFrontFace(GL_CW)) -> pipelines use FrontFace::CW.
wgpu::CullMode TranslateCull(CullFace mode);

wgpu::ColorWriteMask TranslateColorMask(bool r, bool g, bool b, bool a);

//! Topology natively supported by WebGPU for the given type, or Undefined if it must be converted (FAN, LOOP)
wgpu::PrimitiveTopology NativeTopology(PrimitiveType type);

//! Topology after ConvertPrimitives() (FAN->TriangleList, LOOP->LineList, POINTS->TriangleList quads)
wgpu::PrimitiveTopology ConvertedTopology(PrimitiveType type);

/**
 * Converts one primitive run into a WebGPU-drawable vertex list, appending to out.
 *  - TRIANGLES, TRIANGLE_STRIP, LINES, LINE_STRIP: copied as-is (draw with NativeTopology)
 *  - TRIANGLE_FAN  -> triangle list
 *  - LINE_LOOP     -> line list
 *  - POINTS        -> two triangles per point, offset by pointHalfSize in position.x/.y
 * Returns number of vertices appended. Use ConvertedTopology() when NativeTopology() is Undefined
 * or when POINTS are passed.
 */
template<typename V>
int ConvertPrimitives(PrimitiveType type, const V* in, int count, std::vector<V>& out, float pointHalfSize = 0.5f)
{
    size_t start = out.size();
    switch (type)
    {
    case PrimitiveType::TRIANGLE_FAN:
        for (int i = 1; i + 1 < count; ++i)
        {
            out.push_back(in[0]);
            out.push_back(in[i]);
            out.push_back(in[i + 1]);
        }
        break;
    case PrimitiveType::LINE_LOOP:
        for (int i = 0; i < count && count > 1; ++i)
        {
            out.push_back(in[i]);
            out.push_back(in[(i + 1) % count]);
        }
        break;
    case PrimitiveType::POINTS:
        for (int i = 0; i < count; ++i)
        {
            V c[4] = { in[i], in[i], in[i], in[i] };
            c[0].position.x -= pointHalfSize; c[0].position.y -= pointHalfSize;
            c[1].position.x += pointHalfSize; c[1].position.y -= pointHalfSize;
            c[2].position.x += pointHalfSize; c[2].position.y += pointHalfSize;
            c[3].position.x -= pointHalfSize; c[3].position.y += pointHalfSize;
            out.push_back(c[0]); out.push_back(c[1]); out.push_back(c[2]);
            out.push_back(c[0]); out.push_back(c[2]); out.push_back(c[3]);
        }
        break;
    default:
        out.insert(out.end(), in, in + count);
        break;
    }
    return static_cast<int>(out.size() - start);
}

//! Tightly packed RGBA8 pixels
struct RGBAImage
{
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
    bool alpha = false;     //!< source had an alpha channel
};

/**
 * Converts an SDL surface (ImageData) into RGBA8, honoring the explicit TextureFormat like
 * gl33 PrepareTextureData() did (RGB/BGR/RGBA/BGRA interpret raw bytes; AUTO uses the SDL masks).
 */
bool ConvertToRGBA(ImageData* data, TextureFormat format, RGBAImage& out);

//! Box-filter one mip level down (RGBA8). Sizes are clamped to >= 1.
RGBAImage DownsampleRGBA(const RGBAImage& src);

//! Number of mip levels for a full chain
uint32_t MipLevelCount(int width, int height);

//! Make a shader module from concatenated WGSL parts (logs compilation errors via uncaptured error callback)
wgpu::ShaderModule CreateShaderModule(const wgpu::Device& device, const std::string& label,
                                      std::initializer_list<const char*> parts);

} // namespace Gfx
