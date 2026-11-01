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
 * \file graphics/webgpu/wgpu_pipeline_cache.h
 * \brief Render pipeline cache keyed on fixed-function state (colobot-web WP4)
 */

#pragma once

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <functional>
#include <unordered_map>

namespace Gfx
{

/**
 * \struct PipelineKey
 * \brief Every piece of state that WebGPU bakes into a pipeline. POD, zero-initialized, hashed bytewise.
 *
 * Fill it with CWebGPUDevice::MakePipelineKey() (takes current device state + current pass target)
 * and override renderer-specific fields.
 */
struct PipelineKey
{
    uint32_t renderer = 0;          //!< renderer id (see RendererId in wgpu_device.h)
    uint32_t variant = 0;           //!< renderer-defined shader/layout variant
    uint32_t topology = 0;          //!< wgpu::PrimitiveTopology
    uint32_t transparency = 0;      //!< Gfx::TransparencyMode
    uint32_t cull = 0;              //!< wgpu::CullMode
    uint32_t colorWriteMask = 0xF;  //!< wgpu::ColorWriteMask
    uint32_t colorFormat = 0;       //!< wgpu::TextureFormat (0 = no color target)
    uint32_t depthFormat = 0;       //!< wgpu::TextureFormat (0 = no depth)
    uint32_t sampleCount = 1;
    uint32_t depthTest = 0;
    uint32_t depthWrite = 0;
    int32_t depthBias = 0;
    float depthBiasSlopeScale = 0.0f;

    bool operator==(const PipelineKey& o) const;
};

struct PipelineKeyHash
{
    size_t operator()(const PipelineKey& key) const;
};

class CWebGPUPipelineCache
{
public:
    using Builder = std::function<wgpu::RenderPipeline(const PipelineKey&)>;

    //! Returns the cached pipeline or creates it with builder
    const wgpu::RenderPipeline& Get(const PipelineKey& key, const Builder& builder);
    void Clear() { m_pipelines.clear(); }
    size_t Size() const { return m_pipelines.size(); }

private:
    std::unordered_map<PipelineKey, wgpu::RenderPipeline, PipelineKeyHash> m_pipelines;
};

} // namespace Gfx
