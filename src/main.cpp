#include "engine.hpp"
#include "platform.hpp"
#include "renderer.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace {
using Clock = std::chrono::steady_clock;

struct Overlay {
    GLFWmonitor* monitor = nullptr;
    snow::Rect rect;
    GLFWwindow* window = nullptr;
    std::unique_ptr<snow::World> world;
    std::unique_ptr<Renderer> renderer;
    bool cleared = false;
};

snow::Settings g_settings;
std::vector<std::unique_ptr<Overlay>> g_overlays;
bool g_running = true;
bool g_monitor_dirty = true;

void log_glfw_error(int code, const char* description) {
    std::cerr << "GLFW error " << code << ": " << (description ? description : "unknown") << '\n';
}

void load_settings() {
    std::ifstream file(platform::settings_path());
    std::string line;
    while (std::getline(file, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0,eq), value = line.substr(eq+1);
        try {
            const int n = std::stoi(value);
            if (key == "intensity" && n >= 0 && n <= 2)
                g_settings.intensity = static_cast<snow::Intensity>(n);
            else if (key == "tool" && n >= 0 && n <= 2)
                g_settings.tool = static_cast<snow::Tool>(n);
            else if (key == "wind")
                g_settings.wind = static_cast<float>(std::max(-70, std::min(70, n)));
            else if (key == "fps" && (n == 15 || n == 30 || n == 60))
                g_settings.target_fps = n;
            else if (key == "pause_fullscreen")
                g_settings.pause_fullscreen = n != 0;
        } catch (const std::exception&) { /* Ignore malformed preference entries. */ }
    }
}

void save_settings() {
    try {
        const auto path = platform::settings_path();
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::trunc);
        file << "intensity=" << static_cast<int>(g_settings.intensity) << '\n'
             << "tool=" << static_cast<int>(g_settings.tool) << '\n'
             << "wind=" << static_cast<int>(g_settings.wind) << '\n'
             << "fps=" << g_settings.target_fps << '\n'
             << "pause_fullscreen=" << (g_settings.pause_fullscreen ? 1 : 0) << '\n';
    } catch (const std::exception& e) {
        std::cerr << "Could not save settings: " << e.what() << '\n';
    }
}

void menu_action(int action) {
    using namespace platform;
    switch (action) {
        case Pause: g_settings.paused = true; break;
        case Resume: g_settings.paused = false; break;
        case Clear:
            for (auto& overlay : g_overlays) overlay->world->clear();
            break;
        case Light: g_settings.intensity = snow::Intensity::Light; break;
        case Normal: g_settings.intensity = snow::Intensity::Normal; break;
        case Blizzard: g_settings.intensity = snow::Intensity::Blizzard; break;
        case WindLeft: g_settings.wind = -55; break;
        case WindCalm: g_settings.wind = 0; break;
        case WindRight: g_settings.wind = 55; break;
        case Brush: g_settings.tool = snow::Tool::Brush; break;
        case Roll: g_settings.tool = snow::Tool::Roll; break;
        case ToolOff: g_settings.tool = snow::Tool::None; break;
        case Fps15: g_settings.target_fps = 15; break;
        case Fps30: g_settings.target_fps = 30; break;
        case Fps60: g_settings.target_fps = 60; break;
        case FullscreenToggle: g_settings.pause_fullscreen = !g_settings.pause_fullscreen; break;
        case Quit: g_running = false; break;
        default: return;
    }
    platform::update_status_icon(g_settings);
    if (action != Clear && action != Quit) save_settings();
}

void monitor_callback(GLFWmonitor*, int) { g_monitor_dirty = true; }

struct MonitorLayout {
    GLFWmonitor* monitor;
    snow::Rect rect;
};

std::vector<MonitorLayout> get_monitor_layout() {
    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    std::vector<MonitorLayout> result;
    for (int i = 0; i < count; ++i) {
        if (!monitors[i]) continue;
        const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
        if (!mode || mode->width <= 0 || mode->height <= 0) continue;
        int x = 0, y = 0;
        glfwGetMonitorPos(monitors[i], &x, &y);
        result.push_back({monitors[i], {static_cast<float>(x), static_cast<float>(y),
                                       static_cast<float>(mode->width),
                                       static_cast<float>(mode->height)}});
    }
    return result;
}

bool same_layout(const std::vector<MonitorLayout>& layout) {
    if (layout.size() != g_overlays.size()) return false;
    for (std::size_t i = 0; i < layout.size(); ++i) {
        const Overlay& overlay = *g_overlays[i];
        const auto& next = layout[i];
        if (overlay.monitor != next.monitor || overlay.rect.x != next.rect.x ||
            overlay.rect.y != next.rect.y || overlay.rect.w != next.rect.w ||
            overlay.rect.h != next.rect.h) return false;
    }
    return true;
}

void remove_all_monitors() {
    for (auto& overlay : g_overlays) {
        if (!overlay->window) continue;
        glfwMakeContextCurrent(overlay->window);
        overlay->renderer.reset();
        glfwDestroyWindow(overlay->window);
        overlay->window = nullptr;
    }
    g_overlays.clear();
    glfwMakeContextCurrent(nullptr);
}

