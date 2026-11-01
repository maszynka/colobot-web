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

#include "graphics/webgpu/wgpu_particle_renderer.h"

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

CWebGPUParticleRenderer::CWebGPUParticleRenderer(CWebGPUDevice* device)
    : m_device(device)
{
    GetLogger()->Info("Creating CWebGPUParticleRenderer (stub)");
    m_shader = CreateShaderModule(m_device->GetWGPUDevice(), "particle", { WGSL::preamble, WGSL::shadow, WGSL::particle });
}

CWebGPUParticleRenderer::~CWebGPUParticleRenderer() = default;

void CWebGPUParticleRenderer::Begin()
{
    // TODO(WP10)
}

void CWebGPUParticleRenderer::End()
{
    // TODO(WP10)
}

void CWebGPUParticleRenderer::SetProjectionMatrix(const glm::mat4& matrix)
{
    m_projectionMatrix = matrix;
}

void CWebGPUParticleRenderer::SetViewMatrix(const glm::mat4& matrix)
{
    m_viewMatrix = matrix;
}

void CWebGPUParticleRenderer::SetModelMatrix(const glm::mat4& matrix)
{
    m_modelMatrix = matrix;
}

void CWebGPUParticleRenderer::SetColor(const glm::vec4& color)
{
    // TODO(WP10)
}

void CWebGPUParticleRenderer::SetTexture(const Texture& texture)
{
    // TODO(WP10)
}

void CWebGPUParticleRenderer::SetTransparency(TransparencyMode mode)
{
    m_device->SetTransparency(mode);
}

void CWebGPUParticleRenderer::DrawParticle(PrimitiveType type, int count, const VertexParticle* vertices)
{
    // TODO(WP10)
}

} // namespace Gfx
