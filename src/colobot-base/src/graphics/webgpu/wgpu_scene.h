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
 * \file graphics/webgpu/wgpu_scene.h
 * \brief FROZEN shared scene interface: bind group 0 (lighting + shadow) for 3D renderers (colobot-web WP4)
 *
 * C++ mirrors of the WGSL structs in shaders/preamble.wgsl. Do not change without the WP4 owner
 * (Phase 2 change requests go to WP10b "shared WGSL reconcile").
 */

#pragma once

#include "graphics/core/renderers.h"

#include <webgpu/webgpu_cpp.h>

#include <glm/glm.hpp>

#include <cstdint>

namespace Gfx
{

class CWebGPUDevice;

//! WGSL: struct LightUniforms (preamble.wgsl)
struct alignas(16) LightUniforms
{
    glm::vec3 cameraPosition = { 0.0f, 0.0f, 0.0f };
    float lightIntensity = 1.0f;
    glm::vec4 lightPosition = { 0.0f, 0.0f, 1.0f, 0.0f };
    glm::vec3 lightColor = { 1.0f, 1.0f, 1.0f };
    float skyIntensity = 1.0f;
    glm::vec3 skyColor = { 1.0f, 1.0f, 1.0f };
    uint32_t lighting = 1;
};
static_assert(sizeof(LightUniforms) == 64, "LightUniforms must match WGSL layout");

//! WGSL: struct ShadowParam (preamble.wgsl)
struct alignas(16) ShadowParamGPU
{
    glm::mat4 transform = glm::mat4(1.0f);
    glm::vec2 uvOffset = { 0.0f, 0.0f };
    glm::vec2 uvScale = { 1.0f, 1.0f };
};
static_assert(sizeof(ShadowParamGPU) == 80, "ShadowParam must match WGSL layout");

//! WGSL: struct ShadowUniforms (preamble.wgsl)
struct alignas(16) ShadowUniforms
{
    ShadowParamGPU params[4];
    int32_t regions = 0;
    int32_t pad[3] = {};
};
static_assert(sizeof(ShadowUniforms) == 336, "ShadowUniforms must match WGSL layout");

/**
 * \class CWebGPUSceneBindings
 * \brief Per-renderer instance of group(0). Renderers set fields, then call Bind() before drawing.
 *
 * Bind() ring-allocates both uniform blocks (dynamic offsets) only when they changed since the
 * last Bind() of this frame, and rebuilds the bind group when the ring buffer or shadow map changes.
 */
class CWebGPUSceneBindings
{
public:
    explicit CWebGPUSceneBindings(CWebGPUDevice* device) : m_device(device) {}

    LightUniforms& Light() { m_dirty = true; return m_light; }
    void SetShadowParams(int count, const ShadowParam* params);
    void SetShadowMap(unsigned int textureId) { m_shadowMap = textureId; }

    //! setBindGroup(0, ...) on pass
    void Bind(wgpu::RenderPassEncoder& pass);

    //! Device-wide layout of group(0) (identical for all renderers)
    static wgpu::BindGroupLayout CreateLayout(const wgpu::Device& device);

private:
    CWebGPUDevice* m_device;
    LightUniforms m_light;
    ShadowUniforms m_shadow;
    unsigned int m_shadowMap = 0;
    bool m_dirty = true;
    uint64_t m_frame = ~0ull;
    uint32_t m_offsets[2] = {};
    wgpu::Buffer m_allocBuffer;
    wgpu::BindGroup m_bindGroup;
    WGPUBuffer m_bgBuffer = nullptr;
    uint64_t m_bgShadowSerial = 0;
};

} // namespace Gfx
