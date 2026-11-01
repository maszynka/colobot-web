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
 * \file graphics/webgpu/wgpu_ui_renderer.h
 * \brief WebGPU UI renderer - full implementation and reference for the renderer pattern (colobot-web WP4)
 */

#pragma once

#include "graphics/core/renderers.h"
#include "graphics/core/vertex.h"

#include <webgpu/webgpu_cpp.h>

#include <glm/glm.hpp>

#include <unordered_map>
#include <vector>

namespace Gfx
{

class CWebGPUDevice;
struct PipelineKey;

class CWebGPUUIRenderer : public CUIRenderer
{
public:
    explicit CWebGPUUIRenderer(CWebGPUDevice* device);
    ~CWebGPUUIRenderer() override;

    void SetProjection(float left, float right, float bottom, float top) override;
    void SetTexture(const Texture& texture) override;
    void SetColor(const glm::vec4& color) override;
    void SetTransparency(TransparencyMode mode) override;

    Vertex2D* BeginPrimitive(PrimitiveType type, int count) override;
    Vertex2D* BeginPrimitives(PrimitiveType type, int drawCount, const int* counts) override;
    bool EndPrimitive() override;

private:
    struct Uniforms
    {
        glm::mat4 projectionMatrix;
        glm::vec4 color;
    };

    wgpu::RenderPipeline CreatePipeline(const PipelineKey& key);
    void BindTexture(wgpu::RenderPassEncoder& pass);
    void BindUniforms(wgpu::RenderPassEncoder& pass);

    CWebGPUDevice* m_device;

    wgpu::ShaderModule m_shader;
    wgpu::BindGroupLayout m_uniformLayout;   //!< group(1)
    wgpu::BindGroupLayout m_textureLayout;   //!< group(2)
    wgpu::PipelineLayout m_pipelineLayout;

    Uniforms m_uniforms = {};
    bool m_uniformsDirty = true;
    uint64_t m_uniformFrame = ~0ull;
    uint32_t m_uniformOffset = 0;
    wgpu::Buffer m_uniformBuffer;            //!< ring chunk holding the current uniforms
    wgpu::BindGroup m_uniformGroup;
    WGPUBuffer m_uniformGroupBuffer = nullptr;

    unsigned int m_currentTexture = 0;
    std::unordered_map<uint64_t, wgpu::BindGroup> m_textureGroups; //!< keyed by texture serial
    uint64_t m_textureEpoch = 0;

    // Current primitive
    std::vector<Vertex2D> m_vertices;
    std::vector<int> m_counts;
    std::vector<Vertex2D> m_converted;
    PrimitiveType m_type = PrimitiveType::TRIANGLES;
    bool m_mapped = false;
};

} // namespace Gfx
