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

#include "graphics/webgpu/wgpu_pass_manager.h"

#include <algorithm>

namespace Gfx
{

void CWebGPUPassManager::BeginFrame(wgpu::CommandEncoder encoder, const RenderTarget& screen)
{
    m_encoder = std::move(encoder);
    m_passOpen = false;
    m_pendingClearColor = m_pendingClearDepth = false;
    m_target = RenderTarget{};
    SetTarget(screen);
}

void CWebGPUPassManager::EndFrame()
{
    EndPass();
    m_encoder = nullptr;
    m_target = RenderTarget{};
}

void CWebGPUPassManager::SetTarget(const RenderTarget& target)
{
    if (target.identity == m_target.identity && target.color.Get() == m_target.color.Get()
        && target.depth.Get() == m_target.depth.Get())
        return;
    EndPass();
    m_target = target;
    m_vpX = 0; m_vpY = 0; m_vpW = target.width; m_vpH = target.height;
    m_vpSet = false;
}

void CWebGPUPassManager::Clear(bool color, bool depth)
{
    if (m_passOpen)
    {
        m_pass.End();
        m_pass = nullptr;
        m_passOpen = false;
    }
    m_pendingClearColor = m_pendingClearColor || color;
    m_pendingClearDepth = m_pendingClearDepth || depth;
}

void CWebGPUPassManager::SetViewport(int x, int y, int width, int height)
{
    m_vpX = x; m_vpY = y; m_vpW = width; m_vpH = height;
    m_vpSet = true;
    if (m_passOpen) ApplyViewport();
}

void CWebGPUPassManager::ApplyViewport()
{
    int tw = std::max(1, m_target.width), th = std::max(1, m_target.height);
    // GL viewport origin is bottom-left; clamp to target (WebGPU validation requires it)
    float x = static_cast<float>(std::clamp(m_vpX, 0, tw));
    float yTop = static_cast<float>(th - (m_vpY + m_vpH));
    float w = static_cast<float>(m_vpW);
    float h = static_cast<float>(m_vpH);
    if (yTop < 0) { h += yTop; yTop = 0; }
    w = std::clamp(w, 0.0f, tw - x);
    h = std::clamp(h, 0.0f, th - yTop);
    m_pass.SetViewport(x, yTop, w, h, 0.0f, 1.0f);
}

wgpu::RenderPassEncoder* CWebGPUPassManager::GetPass()
{
    if (!m_encoder) return nullptr;
    if (m_passOpen) return &m_pass;
    if (!m_target.color && !m_target.depth) return nullptr;

    wgpu::RenderPassColorAttachment colorAttachment;
    wgpu::RenderPassDepthStencilAttachment depthAttachment;
    wgpu::RenderPassDescriptor desc;

    if (m_target.color)
    {
        colorAttachment.view = m_target.color;
        colorAttachment.loadOp = m_pendingClearColor ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
        colorAttachment.storeOp = wgpu::StoreOp::Store;
        colorAttachment.clearValue = m_clearColor;
        desc.colorAttachmentCount = 1;
        desc.colorAttachments = &colorAttachment;
    }
    if (m_target.depth)
    {
        depthAttachment.view = m_target.depth;
        depthAttachment.depthLoadOp = m_pendingClearDepth ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
        depthAttachment.depthStoreOp = wgpu::StoreOp::Store;
        depthAttachment.depthClearValue = 1.0f;
        desc.depthStencilAttachment = &depthAttachment;
    }

    m_pass = m_encoder.BeginRenderPass(&desc);
    m_passOpen = true;
    m_pendingClearColor = m_pendingClearDepth = false;
    if (m_vpSet) ApplyViewport();
    return &m_pass;
}

void CWebGPUPassManager::EndPass()
{
    if (!m_passOpen && (m_pendingClearColor || m_pendingClearDepth) && m_encoder)
        GetPass(); // empty pass applies the deferred clear
    if (m_passOpen)
    {
        m_pass.End();
        m_pass = nullptr;
        m_passOpen = false;
    }
}

void CWebGPUPassManager::CopyColorToTexture(const wgpu::Texture& dst, int dstHeight, int dstX, int dstY,
                                            int srcX, int srcY, int width, int height)
{
    if (!m_encoder || !m_target.colorTexture || m_target.samples > 1) return;
    EndPass();

    // clamp source rect to target
    int srcTop = m_target.height - (srcY + height);
    if (srcX < 0 || srcTop < 0 || srcX + width > m_target.width || width <= 0 || height <= 0) return;
    int dstTop = dstHeight - (dstY + height);
    if (dstTop < 0 || dstX < 0) return;

    wgpu::TexelCopyTextureInfo src;
    src.texture = m_target.colorTexture;
    src.origin = { static_cast<uint32_t>(srcX), static_cast<uint32_t>(srcTop), 0 };
    wgpu::TexelCopyTextureInfo dstInfo;
    dstInfo.texture = dst;
    dstInfo.origin = { static_cast<uint32_t>(dstX), static_cast<uint32_t>(dstTop), 0 };
    wgpu::Extent3D size = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
    m_encoder.CopyTextureToTexture(&src, &dstInfo, &size);
}

} // namespace Gfx
