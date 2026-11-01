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
 * \file graphics/webgpu/wgpu_particle_renderer.h
 * \brief WebGPU particle renderer
 *
 * Compiles, keeps state, draws nothing. Follow docs/webgpu-renderer-guide.md;
 * WP10 edits only this file, wgpu_particle_renderer.cpp and shaders/particle.wgsl.
 */

#pragma once

#include "graphics/core/renderers.h"
#include "graphics/core/color.h"
#include "graphics/core/texture.h"
#include "graphics/core/vertex.h"
#include "graphics/webgpu/wgpu_pass_manager.h"

#include <webgpu/webgpu_cpp.h>

#include "graphics/webgpu/wgpu_pipeline_cache.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Gfx
{

class CWebGPUDevice;

class CWebGPUParticleRenderer : public CParticleRenderer
{
public:
    explicit CWebGPUParticleRenderer(CWebGPUDevice* device);
    ~CWebGPUParticleRenderer() override;

    void Begin() override;
    void End() override;
    void SetProjectionMatrix(const glm::mat4& matrix) override;
    void SetViewMatrix(const glm::mat4& matrix) override;
    void SetModelMatrix(const glm::mat4& matrix) override;
    void SetColor(const glm::vec4& color) override;
    void SetTexture(const Texture& texture) override;
    void SetTransparency(TransparencyMode mode) override;
    void DrawParticle(PrimitiveType type, int count, const VertexParticle* vertices) override;

private:
    struct Uniforms
    {
        glm::mat4 mvp;
        glm::vec4 color;
    };

    wgpu::RenderPipeline CreatePipeline(const PipelineKey& key);
    void BindUniforms(wgpu::RenderPassEncoder& pass);
    void BindTexture(wgpu::RenderPassEncoder& pass);

    CWebGPUDevice* m_device;
    wgpu::ShaderModule m_shader;
    wgpu::BindGroupLayout m_uniformLayout;
    wgpu::BindGroupLayout m_textureLayout;
    wgpu::PipelineLayout m_pipelineLayout;
    glm::mat4 m_projectionMatrix = glm::mat4(1.0f);
    glm::mat4 m_viewMatrix = glm::mat4(1.0f);   //!< with GL z flip applied
    glm::mat4 m_modelMatrix = glm::mat4(1.0f);
    glm::vec4 m_color = { 1.0f, 1.0f, 1.0f, 1.0f };
    unsigned int m_texture = 0;

    bool m_uniformsDirty = true;
    uint64_t m_uniformFrame = ~0ull;
    uint32_t m_uniformOffset = 0;
    wgpu::Buffer m_uniformBuffer;
    wgpu::BindGroup m_uniformGroup;
    WGPUBuffer m_uniformGroupBuffer = nullptr;

    uint64_t m_textureEpoch = ~0ull;
    std::unordered_map<uint64_t, wgpu::BindGroup> m_textureGroups;

    std::vector<VertexParticle> m_converted;
};

} // namespace Gfx
