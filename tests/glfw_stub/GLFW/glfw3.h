#pragma once
#ifdef __cplusplus
extern "C" {
#endif
struct GLFWwindow;
struct GLFWmonitor;
typedef struct GLFWwindow GLFWwindow;
typedef struct GLFWmonitor GLFWmonitor;
typedef void (*GLFWglproc)(void);
typedef void (*GLFWerrorfun)(int,const char*);
typedef void (*GLFWmonitorfun)(GLFWmonitor*,int);
typedef struct GLFWvidmode { int width,height,redBits,greenBits,blueBits,refreshRate; } GLFWvidmode;
#define GLFW_TRUE 1
#define GLFW_FALSE 0
#define GLFW_CONTEXT_VERSION_MAJOR 0x00022002
#define GLFW_CONTEXT_VERSION_MINOR 0x00022003
#define GLFW_OPENGL_PROFILE 0x00022008
#define GLFW_OPENGL_CORE_PROFILE 0x00032001
#define GLFW_OPENGL_FORWARD_COMPAT 0x00022006
#define GLFW_DECORATED 0x00020005
#define GLFW_FLOATING 0x00020007
#define GLFW_RESIZABLE 0x00020003
#define GLFW_VISIBLE 0x00020004
#define GLFW_FOCUS_ON_SHOW 0x0002000C
#define GLFW_FOCUSED 0x00020001
#define GLFW_TRANSPARENT_FRAMEBUFFER 0x0002000A
#define GLFW_MOUSE_PASSTHROUGH 0x0002000D
GLFWglproc glfwGetProcAddress(const char*);
void glfwGetFramebufferSize(GLFWwindow*,int*,int*);
void glfwGetWindowSize(GLFWwindow*,int*,int*);
void glfwSetErrorCallback(GLFWerrorfun);
int glfwInit(void);
void glfwTerminate(void);
void glfwSetMonitorCallback(GLFWmonitorfun);
GLFWmonitor** glfwGetMonitors(int*);
const GLFWvidmode* glfwGetVideoMode(GLFWmonitor*);
void glfwGetMonitorPos(GLFWmonitor*,int*,int*);
void glfwMakeContextCurrent(GLFWwindow*);
void glfwDestroyWindow(GLFWwindow*);
void glfwDefaultWindowHints(void);
void glfwWindowHint(int,int);
GLFWwindow* glfwCreateWindow(int,int,const char*,GLFWmonitor*,GLFWwindow*);
void glfwSetWindowPos(GLFWwindow*,int,int);
int glfwGetWindowAttrib(GLFWwindow*,int);
void glfwSwapInterval(int);
void glfwShowWindow(GLFWwindow*);
void glfwSwapBuffers(GLFWwindow*);
void glfwWaitEventsTimeout(double);
void glfwPollEvents(void);
#ifdef __cplusplus
}
#endif
