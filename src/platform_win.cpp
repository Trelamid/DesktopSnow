#include "platform.hpp"
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <vector>

namespace {
const UINT WM_SNOW_TRAY = WM_APP + 72;
const wchar_t* kTrayClass = L"DesktopSnowNotificationClass";
HWND g_tray_window = nullptr;
NOTIFYICONDATAW g_icon{};
void (*g_callback)(int) = nullptr;
snow::Settings g_settings;

bool overlaps(snow::Rect a, snow::Rect b) {
    return a.right() > b.x && a.x < b.right() && a.bottom() > b.y && a.y < b.bottom();
}

void menu_item(HMENU menu, UINT code, const wchar_t* title, bool checked = false) {
    AppendMenuW(menu, MF_STRING | (checked ? MF_CHECKED : 0), code, title);
}

void show_menu(HWND window) {
    HMENU menu = CreatePopupMenu();
    menu_item(menu, g_settings.paused ? platform::Resume : platform::Pause,
              g_settings.paused ? L"Resume snow" : L"Pause snow");
    menu_item(menu, platform::Clear, L"Clear all snow");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    HMENU densities = CreatePopupMenu();
    menu_item(densities, platform::Light, L"Light", g_settings.intensity == snow::Intensity::Light);
    menu_item(densities, platform::Normal, L"Normal", g_settings.intensity == snow::Intensity::Normal);
    menu_item(densities, platform::Blizzard, L"Blizzard", g_settings.intensity == snow::Intensity::Blizzard);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(densities), L"Snowfall");
    HMENU winds = CreatePopupMenu();
    menu_item(winds, platform::WindLeft, L"Left", g_settings.wind < -1);
    menu_item(winds, platform::WindCalm, L"Calm", std::abs(g_settings.wind) <= 1);
    menu_item(winds, platform::WindRight, L"Right", g_settings.wind > 1);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(winds), L"Wind");
    HMENU tools = CreatePopupMenu();
    menu_item(tools, platform::Brush, L"Brush (move cursor)", g_settings.tool == snow::Tool::Brush);
    menu_item(tools, platform::Roll, L"Snowballs (hold Shift)", g_settings.tool == snow::Tool::Roll);
    menu_item(tools, platform::ToolOff, L"Off", g_settings.tool == snow::Tool::None);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(tools), L"Cursor tool");
    HMENU fps = CreatePopupMenu();
    menu_item(fps, platform::Fps15, L"Eco (15 fps)", g_settings.target_fps == 15);
    menu_item(fps, platform::Fps30, L"Normal (30 fps)", g_settings.target_fps == 30);
    menu_item(fps, platform::Fps60, L"Smooth (60 fps)", g_settings.target_fps == 60);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(fps), L"Frame rate");
    menu_item(menu, platform::FullscreenToggle, L"Pause in fullscreen", g_settings.pause_fullscreen);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    menu_item(menu, platform::Quit, L"Quit");
    POINT mouse{};
    GetCursorPos(&mouse);
    SetForegroundWindow(window);
    const UINT choice = TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON |
                                      TPM_RETURNCMD | TPM_NONOTIFY, mouse.x, mouse.y, 0, window, nullptr);
    PostMessageW(window, WM_NULL, 0, 0);
    DestroyMenu(menu);
    if (choice && g_callback) g_callback(static_cast<int>(choice));
}

