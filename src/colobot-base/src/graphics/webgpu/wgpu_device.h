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
 * \file graphics/webgpu/wgpu_device.h
 * \brief WebGPU implementation of Gfx::CDevice (colobot-web WP4)
 *
 * Owns instance/adapter/device/surface, frame encoder, pass manager, ring buffers,
 * pipeline cache, texture registry and framebuffers. Renderers live in wgpu_*_renderer.*;
 * see docs/webgpu-renderer-guide.md for the pattern they follow.
 */

#pragma once

#include "graphics/core/device.h"
#include "graphics/core/material.h"
#include "graphics/core/transparency.h"

#include "graphics/webgpu/wgpu_pass_manager.h"
#include "graphics/webgpu/wgpu_pipeline_cache.h"
#include "graphics/webgpu/wgpu_ring_buffer.h"
#include "graphics/webgpu/wgpu_texture.h"

#include <webgpu/webgpu_cpp.h>

#include <map>
#include <memory>
#include <set>
#include <string>

namespace Gfx
{

class CWebGPUFramebuffer;
class CWebGPUUIRenderer;
class CWebGPUTerrainRenderer;
class CWebGPUObjectRenderer;
class CWebGPUParticleRenderer;
class CWebGPUShadowRenderer;

//! PipelineKey::renderer values
enum class RendererId : uint32_t
{
    UI = 1,
    Terrain = 2,
    Object = 3,
    Particle = 4,
    Shadow = 5,
};

/**
 * \class CWebGPUVertexBuffer
 * \brief Static vertex buffer (Vertex3D). FAN/LOOP/POINTS are converted to lists on Update().
 */
class CWebGPUVertexBuffer : public CVertexBuffer
{
public:
    CWebGPUVertexBuffer(wgpu::Device device, PrimitiveType type, size_t size);
    void Update() override;

    const wgpu::Buffer& GetGPUBuffer() const { return m_buffer; }
    uint32_t GetGPUVertexCount() const { return m_gpuCount; }
    wgpu::PrimitiveTopology GetGPUTopology() const { return m_topology; }

private:
    wgpu::Device m_device;
    wgpu::Buffer m_buffer;
    uint64_t m_capacity = 0;
    uint32_t m_gpuCount = 0;
    wgpu::PrimitiveTopology m_topology = wgpu::PrimitiveTopology::TriangleList;
};

class CWebGPUDevice : public CDevice
{
public:
    explicit CWebGPUDevice(const DeviceConfig& config);
    ~CWebGPUDevice() override;

    std::string GetName() override;
    bool Create() override;
    void Destroy() override;
    void ConfigChanged(const DeviceConfig& newConfig) override;

    void BeginScene() override;
    void EndScene() override;
    void Clear() override;

    CUIRenderer* GetUIRenderer() override;
    CTerrainRenderer* GetTerrainRenderer() override;
    CObjectRenderer* GetObjectRenderer() override;
    CParticleRenderer* GetParticleRenderer() override;
    CShadowRenderer* GetShadowRenderer() override;

    Texture CreateTexture(CImage* image, const TextureCreateParams& params) override;
    Texture CreateTexture(ImageData* data, const TextureCreateParams& params) override;
    Texture CreateDepthTexture(int width, int height, int depth) override;
    void UpdateTexture(const Texture& texture, const glm::ivec2& offset, ImageData* data, TextureFormat format) override;
    void DestroyTexture(const Texture& texture) override;
    void DestroyAllTextures() override;

    CVertexBuffer* CreateVertexBuffer(PrimitiveType primitiveType, const Vertex3D* vertices, int vertexCount) override;
    void DestroyVertexBuffer(CVertexBuffer*) override;

    void SetViewport(int x, int y, int width, int height) override;
    void SetDepthTest(bool enabled) override;
    void SetDepthMask(bool enabled) override;
    void SetCullFace(CullFace mode) override;
    void SetTransparency(TransparencyMode mode) override;
    void SetColorMask(bool red, bool green, bool blue, bool alpha) override;
    void SetClearColor(const Color& color) override;

    void CopyFramebufferToTexture(Texture& texture, int xOffset, int yOffset, int x, int y, int width, int height) override;
    std::unique_ptr<CFrameBufferPixels> GetFrameBufferPixels() const override;

    CFramebuffer* GetFramebuffer(std::string name) override;
    CFramebuffer* CreateFramebuffer(std::string name, const FramebufferParams& params) override;
    void DeleteFramebuffer(std::string name) override;

    bool IsAnisotropySupported() override;
    int GetMaxAnisotropyLevel() override;
    int GetMaxSamples() override;
    bool IsShadowMappingSupported() override;
    int GetMaxTextureSize() override;
    bool IsFramebufferSupported() override;

    // ---- WebGPU-specific API used by renderers / framebuffers ----

