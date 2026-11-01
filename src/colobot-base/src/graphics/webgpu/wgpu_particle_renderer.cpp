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

#include "graphics/webgpu/wgpu_particle_renderer.h"

#include "graphics/webgpu/wgpu_device.h"
#include "graphics/webgpu/wgpu_util.h"

#include "graphics/core/material.h"
#include "graphics/core/transparency.h"

#include "common/logger.h"

#include "wgsl_sources.h"

#include <cstddef>

namespace Gfx
{

static wgpu::BindGroupLayout MakeUniformLayout(const wgpu::Device& dev, const char* label, uint64_t size)
{
    wgpu::BindGroupLayoutEntry entry;
    entry.binding = 0;
    entry.visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
    entry.buffer.type = wgpu::BufferBindingType::Uniform;
    entry.buffer.hasDynamicOffset = true;
    entry.buffer.minBindingSize = size;
    wgpu::BindGroupLayoutDescriptor desc;
    desc.label = label;
    desc.entryCount = 1;
    desc.entries = &entry;
    return dev.CreateBindGroupLayout(&desc);
}

static wgpu::BindGroupLayout MakeTextureLayout(const wgpu::Device& dev, const char* label)
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
    desc.label = label;
    desc.entryCount = 2;
    desc.entries = entries;
    return dev.CreateBindGroupLayout(&desc);
}

static wgpu::BindGroup GetTextureGroup(CWebGPUDevice* device, const wgpu::BindGroupLayout& layout,
                                       std::unordered_map<uint64_t, wgpu::BindGroup>& cache, uint64_t& epoch, unsigned int id)
{
    CWebGPUTextureManager& textures = device->Textures();
    if (epoch != textures.Epoch())
    {
        cache.clear();
        epoch = textures.Epoch();
    }
    const WebGPUTextureEntry& tex = textures.Get(id);
    auto it = cache.find(tex.serial);
    if (it != cache.end()) return it->second;
    wgpu::BindGroupEntry entries[2];
    entries[0].binding = 0;
    entries[0].textureView = tex.view;
    entries[1].binding = 1;
    entries[1].sampler = tex.sampler;
    wgpu::BindGroupDescriptor desc;
    desc.layout = layout;
    desc.entryCount = 2;
    desc.entries = entries;
    wgpu::BindGroup group = device->GetWGPUDevice().CreateBindGroup(&desc);
    cache[tex.serial] = group;
    return group;
}

static wgpu::PipelineLayout MakePipelineLayout(CWebGPUDevice* device, const char* label,
                                               const wgpu::BindGroupLayout& g1, const wgpu::BindGroupLayout& g2)
{
    wgpu::BindGroupLayout layouts[3] = { device->EmptyBindGroupLayout(), g1, g2 };
    wgpu::PipelineLayoutDescriptor desc;
    desc.label = label;
    desc.bindGroupLayoutCount = 3;
    desc.bindGroupLayouts = layouts;
    return device->GetWGPUDevice().CreatePipelineLayout(&desc);
}

} // namespace Gfx
using namespace Gfx;

