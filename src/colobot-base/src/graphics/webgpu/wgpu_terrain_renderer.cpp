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

#include "graphics/webgpu/wgpu_terrain_renderer.h"

#include "graphics/webgpu/wgpu_device.h"
#include "graphics/webgpu/wgpu_util.h"

#include "graphics/core/material.h"
#include "graphics/core/transparency.h"
#include "graphics/core/vertex.h"

#include "common/logger.h"

#include "wgsl_sources.h"

#include <glm/ext.hpp>

namespace Gfx
{

CWebGPUTerrainRenderer::CWebGPUTerrainRenderer(CWebGPUDevice* device)
    : m_device(device), m_scene(device)
{
    GetLogger()->Info("Creating CWebGPUTerrainRenderer");
    const wgpu::Device& dev = m_device->GetWGPUDevice();

    m_shader = CreateShaderModule(dev, "terrain", { WGSL::preamble, WGSL::shadow, WGSL::lighting, WGSL::terrain });

    // group(1): TerrainUniforms with dynamic offset
    {
        wgpu::BindGroupLayoutEntry entry;
        entry.binding = 0;
        entry.visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
        entry.buffer.type = wgpu::BufferBindingType::Uniform;
        entry.buffer.hasDynamicOffset = true;
        entry.buffer.minBindingSize = sizeof(Uniforms);
        wgpu::BindGroupLayoutDescriptor desc;
        desc.label = "terrain-group1";
        desc.entryCount = 1;
        desc.entries = &entry;
        m_uniformLayout = dev.CreateBindGroupLayout(&desc);
    }
    // group(2): albedo, detail, emissive, material textures + their samplers
    {
        wgpu::BindGroupLayoutEntry entries[8];
        for (uint32_t i = 0; i < 4; i++)
        {
            entries[i].binding = i;
            entries[i].visibility = wgpu::ShaderStage::Fragment;
            entries[i].texture.sampleType = wgpu::TextureSampleType::Float;
            entries[i].texture.viewDimension = wgpu::TextureViewDimension::e2D;
            entries[4 + i].binding = 4 + i;
            entries[4 + i].visibility = wgpu::ShaderStage::Fragment;
            entries[4 + i].sampler.type = wgpu::SamplerBindingType::Filtering;
        }
        wgpu::BindGroupLayoutDescriptor desc;
        desc.label = "terrain-group2";
        desc.entryCount = 8;
        desc.entries = entries;
        m_textureLayout = dev.CreateBindGroupLayout(&desc);
    }
    {
        wgpu::BindGroupLayout layouts[3] = { m_device->SceneBindGroupLayout(), m_uniformLayout, m_textureLayout };
        wgpu::PipelineLayoutDescriptor desc;
        desc.label = "terrain-layout";
        desc.bindGroupLayoutCount = 3;
        desc.bindGroupLayouts = layouts;
        m_pipelineLayout = dev.CreatePipelineLayout(&desc);
    }

    GetLogger()->Info("CWebGPUTerrainRenderer created successfully");
}

CWebGPUTerrainRenderer::~CWebGPUTerrainRenderer() = default;

void CWebGPUTerrainRenderer::Begin()
{
    m_device->SetDepthTest(true);
    m_device->SetDepthMask(true);
    m_device->SetCullFace(CullFace::BACK);
    m_device->SetTransparency(TransparencyMode::NONE);

    m_albedoTexture = 0;
    m_detailTexture = 0;
    m_emissiveTexture = 0;
    m_materialTexture = 0;
    m_scene.SetShadowMap(0);

    SetFog(1e+6f, 1e+6f, {});
}

void CWebGPUTerrainRenderer::End()
{
    m_albedoTexture = 0;
    m_detailTexture = 0;
    m_emissiveTexture = 0;
    m_materialTexture = 0;
    m_scene.SetShadowMap(0);
}

void CWebGPUTerrainRenderer::SetProjectionMatrix(const glm::mat4& matrix)
{
    m_uniforms.projection = matrix;
}

void CWebGPUTerrainRenderer::SetViewMatrix(const glm::mat4& matrix)
{
    // same convention as gl33: flip Z of the view matrix
    glm::mat4 scale(1.0f);
    scale[2][2] = -1.0f;

    glm::mat4 viewMatrix = scale * matrix;
    glm::mat4 cameraMatrix = glm::inverse(viewMatrix);

    m_uniforms.view = viewMatrix;
    m_scene.Light().cameraPosition = glm::vec3(cameraMatrix[3]);
}

void CWebGPUTerrainRenderer::SetModelMatrix(const glm::mat4& matrix)
{
    m_uniforms.model = matrix;
    m_uniforms.normalMatrix = glm::mat4(glm::transpose(glm::inverse(glm::mat3(matrix))));
}

void CWebGPUTerrainRenderer::SetAlbedoColor(const Color& color)
{
    m_uniforms.albedoColor = { color.r, color.g, color.b, color.a };
}

void CWebGPUTerrainRenderer::SetAlbedoTexture(const Texture& texture)
{
    m_albedoTexture = texture.id;
}

void CWebGPUTerrainRenderer::SetEmissiveColor(const Color& color)
{
    m_uniforms.emissiveColor = { color.r, color.g, color.b };
}

void CWebGPUTerrainRenderer::SetEmissiveTexture(const Texture& texture)
{
    m_emissiveTexture = texture.id;
}

void CWebGPUTerrainRenderer::SetMaterialParams(float roughness, float metalness, float aoStrength)
{
    m_uniforms.roughness = roughness;
    m_uniforms.metalness = metalness;
    m_uniforms.aoStrength = aoStrength;
}

void CWebGPUTerrainRenderer::SetMaterialTexture(const Texture& texture)
{
    m_materialTexture = texture.id;
}

void CWebGPUTerrainRenderer::SetDetailTexture(const Texture& texture)
{
    m_detailTexture = texture.id;
}

void CWebGPUTerrainRenderer::SetShadowMap(const Texture& texture)
{
    m_scene.SetShadowMap(texture.id);
}

void CWebGPUTerrainRenderer::SetLight(const glm::vec4& position, const float& intensity, const glm::vec3& color)
{
    m_scene.Light().lightPosition = position;
    m_scene.Light().lightIntensity = intensity;
    m_scene.Light().lightColor = color;
}

void CWebGPUTerrainRenderer::SetSky(const Color& color, float intensity)
{
    m_scene.Light().skyColor = { color.r, color.g, color.b };
    m_scene.Light().skyIntensity = intensity;
}

void CWebGPUTerrainRenderer::SetShadowParams(int count, const ShadowParam* params)
{
    m_scene.SetShadowParams(count, params);
}

void CWebGPUTerrainRenderer::SetFog(float min, float max, const glm::vec3& color)
{
    m_uniforms.fogRange = { min, max };
    m_uniforms.fogColor = color;
}

wgpu::RenderPipeline CWebGPUTerrainRenderer::CreatePipeline(const PipelineKey& key)
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
    desc.label = "terrain";
    desc.layout = m_pipelineLayout;
    desc.vertex.module = m_shader;
    desc.vertex.entryPoint = "vs_main";
    desc.vertex.bufferCount = 1;
    desc.vertex.buffers = &vbLayout;

