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

#include "common/resources/vorbis_wav_reader.h"

#include "common/stringutils.h"
#include "common/resources/physfs_utils.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include <physfs.h>

// stb_vorbis.c is compiled as a separate C translation unit (third_party/stb_vorbis)
extern "C" int stb_vorbis_decode_memory(const unsigned char* mem, int len, int* channels, int* sample_rate, short** output);

namespace
{

uint16_t RdU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t RdU32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }

} // namespace

CVorbisWavReader::CVorbisWavReader(const std::filesystem::path& filename)
{
    if (!PHYSFS_isInit())
    {
        m_lastError = "Resource system not started!";
        return;
    }
    PHYSFS_File* file = LoudOpenRead(StrUtils::ToString(filename).c_str());
    if (!file)
    {
        m_lastError = "Error in PHYSFS_openRead()";
        return;
    }
    std::vector<uint8_t> data;
    PHYSFS_sint64 len = PHYSFS_fileLength(file);
    if (len > 0)
    {
        data.resize(static_cast<size_t>(len));
        PHYSFS_sint64 got = PHYSFS_readBytes(file, data.data(), data.size());
        data.resize(got > 0 ? static_cast<size_t>(got) : 0);
    }
    PHYSFS_close(file);

    if (data.size() >= 12 && std::memcmp(data.data(), "RIFF", 4) == 0 && std::memcmp(data.data() + 8, "WAVE", 4) == 0)
        m_open = DecodeWav(data);
    else if (data.size() >= 4 && std::memcmp(data.data(), "OggS", 4) == 0)
        m_open = DecodeOgg(data);
    else
        m_lastError = "Unknown audio format";

    if (m_open)
    {
        m_info.frames = m_info.channels > 0 ? static_cast<int64_t>(m_samples.size()) / m_info.channels : 0;
    }
}

bool CVorbisWavReader::DecodeOgg(const std::vector<uint8_t>& data)
{
    int channels = 0, rate = 0;
    short* out = nullptr;
    int n = stb_vorbis_decode_memory(data.data(), static_cast<int>(data.size()), &channels, &rate, &out);
    if (n <= 0 || !out)
    {
        m_lastError = "Could not decode Ogg Vorbis";
        std::free(out);
        return false;
    }
    m_samples.assign(out, out + static_cast<size_t>(n) * channels);
    std::free(out);
    m_info.channels = channels;
    m_info.samplerate = rate;
    return true;
}

bool CVorbisWavReader::DecodeWav(const std::vector<uint8_t>& data)
{
    size_t pos = 12;
    uint16_t fmtTag = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    bool haveFmt = false;
    while (pos + 8 <= data.size())
    {
        const uint8_t* ck = data.data() + pos;
        size_t size = RdU32(ck + 4);
        size_t body = pos + 8;
        if (body + size > data.size()) size = data.size() - body; // tolerate truncated chunk
        if (std::memcmp(ck, "fmt ", 4) == 0 && size >= 16)
        {
            fmtTag = RdU16(data.data() + body);
            channels = RdU16(data.data() + body + 2);
            rate = RdU32(data.data() + body + 4);
            bits = RdU16(data.data() + body + 14);
            if (fmtTag == 0xFFFE && size >= 26) fmtTag = RdU16(data.data() + body + 24); // EXTENSIBLE
            haveFmt = true;
        }
        else if (std::memcmp(ck, "data", 4) == 0)
        {
            if (!haveFmt || channels == 0 || channels > 2)
            {
                m_lastError = "Bad WAV header";
                return false;
            }
            const uint8_t* p = data.data() + body;
            bool pcm = fmtTag == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32);
            bool flt = fmtTag == 3 && bits == 32;
            if (!pcm && !flt)
            {
                m_lastError = "Unsupported WAV encoding";
                return false;
            }
            size_t bytesPer = bits / 8;
            size_t count = size / bytesPer;
            count -= count % channels;
            m_samples.resize(count);
            for (size_t i = 0; i < count; i++, p += bytesPer)
            {
                int v;
                if (flt)
                {
                    float f;
                    std::memcpy(&f, p, 4);
                    f = std::clamp(f, -1.0f, 1.0f);
                    v = static_cast<int>(f * 32767.0f);
                }
                else if (bits == 8)  v = (static_cast<int>(p[0]) - 128) << 8;
                else if (bits == 16) v = static_cast<int16_t>(RdU16(p));
                else if (bits == 24) v = static_cast<int16_t>(RdU16(p + 1));
                else                 v = static_cast<int16_t>(RdU16(p + 2));
                m_samples[i] = static_cast<int16_t>(v);
            }
            m_info.channels = channels;
            m_info.samplerate = static_cast<int>(rate);
            return true;
        }
        pos = body + size + (size & 1);
    }
    m_lastError = "WAV has no data chunk";
    return false;
}

int64_t CVorbisWavReader::Read(short* ptr, int64_t items)
{
    size_t n = std::min<size_t>(static_cast<size_t>(std::max<int64_t>(items, 0)), m_samples.size() - m_pos);
    std::memcpy(ptr, m_samples.data() + m_pos, n * sizeof(int16_t));
    m_pos += n;
    return static_cast<int64_t>(n);
}
