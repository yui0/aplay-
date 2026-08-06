#ifndef SCREENSHOT_GLFW3_H
#define SCREENSHOT_GLFW3_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef void GLvoid;
typedef signed char GLbyte;
typedef short GLshort;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef double GLdouble;
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_FAN 0x0006
#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_DST_COLOR 0x0306
#define GL_ONE_MINUS_DST_COLOR 0x0307
#define GL_ONE 1
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_FLOAT 0x1406
#define GL_UNSIGNED_BYTE 0x1401
#define GL_RED 0x1903
#define GL_RGBA 0x1908
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_LINEAR 0x2601
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5

typedef struct GLFWwindow GLFWwindow;
typedef struct GLFWmonitor GLFWmonitor;
typedef struct GLFWcursor GLFWcursor;
typedef void (*GLFWerrorfun)(int,const char*);
typedef void (*GLFWwindowposfun)(GLFWwindow*,int,int);
typedef void (*GLFWwindowsizefun)(GLFWwindow*,int,int);
typedef void (*GLFWframebuffersizefun)(GLFWwindow*,int,int);
typedef void (*GLFWmousebuttonfun)(GLFWwindow*,int,int,int);
typedef void (*GLFWcursorposfun)(GLFWwindow*,double,double);
typedef void (*GLFWscrollfun)(GLFWwindow*,double,double);
typedef void (*GLFWkeyfun)(GLFWwindow*,int,int,int,int);
typedef void (*GLFWcharfun)(GLFWwindow*,unsigned int);
typedef void (*GLFWglproc)(void);

#define GLFW_TRUE 1
#define GLFW_FALSE 0
#define GLFW_DONT_CARE -1
#define GLFW_PRESS 1
#define GLFW_RELEASE 0
#define GLFW_REPEAT 2
#define GLFW_MOUSE_BUTTON_LEFT 0
#define GLFW_MOUSE_BUTTON_RIGHT 1
#define GLFW_MOD_SHIFT 0x0001
#define GLFW_VISIBLE 0x00020004
#define GLFW_RESIZABLE 0x00020003
#define GLFW_DECORATED 0x00020005
#define GLFW_FLOATING 0x00020007
#define GLFW_FOCUS_ON_SHOW 0x0002000C
#define GLFW_CONTEXT_VERSION_MAJOR 0x00022002
#define GLFW_CONTEXT_VERSION_MINOR 0x00022003
#define GLFW_OPENGL_FORWARD_COMPAT 0x00022006
#define GLFW_OPENGL_PROFILE 0x00022008
#define GLFW_OPENGL_CORE_PROFILE 0x00032001
#define GLFW_IBEAM_CURSOR 0x00036002
#define GLFW_CROSSHAIR_CURSOR 0x00036003
#define GLFW_HAND_CURSOR 0x00036004
#define GLFW_HRESIZE_CURSOR 0x00036005
#define GLFW_VRESIZE_CURSOR 0x00036006
#define GLFW_RESIZE_NWSE_CURSOR 0x00036008
#define GLFW_KEY_SPACE 32
#define GLFW_KEY_EQUAL 61
#define GLFW_KEY_B 66
#define GLFW_KEY_C 67
#define GLFW_KEY_D 68
#define GLFW_KEY_E 69
#define GLFW_KEY_F 70
#define GLFW_KEY_L 76
#define GLFW_KEY_N 78
#define GLFW_KEY_Q 81
#define GLFW_KEY_S 83
#define GLFW_KEY_T 84
#define GLFW_KEY_ESCAPE 256
#define GLFW_KEY_ENTER 257
#define GLFW_KEY_TAB 258
#define GLFW_KEY_RIGHT 262
#define GLFW_KEY_LEFT 263
#define GLFW_KEY_DOWN 264
#define GLFW_KEY_UP 265
#define GLFW_KEY_PAGE_UP 266
#define GLFW_KEY_PAGE_DOWN 267
#define GLFW_KEY_HOME 268
#define GLFW_KEY_END 269
#define GLFW_KEY_KP_SUBTRACT 333
#define GLFW_KEY_KP_ADD 334
#define GLFW_KEY_KP_ENTER 335
#define GLFW_KEY_MINUS 45

int glfwInit(void);
void glfwTerminate(void);
GLFWerrorfun glfwSetErrorCallback(GLFWerrorfun cbfun);
void glfwWindowHint(int hint, int value);
GLFWwindow* glfwCreateWindow(int width,int height,const char* title,GLFWmonitor* monitor,GLFWwindow* share);
void glfwDestroyWindow(GLFWwindow* window);
void glfwMakeContextCurrent(GLFWwindow* window);
GLFWwindow* glfwGetCurrentContext(void);
void glfwSwapBuffers(GLFWwindow* window);
void glfwSwapInterval(int interval);
void glfwWaitEventsTimeout(double timeout);
double glfwGetTime(void);
int glfwWindowShouldClose(GLFWwindow* window);
void glfwSetWindowShouldClose(GLFWwindow* window,int value);
void glfwShowWindow(GLFWwindow* window);
void glfwHideWindow(GLFWwindow* window);
void glfwFocusWindow(GLFWwindow* window);
void glfwGetWindowSize(GLFWwindow* window,int* width,int* height);
void glfwSetWindowSize(GLFWwindow* window,int width,int height);
void glfwGetFramebufferSize(GLFWwindow* window,int* width,int* height);
void glfwGetWindowPos(GLFWwindow* window,int* xpos,int* ypos);
void glfwSetWindowPos(GLFWwindow* window,int xpos,int ypos);
void glfwSetWindowSizeLimits(GLFWwindow* window,int minw,int minh,int maxw,int maxh);
void glfwSetWindowAspectRatio(GLFWwindow* window,int numer,int denom);
void glfwSetWindowAttrib(GLFWwindow* window,int attrib,int value);
void glfwSetWindowUserPointer(GLFWwindow* window,void* pointer);
void* glfwGetWindowUserPointer(GLFWwindow* window);
void glfwGetCursorPos(GLFWwindow* window,double* xpos,double* ypos);
void glfwSetCursor(GLFWwindow* window,GLFWcursor* cursor);
GLFWcursor* glfwCreateStandardCursor(int shape);
void glfwDestroyCursor(GLFWcursor* cursor);
GLFWframebuffersizefun glfwSetFramebufferSizeCallback(GLFWwindow* window,GLFWframebuffersizefun cbfun);
GLFWwindowsizefun glfwSetWindowSizeCallback(GLFWwindow* window,GLFWwindowsizefun cbfun);
GLFWcursorposfun glfwSetCursorPosCallback(GLFWwindow* window,GLFWcursorposfun cbfun);
GLFWmousebuttonfun glfwSetMouseButtonCallback(GLFWwindow* window,GLFWmousebuttonfun cbfun);
GLFWscrollfun glfwSetScrollCallback(GLFWwindow* window,GLFWscrollfun cbfun);
GLFWkeyfun glfwSetKeyCallback(GLFWwindow* window,GLFWkeyfun cbfun);
GLFWcharfun glfwSetCharCallback(GLFWwindow* window,GLFWcharfun cbfun);
GLFWmonitor** glfwGetMonitors(int* count);
void glfwGetMonitorWorkarea(GLFWmonitor* monitor,int* xpos,int* ypos,int* width,int* height);
GLFWglproc glfwGetProcAddress(const char* procname);

/* Screenshot-only extension used by the harness. */
void screenshot_glfw_set_output(const char* path, int background_variant);
void screenshot_glfw_finish_after_frames(int frames);
#ifdef __cplusplus
}
#endif
#endif