    CWebGPUDevice::FixedFunction ff;
    m_device->FillFixedFunction(key, desc, ff);
    ff.fragment.module = m_shader;
    ff.fragment.entryPoint = "fs_main";
    if (key.colorFormat != 0)
        desc.fragment = &ff.fragment;

    return m_device->GetWGPUDevice().CreateRenderPipeline(&desc);
}

wgpu::BindGroup CWebGPUTerrainRenderer::GetTextureGroup()
{
    CWebGPUTextureManager& textures = m_device->Textures();
    if (m_textureEpoch != textures.Epoch())
    {
        m_textureGroups.clear();
        m_textureEpoch = textures.Epoch();
    }

    const unsigned int ids[4] = { m_albedoTexture, m_detailTexture, m_emissiveTexture, m_materialTexture };
    const WebGPUTextureEntry* entries[4];
    std::array<uint64_t, 4> keyArr;
    for (int i = 0; i < 4; i++)
    {
        const WebGPUTextureEntry* e = textures.Find(ids[i]);
        if (e == nullptr || e->depth) e = &textures.White();
        entries[i] = e;
        keyArr[i] = e->serial;
    }

    auto it = m_textureGroups.find(keyArr);
    if (it == m_textureGroups.end())
    {
        wgpu::BindGroupEntry bg[8];
        for (uint32_t i = 0; i < 4; i++)
        {
            bg[i].binding = i;
            bg[i].textureView = entries[i]->view;
            bg[4 + i].binding = 4 + i;
            bg[4 + i].sampler = entries[i]->sampler;
        }
        wgpu::BindGroupDescriptor desc;
        desc.layout = m_textureLayout;
        desc.entryCount = 8;
        desc.entries = bg;
        it = m_textureGroups.emplace(keyArr, m_device->GetWGPUDevice().CreateBindGroup(&desc)).first;
    }
    return it->second;
}

void CWebGPUTerrainRenderer::DrawObject(const glm::mat4& matrix, const CVertexBuffer* buffer)
{
    auto b = dynamic_cast<const CWebGPUVertexBuffer*>(buffer);
    if (b == nullptr) return;

    SetModelMatrix(matrix);

    // CullFace::BOTH maps to None in WebGPU: nothing would be visible in GL, so skip
    if (m_device->GetCullFace() == CullFace::BOTH) return;

    uint32_t count = b->GetGPUVertexCount();
    if (count == 0 || !b->GetGPUBuffer()) return;

    wgpu::RenderPassEncoder* pass = m_device->Passes().GetPass();
    if (pass == nullptr) return;

    PipelineKey key = m_device->MakePipelineKey(RendererId::Terrain, b->GetGPUTopology());
    const wgpu::RenderPipeline& pipeline = m_device->Pipelines().Get(key,
        [this](const PipelineKey& k) { return CreatePipeline(k); });

    RingAllocation a = m_device->UniformRing().Upload(&m_uniforms, sizeof(Uniforms));
    uint32_t offset = static_cast<uint32_t>(a.offset);
    if (!m_uniformGroup || m_uniformGroupBuffer.Get() != a.buffer.Get())
    {
        wgpu::BindGroupEntry entry;
        entry.binding = 0;
        entry.buffer = a.buffer;
        entry.size = sizeof(Uniforms);
        wgpu::BindGroupDescriptor desc;
        desc.layout = m_uniformLayout;
        desc.entryCount = 1;
        desc.entries = &entry;
        m_uniformGroup = m_device->GetWGPUDevice().CreateBindGroup(&desc);
        m_uniformGroupBuffer = a.buffer;
    }

    pass->SetPipeline(pipeline);
    m_scene.Bind(*pass);
    pass->SetBindGroup(1, m_uniformGroup, 1, &offset);
    pass->SetBindGroup(2, GetTextureGroup());
    pass->SetVertexBuffer(0, b->GetGPUBuffer(), 0, wgpu::kWholeSize);
    pass->Draw(count, 1, 0, 0);
}

} // namespace Gfx
