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
 * \file graphics/webgpu/wgpu_pass_manager.h
 * \brief Emulates GL's "bound framebuffer + glClear + glViewport" on top of WebGPU render passes (colobot-web WP4)
 *
 * Rules:
 *  - A render pass is opened lazily by GetPass() on the first draw after a target change / clear.
 *  - Clear() is deferred: it closes the open pass and makes the next pass use LoadOp::Clear.
 *    If nothing is drawn before the target changes or the frame ends, an empty pass applies the clear.
 *  - Target changes (SetTarget) close the open pass.
 *  - Copies (CopyColorToTexture) close the open pass and record on the frame encoder.
 *  - Viewports use GL convention (origin bottom-left) and are converted.
 */

#pragma once

#include <webgpu/webgpu_cpp.h>

namespace Gfx
{

struct RenderTarget
{
    const void* identity = nullptr;       //!< used to detect target changes
    wgpu::Texture colorTexture;           //!< null for depth-only targets
    wgpu::TextureView color;
    wgpu::Texture depthTexture;           //!< null if no depth
    wgpu::TextureView depth;
    wgpu::TextureFormat colorFormat = wgpu::TextureFormat::Undefined;
    wgpu::TextureFormat depthFormat = wgpu::TextureFormat::Undefined;
    uint32_t samples = 1;
    int width = 0;
    int height = 0;
};

class CWebGPUPassManager
{
public:
    //! Starts recording a frame into encoder; target = screen
    void BeginFrame(wgpu::CommandEncoder encoder, const RenderTarget& screen);
    //! Ends the open pass, applies pending clears; encoder can then be finished
    void EndFrame();
    bool InFrame() const { return m_encoder != nullptr; }

    void SetTarget(const RenderTarget& target);
    const RenderTarget& GetTarget() const { return m_target; }

    void SetClearColor(const wgpu::Color& color) { m_clearColor = color; }
    void Clear(bool color = true, bool depth = true);

    //! GL-style viewport (origin bottom-left)
    void SetViewport(int x, int y, int width, int height);

    //! Open (or reuse) the render pass for the current target; null outside a frame
    wgpu::RenderPassEncoder* GetPass();
    //! Close the open pass (applying pending clears)
    void EndPass();

    wgpu::CommandEncoder& Encoder() { return m_encoder; }

    /**
     * glCopyTexSubImage2D equivalent: copies a rectangle of the current color target into dst.
     * Coordinates are GL-style (bottom-left origin); converted to WebGPU's top-left origin.
     * Note: content lands in WebGPU row order (top row first) - shaders sampling it with
     * GL UVs must use flip_rt_uv().
     */
    void CopyColorToTexture(const wgpu::Texture& dst, int dstHeight, int dstX, int dstY, int srcX, int srcY, int width, int height);

private:
    void ApplyViewport();

    wgpu::CommandEncoder m_encoder;
    wgpu::RenderPassEncoder m_pass;
    bool m_passOpen = false;
    RenderTarget m_target;
    bool m_pendingClearColor = false;
    bool m_pendingClearDepth = false;
    wgpu::Color m_clearColor = { 0.0, 0.0, 0.0, 1.0 };
    int m_vpX = 0, m_vpY = 0, m_vpW = 0, m_vpH = 0;
    bool m_vpSet = false;
};

} // namespace Gfx
