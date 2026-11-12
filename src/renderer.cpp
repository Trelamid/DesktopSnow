#include "renderer.hpp"
#include <GLFW/glfw3.h>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
  #define SNOW_GL_CALL __stdcall
#else
  #define SNOW_GL_CALL
#endif

namespace {
using U32 = unsigned int;
using I32 = int;
using Size = std::ptrdiff_t;
using Char = char;
using Byte = unsigned char;
constexpr U32 VERTEX_SHADER = 0x8B31, FRAGMENT_SHADER = 0x8B30;
constexpr U32 COMPILE_STATUS = 0x8B81, LINK_STATUS = 0x8B82;
constexpr U32 ARRAY_BUFFER = 0x8892, STREAM_DRAW = 0x88E0;
constexpr U32 FLOAT = 0x1406, TRIANGLES = 0x0004, POINTS = 0x0000;
constexpr U32 BLEND = 0x0BE2, DEPTH_TEST = 0x0B71, PROGRAM_POINT_SIZE = 0x8642;
constexpr U32 SRC_ALPHA = 0x0302, ONE_MINUS_SRC_ALPHA = 0x0303, COLOR_BUFFER_BIT = 0x00004000;

static const char* shader_vertex = R"GLSL(#version 330 core
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec4 aColor;
layout(location = 2) in float aSize;
uniform vec2 uViewport;
uniform float uPixelScale;
out vec4 vColor;
void main() {
    vec2 p = aPosition / uViewport * vec2(2.0, -2.0) + vec2(-1.0, 1.0);
    gl_Position = vec4(p, 0.0, 1.0);
    gl_PointSize = aSize * uPixelScale;
    vColor = aColor;
})GLSL";

static const char* shader_fragment = R"GLSL(#version 330 core
in vec4 vColor;
out vec4 FragColor;
uniform int uPointPass;
void main() {
    if (uPointPass == 1) {
        vec2 p = gl_PointCoord * 2.0 - 1.0;
        float distanceToCenter = length(p);
        float edge = 1.0 - smoothstep(0.72, 1.0, distanceToCenter);
        float lighting = 1.0 - 0.13 * gl_PointCoord.y;
        FragColor = vec4(vColor.rgb * lighting, vColor.a * edge);
    } else {
        FragColor = vColor;
    }
})GLSL";

template <typename T> T gl_load(const char* name) {
    const auto proc = glfwGetProcAddress(name);
    if (!proc) throw std::runtime_error(std::string("Missing OpenGL function: ") + name);
    return reinterpret_cast<T>(proc);
}
} // namespace

