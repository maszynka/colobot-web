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

#include "graphics/null/null_device.h"

#include "common/image.h"

#include "graphics/core/vertex.h"

#include <SDL.h>

#include <numeric>

namespace Gfx
{

namespace
{

class CNullFrameBufferPixels : public CFrameBufferPixels
{
public:
    explicit CNullFrameBufferPixels(size_t size) : m_pixels(size, 0) {}
    void* GetPixelsData() override { return m_pixels.data(); }
private:
    std::vector<unsigned char> m_pixels;
};

} // namespace

Vertex2D* CNullUIRenderer::BeginPrimitive(PrimitiveType, int count)
{
    m_buffer.assign(count > 0 ? count : 0, Vertex2D{});
    return m_buffer.data();
}

Vertex2D* CNullUIRenderer::BeginPrimitives(PrimitiveType, int drawCount, const int* counts)
{
    int total = 0;
    for (int i = 0; i < drawCount; ++i) total += counts[i];
    m_buffer.assign(total > 0 ? total : 0, Vertex2D{});
    return m_buffer.data();
}

CNullDevice::CNullDevice(const DeviceConfig& config)
    : m_config(config)
{
}

CNullDevice::~CNullDevice() = default;

bool CNullDevice::Create()
{
    FramebufferParams params;
    params.width = m_config.size.x;
    params.height = m_config.size.y;
    m_framebuffers["default"] = std::make_unique<CNullFramebuffer>(params, true);
    return true;
}

void CNullDevice::Destroy()
{
    m_framebuffers.clear();
}

void CNullDevice::ConfigChanged(const DeviceConfig& newConfig)
{
    m_config = newConfig;
}

Texture CNullDevice::MakeTexture(int width, int height, bool alpha)
{
    Texture texture;
    texture.id = m_nextTextureId++;
    texture.size = { width, height };
    texture.originalSize = texture.size;
    texture.alpha = alpha;
    return texture;
}

Texture CNullDevice::CreateTexture(CImage* image, const TextureCreateParams& params)
{
    if (image == nullptr) return Texture{};
    glm::ivec2 size = image->GetSize();
    return MakeTexture(size.x, size.y, params.format != TextureFormat::RGB);
}

Texture CNullDevice::CreateTexture(ImageData* data, const TextureCreateParams& params)
{
    if (data == nullptr || data->surface == nullptr) return Texture{};
    return MakeTexture(data->surface->w, data->surface->h, params.format != TextureFormat::RGB);
}

Texture CNullDevice::CreateDepthTexture(int width, int height, int)
{
    return MakeTexture(width, height, false);
}

CVertexBuffer* CNullDevice::CreateVertexBuffer(PrimitiveType primitiveType, const Vertex3D* vertices, int vertexCount)
{
    auto buffer = new CNullVertexBuffer(primitiveType, vertexCount);
    if (vertices != nullptr && vertexCount > 0) buffer->SetData(vertices, 0, vertexCount);
    return buffer;
}

void CNullDevice::DestroyVertexBuffer(CVertexBuffer* buffer)
{
    delete buffer;
}

std::unique_ptr<CFrameBufferPixels> CNullDevice::GetFrameBufferPixels() const
{
    size_t size = static_cast<size_t>(m_config.size.x) * m_config.size.y * 4;
    return std::make_unique<CNullFrameBufferPixels>(size);
}

CFramebuffer* CNullDevice::GetFramebuffer(std::string name)
{
    auto it = m_framebuffers.find(name);
    return it == m_framebuffers.end() ? nullptr : it->second.get();
}

CFramebuffer* CNullDevice::CreateFramebuffer(std::string name, const FramebufferParams& params)
{
    if (name == "default") return nullptr;
    auto& fb = m_framebuffers[name];
    fb = std::make_unique<CNullFramebuffer>(params);
    return fb.get();
}

void CNullDevice::DeleteFramebuffer(std::string name)
{
    if (name == "default") return;
    m_framebuffers.erase(name);
}

} // namespace Gfx