    const wgpu::Device& GetWGPUDevice() const { return m_device; }
    const wgpu::Queue& GetQueue() const { return m_queue; }
    CWebGPUPassManager& Passes() { return m_passes; }
    CWebGPUTextureManager& Textures() { return m_textures; }
    CWebGPUPipelineCache& Pipelines() { return m_pipelines; }
    CWebGPURingBuffer& VertexRing() { return *m_vertexRing; }
    CWebGPURingBuffer& UniformRing() { return *m_uniformRing; }
    //! Incremented every BeginScene()
    uint64_t FrameIndex() const { return m_frameIndex; }

    wgpu::TextureFormat SurfaceFormat() const { return m_surfaceFormat; }
    static constexpr wgpu::TextureFormat DepthFormat = wgpu::TextureFormat::Depth32Float;

    //! Empty layout / group for unused group(0) (UI renderer)
    const wgpu::BindGroupLayout& EmptyBindGroupLayout() const { return m_emptyLayout; }
    const wgpu::BindGroup& EmptyBindGroup() const { return m_emptyGroup; }
    //! Frozen group(0) layout shared by 3D renderers (see wgpu_scene.h)
    const wgpu::BindGroupLayout& SceneBindGroupLayout() const { return m_sceneLayout; }
    //! 1x1 Depth32Float texture used when no shadow map is bound
    const WebGPUTextureEntry& DummyDepth() const { return m_dummyDepth; }

    //! Pipeline key pre-filled from current device state + current pass target
    PipelineKey MakePipelineKey(RendererId renderer, wgpu::PrimitiveTopology topology) const;
    //! Fills fixed-function parts of a pipeline descriptor from key (blend, depth, cull, formats, samples)
    struct FixedFunction
    {
        wgpu::BlendState blend;
        wgpu::ColorTargetState colorTarget;
        wgpu::DepthStencilState depthStencil;
        wgpu::FragmentState fragment;   //!< caller sets module + entryPoint
    };
    void FillFixedFunction(const PipelineKey& key, wgpu::RenderPipelineDescriptor& desc, FixedFunction& storage) const;

    bool GetDepthTest() const { return m_depthTest; }
    bool GetDepthMask() const { return m_depthMask; }
    CullFace GetCullFace() const { return m_cullFace; }
    TransparencyMode GetTransparency() const { return m_transparency; }

    //! Called by CWebGPUFramebuffer::Bind()/Unbind()
    void BindFramebuffer(CWebGPUFramebuffer* framebuffer);
    //! Render target for the swapchain image of this frame
    RenderTarget ScreenTarget() const;
    //! Current swapchain texture (valid between BeginScene and EndScene)
    const wgpu::Texture& CurrentSurfaceTexture() const { return m_surfaceTexture; }

private:
    void ConfigureSurface();
    void CreateScreenDepth();

    DeviceConfig m_config;

    wgpu::Instance m_instance;
    wgpu::Adapter m_adapter;
    wgpu::Device m_device;
    wgpu::Queue m_queue;
    wgpu::Surface m_surface;
    wgpu::TextureFormat m_surfaceFormat = wgpu::TextureFormat::RGBA8Unorm;
    wgpu::Texture m_surfaceTexture;
    wgpu::TextureView m_surfaceView;
    wgpu::Texture m_screenDepth;
    wgpu::TextureView m_screenDepthView;
    wgpu::CommandEncoder m_encoder;

    wgpu::BindGroupLayout m_emptyLayout;
    wgpu::BindGroup m_emptyGroup;
    wgpu::BindGroupLayout m_sceneLayout;
    WebGPUTextureEntry m_dummyDepth;

    CWebGPUPassManager m_passes;
    CWebGPUTextureManager m_textures;
    CWebGPUPipelineCache m_pipelines;
    std::unique_ptr<CWebGPURingBuffer> m_vertexRing;
    std::unique_ptr<CWebGPURingBuffer> m_uniformRing;
    uint64_t m_frameIndex = 0;

    std::map<std::string, std::unique_ptr<CWebGPUFramebuffer>> m_framebuffers;
    CWebGPUFramebuffer* m_boundFramebuffer = nullptr;
    std::set<CVertexBuffer*> m_buffers;

    std::unique_ptr<CWebGPUUIRenderer> m_uiRenderer;
    std::unique_ptr<CWebGPUTerrainRenderer> m_terrainRenderer;
    std::unique_ptr<CWebGPUObjectRenderer> m_objectRenderer;
    std::unique_ptr<CWebGPUParticleRenderer> m_particleRenderer;
    std::unique_ptr<CWebGPUShadowRenderer> m_shadowRenderer;

    bool m_depthTest = false;
    bool m_depthMask = true;
    CullFace m_cullFace = CullFace::NONE;
    TransparencyMode m_transparency = TransparencyMode::NONE;
    bool m_colorMask[4] = { true, true, true, true };
};

} // namespace Gfx