void create_monitors(const std::vector<MonitorLayout>& layout) {
    remove_all_monitors();
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_MOUSE_PASSTHROUGH, GLFW_TRUE);

    for (std::size_t i = 0; i < layout.size(); ++i) {
        const auto& display = layout[i];
        auto o = std::make_unique<Overlay>();
        o->monitor = display.monitor;
        o->rect = display.rect;
        o->window = glfwCreateWindow(static_cast<int>(display.rect.w),
                                     static_cast<int>(display.rect.h),
                                     "Desktop Snow Overlay", nullptr, nullptr);
        if (!o->window) throw std::runtime_error("GLFW failed to create OpenGL overlay window");
        glfwSetWindowPos(o->window, static_cast<int>(display.rect.x), static_cast<int>(display.rect.y));
        if (!glfwGetWindowAttrib(o->window, GLFW_TRANSPARENT_FRAMEBUFFER)) {
            glfwDestroyWindow(o->window);
            o->window = nullptr;
            throw std::runtime_error("Transparent desktop composition unavailable on this display");
        }
        try {
            platform::configure_overlay(o->window);
            glfwMakeContextCurrent(o->window);
            glfwSwapInterval(0); // explicit pacing; no per-monitor vsync blocking
            o->renderer = std::make_unique<Renderer>(o->window);
            o->world = std::make_unique<snow::World>(o->rect, 0xAB57F20ULL + i * 131);
            glfwShowWindow(o->window);
            o->renderer->clear();
            glfwSwapBuffers(o->window);
            g_overlays.push_back(std::move(o));
        } catch (...) {
            if (o && o->window) {
                glfwMakeContextCurrent(o->window);
                o->renderer.reset();
                glfwDestroyWindow(o->window);
            }
            throw;
        }
    }
    g_monitor_dirty = false;
}

int run() {
    platform::prepare_process();
    load_settings();
    glfwSetErrorCallback(log_glfw_error);
    if (!glfwInit()) throw std::runtime_error("GLFW initialization failed");
    glfwSetMonitorCallback(monitor_callback);
    try {
        platform::create_status_icon(menu_action);
        platform::update_status_icon(g_settings);
        create_monitors(get_monitor_layout());
        platform::SceneSnapshot scene;
        auto last_tick = Clock::now();
        auto next_frame = last_tick;
        auto last_scene = last_tick - std::chrono::seconds(1);
        auto last_layout = last_tick - std::chrono::seconds(2);
        auto last_power = last_tick - std::chrono::seconds(10);
        bool low_power = false;
        double avg_cpu_ms = 0.0;
        int effective_fps = g_settings.target_fps;
        while (g_running) {
            const auto now = Clock::now();
            if (now - last_layout >= std::chrono::seconds(2) || g_monitor_dirty) {
                const auto layout = get_monitor_layout();
                if (!same_layout(layout)) {
                    create_monitors(layout);
                    scene = {};
                    last_scene = now - std::chrono::seconds(1);
                }
                last_layout = now;
                g_monitor_dirty = false;
            }
            if (now - last_power >= std::chrono::seconds(5)) {
                low_power = platform::low_power_mode();
                last_power = now;
            }
#ifdef __APPLE__
            constexpr int scan_ms = 180;
#else
            constexpr int scan_ms = 110;
#endif
            if (!g_settings.paused &&
                now - last_scene >= std::chrono::milliseconds(low_power ? 650 : scan_ms)) {
                std::vector<snow::Rect> monitors;
                monitors.reserve(g_overlays.size());
                for (const auto& o : g_overlays) monitors.push_back(o->rect);
                scene = platform::query_scene(monitors);
                for (auto& o : g_overlays) o->world->set_surfaces(scene.surfaces);
                last_scene = now;
            }
            // Lightweight automatic throttling when on battery/low-power or CPU-constrained.
            effective_fps = low_power ? std::min(15, g_settings.target_fps) : g_settings.target_fps;
            if (avg_cpu_ms > 12.0 && effective_fps > 15) effective_fps = 15;
            if (g_settings.paused) effective_fps = 5;
            if (now < next_frame) {
                const double seconds = std::chrono::duration<double>(next_frame - now).count();
                glfwWaitEventsTimeout(std::min(0.10, seconds));
                continue;
            }
            glfwPollEvents();
            const auto cpu_start = Clock::now();
            const float dt = std::chrono::duration<float>(now - last_tick).count();
            const snow::Cursor cursor = platform::cursor();
            bool all_suppressed = true;
            for (std::size_t i = 0; i < g_overlays.size(); ++i) {
                auto& o = *g_overlays[i];
                const bool fullscreen = i < scene.fullscreen.size() && scene.fullscreen[i];
                const bool suppressed = g_settings.paused || (g_settings.pause_fullscreen && fullscreen);
                if (suppressed) {
                    if (!o.cleared) {
                        glfwMakeContextCurrent(o.window);
                        o.renderer->clear();
                        glfwSwapBuffers(o.window);
                        o.cleared = true;
                    }
                    continue;
                }
                all_suppressed = false;
                o.cleared = false;
                glfwMakeContextCurrent(o.window);
                o.world->step(dt, g_settings, cursor);
                o.renderer->draw(*o.world);
                glfwSwapBuffers(o.window);
            }
            if (all_suppressed) effective_fps = 5;
            const double cpu_ms = std::chrono::duration<double, std::milli>(Clock::now()-cpu_start).count();
            avg_cpu_ms = avg_cpu_ms == 0.0 ? cpu_ms : avg_cpu_ms * 0.97 + cpu_ms * 0.03;
            last_tick = now;
            next_frame = now + std::chrono::duration_cast<Clock::duration>(
                std::chrono::duration<double>(1.0 / effective_fps));
        }
        platform::destroy_status_icon();
        remove_all_monitors();
        glfwTerminate();
        return 0;
    } catch (...) {
        platform::destroy_status_icon();
        remove_all_monitors();
        glfwTerminate();
        throw;
    }
}
} // namespace

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
#else
int main(int, char**) {
#endif
    try {
        return run();
    } catch (const std::exception& e) {
#ifdef _WIN32
        MessageBoxA(nullptr, e.what(), "Desktop Snow startup error", MB_OK | MB_ICONERROR);
#else
        std::cerr << "Desktop Snow startup error: " << e.what() << '\n';
#endif
        return 1;
    }
}
