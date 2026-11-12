#pragma once
#include "engine.hpp"
#include <cstdint>
#include <memory>
#include <vector>

struct GLFWwindow;

// One small OpenGL 3.3 renderer per display; no textures or external GL loader.
class Renderer {
public:
    explicit Renderer(GLFWwindow* window);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    void draw(const snow::World& world);
    void clear();
private:
    struct Impl;
    GLFWwindow* window_;
    std::unique_ptr<Impl> impl_;
    std::vector<snow::Vertex> points_;
    std::vector<snow::Vertex> triangles_;
    std::uint64_t cached_geometry_version_ = 0;
    void frame_begin();
};
