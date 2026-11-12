#include "engine.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>

using namespace snow;
static int checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { std::cerr << "FAILED " << __FILE__ << ":" << __LINE__ << " -> " #c "\n"; std::exit(1); } } while (0)

void accumulate(World& world, const Settings& cfg, int frames) {
    for (int i = 0; i < frames; ++i) world.step(1.0f / 30.0f, cfg);
}

void test_initialization() {
    World w({-100, -20, 500, 350}, 123);
    CHECK(w.stats().surfaces == 1);
    CHECK(w.stats().particles == 0);
    Settings cfg;
    accumulate(w, cfg, 30);
    CHECK(w.stats().particles > 0);
    CHECK(w.stats().particles < 1801);
    CHECK(w.total_snow(UINT64_MAX) >= 0);
}

void test_snow_on_window_and_move() {
    World w({0,0,500,300}, 42);
    Settings cfg;
    w.set_surfaces({{17, {0,110,500,150}, false}});
    CHECK(w.stats().surfaces == 2);
    accumulate(w, cfg, 700);
    const float before = w.total_snow(17);
    CHECK(before > 1.0f);
    w.set_surfaces({{17, {0,150,500,150}, false}});
    CHECK(std::abs(w.total_snow(17) - before) < 0.1f);
    w.set_surfaces({{17, {0,150,300,150}, false}});
    CHECK(w.total_snow(17) > 0);
    w.set_surfaces({});
    CHECK(w.total_snow(17) == 0);
}

void test_clear_pause_and_vertices() {
    World w({0,0,720,420}, 37);
    Settings cfg;
    w.set_surfaces({{2, {90,140,500,220}, false}});
    accumulate(w, cfg, 1500);
    CHECK(w.total_snow(2) > 0);
    const auto start = w.particles();
    cfg.paused = true;
    w.step(3.0f, cfg, {200,200,true,false});
    CHECK(start.size() == w.particles().size());
    for (std::size_t i = 0; i < start.size(); ++i) {
        CHECK(start[i].x == w.particles()[i].x);
        CHECK(start[i].y == w.particles()[i].y);
    }
    std::vector<Vertex> points, triangles;
    w.build_vertices(points, triangles);
    CHECK(!points.empty());
    CHECK(triangles.size() % 3 == 0);
    CHECK(!triangles.empty());
    w.clear();
    CHECK(w.stats().particles == 0);
    CHECK(w.total_snow(2) == 0);
    CHECK(w.total_snow(UINT64_MAX) == 0);
    CHECK(w.stats().balls == 0);
}

void test_wind_and_determinism() {
    World a({0,0,720,420}, 127), b({0,0,720,420}, 127), c({0,0,720,420}, 127);
    Settings cfg;
    Settings windy = cfg;
    windy.wind = 70;
    for (int i = 0; i < 15; ++i) {
        a.step(1/30.0f, cfg);
        b.step(1/30.0f, cfg);
        c.step(1/30.0f, windy);
    }
    CHECK(a.particles().size() == b.particles().size());
    CHECK(a.particles().size() == c.particles().size());
    CHECK(!a.particles().empty());
    for (std::size_t i = 0; i < a.particles().size(); ++i) {
        CHECK(a.particles()[i].x == b.particles()[i].x);
        CHECK(a.particles()[i].y == b.particles()[i].y);
        CHECK(c.particles()[i].x > a.particles()[i].x);
    }
}

void test_intensity_cap_and_dt() {
    World w({0,0,3840,2160}, 99);
    Settings s;
    s.intensity = Intensity::Blizzard;
    for (int i = 0; i < 1000; ++i) w.step(1.0f / 30, s);
    CHECK(w.stats().particles <= 1800);
    w.step(9999.0f, s); // clamped, must not explode or allocate unboundedly
    CHECK(w.stats().particles <= 1800);
    for (const auto& p : w.particles()) {
        CHECK(std::isfinite(p.x) && std::isfinite(p.y));
    }
    w.set_viewport({-2560, 0, 2560, 1440});
    CHECK(w.stats().particles == 0);
    CHECK(w.stats().surfaces == 1);
}

void test_invalid_surfaces() {
    World w({0,0,600,300});
    w.set_surfaces({{12,{10,40,400,200},false}, {12,{10,50,400,200},false},
        {13,{-50,-100,1,1},false}, {14,{9999,0,200,200},false},
        {15,{30,30,-400,200},false}});
    CHECK(w.stats().surfaces == 2);
    const auto version = w.geometry_version();
    w.set_surfaces({{12,{10,40,400,200},false}});
    CHECK(w.geometry_version() == version);
}

void test_occluded_window_edges_and_zorder() {
    World w({0,0,450,270}, 715);
    Settings cfg;
    const SurfaceInput back{9,{0,120,450,180},false};
    const SurfaceInput front{8,{0,65,400,180},false};
    w.set_surfaces({back});
    accumulate(w, cfg, 750);
    std::vector<Vertex> points, visible;
    w.build_vertices(points, visible);
    const std::size_t pile_triangles_before = visible.size();
    CHECK(pile_triangles_before > 0);
    const auto version = w.geometry_version();
    w.set_surfaces({front, back});
    CHECK(w.geometry_version() > version);
    w.build_vertices(points, visible);
    CHECK(visible.size() < pile_triangles_before);
    const auto changed_version = w.geometry_version();
    w.set_surfaces({back,front});
    CHECK(w.geometry_version() > changed_version);
}

void test_negative_monitor_coordinates() {
    World left({-1920,-100,1920,1080}, 311);
    World right({0,0,1920,1080}, 311);
    Settings cfg;
    left.set_surfaces({{90, {-1500,300,900,400},false}});
    right.set_surfaces({{91, {200,300,900,400},false}});
    accumulate(left,cfg,100);
    accumulate(right,cfg,100);
    CHECK(left.stats().particles > 0);
    CHECK(right.stats().particles > 0);
    for (const auto& p : left.particles()) {
        CHECK(p.x >= -1935 && p.x < 15);
    }
    for (const auto& p : right.particles()) {
        CHECK(p.x >= -15 && p.x < 1935);
    }
}

void test_randomized_stress() {
    std::mt19937 random(20261112);
    std::uniform_int_distribution<int> x(-700, 1500), y(-350, 850);
    std::uniform_int_distribution<int> w(5, 1100), h(3, 900);
    std::uniform_int_distribution<int> cnt(0, 80);
    World world({-420,-200,1600,900}, 91872);
    Settings cfg;
    cfg.intensity = Intensity::Blizzard;
    cfg.wind = -65;
    std::vector<Vertex> points, triangles;
    for (int frame = 0; frame < 2200; ++frame) {
        if (frame % 18 == 0) {
            std::vector<SurfaceInput> surfaces;
            const int n = cnt(random);
            for (int i = 0; i < n; ++i) {
                surfaces.push_back({static_cast<std::uint64_t>(i + 1),
                    {static_cast<float>(x(random)), static_cast<float>(y(random)),
                     static_cast<float>(w(random)), static_cast<float>(h(random))}, false});
            }
            surfaces.push_back({987654,{0,0,1e25f,30},false}); // absurd geometry is rejected
            world.set_surfaces(surfaces);
        }
        cfg.tool = (frame % 3 == 0) ? Tool::Roll : Tool::Brush;
        const Cursor cursor{static_cast<float>(x(random)),static_cast<float>(y(random)),
                            true, (frame % 7) < 5};
        world.step(frame % 31 == 0 ? 2.0f : 1.0f/30, cfg, cursor);
        CHECK(world.stats().particles <= 1800);
        CHECK(world.stats().surfaces <= 257);
        if (frame % 23 == 0) {
            world.build_vertices(points,triangles);
            CHECK(triangles.size() % 3 == 0);
            for (const auto& v : points)
                CHECK(std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.size));
        }
    }
}

