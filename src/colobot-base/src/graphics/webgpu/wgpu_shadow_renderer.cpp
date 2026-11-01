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

// STUB (WP4): state is stored, nothing is drawn. WP10 replaces the bodies; see docs/webgpu-renderer-guide.md.
#pragma clang diagnostic ignored "-Wunused-parameter"

namespace Gfx
{

CWebGPUShadowRenderer::CWebGPUShadowRenderer(CWebGPUDevice* device)
    : m_device(device)
{
    GetLogger()->Info("Creating CWebGPUShadowRenderer (stub)");
    m_shader = CreateShaderModule(m_device->GetWGPUDevice(), "shadow", { WGSL::preamble, WGSL::shadow_pass });
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
    m_viewMatrix = matrix;
}

void CWebGPUShadowRenderer::SetModelMatrix(const glm::mat4& matrix)
{
    m_modelMatrix = matrix;
}

void CWebGPUShadowRenderer::SetTexture(const Texture& texture)
{
    // TODO(WP10)
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

void CWebGPUShadowRenderer::DrawObject(const CVertexBuffer* buffer, bool transparent)
{
    // TODO(WP10): pipeline with RendererId::Shadow, key.colorFormat = 0, key.depthBias = 8,
    // key.depthBiasSlopeScale = 2.0f (gl33 glPolygonOffset(2, 8)); alpha-tested variant when transparent.
}

} // namespace Gfx
