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

#include "common/logger.h"

#include "wgsl_sources.h"

// STUB (WP4): state is stored, nothing is drawn. WP8 replaces the bodies; see docs/webgpu-renderer-guide.md.
#pragma clang diagnostic ignored "-Wunused-parameter"

namespace Gfx
{

CWebGPUTerrainRenderer::CWebGPUTerrainRenderer(CWebGPUDevice* device)
    : m_device(device), m_scene(device)
{
    GetLogger()->Info("Creating CWebGPUTerrainRenderer (stub)");
    m_shader = CreateShaderModule(m_device->GetWGPUDevice(), "terrain", { WGSL::preamble, WGSL::shadow, WGSL::lighting, WGSL::terrain });
}

CWebGPUTerrainRenderer::~CWebGPUTerrainRenderer() = default;

void CWebGPUTerrainRenderer::Begin()
{
    // TODO(WP8)
}

void CWebGPUTerrainRenderer::End()
{
    // TODO(WP8)
}

void CWebGPUTerrainRenderer::SetProjectionMatrix(const glm::mat4& matrix)
{
    m_projectionMatrix = matrix;
}

void CWebGPUTerrainRenderer::SetViewMatrix(const glm::mat4& matrix)
{
    m_viewMatrix = matrix;
}

void CWebGPUTerrainRenderer::SetModelMatrix(const glm::mat4& matrix)
{
    m_modelMatrix = matrix;
}

void CWebGPUTerrainRenderer::SetAlbedoColor(const Color& color)
{
    // TODO(WP8)
}

void CWebGPUTerrainRenderer::SetAlbedoTexture(const Texture& texture)
{
    // TODO(WP8)
}

void CWebGPUTerrainRenderer::SetEmissiveColor(const Color& color)
{
    // TODO(WP8)
}

void CWebGPUTerrainRenderer::SetEmissiveTexture(const Texture& texture)
{
    // TODO(WP8)
}

void CWebGPUTerrainRenderer::SetMaterialParams(float roughness, float metalness, float aoStrength)
{
    // TODO(WP8)
}

void CWebGPUTerrainRenderer::SetMaterialTexture(const Texture& texture)
{
    // TODO(WP8)
}

void CWebGPUTerrainRenderer::SetDetailTexture(const Texture& texture)
{
    // TODO(WP8)
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
    // TODO(WP8)
}

void CWebGPUTerrainRenderer::DrawObject(const glm::mat4& matrix, const CVertexBuffer* buffer)
{
    // TODO(WP8)
}

} // namespace Gfx