void test_fullscreen() {
    const Rect monitor{-1920, 0, 1920, 1080};
    CHECK(World::is_borderless_fullscreen(monitor, monitor, false));
    CHECK(!World::is_borderless_fullscreen(monitor, monitor, true));
    CHECK(!World::is_borderless_fullscreen(monitor, {-1920,0,1900,1050}, false));
    CHECK(!World::is_borderless_fullscreen(monitor, {0,0,1920,1080}, false));
}

void test_tools_safe() {
    World w({0,0,540,240}, 2);
    Settings s;
    w.set_surfaces({{11,{0,90,540,200},false}});
    accumulate(w,s,1000);
    auto h = w.total_snow(11);
    CHECK(h > 0);
    const auto particles_before = w.stats().particles;
    s.tool = Tool::Brush;
    for (int x = 3; x <= 530; x += 7) w.step(1/30.0f,s,{(float)x,88.0f,true,false});
    CHECK(w.total_snow(11) < h + 60);
    s.tool = Tool::Roll;
    for (int x = 10; x <= 520; x += 5) w.step(1/30.0f,s,{(float)x,89.0f,true,true});
    w.step(1/30.0f,s,{515,89,true,false});
    CHECK(w.stats().balls <= 48);
    CHECK(w.stats().particles <= 1800);
    (void)particles_before;
}

void test_framerate_independent_simulation_time() {
    // Equal elapsed time must create equal flakes at 15 and 30 fps.
    // The old 50 ms dt clamp made 15 fps snow fall/spawn too slowly.
    World slow({0,0,1600,4000}, 123), fast({0,0,1600,4000}, 123);
    Settings cfg;
    for (int i = 0; i < 15; ++i) slow.step(1.0f / 15.0f, cfg);
    for (int i = 0; i < 30; ++i) fast.step(1.0f / 30.0f, cfg);
    CHECK(std::abs(static_cast<int>(slow.stats().particles) -
                   static_cast<int>(fast.stats().particles)) <= 1);
}

void test_unchanged_scene_keeps_cached_geometry() {
    World w({0,0,1500,900}, 55);
    std::vector<SurfaceInput> surfaces;
    for (int i = 0; i < 240; ++i)
        surfaces.push_back({static_cast<std::uint64_t>(i+1),
          {float((i*233)%1490),float((i*107)%800),200,120},false});
    w.set_surfaces(surfaces);
    const auto version = w.geometry_version();
    for (int i = 0; i < 50; ++i) w.set_surfaces(surfaces);
    CHECK(w.geometry_version() == version);
    CHECK(w.stats().surfaces == 241);
    surfaces[60].bounds.x += 3;
    w.set_surfaces(surfaces);
    CHECK(w.geometry_version() > version);
}

int main() {
    test_initialization();
    test_snow_on_window_and_move();
    test_clear_pause_and_vertices();
    test_wind_and_determinism();
    test_intensity_cap_and_dt();
    test_invalid_surfaces();
    test_fullscreen();
    test_occluded_window_edges_and_zorder();
    test_negative_monitor_coordinates();
    test_randomized_stress();
    test_tools_safe();
    test_framerate_independent_simulation_time();
    test_unchanged_scene_keeps_cached_geometry();
    std::cout << "OK: " << checks << " assertions across 13 test groups\n";
}
