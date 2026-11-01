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
 * \file graphics/webgpu/wgpu_object_renderer.h
 * \brief WebGPU object renderer (WP9), port of CGL33ObjectRenderer
 *
 * group(0) shared scene, group(1) Uniforms (ring-allocated, dynamic offset),
 * group(2) albedo/detail/emissive/material textures + samplers.
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

#include <array>
#include <map>
#include <vector>

namespace Gfx
{

class CWebGPUDevice;
struct PipelineKey;

class CWebGPUObjectRenderer : public CObjectRenderer
{
public:
    explicit CWebGPUObjectRenderer(CWebGPUDevice* device);
    ~CWebGPUObjectRenderer() override;

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
    void SetLighting(bool enabled) override;
    void SetLight(const glm::vec4& position, const float& intensity, const glm::vec3& color) override;
    void SetSky(const Color& color, float intensity) override;
    void SetShadowParams(int count, const ShadowParam* params) override;
    void SetFog(float min, float max, const glm::vec3& color) override;
    void SetAlphaScissor(float alpha) override;
    void SetRecolor(bool enabled, const glm::vec3& from, const glm::vec3& to, float threshold) override;
    void SetDepthTest(bool enabled) override;
    void SetDepthMask(bool enabled) override;
    void SetCullFace(CullFace mode) override;
    void SetTransparency(TransparencyMode mode) override;
    void SetUVTransform(const glm::vec2& offset, const glm::vec2& scale) override;
    void SetTriplanarMode(bool enabled) override;
    void SetTriplanarScale(float scale) override;
    void DrawObject(const CVertexBuffer* buffer) override;
    void DrawPrimitive(PrimitiveType type, int count, const Vertex3D* vertices) override;
    void DrawPrimitives(PrimitiveType type, int drawCount, int count[], const Vertex3D* vertices) override;

private:
    //! WGSL mirror: struct ObjectUniforms (shaders/object.wgsl), 384 bytes
    struct alignas(16) Uniforms
    {
        glm::mat4 projection = glm::mat4(1.0f);
        glm::mat4 view = glm::mat4(1.0f);
        glm::mat4 model = glm::mat4(1.0f);
        glm::mat4 normalMatrix = glm::mat4(1.0f);
        glm::vec4 albedoColor = { 1.0f, 1.0f, 1.0f, 1.0f };
        glm::vec3 emissiveColor = { 0.0f, 0.0f, 0.0f };
        float roughness = 1.0f;
        glm::vec3 recolorFrom = { 0.0f, 0.0f, 0.0f };
        float metalness = 0.0f;
        glm::vec3 recolorTo = { 0.0f, 0.0f, 0.0f };
        float aoStrength = 0.0f;
        glm::vec3 fogColor = { 0.0f, 0.0f, 0.0f };
        float recolorThreshold = 0.0f;
        glm::vec2 fogRange = { 1e+6f, 1e+6f };
        glm::vec2 uvOffset = { 0.0f, 0.0f };
        glm::vec2 uvScale = { 1.0f, 1.0f };
        float alphaScissor = 0.0f;
        float triplanarScale = 1.0f;
        uint32_t triplanarMode = 0;
        uint32_t recolor = 0;
        uint32_t pad0 = 0;
        uint32_t pad1 = 0;
    };
    static_assert(sizeof(Uniforms) == 384, "Uniforms must match WGSL ObjectUniforms");

    enum TextureSlot { ALBEDO = 0, DETAIL, EMISSIVE, MATERIAL, SLOT_COUNT };

    Uniforms& U() { m_uniformsDirty = true; return m_uniforms; }
    void SetSlot(TextureSlot slot, unsigned int id);
    wgpu::RenderPipeline CreatePipeline(const PipelineKey& key);
    //! Sets pipeline + all bind groups; false when the draw must be skipped
    bool Prepare(wgpu::RenderPassEncoder& pass, wgpu::PrimitiveTopology topology);
    void BindUniforms(wgpu::RenderPassEncoder& pass);
    void BindTextures(wgpu::RenderPassEncoder& pass);

    CWebGPUDevice* m_device;
    wgpu::ShaderModule m_shader;
    CWebGPUSceneBindings m_scene;   //!< group(0)

    wgpu::BindGroupLayout m_uniformLayout;
    wgpu::BindGroupLayout m_textureLayout;
    wgpu::PipelineLayout m_pipelineLayout;

    Uniforms m_uniforms;
    bool m_uniformsDirty = true;
    uint64_t m_uniformFrame = UINT64_MAX;
    uint32_t m_uniformOffset = 0;
    wgpu::Buffer m_uniformBuffer;
    wgpu::BindGroup m_uniformGroup;
    WGPUBuffer m_uniformGroupBuffer = nullptr;

    std::array<unsigned int, SLOT_COUNT> m_textures = {};
    uint64_t m_textureEpoch = UINT64_MAX;
    std::map<std::array<uint64_t, SLOT_COUNT>, wgpu::BindGroup> m_textureGroups;

    std::vector<Vertex3D> m_converted;
    std::vector<int> m_counts;
};

} // namespace Gfx
