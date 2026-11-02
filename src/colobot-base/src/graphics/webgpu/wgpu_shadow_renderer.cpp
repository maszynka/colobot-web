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

#include "graphics/webgpu/wgpu_shadow_renderer.h"

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

CWebGPUShadowRenderer::CWebGPUShadowRenderer(CWebGPUDevice* device)
    : m_device(device)
{
    GetLogger()->Info("Creating CWebGPUShadowRenderer");
    const wgpu::Device& dev = m_device->GetWGPUDevice();
    m_shader = CreateShaderModule(dev, "shadow", { WGSL::preamble, WGSL::shadow_pass });
    m_uniformLayout = MakeUniformLayout(dev, "shadow-group1", sizeof(glm::mat4));
    m_textureLayout = MakeTextureLayout(dev, "shadow-group2");
    m_pipelineLayout = MakePipelineLayout(m_device, "shadow-layout", m_uniformLayout, m_textureLayout);
}

CWebGPUShadowRenderer::~CWebGPUShadowRenderer() = default;

void CWebGPUShadowRenderer::Begin()
{
    // Real (WP4): switch the pass target to the shadow map (depth-only, cleared), like gl33 Begin().
    const WebGPUTextureEntry* map = m_device->Textures().Find(m_shadowMap);
    if (map == nullptr || !map->depth || !m_device->Passes().InFrame()) return;
    m_previousTarget = m_device->Passes().GetTarget();
    RenderTarget target;
    target.identity = this;
    target.depthTexture = map->texture;
    target.depth = map->view;
    target.depthFormat = map->format;
    target.width = map->width;
    target.height = map->height;
    m_device->Passes().SetTarget(target);
    m_device->Passes().Clear(false, true);
    m_device->SetColorMask(false, false, false, false);
    m_device->SetDepthTest(true);
    m_device->SetDepthMask(true);
    m_device->SetTransparency(TransparencyMode::NONE);
    m_device->SetCullFace(CullFace::NONE);
    m_active = true;
}

void CWebGPUShadowRenderer::End()
{
    if (!m_active) return;
    m_active = false;
    m_device->Passes().SetTarget(m_previousTarget);
    m_device->SetColorMask(true, true, true, true);
}

void CWebGPUShadowRenderer::SetProjectionMatrix(const glm::mat4& matrix)
{
    m_projectionMatrix = matrix;
}

void CWebGPUShadowRenderer::SetViewMatrix(const glm::mat4& matrix)
{
    glm::mat4 scale(1.0f);
    scale[2][2] = -1.0f;
    m_viewMatrix = scale * matrix;
}

void CWebGPUShadowRenderer::SetModelMatrix(const glm::mat4& matrix)
{
    m_modelMatrix = matrix;
}

void CWebGPUShadowRenderer::SetTexture(const Texture& texture)
{
    m_texture = texture.id;
}

void CWebGPUShadowRenderer::SetShadowMap(const Texture& texture)
{
    m_shadowMap = texture.id;
    m_shadowMapSize = texture.size;
}

void CWebGPUShadowRenderer::SetShadowRegion(const glm::vec2& offset, const glm::vec2& scale)
{
    m_regionOffset = offset;
    m_regionScale = scale;
    // Region = viewport inside the shadow atlas (gl33 uses glViewport(offset * size, scale * size))
    m_device->Passes().SetViewport(static_cast<int>(offset.x * m_shadowMapSize.x), static_cast<int>(offset.y * m_shadowMapSize.y),
                                   static_cast<int>(scale.x * m_shadowMapSize.x), static_cast<int>(scale.y * m_shadowMapSize.y));
}

wgpu::RenderPipeline CWebGPUShadowRenderer::CreatePipeline(const PipelineKey& key)
{
    wgpu::VertexAttribute attributes[2];
    attributes[0].format = wgpu::VertexFormat::Float32x3;
    attributes[0].offset = offsetof(Vertex3D, position);
    attributes[0].shaderLocation = 0;
    attributes[1].format = wgpu::VertexFormat::Float32x2;
    attributes[1].offset = offsetof(Vertex3D, uv);
    attributes[1].shaderLocation = 1;

    wgpu::VertexBufferLayout vbLayout;
    vbLayout.arrayStride = sizeof(Vertex3D);
    vbLayout.attributeCount = 2;
    vbLayout.attributes = attributes;

    wgpu::RenderPipelineDescriptor desc;
    desc.label = key.variant ? "shadow-alpha" : "shadow";
    desc.layout = m_pipelineLayout;
    desc.vertex.module = m_shader;
    desc.vertex.entryPoint = "vs_main";
    desc.vertex.bufferCount = 1;
    desc.vertex.buffers = &vbLayout;

    CWebGPUDevice::FixedFunction ff;
    m_device->FillFixedFunction(key, desc, ff);
    ff.fragment.module = m_shader;
    ff.fragment.entryPoint = "fs_alpha";
    if (key.variant != 0) desc.fragment = &ff.fragment;   // zero color targets, discard only

    return m_device->GetWGPUDevice().CreateRenderPipeline(&desc);
}

void CWebGPUShadowRenderer::DrawObject(const CVertexBuffer* buffer, bool transparent)
{
    if (!m_active) return;
    auto b = dynamic_cast<const CWebGPUVertexBuffer*>(buffer);
    if (b == nullptr || b->GetGPUVertexCount() == 0 || !b->GetGPUBuffer()) return;

    wgpu::RenderPassEncoder* pass = m_device->Passes().GetPass();
    if (pass == nullptr) return;

    glm::mat4 mvp = m_projectionMatrix * m_viewMatrix * m_modelMatrix;
    RingAllocation a = m_device->UniformRing().Upload(&mvp, sizeof(mvp));
    if (!m_uniformGroup || m_uniformGroupBuffer.Get() != a.buffer.Get())
    {
        wgpu::BindGroupEntry entry;
        entry.binding = 0;
        entry.buffer = a.buffer;
        entry.size = sizeof(glm::mat4);
        wgpu::BindGroupDescriptor desc;
        desc.layout = m_uniformLayout;
        desc.entryCount = 1;
        desc.entries = &entry;
        m_uniformGroup = m_device->GetWGPUDevice().CreateBindGroup(&desc);
        m_uniformGroupBuffer = a.buffer;
    }
    uint32_t offset = static_cast<uint32_t>(a.offset);

    PipelineKey key = m_device->MakePipelineKey(RendererId::Shadow, b->GetGPUTopology());
    key.variant = transparent ? 1 : 0;
    key.colorFormat = 0;
    key.depthBias = 8;                  // gl33 glPolygonOffset(2, 8): constant 8, slope 2
    key.depthBiasSlopeScale = 2.0f;
    const wgpu::RenderPipeline& pipeline = m_device->Pipelines().Get(key,
        [this](const PipelineKey& k) { return CreatePipeline(k); });

    pass->SetPipeline(pipeline);
    pass->SetBindGroup(0, m_device->EmptyBindGroup());
    pass->SetBindGroup(1, m_uniformGroup, 1, &offset);
    pass->SetBindGroup(2, GetTextureGroup(m_device, m_textureLayout, m_textureGroups, m_textureEpoch, transparent ? m_texture : 0));
    pass->SetVertexBuffer(0, b->GetGPUBuffer());
    pass->Draw(b->GetGPUVertexCount(), 1, 0, 0);
}

} // namespace Gfx
