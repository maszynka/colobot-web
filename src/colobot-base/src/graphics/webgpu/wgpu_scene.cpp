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

#include "graphics/webgpu/wgpu_scene.h"

#include "graphics/webgpu/wgpu_device.h"

#include <algorithm>

namespace Gfx
{

wgpu::BindGroupLayout CWebGPUSceneBindings::CreateLayout(const wgpu::Device& device)
{
    wgpu::BindGroupLayoutEntry entries[4];

    entries[0].binding = 0; // LightUniforms
    entries[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
    entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
    entries[0].buffer.hasDynamicOffset = true;
    entries[0].buffer.minBindingSize = sizeof(LightUniforms);

    entries[1].binding = 1; // ShadowUniforms
    entries[1].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
    entries[1].buffer.type = wgpu::BufferBindingType::Uniform;
    entries[1].buffer.hasDynamicOffset = true;
    entries[1].buffer.minBindingSize = sizeof(ShadowUniforms);

    entries[2].binding = 2; // shadow map
    entries[2].visibility = wgpu::ShaderStage::Fragment | wgpu::ShaderStage::Vertex;
    entries[2].texture.sampleType = wgpu::TextureSampleType::Depth;
    entries[2].texture.viewDimension = wgpu::TextureViewDimension::e2D;

    entries[3].binding = 3; // comparison sampler
    entries[3].visibility = wgpu::ShaderStage::Fragment | wgpu::ShaderStage::Vertex;
    entries[3].sampler.type = wgpu::SamplerBindingType::Comparison;

    wgpu::BindGroupLayoutDescriptor desc;
    desc.label = "scene-group0";
    desc.entryCount = 4;
    desc.entries = entries;
    return device.CreateBindGroupLayout(&desc);
}

void CWebGPUSceneBindings::SetShadowParams(int count, const ShadowParam* params)
{
    count = std::clamp(count, 0, 4);
    m_shadow.regions = count;
    for (int i = 0; i < count; ++i)
    {
        m_shadow.params[i].transform = params[i].matrix;
        m_shadow.params[i].uvOffset = params[i].uv_offset;
        m_shadow.params[i].uvScale = params[i].uv_scale;
    }
    m_dirty = true;
}

void CWebGPUSceneBindings::Bind(wgpu::RenderPassEncoder& pass)
{
    CWebGPURingBuffer& ring = m_device->UniformRing();

    if (m_dirty || m_frame != m_device->FrameIndex())
    {
        RingAllocation light = ring.Upload(&m_light, sizeof(LightUniforms));
        RingAllocation shadow = ring.Upload(&m_shadow, sizeof(ShadowUniforms));
        m_offsets[0] = static_cast<uint32_t>(light.offset);
        m_offsets[1] = static_cast<uint32_t>(shadow.offset);
        m_frame = m_device->FrameIndex();
        m_dirty = false;

        // Both allocations come from the same chunk unless the ring just grew; in that case
        // the second one is in the new chunk -> re-upload the first there as well.
        if (light.buffer.Get() != shadow.buffer.Get())
        {
            light = ring.Upload(&m_light, sizeof(LightUniforms));
            m_offsets[0] = static_cast<uint32_t>(light.offset);
        }
        m_allocBuffer = shadow.buffer;
    }

    const WebGPUTextureEntry* shadowMap = m_device->Textures().Find(m_shadowMap);
    if (shadowMap == nullptr || !shadowMap->depth) shadowMap = &m_device->DummyDepth();

    WGPUBuffer buffer = m_allocBuffer.Get();
    if (!m_bindGroup || m_bgBuffer != buffer || m_bgShadowSerial != shadowMap->serial)
    {
        wgpu::BindGroupEntry entries[4];
        entries[0].binding = 0;
        entries[0].buffer = m_allocBuffer;
        entries[0].size = sizeof(LightUniforms);
        entries[1].binding = 1;
        entries[1].buffer = m_allocBuffer;
        entries[1].size = sizeof(ShadowUniforms);
        entries[2].binding = 2;
        entries[2].textureView = shadowMap->view;
        entries[3].binding = 3;
        entries[3].sampler = m_device->Textures().GetComparisonSampler();

        wgpu::BindGroupDescriptor desc;
        desc.layout = m_device->SceneBindGroupLayout();
        desc.entryCount = 4;
        desc.entries = entries;
        m_bindGroup = m_device->GetWGPUDevice().CreateBindGroup(&desc);
        m_bgBuffer = buffer;
        m_bgShadowSerial = shadowMap->serial;
    }

    pass.SetBindGroup(0, m_bindGroup, 2, m_offsets);
}

} // namespace Gfx
