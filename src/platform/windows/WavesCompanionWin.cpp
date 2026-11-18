// Detección y arranque de las aplicaciones de Waves en Windows (solo lectura del registro).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>

#include "../WavesCompanion.h"

#include <string>
#include <vector>

namespace sg {

namespace {

std::wstring widen(const std::string &s)
{
    if (s.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string narrow(const std::wstring &w)
{
    if (w.empty())
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

bool fileExists(const std::wstring &p)
{
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring readString(HKEY root, const wchar_t *path, const wchar_t *value, REGSAM view)
{
    HKEY k = nullptr;
    if (RegOpenKeyExW(root, path, 0, KEY_READ | view, &k) != ERROR_SUCCESS)
        return {};
    wchar_t buf[1024] = {};
    DWORD sz = sizeof(buf) - sizeof(wchar_t);
    DWORD type = 0;
    std::wstring out;
    if (RegQueryValueExW(k, value, nullptr, &type, reinterpret_cast<BYTE *>(buf), &sz) == ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ)) {
        wchar_t exp[1024] = {};
        out = ExpandEnvironmentStringsW(buf, exp, 1024) ? exp : buf;
    }
    RegCloseKey(k);
    return out;
}

// InstallLocation de "Waves SoundGrid Drivers ..." en Programas y características.
std::wstring wavesDriverInstallLocation()
{
    for (REGSAM view : {KEY_WOW64_32KEY, KEY_WOW64_64KEY}) {
        HKEY root = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0,
                          KEY_READ | view, &root) != ERROR_SUCCESS)
            continue;
        std::wstring found;
        for (DWORD i = 0; found.empty(); ++i) {
            wchar_t sub[256];
            DWORD len = 256;
            if (RegEnumKeyExW(root, i, sub, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
                break;
            const std::wstring path = std::wstring(L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\") + sub;
            const std::wstring name = readString(HKEY_LOCAL_MACHINE, path.c_str(), L"DisplayName", view);
            if (name.rfind(L"Waves SoundGrid Drivers", 0) == 0)
                found = readString(HKEY_LOCAL_MACHINE, path.c_str(), L"InstallLocation", view);
        }
        RegCloseKey(root);
        if (!found.empty())
            return found;
    }
    return {};
}

std::wstring dirOf(const std::wstring &p)
{
    const auto pos = p.find_last_of(L"\\/");
    return pos == std::wstring::npos ? std::wstring() : p.substr(0, pos);
}

class WinProcessOps final : public IProcessOps {
public:
    bool isRunning(const std::string &processName) override
    {
        const std::wstring want = widen(processName);
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE)
            return true; // si no se puede observar, no se intenta abrir nada
        PROCESSENTRY32W pe{sizeof(pe)};
        bool found = false;
        for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe))
            found = _wcsicmp(pe.szExeFile, want.c_str()) == 0;
        CloseHandle(snap);
        return found;
    }

    bool launch(const LaunchTarget &t, std::string &error) override
    {
        const std::wstring exe = widen(t.exePath);
        if (!fileExists(exe)) {
            error = "no existe " + t.exePath;
            return false;
        }
        std::wstring cmd = L"\"" + exe + L"\"";
        if (!t.args.empty())
            cmd += L" " + widen(t.args);
        std::vector<wchar_t> buf(cmd.begin(), cmd.end());
        buf.push_back(L'\0');
        STARTUPINFOW si{sizeof(si)};
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_SHOWMINNOACTIVE; // minimizado y sin quitar el foco a OBS
        PROCESS_INFORMATION pi{};
        const std::wstring cwd = dirOf(exe);
        // CreateProcess (no ShellExecute): si el programa exigiera administrador falla con 740 en
        // lugar de mostrar un aviso de UAC en mitad de una transmisión.
        if (!CreateProcessW(exe.c_str(), buf.data(), nullptr, nullptr, FALSE, CREATE_DEFAULT_ERROR_MODE, nullptr,
                            cwd.empty() ? nullptr : cwd.c_str(), &si, &pi)) {
            const DWORD e = GetLastError();
            error = e == ERROR_ELEVATION_REQUIRED ? "requiere permisos de administrador (no se pide UAC)"
                                                   : "error de Windows " + std::to_string(e);
            return false;
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }
};

} // namespace

std::unique_ptr<IProcessOps> createPlatformProcessOps()
{
    return std::make_unique<WinProcessOps>();
}

std::optional<LaunchTarget> wavesControlPanelTarget()
{
    std::wstring dir = wavesDriverInstallLocation();
    if (dir.empty()) {
        wchar_t pf[MAX_PATH] = {};
        if (ExpandEnvironmentStringsW(L"%ProgramFiles(x86)%\\Waves\\SoundGrid\\Driver Control Panel\\", pf, MAX_PATH))
            dir = pf;
    }
    if (!dir.empty() && dir.back() != L'\\')
        dir += L'\\';
    const std::wstring exe = dir + L"SoundGrid Driver Control Panel.exe";
    if (!fileExists(exe))
        return std::nullopt;
    return LaunchTarget{"SoundGrid Driver Control Panel", "SoundGrid Driver Control Panel.exe", narrow(exe), ""};
}

namespace {
struct FindWnd {
    DWORD pid;
    HWND hwnd;
};
BOOL CALLBACK findTopWindow(HWND h, LPARAM lp)
{
    auto *f = reinterpret_cast<FindWnd *>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == f->pid && GetWindow(h, GW_OWNER) == nullptr && GetWindowTextLengthW(h) > 0) {
        f->hwnd = h;
        return FALSE;
    }
    return TRUE;
}
} // namespace

bool showOrLaunch(IProcessOps &ops, const LaunchTarget &t, std::string &error)
{
    if (ops.isRunning(t.processName)) {
        const std::wstring want = widen(t.processName);
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        DWORD pid = 0;
        if (snap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe{sizeof(pe)};
            for (BOOL ok = Process32FirstW(snap, &pe); ok && !pid; ok = Process32NextW(snap, &pe))
                if (_wcsicmp(pe.szExeFile, want.c_str()) == 0)
                    pid = pe.th32ProcessID;
            CloseHandle(snap);
        }
        FindWnd f{pid, nullptr};
        if (pid)
            EnumWindows(findTopWindow, reinterpret_cast<LPARAM>(&f));
        if (f.hwnd) {
            ShowWindow(f.hwnd, IsIconic(f.hwnd) ? SW_RESTORE : SW_SHOW);
            SetForegroundWindow(f.hwnd);
        }
        return true; // ya estaba abierto (puede vivir solo en la bandeja del sistema)
    }
    const std::wstring exe = widen(t.exePath);
    std::wstring cmd = L"\"" + exe + L"\"";
    if (!t.args.empty())
        cmd += L" " + widen(t.args);
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    const std::wstring cwd = dirOf(exe);
    if (!CreateProcessW(exe.c_str(), buf.data(), nullptr, nullptr, FALSE, CREATE_DEFAULT_ERROR_MODE, nullptr,
                        cwd.empty() ? nullptr : cwd.c_str(), &si, &pi)) {
        const DWORD e = GetLastError();
        error = e == ERROR_ELEVATION_REQUIRED ? "requiere permisos de administrador" : "error de Windows " + std::to_string(e);
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

std::optional<LaunchTarget> wavesNodeServiceTarget()
{
    // El mismo comando que Waves registra para arrancarlo al iniciar sesión.
    std::wstring cmd =
        readString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", L"SGDawNodeService", KEY_WOW64_64KEY);
    if (cmd.empty())
        cmd = readString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", L"SGDawNodeService",
                         KEY_WOW64_32KEY);
    std::wstring exe, args;
    if (!cmd.empty()) {
        if (cmd[0] == L'"') {
            const auto end = cmd.find(L'"', 1);
            exe = cmd.substr(1, end == std::wstring::npos ? std::wstring::npos : end - 1);
            if (end != std::wstring::npos && end + 1 < cmd.size())
                args = cmd.substr(end + 1);
        } else {
            std::wstring lower = cmd;
            for (auto &c : lower)
                c = static_cast<wchar_t>(towlower(c));
            const auto pos = lower.find(L".exe");
            exe = pos == std::wstring::npos ? cmd : cmd.substr(0, pos + 4);
            if (pos != std::wstring::npos && pos + 4 < cmd.size())
                args = cmd.substr(pos + 4);
        }
        while (!args.empty() && args.front() == L' ')
            args.erase(args.begin());
    }
    if (exe.empty() || !fileExists(exe))
        return std::nullopt;
    return LaunchTarget{"Servicio SoundGrid (SGDawNodeService)", "SGDawNodeService.exe", narrow(exe), narrow(args)};
}

} // namespace sg
