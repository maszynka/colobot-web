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

#include "graphics/core/device_factory.h"

#ifdef __EMSCRIPTEN__

#include "graphics/null/null_device.h"

namespace Gfx
{

// TODO(WP4): return CWebGPUDevice for "default"/"webgpu" once graphics/webgpu exists.
std::unique_ptr<CDevice> CreateDevice(const DeviceConfig &config, const std::string& name)
{
    return std::make_unique<CNullDevice>(config);
}

} // namespace Gfx

#endif // __EMSCRIPTEN__ (native: implemented in graphics/opengl33/glutil.cpp)
