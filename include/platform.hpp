#pragma once
#include "engine.hpp"
#include <filesystem>
#include <vector>
#include <GLFW/glfw3.h>

namespace platform {

enum MenuAction {
    Pause = 100,
    Resume, Clear, Light, Normal, Blizzard,
    WindLeft, WindCalm, WindRight,
    Brush, Roll, ToolOff,
    Fps15, Fps30, Fps60,
    FullscreenToggle, Quit
};

struct SceneSnapshot {
    std::vector<snow::SurfaceInput> surfaces;
    std::vector<bool> fullscreen;
};

void prepare_process();
void configure_overlay(GLFWwindow* window);
SceneSnapshot query_scene(const std::vector<snow::Rect>& monitors);
snow::Cursor cursor();
bool low_power_mode();
std::filesystem::path settings_path();
void create_status_icon(void (*on_action)(int));
void update_status_icon(const snow::Settings& settings);
void destroy_status_icon();

} // namespace platform
