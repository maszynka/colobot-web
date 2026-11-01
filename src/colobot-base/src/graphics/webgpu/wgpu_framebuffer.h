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
 * \file graphics/webgpu/wgpu_framebuffer.h
 * \brief Offscreen render targets (colobot-web WP4)
 *
 *  - "default": the swapchain (+ device depth buffer)
 *  - colorAttachment None  -> depth-only target (e.g. "shadow": Depth32Float, sampled with comparison sampler)
 *  - samples > 1           -> MSAA color+depth; CopyToScreen() resolves via a resolveTarget pass
 * Color attachments use the surface format so CopyToScreen/CopyTextureToTexture stay format-compatible.
 */

#pragma once

#include "graphics/core/framebuffer.h"
#include "graphics/webgpu/wgpu_pass_manager.h"

#include <webgpu/webgpu_cpp.h>

namespace Gfx
{

class CWebGPUDevice;

class CWebGPUFramebuffer : public CFramebuffer
{
public:
    CWebGPUFramebuffer(CWebGPUDevice* device, const FramebufferParams& params, bool isDefault);
    ~CWebGPUFramebuffer() override;

    bool Create() override;
    void Destroy() override;
    bool IsDefault() override { return m_default; }
    int GetWidth() override { return m_params.width; }
    int GetHeight() override { return m_params.height; }
    int GetDepth() override { return m_params.depth; }
    int GetSamples() override { return m_params.samples; }
    int GetColorTexture() override { return static_cast<int>(m_colorTextureId); }
    int GetDepthTexture() override { return static_cast<int>(m_depthTextureId); }
    void Bind() override;
    void Unbind() override;
    void CopyToScreen(int fromX, int fromY, int fromWidth, int fromHeight, int toX, int toY, int toWidth, int toHeight) override;

    //! Only for the default framebuffer (window resize)
    void Resize(int width, int height);

    RenderTarget GetTarget() const;

private:
    CWebGPUDevice* m_device;
    FramebufferParams m_params;
    bool m_default;
    wgpu::Texture m_color;
    wgpu::TextureView m_colorView;
    wgpu::Texture m_depth;
    wgpu::TextureView m_depthView;
    unsigned int m_colorTextureId = 0;
    unsigned int m_depthTextureId = 0;
};

} // namespace Gfx
