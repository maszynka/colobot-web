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

#include "common/system/system_emscripten.h"

#include <emscripten.h>

#include <cstdlib>
#include <iostream>

// confirmMode 0: alert (always returns 1); 1: confirm (1 = OK/Yes, 0 = Cancel/No)
EM_JS(int, colobot_js_dialog, (int confirmMode, const char* title, const char* message), {
    const text = UTF8ToString(title) + "\n\n" + UTF8ToString(message);
    if (typeof window === 'undefined') { console.log(text); return 1; }
    if (confirmMode) return window.confirm(text) ? 1 : 0;
    window.alert(text);
    return 1;
});

EM_JS(char*, colobot_js_language, (), {
    const lang = (typeof navigator !== 'undefined' && navigator.language) ? navigator.language : "en";
    return stringToNewUTF8(lang);
});

std::unique_ptr<CSystemUtils> CSystemUtils::Create()
{
    return std::make_unique<CSystemUtilsEmscripten>();
}

void CSystemUtilsEmscripten::Init(const std::vector<std::string>& args)
{
    m_arguments = args;
    m_basePath = "/";
}

SystemDialogResult CSystemUtilsEmscripten::SystemDialog(SystemDialogType type, const std::string& title, const std::string& message)
{
    std::cerr << "[" << title << "] " << message << std::endl; // always mirror to the console
    const bool question = (type == SystemDialogType::YES_NO || type == SystemDialogType::OK_CANCEL);
    const int ok = colobot_js_dialog(question ? 1 : 0, title.c_str(), message.c_str());
    switch (type)
    {
    case SystemDialogType::YES_NO:    return ok ? SystemDialogResult::YES : SystemDialogResult::NO;
    case SystemDialogType::OK_CANCEL: return ok ? SystemDialogResult::OK : SystemDialogResult::CANCEL;
    default:                          return SystemDialogResult::OK;
    }
}

std::filesystem::path CSystemUtilsEmscripten::GetBasePath() const { return "/"; }
std::filesystem::path CSystemUtilsEmscripten::GetDataPath() const { return "/data"; }
std::filesystem::path CSystemUtilsEmscripten::GetLangPath() const { return "/lang"; }
std::filesystem::path CSystemUtilsEmscripten::GetSaveDir() const { return "/save"; }

double CSystemUtilsEmscripten::GetNowMs()
{
    return emscripten_get_now();
}

std::string CSystemUtilsEmscripten::GetBrowserLanguage()
{
    char* s = colobot_js_language();
    std::string result = s ? s : "en";
    std::free(s);
    return result;
}

// Backs third_party/stubs/include/localename.h: "pl-PL" -> "pl_PL" (static buffer)
extern "C" const char* colobot_web_locale_name(void)
{
    static std::string name;
    name = CSystemUtilsEmscripten::GetBrowserLanguage();
    for (char& c : name)
        if (c == '-') c = '_';
    return name.c_str();
}
