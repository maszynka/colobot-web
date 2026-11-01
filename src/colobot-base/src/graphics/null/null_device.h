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
 * \file graphics/null/null_device.h
 * \brief No-op graphics device and renderers (colobot-web WP0 link smoke)
 */

#pragma once

#include "graphics/core/device.h"
#include "graphics/core/framebuffer.h"
#include "graphics/core/renderers.h"

#include <map>

namespace Gfx
{

class CNullVertexBuffer : public CVertexBuffer
{
public:
    CNullVertexBuffer(PrimitiveType type, size_t size) : CVertexBuffer(type, size) {}
    void Update() override {}
};

class CNullFramebuffer : public CFramebuffer
{
public:
    explicit CNullFramebuffer(const FramebufferParams& params, bool isDefault = false)
        : m_params(params), m_default(isDefault) {}
    bool Create() override { return true; }
    void Destroy() override {}
    bool IsDefault() override { return m_default; }
    int GetWidth() override { return m_params.width; }
    int GetHeight() override { return m_params.height; }
    int GetDepth() override { return m_params.depth; }
    int GetSamples() override { return m_params.samples; }
    int GetColorTexture() override { return 0; }
    int GetDepthTexture() override { return 0; }
    void Bind() override {}
    void Unbind() override {}
    void CopyToScreen(int, int, int, int, int, int, int, int) override {}
private:
    FramebufferParams m_params;
    bool m_default;
};

class CNullUIRenderer : public CUIRenderer
{
public:
    void SetProjection(float, float, float, float) override {}
    void SetTexture(const Texture&) override {}
    void SetColor(const glm::vec4&) override {}
    void SetTransparency(TransparencyMode) override {}
    Vertex2D* BeginPrimitive(PrimitiveType type, int count) override;
    Vertex2D* BeginPrimitives(PrimitiveType type, int drawCount, const int* counts) override;
    bool EndPrimitive() override { return true; }
private:
    std::vector<Vertex2D> m_buffer;
};

class CNullTerrainRenderer : public CTerrainRenderer
{
public:
    void Begin() override {}
    void End() override {}
    void SetProjectionMatrix(const glm::mat4&) override {}
    void SetViewMatrix(const glm::mat4&) override {}
    void SetModelMatrix(const glm::mat4&) override {}
    void SetAlbedoColor(const Color&) override {}
    void SetAlbedoTexture(const Texture&) override {}
    void SetEmissiveColor(const Color&) override {}
    void SetEmissiveTexture(const Texture&) override {}
    void SetMaterialParams(float, float, float) override {}
    void SetMaterialTexture(const Texture&) override {}
    void SetDetailTexture(const Texture&) override {}
    void SetShadowMap(const Texture&) override {}
    void SetLight(const glm::vec4&, const float&, const glm::vec3&) override {}
    void SetSky(const Color&, float) override {}
    void SetShadowParams(int, const ShadowParam*) override {}
    void SetFog(float, float, const glm::vec3&) override {}
    void DrawObject(const glm::mat4&, const CVertexBuffer*) override {}
};

class CNullObjectRenderer : public CObjectRenderer
{
public:
    void Begin() override {}
    void End() override {}
    void SetProjectionMatrix(const glm::mat4&) override {}
    void SetViewMatrix(const glm::mat4&) override {}
    void SetModelMatrix(const glm::mat4&) override {}
    void SetAlbedoColor(const Color&) override {}
    void SetAlbedoTexture(const Texture&) override {}
    void SetEmissiveColor(const Color&) override {}
    void SetEmissiveTexture(const Texture&) override {}
    void SetMaterialParams(float, float, float) override {}
    void SetMaterialTexture(const Texture&) override {}
    void SetDetailTexture(const Texture&) override {}
    void SetShadowMap(const Texture&) override {}
    void SetLighting(bool) override {}
    void SetLight(const glm::vec4&, const float&, const glm::vec3&) override {}
    void SetSky(const Color&, float) override {}
    void SetShadowParams(int, const ShadowParam*) override {}
    void SetFog(float, float, const glm::vec3&) override {}
    void SetAlphaScissor(float) override {}
    void SetRecolor(bool, const glm::vec3&, const glm::vec3&, float) override {}
    void SetDepthTest(bool) override {}
    void SetDepthMask(bool) override {}
    void SetCullFace(CullFace) override {}
    void SetTransparency(TransparencyMode) override {}
    void SetUVTransform(const glm::vec2&, const glm::vec2&) override {}
    void SetTriplanarMode(bool) override {}
    void SetTriplanarScale(float) override {}
    void DrawObject(const CVertexBuffer*) override {}
    void DrawPrimitive(PrimitiveType, int, const Vertex3D*) override {}
    void DrawPrimitives(PrimitiveType, int, int[], const Vertex3D*) override {}
};

class CNullParticleRenderer : public CParticleRenderer
{
public:
    void Begin() override {}
    void End() override {}
    void SetProjectionMatrix(const glm::mat4&) override {}
    void SetViewMatrix(const glm::mat4&) override {}
    void SetModelMatrix(const glm::mat4&) override {}
    void SetColor(const glm::vec4&) override {}
    void SetTexture(const Texture&) override {}
    void SetTransparency(TransparencyMode) override {}
    void DrawParticle(PrimitiveType, int, const VertexParticle*) override {}
};

class CNullShadowRenderer : public CShadowRenderer
{
public:
    void Begin() override {}
    void End() override {}
    void SetProjectionMatrix(const glm::mat4&) override {}
    void SetViewMatrix(const glm::mat4&) override {}
    void SetModelMatrix(const glm::mat4&) override {}
    void SetTexture(const Texture&) override {}
    void SetShadowMap(const Texture&) override {}
    void SetShadowRegion(const glm::vec2&, const glm::vec2&) override {}
    void DrawObject(const CVertexBuffer*, bool) override {}
};

/**
 * \class CNullDevice
 * \brief Device that accepts every call and draws nothing.
 *
 * Used by the web build until the WebGPU backend (WP4) lands, and usable
 * natively for headless runs.
 */
class CNullDevice : public CDevice
{
public:
    explicit CNullDevice(const DeviceConfig& config);
    ~CNullDevice() override;