LRESULT CALLBACK tray_wnd_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    (void)wparam;
    if (message == WM_SNOW_TRAY) {
        const UINT event_code = LOWORD(lparam);
        if (event_code == WM_CONTEXTMENU || event_code == WM_RBUTTONUP ||
            event_code == WM_LBUTTONUP) {
            show_menu(hwnd);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

struct EnumContext {
    platform::SceneSnapshot* out = nullptr;
    const std::vector<snow::Rect>* monitors = nullptr;
    DWORD pid = 0;
};

BOOL CALLBACK collect_window(HWND hwnd, LPARAM userdata) {
    auto& state = *reinterpret_cast<EnumContext*>(userdata);
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE;
    DWORD owner_pid = 0;
    GetWindowThreadProcessId(hwnd, &owner_pid);
    if (owner_pid == state.pid) return TRUE;
    LONG_PTR exstyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    wchar_t cls[128]{};
    GetClassNameW(hwnd, cls, 128);
    const bool taskbar = lstrcmpW(cls, L"Shell_TrayWnd") == 0 ||
                         lstrcmpW(cls, L"Shell_SecondaryTrayWnd") == 0;
    if ((exstyle & WS_EX_TOOLWINDOW) != 0 && !taskbar) return TRUE;
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
        return TRUE;
    RECT bounds{};
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds))))
        if (!GetWindowRect(hwnd, &bounds)) return TRUE;
    const snow::Rect rect{static_cast<float>(bounds.left), static_cast<float>(bounds.top),
                          static_cast<float>(bounds.right - bounds.left),
                          static_cast<float>(bounds.bottom - bounds.top)};
    if (rect.w < 60 || rect.h < 20) return TRUE;
    if (lstrcmpW(cls, L"Progman") == 0 || lstrcmpW(cls, L"WorkerW") == 0) return TRUE;
    bool any_intersection = false;
    bool exclude = false;
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const bool framed = (style & WS_CAPTION) != 0;
    for (std::size_t mi = 0; mi < state.monitors->size(); ++mi) {
        const auto& m = (*state.monitors)[mi];
        if (!overlaps(m, rect)) continue;
        any_intersection = true;
        if (GetAncestor(GetForegroundWindow(), GA_ROOT) == hwnd &&
            snow::World::is_borderless_fullscreen(m, rect, framed)) {
            state.out->fullscreen[mi] = true;
            exclude = true;
        }
        if (rect.w >= m.w * 0.985f && rect.h >= m.h * 0.985f &&
            !framed && rect.x <= m.x + 5 && rect.y <= m.y + 5) exclude = true;
    }
    if (!any_intersection || exclude) return TRUE;
    state.out->surfaces.push_back({static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(hwnd)),
                                   rect, taskbar});
    return TRUE;
}
} // namespace

namespace platform {

void prepare_process() {
    // Match GLFW monitor positions, desktop window bounds, and mouse coordinates
    // across different per-monitor DPI settings.
    using SetDpiContext = BOOL (WINAPI*)(DPI_AWARENESS_CONTEXT);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    auto set_context = reinterpret_cast<SetDpiContext>(
        GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (!set_context || !set_context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        SetProcessDPIAware();
}

void configure_overlay(GLFWwindow* window) {
    HWND hwnd = glfwGetWin32Window(window);
    LONG_PTR extended = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    extended |= WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, extended);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

SceneSnapshot query_scene(const std::vector<snow::Rect>& monitors) {
    SceneSnapshot result;
    result.fullscreen.assign(monitors.size(), false);
    EnumContext context{&result, &monitors, GetCurrentProcessId()};
    EnumWindows(collect_window, reinterpret_cast<LPARAM>(&context));
    return result;
}

snow::Cursor cursor() {
    POINT point{};
    if (!GetCursorPos(&point)) return {};
    return {static_cast<float>(point.x), static_cast<float>(point.y), true,
            (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0};
}

bool low_power_mode() {
    SYSTEM_POWER_STATUS ps{};
    if (!GetSystemPowerStatus(&ps)) return false;
    return ps.SystemStatusFlag == 1 ||
        (ps.ACLineStatus == 0 && ps.BatteryLifePercent != 255 && ps.BatteryLifePercent <= 35);
}

std::filesystem::path settings_path() {
    PWSTR home = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &home)) && home) {
        std::filesystem::path path(home);
        CoTaskMemFree(home);
        return path / "DesktopSnow" / "settings.ini";
    }
    return std::filesystem::path("settings.ini");
}

void create_status_icon(void (*on_action)(int)) {
    g_callback = on_action;
    WNDCLASSW cls{};
    cls.lpfnWndProc = tray_wnd_proc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = kTrayClass;
    RegisterClassW(&cls);
    g_tray_window = CreateWindowExW(WS_EX_TOOLWINDOW, kTrayClass, L"Desktop Snow", WS_OVERLAPPED,
                                   0,0,0,0, nullptr, nullptr, cls.hInstance, nullptr);
    if (!g_tray_window) return;
    g_icon = {};
    g_icon.cbSize = sizeof(g_icon);
    g_icon.hWnd = g_tray_window;
    g_icon.uID = 1;
    g_icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_icon.uCallbackMessage = WM_SNOW_TRAY;
    g_icon.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    lstrcpyW(g_icon.szTip, L"Desktop Snow");
    Shell_NotifyIconW(NIM_ADD, &g_icon);
    g_icon.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_icon);
}

void update_status_icon(const snow::Settings& settings) { g_settings = settings; }

void destroy_status_icon() {
    if (g_tray_window) {
        Shell_NotifyIconW(NIM_DELETE, &g_icon);
        DestroyWindow(g_tray_window);
        g_tray_window = nullptr;
    }
    g_callback = nullptr;
}
} // namespace platform
