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
 * \file graphics/webgpu/wgpu_ring_buffer.h
 * \brief Per-frame linear allocator for streamed vertex / uniform data (colobot-web WP4)
 *
 * Data is staged on the CPU during the frame and uploaded with one queue.WriteBuffer
 * per chunk in Flush(), right before the frame's command buffer is submitted.
 * queue.WriteBuffer is ordered before later submits and after earlier ones, so a
 * single GPU buffer can safely be reused every frame.
 *
 * If a frame overflows the current chunk, a new (bigger) chunk is opened; previously
 * returned allocations stay valid until the end of the frame. Next frame all chunks
 * are merged into one buffer sized for the peak usage.
 */

#pragma once

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace Gfx
{

struct RingAllocation
{
    wgpu::Buffer buffer;     //!< GPU buffer to bind
    uint64_t offset = 0;     //!< offset in that buffer (dynamic offset for uniforms)
    void* data = nullptr;    //!< CPU pointer to write the data into (valid until Flush)
    uint64_t size = 0;
};

class CWebGPURingBuffer
{
public:
    CWebGPURingBuffer(wgpu::Device device, wgpu::BufferUsage usage, uint64_t initialSize, uint64_t alignment, const char* label);

    //! Starts a new frame (all previous allocations become invalid)
    void BeginFrame();
    //! Allocate size bytes aligned to the ring's alignment
    RingAllocation Allocate(uint64_t size);
    //! Convenience: allocate and copy
    RingAllocation Upload(const void* data, uint64_t size);
    //! Uploads all staged data via queue.WriteBuffer (call before submit)
    void Flush(const wgpu::Queue& queue);

    //! Buffer of the current chunk (bind groups referencing the ring should be keyed on this)
    const wgpu::Buffer& CurrentBuffer() const { return m_chunks.back().buffer; }

private:
    struct Chunk
    {
        wgpu::Buffer buffer;
        std::vector<uint8_t> staging;
        uint64_t used = 0;
    };
    void AddChunk(uint64_t size);

    wgpu::Device m_device;
    wgpu::BufferUsage m_usage;
    uint64_t m_alignment;
    const char* m_label;
    std::vector<Chunk> m_chunks;
    uint64_t m_peak = 0;
};

} // namespace Gfx
