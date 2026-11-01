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
 * \file graphics/core/device_factory.h
 * \brief Backend-agnostic graphics device factory (colobot-web WP0)
 *
 * app.cpp includes only this header. Each backend (opengl33, null, webgpu)
 * provides the implementation of CreateDevice for its build.
 */

#pragma once

#include <memory>
#include <string>

namespace Gfx
{

class CDevice;
struct DeviceConfig;

//! Description of the platform window / canvas the device renders into
struct PlatformWindow
{
    //! CSS selector of the target canvas (web only), e.g. "#canvas"
    const char* canvasSelector = "#canvas";
    //! Drawable size in pixels
    int width = 0;
    int height = 0;
};

//! Creates a graphics device of the given name ("default", "opengl", "gl33", "auto", "null", "webgpu")
std::unique_ptr<CDevice> CreateDevice(const DeviceConfig &config, const std::string& name);

} // namespace Gfx