struct Renderer::Impl {
    using fnCreateShader = U32 (SNOW_GL_CALL *)(U32);
    using fnShaderSource = void (SNOW_GL_CALL *)(U32, I32, const Char* const*, const I32*);
    using fnCompileShader = void (SNOW_GL_CALL *)(U32);
    using fnGetShaderiv = void (SNOW_GL_CALL *)(U32,U32,I32*);
    using fnGetShaderInfoLog = void (SNOW_GL_CALL *)(U32,I32,I32*,Char*);
    using fnDeleteShader = void (SNOW_GL_CALL *)(U32);
    using fnCreateProgram = U32 (SNOW_GL_CALL *)();
    using fnAttachShader = void (SNOW_GL_CALL *)(U32,U32);
    using fnLinkProgram = void (SNOW_GL_CALL *)(U32);
    using fnGetProgramiv = void (SNOW_GL_CALL *)(U32,U32,I32*);
    using fnGetProgramInfoLog = void (SNOW_GL_CALL *)(U32,I32,I32*,Char*);
    using fnUseProgram = void (SNOW_GL_CALL *)(U32);
    using fnDeleteProgram = void (SNOW_GL_CALL *)(U32);
    using fnGetUniformLocation = I32 (SNOW_GL_CALL *)(U32,const Char*);
    using fnUniform2f = void (SNOW_GL_CALL *)(I32,float,float);
    using fnUniform1f = void (SNOW_GL_CALL *)(I32,float);
    using fnUniform1i = void (SNOW_GL_CALL *)(I32,I32);
    using fnGenVertexArrays = void (SNOW_GL_CALL *)(I32,U32*);
    using fnBindVertexArray = void (SNOW_GL_CALL *)(U32);
    using fnDeleteVertexArrays = void (SNOW_GL_CALL *)(I32,const U32*);
    using fnGenBuffers = void (SNOW_GL_CALL *)(I32,U32*);
    using fnBindBuffer = void (SNOW_GL_CALL *)(U32,U32);
    using fnBufferData = void (SNOW_GL_CALL *)(U32,Size,const void*,U32);
    using fnDeleteBuffers = void (SNOW_GL_CALL *)(I32,const U32*);
    using fnEnableVertexAttribArray = void (SNOW_GL_CALL *)(U32);
    using fnVertexAttribPointer = void (SNOW_GL_CALL *)(U32,I32,U32,Byte,I32,const void*);
    using fnDrawArrays = void (SNOW_GL_CALL *)(U32,I32,I32);
    using fnEnable = void (SNOW_GL_CALL *)(U32);
    using fnDisable = void (SNOW_GL_CALL *)(U32);
    using fnBlendFuncSeparate = void (SNOW_GL_CALL *)(U32,U32,U32,U32);
    using fnViewport = void (SNOW_GL_CALL *)(I32,I32,I32,I32);
    using fnClearColor = void (SNOW_GL_CALL *)(float,float,float,float);
    using fnClear = void (SNOW_GL_CALL *)(U32);

#define GL_FIELD(TYPE, NAME) TYPE NAME = gl_load<TYPE>("gl" #NAME)
    GL_FIELD(fnCreateShader, CreateShader);
    GL_FIELD(fnShaderSource, ShaderSource);
    GL_FIELD(fnCompileShader, CompileShader);
    GL_FIELD(fnGetShaderiv, GetShaderiv);
    GL_FIELD(fnGetShaderInfoLog, GetShaderInfoLog);
    GL_FIELD(fnDeleteShader, DeleteShader);
    GL_FIELD(fnCreateProgram, CreateProgram);
    GL_FIELD(fnAttachShader, AttachShader);
    GL_FIELD(fnLinkProgram, LinkProgram);
    GL_FIELD(fnGetProgramiv, GetProgramiv);
    GL_FIELD(fnGetProgramInfoLog, GetProgramInfoLog);
    GL_FIELD(fnUseProgram, UseProgram);
    GL_FIELD(fnDeleteProgram, DeleteProgram);
    GL_FIELD(fnGetUniformLocation, GetUniformLocation);
    GL_FIELD(fnUniform2f, Uniform2f);
    GL_FIELD(fnUniform1f, Uniform1f);
    GL_FIELD(fnUniform1i, Uniform1i);
    GL_FIELD(fnGenVertexArrays, GenVertexArrays);
    GL_FIELD(fnBindVertexArray, BindVertexArray);
    GL_FIELD(fnDeleteVertexArrays, DeleteVertexArrays);
    GL_FIELD(fnGenBuffers, GenBuffers);
    GL_FIELD(fnBindBuffer, BindBuffer);
    GL_FIELD(fnBufferData, BufferData);
    GL_FIELD(fnDeleteBuffers, DeleteBuffers);
    GL_FIELD(fnEnableVertexAttribArray, EnableVertexAttribArray);
    GL_FIELD(fnVertexAttribPointer, VertexAttribPointer);
    GL_FIELD(fnDrawArrays, DrawArrays);
    GL_FIELD(fnEnable, Enable);
    GL_FIELD(fnDisable, Disable);
    GL_FIELD(fnBlendFuncSeparate, BlendFuncSeparate);
    GL_FIELD(fnViewport, Viewport);
    GL_FIELD(fnClearColor, ClearColor);
    GL_FIELD(fnClear, Clear);
#undef GL_FIELD

    U32 program = 0, vao = 0, points_vbo = 0, piles_vbo = 0;
    I32 viewport_location = -1, scale_location = -1, point_location = -1;