    std::string GetName() override { return "null"; }
    bool Create() override;
    void Destroy() override;
    void ConfigChanged(const DeviceConfig& newConfig) override;

    void BeginScene() override {}
    void EndScene() override {}
    void Clear() override {}

    CUIRenderer* GetUIRenderer() override { return &m_uiRenderer; }
    CTerrainRenderer* GetTerrainRenderer() override { return &m_terrainRenderer; }
    CObjectRenderer* GetObjectRenderer() override { return &m_objectRenderer; }
    CParticleRenderer* GetParticleRenderer() override { return &m_particleRenderer; }
    CShadowRenderer* GetShadowRenderer() override { return &m_shadowRenderer; }

    Texture CreateTexture(CImage* image, const TextureCreateParams& params) override;
    Texture CreateTexture(ImageData* data, const TextureCreateParams& params) override;
    Texture CreateDepthTexture(int width, int height, int depth) override;
    void UpdateTexture(const Texture&, const glm::ivec2&, ImageData*, TextureFormat) override {}
    void DestroyTexture(const Texture&) override {}
    void DestroyAllTextures() override {}

    CVertexBuffer* CreateVertexBuffer(PrimitiveType primitiveType, const Vertex3D* vertices, int vertexCount) override;
    void DestroyVertexBuffer(CVertexBuffer* buffer) override;

    void SetViewport(int, int, int, int) override {}
    void SetDepthTest(bool) override {}
    void SetDepthMask(bool) override {}
    void SetCullFace(CullFace) override {}
    void SetTransparency(TransparencyMode) override {}
    void SetColorMask(bool, bool, bool, bool) override {}
    void SetClearColor(const Color&) override {}

    void CopyFramebufferToTexture(Texture&, int, int, int, int, int, int) override {}
    std::unique_ptr<CFrameBufferPixels> GetFrameBufferPixels() const override;

    CFramebuffer* GetFramebuffer(std::string name) override;
    CFramebuffer* CreateFramebuffer(std::string name, const FramebufferParams& params) override;
    void DeleteFramebuffer(std::string name) override;

    bool IsAnisotropySupported() override { return false; }
    int GetMaxAnisotropyLevel() override { return 1; }
    int GetMaxSamples() override { return 1; }
    bool IsShadowMappingSupported() override { return false; }
    int GetMaxTextureSize() override { return 4096; }
    bool IsFramebufferSupported() override { return false; }

private:
    Texture MakeTexture(int width, int height, bool alpha);

    DeviceConfig m_config;
    unsigned int m_nextTextureId = 1;
    CNullUIRenderer m_uiRenderer;
    CNullTerrainRenderer m_terrainRenderer;
    CNullObjectRenderer m_objectRenderer;
    CNullParticleRenderer m_particleRenderer;
    CNullShadowRenderer m_shadowRenderer;
    std::map<std::string, std::unique_ptr<CNullFramebuffer>> m_framebuffers;
};

} // namespace Gfx
