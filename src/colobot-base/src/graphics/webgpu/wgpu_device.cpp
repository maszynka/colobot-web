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

#include "graphics/webgpu/wgpu_device.h"

#include "graphics/webgpu/wgpu_framebuffer.h"
#include "graphics/webgpu/wgpu_scene.h"
#include "graphics/webgpu/wgpu_util.h"
#include "graphics/webgpu/wgpu_ui_renderer.h"
#include "graphics/webgpu/wgpu_terrain_renderer.h"
#include "graphics/webgpu/wgpu_object_renderer.h"
#include "graphics/webgpu/wgpu_particle_renderer.h"
#include "graphics/webgpu/wgpu_shadow_renderer.h"

#include "graphics/core/vertex.h"
#include "graphics/engine/engine.h"

#include "common/image.h"
#include "common/logger.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace Gfx
{

// ---------------------------------------------------------------------------
// CWebGPUVertexBuffer
// ---------------------------------------------------------------------------

CWebGPUVertexBuffer::CWebGPUVertexBuffer(wgpu::Device device, PrimitiveType type, size_t size)
    : CVertexBuffer(type, size), m_device(std::move(device))
{
}

void CWebGPUVertexBuffer::Update()
{
    std::vector<Vertex3D> converted;
    const Vertex3D* data = m_data.data();
    size_t count = m_data.size();
    m_topology = NativeTopology(m_type);
    if (m_topology == wgpu::PrimitiveTopology::Undefined)
    {
        ConvertPrimitives(m_type, m_data.data(), static_cast<int>(m_data.size()), converted);
        m_topology = ConvertedTopology(m_type);
        data = converted.data();
        count = converted.size();
    }

    m_gpuCount = static_cast<uint32_t>(count);
    uint64_t bytes = AlignUp(std::max<uint64_t>(count * sizeof(Vertex3D), 4), 4);
    if (!m_buffer || bytes > m_capacity)
    {
        if (m_buffer) m_buffer.Destroy();
        wgpu::BufferDescriptor desc;
        desc.label = "static-vertex-buffer";
        desc.size = bytes;
        desc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
        m_buffer = m_device.CreateBuffer(&desc);
        m_capacity = bytes;
    }
    if (count > 0)
    {
        // WriteBuffer size must be a multiple of 4: sizeof(Vertex3D) is 44 -> always fine
        m_device.GetQueue().WriteBuffer(m_buffer, 0, data, count * sizeof(Vertex3D));
    }
}

// ---------------------------------------------------------------------------
// Readback (stub)
// ---------------------------------------------------------------------------

namespace
{

/**
 * Screenshot readback stub. A real implementation copies the swapchain texture into a
 * MapRead buffer during EndScene and maps it with MapAsync + instance.WaitAny (JSPI).
 * TODO(WP12): implement; for now returns opaque black pixels of the right size so
 * CEngine::WriteScreenShot keeps working.
 */
class CWebGPUFrameBufferPixels : public CFrameBufferPixels
{
public:
    explicit CWebGPUFrameBufferPixels(size_t size) : m_pixels(size, 0)
    {
        for (size_t i = 3; i < size; i += 4) m_pixels[i] = 255;
    }
    void* GetPixelsData() override { return m_pixels.data(); }

private:
    std::vector<uint8_t> m_pixels;
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// CWebGPUDevice
// ---------------------------------------------------------------------------

CWebGPUDevice::CWebGPUDevice(const DeviceConfig& config)
    : m_config(config)
{
}

CWebGPUDevice::~CWebGPUDevice()
{
    Destroy();
}

std::string CWebGPUDevice::GetName()
{
    return "webgpu";
}

bool CWebGPUDevice::Create()
{
    GetLogger()->Info("Creating WebGPU device");

    // Instance: TimedWaitAny is required for WaitAny(timeout > 0), which suspends via JSPI
    wgpu::InstanceFeatureName features[] = { wgpu::InstanceFeatureName::TimedWaitAny };
    wgpu::InstanceDescriptor instanceDesc;
    instanceDesc.requiredFeatureCount = 1;
    instanceDesc.requiredFeatures = features;
    m_instance = wgpu::CreateInstance(&instanceDesc);
    if (!m_instance)
    {
        m_errorMessage = "WebGPU: could not create instance (is WebGPU available?)";
        GetLogger()->Error("%%", m_errorMessage);
        return false;
    }

    // Surface on the canvas SDL created (SDL 2.32 hard-codes "#canvas")
    wgpu::EmscriptenSurfaceSourceCanvasHTMLSelector canvas;
    canvas.selector = wgpu::StringView(std::string_view("#canvas"));
    wgpu::SurfaceDescriptor surfaceDesc;
    surfaceDesc.nextInChain = &canvas;
    m_surface = m_instance.CreateSurface(&surfaceDesc);

    // Adapter
    wgpu::RequestAdapterOptions adapterOptions;
    adapterOptions.compatibleSurface = m_surface;
    adapterOptions.powerPreference = wgpu::PowerPreference::HighPerformance;
    wgpu::Future adapterFuture = m_instance.RequestAdapter(&adapterOptions, wgpu::CallbackMode::WaitAnyOnly,
        [this](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter, wgpu::StringView message)
        {
            if (status == wgpu::RequestAdapterStatus::Success)
                m_adapter = std::move(adapter);
            else
                GetLogger()->Error("WebGPU: RequestAdapter failed: %%", std::string(std::string_view(message)));
        });
    m_instance.WaitAny(adapterFuture, UINT64_MAX);
    if (!m_adapter)
    {
        m_errorMessage = "WebGPU: no adapter";
        return false;
    }

    wgpu::Limits adapterLimits;
    m_adapter.GetLimits(&adapterLimits);

    // Device
    wgpu::Limits required;
    required.maxTextureDimension2D = adapterLimits.maxTextureDimension2D;
    wgpu::DeviceDescriptor deviceDesc;
    deviceDesc.label = "colobot";
    deviceDesc.requiredLimits = &required;
    deviceDesc.SetUncapturedErrorCallback(
        [](const wgpu::Device&, wgpu::ErrorType type, wgpu::StringView message)
        {
            GetLogger()->Error("WebGPU error (%%): %%", static_cast<int>(type), std::string(std::string_view(message)));
        });
    deviceDesc.SetDeviceLostCallback(wgpu::CallbackMode::AllowSpontaneous,
        [](const wgpu::Device&, wgpu::DeviceLostReason reason, wgpu::StringView message)
        {
            GetLogger()->Error("WebGPU device lost (%%): %%", static_cast<int>(reason), std::string(std::string_view(message)));
        });
    wgpu::Future deviceFuture = m_adapter.RequestDevice(&deviceDesc, wgpu::CallbackMode::WaitAnyOnly,
        [this](wgpu::RequestDeviceStatus status, wgpu::Device device, wgpu::StringView message)
        {
            if (status == wgpu::RequestDeviceStatus::Success)
                m_device = std::move(device);
            else
                GetLogger()->Error("WebGPU: RequestDevice failed: %%", std::string(std::string_view(message)));
        });
    m_instance.WaitAny(deviceFuture, UINT64_MAX);
    if (!m_device)
    {
        m_errorMessage = "WebGPU: could not create device";
        return false;
    }
    m_queue = m_device.GetQueue();

    wgpu::Limits limits;
    m_device.GetLimits(&limits);
    m_capabilities.maxTextureSize = static_cast<int>(limits.maxTextureDimension2D);
    m_capabilities.maxTextures = 16;
    m_capabilities.multitexturingSupported = true;
    m_capabilities.shadowMappingSupported = true;
    m_capabilities.framebufferSupported = true;
    m_capabilities.maxRenderbufferSize = static_cast<int>(limits.maxTextureDimension2D);
    m_capabilities.anisotropySupported = true;
    m_capabilities.maxAnisotropy = 16;
    m_capabilities.multisamplingSupported = true;
    m_capabilities.maxSamples = 4;

    // Use the canvas' preferred format (navigator.gpu.getPreferredCanvasFormat(), bgra8unorm on
    // most platforms; capabilities list it first) to avoid a compositor copy. Offscreen color
    // framebuffers follow SurfaceFormat(), so fb->screen copies stay legal; uploaded textures stay
    // RGBA8Unorm (CopyFramebufferToTexture checks formats; the engine never calls it).
    m_surfaceFormat = wgpu::TextureFormat::RGBA8Unorm;
    {
        wgpu::SurfaceCapabilities caps;
        if (m_surface.GetCapabilities(m_adapter, &caps) == wgpu::Status::Success && caps.formatCount > 0 &&
            (caps.formats[0] == wgpu::TextureFormat::BGRA8Unorm || caps.formats[0] == wgpu::TextureFormat::RGBA8Unorm))
            m_surfaceFormat = caps.formats[0];
    }
    GetLogger()->Info("WebGPU canvas format: %%", m_surfaceFormat == wgpu::TextureFormat::BGRA8Unorm ? "bgra8unorm" : "rgba8unorm");
    ConfigureSurface();

    m_textures.Init(m_device, m_queue);

    // Shared objects
    wgpu::BindGroupLayoutDescriptor emptyDesc;
    m_emptyLayout = m_device.CreateBindGroupLayout(&emptyDesc);
    wgpu::BindGroupDescriptor emptyGroup;
    emptyGroup.layout = m_emptyLayout;
    m_emptyGroup = m_device.CreateBindGroup(&emptyGroup);
    m_sceneLayout = CWebGPUSceneBindings::CreateLayout(m_device);

    {
        wgpu::TextureDescriptor desc;
        desc.label = "dummy-depth";
        desc.size = { 1, 1, 1 };
        desc.format = DepthFormat;
        desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::RenderAttachment;
        m_dummyDepth.texture = m_device.CreateTexture(&desc);
        m_dummyDepth.view = m_dummyDepth.texture.CreateView();
        m_dummyDepth.sampler = m_textures.GetComparisonSampler();
        m_dummyDepth.depth = true;
        m_dummyDepth.format = DepthFormat;
        m_dummyDepth.width = m_dummyDepth.height = 1;
        m_dummyDepth.serial = ~0ull;
    }

    m_vertexRing = std::make_unique<CWebGPURingBuffer>(m_device, wgpu::BufferUsage::Vertex, 4 << 20, 4, "vertex-ring");
    m_uniformRing = std::make_unique<CWebGPURingBuffer>(m_device, wgpu::BufferUsage::Uniform, 1 << 20, WGPU_UNIFORM_ALIGN, "uniform-ring");

    FramebufferParams fbParams;
    fbParams.width = m_config.size.x;
    fbParams.height = m_config.size.y;
    fbParams.depth = m_config.depthSize;
    m_framebuffers["default"] = std::make_unique<CWebGPUFramebuffer>(this, fbParams, true);

    m_uiRenderer = std::make_unique<CWebGPUUIRenderer>(this);
    m_terrainRenderer = std::make_unique<CWebGPUTerrainRenderer>(this);
    m_objectRenderer = std::make_unique<CWebGPUObjectRenderer>(this);
    m_particleRenderer = std::make_unique<CWebGPUParticleRenderer>(this);
    m_shadowRenderer = std::make_unique<CWebGPUShadowRenderer>(this);

    GetLogger()->Info("WebGPU device created, max texture size %%", m_capabilities.maxTextureSize);
    return true;
}

void CWebGPUDevice::ConfigureSurface()
{
    wgpu::SurfaceConfiguration config;
    config.device = m_device;
    config.format = m_surfaceFormat;
    config.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc | wgpu::TextureUsage::CopyDst;
    config.width = static_cast<uint32_t>(std::max(1, m_config.size.x));
    config.height = static_cast<uint32_t>(std::max(1, m_config.size.y));
    config.alphaMode = wgpu::CompositeAlphaMode::Opaque;
    config.presentMode = wgpu::PresentMode::Fifo;
    m_surface.Configure(&config);
    CreateScreenDepth();
}

void CWebGPUDevice::CreateScreenDepth()
{
    if (m_screenDepth) m_screenDepth.Destroy();
    wgpu::TextureDescriptor desc;
    desc.label = "screen-depth";
    desc.size = { static_cast<uint32_t>(std::max(1, m_config.size.x)), static_cast<uint32_t>(std::max(1, m_config.size.y)), 1 };
    desc.format = DepthFormat;
    desc.usage = wgpu::TextureUsage::RenderAttachment;
    m_screenDepth = m_device.CreateTexture(&desc);
    m_screenDepthView = m_screenDepth.CreateView();
}

void CWebGPUDevice::Destroy()
{
    if (!m_device) return;

    m_uiRenderer.reset();
    m_terrainRenderer.reset();
    m_objectRenderer.reset();
    m_particleRenderer.reset();
    m_shadowRenderer.reset();

    for (CVertexBuffer* buffer : m_buffers) delete buffer;
    m_buffers.clear();

    m_boundFramebuffer = nullptr;
    m_framebuffers.clear();
    m_pipelines.Clear();
    m_textures.Shutdown();
    m_vertexRing.reset();
    m_uniformRing.reset();

    m_dummyDepth = WebGPUTextureEntry{};
    m_emptyGroup = nullptr;
    m_emptyLayout = nullptr;
    m_sceneLayout = nullptr;
    m_screenDepthView = nullptr;
    m_screenDepth = nullptr;
    m_surfaceView = nullptr;
    m_surfaceTexture = nullptr;
    m_encoder = nullptr;
    m_surface = nullptr;
    m_queue = nullptr;
    m_device = nullptr;
    m_adapter = nullptr;
    m_instance = nullptr;
}

void CWebGPUDevice::ConfigChanged(const DeviceConfig& newConfig)
{
    m_config = newConfig;
    if (!m_device) return;
    ConfigureSurface();
    auto it = m_framebuffers.find("default");
    if (it != m_framebuffers.end()) it->second->Resize(m_config.size.x, m_config.size.y);
}

RenderTarget CWebGPUDevice::ScreenTarget() const
{
    RenderTarget t;
    t.identity = this;
    t.colorTexture = m_surfaceTexture;
    t.color = m_surfaceView;
    t.depthTexture = m_screenDepth;
    t.depth = m_screenDepthView;
    t.colorFormat = m_surfaceFormat;
    t.depthFormat = DepthFormat;
    t.samples = 1;
    t.width = m_config.size.x;
    t.height = m_config.size.y;
    return t;
}

void CWebGPUDevice::BeginScene()
{
    if (!m_device) return;
    if (m_passes.InFrame()) EndScene();

    ++m_frameIndex;
    m_vertexRing->BeginFrame();
    m_uniformRing->BeginFrame();

    wgpu::SurfaceTexture surfaceTexture;
    m_surface.GetCurrentTexture(&surfaceTexture);
    if (surfaceTexture.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal
        && surfaceTexture.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal)
    {
        GetLogger()->Warn("WebGPU: GetCurrentTexture failed (%%), reconfiguring", static_cast<int>(surfaceTexture.status));
        ConfigureSurface();
        m_surfaceTexture = nullptr;
        m_surfaceView = nullptr;
        return;
    }
    m_surfaceTexture = surfaceTexture.texture;
    m_surfaceView = m_surfaceTexture.CreateView();

    wgpu::CommandEncoderDescriptor encDesc;
    encDesc.label = "frame";
    m_encoder = m_device.CreateCommandEncoder(&encDesc);
    m_boundFramebuffer = nullptr;
    m_passes.BeginFrame(m_encoder, ScreenTarget());

    // same as CGL33Device::BeginScene
    Clear();
    m_transparency = TransparencyMode::NONE;
    m_depthTest = false;
    m_depthMask = true;
    m_cullFace = CullFace::NONE;
}

void CWebGPUDevice::EndScene()
{
    if (!m_passes.InFrame()) return;
    m_passes.EndFrame();

    m_vertexRing->Flush(m_queue);
    m_uniformRing->Flush(m_queue);

    wgpu::CommandBuffer commands = m_encoder.Finish();
    m_queue.Submit(1, &commands);
    m_encoder = nullptr;
    // No Surface::Present() on the web: the browser presents the canvas after the rAF callback returns.
    m_surfaceView = nullptr;
    m_surfaceTexture = nullptr;
}

void CWebGPUDevice::Clear()
{
    m_passes.Clear(true, true);
}

CUIRenderer* CWebGPUDevice::GetUIRenderer() { return m_uiRenderer.get(); }
CTerrainRenderer* CWebGPUDevice::GetTerrainRenderer() { return m_terrainRenderer.get(); }
CObjectRenderer* CWebGPUDevice::GetObjectRenderer() { return m_objectRenderer.get(); }
CParticleRenderer* CWebGPUDevice::GetParticleRenderer() { return m_particleRenderer.get(); }
CShadowRenderer* CWebGPUDevice::GetShadowRenderer() { return m_shadowRenderer.get(); }

Texture CWebGPUDevice::CreateTexture(CImage* image, const TextureCreateParams& params)
{
    ImageData* data = image->GetData();
    if (data == nullptr)
    {
        GetLogger()->Error("Invalid texture data");
        return Texture();
    }

    glm::ivec2 originalSize = image->GetSize();
    if (params.padToNearestPowerOfTwo)
        image->PadToNearestPowerOfTwo();

    Texture tex = CreateTexture(data, params);
    tex.originalSize = originalSize;
    return tex;
}

Texture CWebGPUDevice::CreateTexture(ImageData* data, const TextureCreateParams& params)
{
    int mipmapLevel = 1;
    int anisotropy = 1;
    if (params.filter == TextureFilter::TRILINEAR)
    {
        mipmapLevel = CEngine::GetInstance().GetTextureMipmapLevel();
        anisotropy = std::min(m_capabilities.maxAnisotropy, CEngine::GetInstance().GetTextureAnisotropyLevel());
    }
    return m_textures.Create(data, params, mipmapLevel, anisotropy);
}

Texture CWebGPUDevice::CreateDepthTexture(int width, int height, int /*depth*/)
{
    // Always Depth32Float (+ comparison sampler, LessEqual) regardless of requested bits
    return m_textures.CreateDepth(width, height);
}

void CWebGPUDevice::UpdateTexture(const Texture& texture, const glm::ivec2& offset, ImageData* data, TextureFormat format)
{
    m_textures.Update(texture, offset, data, format);
}

void CWebGPUDevice::DestroyTexture(const Texture& texture)
{
    m_textures.Destroy(texture.id);
}

void CWebGPUDevice::DestroyAllTextures()
{
    m_textures.DestroyAll();
}

CVertexBuffer* CWebGPUDevice::CreateVertexBuffer(PrimitiveType primitiveType, const Vertex3D* vertices, int vertexCount)
{
    auto buffer = new CWebGPUVertexBuffer(m_device, primitiveType, vertexCount);
    buffer->SetData(vertices, 0, vertexCount);
    buffer->Update();
    m_buffers.insert(buffer);
    return buffer;
}

void CWebGPUDevice::DestroyVertexBuffer(CVertexBuffer* buffer)
{
    if (m_buffers.erase(buffer) == 0) return;
    delete buffer;
}

void CWebGPUDevice::SetViewport(int x, int y, int width, int height)
{
    m_passes.SetViewport(x, y, width, height);
}

void CWebGPUDevice::SetDepthTest(bool enabled) { m_depthTest = enabled; }
void CWebGPUDevice::SetDepthMask(bool enabled) { m_depthMask = enabled; }
void CWebGPUDevice::SetCullFace(CullFace mode) { m_cullFace = mode; }
void CWebGPUDevice::SetTransparency(TransparencyMode mode) { m_transparency = mode; }

void CWebGPUDevice::SetColorMask(bool red, bool green, bool blue, bool alpha)
{
    m_colorMask[0] = red;
    m_colorMask[1] = green;
    m_colorMask[2] = blue;
    m_colorMask[3] = alpha;
}

void CWebGPUDevice::SetClearColor(const Color& color)
{
    m_passes.SetClearColor({ color.r, color.g, color.b, color.a });
}

void CWebGPUDevice::CopyFramebufferToTexture(Texture& texture, int xOffset, int yOffset, int x, int y, int width, int height)
{
    const WebGPUTextureEntry* entry = m_textures.Find(texture.id);
    if (entry == nullptr || entry->depth || entry->format != m_surfaceFormat) return;
    m_passes.CopyColorToTexture(entry->texture, entry->height, xOffset, yOffset, x, y, width, height);
}

std::unique_ptr<CFrameBufferPixels> CWebGPUDevice::GetFrameBufferPixels() const
{
    // TODO(WP12): async readback (CopyTextureToBuffer in EndScene + MapAsync/WaitAny under JSPI)
    GetLogger()->Warn("WebGPU: GetFrameBufferPixels is a stub (black image)");
    return std::make_unique<CWebGPUFrameBufferPixels>(static_cast<size_t>(m_config.size.x) * m_config.size.y * 4);
}

CFramebuffer* CWebGPUDevice::GetFramebuffer(std::string name)
{
    auto it = m_framebuffers.find(name);
    return it == m_framebuffers.end() ? nullptr : it->second.get();
}

CFramebuffer* CWebGPUDevice::CreateFramebuffer(std::string name, const FramebufferParams& params)
{
    if (m_framebuffers.find(name) != m_framebuffers.end()) return nullptr;

    FramebufferParams p = params;
    if (name == "shadow")
    {
        // Depth-only Depth32Float, sampled through the comparison sampler
        p.colorAttachment = FramebufferParams::AttachmentType::None;
        p.depthAttachment = FramebufferParams::AttachmentType::Texture;
        p.samples = 1;
    }

    auto framebuffer = std::make_unique<CWebGPUFramebuffer>(this, p, false);
    if (!framebuffer->Create()) return nullptr;
    CFramebuffer* ptr = framebuffer.get();
    m_framebuffers[name] = std::move(framebuffer);
    return ptr;
}

void CWebGPUDevice::DeleteFramebuffer(std::string name)
{
    if (name == "default") return;
    auto it = m_framebuffers.find(name);
    if (it == m_framebuffers.end()) return;
    if (m_boundFramebuffer == it->second.get()) BindFramebuffer(nullptr);
    m_framebuffers.erase(it);
}

void CWebGPUDevice::BindFramebuffer(CWebGPUFramebuffer* framebuffer)
{
    if (framebuffer != nullptr && framebuffer->IsDefault()) framebuffer = nullptr;
    m_boundFramebuffer = framebuffer;
    if (!m_passes.InFrame()) return;
    m_passes.SetTarget(framebuffer != nullptr ? framebuffer->GetTarget() : ScreenTarget());
}

PipelineKey CWebGPUDevice::MakePipelineKey(RendererId renderer, wgpu::PrimitiveTopology topology) const
{
    const RenderTarget& target = m_passes.GetTarget();
    PipelineKey key;
    key.renderer = static_cast<uint32_t>(renderer);
    key.topology = static_cast<uint32_t>(topology);
    key.transparency = static_cast<uint32_t>(m_transparency);
    key.cull = static_cast<uint32_t>(TranslateCull(m_cullFace));
    key.colorWriteMask = static_cast<uint32_t>(TranslateColorMask(m_colorMask[0], m_colorMask[1], m_colorMask[2], m_colorMask[3]));
    key.colorFormat = static_cast<uint32_t>(target.colorFormat);
    key.depthFormat = static_cast<uint32_t>(target.depthFormat);
    key.sampleCount = target.samples;
    key.depthTest = m_depthTest ? 1 : 0;
    key.depthWrite = m_depthMask ? 1 : 0;
    return key;
}

void CWebGPUDevice::FillFixedFunction(const PipelineKey& key, wgpu::RenderPipelineDescriptor& desc, FixedFunction& s) const
{
    desc.primitive.topology = static_cast<wgpu::PrimitiveTopology>(key.topology);
    desc.primitive.frontFace = wgpu::FrontFace::CW;   // Colobot issue: faces are reversed (gl33 glFrontFace(GL_CW))
    desc.primitive.cullMode = static_cast<wgpu::CullMode>(key.cull);

    desc.multisample.count = key.sampleCount;

    if (key.depthFormat != 0)
    {
        s.depthStencil.format = static_cast<wgpu::TextureFormat>(key.depthFormat);
        // GL: depth writes are disabled when the depth test is disabled
        s.depthStencil.depthWriteEnabled = (key.depthTest && key.depthWrite) ? wgpu::OptionalBool::True : wgpu::OptionalBool::False;
        s.depthStencil.depthCompare = key.depthTest ? wgpu::CompareFunction::LessEqual : wgpu::CompareFunction::Always;
        s.depthStencil.depthBias = key.depthBias;
        s.depthStencil.depthBiasSlopeScale = key.depthBiasSlopeScale;
        desc.depthStencil = &s.depthStencil;
    }
    else
    {
        desc.depthStencil = nullptr;
    }

    if (key.colorFormat != 0)
    {
        s.colorTarget.format = static_cast<wgpu::TextureFormat>(key.colorFormat);
        s.colorTarget.writeMask = static_cast<wgpu::ColorWriteMask>(key.colorWriteMask);
        s.colorTarget.blend = TranslateBlend(static_cast<TransparencyMode>(key.transparency), s.blend) ? &s.blend : nullptr;
        s.fragment.targetCount = 1;
        s.fragment.targets = &s.colorTarget;
    }
    else
    {
        s.fragment.targetCount = 0;
        s.fragment.targets = nullptr;
    }
}

bool CWebGPUDevice::IsAnisotropySupported() { return m_capabilities.anisotropySupported; }
int CWebGPUDevice::GetMaxAnisotropyLevel() { return m_capabilities.maxAnisotropy; }
int CWebGPUDevice::GetMaxSamples() { return m_capabilities.maxSamples; }
bool CWebGPUDevice::IsShadowMappingSupported() { return true; }
int CWebGPUDevice::GetMaxTextureSize() { return m_capabilities.maxTextureSize; }
bool CWebGPUDevice::IsFramebufferSupported() { return true; }

} // namespace Gfx
