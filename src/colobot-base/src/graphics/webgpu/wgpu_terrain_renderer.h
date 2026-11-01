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
 * \brief WebGPU terrain renderer - STUB from WP4, implemented by WP8
 *
 * Compiles, keeps state, draws nothing. Follow docs/webgpu-renderer-guide.md;
 * WP8 edits only this file, wgpu_terrain_renderer.cpp and shaders/terrain.wgsl.
 */

#pragma once

#include "graphics/core/renderers.h"
#include "graphics/core/color.h"
#include "graphics/core/texture.h"
#include "graphics/core/vertex.h"
#include "graphics/webgpu/wgpu_pass_manager.h"
#include "graphics/webgpu/wgpu_scene.h"

#include <webgpu/webgpu_cpp.h>

#include <glm/glm.hpp>

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
    CWebGPUDevice* m_device;
    wgpu::ShaderModule m_shader;
    CWebGPUSceneBindings m_scene;   //!< group(0)
    glm::mat4 m_projectionMatrix = glm::mat4(1.0f);
    glm::mat4 m_viewMatrix = glm::mat4(1.0f);
    glm::mat4 m_modelMatrix = glm::mat4(1.0f);
};

} // namespace Gfx