namespace Gfx
{

CWebGPUParticleRenderer::CWebGPUParticleRenderer(CWebGPUDevice* device)
    : m_device(device)
{
    GetLogger()->Info("Creating CWebGPUParticleRenderer");
    const wgpu::Device& dev = m_device->GetWGPUDevice();
    m_shader = CreateShaderModule(dev, "particle", { WGSL::preamble, WGSL::shadow, WGSL::particle });
    m_uniformLayout = MakeUniformLayout(dev, "particle-group1", sizeof(Uniforms));
    m_textureLayout = MakeTextureLayout(dev, "particle-group2");
    m_pipelineLayout = MakePipelineLayout(m_device, "particle-layout", m_uniformLayout, m_textureLayout);
}

CWebGPUParticleRenderer::~CWebGPUParticleRenderer() = default;

void CWebGPUParticleRenderer::Begin()
{
    m_texture = 0;
    m_color = { 1.0f, 1.0f, 1.0f, 1.0f };
    m_uniformsDirty = true;

    m_device->SetDepthTest(true);
    m_device->SetDepthMask(false);
    m_device->SetCullFace(CullFace::NONE);
}

void CWebGPUParticleRenderer::End()
{
    m_texture = 0;
}

void CWebGPUParticleRenderer::SetProjectionMatrix(const glm::mat4& matrix)
{
    m_projectionMatrix = matrix;
    m_uniformsDirty = true;
}

void CWebGPUParticleRenderer::SetViewMatrix(const glm::mat4& matrix)
{
    glm::mat4 scale(1.0f);
    scale[2][2] = -1.0f;
    m_viewMatrix = scale * matrix;
    m_uniformsDirty = true;
}

void CWebGPUParticleRenderer::SetModelMatrix(const glm::mat4& matrix)
{
    m_modelMatrix = matrix;
    m_uniformsDirty = true;
}

void CWebGPUParticleRenderer::SetColor(const glm::vec4& color)
{
    m_color = color;
    m_uniformsDirty = true;
}

void CWebGPUParticleRenderer::SetTexture(const Texture& texture)
{
    m_texture = texture.id;
}

void CWebGPUParticleRenderer::SetTransparency(TransparencyMode mode)
{
    m_device->SetTransparency(mode);
}

wgpu::RenderPipeline CWebGPUParticleRenderer::CreatePipeline(const PipelineKey& key)
{
    wgpu::VertexAttribute attributes[3];
    attributes[0].format = wgpu::VertexFormat::Float32x3;
    attributes[0].offset = offsetof(VertexParticle, position);
    attributes[0].shaderLocation = 0;
    attributes[1].format = wgpu::VertexFormat::Unorm8x4;
    attributes[1].offset = offsetof(VertexParticle, color);
    attributes[1].shaderLocation = 1;
    attributes[2].format = wgpu::VertexFormat::Float32x2;
    attributes[2].offset = offsetof(VertexParticle, uv);
    attributes[2].shaderLocation = 2;

    wgpu::VertexBufferLayout vbLayout;
    vbLayout.arrayStride = sizeof(VertexParticle);
    vbLayout.attributeCount = 3;
    vbLayout.attributes = attributes;

    wgpu::RenderPipelineDescriptor desc;
    desc.label = "particle";
    desc.layout = m_pipelineLayout;
    desc.vertex.module = m_shader;
    desc.vertex.entryPoint = "vs_main";
    desc.vertex.bufferCount = 1;
    desc.vertex.buffers = &vbLayout;

    CWebGPUDevice::FixedFunction ff;
    m_device->FillFixedFunction(key, desc, ff);
    ff.fragment.module = m_shader;
    ff.fragment.entryPoint = "fs_main";
    if (key.colorFormat != 0) desc.fragment = &ff.fragment;

    return m_device->GetWGPUDevice().CreateRenderPipeline(&desc);
}

void CWebGPUParticleRenderer::BindUniforms(wgpu::RenderPassEncoder& pass)
{
    if (m_uniformsDirty || m_uniformFrame != m_device->FrameIndex())
    {
        Uniforms u;
        u.mvp = m_projectionMatrix * m_viewMatrix * m_modelMatrix;
        u.color = m_color;
        RingAllocation a = m_device->UniformRing().Upload(&u, sizeof(Uniforms));
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

void CWebGPUParticleRenderer::BindTexture(wgpu::RenderPassEncoder& pass)
{
    pass.SetBindGroup(2, GetTextureGroup(m_device, m_textureLayout, m_textureGroups, m_textureEpoch, m_texture));
}

void CWebGPUParticleRenderer::DrawParticle(PrimitiveType type, int count, const VertexParticle* vertices)
{
    if (count <= 0 || vertices == nullptr) return;

    wgpu::RenderPassEncoder* pass = m_device->Passes().GetPass();
    if (pass == nullptr) return;

    wgpu::PrimitiveTopology topology = NativeTopology(type);
    const VertexParticle* data = vertices;
    size_t vertexCount = static_cast<size_t>(count);
    if (topology == wgpu::PrimitiveTopology::Undefined || type == PrimitiveType::POINTS)
    {
        m_converted.clear();
        ConvertPrimitives(type, vertices, count, m_converted, 0.05f);
        topology = ConvertedTopology(type);
        data = m_converted.data();
        vertexCount = m_converted.size();
        if (vertexCount == 0) return;
    }

    RingAllocation vb = m_device->VertexRing().Upload(data, vertexCount * sizeof(VertexParticle));

    PipelineKey key = m_device->MakePipelineKey(RendererId::Particle, topology);
    const wgpu::RenderPipeline& pipeline = m_device->Pipelines().Get(key,
        [this](const PipelineKey& k) { return CreatePipeline(k); });

    pass->SetPipeline(pipeline);
    pass->SetBindGroup(0, m_device->EmptyBindGroup());
    BindUniforms(*pass);
    BindTexture(*pass);
    pass->SetVertexBuffer(0, vb.buffer, vb.offset, vb.size);
    pass->Draw(static_cast<uint32_t>(vertexCount), 1, 0, 0);
}

} // namespace Gfx
