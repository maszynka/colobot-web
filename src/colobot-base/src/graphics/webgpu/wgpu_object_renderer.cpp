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

// STUB (WP4): state is stored, nothing is drawn. WP9 replaces the bodies; see docs/webgpu-renderer-guide.md.
#pragma clang diagnostic ignored "-Wunused-parameter"

namespace Gfx
{

CWebGPUObjectRenderer::CWebGPUObjectRenderer(CWebGPUDevice* device)
    : m_device(device), m_scene(device)
{
    GetLogger()->Info("Creating CWebGPUObjectRenderer (stub)");
    m_shader = CreateShaderModule(m_device->GetWGPUDevice(), "object", { WGSL::preamble, WGSL::shadow, WGSL::lighting, WGSL::object });
}

CWebGPUObjectRenderer::~CWebGPUObjectRenderer() = default;

void CWebGPUObjectRenderer::Begin()
{
    // TODO(WP9)
}

void CWebGPUObjectRenderer::End()
{
    // TODO(WP9)
}

void CWebGPUObjectRenderer::SetProjectionMatrix(const glm::mat4& matrix)
{
    m_projectionMatrix = matrix;
}

void CWebGPUObjectRenderer::SetViewMatrix(const glm::mat4& matrix)
{
    m_viewMatrix = matrix;
}

void CWebGPUObjectRenderer::SetModelMatrix(const glm::mat4& matrix)
{
    m_modelMatrix = matrix;
}

void CWebGPUObjectRenderer::SetAlbedoColor(const Color& color)
{
    // TODO(WP9)
}

void CWebGPUObjectRenderer::SetAlbedoTexture(const Texture& texture)
{
    // TODO(WP9)
}

void CWebGPUObjectRenderer::SetEmissiveColor(const Color& color)
{
    // TODO(WP9)
}

void CWebGPUObjectRenderer::SetEmissiveTexture(const Texture& texture)
{
    // TODO(WP9)
}

void CWebGPUObjectRenderer::SetMaterialParams(float roughness, float metalness, float aoStrength)
{
    // TODO(WP9)
}

void CWebGPUObjectRenderer::SetMaterialTexture(const Texture& texture)
{
    // TODO(WP9)
}

void CWebGPUObjectRenderer::SetDetailTexture(const Texture& texture)
{
    // TODO(WP9)
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
    // TODO(WP9)
}

void CWebGPUObjectRenderer::SetAlphaScissor(float alpha)
{
    // TODO(WP9)
}

void CWebGPUObjectRenderer::SetRecolor(bool enabled, const glm::vec3& from, const glm::vec3& to, float threshold)
{
    // TODO(WP9)
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
    // TODO(WP9)
}

void CWebGPUObjectRenderer::SetTriplanarMode(bool enabled)
{
    // TODO(WP9)
}

void CWebGPUObjectRenderer::SetTriplanarScale(float scale)
{
    // TODO(WP9)
}

void CWebGPUObjectRenderer::DrawObject(const CVertexBuffer* buffer)
{
    // TODO(WP9)
}

void CWebGPUObjectRenderer::DrawPrimitive(PrimitiveType type, int count, const Vertex3D* vertices)
{
    // TODO(WP9)
}

void CWebGPUObjectRenderer::DrawPrimitives(PrimitiveType type, int drawCount, int count[], const Vertex3D* vertices)
{
    // TODO(WP9)
}

} // namespace Gfx