    U32 compile(U32 type, const char* source) {
        const U32 shader = CreateShader(type);
        ShaderSource(shader, 1, &source, nullptr);
        CompileShader(shader);
        I32 okay = 0;
        GetShaderiv(shader, COMPILE_STATUS, &okay);
        if (!okay) {
            Char log[4096]{};
            GetShaderInfoLog(shader, 4095, nullptr, log);
            DeleteShader(shader);
            throw std::runtime_error(std::string("Shader compilation failed: ") + log);
        }
        return shader;
    }
    Impl() {
        const U32 vertex = compile(VERTEX_SHADER, shader_vertex);
        const U32 fragment = compile(FRAGMENT_SHADER, shader_fragment);
        program = CreateProgram();
        AttachShader(program, vertex);
        AttachShader(program, fragment);
        LinkProgram(program);
        DeleteShader(vertex);
        DeleteShader(fragment);
        I32 okay = 0;
        GetProgramiv(program, LINK_STATUS, &okay);
        if (!okay) {
            Char log[4096]{};
            GetProgramInfoLog(program, 4095, nullptr, log);
            throw std::runtime_error(std::string("Shader link failed: ") + log);
        }
        viewport_location = GetUniformLocation(program, "uViewport");
        scale_location = GetUniformLocation(program, "uPixelScale");
        point_location = GetUniformLocation(program, "uPointPass");
        GenVertexArrays(1, &vao);
        GenBuffers(1, &points_vbo);
        GenBuffers(1, &piles_vbo);
        Enable(BLEND);
        BlendFuncSeparate(SRC_ALPHA, ONE_MINUS_SRC_ALPHA, 1 /* GL_ONE */, ONE_MINUS_SRC_ALPHA);
        Disable(DEPTH_TEST);
        Enable(PROGRAM_POINT_SIZE);
    }
    ~Impl() {
        if (program) DeleteProgram(program);
        if (points_vbo) DeleteBuffers(1, &points_vbo);
        if (piles_vbo) DeleteBuffers(1, &piles_vbo);
        if (vao) DeleteVertexArrays(1, &vao);
    }
    void set_buffer(U32 vbo, const std::vector<snow::Vertex>& data) {
        BindVertexArray(vao);
        BindBuffer(ARRAY_BUFFER, vbo);
        BufferData(ARRAY_BUFFER, static_cast<Size>(data.size() * sizeof(snow::Vertex)),
                   data.empty() ? nullptr : data.data(), STREAM_DRAW);
        EnableVertexAttribArray(0);
        EnableVertexAttribArray(1);
        EnableVertexAttribArray(2);
        VertexAttribPointer(0, 2, FLOAT, 0, sizeof(snow::Vertex),
                            reinterpret_cast<const void*>(offsetof(snow::Vertex, x)));
        VertexAttribPointer(1, 4, FLOAT, 0, sizeof(snow::Vertex),
                            reinterpret_cast<const void*>(offsetof(snow::Vertex, r)));
        VertexAttribPointer(2, 1, FLOAT, 0, sizeof(snow::Vertex),
                            reinterpret_cast<const void*>(offsetof(snow::Vertex, size)));
    }
};

Renderer::Renderer(GLFWwindow* window) : window_(window), impl_(std::make_unique<Impl>()) {}
Renderer::~Renderer() = default;

void Renderer::frame_begin() {
    int framebuffer_w = 0, framebuffer_h = 0;
    glfwGetFramebufferSize(window_, &framebuffer_w, &framebuffer_h);
    impl_->Viewport(0, 0, framebuffer_w, framebuffer_h);
    impl_->ClearColor(0, 0, 0, 0);
    impl_->Clear(COLOR_BUFFER_BIT);
    int window_w = 0, window_h = 0;
    glfwGetWindowSize(window_, &window_w, &window_h);
    impl_->UseProgram(impl_->program);
    impl_->Uniform2f(impl_->viewport_location, static_cast<float>(window_w), static_cast<float>(window_h));
    impl_->Uniform1f(impl_->scale_location, window_w ? static_cast<float>(framebuffer_w) / window_w : 1.0f);
}

void Renderer::draw(const snow::World& world) {
    frame_begin();
    world.build_point_vertices(points_);
    if (world.geometry_version() != cached_geometry_version_) {
        world.build_pile_vertices(triangles_);
        impl_->set_buffer(impl_->piles_vbo, triangles_);
        cached_geometry_version_ = world.geometry_version();
    }
    impl_->Uniform1i(impl_->point_location, 0);
    impl_->BindVertexArray(impl_->vao);
    impl_->BindBuffer(ARRAY_BUFFER, impl_->piles_vbo);
    // Attribute pointers are VAO state and must follow the active VBO.
    // No upload is performed here when the snow geometry is unchanged.
    impl_->VertexAttribPointer(0, 2, FLOAT, 0, sizeof(snow::Vertex), reinterpret_cast<const void*>(offsetof(snow::Vertex,x)));
    impl_->VertexAttribPointer(1, 4, FLOAT, 0, sizeof(snow::Vertex), reinterpret_cast<const void*>(offsetof(snow::Vertex,r)));
    impl_->VertexAttribPointer(2, 1, FLOAT, 0, sizeof(snow::Vertex), reinterpret_cast<const void*>(offsetof(snow::Vertex,size)));
    if (!triangles_.empty()) impl_->DrawArrays(TRIANGLES, 0, static_cast<I32>(triangles_.size()));
    impl_->set_buffer(impl_->points_vbo, points_);
    impl_->Uniform1i(impl_->point_location, 1);
    if (!points_.empty()) impl_->DrawArrays(POINTS, 0, static_cast<I32>(points_.size()));
}

void Renderer::clear() { frame_begin(); }
