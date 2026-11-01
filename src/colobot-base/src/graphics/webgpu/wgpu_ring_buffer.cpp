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

#include "graphics/webgpu/wgpu_ring_buffer.h"

#include "graphics/webgpu/wgpu_util.h"

#include <algorithm>
#include <cstring>

namespace Gfx
{

CWebGPURingBuffer::CWebGPURingBuffer(wgpu::Device device, wgpu::BufferUsage usage, uint64_t initialSize, uint64_t alignment, const char* label)
    : m_device(std::move(device)), m_usage(usage | wgpu::BufferUsage::CopyDst), m_alignment(alignment), m_label(label)
{
    AddChunk(AlignUp(initialSize, 256));
}

void CWebGPURingBuffer::AddChunk(uint64_t size)
{
    Chunk chunk;
    wgpu::BufferDescriptor desc;
    desc.label = wgpu::StringView(std::string_view(m_label));
    desc.size = size;
    desc.usage = m_usage;
    chunk.buffer = m_device.CreateBuffer(&desc);
    chunk.staging.resize(size);
    m_chunks.push_back(std::move(chunk));
}

void CWebGPURingBuffer::BeginFrame()
{
    uint64_t total = 0;
    for (auto& c : m_chunks) total += c.used;
    m_peak = std::max(m_peak, total);

    if (m_chunks.size() > 1)
    {
        // Merge into one chunk that fits last frame's peak with headroom
        uint64_t size = AlignUp(m_peak + m_peak / 2, 256);
        m_chunks.clear();
        AddChunk(size);
    }
    m_chunks.back().used = 0;
}

RingAllocation CWebGPURingBuffer::Allocate(uint64_t size)
{
    Chunk* chunk = &m_chunks.back();
    uint64_t offset = AlignUp(chunk->used, m_alignment);
    if (offset + size > chunk->staging.size())
    {
        uint64_t newSize = AlignUp(std::max<uint64_t>(chunk->staging.size() * 2, size + m_alignment), 256);
        AddChunk(newSize);
        chunk = &m_chunks.back();
        offset = 0;
    }
    chunk->used = offset + size;

    RingAllocation result;
    result.buffer = chunk->buffer;
    result.offset = offset;
    result.data = chunk->staging.data() + offset;
    result.size = size;
    return result;
}

RingAllocation CWebGPURingBuffer::Upload(const void* data, uint64_t size)
{
    RingAllocation a = Allocate(size);
    std::memcpy(a.data, data, size);
    return a;
}

void CWebGPURingBuffer::Flush(const wgpu::Queue& queue)
{
    for (auto& c : m_chunks)
    {
        if (c.used == 0) continue;
        queue.WriteBuffer(c.buffer, 0, c.staging.data(), AlignUp(c.used, 4));
    }
}

} // namespace Gfx
