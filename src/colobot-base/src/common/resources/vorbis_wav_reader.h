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

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

/**
 * \class CVorbisWavReader
 * \brief Decodes Ogg Vorbis (stb_vorbis) and PCM WAV from PhysFS into 16-bit interleaved samples.
 *
 * Replaces CSNDFileWrapper (libsndfile) on the web build. The whole file is decoded on open.
 */
class CVorbisWavReader final
{
public:
    struct Info
    {
        int64_t frames = 0;
        int samplerate = 0;
        int channels = 0;
        int format = 0;
        int sections = 1;
    };

    explicit CVorbisWavReader(const std::filesystem::path& filename);
    ~CVorbisWavReader() = default;

    CVorbisWavReader(const CVorbisWavReader&) = delete;
    CVorbisWavReader& operator=(const CVorbisWavReader&) = delete;

    const Info& GetFileInfo() const { return m_info; }
    bool IsOpen() const { return m_open; }
    const std::string& GetLastError() const { return m_lastError; }

    /// Reads up to \a items 16-bit samples; returns number read (0 at end)
    int64_t Read(short* ptr, int64_t items);

private:
    bool DecodeOgg(const std::vector<uint8_t>& data);
    bool DecodeWav(const std::vector<uint8_t>& data);

    Info m_info;
    bool m_open = false;
    std::string m_lastError;
    std::vector<int16_t> m_samples;
    size_t m_pos = 0;
};
