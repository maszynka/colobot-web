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

#include "graphics/webgpu/wgpu_object_renderer.h"

#include "graphics/webgpu/wgpu_device.h"
#include "graphics/webgpu/wgpu_util.h"

#include "graphics/core/material.h"
#include "graphics/core/transparency.h"

#include "common/logger.h"

#include "wgsl_sources.h"

#include <glm/ext.hpp>

#include <algorithm>
#include <cstddef>
#include <numeric>

namespace Gfx
{

CWebGPUObjectRenderer::CWebGPUObjectRenderer(CWebGPUDevice* device)
    : m_device(device), m_scene(device)
{
    GetLogger()->Info("Creating CWebGPUObjectRenderer");

    const wgpu::Device& dev = m_device->GetWGPUDevice();
    m_shader = CreateShaderModule(dev, "object", { WGSL::preamble, WGSL::shadow, WGSL::lighting, WGSL::object });

    // group(1): ObjectUniforms, dynamic offset
    {
        wgpu::BindGroupLayoutEntry entry;
        entry.binding = 0;
        entry.visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
        entry.buffer.type = wgpu::BufferBindingType::Uniform;
        entry.buffer.hasDynamicOffset = true;
        entry.buffer.minBindingSize = sizeof(Uniforms);

        wgpu::BindGroupLayoutDescriptor desc;
        desc.label = "object-group1";
        desc.entryCount = 1;
        desc.entries = &entry;
        m_uniformLayout = dev.CreateBindGroupLayout(&desc);
    }

    // group(2): albedo, detail, emissive, material -> (texture, sampler) pairs
    {
        wgpu::BindGroupLayoutEntry entries[SLOT_COUNT * 2];
        for (uint32_t i = 0; i < SLOT_COUNT; ++i)
        {
            auto& t = entries[i * 2];
            t.binding = i * 2;
            t.visibility = wgpu::ShaderStage::Fragment;
            t.texture.sampleType = wgpu::TextureSampleType::Float;
            t.texture.viewDimension = wgpu::TextureViewDimension::e2D;

            auto& s = entries[i * 2 + 1];
            s.binding = i * 2 + 1;
            s.visibility = wgpu::ShaderStage::Fragment;
            s.sampler.type = wgpu::SamplerBindingType::Filtering;
        }

        wgpu::BindGroupLayoutDescriptor desc;
        desc.label = "object-group2";
        desc.entryCount = SLOT_COUNT * 2;
        desc.entries = entries;
        m_textureLayout = dev.CreateBindGroupLayout(&desc);
    }

    {
        wgpu::BindGroupLayout layouts[3] = { m_device->SceneBindGroupLayout(), m_uniformLayout, m_textureLayout };
        wgpu::PipelineLayoutDescriptor desc;
        desc.label = "object-layout";
        desc.bindGroupLayoutCount = 3;
        desc.bindGroupLayouts = layouts;
        m_pipelineLayout = dev.CreatePipelineLayout(&desc);
    }

    GetLogger()->Info("CWebGPUObjectRenderer created successfully");
}

CWebGPUObjectRenderer::~CWebGPUObjectRenderer() = default;

void CWebGPUObjectRenderer::Begin()
{
    m_textures.fill(0);

    m_device->SetDepthTest(true);
    m_device->SetDepthMask(true);
    m_device->SetTransparency(TransparencyMode::NONE);
    m_device->SetCullFace(CullFace::BACK);

    SetUVTransform({ 0.0f, 0.0f }, { 1.0f, 1.0f });
    SetAlphaScissor(0.0f);
    SetFog(1e+6f, 1e+6f, {});
    SetEmissiveColor({ 0, 0, 0, 0 });
    SetAlbedoColor({ 1, 1, 1, 1 });
    SetMaterialParams(1.0f, 0.0f, 0.0f);
    SetRecolor(false, {}, {}, 0.0f);
}

void CWebGPUObjectRenderer::End()
{
    m_textures.fill(0);
    m_scene.SetShadowMap(0);
}

void CWebGPUObjectRenderer::SetProjectionMatrix(const glm::mat4& matrix)
{
    U().projection = matrix;
}

void CWebGPUObjectRenderer::SetViewMatrix(const glm::mat4& matrix)
{
    // Same as gl33: flip Z of the engine's view matrix
    glm::mat4 scale(1.0f);
    scale[2][2] = -1.0f;
    glm::mat4 viewMatrix = scale * matrix;
    glm::vec4 cameraPos = glm::inverse(viewMatrix)[3];

    U().view = viewMatrix;
    m_scene.Light().cameraPosition = glm::vec3(cameraPos);
}

void CWebGPUObjectRenderer::SetModelMatrix(const glm::mat4& matrix)
{
    Uniforms& u = U();
    u.model = matrix;
    u.normalMatrix = glm::mat4(glm::transpose(glm::inverse(glm::mat3(matrix))));
}

void CWebGPUObjectRenderer::SetAlbedoColor(const Color& color)
{
    U().albedoColor = { color.r, color.g, color.b, color.a };
}

void CWebGPUObjectRenderer::SetSlot(TextureSlot slot, unsigned int id)
{
    m_textures[slot] = id;
}

void CWebGPUObjectRenderer::SetAlbedoTexture(const Texture& texture)
{
    SetSlot(ALBEDO, texture.id);
}

void CWebGPUObjectRenderer::SetEmissiveColor(const Color& color)
{
    U().emissiveColor = { color.r, color.g, color.b };
}

void CWebGPUObjectRenderer::SetEmissiveTexture(const Texture& texture)
{
    SetSlot(EMISSIVE, texture.id);
}

void CWebGPUObjectRenderer::SetMaterialParams(float roughness, float metalness, float aoStrength)
{
    Uniforms& u = U();
    u.roughness = roughness;
    u.metalness = metalness;
    u.aoStrength = aoStrength;
}

void CWebGPUObjectRenderer::SetMaterialTexture(const Texture& texture)
{
    SetSlot(MATERIAL, texture.id);
}

void CWebGPUObjectRenderer::SetDetailTexture(const Texture& texture)
{
    SetSlot(DETAIL, texture.id);
}

void CWebGPUObjectRenderer::SetShadowMap(const Texture& texture)
{
    m_scene.SetShadowMap(texture.id);
}

void CWebGPUObjectRenderer::SetLighting(bool enabled)
{
    m_scene.Light().lighting = enabled ? 1u : 0u;
}

void CWebGPUObjectRenderer::SetLight(const glm::vec4& position, const float& intensity, const glm::vec3& color)
{
    m_scene.Light().lightPosition = position;
    m_scene.Light().lightIntensity = intensity;
    m_scene.Light().lightColor = color;
}

void CWebGPUObjectRenderer::SetSky(const Color& color, float intensity)
{
    m_scene.Light().skyColor = { color.r, color.g, color.b };
    m_scene.Light().skyIntensity = intensity;
}

void CWebGPUObjectRenderer::SetShadowParams(int count, const ShadowParam* params)
{
    m_scene.SetShadowParams(count, params);
}

void CWebGPUObjectRenderer::SetFog(float min, float max, const glm::vec3& color)
{
    Uniforms& u = U();
    u.fogRange = { min, max };
    u.fogColor = color;
}

void CWebGPUObjectRenderer::SetAlphaScissor(float alpha)
{
    U().alphaScissor = alpha;
}

void CWebGPUObjectRenderer::SetRecolor(bool enabled, const glm::vec3& from, const glm::vec3& to, float threshold)
{
    Uniforms& u = U();
    u.recolor = enabled ? 1u : 0u;

    if (enabled)
    {
        ColorHSV fromHSV = RGB2HSV(Color(from.r, from.g, from.b, 1.0f));
        ColorHSV toHSV = RGB2HSV(Color(to.r, to.g, to.b, 1.0f));

        u.recolorFrom = { fromHSV.h, fromHSV.s, fromHSV.v };
        u.recolorTo = { toHSV.h, toHSV.s, toHSV.v };
        u.recolorThreshold = threshold;
    }
}

void CWebGPUObjectRenderer::SetDepthTest(bool enabled)
{
    m_device->SetDepthTest(enabled);
}

void CWebGPUObjectRenderer::SetDepthMask(bool enabled)
{
    m_device->SetDepthMask(enabled);
}

void CWebGPUObjectRenderer::SetCullFace(CullFace mode)
{
    m_device->SetCullFace(mode);
}

void CWebGPUObjectRenderer::SetTransparency(TransparencyMode mode)
{
    m_device->SetTransparency(mode);
}

void CWebGPUObjectRenderer::SetUVTransform(const glm::vec2& offset, const glm::vec2& scale)
{
    Uniforms& u = U();
    u.uvOffset = offset;
    u.uvScale = scale;
}

void CWebGPUObjectRenderer::SetTriplanarMode(bool enabled)
{
    U().triplanarMode = enabled ? 1u : 0u;
}

void CWebGPUObjectRenderer::SetTriplanarScale(float scale)
{
    U().triplanarScale = scale;
}

wgpu::RenderPipeline CWebGPUObjectRenderer::CreatePipeline(const PipelineKey& key)
{
    wgpu::VertexAttribute attributes[5];
    attributes[0].format = wgpu::VertexFormat::Float32x3;
    attributes[0].offset = offsetof(Vertex3D, position);
    attributes[0].shaderLocation = 0;
    attributes[1].format = wgpu::VertexFormat::Float32x3;
    attributes[1].offset = offsetof(Vertex3D, normal);
    attributes[1].shaderLocation = 1;
    attributes[2].format = wgpu::VertexFormat::Unorm8x4;
    attributes[2].offset = offsetof(Vertex3D, color);
    attributes[2].shaderLocation = 2;
    attributes[3].format = wgpu::VertexFormat::Float32x2;
    attributes[3].offset = offsetof(Vertex3D, uv);
    attributes[3].shaderLocation = 3;
    attributes[4].format = wgpu::VertexFormat::Float32x2;
    attributes[4].offset = offsetof(Vertex3D, uv2);
    attributes[4].shaderLocation = 4;

    wgpu::VertexBufferLayout vbLayout;
    vbLayout.arrayStride = sizeof(Vertex3D);
    vbLayout.attributeCount = 5;
    vbLayout.attributes = attributes;

    wgpu::RenderPipelineDescriptor desc;
    desc.label = "object";
    desc.layout = m_pipelineLayout;
    desc.vertex.module = m_shader;
    desc.vertex.entryPoint = "vs_main";
    desc.vertex.bufferCount = 1;
    desc.vertex.buffers = &vbLayout;

    CWebGPUDevice::FixedFunction ff;
    m_device->FillFixedFunction(key, desc, ff);

    if (key.colorFormat != 0)
    {
        ff.fragment.module = m_shader;
        ff.fragment.entryPoint = "fs_main";
        desc.fragment = &ff.fragment;
    }

    return m_device->GetWGPUDevice().CreateRenderPipeline(&desc);
}

void CWebGPUObjectRenderer::BindUniforms(wgpu::RenderPassEncoder& pass)
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

void CWebGPUObjectRenderer::BindTextures(wgpu::RenderPassEncoder& pass)
{
    CWebGPUTextureManager& textures = m_device->Textures();

    if (m_textureEpoch != textures.Epoch())
    {
        m_textureGroups.clear();
        m_textureEpoch = textures.Epoch();
    }

    const WebGPUTextureEntry* entries[SLOT_COUNT];
    std::array<uint64_t, SLOT_COUNT> key;

    for (uint32_t i = 0; i < SLOT_COUNT; ++i)
    {
        const WebGPUTextureEntry* e = m_textures[i] == 0 ? nullptr : textures.Find(m_textures[i]);
        if (e == nullptr || e->depth) e = &textures.White();
        entries[i] = e;
        key[i] = e->serial;
    }

    auto it = m_textureGroups.find(key);
    if (it == m_textureGroups.end())
    {
        wgpu::BindGroupEntry bg[SLOT_COUNT * 2];
        for (uint32_t i = 0; i < SLOT_COUNT; ++i)
        {
            bg[i * 2].binding = i * 2;
            bg[i * 2].textureView = entries[i]->view;
            bg[i * 2 + 1].binding = i * 2 + 1;
            bg[i * 2 + 1].sampler = entries[i]->sampler;
        }

        wgpu::BindGroupDescriptor desc;
        desc.layout = m_textureLayout;
        desc.entryCount = SLOT_COUNT * 2;
        desc.entries = bg;
        it = m_textureGroups.emplace(key, m_device->GetWGPUDevice().CreateBindGroup(&desc)).first;
    }

    pass.SetBindGroup(2, it->second);
}

bool CWebGPUObjectRenderer::Prepare(wgpu::RenderPassEncoder& pass, wgpu::PrimitiveTopology topology)
{
    // WebGPU has no "cull both"; nothing would be visible anyway
    if (m_device->GetCullFace() == CullFace::BOTH) return false;

    PipelineKey key = m_device->MakePipelineKey(RendererId::Object, topology);
    const wgpu::RenderPipeline& pipeline = m_device->Pipelines().Get(key,
        [this](const PipelineKey& k) { return CreatePipeline(k); });

    pass.SetPipeline(pipeline);
    m_scene.Bind(pass);
    BindUniforms(pass);
    BindTextures(pass);
    return true;
}

void CWebGPUObjectRenderer::DrawObject(const CVertexBuffer* buffer)
{
    auto b = dynamic_cast<const CWebGPUVertexBuffer*>(buffer);
    if (b == nullptr || !b->GetGPUBuffer() || b->GetGPUVertexCount() == 0) return;

    wgpu::RenderPassEncoder* pass = m_device->Passes().GetPass();
    if (pass == nullptr) return;

    if (!Prepare(*pass, b->GetGPUTopology())) return;

    pass->SetVertexBuffer(0, b->GetGPUBuffer(), 0, static_cast<uint64_t>(b->GetGPUVertexCount()) * sizeof(Vertex3D));
    pass->Draw(b->GetGPUVertexCount(), 1, 0, 0);
}

void CWebGPUObjectRenderer::DrawPrimitive(PrimitiveType type, int count, const Vertex3D* vertices)
{
    DrawPrimitives(type, 1, &count, vertices);
}

void CWebGPUObjectRenderer::DrawPrimitives(PrimitiveType type, int drawCount, int count[], const Vertex3D* vertices)
{
    if (drawCount <= 0 || count == nullptr || vertices == nullptr) return;

    m_counts.assign(count, count + drawCount);
    for (int& c : m_counts) c = std::max(c, 0);
    int total = std::accumulate(m_counts.begin(), m_counts.end(), 0);
    if (total == 0) return;

    wgpu::RenderPassEncoder* pass = m_device->Passes().GetPass();
    if (pass == nullptr) return;   // outside BeginScene/EndScene: dropped

    // Native topologies draw sub-ranges, others (fan/loop/points) are converted to lists
    wgpu::PrimitiveTopology topology = NativeTopology(type);
    const Vertex3D* data = vertices;
    size_t vertexCount = static_cast<size_t>(total);

    if (topology == wgpu::PrimitiveTopology::Undefined)
    {
        m_converted.clear();
        size_t first = 0;
        for (int c : m_counts)
        {
            ConvertPrimitives(type, vertices + first, c, m_converted, 0.05f);
            first += static_cast<size_t>(c);
        }
        topology = ConvertedTopology(type);
        data = m_converted.data();
        vertexCount = m_converted.size();
        m_counts.assign(1, static_cast<int>(vertexCount));
        if (vertexCount == 0) return;
    }

    if (!Prepare(*pass, topology)) return;

    RingAllocation vb = m_device->VertexRing().Upload(data, vertexCount * sizeof(Vertex3D));
    pass->SetVertexBuffer(0, vb.buffer, vb.offset, vb.size);

    uint32_t first = 0;
    for (int c : m_counts)
    {
        if (c > 0) pass->Draw(static_cast<uint32_t>(c), 1, first, 0);
        first += static_cast<uint32_t>(c);
    }
}

} // namespace Gfx
