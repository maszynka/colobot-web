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

#include "graphics/webgpu/wgpu_framebuffer.h"

#include "graphics/webgpu/wgpu_device.h"

#include "common/logger.h"

namespace Gfx
{

CWebGPUFramebuffer::CWebGPUFramebuffer(CWebGPUDevice* device, const FramebufferParams& params, bool isDefault)
    : m_device(device), m_params(params), m_default(isDefault)
{
    if (m_params.samples < 1) m_params.samples = 1;
    if (m_params.samples > 1) m_params.samples = 4; // WebGPU supports only 1 and 4
}

CWebGPUFramebuffer::~CWebGPUFramebuffer()
{
    Destroy();
}

bool CWebGPUFramebuffer::Create()
{
    if (m_default) return true;

    const wgpu::Device& device = m_device->GetWGPUDevice();
    wgpu::Extent3D size = { static_cast<uint32_t>(m_params.width), static_cast<uint32_t>(m_params.height), 1 };
    const bool msaa = m_params.samples > 1;

    if (m_params.colorAttachment != FramebufferParams::AttachmentType::None)
    {
        wgpu::TextureDescriptor desc;
        desc.label = "fb-color";
        desc.size = size;
        desc.format = m_device->SurfaceFormat();
        desc.sampleCount = static_cast<uint32_t>(m_params.samples);
        desc.usage = wgpu::TextureUsage::RenderAttachment;
        if (!msaa) desc.usage = desc.usage | wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopySrc;
        m_color = device.CreateTexture(&desc);
        m_colorView = m_color.CreateView();

        if (!msaa && m_params.colorAttachment == FramebufferParams::AttachmentType::Texture)
        {
            WebGPUTextureEntry entry;
            entry.texture = m_color;
            entry.view = m_colorView;
            entry.format = desc.format;
            entry.width = m_params.width;
            entry.height = m_params.height;
            entry.sampler = m_device->Textures().GetSampler(TextureFilter::BILINEAR, TextureWrapMode::CLAMP, false, 1);
            m_colorTextureId = m_device->Textures().Register(entry);
        }
    }

    if (m_params.depthAttachment != FramebufferParams::AttachmentType::None)
    {
        wgpu::TextureDescriptor desc;
        desc.label = "fb-depth";
        desc.size = size;
        desc.format = CWebGPUDevice::DepthFormat;
        desc.sampleCount = static_cast<uint32_t>(m_params.samples);
        desc.usage = wgpu::TextureUsage::RenderAttachment;
        if (!msaa) desc.usage = desc.usage | wgpu::TextureUsage::TextureBinding;
        m_depth = device.CreateTexture(&desc);
        m_depthView = m_depth.CreateView();

        if (!msaa && m_params.depthAttachment == FramebufferParams::AttachmentType::Texture)
        {
            WebGPUTextureEntry entry;
            entry.texture = m_depth;
            entry.view = m_depthView;
            entry.format = desc.format;
            entry.depth = true;
            entry.width = m_params.width;
            entry.height = m_params.height;
            entry.sampler = m_device->Textures().GetComparisonSampler();
            m_depthTextureId = m_device->Textures().Register(entry);
        }
    }

    return m_color || m_depth;
}

void CWebGPUFramebuffer::Destroy()
{
    if (m_colorTextureId != 0) m_device->Textures().Destroy(m_colorTextureId);
    if (m_depthTextureId != 0) m_device->Textures().Destroy(m_depthTextureId);
    m_colorTextureId = m_depthTextureId = 0;
    if (m_color) m_color.Destroy();
    if (m_depth) m_depth.Destroy();
    m_color = nullptr;
    m_depth = nullptr;
    m_colorView = nullptr;
    m_depthView = nullptr;
}

void CWebGPUFramebuffer::Bind()
{
    m_device->BindFramebuffer(this);
}

void CWebGPUFramebuffer::Unbind()
{
    m_device->BindFramebuffer(nullptr);
}

void CWebGPUFramebuffer::Resize(int width, int height)
{
    m_params.width = width;
    m_params.height = height;
}

RenderTarget CWebGPUFramebuffer::GetTarget() const
{
    if (m_default) return m_device->ScreenTarget();

    RenderTarget t;
    t.identity = this;
    t.colorTexture = m_params.samples > 1 ? wgpu::Texture() : m_color;
    t.color = m_colorView;
    t.depthTexture = m_depth;
    t.depth = m_depthView;
    t.colorFormat = m_color ? m_device->SurfaceFormat() : wgpu::TextureFormat::Undefined;
    t.depthFormat = m_depth ? CWebGPUDevice::DepthFormat : wgpu::TextureFormat::Undefined;
    t.samples = static_cast<uint32_t>(m_params.samples);
    t.width = m_params.width;
    t.height = m_params.height;
    return t;
}

void CWebGPUFramebuffer::CopyToScreen(int fromX, int fromY, int fromWidth, int fromHeight, int toX, int toY, int toWidth, int toHeight)
{
    if (m_default || !m_color) return;
    CWebGPUPassManager& passes = m_device->Passes();
    if (!passes.InFrame()) return;

    RenderTarget screen = m_device->ScreenTarget();
    bool fullCopy = fromX == 0 && fromY == 0 && toX == 0 && toY == 0
        && fromWidth == m_params.width && fromHeight == m_params.height
        && toWidth == fromWidth && toHeight == fromHeight
        && screen.width == m_params.width && screen.height == m_params.height;
    if (!fullCopy)
    {
        // TODO(WP12): scaled / partial blits need a fullscreen-triangle pass
        GetLogger()->Warn("WebGPU: CopyToScreen supports only full-size 1:1 copies");
        return;
    }

    passes.EndPass();
    if (m_params.samples > 1)
    {
        // MSAA resolve: empty pass that loads the MSAA image and resolves into the swapchain
        wgpu::RenderPassColorAttachment color;
        color.view = m_colorView;
        color.resolveTarget = screen.color;
        color.loadOp = wgpu::LoadOp::Load;
        color.storeOp = wgpu::StoreOp::Store;
        wgpu::RenderPassDescriptor desc;
        desc.colorAttachmentCount = 1;
        desc.colorAttachments = &color;
        wgpu::RenderPassEncoder pass = passes.Encoder().BeginRenderPass(&desc);
        pass.End();
    }
    else
    {
        wgpu::TexelCopyTextureInfo src;
        src.texture = m_color;
        wgpu::TexelCopyTextureInfo dst;
        dst.texture = screen.colorTexture;
        wgpu::Extent3D size = { static_cast<uint32_t>(fromWidth), static_cast<uint32_t>(fromHeight), 1 };
        passes.Encoder().CopyTextureToTexture(&src, &dst, &size);
    }
}

} // namespace Gfx
