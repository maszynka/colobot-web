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
 * \file graphics/webgpu/wgpu_texture.h
 * \brief Texture registry: Gfx::Texture::id -> GPU texture/view/sampler (colobot-web WP4)
 */

#pragma once

#include "graphics/core/texture.h"
#include "graphics/webgpu/wgpu_util.h"

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <map>
#include <unordered_map>

namespace Gfx
{

struct WebGPUTextureEntry
{
    wgpu::Texture texture;
    wgpu::TextureView view;
    wgpu::Sampler sampler;          //!< filtering sampler, or comparison sampler for depth textures
    wgpu::TextureFormat format = wgpu::TextureFormat::RGBA8Unorm;
    int width = 0;
    int height = 0;
    uint32_t mipLevels = 1;
    bool depth = false;
    bool managed = false;           //!< created by Create(); removed by DestroyAll()
    //! Unique, never reused; key renderer bind-group caches on this (ids may be reused by GL-style code)
    uint64_t serial = 0;
    //! CPU copy of level 0 (only for mipmapped textures, needed to rebuild mips after UpdateTexture)
    RGBAImage cpuLevel0;
};

class CWebGPUTextureManager
{
public:
    CWebGPUTextureManager() = default;
    void Init(wgpu::Device device, wgpu::Queue queue);
    void Shutdown();

    Texture Create(ImageData* data, const TextureCreateParams& params, int mipmapLevel, int anisotropy);
    Texture CreateDepth(int width, int height);
    //! Registers a texture created elsewhere (framebuffer attachments); returns its id
    unsigned int Register(WebGPUTextureEntry entry);
    void Update(const Texture& texture, const glm::ivec2& offset, ImageData* data, TextureFormat format);
    void Destroy(unsigned int id);
    void DestroyAll();

    //! Never null: unknown / 0 ids return the 1x1 white texture (gl33 UI semantics)
    const WebGPUTextureEntry& Get(unsigned int id) const;
    const WebGPUTextureEntry* Find(unsigned int id) const;
    const WebGPUTextureEntry& White() const { return m_white; }

    //! Bumped on every destroy; renderers drop bind-group caches when it changes
    uint64_t Epoch() const { return m_epoch; }

    wgpu::Sampler GetSampler(TextureFilter filter, TextureWrapMode wrap, bool mipmap, int anisotropy);
    wgpu::Sampler GetComparisonSampler();

private:
    void Upload(const WebGPUTextureEntry& entry, const RGBAImage& image, int x, int y);
    void UploadMips(const WebGPUTextureEntry& entry, const RGBAImage& level0);

    wgpu::Device m_device;
    wgpu::Queue m_queue;
    std::unordered_map<unsigned int, WebGPUTextureEntry> m_textures;
    std::map<uint32_t, wgpu::Sampler> m_samplers;
    wgpu::Sampler m_comparisonSampler;
    WebGPUTextureEntry m_white;
    unsigned int m_nextId = 1;
    uint64_t m_nextSerial = 1;
    uint64_t m_epoch = 0;
};

} // namespace Gfx
