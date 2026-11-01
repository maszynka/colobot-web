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

#include "graphics/webgpu/wgpu_texture.h"

#include "common/logger.h"

#include <algorithm>

namespace Gfx
{

void CWebGPUTextureManager::Init(wgpu::Device device, wgpu::Queue queue)
{
    m_device = std::move(device);
    m_queue = std::move(queue);

    // 1x1 white default texture (bound for texture id 0, like gl33 m_whiteTexture)
    wgpu::TextureDescriptor desc;
    desc.label = "white";
    desc.size = { 1, 1, 1 };
    desc.format = wgpu::TextureFormat::RGBA8Unorm;
    desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
    m_white.texture = m_device.CreateTexture(&desc);
    m_white.view = m_white.texture.CreateView();
    m_white.sampler = GetSampler(TextureFilter::NEAREST, TextureWrapMode::CLAMP, false, 1);
    m_white.width = m_white.height = 1;
    m_white.serial = m_nextSerial++;
    RGBAImage px;
    px.width = px.height = 1;
    px.pixels = { 255, 255, 255, 255 };
    Upload(m_white, px, 0, 0);
}

void CWebGPUTextureManager::Shutdown()
{
    m_textures.clear();
    m_samplers.clear();
    m_comparisonSampler = nullptr;
    m_white = WebGPUTextureEntry{};
}

wgpu::Sampler CWebGPUTextureManager::GetSampler(TextureFilter filter, TextureWrapMode wrap, bool mipmap, int anisotropy)
{
    anisotropy = std::clamp(anisotropy, 1, 16);
    if (filter != TextureFilter::TRILINEAR || !mipmap) anisotropy = 1; // WebGPU: anisotropy requires all-linear
    uint32_t key = static_cast<uint32_t>(filter) | (static_cast<uint32_t>(wrap) << 4)
                 | (static_cast<uint32_t>(mipmap) << 8) | (static_cast<uint32_t>(anisotropy) << 12);
    auto it = m_samplers.find(key);
    if (it != m_samplers.end()) return it->second;

    wgpu::SamplerDescriptor desc;
    wgpu::AddressMode mode = wrap == TextureWrapMode::REPEAT ? wgpu::AddressMode::Repeat : wgpu::AddressMode::ClampToEdge;
    desc.addressModeU = desc.addressModeV = desc.addressModeW = mode;
    switch (filter)
    {
    case TextureFilter::NEAREST:
        desc.magFilter = desc.minFilter = wgpu::FilterMode::Nearest;
        desc.mipmapFilter = wgpu::MipmapFilterMode::Nearest;
        break;
    case TextureFilter::BILINEAR:
        desc.magFilter = desc.minFilter = wgpu::FilterMode::Linear;
        desc.mipmapFilter = wgpu::MipmapFilterMode::Nearest;
        break;
    case TextureFilter::TRILINEAR:
        desc.magFilter = desc.minFilter = wgpu::FilterMode::Linear;
        desc.mipmapFilter = wgpu::MipmapFilterMode::Linear;
        break;
    }
    desc.maxAnisotropy = static_cast<uint16_t>(anisotropy);
    wgpu::Sampler sampler = m_device.CreateSampler(&desc);
    m_samplers[key] = sampler;
    return sampler;
}

wgpu::Sampler CWebGPUTextureManager::GetComparisonSampler()
{
    if (!m_comparisonSampler)
    {
        // gl33: GL_COMPARE_REF_TO_TEXTURE, GL_LEQUAL, linear, clamp-to-border(1.0).
        // WebGPU has no border colour; out-of-range coords are rejected in calculate_shadow() instead.
        wgpu::SamplerDescriptor desc;
        desc.label = "shadow-compare";
        desc.addressModeU = desc.addressModeV = desc.addressModeW = wgpu::AddressMode::ClampToEdge;
        desc.magFilter = desc.minFilter = wgpu::FilterMode::Linear;
        desc.compare = wgpu::CompareFunction::LessEqual;
        m_comparisonSampler = m_device.CreateSampler(&desc);
    }
    return m_comparisonSampler;
}

void CWebGPUTextureManager::Upload(const WebGPUTextureEntry& entry, const RGBAImage& image, int x, int y)
{
    wgpu::TexelCopyTextureInfo dst;
    dst.texture = entry.texture;
    dst.origin = { static_cast<uint32_t>(x), static_cast<uint32_t>(y), 0 };
    wgpu::TexelCopyBufferLayout layout;
    layout.bytesPerRow = static_cast<uint32_t>(image.width) * 4;
    layout.rowsPerImage = static_cast<uint32_t>(image.height);
    wgpu::Extent3D size = { static_cast<uint32_t>(image.width), static_cast<uint32_t>(image.height), 1 };
    m_queue.WriteTexture(&dst, image.pixels.data(), image.pixels.size(), &layout, &size);
}

void CWebGPUTextureManager::UploadMips(const WebGPUTextureEntry& entry, const RGBAImage& level0)
{
    RGBAImage level = level0;
    for (uint32_t mip = 1; mip < entry.mipLevels; ++mip)
    {
        level = DownsampleRGBA(level);
        wgpu::TexelCopyTextureInfo dst;
        dst.texture = entry.texture;
        dst.mipLevel = mip;
        wgpu::TexelCopyBufferLayout layout;
        layout.bytesPerRow = static_cast<uint32_t>(level.width) * 4;
        layout.rowsPerImage = static_cast<uint32_t>(level.height);
        wgpu::Extent3D size = { static_cast<uint32_t>(level.width), static_cast<uint32_t>(level.height), 1 };
        m_queue.WriteTexture(&dst, level.pixels.data(), level.pixels.size(), &layout, &size);
    }
}

Texture CWebGPUTextureManager::Create(ImageData* data, const TextureCreateParams& params, int mipmapLevel, int anisotropy)
{
    RGBAImage image;
    if (!ConvertToRGBA(data, params.format, image) || image.width <= 0 || image.height <= 0)
    {
        GetLogger()->Error("WebGPU: invalid texture data");
        return Texture();
    }

    const bool alpha = image.alpha;
    WebGPUTextureEntry entry;
    entry.managed = true;
    entry.width = image.width;
    entry.height = image.height;
    entry.mipLevels = 1;
    if (params.mipmap)
    {
        uint32_t full = MipLevelCount(image.width, image.height);
        uint32_t wanted = params.filter == TextureFilter::TRILINEAR ? static_cast<uint32_t>(std::max(1, mipmapLevel)) : full;
        entry.mipLevels = std::clamp<uint32_t>(wanted, 1, full);
    }

    wgpu::TextureDescriptor desc;
    desc.size = { static_cast<uint32_t>(image.width), static_cast<uint32_t>(image.height), 1 };
    desc.format = wgpu::TextureFormat::RGBA8Unorm;
    desc.mipLevelCount = entry.mipLevels;
    // CopySrc/RenderAttachment: CopyFramebufferToTexture targets and future GPU mip generation
    desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst | wgpu::TextureUsage::CopySrc;
    entry.texture = m_device.CreateTexture(&desc);
    entry.view = entry.texture.CreateView();
    entry.sampler = GetSampler(params.filter, params.wrap, params.mipmap, anisotropy);

    Upload(entry, image, 0, 0);
    if (entry.mipLevels > 1)
    {
        UploadMips(entry, image);
        entry.cpuLevel0 = std::move(image);
    }

    Texture result;
    result.size = { entry.width, entry.height };
    result.originalSize = result.size;
    result.alpha = alpha;
    result.id = Register(std::move(entry));
    return result;
}

Texture CWebGPUTextureManager::CreateDepth(int width, int height)
{
    WebGPUTextureEntry entry;
    entry.width = width;
    entry.height = height;
    entry.depth = true;
    entry.format = wgpu::TextureFormat::Depth32Float;

    wgpu::TextureDescriptor desc;
    desc.label = "depth-texture";
    desc.size = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
    desc.format = entry.format;
    desc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
    entry.texture = m_device.CreateTexture(&desc);
    entry.view = entry.texture.CreateView();
    entry.sampler = GetComparisonSampler();

    Texture result;
    result.size = { width, height };
    result.originalSize = result.size;
    result.id = Register(std::move(entry));
    return result;
}

unsigned int CWebGPUTextureManager::Register(WebGPUTextureEntry entry)
{
    unsigned int id = m_nextId++;
    entry.serial = m_nextSerial++;
    m_textures[id] = std::move(entry);
    return id;
}

void CWebGPUTextureManager::Update(const Texture& texture, const glm::ivec2& offset, ImageData* data, TextureFormat format)
{
    auto it = m_textures.find(texture.id);
    if (it == m_textures.end() || it->second.depth) return;

    RGBAImage image;
    if (!ConvertToRGBA(data, format, image)) return;

    WebGPUTextureEntry& entry = it->second;
    // Clip to texture bounds (WebGPU validation rejects out-of-range writes)
    int w = std::min(image.width, entry.width - offset.x);
    int h = std::min(image.height, entry.height - offset.y);
    if (w <= 0 || h <= 0 || offset.x < 0 || offset.y < 0) return;
    if (w != image.width || h != image.height)
    {
        RGBAImage clipped;
        clipped.width = w;
        clipped.height = h;
        clipped.pixels.resize(static_cast<size_t>(w) * h * 4);
        for (int y = 0; y < h; ++y)
            std::copy_n(image.pixels.data() + static_cast<size_t>(y) * image.width * 4, w * 4,
                        clipped.pixels.data() + static_cast<size_t>(y) * w * 4);
        image = std::move(clipped);
    }

    Upload(entry, image, offset.x, offset.y);

    if (entry.mipLevels > 1 && !entry.cpuLevel0.pixels.empty())
    {
        // gl33 calls glGenerateMipmap after every sub-update: patch the CPU copy and rebuild
        for (int y = 0; y < h; ++y)
            std::copy_n(image.pixels.data() + static_cast<size_t>(y) * w * 4, w * 4,
                        entry.cpuLevel0.pixels.data() + (static_cast<size_t>(offset.y + y) * entry.width + offset.x) * 4);
        UploadMips(entry, entry.cpuLevel0);
    }
}

void CWebGPUTextureManager::Destroy(unsigned int id)
{
    auto it = m_textures.find(id);
    if (it == m_textures.end()) return;
    it->second.texture.Destroy();
    m_textures.erase(it);
    ++m_epoch;
}

void CWebGPUTextureManager::DestroyAll()
{
    // Like gl33 m_allTextures: only textures made by Create(); depth textures and
    // framebuffer attachments stay alive (their owners destroy them).
    for (auto it = m_textures.begin(); it != m_textures.end();)
    {
        if (it->second.managed)
        {
            it->second.texture.Destroy();
            it = m_textures.erase(it);
        }
        else
            ++it;
    }
    ++m_epoch;
}

const WebGPUTextureEntry& CWebGPUTextureManager::Get(unsigned int id) const
{
    const WebGPUTextureEntry* e = Find(id);
    return e != nullptr ? *e : m_white;
}

const WebGPUTextureEntry* CWebGPUTextureManager::Find(unsigned int id) const
{
    if (id == 0) return nullptr;
    auto it = m_textures.find(id);
    return it == m_textures.end() ? nullptr : &it->second;
}

} // namespace Gfx
