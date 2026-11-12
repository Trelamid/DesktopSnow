#include "engine.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <vector>

int main(int argc, char** argv) {
    const int frames = (argc > 1) ? std::max(1, std::atoi(argv[1])) : 3000;
    std::vector<snow::World> worlds;
    worlds.emplace_back(snow::Rect{0,0,2560,1440}, 71);
    worlds.emplace_back(snow::Rect{-1920,0,1920,1080}, 72);
    const snow::Settings settings{snow::Intensity::Blizzard, snow::Tool::Brush, 25, 30, false, true};
    std::vector<snow::SurfaceInput> windows;
    for (std::uint64_t i = 0; i < 75; ++i) {
        const float x = static_cast<float>((i * 83) % 2800) - 300;
        const float y = static_cast<float>((i * 47) % 990) + 35;
        windows.push_back({i + 1, {x, y, 480, 315}, false});
    }
    const auto start = std::chrono::steady_clock::now();
    std::size_t sampled_particles = 0;
    std::vector<snow::Vertex> points, triangles;
    for (int i = 0; i < frames; ++i) {
        if (i % 9 == 0) {
            for (auto& window : windows) window.bounds.x += (i % 2 ? 1.0f : -1.0f);
            for (auto& world : worlds) world.set_surfaces(windows);
        }
        for (auto& world : worlds) {
            world.step(1/30.0f,settings);
            world.build_vertices(points, triangles);
            sampled_particles += world.stats().particles;
        }
    }
    const auto end = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(end - start).count();
    const double per_frame = ms / (frames * worlds.size());
    std::cout << "CPU physics + mesh benchmark (no GPU, no OS enumeration):\n"
              << "  monitors: " << worlds.size() << ", frames/monitor: " << frames << "\n"
              << "  avg particles/monitor/frame: " << sampled_particles / (worlds.size() * frames) << "\n"
              << "  total: " << ms << " ms\n"
              << "  CPU avg per monitor frame: " << per_frame << " ms\n"
              << "  30fps CPU frame budget share: " << per_frame / (1000.0/30.0)*100 << "%\n";
}
