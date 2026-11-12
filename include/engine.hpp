#pragma once
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace snow {

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    float right() const { return x + w; }
    float bottom() const { return y + h; }
};

// OS coordinates use a top-left origin, including monitors to the left of the primary.
struct SurfaceInput {
    std::uint64_t id = 0;
    Rect bounds;
    bool dock = false;
};

struct Particle {
    float x = 0, y = 0, vx = 0, vy = 0, radius = 0, alpha = 0;
};

struct Vertex {
    float x, y;
    float r, g, b, a;
    float size;
};

struct Cursor {
    float x = 0, y = 0;
    bool valid = false;
    bool pressed = false; // Shift held; enables rolling in Tool::Roll.
};

enum class Tool { Brush, Roll, None };
enum class Intensity { Light, Normal, Blizzard };

struct Settings {
    Intensity intensity = Intensity::Normal;
    Tool tool = Tool::Brush;
    float wind = 0.0f; // pixels per second; -70 to +70
    int target_fps = 30;
    bool paused = false;
    bool pause_fullscreen = true;
};

struct Stats {
    std::size_t particles = 0, surfaces = 0, snow_bins = 0, balls = 0;
};

class World {
public:
    explicit World(Rect viewport, std::uint64_t seed = 0x4D595DF4D0F33173ULL);
    void set_viewport(Rect viewport);
    void set_surfaces(const std::vector<SurfaceInput>& surfaces);
    void step(float delta_seconds, const Settings& settings, Cursor cursor = {});
    void clear();
    void clear_piles();
    void build_vertices(std::vector<Vertex>& points, std::vector<Vertex>& triangles) const;
    void build_point_vertices(std::vector<Vertex>& points) const;
    void build_pile_vertices(std::vector<Vertex>& triangles) const;
    Stats stats() const;
    const std::vector<Particle>& particles() const { return flakes_; }
    float total_snow(std::uint64_t surface_id) const;
    Rect viewport() const { return viewport_; }
    std::uint64_t geometry_version() const { return geometry_version_; }

    static bool is_borderless_fullscreen(Rect screen, Rect window, bool has_standard_frame);

private:
    struct Surface {
        std::uint64_t id = 0;
        Rect rect;
        bool dock = false;
        std::vector<float> heights;
        std::vector<std::uint8_t> visible;
    };
    struct Ball {
        std::uint64_t id = 0;
        float offset = 0;
        float radius = 0;
        unsigned stack = 1;
    };
    Rect viewport_;
    std::uint64_t rng_;
    std::uint64_t geometry_version_ = 1;
    std::vector<Particle> flakes_;
    std::vector<Surface> surfaces_;
    std::vector<std::vector<std::size_t>> columns_;
    std::vector<Ball> balls_;
    Cursor previous_cursor_{};
    float spawn_budget_ = 0;
    float rolling_mass_ = 0;
    std::uint64_t rolling_surface_id_ = 0;
    bool was_pressed_ = false;

    float random01();
    void spawn_one();
    void rebuild_columns();
    void interact(Cursor cursor, const Settings& settings);
    const Surface* find_surface(std::uint64_t id) const;
    Surface* find_surface(std::uint64_t id);
    static std::size_t bin_of(const Surface& surface, float x);
};

} // namespace snow
