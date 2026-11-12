#include "engine.hpp"
#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    snow::World world({0, 0, 3840, 2160}, 7);
    std::vector<snow::SurfaceInput> windows;
    windows.reserve(250);
    for (int i = 0; i < 250; ++i) {
        windows.push_back({static_cast<std::uint64_t>(i + 1),
            {float((i * 331) % 3800 - 200), float((i * 107) % 1700 + 70),
             float(350 + (i % 6) * 100), float(160 + (i % 4) * 140)}, false});
    }
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    for (int i = 0; i < 100; ++i) world.set_surfaces(windows);
    const auto unchanged = Clock::now();
    for (int i = 0; i < 100; ++i) {
        windows[static_cast<std::size_t>(i) % windows.size()].bounds.x += 0.25f;
        world.set_surfaces(windows);
    }
    const auto moved = Clock::now();
    std::cout << "Scene update benchmark (250 windows, 4K virtual monitor; CPU only):\n"
              << "  unchanged scene: "
              << std::chrono::duration<double, std::milli>(unchanged - start).count() / 100
              << " ms/update\n"
              << "  moving scene: "
              << std::chrono::duration<double, std::milli>(moved - unchanged).count() / 100
              << " ms/update\n"
              << "  geometry version: " << world.geometry_version() << "\n";
}
