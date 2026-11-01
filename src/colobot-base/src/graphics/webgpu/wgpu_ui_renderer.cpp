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

#include "graphics/webgpu/wgpu_ui_renderer.h"

#include "graphics/webgpu/wgpu_device.h"
#include "graphics/webgpu/wgpu_util.h"

#include "graphics/core/material.h"
#include "graphics/core/transparency.h"

#include "common/logger.h"

#include "wgsl_sources.h"

#include <glm/ext.hpp>

#include <numeric>

namespace Gfx
{

CWebGPUUIRenderer::CWebGPUUIRenderer(CWebGPUDevice* device)
    : m_device(device)
{
    GetLogger()->Info("Creating CWebGPUUIRenderer");
    const wgpu::Device& dev = m_device->GetWGPUDevice();

    m_shader = CreateShaderModule(dev, "ui", { WGSL::preamble, WGSL::ui });

    // group(1): UIUniforms with dynamic offset
    {
        wgpu::BindGroupLayoutEntry entry;
        entry.binding = 0;
        entry.visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
        entry.buffer.type = wgpu::BufferBindingType::Uniform;
        entry.buffer.hasDynamicOffset = true;
        entry.buffer.minBindingSize = sizeof(Uniforms);
        wgpu::BindGroupLayoutDescriptor desc;
        desc.label = "ui-group1";
        desc.entryCount = 1;
        desc.entries = &entry;
        m_uniformLayout = dev.CreateBindGroupLayout(&desc);
    }
    // group(2): texture + sampler
    {
        wgpu::BindGroupLayoutEntry entries[2];
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Fragment;
        entries[0].texture.sampleType = wgpu::TextureSampleType::Float;
        entries[0].texture.viewDimension = wgpu::TextureViewDimension::e2D;
        entries[1].binding = 1;
        entries[1].visibility = wgpu::ShaderStage::Fragment;
        entries[1].sampler.type = wgpu::SamplerBindingType::Filtering;
        wgpu::BindGroupLayoutDescriptor desc;
        desc.label = "ui-group2";
        desc.entryCount = 2;
        desc.entries = entries;
        m_textureLayout = dev.CreateBindGroupLayout(&desc);
    }
    {
        wgpu::BindGroupLayout layouts[3] = { m_device->EmptyBindGroupLayout(), m_uniformLayout, m_textureLayout };
        wgpu::PipelineLayoutDescriptor desc;
        desc.label = "ui-layout";
        desc.bindGroupLayoutCount = 3;
        desc.bindGroupLayouts = layouts;
        m_pipelineLayout = dev.CreatePipelineLayout(&desc);
    }

    m_uniforms.projectionMatrix = glm::ortho(0.0f, +1.0f, 0.0f, +1.0f);
    m_uniforms.color = { 1.0f, 1.0f, 1.0f, 1.0f };

    GetLogger()->Info("CWebGPUUIRenderer created successfully");
}

CWebGPUUIRenderer::~CWebGPUUIRenderer() = default;

void CWebGPUUIRenderer::SetProjection(float left, float right, float bottom, float top)
{
    m_uniforms.projectionMatrix = glm::ortho(left, right, bottom, top);
    m_uniformsDirty = true;
}

void CWebGPUUIRenderer::SetTexture(const Texture& texture)
{
    m_currentTexture = texture.id;
}

void CWebGPUUIRenderer::SetColor(const glm::vec4& color)
{
    m_uniforms.color = color;
    m_uniformsDirty = true;
}

void CWebGPUUIRenderer::SetTransparency(TransparencyMode mode)
{
    m_device->SetTransparency(mode);
}

Vertex2D* CWebGPUUIRenderer::BeginPrimitive(PrimitiveType type, int count)
{
    return BeginPrimitives(type, 1, &count);
}

Vertex2D* CWebGPUUIRenderer::BeginPrimitives(PrimitiveType type, int drawCount, const int* counts)
{
    m_counts.assign(counts, counts + drawCount);
    int total = std::accumulate(m_counts.begin(), m_counts.end(), 0);
    m_vertices.assign(static_cast<size_t>(std::max(total, 0)), Vertex2D{});
    m_type = type;
    m_mapped = true;
    return m_vertices.data();
}

wgpu::RenderPipeline CWebGPUUIRenderer::CreatePipeline(const PipelineKey& key)
{
    wgpu::VertexAttribute attributes[3];
    attributes[0].format = wgpu::VertexFormat::Float32x2;
    attributes[0].offset = offsetof(Vertex2D, position);
    attributes[0].shaderLocation = 0;
    attributes[1].format = wgpu::VertexFormat::Float32x2;
    attributes[1].offset = offsetof(Vertex2D, uv);
    attributes[1].shaderLocation = 1;
    attributes[2].format = wgpu::VertexFormat::Unorm8x4;
    attributes[2].offset = offsetof(Vertex2D, color);
    attributes[2].shaderLocation = 2;

    wgpu::VertexBufferLayout vbLayout;
    vbLayout.arrayStride = sizeof(Vertex2D);
    vbLayout.attributeCount = 3;
    vbLayout.attributes = attributes;

    wgpu::RenderPipelineDescriptor desc;
    desc.label = "ui";
    desc.layout = m_pipelineLayout;
    desc.vertex.module = m_shader;
    desc.vertex.entryPoint = "vs_main";
    desc.vertex.bufferCount = 1;
    desc.vertex.buffers = &vbLayout;

    CWebGPUDevice::FixedFunction ff;
    m_device->FillFixedFunction(key, desc, ff);
    ff.fragment.module = m_shader;
    ff.fragment.entryPoint = "fs_main";
    desc.fragment = &ff.fragment;

    return m_device->GetWGPUDevice().CreateRenderPipeline(&desc);
}

void CWebGPUUIRenderer::BindUniforms(wgpu::RenderPassEncoder& pass)
{
    if (m_uniformsDirty || m_uniformFrame != m_device->FrameIndex())
    {
        RingAllocation a = m_device->UniformRing().Upload(&m_uniforms, sizeof(Uniforms));
        m_uniformOffset = static_cast<uint32_t>(a.offset);
        m_uniformBuffer = a.buffer;
        m_uniformFrame = m_device->FrameIndex();
        m_uniformsDirty = false;
    }
    if (!m_uniformGroup || m_uniformGroupBuffer != m_uniformBuffer.Get())
    {
        wgpu::BindGroupEntry entry;
        entry.binding = 0;
        entry.buffer = m_uniformBuffer;
        entry.size = sizeof(Uniforms);
        wgpu::BindGroupDescriptor desc;
        desc.layout = m_uniformLayout;
        desc.entryCount = 1;
        desc.entries = &entry;
        m_uniformGroup = m_device->GetWGPUDevice().CreateBindGroup(&desc);
        m_uniformGroupBuffer = m_uniformBuffer.Get();
    }
    pass.SetBindGroup(1, m_uniformGroup, 1, &m_uniformOffset);
}

void CWebGPUUIRenderer::BindTexture(wgpu::RenderPassEncoder& pass)
{
    CWebGPUTextureManager& textures = m_device->Textures();
    if (m_textureEpoch != textures.Epoch())
    {
        m_textureGroups.clear();
        m_textureEpoch = textures.Epoch();
    }

    const WebGPUTextureEntry* entry = textures.Find(m_currentTexture);
    if (entry == nullptr || entry->depth) entry = &textures.White();

    auto it = m_textureGroups.find(entry->serial);
    if (it == m_textureGroups.end())
    {
        wgpu::BindGroupEntry entries[2];
        entries[0].binding = 0;
        entries[0].textureView = entry->view;
        entries[1].binding = 1;
        entries[1].sampler = entry->sampler;
        wgpu::BindGroupDescriptor desc;
        desc.layout = m_textureLayout;
        desc.entryCount = 2;
        desc.entries = entries;
        it = m_textureGroups.emplace(entry->serial, m_device->GetWGPUDevice().CreateBindGroup(&desc)).first;
    }
    pass.SetBindGroup(2, it->second);
}

bool CWebGPUUIRenderer::EndPrimitive()
{
    if (!m_mapped) return false;
    m_mapped = false;

    // gl33: UI always draws without depth test and culling
    m_device->SetDepthTest(false);
    m_device->SetCullFace(CullFace::NONE);

    wgpu::RenderPassEncoder* pass = m_device->Passes().GetPass();
    if (pass == nullptr || m_vertices.empty()) return true; // outside BeginScene/EndScene: dropped

    // Build draw list: native topologies draw sub-ranges, others are converted to lists
    wgpu::PrimitiveTopology topology = NativeTopology(m_type);
    const Vertex2D* data = m_vertices.data();
    size_t vertexCount = m_vertices.size();
    if (topology == wgpu::PrimitiveTopology::Undefined)
    {
        m_converted.clear();
        size_t first = 0;
        for (int count : m_counts)
        {
            ConvertPrimitives(m_type, m_vertices.data() + first, count, m_converted, 0.001f);
            first += count;
        }
        topology = ConvertedTopology(m_type);
        data = m_converted.data();
        vertexCount = m_converted.size();
        m_counts.assign(1, static_cast<int>(vertexCount));
        if (vertexCount == 0) return true;
    }

    RingAllocation vb = m_device->VertexRing().Upload(data, vertexCount * sizeof(Vertex2D));

    PipelineKey key = m_device->MakePipelineKey(RendererId::UI, topology);
    const wgpu::RenderPipeline& pipeline = m_device->Pipelines().Get(key,
        [this](const PipelineKey& k) { return CreatePipeline(k); });

    pass->SetPipeline(pipeline);
    pass->SetBindGroup(0, m_device->EmptyBindGroup());
    BindUniforms(*pass);
    BindTexture(*pass);
    pass->SetVertexBuffer(0, vb.buffer, vb.offset, vb.size);

    uint32_t first = 0;
    for (int count : m_counts)
    {
        if (count > 0) pass->Draw(static_cast<uint32_t>(count), 1, first, 0);
        first += static_cast<uint32_t>(std::max(count, 0));
    }
    return true;
}

} // namespace Gfx
