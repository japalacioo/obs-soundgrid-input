#include "WavesDriverPrefs.h"

#include <cctype>
#include <fstream>
#include <sstream>

#if defined(_WIN32)
#include <cstdlib>
#include <filesystem>
#endif

namespace sg {

std::string normalizeMac(const std::string &mac)
{
    std::string hex;
    for (char c : mac) {
        if (std::isxdigit(static_cast<unsigned char>(c)))
            hex += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        else if (c != ':' && c != '-' && !std::isspace(static_cast<unsigned char>(c)))
            return {};
    }
    if (hex.size() != 12)
        return {};
    std::string out;
    for (std::size_t i = 0; i < 12; i += 2) {
        if (i)
            out += ':';
        out += hex.substr(i, 2);
    }
    return out;
}

std::optional<std::string> parseWavesLocalLanPort(const std::string &xml)
{
    const std::string open = "<LocalLanPort>", close = "</LocalLanPort>";
    const auto a = xml.find(open);
    if (a == std::string::npos)
        return std::nullopt;
    const auto b = xml.find(close, a + open.size());
    if (b == std::string::npos)
        return std::nullopt;
    const std::string mac = normalizeMac(xml.substr(a + open.size(), b - a - open.size()));
    if (mac.empty() || mac == "00:00:00:00:00:00")
        return std::nullopt;
    return mac;
}

std::optional<std::string> wavesDriverPreferredMac()
{
#if defined(_WIN32)
    const wchar_t *appdata = _wgetenv(L"APPDATA");
    if (!appdata)
        return std::nullopt;
    const std::filesystem::path p =
        std::filesystem::path(appdata) / L"Waves Audio" / L"Preferences" / L"SoundGrid.Driver.preferences";
    std::ifstream f(p, std::ios::binary);
    if (!f)
        return std::nullopt;
    std::stringstream ss;
    ss << f.rdbuf();
    return parseWavesLocalLanPort(ss.str());
#else
    return std::nullopt;
#endif
}

} // namespace sg
