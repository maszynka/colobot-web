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
 * \file graphics/webgpu/wgpu_shadow_renderer.h
 * \brief WebGPU shadow renderer - STUB from WP4, implemented by WP10
 *
 * Compiles, keeps state, draws nothing. Follow docs/webgpu-renderer-guide.md;
 * WP10 edits only this file, wgpu_shadow_renderer.cpp and shaders/shadow_pass.wgsl.
 */

#pragma once

#include "graphics/core/renderers.h"
#include "graphics/core/color.h"
#include "graphics/core/texture.h"
#include "graphics/core/vertex.h"
#include "graphics/webgpu/wgpu_pass_manager.h"

#include <webgpu/webgpu_cpp.h>

#include <glm/glm.hpp>

namespace Gfx
{

class CWebGPUDevice;

class CWebGPUShadowRenderer : public CShadowRenderer
{
public:
    explicit CWebGPUShadowRenderer(CWebGPUDevice* device);
    ~CWebGPUShadowRenderer() override;

    void Begin() override;
    void End() override;
    void SetProjectionMatrix(const glm::mat4& matrix) override;
    void SetViewMatrix(const glm::mat4& matrix) override;
    void SetModelMatrix(const glm::mat4& matrix) override;
    void SetTexture(const Texture& texture) override;
    void SetShadowMap(const Texture& texture) override;
    void SetShadowRegion(const glm::vec2& offset, const glm::vec2& scale) override;
    void DrawObject(const CVertexBuffer* buffer, bool transparent) override;

private:
    CWebGPUDevice* m_device;
    wgpu::ShaderModule m_shader;
    glm::mat4 m_projectionMatrix = glm::mat4(1.0f);
    glm::mat4 m_viewMatrix = glm::mat4(1.0f);
    glm::mat4 m_modelMatrix = glm::mat4(1.0f);
    unsigned int m_shadowMap = 0;
    glm::ivec2 m_shadowMapSize = { 0, 0 };
    glm::vec2 m_regionOffset = { 0.0f, 0.0f };
    glm::vec2 m_regionScale = { 1.0f, 1.0f };
    RenderTarget m_previousTarget;
    bool m_active = false;
};

} // namespace Gfx
