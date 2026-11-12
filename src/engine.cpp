#include "engine.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <unordered_set>

namespace snow {
namespace {
constexpr float kBinWidth = 9.0f;
constexpr float kColumnWidth = 24.0f;
constexpr float kMaxPileHeight = 26.0f;
constexpr std::size_t kMaxParticles = 1800;
constexpr std::size_t kMaxSnowballs = 48;
constexpr std::size_t kMaxTrackedSurfaces = 256;
constexpr std::uint64_t kGroundID = UINT64_MAX;

float clampf(float v, float lo, float hi) { return std::max(lo, std::min(v, hi)); }

void add_triangle(std::vector<Vertex>& out, const Vertex& a, const Vertex& b, const Vertex& c) {
    out.push_back(a);
    out.push_back(b);
    out.push_back(c);
}

void add_circle(std::vector<Vertex>& out, float x, float y, float radius,
                float r, float g, float b, float a) {
    if (radius <= 0.0f) return;
    out.push_back({x, y, r, g, b, a, radius * 2.0f});
}
} // namespace

World::World(Rect viewport, std::uint64_t seed)
    : viewport_(viewport), rng_(seed ? seed : 1) {
    set_surfaces({});
}

float World::random01() {
    // xorshift64*; deterministic and branchless in the hot path.
    rng_ ^= rng_ >> 12;
    rng_ ^= rng_ << 25;
    rng_ ^= rng_ >> 27;
    const std::uint64_t n = rng_ * 2685821657736338717ULL;
    return static_cast<float>((n >> 40) * (1.0 / 16777216.0));
}

std::size_t World::bin_of(const Surface& surface, float x) {
    if (surface.heights.empty()) return 0;
    const auto index = static_cast<std::size_t>(std::max(0.0f, (x - surface.rect.x) / kBinWidth));
    return std::min(index, surface.heights.size() - 1);
}

void World::set_viewport(Rect viewport) {
    if (viewport.w <= 0 || viewport.h <= 0) return;
    if (viewport.x == viewport_.x && viewport.y == viewport_.y &&
        viewport.w == viewport_.w && viewport.h == viewport_.h) return;
    viewport_ = viewport;
    flakes_.clear();
    balls_.clear();
    rolling_mass_ = 0;
    set_surfaces({});
}

void World::set_surfaces(const std::vector<SurfaceInput>& inputs) {
    if (viewport_.w <= 0 || viewport_.h <= 0) return;
    std::unordered_map<std::uint64_t, Surface> old;
    old.reserve(surfaces_.size());
    std::vector<std::uint64_t> old_order;
    old_order.reserve(surfaces_.size());
    for (auto& s : surfaces_) {
        old_order.push_back(s.id);
        old.emplace(s.id, std::move(s));
    }

    std::vector<SurfaceInput> all;
    all.reserve(inputs.size() + 1);
    std::unordered_set<std::uint64_t> used;
    for (const auto& input : inputs) {
        if (all.size() >= kMaxTrackedSurfaces) break;
        if (input.id == kGroundID || input.bounds.w < 15.0f ||
            input.bounds.h < 5.0f || input.bounds.w > 65536.0f ||
            input.bounds.h > 65536.0f || !std::isfinite(input.bounds.x) ||
            !std::isfinite(input.bounds.y) || !std::isfinite(input.bounds.w) ||
            !std::isfinite(input.bounds.h)) continue;
        if (input.bounds.right() <= viewport_.x || input.bounds.x >= viewport_.right() ||
            input.bounds.y < viewport_.y - 2 || input.bounds.y >= viewport_.bottom()) continue;
        if (used.insert(input.id).second) all.push_back(input);
    }
    all.push_back({kGroundID,
                   {viewport_.x, viewport_.bottom() - 1.0f, viewport_.w, 12.0f}, false});

    surfaces_.clear();
    surfaces_.reserve(all.size());
    bool changed = old.size() != all.size();
    for (std::size_t order = 0; order < all.size(); ++order) {
        const auto& input = all[order];
        if (order >= old_order.size() || old_order[order] != input.id) changed = true;
        Surface s;
        s.id = input.id;
        s.rect = input.bounds;
        s.dock = input.dock;
        const auto bins = std::max<std::size_t>(1,
            static_cast<std::size_t>(std::ceil(s.rect.w / kBinWidth)));
        s.heights.resize(bins, 0.0f);
        auto it = old.find(s.id);
        if (it != old.end()) {
            const Surface& was = it->second;
            if (was.rect.x != s.rect.x || was.rect.y != s.rect.y ||
                was.rect.w != s.rect.w || was.rect.h != s.rect.h || was.dock != s.dock) changed = true;
            if (!was.heights.empty()) {
                for (std::size_t i = 0; i < bins; ++i) {
                    // Keep snow attached to windows when they're moved or resized.
                    const std::size_t prior = std::min(was.heights.size() - 1,
                        static_cast<std::size_t>((static_cast<double>(i) + 0.5) *
                                                 was.heights.size() / bins));
                    s.heights[i] = was.heights[prior];
                }
            }
        } else {
            changed = true;
        }
        surfaces_.push_back(std::move(s));
    }
    // Input order is front-to-back (native OS z-order). Hide pile bins whose
    // supporting window edge is covered by a foreground window.
    for (std::size_t i = 0; i < surfaces_.size(); ++i) {
        Surface& support = surfaces_[i];
        support.visible.assign(support.heights.size(), 1);
        for (std::size_t bin = 0; bin < support.visible.size(); ++bin) {
            const float x = support.rect.x + std::min(support.rect.w - 0.1f,
                                                       (bin + 0.5f) * kBinWidth);
            const float y = support.rect.y - 0.5f;
            for (std::size_t front_index = 0; front_index < i; ++front_index) {
                const Rect& front = surfaces_[front_index].rect;
                if (x >= front.x && x < front.right() &&
                    y >= front.y && y < front.bottom()) {
                    support.visible[bin] = 0;
                    break;
                }
            }
        }
    }
    if (changed) ++geometry_version_;
    rebuild_columns();
    // Balls on closed windows disappear, as on a real moved-away surface.
    balls_.erase(std::remove_if(balls_.begin(), balls_.end(), [this](const Ball& b) {
        return find_surface(b.id) == nullptr;
    }), balls_.end());
}

void World::rebuild_columns() {
    const std::size_t count = static_cast<std::size_t>(std::ceil(viewport_.w / kColumnWidth)) + 1;
    columns_.assign(count, {});
    for (std::size_t si = 0; si < surfaces_.size(); ++si) {
        const Rect& r = surfaces_[si].rect;
        const float start = clampf((r.x - viewport_.x) / kColumnWidth, 0.0f,
                                   static_cast<float>(count - 1));
        const float stop = clampf((r.right() - viewport_.x) / kColumnWidth, 0.0f,
                                  static_cast<float>(count - 1));
        const std::size_t first = static_cast<std::size_t>(start);
        const std::size_t last = static_cast<std::size_t>(stop);
        for (std::size_t i = first; i <= last; ++i) columns_[i].push_back(si);
    }
}

void World::spawn_one() {
    Particle p;
    p.x = viewport_.x + random01() * viewport_.w;
    p.y = viewport_.y - random01() * 35.0f;
    p.vx = (random01() - 0.5f) * 16.0f;
    p.vy = 44.0f + random01() * 71.0f;
    p.radius = 0.8f + random01() * 2.0f;
    p.alpha = 0.42f + random01() * 0.53f;
    flakes_.push_back(p);
}

World::Surface* World::find_surface(std::uint64_t id) {
    for (auto& s : surfaces_) if (s.id == id) return &s;
    return nullptr;
}
const World::Surface* World::find_surface(std::uint64_t id) const {
    for (const auto& s : surfaces_) if (s.id == id) return &s;
    return nullptr;
}

void World::interact(Cursor cursor, const Settings& settings) {
    if (!cursor.valid || settings.tool == Tool::None) {
        previous_cursor_ = cursor;
        was_pressed_ = cursor.pressed;
        return;
    }
    if (!previous_cursor_.valid) {
        previous_cursor_ = cursor;
        was_pressed_ = cursor.pressed;
        return;
    }
    const float dx = cursor.x - previous_cursor_.x;
    const float dy = cursor.y - previous_cursor_.y;
    const float distance = std::sqrt(dx * dx + dy * dy);
    const bool rolling = settings.tool == Tool::Roll && cursor.pressed;

    if (settings.tool == Tool::Roll && was_pressed_ && !cursor.pressed && rolling_mass_ >= 4.0f) {
        if (balls_.size() == kMaxSnowballs) balls_.erase(balls_.begin());
        Ball ball{rolling_surface_id_, 0.0f,
                  clampf(std::sqrt(rolling_mass_) * 1.5f, 5.0f, 19.0f), 1};
        if (const auto* target = find_surface(ball.id)) {
            ball.offset = clampf(previous_cursor_.x - target->rect.x, 0, target->rect.w);
            bool stacked = false;
            for (auto& b : balls_) {
                if (b.id == ball.id && std::abs(b.offset - ball.offset) < b.radius * 1.3f && b.stack < 3) {
                    b.radius = std::max(b.radius, ball.radius);
                    ++b.stack;
                    stacked = true;
                    break;
                }
            }
            if (!stacked) balls_.push_back(ball);
        }
        rolling_mass_ = 0.0f;
    }
    if (settings.tool != Tool::Roll) rolling_mass_ = 0.0f;
    if (distance > 0.2f && distance < 240.0f && (!rolling || cursor.pressed)) {
        const int samples = std::min(16, std::max(1, static_cast<int>(distance / 9.0f)));
        for (int step = 0; step <= samples; ++step) {
            const float t = static_cast<float>(step) / samples;
            const float x = previous_cursor_.x + dx * t;
            const float y = previous_cursor_.y + dy * t;
            if (x < viewport_.x || x >= viewport_.right() || y < viewport_.y - 35 ||
                y > viewport_.bottom() + 3) continue;
            const std::size_t col = std::min(columns_.size() - 1,
                static_cast<std::size_t>((x - viewport_.x) / kColumnWidth));
            for (std::size_t si : columns_[col]) {
                auto& s = surfaces_[si];
                if (x < s.rect.x || x >= s.rect.right()) continue;
                const auto index = bin_of(s, x);
                if (!s.visible[index]) continue;
                const float h = s.heights[index];
                if (h <= 0.0f || std::abs(y - (s.rect.y - h * 0.45f)) > 23.0f) continue;
                const float removed = std::min(h, rolling ? 1.7f : 3.7f);
                s.heights[index] -= removed;
                if (rolling) {
                    rolling_mass_ = std::min(180.0f, rolling_mass_ + removed);
                    rolling_surface_id_ = s.id;
                }
                ++geometry_version_;
                break;
            }
        }
    }
    previous_cursor_ = cursor;
    was_pressed_ = cursor.pressed;
}

void World::step(float delta_seconds, const Settings& settings, Cursor cursor) {
    if (settings.paused || viewport_.w <= 0 || viewport_.h <= 0) {
        previous_cursor_ = cursor;
        return;
    }
    // Prevent huge physics jumps after sleep, display changes, or the debugger.
    const float dt = clampf(delta_seconds, 0.0f, 0.05f);
    const float intensity = settings.intensity == Intensity::Light ? 0.40f :
                            settings.intensity == Intensity::Blizzard ? 2.0f : 1.0f;
    const std::size_t target = std::min(kMaxParticles,
        static_cast<std::size_t>(viewport_.w * viewport_.h / 1800.0f * intensity + 25));
    const float spawn_per_second = (13.0f + viewport_.w * 0.032f) * intensity;
    spawn_budget_ = std::min(8.0f, spawn_budget_ + dt * spawn_per_second);
    while (spawn_budget_ >= 1.0f && flakes_.size() < target) {
        spawn_one();
        spawn_budget_ -= 1.0f;
    }
    if (flakes_.size() >= target) spawn_budget_ = std::min(spawn_budget_, 1.0f);
    if (flakes_.size() > target) flakes_.resize(target);
    const float wind = clampf(settings.wind, -70.0f, 70.0f);
    std::size_t alive = 0;
    for (std::size_t i = 0; i < flakes_.size(); ++i) {
        Particle p = flakes_[i];
        const float old_y = p.y;
        p.x += (p.vx + wind) * dt;
        p.y += p.vy * dt * (settings.intensity == Intensity::Blizzard ? 1.3f : 1.0f);
        if (p.x < viewport_.x - 15 || p.x >= viewport_.right() + 15 ||
            p.y > viewport_.bottom() + 30) continue;
        if (p.x >= viewport_.x && p.x < viewport_.right() &&
            !columns_.empty()) {
            const std::size_t col = std::min(columns_.size() - 1,
                static_cast<std::size_t>((p.x - viewport_.x) / kColumnWidth));
            float best = std::numeric_limits<float>::infinity();
            std::size_t best_surface = surfaces_.size();
            for (std::size_t si : columns_[col]) {
                const auto& s = surfaces_[si];
                if (p.x < s.rect.x || p.x >= s.rect.right()) continue;
                const std::size_t b = bin_of(s, p.x);
                if (!s.visible[b]) continue;
                const float y = s.rect.y - s.heights[b];
                if (old_y <= y && p.y >= y && y < best) {
                    best = y;
                    best_surface = si;
                }
            }
            if (best_surface < surfaces_.size()) {
                auto& s = surfaces_[best_surface];
                const std::size_t bin = bin_of(s, p.x);
                const float next = std::min(kMaxPileHeight,
                    s.heights[bin] + p.radius * (s.dock ? 0.95f : 0.7f));
                if (next != s.heights[bin]) {
                    s.heights[bin] = next;
                    ++geometry_version_;
                }
                continue;
            }
        }
        flakes_[alive++] = p;
    }
    flakes_.resize(alive);
    interact(cursor, settings);
}

void World::clear() {
    flakes_.clear();
    clear_piles();
}
void World::clear_piles() {
    for (auto& s : surfaces_) std::fill(s.heights.begin(), s.heights.end(), 0.0f);
    balls_.clear();
    rolling_mass_ = 0.0f;
    ++geometry_version_;
}

float World::total_snow(std::uint64_t surface_id) const {
    const Surface* s = find_surface(surface_id);
    if (!s) return 0.0f;
    return std::accumulate(s->heights.begin(), s->heights.end(), 0.0f);
}

Stats World::stats() const {
    Stats s{flakes_.size(), surfaces_.size(), 0, balls_.size()};
    for (const auto& surface : surfaces_) s.snow_bins += surface.heights.size();
    return s;
}

bool World::is_borderless_fullscreen(Rect screen, Rect window, bool has_standard_frame) {
    if (has_standard_frame) return false;
    return std::abs(screen.x - window.x) < 3.0f &&
           std::abs(screen.y - window.y) < 3.0f &&
           std::abs(screen.w - window.w) < 3.0f &&
           std::abs(screen.h - window.h) < 3.0f;
}

void World::build_vertices(std::vector<Vertex>& points, std::vector<Vertex>& triangles) const {
    build_point_vertices(points);
    build_pile_vertices(triangles);
}

void World::build_point_vertices(std::vector<Vertex>& points) const {

    points.clear();
    points.reserve(flakes_.size() + balls_.size() * 9);
    for (const Particle& p : flakes_) {
        add_circle(points, p.x - viewport_.x, p.y - viewport_.y,
                   p.radius, 0.91f, 0.97f, 1.0f, p.alpha);
    }
    for (const auto& b : balls_) {
        const Surface* s = find_surface(b.id);
        if (!s) continue;
        const float world_x = s->rect.x + b.offset;
        if (world_x < viewport_.x - 50 || world_x > viewport_.right() + 50) continue;
        const float x = world_x - viewport_.x;
        const float base = s->rect.y - viewport_.y;
        const float y = base - b.radius;
        add_circle(points, x, y, b.radius, .94f,.98f,1.0f,1);
        if (b.stack >= 2) {
            add_circle(points, x, y - b.radius * 1.62f, b.radius * .72f, .96f,.99f,1,1);
            add_circle(points, x - b.radius * .22f, y - b.radius * 1.75f,
                       std::max(1.1f,b.radius * .07f), .09f,.16f,.22f,1);
            add_circle(points, x + b.radius * .22f, y - b.radius * 1.75f,
                       std::max(1.1f,b.radius * .07f), .09f,.16f,.22f,1);
        }
        if (b.stack >= 3) {
            add_circle(points, x, y - b.radius * 2.75f, b.radius * .5f, .96f,.99f,1,1);
        }
    }
}

void World::build_pile_vertices(std::vector<Vertex>& triangles) const {
    triangles.clear();
    for (const Surface& s : surfaces_) {
        if (s.heights.empty() || s.rect.right() < viewport_.x || s.rect.x > viewport_.right()) continue;
        for (std::size_t i = 0; i < s.heights.size(); ++i) {
            const float h = s.heights[i];
            if (h < 0.12f || !s.visible[i]) continue;
            const float x0 = std::max(s.rect.x + i * kBinWidth, viewport_.x);
            const float x1 = std::min(s.rect.x + (i + 1) * kBinWidth, std::min(s.rect.right(), viewport_.right()));
            if (x1 <= x0) continue;
            const float top = s.rect.y - h;
            const float base = s.rect.y + 1.2f;
            const float local_top = std::max(top, viewport_.y);
            if (local_top >= viewport_.bottom()) continue;
            const Vertex a{x0 - viewport_.x, local_top - viewport_.y, .96f,.98f,1.0f,.96f,1};
            const Vertex b{x1 - viewport_.x, local_top - viewport_.y, .96f,.98f,1.0f,.96f,1};
            const Vertex c{x0 - viewport_.x, base - viewport_.y, .77f,.88f,.97f,.72f,1};
            const Vertex d{x1 - viewport_.x, base - viewport_.y, .77f,.88f,.97f,.72f,1};
            add_triangle(triangles, a, c, b);
            add_triangle(triangles, b, c, d);
        }
    }

}

} // namespace snow
