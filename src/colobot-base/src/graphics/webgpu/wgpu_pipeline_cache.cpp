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

#include "graphics/webgpu/wgpu_pipeline_cache.h"

#include <cstring>

namespace Gfx
{

static_assert(sizeof(PipelineKey) == 13 * 4, "PipelineKey must stay padding-free (hashed bytewise)");

bool PipelineKey::operator==(const PipelineKey& o) const
{
    return std::memcmp(this, &o, sizeof(PipelineKey)) == 0;
}

size_t PipelineKeyHash::operator()(const PipelineKey& key) const
{
    // FNV-1a over the bytes
    const auto* p = reinterpret_cast<const unsigned char*>(&key);
    size_t h = 2166136261u;
    for (size_t i = 0; i < sizeof(PipelineKey); ++i)
    {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

const wgpu::RenderPipeline& CWebGPUPipelineCache::Get(const PipelineKey& key, const Builder& builder)
{
    auto it = m_pipelines.find(key);
    if (it != m_pipelines.end()) return it->second;
    return m_pipelines.emplace(key, builder(key)).first->second;
}

} // namespace Gfx
