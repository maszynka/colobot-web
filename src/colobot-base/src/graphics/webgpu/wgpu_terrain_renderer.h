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
 * \file graphics/webgpu/wgpu_terrain_renderer.h
 * \brief WebGPU terrain renderer (WP8), port of gl33_terrain_renderer
 */

#pragma once

#include "graphics/core/renderers.h"
#include "graphics/core/color.h"
#include "graphics/core/texture.h"
#include "graphics/core/vertex.h"
#include "graphics/webgpu/wgpu_pass_manager.h"
#include "graphics/webgpu/wgpu_pipeline_cache.h"
#include "graphics/webgpu/wgpu_scene.h"

#include <webgpu/webgpu_cpp.h>

#include <glm/glm.hpp>

#include <array>
#include <map>

namespace Gfx
{

class CWebGPUDevice;

class CWebGPUTerrainRenderer : public CTerrainRenderer
{
public:
    explicit CWebGPUTerrainRenderer(CWebGPUDevice* device);
    ~CWebGPUTerrainRenderer() override;

    void Begin() override;
    void End() override;
    void SetProjectionMatrix(const glm::mat4& matrix) override;
    void SetViewMatrix(const glm::mat4& matrix) override;
    void SetModelMatrix(const glm::mat4& matrix) override;
    void SetAlbedoColor(const Color& color) override;
    void SetAlbedoTexture(const Texture& texture) override;
    void SetEmissiveColor(const Color& color) override;
    void SetEmissiveTexture(const Texture& texture) override;
    void SetMaterialParams(float roughness, float metalness, float aoStrength) override;
    void SetMaterialTexture(const Texture& texture) override;
    void SetDetailTexture(const Texture& texture) override;
    void SetShadowMap(const Texture& texture) override;
    void SetLight(const glm::vec4& position, const float& intensity, const glm::vec3& color) override;
    void SetSky(const Color& color, float intensity) override;
    void SetShadowParams(int count, const ShadowParam* params) override;
    void SetFog(float min, float max, const glm::vec3& color) override;
    void DrawObject(const glm::mat4& matrix, const CVertexBuffer* buffer) override;

private:
    //! WGSL: struct TerrainUniforms (shaders/terrain.wgsl)
    struct Uniforms
    {
        glm::mat4 projection = glm::mat4(1.0f);
        glm::mat4 view = glm::mat4(1.0f);
        glm::mat4 model = glm::mat4(1.0f);
        glm::mat4 normalMatrix = glm::mat4(1.0f);
        glm::vec4 albedoColor = { 1.0f, 1.0f, 1.0f, 1.0f };
        glm::vec3 emissiveColor = { 0.0f, 0.0f, 0.0f };
        float roughness = 1.0f;
        glm::vec3 fogColor = { 0.0f, 0.0f, 0.0f };
        float metalness = 0.0f;
        glm::vec2 fogRange = { 1e+6f, 1e+6f };
        float aoStrength = 1.0f;
        float pad = 0.0f;
    };
    static_assert(sizeof(Uniforms) == 320, "Uniforms must match WGSL layout");

    wgpu::RenderPipeline CreatePipeline(const PipelineKey& key);
    wgpu::BindGroup GetTextureGroup();

    CWebGPUDevice* m_device;
    wgpu::ShaderModule m_shader;
    CWebGPUSceneBindings m_scene;   //!< group(0)
    wgpu::BindGroupLayout m_uniformLayout;
    wgpu::BindGroupLayout m_textureLayout;
    wgpu::PipelineLayout m_pipelineLayout;

    Uniforms m_uniforms;

    unsigned int m_albedoTexture = 0;
    unsigned int m_detailTexture = 0;
    unsigned int m_emissiveTexture = 0;
    unsigned int m_materialTexture = 0;

    wgpu::BindGroup m_uniformGroup;
    WGPUBuffer m_uniformGroupBuffer = nullptr;
    std::map<std::array<uint64_t, 4>, wgpu::BindGroup> m_textureGroups;
    uint64_t m_textureEpoch = ~0ull;
};

} // namespace Gfx
