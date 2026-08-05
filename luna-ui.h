/*
 * luna-ui.h — Luna UI: CSS/HTML layout + OpenGL renderer (single-file library)
 *
 * Paint path is CSS-driven (boxes, gradients, shadows, text, caret-color).
 * Text controls: <input type="text|password"> / <textarea>
 *   Host must forward IME commits via luna_char() (GLFW: glfwSetCharCallback).
 * Japanese (and other Unicode) glyphs use a dynamic atlas over a CJK-capable font.
 *
 *   #define LUNA_UI_IMPLEMENTATION
 *   #include "luna-ui.h"
 *
 * Copyright © 2026 Yuichiro Nakada / Project Vespera — MPL 2.0
 */
#ifndef LUNA_UI_H
#define LUNA_UI_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef LUNA_UI_MAX_ELEMENTS
#define LUNA_UI_MAX_ELEMENTS 700
#endif
#ifndef LUNA_UI_MAX_RULES
#define LUNA_UI_MAX_RULES 600
#endif

typedef struct LunaElement LunaElement;
typedef struct LunaContext LunaContext;
typedef void (*LunaEventHandler)(LunaElement* e);
typedef void (*LunaTrapDismissFn)(int trap_idx);
typedef void (*LunaMouseReleaseHook)(int hit, int drag_moved);

typedef void* (*LunaGetProcFn)(const char* name);
typedef double (*LunaGetTimeFn)(void);
typedef void (*LunaSetCursorFn)(int cursor_type);
typedef void (*LunaRequestCloseFn)(void);
typedef void (*LunaIconifyFn)(void);
typedef void (*LunaMaximizeToggleFn)(void);

typedef struct LunaPlatform {
    LunaGetTimeFn         get_time;
    LunaGetProcFn         get_proc;
    LunaSetCursorFn       set_cursor;
    LunaRequestCloseFn    request_close;
    LunaIconifyFn         iconify;
    LunaMaximizeToggleFn  maximize_toggle;
} LunaPlatform;

typedef struct LunaInitConfig {
    float width, height;
    LunaGetProcFn get_proc;
    /* Remove native window decorations when using the GLFW integration.
     * The host must set g_luna_glfw_window before calling luna_init(). */
    int frameless;
} LunaInitConfig;

/*
 * Host-neutral input constants. Values intentionally match GLFW so existing
 * GLFW hosts remain source-compatible while Wayland/EGL hosts need no GLFW
 * headers or library.
 */
enum {
    LUNA_RELEASE = 0,
    LUNA_PRESS = 1,
    LUNA_REPEAT = 2,
    LUNA_MOUSE_BUTTON_LEFT = 0,
    LUNA_MOUSE_BUTTON_RIGHT = 1,
    LUNA_MOUSE_BUTTON_MIDDLE = 2,
    LUNA_MOD_SHIFT = 0x0001,
    LUNA_MOD_CONTROL = 0x0002,
    LUNA_MOD_ALT = 0x0004,
    LUNA_MOD_SUPER = 0x0008,
    LUNA_KEY_SPACE = 32,
    LUNA_KEY_ESCAPE = 256,
    LUNA_KEY_ENTER = 257,
    LUNA_KEY_TAB = 258,
    LUNA_KEY_BACKSPACE = 259,
    LUNA_KEY_DELETE = 261,
    LUNA_KEY_RIGHT = 262,
    LUNA_KEY_LEFT = 263,
    LUNA_KEY_DOWN = 264,
    LUNA_KEY_UP = 265,
    LUNA_KEY_PAGE_UP = 266,
    LUNA_KEY_PAGE_DOWN = 267,
    LUNA_KEY_HOME = 268,
    LUNA_KEY_END = 269,
    LUNA_KEY_F12 = 301,
    LUNA_KEY_KP_ENTER = 335
};

void luna_set_platform(const LunaPlatform* p);
int  luna_init(const LunaInitConfig* cfg);
void luna_shutdown(void);
void luna_set_html_base_dir(const char* path);
int  luna_load_html_file(const char* path);
int  luna_load_css_file(const char* path);
/* Drop the active author stylesheet without rebuilding the DOM.  Hosts use
 * this to replace a desktop skin at runtime, then load the base sheet and the
 * selected skin in cascade order. */
void luna_reset_css(void);
void luna_parse_html(const char* html);
void luna_parse_css(const char* css);
void luna_inject_body_background(void);
void luna_wire_onclick_handlers(void);
void luna_push_focus_trap(int idx, LunaTrapDismissFn on_dismiss, int backdrop_dismiss);
void luna_pop_focus_trap(int idx);
void luna_set_mouse_release_hook(LunaMouseReleaseHook fn);
/* Button that triggered the most recent on_click (LUNA_MOUSE_BUTTON_*). */
int  luna_last_click_button(void);
/* Modifier mask (LUNA_MOD_*) at the most recent on_click. */
int  luna_last_click_mods(void);
void luna_resize(float w, float h);
void luna_mark_layout_dirty(void);
void luna_update(double now, double dt);
/* Update and return whether interactive easing still changes pixels. */
int  luna_update_settling(double now, double dt);
/* Update once and produce per-root settling bits in the same element pass. */
int  luna_update_settling_mask(double now, double dt,
                               const int* roots, int nroots, unsigned* out_mask);
/* 1 while color/scale easing still changes pixels (CSS @keyframes excluded). */
int  luna_visuals_settling(void);
/* Same, but only for root and its descendants. root_idx < 0 → whole tree. */
int  luna_visuals_settling_under(int root_idx);
/* Batched form: bit k of *out_mask = roots[k]'s subtree is settling (root -1 =
 * whole tree).  Returns 1 when anything is settling.  One pass for all roots —
 * hosts with many surfaces should prefer this over calling _under() per root. */
int  luna_visuals_settling_mask(const int* roots, int nroots, unsigned* out_mask);
/* 1 when a CSS @keyframes animation is actually running under root_idx. */
int  luna_css_anim_running_under(int root_idx);
/* Forget cached OpenGL bindings before a host/custom renderer hands control
 * back to Luna UI. Safe to call once at the start of every frame. */
void luna_invalidate_gl_state(void);
void luna_render(int fbw, int fbh);
/* Opt in to per-render damage tracking (off by default).  It costs a hash of
 * each drawn element per render, which only pays for itself on hosts that can
 * forward the result — a Wayland client posting surface damage.  A host that
 * repaints one framebuffer wholesale should leave it off. */
void luna_set_damage_tracking(int enabled);
/* Region of the most recent luna_render()/luna_render_region() whose pixels
 * differ from the previous render of the same root, in surface coordinates
 * (x right, y down, origin at the region origin passed to _region).
 * Returns 1 with the four out-params filled in, 0 when nothing changed.  Hosts
 * can hand this straight to eglSwapBuffersWithDamage / wl_surface.damage. */
int  luna_render_damage(float* x, float* y, float* w, float* h);
/* Walk the current render root and report whether a paint would change any
 * pixels, without issuing GL draws or updating the damage records.  Used by
 * hosts that mark a surface dirty on a timer (clock, stats) but often find
 * the document unchanged — skipping the GL clear/draw/swap removes the
 * periodic hitch those timers produced on the console session. */
int  luna_probe_damage(int root_idx, int fbw, int fbh,
                       float origin_x, float origin_y,
                       float region_w, float region_h);
void luna_render_region(int root_idx, int fbw, int fbh,
                        float origin_x, float origin_y,
                        float region_w, float region_h);
/* A LunaContext is a lightweight native-surface view of the shared Luna
 * document.  Each context owns the OpenGL container objects that are not
 * shared between GLFW/GLX contexts (notably the VAO), plus its DOM root and
 * input-coordinate origin.  Programs, textures, CSS and the DOM remain shared.
 *
 * The host must make the target GL context current before create/destroy/render
 * and create native contexts in the same GL share group.  Existing single-
 * surface hosts can continue using luna_render() and luna_mouse_* unchanged. */
LunaContext* luna_context_create(void);
void luna_context_destroy(LunaContext* ctx);
void luna_context_set_region(LunaContext* ctx, int root_idx,
                             float origin_x, float origin_y,
                             float region_w, float region_h);
void luna_context_render(LunaContext* ctx, int fbw, int fbh);
void luna_context_mouse_move(LunaContext* ctx, double x, double y);
void luna_context_mouse_button(LunaContext* ctx, int button, int action,
                               int mods, double x, double y);
void luna_context_scroll(LunaContext* ctx, double xoffset, double yoffset);
int  luna_element_count(void);
LunaElement* luna_element_at(int idx);
/* 1 when no display:none anywhere between idx and the root — i.e. changing
 * this element's text or style can actually change pixels. */
int  luna_element_visible(int idx);
int  luna_get_element_by_id(const char* id);
/* Currently focused DOM element, or -1. Useful for host keyboard shortcuts. */
int  luna_focused_element(void);
void luna_focus_element(int idx);
void luna_set_text(int idx, const char* text);
void luna_add_class(int idx, const char* cls);
void luna_remove_class(int idx, const char* cls);
/* Apply a whitespace-separated remove/add set and restyle only once.
 * Returns 1 when the class list changed. */
int  luna_update_classes(int idx, const char* remove_classes, const char* add_classes);
/* Mark an element whose visual target fields were changed directly by a host. */
void luna_mark_visual_dirty(int idx);
void luna_update_element_style(int idx);
void luna_register_js_handler(const char* name, LunaEventHandler fn);
void luna_set_on_click(int idx, LunaEventHandler fn);
void luna_mouse_move(double x, double y);
/* Forward pointer motion and return 1 only when hover/drag visuals changed.
 * Hosts can use this to avoid repainting the whole surface for every raw
 * mouse-motion event. */
int  luna_mouse_move_changed(double x, double y);
void luna_mouse_button(int button, int action, int mods, double x, double y);
void luna_scroll(double xoff, double yoff);
void luna_key(int key, int scancode, int action, int mods);
void luna_char(unsigned int codepoint);
const char* luna_get_value(int idx);
void luna_set_value(int idx, const char* value);
void luna_framebuffer_resized(void);
void luna_take_screenshot(const char* path);
void luna_request_screenshot(const char* path);
void luna_flush_pending_screenshot(void);

extern float luna_window_width;
extern float luna_window_height;
extern char  luna_doc_title[128];
extern int   luna_css_from_document;

#ifdef LUNA_UI_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef LUNA_UI_GLFW
#include <GLFW/glfw3.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_FAILURE_STRINGS
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#define CSS_PARSER_IMPLEMENTATION
#include "cssparser.h"

#undef MAX_ELEMENTS
#undef MAX_RULES
#define MAX_ELEMENTS LUNA_UI_MAX_ELEMENTS
#define MAX_RULES    LUNA_UI_MAX_RULES

static LunaPlatform g_luna_platform;
static double g_luna_mx = 0.0, g_luna_my = 0.0;
static int g_luna_shift = 0;
static int g_luna_fbw = 0, g_luna_fbh = 0;
#ifdef LUNA_UI_GLFW
void* g_luna_glfw_window = NULL;
#endif

static double luna_now(void) {
    if (g_luna_platform.get_time) return g_luna_platform.get_time();
#ifdef LUNA_UI_GLFW
    return glfwGetTime();
#else
    return 0.0;
#endif
}

void luna_set_platform(const LunaPlatform* p) {
    if (p) g_luna_platform = *p;
    else memset(&g_luna_platform, 0, sizeof(g_luna_platform));
}

float luna_window_width = 1024.0f;
float luna_window_height = 768.0f;

/* Region-render state: set by luna_render_region(), cleared after each call.
 * When g_render_res_x==0 the full document / window dims are used (default). */
static int   g_render_root  = -1;
static float g_render_off_x = 0.0f;
static float g_render_off_y = 0.0f;
static float g_render_res_x = 0.0f;
static float g_render_res_y = 0.0f;
/* A successful damage probe already built render order + resolved ancestor
 * state for one exact region. The immediately following render can consume
 * that work instead of rebuilding the same O(elements) cache. */
static int   g_probe_prepared = 0;
static int   g_probe_root = -1, g_probe_fbw = 0, g_probe_fbh = 0;
static float g_probe_ox = 0.0f, g_probe_oy = 0.0f;
static float g_probe_rw = 0.0f, g_probe_rh = 0.0f;
#define LUNA_RRES_X (g_render_res_x > 0.0f ? g_render_res_x : window_width)
#define LUNA_RRES_Y (g_render_res_y > 0.0f ? g_render_res_y : window_height)
char  luna_doc_title[128] = "Luna UI";
int   luna_css_from_document = 0;

#define window_width  luna_window_width
#define window_height luna_window_height
#define g_doc_title   luna_doc_title
#define g_css_from_document luna_css_from_document

typedef void (*PFNGLCREATEVERTEXARRAYSPROC)(GLsizei n, GLuint* arrays);
typedef void (*PFNGLDELETEVERTEXARRAYSPROC)(GLsizei n, const GLuint* arrays);
typedef void (*PFNGLGENBUFFERSPROC)(GLsizei n, GLuint* buffers);
typedef void (*PFNGLBINDBUFFERPROC)(GLenum target, GLuint buffer);
typedef void (*PFNGLBUFFERDATAPROC)(GLenum target, GLsizeiptr size, const void* data, GLenum usage);
typedef void (*PFNGLBUFFERSUBDATAPROC)(GLenum target, GLintptr offset, GLsizeiptr size, const void* data);
typedef void (*PFNGLVERTEXATTRIBPOINTERPROC)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer);
typedef void (*PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint index);
typedef void (*PFNGLDELETEBUFFERSPROC)(GLsizei n, const GLuint* buffers);
typedef void (*PFNGLBINDVERTEXARRAYPROC)(GLuint array);
typedef GLuint (*PFNGLCREATESHADERPROC)(GLenum type);
typedef void (*PFNGLSHADERSOURCEPROC)(GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length);
typedef void (*PFNGLCOMPILESHADERPROC)(GLuint shader);
typedef GLuint (*PFNGLCREATEPROGRAMPROC)(void);
typedef void (*PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);
typedef void (*PFNGLLINKPROGRAMPROC)(GLuint program);
typedef void (*PFNGLUSEPROGRAMPROC)(GLuint program);
typedef GLint (*PFNGLGETUNIFORMLOCATIONPROC)(GLuint program, const GLchar* name);
typedef void (*PFNGLUNIFORM4FPROC)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
typedef void (*PFNGLUNIFORM2FPROC)(GLint location, GLfloat v0, GLfloat v1);
typedef void (*PFNGLUNIFORM1FPROC)(GLint location, GLfloat v0);
typedef void (*PFNGLUNIFORM1IPROC)(GLint location, GLint v0);
typedef void (*PFNGLACTIVETEXTUREPROC)(GLenum texture);
typedef void (*PFNGLGENFRAMEBUFFERSPROC)(GLsizei n, GLuint* framebuffers);
typedef void (*PFNGLBINDFRAMEBUFFERPROC)(GLenum target, GLuint framebuffer);
typedef void (*PFNGLFRAMEBUFFERTEXTURE2DPROC)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
typedef void (*PFNGLDELETEFRAMEBUFFERSPROC)(GLsizei n, const GLuint* framebuffers);
typedef GLenum (*PFNGLCHECKFRAMEBUFFERSTATUSPROC)(GLenum target);

/* GL 1.0/1.1 core entry-point typedefs.  <GL/gl.h>/<GL/glext.h> only ship PFN
 * typedefs for GL 1.2+, so we declare the ones we need for the 1.0/1.1 calls
 * that would otherwise be satisfied by directly-linked libGL symbols.  Those
 * direct symbols dispatch through libGL's own current-context slot, which is
 * populated by glXMakeCurrent — NOT by eglMakeCurrent.  On a driver stack
 * where libGL and libEGL do not share one glapi dispatch, that slot stays
 * NULL under EGL and the first such call dereferences a NULL gl_context deep
 * inside the driver (the "NULL-context crash in libgallium" seen right after
 * the first texture upload).  Routing these through get_proc()/eglGetProc-
 * Address() (see load_gl_functions()) makes every GL call in the process share
 * the single dispatch table that eglMakeCurrent() actually made current. */
typedef void (*LUNA_PFNGLVIEWPORTPROC)(GLint x, GLint y, GLsizei w, GLsizei h);
typedef void (*LUNA_PFNGLCLEARPROC)(GLbitfield mask);
typedef void (*LUNA_PFNGLCLEARCOLORPROC)(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
typedef void (*LUNA_PFNGLENABLEPROC)(GLenum cap);
typedef void (*LUNA_PFNGLDISABLEPROC)(GLenum cap);
typedef void (*LUNA_PFNGLBLENDFUNCPROC)(GLenum sfactor, GLenum dfactor);
typedef void (*LUNA_PFNGLBLENDFUNCSEPARATEPROC)(GLenum src_rgb, GLenum dst_rgb,
        GLenum src_alpha, GLenum dst_alpha);
typedef void (*LUNA_PFNGLSCISSORPROC)(GLint x, GLint y, GLsizei w, GLsizei h);
typedef void (*LUNA_PFNGLDRAWARRAYSPROC)(GLenum mode, GLint first, GLsizei count);
typedef void (*LUNA_PFNGLGENTEXTURESPROC)(GLsizei n, GLuint* textures);
typedef void (*LUNA_PFNGLDELETETEXTURESPROC)(GLsizei n, const GLuint* textures);
typedef void (*LUNA_PFNGLBINDTEXTUREPROC)(GLenum target, GLuint texture);
typedef void (*LUNA_PFNGLTEXPARAMETERIPROC)(GLenum target, GLenum pname, GLint param);
typedef void (*LUNA_PFNGLTEXIMAGE2DPROC)(GLenum target, GLint level, GLint internalformat,
        GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void* pixels);
typedef void (*LUNA_PFNGLTEXSUBIMAGE2DPROC)(GLenum target, GLint level, GLint xoffset, GLint yoffset,
        GLsizei width, GLsizei height, GLenum format, GLenum type, const void* pixels);
typedef void (*LUNA_PFNGLCOPYTEXSUBIMAGE2DPROC)(GLenum target, GLint level, GLint xoffset, GLint yoffset,
        GLint x, GLint y, GLsizei width, GLsizei height);
typedef void (*LUNA_PFNGLREADPIXELSPROC)(GLint x, GLint y, GLsizei width, GLsizei height,
        GLenum format, GLenum type, void* pixels);

#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER          0x8D40
#define GL_DRAW_FRAMEBUFFER     0x8CA9
#define GL_READ_FRAMEBUFFER     0x8CA8
#define GL_COLOR_ATTACHMENT0    0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif

PFNGLCREATEVERTEXARRAYSPROC glCreateVertexArrays;
PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays;
PFNGLGENBUFFERSPROC glGenBuffers;
PFNGLBINDBUFFERPROC glBindBuffer;
PFNGLBUFFERDATAPROC glBufferData;
PFNGLBUFFERSUBDATAPROC glBufferSubData;
PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer;
PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray;
PFNGLDELETEBUFFERSPROC glDeleteBuffers;
PFNGLBINDVERTEXARRAYPROC glBindVertexArray;
PFNGLCREATESHADERPROC glCreateShader;
PFNGLSHADERSOURCEPROC glShaderSource;
PFNGLCOMPILESHADERPROC glCompileShader;
PFNGLCREATEPROGRAMPROC glCreateProgram;
PFNGLATTACHSHADERPROC glAttachShader;
PFNGLLINKPROGRAMPROC glLinkProgram;
PFNGLUSEPROGRAMPROC glUseProgram;
PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation;
PFNGLUNIFORM4FPROC glUniform4f;
PFNGLUNIFORM2FPROC glUniform2f;
PFNGLUNIFORM1FPROC glUniform1f;
PFNGLUNIFORM1IPROC glUniform1i_;
PFNGLACTIVETEXTUREPROC glActiveTexture_;
PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers_;
PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer_;
PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D_;
PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers_;
PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus_;

/* GL 1.0/1.1 core function pointers, resolved in load_gl_functions() through
 * the same get_proc()/eglGetProcAddress() path as the modern entry points so
 * every GL call shares one dispatch table (see the typedef comment above).
 * The macros redirect every subsequent gl* call in luna-ui.h and luna-shell.c
 * from the directly-linked libGL symbol to these pointers. */
LUNA_PFNGLVIEWPORTPROC         luna_p_glViewport;
LUNA_PFNGLCLEARPROC            luna_p_glClear;
LUNA_PFNGLCLEARCOLORPROC       luna_p_glClearColor;
LUNA_PFNGLENABLEPROC           luna_p_glEnable;
LUNA_PFNGLDISABLEPROC          luna_p_glDisable;
LUNA_PFNGLBLENDFUNCPROC        luna_p_glBlendFunc;
LUNA_PFNGLBLENDFUNCSEPARATEPROC luna_p_glBlendFuncSeparate;
LUNA_PFNGLSCISSORPROC          luna_p_glScissor;
LUNA_PFNGLDRAWARRAYSPROC       luna_p_glDrawArrays;
LUNA_PFNGLGENTEXTURESPROC      luna_p_glGenTextures;
LUNA_PFNGLDELETETEXTURESPROC   luna_p_glDeleteTextures;
LUNA_PFNGLBINDTEXTUREPROC      luna_p_glBindTexture;
LUNA_PFNGLTEXPARAMETERIPROC    luna_p_glTexParameteri;
LUNA_PFNGLTEXIMAGE2DPROC       luna_p_glTexImage2D;
LUNA_PFNGLTEXSUBIMAGE2DPROC    luna_p_glTexSubImage2D;
LUNA_PFNGLCOPYTEXSUBIMAGE2DPROC luna_p_glCopyTexSubImage2D;
LUNA_PFNGLREADPIXELSPROC       luna_p_glReadPixels;

#define glViewport          luna_p_glViewport
#define glClear             luna_p_glClear
#define glClearColor        luna_p_glClearColor
#define glEnable            luna_p_glEnable
#define glDisable           luna_p_glDisable
#define glBlendFunc         luna_p_glBlendFunc
#define glBlendFuncSeparate luna_p_glBlendFuncSeparate
#define glScissor           luna_p_glScissor
#define glDrawArrays        luna_p_glDrawArrays
#define glGenTextures       luna_p_glGenTextures
#define glDeleteTextures    luna_p_glDeleteTextures
#define glBindTexture       luna_p_glBindTexture
#define glTexParameteri     luna_p_glTexParameteri
#define glTexImage2D        luna_p_glTexImage2D
#define glTexSubImage2D     luna_p_glTexSubImage2D
#define glCopyTexSubImage2D luna_p_glCopyTexSubImage2D
#define glReadPixels        luna_p_glReadPixels

// --- Shaders ---

const char* bg_vs =
    "#version 330 core\n"
    "layout (location = 0) in vec2 aPos;\n"
    "out vec2 FragPos;\n"
    "uniform vec2 uResolution;\n"
    "uniform vec2 uPos;\n"
    "uniform vec2 uSize;\n"
    "void main() {\n"
    "    FragPos = vec2(aPos.x, 1.0 - aPos.y) * uSize;\n"
    "    vec2 screenPos = uPos + vec2(aPos.x * uSize.x, aPos.y * uSize.y);\n"
    "    vec2 ndc = (screenPos / uResolution) * 2.0 - 1.0;\n"
    "    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);\n"
    "}\0";

// uGradient: 0=solid, 1=linear, 2=radial, 3=conic, 4=ellipse
// Up to 8 color stops via uGradStopCount, uGradColors[], uGradStops[]
// uRadius4: per-corner radius (tl, tr, br, bl in screen orientation).
// FragPos.y is flipped (0=bottom), so p.y > 0 means the screen-top half.
const char* bg_fs =
    "#version 330 core\n"
    "in vec2 FragPos;\n"
    "out vec4 FragColor;\n"
    "uniform vec4 uColor;\n"
    "uniform vec4 uBorderColor;\n"
    "uniform float uBorderWidth;\n"
    "uniform vec2 uSize;\n"
    "uniform vec4 uRadius4;\n"
    "uniform int uGradient;\n"
    "uniform int uGradStopCount;\n"
    "uniform vec4 uGradColors[8];\n"
    "uniform float uGradStops[8];\n"
    "uniform float uGradAngle;\n"
    "uniform vec2 uGradCenter;\n"
    "uniform float uGradRadius;\n"
    "uniform float uGradRadRx;\n"
    "uniform float uGradRadRy;\n"
    "uniform float uFilterBrightness;\n"
    "uniform float uFilterContrast;\n"
    "uniform float uFilterSaturate;\n"
    "uniform float uFilterHue;\n"
    "uniform int   uFilterMode;\n"
    "uniform vec2  uPos;\n"
    "uniform int   uClipEnabled;\n"
    "uniform vec2  uClipPos;\n"
    "uniform vec2  uClipSize;\n"
    "uniform vec4  uClipRadius4;\n"
    "#define M_PI 3.14159265358979323846\n"
    "float rr_sdf4(vec2 p, vec2 hs, vec4 r) {\n"
    "    float rc = (p.x < 0.0) ? ((p.y > 0.0) ? r.x : r.w) : ((p.y > 0.0) ? r.y : r.z);\n"
    "    vec2 q = abs(p) - hs + vec2(rc);\n"
    "    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - rc;\n"
    "}\n"
    "vec3 hue_rotate(vec3 col, float angle) {\n"
    "    float c = cos(angle); float s = sin(angle);\n"
    "    mat3 m = mat3(\n"
    "        0.213+c*0.787-s*0.213, 0.715-c*0.715-s*0.715, 0.072-c*0.072+s*0.928,\n"
    "        0.213-c*0.213+s*0.143, 0.715+c*0.285+s*0.140, 0.072-c*0.072-s*0.283,\n"
    "        0.213-c*0.213-s*0.787, 0.715-c*0.715+s*0.715, 0.072+c*0.928+s*0.072);\n"
    "    return clamp(m * col, 0.0, 1.0);\n"
    "}\n"
    "vec4 sampleGradient(float t) {\n"
    "    t = clamp(t, 0.0, 1.0);\n"
    "    if(uGradStopCount <= 1) return uGradColors[0];\n"
    "    if(t <= uGradStops[0]) return uGradColors[0];\n"
    "    for(int i = 0; i < 7; i++) {\n"
    "        if(i + 1 >= uGradStopCount) break;\n"
    "        float a = uGradStops[i];\n"
    "        float b = uGradStops[i + 1];\n"
    "        if(t >= a && t <= b) {\n"
    "            float u = (b > a) ? (t - a) / (b - a) : 0.0;\n"
    "            return mix(uGradColors[i], uGradColors[i + 1], u);\n"
    "        }\n"
    "    }\n"
    "    return uGradColors[uGradStopCount - 1];\n"
    "}\n"
    "void main() {\n"
    "    vec2 halfSize = uSize / 2.0;\n"
    "    float sdf = rr_sdf4(FragPos - halfSize, halfSize, uRadius4);\n"
    "    float alpha = 1.0 - smoothstep(-1.0, 0.5, sdf);\n"
    "    if(alpha <= 0.0) discard;\n"
    "    if(uClipEnabled != 0) {\n"
    "        vec2 s = vec2(uPos.x + FragPos.x, uPos.y + uSize.y - FragPos.y);\n"
    "        vec2 cf = vec2(s.x - uClipPos.x, uClipPos.y + uClipSize.y - s.y);\n"
    "        vec2 ch = uClipSize * 0.5;\n"
    "        float csdf = rr_sdf4(cf - ch, ch, uClipRadius4);\n"
    "        alpha *= 1.0 - smoothstep(-1.0, 0.5, csdf);\n"
    "        if(alpha <= 0.0) discard;\n"
    "    }\n"
    "    vec4 baseColor;\n"
    "    if(uGradient == 1) {\n"
    "        float ca = cos(uGradAngle); float sa = sin(uGradAngle);\n"
    "        vec2 uv = FragPos / uSize;\n"
    "        float t = dot(vec2(uv.x, 1.0-uv.y) - 0.5, vec2(sa, -ca)) + 0.5;\n"
    "        baseColor = sampleGradient(t);\n"
    "    } else if(uGradient == 2) {\n"
    "        vec2 c = vec2(uGradCenter.x, 1.0-uGradCenter.y) * uSize;\n"
    "        float radius = max(uGradRadius * max(uSize.x, uSize.y), 0.001);\n"
    "        float t = distance(FragPos, c) / radius;\n"
    "        baseColor = sampleGradient(t);\n"
    "    } else if(uGradient == 3) {\n"
    "        vec2 c = vec2(uGradCenter.x, 1.0-uGradCenter.y) * uSize;\n"
    "        vec2 d = FragPos - c;\n"
    "        float angle = atan(d.y, d.x);\n"
    "        float t = mod((angle - uGradAngle) / (2.0 * M_PI) + 1.0, 1.0);\n"
    "        baseColor = sampleGradient(t);\n"
    "    } else if(uGradient == 4) {\n"
    "        vec2 c = vec2(uGradCenter.x, 1.0-uGradCenter.y) * uSize;\n"
    "        vec2 d = FragPos - c;\n"
    "        float rx = max(uGradRadRx > 0.0 ? uGradRadRx : uSize.x * 0.5, 0.001);\n"
    "        float ry = max(uGradRadRy > 0.0 ? uGradRadRy : uSize.y * 0.5, 0.001);\n"
    "        float t = sqrt((d.x * d.x) / (rx * rx) + (d.y * d.y) / (ry * ry));\n"
    "        baseColor = sampleGradient(t);\n"
    "    } else {\n"
    "        baseColor = uColor;\n"
    "    }\n"
    "    float bw = uBorderWidth;\n"
    "    float borderMix = (bw > 0.01) ? smoothstep(-bw - 1.0, -bw + 0.5, sdf) : 0.0;\n"
    "    vec4 finalColor = mix(baseColor, uBorderColor, borderMix);\n"
    "    if(uFilterMode != 0) {\n"
    "        vec3 fc = finalColor.rgb;\n"
    "        fc *= uFilterBrightness;\n"
    "        fc = (fc - 0.5) * uFilterContrast + 0.5;\n"
    "        float gray = dot(fc, vec3(0.299, 0.587, 0.114));\n"
    "        fc = mix(vec3(gray), fc, uFilterSaturate);\n"
    "        if(uFilterHue != 0.0) fc = hue_rotate(fc, uFilterHue);\n"
    "        finalColor.rgb = clamp(fc, 0.0, 1.0);\n"
    "    }\n"
    "    FragColor = vec4(finalColor.rgb, finalColor.a * alpha);\n"
    "}\0";

// CSS box-shadow: Gaussian soft shadow via SDF of the rounded rect.
// uShadowInset: top-left of actual element within the (padded) shadow rect (no offset).
// uElemSize:    actual element size.
// uOffset:      shadow offset (sh_dx, sh_dy) in FragPos space (x right, y down-flipped).
const char* shadow_fs =
    "#version 330 core\n"
    "in vec2 FragPos;\n"
    "out vec4 FragColor;\n"
    "uniform vec4 uShadowColor;\n"
    "uniform vec2 uShadowInset;\n"
    "uniform vec2 uElemSize;\n"
    "uniform vec2 uOffset;\n"
    "uniform vec2 uSize;\n"
    "uniform vec4 uRadius4;\n"
    "uniform float uBlur;\n"
    "uniform float uSpread;\n"
    "uniform int uInsetMode;\n"
    // Accurate erf approximation (A&S §7.1.26, max error < 1.5e-7)
    "float erf_approx(float x) {\n"
    "    float sign_x = x >= 0.0 ? 1.0 : -1.0;\n"
    "    float ax = abs(x);\n"
    "    float t = 1.0 / (1.0 + 0.3275911 * ax);\n"
    "    float y = 1.0 - t*(0.254829592 + t*(-0.284496736 + t*(1.421413741\n"
    "              + t*(-1.453152027 + t*1.061405429)))) * exp(-ax*ax);\n"
    "    return sign_x * y;\n"
    "}\n"
    "float rr_sdf4(vec2 p, vec2 hs, vec4 r) {\n"
    "    float rc = (p.x < 0.0) ? ((p.y > 0.0) ? r.x : r.w) : ((p.y > 0.0) ? r.y : r.z);\n"
    "    vec2 q = abs(p) - hs + vec2(rc);\n"
    "    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - rc;\n"
    "}\n"
    "void main() {\n"
    "    vec2 hs = uElemSize * 0.5;\n"
    "    float mhs = min(hs.x, hs.y);\n"
    "    vec4 r4 = clamp(uRadius4, vec4(0.0), vec4(mhs));\n"
    "    // Element's own footprint SDF.  posElem anchors to the element's\n"
    "    // bottom-left corner in FragPos space (fragpos.y = 0 at rect bottom):\n"
    "    //   elem_fp_y = uSize.y - uShadowInset.y - uElemSize.y  (= bot_pad)\n"
    "    //   This compensates for sh_dy baked into the rect height.\n"
    "    vec2 posElem = vec2(FragPos.x - uShadowInset.x,\n"
    "                        FragPos.y - (uSize.y - uShadowInset.y - uElemSize.y));\n"
    "    float distElem = rr_sdf4(posElem - hs, hs, r4);\n"
    "    // Early out before the expensive erf: outer shadows are fully hidden\n"
    "    // by the element interior; inset shadows never paint outside it.\n"
    "    if (uInsetMode == 0 && distElem <= -1.0) discard;\n"
    "    if (uInsetMode == 1 && distElem >= 0.0) discard;\n"
    "    // Shadow shape: element inflated by spread (deflated when inset),\n"
    "    // shifted by offset.\n"
    "    float sp = (uInsetMode == 1) ? -uSpread : uSpread;\n"
    "    vec2 hss = max(hs + vec2(sp), vec2(0.001));\n"
    "    vec4 rs = max(r4 + vec4(sp), vec4(0.0));\n"
    "    vec2 posShadow = FragPos - uShadowInset - uOffset;\n"
    "    float distShadow = rr_sdf4(posShadow - hs, hss, rs);\n"
    "    float sigma = max(uBlur * 0.5, 0.001);\n"
    "    float alpha;\n"
    "    if (uInsetMode == 1) {\n"
    "        // Inset: dark where the shrunk/offset shape does NOT cover,\n"
    "        // clipped to the inside of the element.\n"
    "        alpha = 0.5 + 0.5 * erf_approx(distShadow / (sigma * 1.41421356));\n"
    "        alpha *= 1.0 - smoothstep(-1.0, 0.0, distElem);\n"
    "    } else {\n"
    "        alpha = 0.5 - 0.5 * erf_approx(distShadow / (sigma * 1.41421356));\n"
    "        // Clip shadow inside the element's own footprint to prevent dark\n"
    "        // bleed at transparent rounded corners.\n"
    "        alpha *= smoothstep(-1.0, 0.0, distElem);\n"
    "    }\n"
    "    if (alpha < 0.004) discard;\n"
    "    FragColor = vec4(uShadowColor.rgb, uShadowColor.a * alpha);\n"
    "}\0";

const char* text_vs =
    "#version 330 core\n"
    "layout (location = 0) in vec4 vertex;\n"
    "out vec2 TexCoords;\n"
    "out vec2 ScreenPos;\n"
    "uniform vec2 uResolution;\n"
    "void main() {\n"
    "    vec2 ndc = (vertex.xy / uResolution) * 2.0 - 1.0;\n"
    "    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);\n"
    "    TexCoords = vertex.zw;\n"
    "    ScreenPos = vertex.xy;\n"
    "}\0";

const char* text_fs =
    "#version 330 core\n"
    "in vec2 TexCoords;\n"
    "in vec2 ScreenPos;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D text;\n"
    "uniform vec4 textColor;\n"
    "uniform int uGradMode;\n"
    "uniform int uGradStopCount;\n"
    "uniform vec4 uGradColors[8];\n"
    "uniform float uGradStops[8];\n"
    "uniform float uGradAngle;\n"
    "uniform vec4 uElemBounds;\n"
    "#define M_PI 3.14159265358979323846\n"
    "vec4 sampleGradient(float t) {\n"
    "    t = clamp(t, 0.0, 1.0);\n"
    "    if(uGradStopCount <= 1) return uGradColors[0];\n"
    "    if(t <= uGradStops[0]) return uGradColors[0];\n"
    "    for(int i = 0; i < 7; i++) {\n"
    "        if(i + 1 >= uGradStopCount) break;\n"
    "        float a = uGradStops[i]; float b = uGradStops[i+1];\n"
    "        if(t >= a && t <= b) {\n"
    "            float u = (b > a) ? (t - a) / (b - a) : 0.0;\n"
    "            return mix(uGradColors[i], uGradColors[i+1], u);\n"
    "        }\n"
    "    }\n"
    "    return uGradColors[uGradStopCount - 1];\n"
    "}\n"
    "void main() {\n"
    "    float sampled = texture(text, TexCoords).r;\n"
    "    vec4 fc;\n"
    "    if(uGradMode == 1 && uElemBounds.z > 0.0) {\n"
    "        float ca = cos(uGradAngle); float sa = sin(uGradAngle);\n"
    "        vec2 uv = vec2((ScreenPos.x - uElemBounds.x) / uElemBounds.z,\n"
    "                       (ScreenPos.y - uElemBounds.y) / uElemBounds.w);\n"
    "        float t = dot(vec2(uv.x, 1.0-uv.y) - 0.5, vec2(sa, -ca)) + 0.5;\n"
    "        fc = sampleGradient(t);\n"
    "    } else {\n"
    "        fc = textColor;\n"
    "    }\n"
    "    FragColor = vec4(fc.rgb, fc.a * sampled);\n"
    "}\0";

// Image shader: renders a texture cropped to a rounded rect.
// Uses same vertex shader as bg (bg_vs). FragPos.y is flipped (0=bottom, uSize.y=top).
// stb_image is loaded with flip_vertically so UV = FragPos/uSize maps correctly.
const char* img_fs =
    "#version 330 core\n"
    "in vec2 FragPos;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D uImage;\n"
    "uniform vec2 uSize;\n"
    "uniform float uRadius;\n"
    "uniform float uAlpha;\n"
    "void main() {\n"
    "    vec2 halfSize = uSize / 2.0;\n"
    "    float r = max(uRadius, 0.001);\n"
    "    vec2 d = abs(FragPos - halfSize) - halfSize + vec2(r);\n"
    "    float dist = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);\n"
    "    float alpha = 1.0 - smoothstep(r - 1.0, r + 0.5, dist);\n"
    "    if(alpha <= 0.0) discard;\n"
    "    vec2 uv = FragPos / uSize;\n"
    "    vec4 tc = texture(uImage, uv);\n"
    "    FragColor = vec4(tc.rgb, tc.a * alpha * uAlpha);\n"
    "}\0";

// Backdrop blur: separable 9-tap Gaussian, applied once per axis.
// bg_vs is reused as vertex shader with uPos=(elem_x,elem_y), uSize=(w,h).
// FragPos goes from (0,0) to (w,h) with y-flip (0=bottom).
// uBlurDir = (1/fbw, 0) horizontal or (0, 1/fbh) vertical.
// uBlurRadius = blur radius in pixels; uBlurTexSize = capture texture size.
// uFbSize = framebuffer size.  The capture texture may be *larger* than the
//          framebuffer — it is grown to the biggest surface ever rendered and
//          never shrunk — so the two sizes have to be kept apart: content is
//          anchored at texture y=0 (the bottom), which is where a viewport of
//          the framebuffer's height puts it, while UV normalisation divides by
//          the texture's size.  uFbSize/uBlurTexSize is therefore also the
//          upper UV bound of everything the capture actually wrote.
// uBlurOrigin = (elem_x, elem_y) — where the element sits in screen coords
//               so we can compute correct UV into the full-window capture texture.
const char* blur_fs =
    "#version 330 core\n"
    "in vec2 FragPos;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D uSrc;\n"
    "uniform vec2 uSize;\n"
    "uniform vec2 uBlurDir;\n"
    "uniform float uBlurRadius;\n"
    "uniform vec2 uBlurTexSize;\n"
    "uniform vec2 uFbSize;\n"
    "uniform vec2 uBlurOrigin;\n"
    "void main() {\n"
    /* FragPos: element-local, y-flipped (0=bottom, uSize.y=top).
       Screen (y-down): sx = origin.x + FragPos.x
                        sy = origin.y + (uSize.y - FragPos.y)
       GL texture (y-up): sp.y = fb_h - sy */
    "    vec2 screenPos = vec2(uBlurOrigin.x + FragPos.x,\n"
    "                          uBlurOrigin.y + (uSize.y - FragPos.y));\n"
    "    vec2 uvMax = uFbSize / uBlurTexSize;\n"
    "    vec2 uv = vec2(screenPos.x, uFbSize.y - screenPos.y) / uBlurTexSize;\n"
    "    float r = max(uBlurRadius, 0.5);\n"
    "    float sigma = r * 0.35 + 0.5;\n"
    "    vec4 col = vec4(0.0);\n"
    "    float wsum = 0.0;\n"
    "    for(int i = -4; i <= 4; i++) {\n"
    "        float fi = float(i) * (r / 4.0);\n"
    "        float w = exp(-(fi * fi) / (2.0 * sigma * sigma));\n"
    "        vec2 suv = clamp(uv + uBlurDir * fi, vec2(0.0), uvMax);\n"
    "        col += texture(uSrc, suv) * w;\n"
    "        wsum += w;\n"
    "    }\n"
    "    FragColor = col / max(wsum, 0.001);\n"
    "}\0";

// Backdrop composite: draws a blurred texture clipped to a rounded rect.
// Uses bg_vs. FragPos local coord.
const char* backdrop_fs =
    "#version 330 core\n"
    "in vec2 FragPos;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D uSrc;\n"
    "uniform vec2 uSize;\n"
    "uniform vec4 uRadius4;\n"
    "uniform vec2 uBlurTexSize;\n"
    "uniform vec2 uFbSize;\n"
    "uniform vec2 uBlurOrigin;\n"
    "uniform float uSaturate;\n"
    "uniform float uBrightness;\n"
    "float rr_sdf4(vec2 p, vec2 hs, vec4 r) {\n"
    "    float rc = (p.x < 0.0) ? ((p.y > 0.0) ? r.x : r.w) : ((p.y > 0.0) ? r.y : r.z);\n"
    "    vec2 q = abs(p) - hs + vec2(rc);\n"
    "    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - rc;\n"
    "}\n"
    "void main() {\n"
    "    vec2 halfSize = uSize / 2.0;\n"
    "    float sdf = rr_sdf4(FragPos - halfSize, halfSize, uRadius4);\n"
    "    float alpha = 1.0 - smoothstep(-1.0, 0.5, sdf);\n"
    "    if(alpha <= 0.0) discard;\n"
    "    vec2 screenPos = vec2(uBlurOrigin.x + FragPos.x,\n"
    "                          uBlurOrigin.y + (uSize.y - FragPos.y));\n"
    "    vec2 uv = vec2(screenPos.x, uFbSize.y - screenPos.y) / uBlurTexSize;\n"
    "    vec4 tc = texture(uSrc, clamp(uv, vec2(0.0), uFbSize / uBlurTexSize));\n"
    "    float gray = dot(tc.rgb, vec3(0.299, 0.587, 0.114));\n"
    "    vec3 filtered = mix(vec3(gray), tc.rgb, uSaturate) * uBrightness;\n"
    "    FragColor = vec4(clamp(filtered, 0.0, 1.0), alpha);\n"
    "}\0";

#define MAX_GRAD_STOPS 8
#define GRAD_NONE    0
#define GRAD_LINEAR  1
#define GRAD_RADIAL  2
#define GRAD_CONIC   3
#define GRAD_ELLIPSE 4

#define FLEX_DIR_ROW    0
#define FLEX_DIR_COLUMN 1
#define FLEX_JUSTIFY_START         0
#define FLEX_JUSTIFY_CENTER        1
#define FLEX_JUSTIFY_END           2
#define FLEX_JUSTIFY_SPACE_BETWEEN 3
#define FLEX_ALIGN_START   0
#define FLEX_ALIGN_CENTER  1
#define FLEX_ALIGN_END     2
#define FLEX_ALIGN_STRETCH 3
#define FLEX_ALIGN_SPACE_BETWEEN 4
#define FLEX_ALIGN_SPACE_AROUND  5

#define GRID_AUTO_FLOW_ROW     0
#define GRID_AUTO_FLOW_COLUMN  1
#define GRID_AUTO_FLOW_DENSE   2

#define DISPLAY_BLOCK 0
#define DISPLAY_NONE  1
#define DISPLAY_FLEX  2
#define DISPLAY_GRID  3

#define FLEX_WRAP_NOWRAP 0
#define FLEX_WRAP_WRAP   1

#define ALIGN_SELF_AUTO  -1

#define BOX_CONTENT 0
#define BOX_BORDER  1

#define MAX_GRID_TRACKS 8
#define MAX_GRID_AREA_ROWS 8
#define MAX_GRID_AREA_COLS 8
#define MAX_GRID_AREAS   16

#define GRID_TRACK_PX     0
#define GRID_TRACK_FR     1
#define GRID_TRACK_MINMAX 2

#define OVERFLOW_VISIBLE 0
#define OVERFLOW_HIDDEN  1
#define OVERFLOW_AUTO    2
#define OVERFLOW_SCROLL  3

typedef struct {
    char name[32];
    int col, row, col_span, row_span;
} GridAreaRect;

/* One CSS background layer (background: grad1, grad2, ...) */
#define LUNA_MAX_BG_LAYERS 4
typedef struct {
    int  has_gradient; int grad_type;
    int  grad_stop_count;
    float grad_stop_pos[MAX_GRAD_STOPS];
    float grad_stop_r[MAX_GRAD_STOPS], grad_stop_g[MAX_GRAD_STOPS];
    float grad_stop_b[MAX_GRAD_STOPS], grad_stop_a[MAX_GRAD_STOPS];
    float grad_angle;
    float grad_rad_cx, grad_rad_cy, grad_rad_r;
    float grad_rad_rx, grad_rad_ry; /* ellipse radii (fraction of elem size if _pct set) */
    int  grad_rad_rx_pct, grad_rad_ry_pct; /* 1 = rx/ry are fractions of elem w/h */
    int  has_color; float r, g, b, a;
    int  has_bg_image; char image_path[256];
} LunaBgLayer;

/* One CSS box-shadow layer (multiple layers per element supported). */
#define LUNA_MAX_SHADOWS 4
typedef struct {
    float dx, dy, blur, spread;
    float r, g, b, a;
    int inset;
} LunaShadow;

struct LunaElement;
typedef LunaEventHandler EventHandler;

struct LunaElement {
    int id_idx;
    int parent_idx;
    float rel_x, rel_y;
    float x, y, w, h;
    char text[512], type[32], class_name[96], id[64];
    int is_hovered, is_active, is_draggable;
    /* Text controls (<input>/<textarea>) — value lives in text[] */
    int is_input;
    int input_multiline;   /* textarea */
    int input_password;
    char placeholder[160];
    int caret;             /* UTF-8 byte offset into text */
    float input_scroll_x;
    float caret_r, caret_g, caret_b, caret_a;
    int has_caret_color;
    int drag_mode; // 0=none, 1=move parent window, 2=drag self (slider thumb)

    int pct_w, pct_h, pct_left, pct_top, pct_bottom, pct_right;
    float raw_w, raw_h, raw_left, raw_top, raw_bottom, raw_right;
    float raw_w_off, raw_h_off, raw_left_off, raw_top_off, raw_bottom_off, raw_right_off;
    int has_bottom, has_right;
    int has_top, has_left;
    float bottom_val, right_val;
    int pos_overridden_x, pos_overridden_y;
    int position_fixed;
    int position_sticky;
    int position_mode; /* 0 unset 1 static 2 relative 3 absolute */
    float sticky_top;
    int sticky_use_top;
    int sticky_use_bottom;
    float sticky_bottom;
    int sticky_use_left;
    float sticky_left;
    int sticky_use_right;
    float sticky_right;

    int inert;
    int tabindex; /* -2=unset -1=skip 0+=order */
    char aria_label[128];
    char role[32];
    int aria_live; /* 0=off/unset 1=polite 2=assertive */
    int aria_hidden;
    int aria_expanded; /* -1=unset 0=false 1=true */

    float scroll_margin_top, scroll_margin_right, scroll_margin_bottom, scroll_margin_left;
    float scroll_padding_top, scroll_padding_right, scroll_padding_bottom, scroll_padding_left;

    float r, g, b, a;
    float t_r, t_g, t_b, t_a;
    float bd_r, bd_g, bd_b, bd_a;
    /* per-side borders (drawn as thin rects, independent of unified border_width) */
    int has_border_top, has_border_right, has_border_bottom, has_border_left;
    float border_top_w, border_right_w, border_bottom_w, border_left_w;
    float border_top_r, border_top_g, border_top_b, border_top_a;
    float border_right_r, border_right_g, border_right_b, border_right_a;
    float border_bottom_r, border_bottom_g, border_bottom_b, border_bottom_a;
    float border_left_r, border_left_g, border_left_b, border_left_a;

    float cur_r, cur_g, cur_b, cur_a;
    float cur_bd_r, cur_bd_g, cur_bd_b, cur_bd_a;
    float cur_scale;

    float border_radius;              /* max corner radius (legacy/scrollbar paths) */
    float rad_c[4];                   /* per-corner radius: tl, tr, br, bl */
    float border_width;
    float outline_width;
    float outline_offset;
    float ol_r, ol_g, ol_b, ol_a;
    int has_outline;
    float padding;                       /* legacy uniform value (== pad_t) */
    float pad_t, pad_r, pad_b, pad_l;    /* resolved per-side padding */
    float margin_top, margin_right, margin_bottom, margin_left;

    int margin_top_auto, margin_right_auto, margin_bottom_auto, margin_left_auto;

    float opacity;
    int display_none;
    int cursor_pointer;
    int cursor_type; // 0=default 1=pointer 2=text 3=crosshair 4=ew-resize 5=ns-resize
    int text_align;
    int has_text_align;
    float font_size;
    int font_bold;
    /* 0 = normal UI face, 1 = Luna Symbols, 2 = Luna Brands,
       3 = editor monospace face (LUNA_FONT_MONO / system mono fallback). */
    int font_face;
    float line_height;
    int white_space;    /* 0=normal 1=nowrap */
    int text_overflow;  /* 0=clip 1=ellipsis */
    int overflow_wrap;  /* 0=normal 1=break-word */
    float letter_spacing;
    int text_transform;  /* 0=none 1=uppercase 2=lowercase 3=capitalize */
    int text_decoration; /* bit0=underline bit1=line-through */
    int has_text_shadow;
    float tsh_dx, tsh_dy, tsh_blur;
    float tsh_r, tsh_g, tsh_b, tsh_a;
    int generated_pseudo; /* 0=dom 1=::before 2=::after */

    int has_shadow;
    int shadow_count;
    LunaShadow shadows[LUNA_MAX_SHADOWS];

    // Gradient: type 0=none 1=linear 2=radial 3=conic 4=ellipse
    int has_gradient;
    int grad_type;
    int grad_stop_count;
    float grad_stop_pos[MAX_GRAD_STOPS];
    float grad_stop_r[MAX_GRAD_STOPS], grad_stop_g[MAX_GRAD_STOPS];
    float grad_stop_b[MAX_GRAD_STOPS], grad_stop_a[MAX_GRAD_STOPS];
    float grad_angle;
    float grad_rad_cx, grad_rad_cy, grad_rad_r;
    float grad_rad_rx, grad_rad_ry; /* ellipse radii (fraction if _pct set) */
    int  grad_rad_rx_pct, grad_rad_ry_pct;

    /* background-clip: text — use gradient as text fill color */
    int has_bg_clip_text;
    /* mix-blend-mode: 0=normal 1=screen 2=multiply 3=add */
    int mix_blend_mode;

    /* Multiple background layers (background: grad1, grad2, ...) */
    LunaBgLayer bg_layers[LUNA_MAX_BG_LAYERS];
    int bg_layer_count;

    /* backdrop-filter: blur() saturate() brightness() */
    int has_backdrop_blur;
    float backdrop_blur_radius;
    float backdrop_saturate;
    float backdrop_brightness;

    int display_mode; // 0 block 1 none 2 flex 3 grid
    int flex_direction;
    int justify_content;
    int align_items;
    int justify_items;
    int align_content;
    int flex_wrap;
    int align_self;   // ALIGN_SELF_AUTO or FLEX_ALIGN_*
    int justify_self;
    float flex_gap;
    /* CSS flex factors are <number>, not integers.  Keeping the fractional
       value matters for proportional rows such as flex: .5 1 0. */
    float flex_grow;
    float flex_shrink;
    float flex_basis;
    int has_flex_basis;
    int flex_basis_auto;
    int flex_child;

    /* Direct text following an inline child in a row flex container is an
       anonymous flex item in CSS.  Keep its resolved start separately so the
       paint path can stay batched (no synthetic DOM node/allocation). */
    int has_inline_text_flow;
    float inline_text_x;

    int box_sizing;
    float css_width, css_height;
    float css_min_width, css_min_height;
    float css_max_width, css_max_height;
    int has_css_width, has_css_height;
    int has_min_width, has_min_height;
    int has_max_width, has_max_height;
    /* Percentage max-width is resolved against the containing block.  Keeping
       the ratio is essential: treating `max-width: 100%` as 100px clipped
       shrink-wrapped flex rows such as the shell's window list. */
    int max_width_pct;
    float raw_max_width, raw_max_width_off;
    /* Percentage max-height is resolved against the containing block.
       Previously `max-height: 90%` was parsed as 90px, crushing dialogs. */
    int max_height_pct;
    float raw_max_height, raw_max_height_off;

    int grid_col_count, grid_row_count;
    float grid_col_track[MAX_GRID_TRACKS];
    int   grid_col_type[MAX_GRID_TRACKS];
    float grid_col_min[MAX_GRID_TRACKS];
    float grid_row_track[MAX_GRID_TRACKS];
    int   grid_row_type[MAX_GRID_TRACKS];
    float grid_row_min[MAX_GRID_TRACKS];
    float grid_col_gap, grid_row_gap;
    int grid_auto_flow;
    float grid_auto_row_track, grid_auto_col_track;
    int   grid_auto_row_type, grid_auto_col_type;
    float grid_auto_row_min, grid_auto_col_min;
    int has_grid_auto_rows, has_grid_auto_columns;

    int grid_area_rows, grid_area_cols;
    char grid_area_cell[MAX_GRID_AREA_ROWS][MAX_GRID_AREA_COLS][32];
    GridAreaRect grid_area_rects[MAX_GRID_AREAS];
    int grid_area_rect_count;

    int grid_col, grid_row;
    int grid_col_span, grid_row_span;
    int has_grid_col, has_grid_row;
    char grid_area_name[32];
    int has_grid_area;
    int grid_child;
    int flow_child;

    int overflow_x, overflow_y;
    float scroll_top, scroll_left;
    float scroll_dest_top, scroll_dest_left;
    float scroll_content_h, scroll_content_w;
    int scroll_smooth;
    int scroll_snap_type; /* 0=none 1=y mandatory 2=y proximity */
    int scroll_snap_align; /* 0=start 1=center 2=end */
    float scrollbar_width;
    int has_scrollbar_width;
    float sb_track_r, sb_track_g, sb_track_b, sb_track_a;
    float sb_thumb_r, sb_thumb_g, sb_thumb_b, sb_thumb_a;
    int has_scrollbar_color;

    int css_positioned; // 1=left/right set, 2=top/bottom set

    int z_index;
    int visibility_hidden;
    float transform_scale;
    float transform_tx, transform_ty;
    float raw_transform_tx, raw_transform_ty;
    int transform_tx_pct, transform_ty_pct;
    float cur_tx, cur_ty;
    float anim_speed;
    int pointer_events_none;

    int has_custom_bg, has_custom_color, has_custom_text, has_custom_border;
    EventHandler on_click;
    char onclick[64];
    char data_tab[32];
    char inline_style[256];
    int has_inline_style;

    /* CSS filter */
    int has_filter;
    float filter_brightness; /* 1.0 = no change */
    float filter_contrast;   /* 1.0 = no change */
    float filter_saturate;   /* 1.0 = no change */
    float filter_hue;        /* radians */
    float filter_blur;       /* blur radius in pixels */

    /* font-style */
    int font_italic;

    /* aspect-ratio */
    int has_aspect_ratio;
    float aspect_ratio; /* w/h */

    /* background-size / background-position */
    int bg_size_mode;   /* 0=auto 1=cover 2=contain 3=explicit */
    float bg_size_w, bg_size_h;
    float bg_pos_x, bg_pos_y;

    /* CSS @keyframes animation state */
    char anim_name[64];
    float anim_duration;
    float anim_delay;
    int anim_infinite;
    int anim_alternate;
    int anim_easing; /* 0=linear 1=ease-in-out */
    int has_css_animation;
    double anim_start_time;
    int anim_finished;      /* finite animation final frame already applied */
    int anim_frame_changed; /* this update applied a new keyframe sample */
    int anim_override_layout;
    float anim_base_w;
    float anim_base_left;
    int anim_base_w_pct;
    int anim_base_left_pct;
    int anim_base_captured;

    int has_bg_image;
    char bg_image_path[256];
    GLuint bg_image_tex;

    /* Engine-managed overlay nodes (scrollbars, a11y) — positioned each layout pass */
    int luna_internal;
    int sb_host_idx; /* scrollbar elem → scroll container index */
    int sb_axis;     /* 0=v-track 1=v-thumb 2=h-track 3=h-thumb */
};

// --- CSS Rule ---

#define MAX_SEL_CLASSES   4
#define MAX_SEL_ANCESTORS 3

/* Relation of the compound BELOW an ancestor entry (towards the target). */
#define LUNA_REL_DESC  0   /* descendant: "A B"  */
#define LUNA_REL_CHILD 1   /* child:      "A > B" */
#define LUNA_REL_ADJ   2   /* adjacent:   "A + B" */
#define LUNA_REL_SIB   3   /* sibling:    "A ~ B" */

typedef struct {
    char sel_type[32];
    char sel_id[64];
    char sel_classes[MAX_SEL_CLASSES][64];
    int  sel_class_count;
    int  is_universal;
    int  rel;                       /* ancestors only: LUNA_REL_* */
    int  is_first_child, is_last_child;
    int  has_nth, nth_a, nth_b;     /* :nth-child(An+B), 1-based index */
    int  has_not;                   /* :not(simple) — single type/id/class */
    char not_type[32], not_id[64], not_class[64];
} SimpleSelector;

typedef struct {
    char selector[128];
    SimpleSelector target;
    SimpleSelector ancestors[MAX_SEL_ANCESTORS];
    int ancestor_count;
    /* CSS resolves equal-specificity declarations by source order.  Keep it
       explicitly: qsort is deliberately unstable for equal keys, which used
       to randomly let an earlier About/window rule override its later
       refinement. */
    int specificity, source_order;
    int is_hover, is_active, is_focus, is_focus_visible, is_focus_within;

    int has_bg;     float bg_r, bg_g, bg_b, bg_a;
    int has_bg_reset; /* `background:` shorthand resets image/layer state */
    int has_color;  float c_r,  c_g,  c_b,  c_a;
    int has_caret_color; float caret_r, caret_g, caret_b, caret_a;
    int has_border; float bd_r, bd_g, bd_b, bd_a; float border_width;
    int has_border_top, has_border_right, has_border_bottom, has_border_left;
    float border_top_w, border_right_w, border_bottom_w, border_left_w;
    float border_top_r, border_top_g, border_top_b, border_top_a;
    float border_right_r, border_right_g, border_right_b, border_right_a;
    float border_bottom_r, border_bottom_g, border_bottom_b, border_bottom_a;
    float border_left_r, border_left_g, border_left_b, border_left_a;
    int has_outline; float outline_width, outline_offset;
    float ol_r, ol_g, ol_b, ol_a;
    int has_radius; float border_radius;
    int has_rad_c[4]; float rad_c[4];   /* per-corner radius: tl, tr, br, bl */
    int has_width;  float width;
    int has_height; float height;
    int has_padding; float padding;
    float pad_t, pad_r, pad_b, pad_l;
    int has_margin; float margin_top, margin_right, margin_bottom, margin_left;
    int margin_top_auto, margin_right_auto, margin_bottom_auto, margin_left_auto;
    int has_left;   float left;
    int has_top;    float top;
    int has_bottom; float bottom;
    int has_right;  float right;
    int pct_bottom; float raw_bottom, raw_bottom_off;
    int pct_right;  float raw_right, raw_right_off;
    int has_position; int position_fixed; int position_sticky;
    int position_mode; /* 0 unset 1 static 2 relative 3 absolute */

    int has_opacity; float opacity;
    int has_cursor;  int cursor_pointer; int cursor_type;
    int has_display; int display_none;
    int display_mode; // 0 block 1 none 2 flex 3 grid
    int has_flex_direction; int flex_direction;
    int has_justify_content; int justify_content;
    int has_align_items; int align_items;
    int has_justify_items; int justify_items;
    int has_align_content; int align_content;
    int has_flex_wrap; int flex_wrap;
    int has_align_self; int align_self;
    int has_justify_self; int justify_self;
    int has_gap; float flex_gap;
    int has_flex_grow; float flex_grow;
    int has_flex_shrink; float flex_shrink;
    int has_flex_basis; float flex_basis; int flex_basis_auto;
    int has_min_width; float min_width;
    int has_min_height; float min_height;
    int has_max_width; float max_width;
    int max_width_pct; float raw_max_width, raw_max_width_off;
    int has_max_height; float max_height;
    int max_height_pct; float raw_max_height, raw_max_height_off;
    int has_box_sizing; int box_sizing;
    int has_grid_template_columns;
    float grid_col_track[MAX_GRID_TRACKS];
    int   grid_col_type[MAX_GRID_TRACKS];
    float grid_col_min[MAX_GRID_TRACKS];
    int   grid_col_count;
    int has_grid_template_rows;
    float grid_row_track[MAX_GRID_TRACKS];
    int   grid_row_type[MAX_GRID_TRACKS];
    float grid_row_min[MAX_GRID_TRACKS];
    int   grid_row_count;
    int has_grid_template_areas;
    int grid_area_rows, grid_area_cols;
    char grid_area_cell[MAX_GRID_AREA_ROWS][MAX_GRID_AREA_COLS][32];
    int has_column_gap; float grid_col_gap;
    int has_row_gap; float grid_row_gap;
    int has_grid_auto_flow; int grid_auto_flow;
    int has_grid_auto_rows;
    float grid_auto_row_track;
    int   grid_auto_row_type;
    float grid_auto_row_min;
    int has_grid_auto_columns;
    float grid_auto_col_track;
    int   grid_auto_col_type;
    float grid_auto_col_min;
    int has_grid_column; int grid_col;
    int has_grid_row; int grid_row;
    int has_grid_column_span; int grid_col_span;
    int has_grid_row_span; int grid_row_span;
    int has_grid_area; char grid_area_name[32];
    int has_overflow_x, overflow_x;
    int has_overflow_y, overflow_y;
    int has_scrollbar_width; float scrollbar_width;
    int has_scrollbar_color;
    float sb_thumb_r, sb_thumb_g, sb_thumb_b, sb_thumb_a;
    float sb_track_r, sb_track_g, sb_track_b, sb_track_a;
    int has_scroll_behavior; int scroll_smooth;
    int has_scroll_snap_type; int scroll_snap_type;
    int has_scroll_snap_align; int scroll_snap_align;
    int has_scroll_margin;
    float scroll_margin_top, scroll_margin_right, scroll_margin_bottom, scroll_margin_left;
    int has_scroll_padding;
    float scroll_padding_top, scroll_padding_right, scroll_padding_bottom, scroll_padding_left;
    int has_text_align; int text_align;
    int has_font_size;  float font_size;
    int has_font_weight; int font_bold;
    int has_font_face; int font_face;
    int has_line_height; float line_height;
    int has_white_space; int white_space;
    int has_text_overflow; int text_overflow;
    int has_overflow_wrap; int overflow_wrap;
    int has_letter_spacing; float letter_spacing;
    int has_text_transform; int text_transform;
    int has_text_decoration; int text_decoration;
    int has_text_shadow;
    float tsh_dx, tsh_dy, tsh_blur;
    float tsh_r, tsh_g, tsh_b, tsh_a;
    int has_shadow;
    int shadow_count;
    LunaShadow shadows[LUNA_MAX_SHADOWS];
    int has_content; char content[128];
    int pseudo_elem; /* 0=none 1=before 2=after */

    // Gradient: 0=none 1=linear 2=radial 3=conic 4=ellipse
    int has_gradient;
    int grad_type;
    int grad_stop_count;
    float grad_stop_pos[MAX_GRAD_STOPS];
    float grad_stop_r[MAX_GRAD_STOPS], grad_stop_g[MAX_GRAD_STOPS];
    float grad_stop_b[MAX_GRAD_STOPS], grad_stop_a[MAX_GRAD_STOPS];
    float grad_angle;
    float grad_rad_cx, grad_rad_cy, grad_rad_r;
    float grad_rad_rx, grad_rad_ry; /* ellipse radii (fraction if _pct set) */
    int  grad_rad_rx_pct, grad_rad_ry_pct;

    /* Multiple background layers */
    LunaBgLayer bg_layers[LUNA_MAX_BG_LAYERS];
    int bg_layer_count;

    /* backdrop-filter */
    int has_backdrop_blur;
    float backdrop_blur_radius;
    float backdrop_saturate;
    float backdrop_brightness;

    int has_z_index; int z_index;

    int pct_w; float raw_w; float raw_w_off;
    int pct_h; float raw_h; float raw_h_off;
    int pct_left; float raw_left; float raw_left_off;
    int pct_top; float raw_top; float raw_top_off;

    int has_visibility; int visibility_hidden;
    int has_transform; float transform_scale;
    int has_transform_tx; float transform_tx;
    int has_transform_ty; float transform_ty;
    float raw_transform_tx, raw_transform_ty;
    int transform_tx_pct, transform_ty_pct;
    int has_transition; float transition_duration;
    int has_pointer_events; int pointer_events_none;

    int has_animation;
    char anim_name[64];
    float anim_duration;
    float anim_delay;
    int anim_infinite;
    int anim_alternate;
    int anim_easing;

    int has_bg_image;
    char bg_image_path[256];
    int has_bg_image_reset; /* background-image:none clears prior image */

    /* CSS filter */
    int has_filter;
    float filter_brightness, filter_contrast, filter_saturate, filter_hue;
    float filter_blur; /* blur() radius in pixels */

    /* font-style */
    int has_font_italic; int font_italic;

    /* aspect-ratio */
    int has_aspect_ratio; float aspect_ratio;

    /* background-size / background-position */
    int has_bg_size; int bg_size_mode;
    float bg_size_w, bg_size_h;
    int has_bg_pos; float bg_pos_x, bg_pos_y;

    /* background-clip: text */
    int has_bg_clip_text;

    /* mix-blend-mode */
    int has_mix_blend_mode; int mix_blend_mode; /* 0=normal 1=screen 2=multiply 3=add */
} StyleRule;

#define MAX_KF_ANIMS 48
#define MAX_KF_STOPS 8
#define MAX_JS_HANDLERS 64

/* Keep CSS positioning modes available to both style application and layout. */
#define POS_UNSET     0
#define POS_STATIC    1
#define POS_RELATIVE  2
#define POS_ABSOLUTE  3

typedef struct {
    float position;
    int has_width; float width; int width_pct;
    int has_left; float left; int left_pct;
    int has_opacity; float opacity;
    int has_transform_scale; float transform_scale;
    int has_transform_tx; float transform_tx;
    int has_transform_ty; float transform_ty;
} KeyframeStop;

typedef struct {
    char name[64];
    KeyframeStop stops[MAX_KF_STOPS];
    int stop_count;
} CssKeyframe;

typedef struct {
    const char* name;
    EventHandler fn;
} JsHandlerEntry;

LunaElement  elements[MAX_ELEMENTS]; int elem_count = 0;

/* Hot-path registries.  Most frames have only a handful of active elements;
 * scanning the full DOM for scroll/keyframe/easing work made idle cost scale
 * with document size.  Style changes lazily rebuild the capability lists,
 * while visual candidates remain active only until their interpolation settles. */
static int g_scroll_tick_idx[MAX_ELEMENTS];
static int g_scroll_tick_count = 0;
static int g_css_anim_idx[MAX_ELEMENTS];
static int g_css_anim_count = 0;
static int g_activity_registry_dirty = 1;
static int g_visual_active_idx[MAX_ELEMENTS];
static unsigned char g_visual_active_flag[MAX_ELEMENTS];
static int g_visual_active_count = 0;
static int g_visual_scan_needed = 1;

static void visual_activate_idx(int idx) {
    if (idx < 0 || idx >= elem_count || g_visual_active_flag[idx]) return;
    if (g_visual_active_count >= MAX_ELEMENTS) return;
    g_visual_active_flag[idx] = 1;
    g_visual_active_idx[g_visual_active_count++] = idx;
}

static void visual_remove_pos(int pos) {
    int idx = g_visual_active_idx[pos];
    if (idx >= 0 && idx < MAX_ELEMENTS) g_visual_active_flag[idx] = 0;
    g_visual_active_idx[pos] = g_visual_active_idx[--g_visual_active_count];
}

static void rebuild_activity_registries(void) {
    if (!g_activity_registry_dirty) return;
    g_scroll_tick_count = 0;
    g_css_anim_count = 0;
    for (int i = 0; i < elem_count; i++) {
        LunaElement* e = &elements[i];
        if ((e->scroll_smooth || e->scroll_snap_type) &&
            g_scroll_tick_count < MAX_ELEMENTS)
            g_scroll_tick_idx[g_scroll_tick_count++] = i;
        if (e->has_css_animation && e->anim_name[0] && !e->anim_finished &&
            g_css_anim_count < MAX_ELEMENTS)
            g_css_anim_idx[g_css_anim_count++] = i;
    }
    g_activity_registry_dirty = 0;
}
StyleRule  css_rules[MAX_RULES];   int rule_count = 0;
CssKeyframe g_keyframes[MAX_KF_ANIMS];
int g_keyframe_count = 0;
JsHandlerEntry g_js_handlers[MAX_JS_HANDLERS];
int g_js_handler_count = 0;


static int render_order[MAX_ELEMENTS];

GLuint bg_program, text_program, shadow_program, img_program;
GLuint blur_program, backdrop_program;

/* GL program tracking — avoids redundant glUseProgram calls */
static GLuint g_current_program = 0;
static void luna_use_program(GLuint prog) {
    if (prog != g_current_program) { glUseProgram(prog); g_current_program = prog; }
}

/* Same idea for the vertex array binding.  Every rect primitive re-bound the
 * one shared quad VAO it had just drawn from; a document of a few hundred
 * elements issues that call several times per element. */
static GLuint g_current_vao = 0;
static void luna_bind_vao(GLuint vao) {
    if (vao != g_current_vao) { glBindVertexArray(vao); g_current_vao = vao; }
}

/* ── Uniform shadowing ──────────────────────────────────────────────────────
 *
 * Uniform values live in the program object, so they survive glUseProgram and
 * every framebuffer switch: re-uploading a value the program already holds
 * changes nothing on screen and still costs a driver entry point, an argument
 * marshal and a dirty-flag write on the uniform block.
 *
 * draw_rect_full() uploads two dozen uniforms per element — resolution, filter
 * mode, clip state, border colour, eight gradient stops — while consecutive
 * elements typically differ in three or four of them.  Every element of every
 * layer surface of every frame paid for the rest.  These helpers keep the last
 * uploaded value beside the location and skip the call when it has not moved.
 *
 * The shadow is seeded with NaN, which compares unequal to everything
 * including itself, so the first use of each uniform always uploads. */
typedef struct { float v[4]; } LunaUniShadow;

static float luna_uni_nan(void) {
    /* Avoids depending on <math.h> NAN being a compile-time constant. */
    static const unsigned u = 0x7fc00000u;
    float f;
    memcpy(&f, &u, sizeof f);
    return f;
}

static void luna_uni_invalidate(LunaUniShadow* s, int n) {
    float nan = luna_uni_nan();
    for (int i = 0; i < n; i++)
        s[i].v[0] = s[i].v[1] = s[i].v[2] = s[i].v[3] = nan;
}

static void uni1f(GLint loc, LunaUniShadow* s, float a) {
    if (s->v[0] == a) return;
    s->v[0] = a;
    glUniform1f(loc, a);
}

static void uni2f(GLint loc, LunaUniShadow* s, float a, float b) {
    if (s->v[0] == a && s->v[1] == b) return;
    s->v[0] = a; s->v[1] = b;
    glUniform2f(loc, a, b);
}

static void uni4f(GLint loc, LunaUniShadow* s, float a, float b, float c, float d) {
    if (s->v[0] == a && s->v[1] == b && s->v[2] == c && s->v[3] == d) return;
    s->v[0] = a; s->v[1] = b; s->v[2] = c; s->v[3] = d;
    glUniform4f(loc, a, b, c, d);
}

static void uni1i(GLint loc, LunaUniShadow* s, int a) {
    if (s->v[0] == (float)a) return;
    s->v[0] = (float)a;
    glUniform1i_(loc, a);
}

/* FBO state for backdrop-filter: blur() */
static GLuint g_blur_fbo[2]  = {0, 0};
static GLuint g_blur_tex[2]  = {0, 0};
static int    g_blur_tex_w   = 0;
static int    g_blur_tex_h   = 0;

// Cached uniform locations
static struct {
    GLint uResolution, uPos, uSize, uColor, uBorderColor, uBorderWidth, uRadius4;
    GLint uGradient, uGradStopCount, uGradAngle, uGradCenter, uGradRadius;
    GLint uGradRadRx, uGradRadRy;
    GLint uGradColors[MAX_GRAD_STOPS], uGradStops[MAX_GRAD_STOPS];
    GLint uFilterMode, uFilterBrightness, uFilterContrast, uFilterSaturate, uFilterHue;
    GLint uClipEnabled, uClipPos, uClipSize, uClipRadius4;
} bg_loc;
/* Last value uploaded to each bg_program uniform.  Laid out to mirror bg_loc so
 * `luna_uni_invalidate` can clear the whole block in one sweep. */
static struct {
    LunaUniShadow uResolution, uPos, uSize, uColor, uBorderColor, uBorderWidth, uRadius4;
    LunaUniShadow uGradient, uGradStopCount, uGradAngle, uGradCenter, uGradRadius;
    LunaUniShadow uGradRadRx, uGradRadRy;
    LunaUniShadow uGradColors[MAX_GRAD_STOPS], uGradStops[MAX_GRAD_STOPS];
    LunaUniShadow uFilterMode, uFilterBrightness, uFilterContrast, uFilterSaturate, uFilterHue;
    LunaUniShadow uClipEnabled, uClipPos, uClipSize, uClipRadius4;
} bg_uni;
/* Global rounded-clip state for bg_fs: set before each draw_rect_full call */
static int   g_bg_clip_enabled = 0;
static float g_bg_clip_pos[2]  = {0, 0};
static float g_bg_clip_size[2] = {0, 0};
static float g_bg_clip_rad4[4] = {0, 0, 0, 0};

static struct {
    GLint uResolution, uPos, uSize;
    GLint uShadowColor, uElemSize, uRadius4, uBlur, uSpread, uInsetMode, uShadowInset, uOffset;
} sh_loc;
static struct {
    LunaUniShadow uResolution, uPos, uSize;
    LunaUniShadow uShadowColor, uElemSize, uRadius4, uBlur, uSpread, uInsetMode, uShadowInset, uOffset;
} sh_uni;
static struct {
    GLint uResolution, textColor;
    GLint uGradMode, uGradStopCount, uGradAngle, uElemBounds;
    GLint uGradColors[MAX_GRAD_STOPS], uGradStops[MAX_GRAD_STOPS];
} tx_loc;
static struct {
    LunaUniShadow uResolution, textColor;
    LunaUniShadow uGradMode, uGradStopCount, uGradAngle, uElemBounds;
    LunaUniShadow uGradColors[MAX_GRAD_STOPS], uGradStops[MAX_GRAD_STOPS];
} tx_uni;
static struct {
    GLint uResolution, uPos, uSize, uRadius, uAlpha, uImage;
} img_loc;
static struct {
    GLint uResolution, uPos, uSize;
    GLint uSrc, uBlurDir, uBlurRadius, uBlurTexSize, uFbSize, uBlurOrigin;
} blur_loc;
static struct {
    GLint uResolution, uPos, uSize, uRadius4;
    GLint uSrc, uBlurTexSize, uFbSize, uBlurOrigin, uSaturate, uBrightness;
} backdrop_loc;

// Texture cache — path → GL texture ID (loaded once, reused)
#define MAX_TEXTURES 64
typedef struct { char path[512]; GLuint tex; } TexEntry;
static TexEntry g_tex_cache[MAX_TEXTURES];
static int g_tex_count = 0;
void* g_window_ptr_ptr = NULL;
static char g_html_base_dir[512] = "ui";
int   drag_target_idx = -1;
float drag_offset_x   = 0, drag_offset_y = 0;
static int    g_scroll_drag_idx = -1;
static int    g_scroll_drag_axis = 0; /* 0=vertical 1=horizontal */
static float  g_scroll_drag_off = 0.0f;
static int    g_scroll_hover_idx = -1;
static unsigned g_pointer_visual_revision = 1;
static int    g_scroll_hover_axis = -1; /* -1=none 0=vertical 1=horizontal */
static int    g_drag_moved = 0;
static int    g_drag_mode  = 0;
static double g_press_x = 0, g_press_y = 0;
static int    g_focused_idx = -1;
static int    g_focused_element_idx = -1;
static int    g_focus_before_trap = -1;

#define LUNA_MAX_FOCUS_TRAPS 8
typedef struct {
    int idx;
    LunaTrapDismissFn on_dismiss;
    int backdrop_dismiss;
} LunaFocusTrapEntry;
static LunaFocusTrapEntry g_focus_traps[LUNA_MAX_FOCUS_TRAPS];
static int g_focus_trap_count = 0;
static LunaMouseReleaseHook g_mouse_release_hook = NULL;
static int g_luna_last_click_button = LUNA_MOUSE_BUTTON_LEFT;
static int g_luna_last_click_mods = 0;
static int    g_focus_via_keyboard = 0;
static char   g_a11y_live_msg[256];
static double g_a11y_live_until = 0.0;
static int    g_a11y_live_assertive = 0;
static int    g_top_z = 100;
#define DRAG_THRESHOLD 4.0

#ifdef LUNA_UI_GLFW
GLFWcursor* g_hand_cursor       = NULL;
GLFWcursor* g_cursor_ibeam      = NULL;
GLFWcursor* g_cursor_crosshair  = NULL;
GLFWcursor* g_cursor_hresize    = NULL;
GLFWcursor* g_cursor_vresize    = NULL;
#endif
int         g_current_cursor    = -1;

GLuint text_vao, text_vbo;
int font_loaded      = 0;
int bold_font_loaded = 0;

/* Max glyphs per batched text draw (one line). */
#define LUNA_TEXT_BATCH_GLYPHS 256

/* Active CSS letter-spacing for text measurement/drawing. Set by render_text_fx
   (and the intrinsic-width measure path) before using the text helpers. */
static float g_text_letter_spacing = 0.0f;
/* Exact CSS pixel size for dynamic glyphs.  The regular ASCII atlas remains
 * bucketed for speed, while icon/CJK glyphs no longer inherit that rounding. */
static float g_text_css_px = 0.0f;

/* ---- UTF-8 + dynamic CJK / symbol glyph atlas (CSS text paint path) ---- */
static stbtt_fontinfo g_font_info;
static unsigned char* g_font_ttf = NULL;
static long           g_font_ttf_sz = 0;
static int            g_font_info_ok = 0;
static stbtt_fontinfo g_bold_font_info;
static unsigned char* g_bold_font_ttf = NULL;
static int            g_bold_font_info_ok = 0;
static stbtt_fontinfo g_cjk_font_info;
static unsigned char* g_cjk_font_ttf = NULL;
static int            g_cjk_font_info_ok = 0;
/* Optional monospace face used by code editors and terminals. */
static stbtt_fontinfo g_mono_font_info;
static unsigned char* g_mono_font_ttf = NULL;
static int            g_mono_font_info_ok = 0;
/* LunaSymbols (Font Awesome–derived) — solid UI icons + brand marks. */
static stbtt_fontinfo g_icon_font_info;
static unsigned char* g_icon_font_ttf = NULL;
static int            g_icon_font_info_ok = 0;
static stbtt_fontinfo g_brand_font_info;
static unsigned char* g_brand_font_ttf = NULL;
static int            g_brand_font_info_ok = 0;
/* The CSS-selected icon face for the current text paint.  It is deliberately
 * a tiny integer: changing faces does not add an allocation or a draw call. */
static int            g_font_face_hint = 0;
static int            g_font_bold_hint = 0;
/* Use the exact-size dynamic atlas for ASCII whenever the requested CSS size
 * is not one of the pre-baked sizes, or whenever the framebuffer has a
 * device-pixel ratio above 1.  This avoids scaling a nearby atlas size. */
static int            g_text_dynamic_ascii = 0;

#define LUNA_ASCII_ATLAS_SIZE 512
#define LUNA_DYN_ATLAS_W 1024
#define LUNA_DYN_ATLAS_H 1024
#define LUNA_MAX_DYN_GLYPHS 4096
#define LUNA_GLYPH_HASH 1024

typedef struct {
    int   codepoint;
    int   css_px_q2;   /* CSS pixel size in half-pixel units */
    int   dpr_q2;      /* framebuffer scale in half-step units */
    int   face;        /* CSS face hint; prevents Solid/Brands cache aliasing */
    int   bold;
    float x0, y0, x1, y1;       /* atlas texel bounds */
    float draw_w, draw_h;        /* glyph bitmap size in CSS pixels */
    float xoff, yoff, xadvance;  /* metrics in CSS pixels */
    int   next;
} LunaDynGlyph;

static unsigned char g_dyn_pixels[LUNA_DYN_ATLAS_W * LUNA_DYN_ATLAS_H];
static GLuint g_dyn_tex = 0;
static int    g_dyn_dirty = 0;
/* Rows of the atlas that changed since the last upload, as a half-open range.
 * Baking one new glyph used to re-upload the whole 1 MB atlas; a CJK label or
 * a font-size the shell had not drawn before therefore cost a megabyte of PCIe
 * traffic in the middle of a frame. */
static int    g_dyn_dirty_y0 = 0, g_dyn_dirty_y1 = 0;

static void dyn_mark_rows(int y0, int y1) {
    if (y0 < 0) y0 = 0;
    if (y1 > LUNA_DYN_ATLAS_H) y1 = LUNA_DYN_ATLAS_H;
    if (y1 <= y0) return;
    if (!g_dyn_dirty) { g_dyn_dirty_y0 = y0; g_dyn_dirty_y1 = y1; }
    else {
        if (y0 < g_dyn_dirty_y0) g_dyn_dirty_y0 = y0;
        if (y1 > g_dyn_dirty_y1) g_dyn_dirty_y1 = y1;
    }
    g_dyn_dirty = 1;
}
static int    g_dyn_pack_x = 1, g_dyn_pack_y = 1, g_dyn_pack_row_h = 0;
static LunaDynGlyph g_dyn_glyphs[LUNA_MAX_DYN_GLYPHS];
static int g_dyn_glyph_count = 0;
static int g_dyn_hash[LUNA_GLYPH_HASH];

static int utf8_decode(const char** pp) {
    const unsigned char* s = (const unsigned char*)*pp;
    if (!s || !*s) return 0;
    unsigned char c = s[0];
    if (c < 0x80) { *pp = (const char*)(s + 1); return (int)c; }
    if ((c & 0xE0) == 0xC0 && s[1]) {
        int cp = ((c & 0x1F) << 6) | (s[1] & 0x3F);
        *pp = (const char*)(s + 2); return cp;
    }
    if ((c & 0xF0) == 0xE0 && s[1] && s[2]) {
        int cp = ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        *pp = (const char*)(s + 3); return cp;
    }
    if ((c & 0xF8) == 0xF0 && s[1] && s[2] && s[3]) {
        int cp = ((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        *pp = (const char*)(s + 4); return cp;
    }
    *pp = (const char*)(s + 1);
    return 0xFFFD;
}

static int utf8_prev_boundary(const char* s, int pos) {
    if (pos <= 0) return 0;
    pos--;
    while (pos > 0 && ((unsigned char)s[pos] & 0xC0) == 0x80) pos--;
    return pos;
}

static int utf8_next_boundary(const char* s, int pos) {
    int n = (int)strlen(s);
    if (pos >= n) return n;
    pos++;
    while (pos < n && ((unsigned char)s[pos] & 0xC0) == 0x80) pos++;
    return pos;
}

static int utf8_encode(int cp, char* out) {
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

static int font_path_score(const char* path) {
    char lower[1024];
    size_t n = strlen(path);
    if (n >= sizeof(lower)) n = sizeof(lower) - 1;
    for (size_t i = 0; i < n; i++) lower[i] = (char)tolower((unsigned char)path[i]);
    lower[n] = '\0';
    int score = 0;
    if (strstr(lower, "notosanscjk")) score += 200;
    if (strstr(lower, "notosansjp"))  score += 180;
    if (strstr(lower, "cjk"))         score += 120;
    if (strstr(lower, "/ja/") || strstr(lower, "japanese")) score += 100;
    if (strstr(lower, "noto"))        score += 40;
    if (strstr(lower, "mplus") || strstr(lower, "sourcehansans")) score += 60;
    if (strstr(lower, "dejavu") || strstr(lower, "liberation")) score += 10;
    if (strstr(lower, "emoji") || strstr(lower, "icon") || strstr(lower, "symbol")) score -= 100;
    if (strstr(lower, "lunasymbols") || strstr(lower, "fontawesome") || strstr(lower, "awesome")) score -= 500;
    return score;
}

/* UI sans-serif: match demo.css stack (Inter / system UI), not CJK faces. */
static int font_path_score_mono(const char* path) {
    char lower[1024];
    size_t n = strlen(path);
    if (n >= sizeof(lower)) n = sizeof(lower) - 1;
    for (size_t i = 0; i < n; i++) lower[i] = (char)tolower((unsigned char)path[i]);
    lower[n] = '\0';
    if (strstr(lower, "emoji") || strstr(lower, "icon") || strstr(lower, "symbol")) return -1000;
    int score = 0;
    if (strstr(lower, "jetbrainsmono") || strstr(lower, "jetbrains mono")) score += 600;
    if (strstr(lower, "sfmono") || strstr(lower, "sf mono")) score += 580;
    if (strstr(lower, "cascadiacode") || strstr(lower, "cascadia code")) score += 560;
    if (strstr(lower, "firacode") || strstr(lower, "fira code")) score += 540;
    if (strstr(lower, "ibmplexmono") || strstr(lower, "ibm plex mono")) score += 520;
    if (strstr(lower, "sourcecodpro") || strstr(lower, "source code pro")) score += 500;
    if (strstr(lower, "ubuntumono") || strstr(lower, "ubuntu mono")) score += 480;
    if (strstr(lower, "dejavusansmono") || strstr(lower, "dejavu sans mono")) score += 460;
    if (strstr(lower, "liberationmono") || strstr(lower, "liberation mono")) score += 440;
    if (strstr(lower, "notosansmono") || strstr(lower, "noto sans mono")) score += 420;
    if (score == 0 && strstr(lower, "mono")) score += 100;
    if (strstr(lower, "bold") || strstr(lower, "italic") || strstr(lower, "oblique")) score -= 80;
    return score;
}

static int font_path_score_ui(const char* path) {
    char lower[1024];
    size_t n = strlen(path);
    if (n >= sizeof(lower)) n = sizeof(lower) - 1;
    for (size_t i = 0; i < n; i++) lower[i] = (char)tolower((unsigned char)path[i]);
    lower[n] = '\0';
    if (strstr(lower, "emoji") || strstr(lower, "icon") || strstr(lower, "symbol")) return -1000;
    if (strstr(lower, "lunasymbols") || strstr(lower, "fontawesome") || strstr(lower, "awesome")) return -1000;
    if (strstr(lower, "comic")) return -500;
    if (strstr(lower, "cjk") || strstr(lower, "notosansjp") || strstr(lower, "notosanscjk")) return -1000;
    if (strstr(lower, "serif") && !strstr(lower, "sans")) return -400;
    if (strstr(lower, "mono")) return -400;
    int score = 0;
    if (strstr(lower, "inter")) score += 500;
    if (strstr(lower, "segoeui") || strstr(lower, "segoe ui")) score += 480;
    if (strstr(lower, "liberationsans")) score += 460;
    if (strstr(lower, "dejavusans")) score += 450;
    if (strstr(lower, "notosans-regular")) score += 440;
    if (strstr(lower, "roboto")) score += 420;
    if (strstr(lower, "ubuntu")) score += 400;
    if (strstr(lower, "cantarell")) score += 390;
    if (strstr(lower, "raleway")) score += 430;
    if (strstr(lower, "josefin")) score += 400;
    if (strstr(lower, "hanken")) score += 330;
    if (strstr(lower, "opensans") || strstr(lower, "open sans")) score += 320;
    if (strstr(lower, "arial")) score += 300;
    if (strstr(lower, "helvetica")) score += 290;
    if (score == 0 && strstr(lower, "sans")) score += 80;
    return score;
}

static void dyn_atlas_reset(void) {
    memset(g_dyn_pixels, 0, sizeof(g_dyn_pixels));
    g_dyn_pack_x = 1; g_dyn_pack_y = 1; g_dyn_pack_row_h = 0;
    g_dyn_glyph_count = 0;
    for (int i = 0; i < LUNA_GLYPH_HASH; i++) g_dyn_hash[i] = -1;
    g_dyn_dirty = 0;
    dyn_mark_rows(0, LUNA_DYN_ATLAS_H);   /* the clear itself has to reach GL */
}

static float text_device_scale(void) {
    float sy = (LUNA_RRES_Y > 0.0f && g_luna_fbh > 0)
        ? (float)g_luna_fbh / LUNA_RRES_Y : 1.0f;
    if (sy < 0.5f) sy = 0.5f;
    if (sy > 4.0f) sy = 4.0f;
    return sy;
}

static float text_snap_x(float v) {
    float sx = (LUNA_RRES_X > 0.0f && g_luna_fbw > 0)
        ? (float)g_luna_fbw / LUNA_RRES_X : 1.0f;
    if (sx <= 0.0f) return v;
    return floorf(v * sx + 0.5f) / sx;
}

static float text_snap_y(float v) {
    float sy = (LUNA_RRES_Y > 0.0f && g_luna_fbh > 0)
        ? (float)g_luna_fbh / LUNA_RRES_Y : 1.0f;
    if (sy <= 0.0f) return v;
    return floorf(v * sy + 0.5f) / sy;
}

static unsigned dyn_glyph_hash(int cp, int css_px_q2, int dpr_q2) {
    return (unsigned)((cp * 2654435761u) ^
                      (unsigned)(css_px_q2 * 97) ^
                      (unsigned)(dpr_q2 * 389) ^
                      (unsigned)(g_font_face_hint * 193) ^
                      (unsigned)(g_font_bold_hint * 769)) % LUNA_GLYPH_HASH;
}

static LunaDynGlyph* dyn_find_glyph(int cp, int css_px_q2, int dpr_q2) {
    unsigned h = dyn_glyph_hash(cp, css_px_q2, dpr_q2);
    for (int i = g_dyn_hash[h]; i >= 0; i = g_dyn_glyphs[i].next) {
        LunaDynGlyph* g = &g_dyn_glyphs[i];
        if (g->codepoint == cp && g->css_px_q2 == css_px_q2 &&
            g->dpr_q2 == dpr_q2 && g->face == g_font_face_hint &&
            g->bold == g_font_bold_hint)
            return g;
    }
    return NULL;
}

static int font_has_cp(const stbtt_fontinfo* fi, int ok, int cp) {
    return ok && fi && stbtt_FindGlyphIndex(fi, cp) != 0;
}

static int codepoint_prefers_cjk(int cp) {
    return (cp >= 0x2E80 && cp <= 0x30FF) ||
           (cp >= 0x31F0 && cp <= 0x31FF) ||
           (cp >= 0x3400 && cp <= 0x9FFF) ||
           (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0xFF00 && cp <= 0xFFEF) ||
           (cp >= 0x20000 && cp <= 0x323AF);
}

/* Pick a face that actually contains `cp`. LunaSymbols win for the selected
 * icon family and for PUA codepoints. Latin/ASCII keep the UI font, while
 * Japanese/CJK ranges prefer the CJK face. This matters when exact-size ASCII
 * is moved to the dynamic atlas on a HiDPI output: it must not silently switch
 * from the UI font to Noto CJK. */
static stbtt_fontinfo* font_for_codepoint(int cp) {
    int pua = (cp >= 0xE000 && cp <= 0xF8FF);
    int prefer_cjk = codepoint_prefers_cjk(cp);
    int icon_has  = font_has_cp(&g_icon_font_info,  g_icon_font_info_ok,  cp);
    int brand_has = font_has_cp(&g_brand_font_info, g_brand_font_info_ok, cp);
    int bold_has  = font_has_cp(&g_bold_font_info,  g_bold_font_info_ok,  cp);
    int ui_has    = font_has_cp(&g_font_info,       g_font_info_ok,       cp);
    int cjk_has   = font_has_cp(&g_cjk_font_info,   g_cjk_font_info_ok,   cp);
    int mono_has  = font_has_cp(&g_mono_font_info,  g_mono_font_info_ok,  cp);

    /* CSS font-family must win when both icon faces expose a codepoint. */
    if (g_font_face_hint == 1 && icon_has) return &g_icon_font_info;
    if (g_font_face_hint == 2 && brand_has) return &g_brand_font_info;
    if (g_font_face_hint == 3 && mono_has) return &g_mono_font_info;
    if (pua) {
        if (icon_has)  return &g_icon_font_info;
        if (brand_has) return &g_brand_font_info;
    }
    if (!prefer_cjk) {
        if (g_font_bold_hint && bold_has) return &g_bold_font_info;
        if (ui_has)  return &g_font_info;
        if (cjk_has) return &g_cjk_font_info;
    } else {
        if (cjk_has) return &g_cjk_font_info;
        if (g_font_bold_hint && bold_has) return &g_bold_font_info;
        if (ui_has) return &g_font_info;
    }
    if (icon_has)  return &g_icon_font_info;
    if (brand_has) return &g_brand_font_info;
    if (g_font_bold_hint && g_bold_font_info_ok) return &g_bold_font_info;
    if (prefer_cjk && g_cjk_font_info_ok) return &g_cjk_font_info;
    if (g_font_face_hint == 3 && g_mono_font_info_ok) return &g_mono_font_info;
    if (g_font_info_ok) return &g_font_info;
    if (g_cjk_font_info_ok) return &g_cjk_font_info;
    if (g_icon_font_info_ok) return &g_icon_font_info;
    if (g_brand_font_info_ok) return &g_brand_font_info;
    return NULL;
}

static LunaDynGlyph* dyn_bake_glyph(int cp, float css_px) {
    stbtt_fontinfo* finfo = font_for_codepoint(cp);
    if (!finfo) return NULL;
    if (css_px < 4.0f) return NULL;

    float dpr = text_device_scale();
    int css_px_q2 = (int)floorf(css_px * 2.0f + 0.5f);
    int dpr_q2 = (int)floorf(dpr * 2.0f + 0.5f);
    if (css_px_q2 < 8) css_px_q2 = 8;
    if (dpr_q2 < 1) dpr_q2 = 1;
    dpr = (float)dpr_q2 * 0.5f;
    css_px = (float)css_px_q2 * 0.5f;

    LunaDynGlyph* hit = dyn_find_glyph(cp, css_px_q2, dpr_q2);
    if (hit) return hit;
    if (g_dyn_glyph_count >= LUNA_MAX_DYN_GLYPHS) {
        dyn_atlas_reset();
    }

    /* Rasterise at framebuffer resolution, but store all layout metrics in
     * CSS pixels.  On a scale-2 Wayland output this produces a true 2x glyph
     * instead of asking the compositor or GL_LINEAR to enlarge a 1x bitmap. */
    float raster_px = css_px * dpr;
    float raster_scale = stbtt_ScaleForMappingEmToPixels(finfo, raster_px);
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(finfo, &ascent, &descent, &lineGap);
    int advance, lsb;
    stbtt_GetCodepointHMetrics(finfo, cp, &advance, &lsb);
    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(finfo, cp, raster_scale, raster_scale,
                                &x0, &y0, &x1, &y1);
    int gw = x1 - x0;
    int gh = y1 - y0;
    if (gw < 1) gw = 1;
    if (gh < 1) gh = 1;
    int pad = 1;
    int need_w = gw + pad * 2;
    int need_h = gh + pad * 2;
    if (g_dyn_pack_x + need_w >= LUNA_DYN_ATLAS_W) {
        g_dyn_pack_x = 1;
        g_dyn_pack_y += g_dyn_pack_row_h + 1;
        g_dyn_pack_row_h = 0;
    }
    if (g_dyn_pack_y + need_h >= LUNA_DYN_ATLAS_H) {
        dyn_atlas_reset();
    }
    if (g_dyn_pack_y + need_h >= LUNA_DYN_ATLAS_H) return NULL;
    if (need_h > g_dyn_pack_row_h) g_dyn_pack_row_h = need_h;

    int ax = g_dyn_pack_x + pad;
    int ay = g_dyn_pack_y + pad;
    stbtt_MakeCodepointBitmap(finfo, &g_dyn_pixels[ay * LUNA_DYN_ATLAS_W + ax],
                              gw, gh, LUNA_DYN_ATLAS_W,
                              raster_scale, raster_scale, cp);

    LunaDynGlyph* g = &g_dyn_glyphs[g_dyn_glyph_count];
    g->codepoint = cp;
    g->css_px_q2 = css_px_q2;
    g->dpr_q2 = dpr_q2;
    g->face = g_font_face_hint;
    g->bold = g_font_bold_hint;
    g->x0 = (float)ax; g->y0 = (float)ay;
    g->x1 = (float)(ax + gw); g->y1 = (float)(ay + gh);
    g->draw_w = (float)gw / dpr;
    g->draw_h = (float)gh / dpr;
    g->xoff = (float)x0 / dpr;
    g->yoff = (float)y0 / dpr;
    g->xadvance = (float)advance * raster_scale / dpr;
    unsigned h = dyn_glyph_hash(cp, css_px_q2, dpr_q2);
    g->next = g_dyn_hash[h];
    g_dyn_hash[h] = g_dyn_glyph_count;
    g_dyn_glyph_count++;
    g_dyn_pack_x += need_w + 1;
    dyn_mark_rows(ay, ay + gh);
    return g;
}

static void dyn_flush_atlas(void) {
    if (!g_dyn_tex) {
        glGenTextures(1, &g_dyn_tex);
        glBindTexture(GL_TEXTURE_2D, g_dyn_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, LUNA_DYN_ATLAS_W, LUNA_DYN_ATLAS_H, 0,
                     GL_RED, GL_UNSIGNED_BYTE, g_dyn_pixels);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        g_dyn_dirty = 0;
        return;
    }
    if (!g_dyn_dirty) return;
    /* Rows are contiguous at the atlas stride, so the changed band uploads as
     * one sub-rectangle — typically a few dozen rows rather than 1024. */
    glBindTexture(GL_TEXTURE_2D, g_dyn_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, g_dyn_dirty_y0,
                    LUNA_DYN_ATLAS_W, g_dyn_dirty_y1 - g_dyn_dirty_y0,
                    GL_RED, GL_UNSIGNED_BYTE,
                    &g_dyn_pixels[(size_t)g_dyn_dirty_y0 * LUNA_DYN_ATLAS_W]);
    g_dyn_dirty = 0;
}

// Shared VAO/VBO for rectangle drawing — created once, reused every frame.
static GLuint g_rect_vao = 0, g_rect_vbo = 0;

struct LunaContext {
    GLuint rect_vao;
    GLuint text_vao;
    int root_idx;
    float origin_x, origin_y;
    float region_w, region_h;
};

/*
 * CSS font sizes must not be snapped to a sparse set of atlas sizes.  In
 * particular, the shipped UIs use sizes from 6px skin labels through 9.5px
 * shell labels and 13.5px controls up to a 46px clock; selecting the old
 * nearest atlas (12/16/22/32) visibly changed both
 * glyph metrics and flex intrinsic sizing relative to a browser.  These are
 * the half-pixel CSS sizes used by the shipped UI, baked once at startup and
 * reused by the batched text path on every frame.
 */
#define NUM_FONT_SIZES 25
static const float font_sizes[NUM_FONT_SIZES] = {
    6.0f, 7.0f, 8.0f, 8.5f, 9.0f, 9.5f, 10.0f, 10.5f, 11.0f,
    11.5f, 12.0f, 12.5f, 13.0f, 13.5f,
    14.0f, 15.0f, 16.0f, 18.0f, 19.0f, 20.0f, 22.0f, 24.0f, 25.0f,
    28.0f, 46.0f
};

typedef struct {
    stbtt_bakedchar cdata[96];
    GLuint tex;
    int    loaded;
} FontAtlas;

FontAtlas font_regular[NUM_FONT_SIZES];
FontAtlas font_bold_atlas[NUM_FONT_SIZES];

static float glyph_advance(FontAtlas* atlas, int cp, float css_px) {
    if (cp >= 32 && cp < 128 && !g_text_dynamic_ascii)
        return atlas->cdata[cp - 32].xadvance;
    LunaDynGlyph* g = dyn_bake_glyph(cp, css_px);
    return g ? g->xadvance : css_px * 0.5f;
}

static char g_screenshot_path[512] = {0};
static int g_screenshot_pending = 0;
static int g_layout_dirty = 1;
/* Intrinsic widths are queried repeatedly while nested flex containers are
 * resolved.  Cache them for one layout pass: the DOM/style state is immutable
 * during a pass, and this turns the common nested-flex case from repeated
 * full element scans into one scan per element. */
static float g_intrinsic_width_cache[MAX_ELEMENTS];
static unsigned char g_intrinsic_width_valid[MAX_ELEMENTS];
static int g_render_order_dirty = 1;
static int g_cached_eff_z[MAX_ELEMENTS];

// ============================================================
// Utilities
// ============================================================

char* read_file(const char* filename) {
    FILE* file = fopen(filename, "rb");
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    char* buffer = (char*)malloc(length + 1);
    if (buffer) { fread(buffer, 1, length, file); buffer[length] = '\0'; }
    fclose(file);
    return buffer;
}

unsigned char* read_file_bytes(const char* filename, long* out_size) {
    FILE* file = fopen(filename, "rb");
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    unsigned char* buffer = (unsigned char*)malloc(length);
    if (buffer) fread(buffer, 1, length, file);
    fclose(file);
    if (out_size) *out_size = length;
    return buffer;
}

void trim_whitespace(char* str) {
    if (!str) return;
    char* start = str;
    while (isspace((unsigned char)*start)) start++;
    memmove(str, start, strlen(start) + 1);
    if (!*str) return;
    char* end = str + strlen(str) - 1;
    while (end >= str && isspace((unsigned char)*end)) { *end = '\0'; end--; }
}

// ============================================================
// Color parsing
// ============================================================

typedef struct { const char* name; float r, g, b, a; } NamedColor;
static const NamedColor named_colors[] = {
    {"transparent",  0.00f, 0.00f, 0.00f, 0.00f},
    {"white",        1.00f, 1.00f, 1.00f, 1.00f},
    {"black",        0.00f, 0.00f, 0.00f, 1.00f},
    {"red",          1.00f, 0.00f, 0.00f, 1.00f},
    {"green",        0.00f, 0.50f, 0.00f, 1.00f},
    {"blue",         0.00f, 0.00f, 1.00f, 1.00f},
    {"gray",         0.50f, 0.50f, 0.50f, 1.00f},
    {"grey",         0.50f, 0.50f, 0.50f, 1.00f},
    {"lightgray",    0.83f, 0.83f, 0.83f, 1.00f},
    {"lightgrey",    0.83f, 0.83f, 0.83f, 1.00f},
    {"darkgray",     0.41f, 0.41f, 0.41f, 1.00f},
    {"darkgrey",     0.41f, 0.41f, 0.41f, 1.00f},
    {"orange",       1.00f, 0.65f, 0.00f, 1.00f},
    {"yellow",       1.00f, 1.00f, 0.00f, 1.00f},
    {"purple",       0.50f, 0.00f, 0.50f, 1.00f},
    {"violet",       0.56f, 0.00f, 1.00f, 1.00f},
    {"pink",         1.00f, 0.75f, 0.80f, 1.00f},
    {"cyan",         0.00f, 1.00f, 1.00f, 1.00f},
    {"indigo",       0.29f, 0.00f, 0.51f, 1.00f},
    {"teal",         0.00f, 0.50f, 0.50f, 1.00f},
    {"silver",       0.75f, 0.75f, 0.75f, 1.00f},
    {"coral",        1.00f, 0.50f, 0.31f, 1.00f},
    {"gold",         1.00f, 0.84f, 0.00f, 1.00f},
    {"crimson",      0.86f, 0.08f, 0.24f, 1.00f},
    {"navy",         0.00f, 0.00f, 0.50f, 1.00f},
    {"skyblue",      0.53f, 0.81f, 0.92f, 1.00f},
};

void parse_color(const char* val, float* r, float* g, float* b, float* a) {
    if (!val) return;
    while (isspace((unsigned char)*val)) val++;
    *a = 1.0f;

    if (strcmp(val, "none") == 0) {
        *r = *g = *b = 0.0f; *a = 0.0f;
        return;
    }

    if (val[0] == '#') {
        int len = (int)strlen(val);
        if (len == 4) {
            int rv, gv, bv;
            if (sscanf(val, "#%1x%1x%1x", &rv, &gv, &bv) == 3) {
                *r = (rv * 17) / 255.0f; *g = (gv * 17) / 255.0f; *b = (bv * 17) / 255.0f;
            }
        } else if (len == 7) {
            unsigned int rv, gv, bv;
            if (sscanf(val, "#%2x%2x%2x", &rv, &gv, &bv) == 3) {
                *r = rv / 255.0f; *g = gv / 255.0f; *b = bv / 255.0f;
            }
        } else if (len >= 9) {
            unsigned int rv, gv, bv, av;
            if (sscanf(val, "#%2x%2x%2x%2x", &rv, &gv, &bv, &av) == 4) {
                *r = rv / 255.0f; *g = gv / 255.0f; *b = bv / 255.0f; *a = av / 255.0f;
            }
        }
    } else if (strncmp(val, "rgba(", 5) == 0 || strncmp(val, "rgb(", 4) == 0) {
        const char* paren = strchr(val, '(');
        if (paren) {
            char buf[64] = {0};
            strncpy(buf, paren + 1, sizeof(buf) - 1);
            float vals[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
            int idx = 0;
            char* tok = strtok(buf, ", )");
            while (tok && idx < 4) { vals[idx++] = (float)atof(tok); tok = strtok(NULL, ", )"); }
            if (idx >= 3) {
                *r = vals[0] / 255.0f; *g = vals[1] / 255.0f; *b = vals[2] / 255.0f;
                *a = (idx == 4) ? vals[3] : 1.0f;
            }
        }
    } else if (strncmp(val, "hsla(", 5) == 0 || strncmp(val, "hsl(", 4) == 0) {
        const char* paren = strchr(val, '(');
        if (paren) {
            char buf[64] = {0};
            strncpy(buf, paren + 1, sizeof(buf) - 1);
            float h = 0.0f, s = 0.0f, l = 0.0f, alpha = 1.0f;
            int idx = 0;
            char* tok = strtok(buf, ", )");
            while (tok && idx < 4) {
                float v = (float)atof(tok);
                if (idx == 0) h = v;
                else if (idx == 1) s = v;
                else if (idx == 2) l = v;
                else alpha = v;
                idx++;
                tok = strtok(NULL, ", )");
            }
            if (idx >= 3) {
                s /= 100.0f;
                l /= 100.0f;
                if (s < 0.0f) s = 0.0f;
                if (s > 1.0f) s = 1.0f;
                if (l < 0.0f) l = 0.0f;
                if (l > 1.0f) l = 1.0f;
                float c = (1.0f - fabsf(2.0f * l - 1.0f)) * s;
                float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
                float m = l - c * 0.5f;
                float rr = 0.0f, gg = 0.0f, bb = 0.0f;
                if      (h < 60.0f)  { rr = c; gg = x; }
                else if (h < 120.0f) { rr = x; gg = c; }
                else if (h < 180.0f) { gg = c; bb = x; }
                else if (h < 240.0f) { gg = x; bb = c; }
                else if (h < 300.0f) { rr = x; bb = c; }
                else                 { rr = c; bb = x; }
                *r = rr + m;
                *g = gg + m;
                *b = bb + m;
                *a = (idx == 4) ? alpha : 1.0f;
            }
        }
    } else {
        size_t nc = sizeof(named_colors) / sizeof(named_colors[0]);
        for (size_t i = 0; i < nc; i++) {
            if (strcmp(val, named_colors[i].name) == 0) {
                *r = named_colors[i].r; *g = named_colors[i].g;
                *b = named_colors[i].b; *a = named_colors[i].a;
                break;
            }
        }
    }
}

// ============================================================
// CSS value helpers
// ============================================================

/* Convert a parsed number to px based on the unit that follows it.
   Root font-size is fixed at 16px (rem == em here: no font-relative cascade). */
#define LUNA_REM_PX 16.0f
static float apply_css_unit(float v, const char* unit) {
    if (!unit) return v;
    while (isspace((unsigned char)*unit)) unit++;
    if (strncmp(unit, "rem", 3) == 0) return v * LUNA_REM_PX;
    if (strncmp(unit, "em", 2) == 0)  return v * LUNA_REM_PX;
    if (strncmp(unit, "pt", 2) == 0)  return v * (96.0f / 72.0f);
    return v; /* px (default), %, vw/vh handled by callers */
}

// Parse a CSS numeric value; px passthrough, rem/em/pt converted to px.
static float parse_float_val(const char* v) {
    char* endp = NULL;
    float f = strtof(v, &endp);
    return apply_css_unit(f, endp);
}

static const char* skip_css_unit(const char* p) {
    if (strncmp(p, "rem", 3) == 0) return p + 3;
    if (strncmp(p, "px", 2) == 0 || strncmp(p, "em", 2) == 0 ||
        strncmp(p, "vh", 2) == 0 || strncmp(p, "vw", 2) == 0 ||
        strncmp(p, "pt", 2) == 0 || strncmp(p, "vmin", 4) == 0) return p + 2;
    if (*p == '%') return p + 1;
    return p;
}

static float read_css_length(const char** pp) {
    char* endp;
    float v = strtof(*pp, &endp);
    v = apply_css_unit(v, endp);
    *pp = skip_css_unit(endp);
    while (isspace((unsigned char)**pp)) (*pp)++;
    return v;
}

// Parse length with optional % (stored as 0.0-1.0 ratio when pct).
static void parse_length(const char* val, float* out_num, int* out_pct) {
    char buf[64] = {0};
    strncpy(buf, val, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    /* strip leading whitespace */
    char* st = buf; while (isspace((unsigned char)*st)) st++;
    if (st != buf) memmove(buf, st, strlen(st) + 1);
    int len = (int)strlen(buf);
    while (len > 0 && isspace((unsigned char)buf[len-1])) buf[--len] = '\0';
    *out_pct = 0;
    *out_num = 0.0f;
    if (len > 0 && buf[len-1] == '%') { *out_pct = 1; buf[--len] = '\0'; }
    if (*out_pct) { sscanf(buf, "%f", out_num); *out_num /= 100.0f; return; }
    /* vw/vh map naturally to a fraction of the root (body spans the window) */
    if (len > 2 && strcmp(buf + len - 2, "vw") == 0) { *out_pct = 1; sscanf(buf, "%f", out_num); *out_num /= 100.0f; return; }
    if (len > 2 && strcmp(buf + len - 2, "vh") == 0) { *out_pct = 1; sscanf(buf, "%f", out_num); *out_num /= 100.0f; return; }
    char* endp = NULL;
    *out_num = strtof(buf, &endp);
    *out_num = apply_css_unit(*out_num, endp);
}

/* Parse length including calc(X% +/- Ypx). Returns 1 if calc was parsed. */
static int parse_length_calc(const char* val, float* out_num, int* out_pct, float* out_offset) {
    *out_offset = 0.0f;
    char buf[128] = {0};
    strncpy(buf, val, sizeof(buf) - 1);
    /* strip whitespace */
    char* s = buf; while (isspace((unsigned char)*s)) s++;
    if (s != buf) memmove(buf, s, strlen(s) + 1);
    int bl = (int)strlen(buf);
    while (bl > 0 && isspace((unsigned char)buf[bl-1])) buf[--bl] = '\0';
    if (strncmp(buf, "calc(", 5) == 0) {
        /* find the inner expression */
        const char* inner = buf + 5;
        /* trim trailing ) */
        char expr[96] = {0}; snprintf(expr, sizeof(expr), "%.*s", (int)(sizeof(expr) - 1), inner);
        int el = (int)strlen(expr);
        while (el > 0 && (expr[el-1] == ')' || isspace((unsigned char)expr[el-1]))) expr[--el] = '\0';
        /* parse: X% op Ypx */
        char* endp = NULL;
        float v1 = strtof(expr, &endp);
        if (endp && *endp == '%') {
            *out_pct = 1; *out_num = v1 / 100.0f;
            endp++;
            while (isspace((unsigned char)*endp)) endp++;
            char op = *endp; if (op == '+' || op == '-') endp++;
            while (isspace((unsigned char)*endp)) endp++;
            float v2 = strtof(endp, NULL);
            *out_offset = (op == '-') ? -v2 : v2;
            return 1;
        } else if (endp) {
            /* Npx op X% */
            float px_part = v1;
            while (isspace((unsigned char)*endp)) endp++;
            char op = *endp; if (op == '+' || op == '-') endp++;
            while (isspace((unsigned char)*endp)) endp++;
            float v2 = strtof(endp, &endp);
            if (endp && *endp == '%') {
                *out_pct = 1; *out_num = v2 / 100.0f;
                *out_offset = (op == '-') ? -px_part : px_part;
                return 1;
            }
            /* both px */
            *out_pct = 0; *out_num = (op == '-') ? px_part - v2 : px_part + v2;
            return 1;
        }
        *out_pct = 0; *out_num = 0.0f; return 1;
    }
    /* not calc - use regular parse */
    parse_length(val, out_num, out_pct);
    return 0;
}


// Parse box-shadow: comma-separated multi-shadow list; each layer supports
// "[inset] dx dy [blur [spread]] color" in any inset/color position.
static void parse_box_shadow(const char* val, StyleRule* rule) {
    const char* p = val;
    rule->shadow_count = 0;
    if (strncmp(val, "none", 4) == 0) { rule->has_shadow = 1; return; }
    while (*p && rule->shadow_count < LUNA_MAX_SHADOWS) {
        while (isspace((unsigned char)*p)) p++;
        if (!*p) break;
        /* Find end of this shadow entry (comma at depth 0) */
        const char* shadow_start = p;
        int depth = 0;
        while (*p) {
            if (*p == '(') depth++;
            else if (*p == ')') depth--;
            else if (*p == ',' && depth == 0) break;
            p++;
        }
        char shadow_buf[256] = {0};
        int slen = (int)(p - shadow_start);
        if (slen > 255) slen = 255;
        strncpy(shadow_buf, shadow_start, slen);
        if (*p == ',') p++;

        LunaShadow sh; memset(&sh, 0, sizeof(sh));
        sh.a = 1.0f;

        /* detect + strip "inset" keyword wherever it appears */
        char* ins = strstr(shadow_buf, "inset");
        if (ins && (ins == shadow_buf || isspace((unsigned char)ins[-1])) &&
            (ins[5] == '\0' || isspace((unsigned char)ins[5]))) {
            sh.inset = 1;
            memset(ins, ' ', 5);
        }

        const char* sp = shadow_buf;
        while (isspace((unsigned char)*sp)) sp++;
        if (!*sp) continue;

        sh.dx = read_css_length(&sp);
        sh.dy = read_css_length(&sp);
        if (*sp && (*sp == '-' || *sp == '+' || isdigit((unsigned char)*sp) || *sp == '.'))
            sh.blur = read_css_length(&sp);
        if (*sp && (*sp == '-' || *sp == '+' || isdigit((unsigned char)*sp) || *sp == '.'))
            sh.spread = read_css_length(&sp);
        if (sh.blur < 0.0f) sh.blur = 0.0f;

        /* rest is the color */
        char colbuf[128] = {0};
        strncpy(colbuf, sp, sizeof(colbuf) - 1);
        trim_whitespace(colbuf);
        if (colbuf[0]) {
            parse_color(colbuf, &sh.r, &sh.g, &sh.b, &sh.a);
            rule->shadows[rule->shadow_count++] = sh;
            rule->has_shadow = 1;
        }
    }
}

// Parse border-radius shorthand: 1-4 values → tl, tr, br, bl (CSS rules).
static void parse_border_radius_shorthand(const char* val, StyleRule* rule) {
    const char* p = val;
    float v[4]; int n = 0;
    while (*p && n < 4) {
        while (isspace((unsigned char)*p)) p++;
        if (!*p || *p == '/') break; /* elliptical radii unsupported: use first set */
        v[n++] = read_css_length(&p);
    }
    if (n == 0) return;
    rule->has_radius = 1;
    float tl, tr, br, bl;
    switch (n) {
    case 1:  tl = tr = br = bl = v[0]; break;
    case 2:  tl = br = v[0]; tr = bl = v[1]; break;
    case 3:  tl = v[0]; tr = bl = v[1]; br = v[2]; break;
    default: tl = v[0]; tr = v[1]; br = v[2]; bl = v[3]; break;
    }
    rule->rad_c[0] = tl; rule->rad_c[1] = tr; rule->rad_c[2] = br; rule->rad_c[3] = bl;
    rule->has_rad_c[0] = rule->has_rad_c[1] = rule->has_rad_c[2] = rule->has_rad_c[3] = 1;
    float mx = tl;
    if (tr > mx) mx = tr;
    if (br > mx) mx = br;
    if (bl > mx) mx = bl;
    rule->border_radius = mx;
}

// Parse border shorthand: "1px solid #color" or "none" or "0"
static void parse_border_shorthand(const char* val, StyleRule* rule) {
    rule->has_border = 1;
    if (strcmp(val, "none") == 0 || strcmp(val, "0") == 0) {
        rule->border_width = 0; rule->bd_a = 0; return;
    }
    char buf[256]; strncpy(buf, val, 255); buf[255] = 0;
    char* p = buf;
    // width
    char* endp;
    float bw = strtof(p, &endp);
    if (endp != p) { rule->border_width = bw; p = endp; }
    // skip "px" or other unit
    while (*p && !isspace((unsigned char)*p) && *p != '#' && *p != 'r') p++;
    while (isspace((unsigned char)*p)) p++;
    // style keyword (solid/dashed/dotted/none)
    if (strncmp(p, "none", 4) == 0) { rule->border_width = 0; return; }
    if (strncmp(p, "solid", 5) == 0 || strncmp(p, "dashed", 6) == 0 || strncmp(p, "dotted", 6) == 0) {
        while (*p && !isspace((unsigned char)*p)) p++;
        while (isspace((unsigned char)*p)) p++;
    }
    // color
    if (*p) parse_color(p, &rule->bd_r, &rule->bd_g, &rule->bd_b, &rule->bd_a);
}

// Copy gradient stops into rule/element
static void apply_gradient_rule(StyleRule* rule, int type, float angle,
                                float rcx, float rcy, float rr) {
    rule->has_gradient = 1;
    rule->grad_type = type;
    rule->grad_angle = angle;
    rule->grad_rad_cx = rcx;
    rule->grad_rad_cy = rcy;
    rule->grad_rad_r = rr;
    rule->has_bg = 1;
    if (rule->grad_stop_count > 0) {
        rule->bg_r = rule->grad_stop_r[0];
        rule->bg_g = rule->grad_stop_g[0];
        rule->bg_b = rule->grad_stop_b[0];
        rule->bg_a = rule->grad_stop_a[0];
    }
}

// Parse comma-separated color stops after gradient header
static int parse_gradient_stops(const char** p_in, StyleRule* rule) {
    const char* p = *p_in;
    int count = 0;
    while (*p && *p != ')' && count < MAX_GRAD_STOPS) {
        while (isspace((unsigned char)*p) || *p == ',') p++;
        if (*p == ')' || !*p) break;

        const char* start = p;
        int depth = 0;
        while (*p && !(*p == ',' && depth == 0)) {
            if (*p == '(') depth++;
            else if (*p == ')') { if (depth == 0) break; depth--; }
            p++;
        }
        char token[96] = {0};
        int len = (int)(p - start);
        if (len > 95) len = 95;
        strncpy(token, start, len);
        trim_whitespace(token);

        char color_buf[96] = {0};
        float pos = -1.0f;
        /* A stop position is separated from the color by whitespace at the
           top level.  strrchr() also found spaces inside rgba()/hsl(), so
           `rgba(141, 123, 255, 0.38)` was split before its alpha component and
           became an opaque color. */
        char* sp = NULL;
        int token_depth = 0;
        for (char* q = token; *q; q++) {
            if (*q == '(') token_depth++;
            else if (*q == ')' && token_depth > 0) token_depth--;
            else if (isspace((unsigned char)*q) && token_depth == 0) sp = q;
        }
        if (sp) {
            char posbuf[24] = {0};
            strncpy(posbuf, sp + 1, 23);
            trim_whitespace(posbuf);
            int plen = (int)strlen(posbuf);
            char* pos_end = NULL;
            float parsed_pos = strtof(posbuf, &pos_end);
            while (pos_end && isspace((unsigned char)*pos_end)) pos_end++;
            int is_percent = pos_end && *pos_end == '%' && pos_end[1] == '\0';
            int is_unitless_zero = pos_end && *pos_end == '\0' && parsed_pos == 0.0f;
            if (plen > 0 && (is_percent || is_unitless_zero)) {
                pos = is_percent ? parsed_pos / 100.0f : 0.0f;
                int clen = (int)(sp - token);
                if (clen > (int)sizeof(color_buf) - 1)
                    clen = (int)sizeof(color_buf) - 1;
                strncpy(color_buf, token, clen);
                color_buf[clen] = '\0';
                trim_whitespace(color_buf);
            } else {
                snprintf(color_buf, sizeof(color_buf), "%s", token);
            }
        } else {
            snprintf(color_buf, sizeof(color_buf), "%s", token);
        }

        parse_color(color_buf,
                    &rule->grad_stop_r[count], &rule->grad_stop_g[count],
                    &rule->grad_stop_b[count], &rule->grad_stop_a[count]);
        rule->grad_stop_pos[count] = pos;
        count++;
        if (*p == ',') p++;
    }
    rule->grad_stop_count = count;
    if (count >= 2) {
        int all_auto = 1;
        for (int i = 0; i < count; i++) {
            if (rule->grad_stop_pos[i] >= 0.0f) all_auto = 0;
        }
        for (int i = 0; i < count; i++) {
            if (rule->grad_stop_pos[i] < 0.0f || all_auto)
                rule->grad_stop_pos[i] = (float)i / (float)(count - 1);
        }
    }
    *p_in = p;
    return count;
}

// Parse linear-gradient(angle, stop1, stop2, ...)
static void parse_linear_gradient(const char* val, StyleRule* rule) {
    const char* p = strchr(val, '(');
    if (!p) return;
    p++;
    while (isspace((unsigned char)*p)) p++;

    float angle = 180.0f;
    if (strncmp(p, "to ", 3) == 0) {
        if (strstr(p, "top"))         angle = 0.0f;
        else if (strstr(p, "right"))  angle = 90.0f;
        else if (strstr(p, "bottom")) angle = 180.0f;
        else if (strstr(p, "left"))   angle = 270.0f;
        const char* comma = strchr(p, ',');
        if (!comma) return;
        p = comma + 1;
    } else if (isdigit((unsigned char)*p) || *p == '-' || *p == '.') {
        char* endp;
        angle = strtof(p, &endp);
        p = endp;
        if (strncmp(p, "deg", 3) == 0) p += 3;
        while (isspace((unsigned char)*p)) p++;
        if (*p == ',') p++;
    }
    while (isspace((unsigned char)*p)) p++;

    memset(rule->grad_stop_pos, 0, sizeof(rule->grad_stop_pos));
    parse_gradient_stops(&p, rule);
    if (rule->grad_stop_count < 2) return;

    apply_gradient_rule(rule, GRAD_LINEAR, angle * (float)M_PI / 180.0f, 0.5f, 0.5f, 0.75f);
}

// Parse radial-gradient(shape at cx cy, stops...)
// Handles: circle at x% y%, ellipse at x% y%, ellipse Wpx Hpx at x% y%
static void parse_radial_gradient(const char* val, StyleRule* rule) {
    const char* p = strchr(val, '(');
    if (!p) return;
    p++;
    while (isspace((unsigned char)*p)) p++;

    float cx = 0.5f, cy = 0.5f, radius = 0.75f;
    float rx = 0.0f, ry = 0.0f; /* ellipse radii (fraction if pct, pixels otherwise) */
    int rx_pct = 0, ry_pct = 0;
    int is_ellipse = 0;

    if (strncmp(p, "ellipse", 7) == 0) {
        is_ellipse = 1;
        p += 7;
        while (isspace((unsigned char)*p)) p++;
        /* Check for explicit size: ellipse W% H% at ... or ellipse Wpx Hpx at ... */
        if (*p != 'a' && *p != ',') {
            char* endp;
            float v1 = strtof(p, &endp);
            if (endp != p) {
                int pct1 = (*endp == '%');
                /* skip unit (px, %, em...) */
                while (*endp && !isspace((unsigned char)*endp) && *endp != ',') endp++;
                p = endp;
                while (isspace((unsigned char)*p)) p++;
                float v2 = strtof(p, &endp);
                if (endp != p) {
                    int pct2 = (*endp == '%');
                    while (*endp && !isspace((unsigned char)*endp) && *endp != ',') endp++;
                    p = endp;
                    rx = pct1 ? v1 / 100.0f : v1;
                    ry = pct2 ? v2 / 100.0f : v2;
                    rx_pct = pct1; ry_pct = pct2;
                }
            }
        }
        while (isspace((unsigned char)*p)) p++;
    } else if (strncmp(p, "circle", 6) == 0) {
        p += 6;
        while (isspace((unsigned char)*p)) p++;
        /* skip optional radius */
        if (*p != 'a' && *p != ',') {
            char* endp;
            (void)strtof(p, &endp);
            if (endp != p) {
                while (*endp && !isspace((unsigned char)*endp) && *endp != ',') endp++;
                p = endp;
            }
        }
        while (isspace((unsigned char)*p)) p++;
    }

    if (strncmp(p, "at ", 3) == 0) {
        p += 3;
        while (isspace((unsigned char)*p)) p++;
        if (strncmp(p, "center", 6) == 0) {
            cx = 0.5f; cy = 0.5f;
            p += 6;
            while (isspace((unsigned char)*p)) p++;
            if (*p == ',') p++;
        } else {
            char* endp;
            float vx = strtof(p, &endp);
            int pct_x = 0;
            p = endp;
            if (*p == '%') { pct_x = 1; p++; }
            else { /* px */ while (*p && *p != ' ' && *p != ',') p++; }
            while (isspace((unsigned char)*p)) p++;
            float vy = strtof(p, &endp);
            int pct_y = 0;
            p = endp;
            if (*p == '%') { pct_y = 1; p++; }
            else { while (*p && *p != ',' ) p++; }
            cx = pct_x ? vx / 100.0f : 0.5f;
            cy = pct_y ? vy / 100.0f : 0.5f;
            while (isspace((unsigned char)*p)) p++;
            if (*p == ',') p++;
        }
    } else if (*p == ',') {
        p++;
    }
    while (isspace((unsigned char)*p)) p++;

    memset(rule->grad_stop_pos, 0, sizeof(rule->grad_stop_pos));
    parse_gradient_stops(&p, rule);
    if (rule->grad_stop_count < 2) return;

    if (is_ellipse && (rx > 0.0f || ry > 0.0f)) {
        /* Explicit ellipse radii: use GRAD_ELLIPSE */
        rule->has_gradient = 1;
        rule->grad_type = GRAD_ELLIPSE;
        rule->grad_angle = 0.0f;
        rule->grad_rad_cx = cx;
        rule->grad_rad_cy = cy;
        rule->grad_rad_r  = 0.75f;
        rule->grad_rad_rx = rx;
        rule->grad_rad_ry = ry;
        rule->grad_rad_rx_pct = rx_pct;
        rule->grad_rad_ry_pct = ry_pct;
    } else if (is_ellipse) {
        /* Ellipse without explicit radii: use GRAD_ELLIPSE with rx=ry=0 (shader uses element size) */
        rule->has_gradient = 1;
        rule->grad_type = GRAD_ELLIPSE;
        rule->grad_angle = 0.0f;
        rule->grad_rad_cx = cx;
        rule->grad_rad_cy = cy;
        rule->grad_rad_r  = 0.75f;
        rule->grad_rad_rx = 0.0f;
        rule->grad_rad_ry = 0.0f;
    } else {
        apply_gradient_rule(rule, GRAD_RADIAL, 0.0f, cx, cy, radius);
    }
}

// Parse conic-gradient(from Adeg at x% y%, stop1, stop2, ...)
static void parse_conic_gradient(const char* val, StyleRule* rule) {
    const char* p = strchr(val, '(');
    if (!p) return;
    p++;
    while (isspace((unsigned char)*p)) p++;

    float from_angle = 0.0f;
    float cx = 0.5f, cy = 0.5f;

    if (strncmp(p, "from ", 5) == 0) {
        p += 5;
        while (isspace((unsigned char)*p)) p++;
        char* endp;
        from_angle = strtof(p, &endp);
        p = endp;
        if (strncmp(p, "deg", 3) == 0) p += 3;
        while (isspace((unsigned char)*p)) p++;
        if (strncmp(p, "at ", 3) == 0) {
            p += 3;
            while (isspace((unsigned char)*p)) p++;
            float vx = strtof(p, &endp); p = endp;
            if (*p == '%') p++;
            while (isspace((unsigned char)*p)) p++;
            float vy = strtof(p, &endp); p = endp;
            if (*p == '%') p++;
            cx = vx / 100.0f; cy = vy / 100.0f;
        }
        while (isspace((unsigned char)*p)) p++;
        if (*p == ',') p++;
    } else if (strncmp(p, "at ", 3) == 0) {
        p += 3;
        char* endp;
        float vx = strtof(p, &endp); p = endp;
        if (*p == '%') p++;
        while (isspace((unsigned char)*p)) p++;
        float vy = strtof(p, &endp); p = endp;
        if (*p == '%') p++;
        cx = vx / 100.0f; cy = vy / 100.0f;
        while (isspace((unsigned char)*p)) p++;
        if (*p == ',') p++;
    }
    while (isspace((unsigned char)*p)) p++;

    memset(rule->grad_stop_pos, 0, sizeof(rule->grad_stop_pos));
    parse_gradient_stops(&p, rule);
    if (rule->grad_stop_count < 2) return;

    float angle_rad = from_angle * (float)M_PI / 180.0f;
    rule->has_gradient = 1;
    rule->grad_type    = GRAD_CONIC;
    rule->grad_angle   = angle_rad;
    rule->grad_rad_cx  = cx;
    rule->grad_rad_cy  = cy;
    rule->grad_rad_r   = 0.75f;
    rule->grad_rad_rx  = 0.0f;
    rule->grad_rad_ry  = 0.0f;
}

// Parse url(...) helper: extracts the path into out_path (max len), returns 1 on success
static int parse_url(const char* val, char* out_path, int max_len) {
    const char* up = strstr(val, "url(");
    if (!up) return 0;
    up += 4;
    while (*up == ' ' || *up == '\'' || *up == '"') up++;
    const char* ue = strpbrk(up, "'\") ");
    if (!ue) ue = up + strlen(up);
    int ulen = (int)(ue - up);
    if (ulen <= 0 || ulen >= max_len) return 0;
    strncpy(out_path, up, (size_t)ulen);
    out_path[ulen] = '\0';
    return 1;
}

/* Copy gradient info from a StyleRule into a LunaBgLayer */
static void rule_to_bg_layer(const StyleRule* rule, LunaBgLayer* layer) {
    memset(layer, 0, sizeof(*layer));
    layer->has_gradient = rule->has_gradient;
    layer->grad_type    = rule->grad_type;
    layer->grad_stop_count = rule->grad_stop_count;
    layer->grad_angle   = rule->grad_angle;
    layer->grad_rad_cx  = rule->grad_rad_cx;
    layer->grad_rad_cy  = rule->grad_rad_cy;
    layer->grad_rad_r   = rule->grad_rad_r;
    layer->grad_rad_rx  = rule->grad_rad_rx;
    layer->grad_rad_ry  = rule->grad_rad_ry;
    layer->grad_rad_rx_pct = rule->grad_rad_rx_pct;
    layer->grad_rad_ry_pct = rule->grad_rad_ry_pct;
    for (int i = 0; i < rule->grad_stop_count && i < MAX_GRAD_STOPS; i++) {
        layer->grad_stop_pos[i] = rule->grad_stop_pos[i];
        layer->grad_stop_r[i]   = rule->grad_stop_r[i];
        layer->grad_stop_g[i]   = rule->grad_stop_g[i];
        layer->grad_stop_b[i]   = rule->grad_stop_b[i];
        layer->grad_stop_a[i]   = rule->grad_stop_a[i];
    }
    layer->has_color = rule->has_bg;
    layer->r = rule->bg_r; layer->g = rule->bg_g;
    layer->b = rule->bg_b; layer->a = rule->bg_a;
    layer->has_bg_image = rule->has_bg_image;
    if (rule->has_bg_image)
        snprintf(layer->image_path, sizeof(layer->image_path), "%s", rule->bg_image_path);
}

/* Split a CSS value string on top-level commas (commas inside () are not separators).
   Returns number of pieces; fills pieces[] with null-terminated strings into buf. */
static int split_top_level_commas(const char* val, char* buf, int bufsz,
                                   char* pieces[], int max_pieces) {
    snprintf(buf, bufsz, "%s", val);
    int count = 0;
    char* p = buf;
    char* start = p;
    int depth = 0;
    while (*p && count < max_pieces - 1) {
        if (*p == '(') depth++;
        else if (*p == ')') { if (depth > 0) depth--; }
        else if (*p == ',' && depth == 0) {
            *p = '\0';
            pieces[count++] = start;
            start = p + 1;
            while (*start == ' ') start++;
        }
        p++;
    }
    pieces[count++] = start;
    return count;
}

// Parse background shorthand: color, gradient, or url(...)
// Supports comma-separated multiple background layers.
static void parse_background_shorthand(const char* val, StyleRule* rule) {
    char buf[1024];
    strncpy(buf, val, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    trim_whitespace(buf);
    rule->has_bg_reset = 1;
    if (strcmp(buf, "none") == 0) {
        /* `background: none` is a reset, not an absent declaration.  Keep a
         * transparent background declaration so the cascade clears an earlier
         * gradient/image (notably the final About title rule). */
        rule->has_bg = 1;
        rule->bg_r = rule->bg_g = rule->bg_b = rule->bg_a = 0.0f;
        rule->has_gradient = 0;
        rule->bg_layer_count = 0;
        rule->has_bg_image = 0;
        rule->bg_image_path[0] = '\0';
        return;
    }

    /* Split into layers at top-level commas */
    char layer_buf[1024];
    char* pieces[LUNA_MAX_BG_LAYERS + 1];
    int n = split_top_level_commas(buf, layer_buf, sizeof(layer_buf), pieces, LUNA_MAX_BG_LAYERS + 1);

    if (n <= 1) {
        /* Single layer — fast path, keep existing behaviour */
        if (strncmp(buf, "url(", 4) == 0) {
            char path[256];
            if (parse_url(buf, path, sizeof(path))) {
                rule->has_bg_image = 1;
                strncpy(rule->bg_image_path, path, sizeof(rule->bg_image_path) - 1);
                rule->bg_image_path[sizeof(rule->bg_image_path) - 1] = '\0';
            }
            return;
        }
        if (strncmp(buf, "linear-gradient", 15) == 0) {
            parse_linear_gradient(buf, rule);
        } else if (strncmp(buf, "radial-gradient", 15) == 0) {
            parse_radial_gradient(buf, rule);
        } else if (strncmp(buf, "conic-gradient", 14) == 0) {
            parse_conic_gradient(buf, rule);
        } else {
            rule->has_bg = 1;
            parse_color(buf, &rule->bg_r, &rule->bg_g, &rule->bg_b, &rule->bg_a);
        }
        return;
    }

    /* Multiple layers */
    rule->bg_layer_count = 0;
    for (int li = 0; li < n && li < LUNA_MAX_BG_LAYERS; li++) {
        char* piece = pieces[li];
        trim_whitespace(piece);
        if (!piece[0]) continue;

        /* Parse into a temporary rule to capture gradient info */
        StyleRule tmp; memset(&tmp, 0, sizeof(tmp));
        if (strncmp(piece, "linear-gradient", 15) == 0) {
            parse_linear_gradient(piece, &tmp);
        } else if (strncmp(piece, "radial-gradient", 15) == 0) {
            parse_radial_gradient(piece, &tmp);
        } else if (strncmp(piece, "conic-gradient", 14) == 0) {
            parse_conic_gradient(piece, &tmp);
        } else if (strncmp(piece, "url(", 4) == 0) {
            char path[256];
            if (parse_url(piece, path, sizeof(path))) {
                tmp.has_bg_image = 1;
                snprintf(tmp.bg_image_path, sizeof(tmp.bg_image_path), "%s", path);
            }
        } else {
            tmp.has_bg = 1;
            parse_color(piece, &tmp.bg_r, &tmp.bg_g, &tmp.bg_b, &tmp.bg_a);
        }
        rule_to_bg_layer(&tmp, &rule->bg_layers[rule->bg_layer_count++]);
    }
    /* For compatibility, also set the primary gradient from the LAST layer
       (bottom-most, per CSS stacking order — layers are listed front-to-back). */
    if (rule->bg_layer_count > 0) {
        LunaBgLayer* last = &rule->bg_layers[rule->bg_layer_count - 1];
        if (last->has_gradient) {
            rule->has_gradient  = last->has_gradient;
            rule->grad_type     = last->grad_type;
            rule->grad_stop_count = last->grad_stop_count;
            rule->grad_angle    = last->grad_angle;
            rule->grad_rad_cx   = last->grad_rad_cx;
            rule->grad_rad_cy   = last->grad_rad_cy;
            rule->grad_rad_r    = last->grad_rad_r;
            rule->grad_rad_rx   = last->grad_rad_rx;
            rule->grad_rad_ry   = last->grad_rad_ry;
            rule->grad_rad_rx_pct = last->grad_rad_rx_pct;
            rule->grad_rad_ry_pct = last->grad_rad_ry_pct;
            for (int s = 0; s < last->grad_stop_count && s < MAX_GRAD_STOPS; s++) {
                rule->grad_stop_pos[s] = last->grad_stop_pos[s];
                rule->grad_stop_r[s]   = last->grad_stop_r[s];
                rule->grad_stop_g[s]   = last->grad_stop_g[s];
                rule->grad_stop_b[s]   = last->grad_stop_b[s];
                rule->grad_stop_a[s]   = last->grad_stop_a[s];
            }
        } else if (last->has_color) {
            rule->has_bg = 1;
            rule->bg_r = last->r; rule->bg_g = last->g;
            rule->bg_b = last->b; rule->bg_a = last->a;
        }
    }
}

static void parse_padding_shorthand(const char* val, StyleRule* rule) {
    float vals[4] = {0, 0, 0, 0};
    char buf[128];
    strncpy(buf, val, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    int count = 0;
    char* tok = strtok(buf, " \t");
    while (tok && count < 4) {
        vals[count++] = parse_float_val(tok);
        tok = strtok(NULL, " \t");
    }
    rule->has_padding = 1;
    /* CSS shorthand: 1=all, 2=(v h), 3=(t h b), 4=(t r b l) */
    if (count == 1) {
        rule->pad_t = rule->pad_r = rule->pad_b = rule->pad_l = vals[0];
    } else if (count == 2) {
        rule->pad_t = rule->pad_b = vals[0];
        rule->pad_r = rule->pad_l = vals[1];
    } else if (count == 3) {
        rule->pad_t = vals[0];
        rule->pad_r = rule->pad_l = vals[1];
        rule->pad_b = vals[2];
    } else {
        rule->pad_t = vals[0]; rule->pad_r = vals[1];
        rule->pad_b = vals[2]; rule->pad_l = vals[3];
    }
    rule->padding = rule->pad_t;
}

static void parse_margin_shorthand(const char* val, StyleRule* rule) {
    float vals[4] = {0, 0, 0, 0};
    char buf[128];
    strncpy(buf, val, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    int count = 0;
    char* tok = strtok(buf, " \t");
    while (tok && count < 4) {
        vals[count++] = parse_float_val(tok);
        tok = strtok(NULL, " \t");
    }
    rule->has_margin = 1;
    if (count == 1) {
        rule->margin_top = rule->margin_right = rule->margin_bottom = rule->margin_left = vals[0];
    } else if (count == 2) {
        rule->margin_top = rule->margin_bottom = vals[0];
        rule->margin_right = rule->margin_left = vals[1];
    } else if (count == 3) {
        rule->margin_top = vals[0];
        rule->margin_right = rule->margin_left = vals[1];
        rule->margin_bottom = vals[2];
    } else if (count >= 4) {
        rule->margin_top = vals[0];
        rule->margin_right = vals[1];
        rule->margin_bottom = vals[2];
        rule->margin_left = vals[3];
    }
}

static void parse_scroll_margin_shorthand(const char* val, StyleRule* rule) {
    float vals[4] = {0, 0, 0, 0};
    char buf[128];
    strncpy(buf, val, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    int count = 0;
    char* tok = strtok(buf, " \t");
    while (tok && count < 4) {
        vals[count++] = parse_float_val(tok);
        tok = strtok(NULL, " \t");
    }
    rule->has_scroll_margin = 1;
    if (count == 1) {
        rule->scroll_margin_top = rule->scroll_margin_right =
            rule->scroll_margin_bottom = rule->scroll_margin_left = vals[0];
    } else if (count == 2) {
        rule->scroll_margin_top = rule->scroll_margin_bottom = vals[0];
        rule->scroll_margin_right = rule->scroll_margin_left = vals[1];
    } else if (count == 3) {
        rule->scroll_margin_top = vals[0];
        rule->scroll_margin_right = rule->scroll_margin_left = vals[1];
        rule->scroll_margin_bottom = vals[2];
    } else if (count >= 4) {
        rule->scroll_margin_top = vals[0];
        rule->scroll_margin_right = vals[1];
        rule->scroll_margin_bottom = vals[2];
        rule->scroll_margin_left = vals[3];
    }
}

static void parse_scroll_padding_shorthand(const char* val, StyleRule* rule) {
    float vals[4] = {0, 0, 0, 0};
    char buf[128];
    strncpy(buf, val, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    int count = 0;
    char* tok = strtok(buf, " \t");
    while (tok && count < 4) {
        vals[count++] = parse_float_val(tok);
        tok = strtok(NULL, " \t");
    }
    rule->has_scroll_padding = 1;
    if (count == 1) {
        rule->scroll_padding_top = rule->scroll_padding_right =
            rule->scroll_padding_bottom = rule->scroll_padding_left = vals[0];
    } else if (count == 2) {
        rule->scroll_padding_top = rule->scroll_padding_bottom = vals[0];
        rule->scroll_padding_right = rule->scroll_padding_left = vals[1];
    } else if (count == 3) {
        rule->scroll_padding_top = vals[0];
        rule->scroll_padding_right = rule->scroll_padding_left = vals[1];
        rule->scroll_padding_bottom = vals[2];
    } else if (count >= 4) {
        rule->scroll_padding_top = vals[0];
        rule->scroll_padding_right = vals[1];
        rule->scroll_padding_bottom = vals[2];
        rule->scroll_padding_left = vals[3];
    }
}

// Parse transform: scale(), translate(x,y), translateX(), translateY() (may be combined)
static void parse_transform(const char* val, StyleRule* rule) {
    const char* p = val;
    while (p && *p) {
        while (isspace((unsigned char)*p)) p++;
        if (strncmp(p, "scale(", 6) == 0) {
            float s = 1.0f;
            if (sscanf(p + 6, "%f", &s) == 1) {
                rule->has_transform = 1;
                rule->transform_scale = s;
            }
        } else if (strncmp(p, "translate(", 10) == 0) {
            const char* arg = p + 10;
            char* endp;
            float tx = strtof(arg, &endp);
            int tx_pct = 0;
            if (*endp == '%') { tx_pct = 1; tx /= 100.0f; endp++; }
            /* find comma */
            while (*endp && *endp != ',' && *endp != ')') endp++;
            float ty = 0.0f;
            int ty_pct = 0;
            if (*endp == ',') {
                endp++;
                while (*endp == ' ') endp++;
                char* endp2;
                ty = strtof(endp, &endp2);
                if (*endp2 == '%') { ty_pct = 1; ty /= 100.0f; }
            }
            rule->has_transform_tx = 1;
            rule->has_transform_ty = 1;
            rule->transform_tx_pct = tx_pct;
            rule->transform_ty_pct = ty_pct;
            if (tx_pct) { rule->raw_transform_tx = tx; rule->transform_tx = 0.0f; }
            else rule->transform_tx = tx;
            if (ty_pct) { rule->raw_transform_ty = ty; rule->transform_ty = 0.0f; }
            else rule->transform_ty = ty;
        } else if (strncmp(p, "translateX(", 11) == 0) {
            char* endp;
            float tx = strtof(p + 11, &endp);
            int tx_pct = (*endp == '%');
            if (tx_pct) tx /= 100.0f;
            rule->has_transform_tx = 1;
            rule->transform_tx_pct = tx_pct;
            if (tx_pct) { rule->raw_transform_tx = tx; rule->transform_tx = 0.0f; }
            else rule->transform_tx = tx;
        } else if (strncmp(p, "translateY(", 11) == 0) {
            char* endp;
            float ty = strtof(p + 11, &endp);
            int ty_pct = (*endp == '%');
            if (ty_pct) ty /= 100.0f;
            rule->has_transform_ty = 1;
            rule->transform_ty_pct = ty_pct;
            if (ty_pct) { rule->raw_transform_ty = ty; rule->transform_ty = 0.0f; }
            else rule->transform_ty = ty;
        }
        const char* close = strchr(p, ')');
        if (!close) break;
        p = close + 1;
    }
}

static int parse_flex_direction(const char* val) {
    if (strstr(val, "column")) return FLEX_DIR_COLUMN;
    return FLEX_DIR_ROW;
}

static int parse_justify_content(const char* val) {
    if (strstr(val, "center"))        return FLEX_JUSTIFY_CENTER;
    if (strstr(val, "flex-end") || strstr(val, "end")) return FLEX_JUSTIFY_END;
    if (strstr(val, "space-between")) return FLEX_JUSTIFY_SPACE_BETWEEN;
    return FLEX_JUSTIFY_START;
}

static int parse_align_items(const char* val) {
    if (strstr(val, "center"))        return FLEX_ALIGN_CENTER;
    if (strstr(val, "flex-end") || strstr(val, "end")) return FLEX_ALIGN_END;
    if (strstr(val, "stretch"))       return FLEX_ALIGN_STRETCH;
    return FLEX_ALIGN_START;
}

static int parse_align_content(const char* val) {
    if (strstr(val, "space-between")) return FLEX_ALIGN_SPACE_BETWEEN;
    if (strstr(val, "space-around"))  return FLEX_ALIGN_SPACE_AROUND;
    return parse_align_items(val);
}

static int parse_grid_auto_flow(const char* val) {
    int flow = GRID_AUTO_FLOW_ROW;
    if (strstr(val, "column")) flow = GRID_AUTO_FLOW_COLUMN;
    if (strstr(val, "dense")) flow |= GRID_AUTO_FLOW_DENSE;
    return flow;
}

static int parse_overflow(const char* val) {
    if (strstr(val, "scroll")) return OVERFLOW_SCROLL;
    if (strstr(val, "auto")) return OVERFLOW_AUTO;
    if (strstr(val, "hidden")) return OVERFLOW_HIDDEN;
    return OVERFLOW_VISIBLE;
}

static int overflow_clips(int mode) {
    return mode == OVERFLOW_HIDDEN || mode == OVERFLOW_AUTO || mode == OVERFLOW_SCROLL;
}

static int overflow_scrollable(int mode) {
    return mode == OVERFLOW_AUTO || mode == OVERFLOW_SCROLL;
}

static float parse_scrollbar_width(const char* val) {
    if (strstr(val, "none")) return 0.0f;
    if (strstr(val, "thin")) return 3.0f;
    if (strstr(val, "auto")) return 5.0f;
    return parse_float_val(val);
}

static void parse_scrollbar_color(const char* val, StyleRule* rule) {
    char buf[128];
    strncpy(buf, val, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char* sp = strchr(buf, ' ');
    rule->has_scrollbar_color = 1;
    if (sp) {
        *sp = '\0';
        parse_color(buf, &rule->sb_thumb_r, &rule->sb_thumb_g, &rule->sb_thumb_b, &rule->sb_thumb_a);
        trim_whitespace(sp + 1);
        parse_color(sp + 1, &rule->sb_track_r, &rule->sb_track_g, &rule->sb_track_b, &rule->sb_track_a);
    } else {
        parse_color(buf, &rule->sb_thumb_r, &rule->sb_thumb_g, &rule->sb_thumb_b, &rule->sb_thumb_a);
        rule->sb_track_r = rule->sb_thumb_r * 0.85f + 0.08f;
        rule->sb_track_g = rule->sb_thumb_g * 0.85f + 0.08f;
        rule->sb_track_b = rule->sb_thumb_b * 0.85f + 0.08f;
        rule->sb_track_a = rule->sb_thumb_a * 0.35f;
    }
}

static float element_sb_width(const LunaElement* c) {
    if (c->has_scrollbar_width) return c->scrollbar_width;
    return 5.0f;
}

static int parse_align_self(const char* val) {
    if (strstr(val, "auto")) return ALIGN_SELF_AUTO;
    return parse_align_items(val);
}

static int parse_flex_wrap(const char* val) {
    if (strstr(val, "wrap")) return FLEX_WRAP_WRAP;
    return FLEX_WRAP_NOWRAP;
}

static int parse_box_sizing(const char* val) {
    if (strstr(val, "border-box")) return BOX_BORDER;
    return BOX_CONTENT;
}

static void parse_one_grid_track(const char* tok, float* size, int* type, float* min_px) {
    *type = GRID_TRACK_PX;
    *min_px = 0.0f;
    if (strncmp(tok, "minmax(", 7) == 0) {
        *type = GRID_TRACK_MINMAX;
        const char* mp = tok + 7;
        *min_px = parse_float_val(mp);
        const char* comma = strchr(mp, ',');
        *size = comma ? parse_float_val(comma + 1) : 1.0f;
    } else if (strstr(tok, "fr")) {
        *type = GRID_TRACK_FR;
        *size = parse_float_val(tok);
    } else {
        *size = parse_float_val(tok);
    }
}

static void parse_grid_tracks(const char* val, float* sizes, int* types, float* mins, int* count) {
    const char* p = val;
    *count = 0;
    while (*p && *count < MAX_GRID_TRACKS) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        char tok[64] = {0};
        int ti = 0;
        if (strncmp(p, "minmax(", 7) == 0) {
            int depth = 0;
            while (*p && ti < 63) {
                tok[ti++] = *p;
                if (*p == '(') depth++;
                else if (*p == ')') { depth--; if (depth == 0) { p++; break; } }
                p++;
            }
        } else {
            while (*p && !isspace((unsigned char)*p) && ti < 63) tok[ti++] = *p++;
        }
        tok[ti] = '\0';
        trim_whitespace(tok);
        parse_one_grid_track(tok, &sizes[*count], &types[*count], &mins[*count]);
        (*count)++;
    }
}

static void parse_single_grid_track(const char* val, float* size, int* type, float* min_px) {
    char tok[64] = {0};
    strncpy(tok, val, sizeof(tok) - 1);
    trim_whitespace(tok);
    parse_one_grid_track(tok, size, type, min_px);
}

static void parse_grid_template_areas(const char* val, StyleRule* rule) {
    memset(rule->grid_area_cell, 0, sizeof(rule->grid_area_cell));
    rule->grid_area_rows = 0;
    rule->grid_area_cols = 0;
    const char* p = val;
    while (*p && rule->grid_area_rows < MAX_GRID_AREA_ROWS) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        if (*p != '"') { p++; continue; }
        p++;
        char rowbuf[256] = {0};
        int ri = 0;
        while (*p && *p != '"' && ri < 255) rowbuf[ri++] = *p++;
        if (*p == '"') p++;
        int col = 0;
        char* tok = strtok(rowbuf, " \t");
        while (tok && col < MAX_GRID_AREA_COLS) {
            strncpy(rule->grid_area_cell[rule->grid_area_rows][col], tok, 31);
            tok = strtok(NULL, " \t");
            col++;
        }
        if (col > rule->grid_area_cols) rule->grid_area_cols = col;
        rule->grid_area_rows++;
    }
    rule->has_grid_template_areas = (rule->grid_area_rows > 0);
}

static void compile_grid_area_rects(LunaElement* cont) {
    cont->grid_area_rect_count = 0;
    int rows = cont->grid_area_rows, cols = cont->grid_area_cols;
    if (rows < 1 || cols < 1) return;
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            char* name = cont->grid_area_cell[r][c];
            if (!name[0] || strcmp(name, ".") == 0) continue;
            int found = 0;
            for (int i = 0; i < cont->grid_area_rect_count; i++) {
                if (strcmp(cont->grid_area_rects[i].name, name) == 0) { found = 1; break; }
            }
            if (found) continue;
            int minc = c, maxc = c, minr = r, maxr = r;
            for (int r2 = 0; r2 < rows; r2++) {
                for (int c2 = 0; c2 < cols; c2++) {
                    if (strcmp(cont->grid_area_cell[r2][c2], name) == 0) {
                        if (c2 < minc) minc = c2;
                        if (c2 > maxc) maxc = c2;
                        if (r2 < minr) minr = r2;
                        if (r2 > maxr) maxr = r2;
                    }
                }
            }
            if (cont->grid_area_rect_count >= MAX_GRID_AREAS) continue;
            GridAreaRect* ar = &cont->grid_area_rects[cont->grid_area_rect_count++];
            strncpy(ar->name, name, 31);
            ar->col = minc; ar->row = minr;
            ar->col_span = maxc - minc + 1;
            ar->row_span = maxr - minr + 1;
        }
    }
}

static int grid_area_lookup(LunaElement* cont, const char* name, int* gc, int* gr, int* cs, int* rs) {
    for (int i = 0; i < cont->grid_area_rect_count; i++) {
        if (strcmp(cont->grid_area_rects[i].name, name) == 0) {
            *gc = cont->grid_area_rects[i].col;
            *gr = cont->grid_area_rects[i].row;
            *cs = cont->grid_area_rects[i].col_span;
            *rs = cont->grid_area_rects[i].row_span;
            return 1;
        }
    }
    return 0;
}

static int parse_grid_line_val(const char* val, int* span_out) {
    char buf[64];
    strncpy(buf, val, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    trim_whitespace(buf);
    if (strncmp(buf, "span ", 5) == 0) {
        *span_out = atoi(buf + 5);
        if (*span_out < 1) *span_out = 1;
        return -2;
    }
    int line = atoi(buf);
    if (line < 1) line = 1;
    return line - 1;
}

static int parse_cursor_type(const char* val) {
    if (strstr(val, "pointer"))   return 1;
    if (strstr(val, "text"))      return 2;
    if (strstr(val, "crosshair")) return 3;
    if (strstr(val, "ew-resize") || strstr(val, "e-resize")) return 4;
    if (strstr(val, "ns-resize") || strstr(val, "n-resize")) return 5;
    return 0;
}

// ============================================================
// LunaElement helpers
// ============================================================

/* id → element index map.  luna-shell resolves dozens of ids per frame (clock,
 * stats, sliders, dock, popovers), and each lookup used to strcmp its way down
 * the whole element array.  Element ids are only ever written when an element
 * is appended, so the table just needs extending as elem_count grows. */
#define LUNA_ID_MAP_SIZE 4096   /* power of two, comfortably > MAX_ELEMENTS */
static int g_id_map[LUNA_ID_MAP_SIZE];
static int g_id_map_ready = 0;
static int g_id_map_built = 0;  /* elements already inserted */

static unsigned luna_id_hash(const char* s) {
    unsigned h = 2166136261u;
    while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
    return h;
}

static void id_map_insert(int idx) {
    const char* id = elements[idx].id;
    if (!id[0]) return;
    unsigned h = luna_id_hash(id) & (LUNA_ID_MAP_SIZE - 1);
    while (g_id_map[h] != -1) {
        /* Duplicate id: the first element wins, exactly as the old
         * front-to-back linear scan did. */
        if (strcmp(elements[g_id_map[h]].id, id) == 0) return;
        h = (h + 1) & (LUNA_ID_MAP_SIZE - 1);
    }
    g_id_map[h] = idx;
}

/* Returns 0 when the table cannot stay sparse enough to probe cheaply, in
 * which case the caller falls back to the plain scan. */
static int id_map_sync(void) {
    if (elem_count * 2 > LUNA_ID_MAP_SIZE) return 0;
    if (!g_id_map_ready || g_id_map_built > elem_count) {
        for (int i = 0; i < LUNA_ID_MAP_SIZE; i++) g_id_map[i] = -1;
        g_id_map_built = 0;
        g_id_map_ready = 1;
    }
    for (; g_id_map_built < elem_count; g_id_map_built++)
        id_map_insert(g_id_map_built);
    return 1;
}

int get_element_by_id(const char* id) {
    if (!id || !id[0]) return -1;
    if (!id_map_sync()) {
        for (int i = 0; i < elem_count; i++)
            if (strcmp(elements[i].id, id) == 0) return i;
        return -1;
    }
    unsigned h = luna_id_hash(id) & (LUNA_ID_MAP_SIZE - 1);
    for (int probe = 0; probe < LUNA_ID_MAP_SIZE; probe++) {
        int idx = g_id_map[h];
        if (idx == -1) return -1;
        if (strcmp(elements[idx].id, id) == 0) return idx;
        h = (h + 1) & (LUNA_ID_MAP_SIZE - 1);
    }
    return -1;
}

void set_text(int idx, const char* new_text) {
    if (idx >= 0 && idx < elem_count) {
        if (!new_text) new_text = "";
        if (strcmp(elements[idx].text, new_text) == 0) return;
        strncpy(elements[idx].text, new_text, sizeof(elements[idx].text) - 1);
        elements[idx].text[sizeof(elements[idx].text) - 1] = '\0';
        if (elements[idx].is_input) {
            int n = (int)strlen(elements[idx].text);
            if (elements[idx].caret > n) elements[idx].caret = n;
        }
        elements[idx].has_custom_text = 1;
        if (elements[idx].aria_live == 1 || elements[idx].aria_live == 2) {
            snprintf(g_a11y_live_msg, sizeof(g_a11y_live_msg), "%s", new_text);
            g_a11y_live_assertive = (elements[idx].aria_live == 2);
            g_a11y_live_until = luna_now() + 3.0;
        }
    }
}

void set_on_click(int idx, EventHandler cb) {
    if (idx >= 0 && idx < elem_count) elements[idx].on_click = cb;
}

static void register_js_handler(const char* name, EventHandler fn) {
    if (!name || !name[0] || !fn) return;
    for (int i = 0; i < g_js_handler_count; i++) {
        if (strcmp(g_js_handlers[i].name, name) == 0) {
            g_js_handlers[i].fn = fn;
            return;
        }
    }
    if (g_js_handler_count < MAX_JS_HANDLERS) {
        g_js_handlers[g_js_handler_count].name = name;
        g_js_handlers[g_js_handler_count].fn = fn;
        g_js_handler_count++;
    }
}

static EventHandler lookup_js_handler(const char* name) {
    if (!name || !name[0]) return NULL;
    for (int i = 0; i < g_js_handler_count; i++)
        if (strcmp(g_js_handlers[i].name, name) == 0)
            return g_js_handlers[i].fn;
    return NULL;
}

/* Parse onclick="onButton()" / "onButton(); return false;" → "onButton" */
static void parse_onclick_expr(const char* expr, char* out, int out_len) {
    if (!expr || !out || out_len <= 0) { if (out) out[0] = '\0'; return; }
    const char* p = expr;
    while (*p && isspace((unsigned char)*p)) p++;
    int i = 0;
    while (*p && (isalnum((unsigned char)*p) || *p == '_') && i < out_len - 1)
        out[i++] = *p++;
    out[i] = '\0';
}

static void wire_element_onclick_handlers(void) {
    for (int i = 0; i < elem_count; i++) {
        if (!elements[i].onclick[0]) continue;
        EventHandler fn = lookup_js_handler(elements[i].onclick);
        if (fn) elements[i].on_click = fn;
        else fprintf(stderr, "[vespera] Unknown onclick handler: %s (id=%s)\n",
                     elements[i].onclick, elements[i].id[0] ? elements[i].id : "(none)");
    }
}

static int extract_html_attr(const char* tag_buf, const char* attr, char* out, int out_len) {
    if (!tag_buf || !attr || !out || out_len <= 0) return 0;
    char needle[64];
    snprintf(needle, sizeof(needle), "%s=\"", attr);
    const char* p = strstr(tag_buf, needle);
    if (!p) {
        snprintf(needle, sizeof(needle), "%s='", attr);
        p = strstr(tag_buf, needle);
        if (!p) return 0;
        p += strlen(attr) + 2;
        const char* end = strchr(p, '\'');
        if (!end) return 0;
        int n = (int)(end - p);
        if (n >= out_len) n = out_len - 1;
        strncpy(out, p, (size_t)n);
        out[n] = '\0';
        return 1;
    }
    p += strlen(attr) + 2;
    const char* end = strchr(p, '"');
    if (!end) return 0;
    int n = (int)(end - p);
    if (n >= out_len) n = out_len - 1;
    strncpy(out, p, (size_t)n);
    out[n] = '\0';
    return 1;
}

void parse_declarations(char* declarations, StyleRule* rule);

static void apply_element_inline_style(LunaElement* e) {
    if (!e || !e->has_inline_style || !e->inline_style[0]) return;
    StyleRule rule;
    memset(&rule, 0, sizeof(rule));
    char buf[320];
    strncpy(buf, e->inline_style, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    parse_declarations(buf, &rule);

    if (rule.has_bg) {
        e->r = rule.bg_r; e->g = rule.bg_g; e->b = rule.bg_b; e->a = rule.bg_a;
        e->has_custom_bg = 1;
        if (rule.has_gradient) {
            e->has_gradient = 1;
            e->grad_type = rule.grad_type;
            e->grad_stop_count = rule.grad_stop_count;
            for (int s = 0; s < rule.grad_stop_count; s++) {
                e->grad_stop_pos[s] = rule.grad_stop_pos[s];
                e->grad_stop_r[s] = rule.grad_stop_r[s];
                e->grad_stop_g[s] = rule.grad_stop_g[s];
                e->grad_stop_b[s] = rule.grad_stop_b[s];
                e->grad_stop_a[s] = rule.grad_stop_a[s];
            }
            e->grad_angle = rule.grad_angle;
            e->grad_rad_cx = rule.grad_rad_cx;
            e->grad_rad_cy = rule.grad_rad_cy;
            e->grad_rad_r = rule.grad_rad_r;
        }
    }
    if (rule.has_color) {
        e->t_r = rule.c_r; e->t_g = rule.c_g; e->t_b = rule.c_b; e->t_a = rule.c_a;
        e->has_custom_color = 1;
    }
    if (rule.has_width) {
        e->has_css_width = 1;
        e->pct_w = rule.pct_w;
        e->raw_w = rule.raw_w;
        e->raw_w_off = rule.raw_w_off;
        e->css_width = rule.width;
    }
    if (rule.has_height) {
        e->has_css_height = 1;
        e->pct_h = rule.pct_h;
        e->raw_h = rule.raw_h;
        e->raw_h_off = rule.raw_h_off;
        e->css_height = rule.height;
    }
    if (rule.has_max_height) {
        e->has_max_height = 1;
        e->css_max_height = rule.max_height;
        e->max_height_pct = rule.max_height_pct;
        e->raw_max_height = rule.raw_max_height;
        e->raw_max_height_off = rule.raw_max_height_off;
    }
    if (rule.has_left) {
        e->has_left = 1;
        e->pct_left = rule.pct_left;
        e->raw_left = rule.pct_left ? rule.raw_left : rule.left;
        e->raw_left_off = rule.raw_left_off;
        if (e->position_fixed || e->position_mode == POS_ABSOLUTE)
            e->css_positioned |= 1;
        /* Layout reads non-% left from rel_x (same as stylesheet path). */
        if (e->position_mode != POS_RELATIVE && !rule.pct_left)
            e->rel_x = rule.left;
    }
    if (rule.has_top) {
        e->has_top = 1;
        e->pct_top = rule.pct_top;
        e->raw_top = rule.pct_top ? rule.raw_top : rule.top;
        e->raw_top_off = rule.raw_top_off;
        if (e->position_fixed || e->position_mode == POS_ABSOLUTE)
            e->css_positioned |= 2;
        /* Layout reads non-% top from rel_y (same as stylesheet path). */
        if (e->position_mode != POS_RELATIVE && !rule.pct_top)
            e->rel_y = rule.top;
    }
    if (rule.has_display) {
        e->display_none = rule.display_none;
        e->display_mode = rule.display_mode;
    }
    if (rule.has_margin) {
        e->margin_top = rule.margin_top;
        e->margin_right = rule.margin_right;
        e->margin_bottom = rule.margin_bottom;
        e->margin_left = rule.margin_left;
    }
    if (rule.has_shadow) {
        e->has_shadow = (rule.shadow_count > 0);
        e->shadow_count = rule.shadow_count;
        for (int s = 0; s < rule.shadow_count; s++) e->shadows[s] = rule.shadows[s];
    }
    if (rule.has_flex_direction || rule.has_display) {
        if (rule.display_mode == DISPLAY_FLEX) e->display_mode = DISPLAY_FLEX;
        if (rule.has_flex_direction) e->flex_direction = rule.flex_direction;
        if (rule.has_align_items) e->align_items = rule.align_items;
        if (rule.has_gap) e->flex_gap = rule.flex_gap;
    }
    if (rule.has_border) {
        e->border_width = rule.border_width;
        e->bd_r = rule.bd_r; e->bd_g = rule.bd_g; e->bd_b = rule.bd_b; e->bd_a = rule.bd_a;
        e->has_custom_border = 1;
    }
    if (rule.has_radius) {
        e->border_radius = rule.border_radius;
        for (int c = 0; c < 4; c++)
            if (rule.has_rad_c[c]) e->rad_c[c] = rule.rad_c[c];
    }
    if (rule.has_text_align) e->text_align = rule.text_align;
    if (rule.has_font_size) e->font_size = rule.font_size;
    if (rule.has_font_weight) e->font_bold = rule.font_bold;
    if (rule.has_font_face) e->font_face = rule.font_face;
    if (rule.has_line_height) e->line_height = rule.line_height;
    if (rule.has_white_space) e->white_space = rule.white_space;
    if (rule.has_text_overflow) e->text_overflow = rule.text_overflow;
    if (rule.has_overflow_wrap) e->overflow_wrap = rule.overflow_wrap;
    g_layout_dirty = 1; g_render_order_dirty = 1;
}

static const char* class_next_token(const char* p, char* out, size_t out_n) {
    if (!p || !out || out_n == 0) return NULL;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (!*p) { out[0] = 0; return NULL; }
    const char* start = p;
    while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
    size_t n = (size_t)(p - start);
    if (n >= out_n) n = out_n - 1;
    memcpy(out, start, n);
    out[n] = 0;
    return p;
}

int element_has_class(LunaElement* e, const char* cls) {
    if (!e || !cls || !*cls) return 0;
    char tok[96];
    const char* p = e->class_name;
    while ((p = class_next_token(p, tok, sizeof(tok))) != NULL)
        if (!strcmp(tok, cls)) return 1;
    return 0;
}

void update_element_style(LunaElement* e);

/* Class changes on an ancestor invalidate descendant selectors such as
 * `#app.ts-2 .skin-list-row`.  Restyle the node and every descendant. */
static void restyle_element_and_descendants(LunaElement* e) {
    int root = (int)(e - elements);
    if (root < 0 || root >= elem_count) return;
    update_element_style(e);
    for (int i = 0; i < elem_count; i++) {
        if (i == root) continue;
        for (int p = elements[i].parent_idx; p != -1; p = elements[p].parent_idx) {
            if (p == root) {
                update_element_style(&elements[i]);
                break;
            }
        }
    }
}

static int add_class_no_restyle(LunaElement* e, const char* cls) {
    if (!e || !cls || !*cls || element_has_class(e, cls)) return 0;
    size_t used = strlen(e->class_name), add = strlen(cls);
    if (used + add + (used ? 1u : 0u) + 1u > sizeof(e->class_name)) return 0;
    if (used) e->class_name[used++] = ' ';
    memcpy(e->class_name + used, cls, add + 1);
    return 1;
}

static int remove_class_no_restyle(LunaElement* e, const char* cls) {
    if (!e || !cls || !*cls || !element_has_class(e, cls)) return 0;
    char result[96] = {0}, tok[96];
    const char* p = e->class_name;
    while ((p = class_next_token(p, tok, sizeof(tok))) != NULL) {
        if (strcmp(tok, cls) != 0) {
            size_t used = strlen(result), add = strlen(tok);
            if (used + add + (used ? 1u : 0u) + 1u <= sizeof(result)) {
                if (used) result[used++] = ' ';
                memcpy(result + used, tok, add + 1);
            }
        }
    }
    snprintf(e->class_name, sizeof(e->class_name), "%s", result);
    return 1;
}

static int class_list_has_token(const char* list, const char* cls) {
    if (!list || !*list || !cls || !*cls) return 0;
    size_t want = strlen(cls);
    const char* p = list;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        const char* start = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
        if ((size_t)(p - start) == want && !strncmp(start, cls, want)) return 1;
    }
    return 0;
}

static int update_classes_internal(LunaElement* e, const char* remove_list,
                                   const char* add_list) {
    int changed = 0;
    char tok[96];
    const char* p = remove_list;
    while (p && (p = class_next_token(p, tok, sizeof(tok))) != NULL) {
        /* A class requested in both sets is already in its desired state;
         * keeping it in place also avoids a remove/re-add reorder. */
        if (!class_list_has_token(add_list, tok))
            changed |= remove_class_no_restyle(e, tok);
    }
    p = add_list;
    while (p && (p = class_next_token(p, tok, sizeof(tok))) != NULL)
        changed |= add_class_no_restyle(e, tok);
    if (changed) restyle_element_and_descendants(e);
    return changed;
}

void add_class(LunaElement* e, const char* cls) {
    if (add_class_no_restyle(e, cls)) restyle_element_and_descendants(e);
}

void remove_class(LunaElement* e, const char* cls) {
    if (remove_class_no_restyle(e, cls)) restyle_element_and_descendants(e);
}

void set_bg(int idx, float r, float g, float b, float a) {
    if (idx < 0 || idx >= elem_count) return;
    elements[idx].r = r; elements[idx].g = g;
    elements[idx].b = b; elements[idx].a = a;
    elements[idx].has_custom_bg = 1;
    visual_activate_idx(idx);
}

int is_visible(int idx) {
    while (idx != -1) {
        if (elements[idx].display_none) return 0;
        idx = elements[idx].parent_idx;
    }
    return 1;
}

static int rects_intersect(float ax, float ay, float aw, float ah,
                           float bx, float by, float bw, float bh) {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

static int element_overflow_visible(int idx) {
    LunaElement* e = &elements[idx];
    float ex = e->x, ey = e->y, ew = e->w, eh = e->h;
    int p = e->parent_idx;
    while (p != -1) {
        LunaElement* par = &elements[p];
        if (overflow_clips(par->overflow_x) || overflow_clips(par->overflow_y)) {
            float cx = par->x + par->border_width + par->pad_l, cy = par->y + par->border_width + par->pad_t;
            float cw = par->w - par->border_width * 2.0f - par->pad_l - par->pad_r;
            float ch = par->h - par->border_width * 2.0f - par->pad_t - par->pad_b;
            if (cw <= 0.0f || ch <= 0.0f) return 0;
            int clip_x = overflow_clips(par->overflow_x);
            int clip_y = overflow_clips(par->overflow_y);
            if (clip_x && (ex + ew <= cx || ex >= cx + cw)) return 0;
            if (clip_y && (ey + eh <= cy || ey >= cy + ch)) return 0;
            if (clip_x && clip_y && !rects_intersect(ex, ey, ew, eh, cx, cy, cw, ch)) return 0;
        }
        p = par->parent_idx;
    }
    return 1;
}

int is_rendered(int idx) {
    if (!is_visible(idx)) return 0;
    if (elements[idx].visibility_hidden) return 0;
    if (!element_overflow_visible(idx)) return 0;
    return 1;
}

// ============================================================
// CSS selector parsing
// ============================================================

void parse_simple_selector(const char* sel_in, SimpleSelector* out) {
    memset(out, 0, sizeof(SimpleSelector));
    const char* p = sel_in;
    while (*p) {
        if (*p == '.' || *p == '#') {
            char kind = *p; p++;
            char token[64] = {0}; int ti = 0;
            while (*p && *p != '.' && *p != '#' && ti < 63) token[ti++] = *p++;
            token[ti] = 0;
            if (kind == '.') {
                if (out->sel_class_count < MAX_SEL_CLASSES) {
                    snprintf(out->sel_classes[out->sel_class_count], sizeof(out->sel_classes[0]), "%s", token);
                    out->sel_class_count++;
                }
            } else {
                snprintf(out->sel_id, sizeof(out->sel_id), "%s", token);
            }
        } else {
            char token[32] = {0}; int ti = 0;
            while (*p && *p != '.' && *p != '#' && ti < 31) token[ti++] = *p++;
            token[ti] = 0;
            if (strcmp(token, "*") == 0) out->is_universal = 1;
            else if (strlen(token) > 0) snprintf(out->sel_type, sizeof(out->sel_type), "%s", token);
        }
    }
}

void parse_selector(const char* sel_in, StyleRule* rule) {
    char sel[128]; strncpy(sel, sel_in, sizeof(sel) - 1); sel[sizeof(sel)-1] = 0;
    rule->ancestor_count = 0;

    char* compounds[MAX_SEL_ANCESTORS + 1];
    int compound_count = 0;
    char* tok = strtok(sel, " \t");
    while (tok && compound_count < MAX_SEL_ANCESTORS + 1) {
        compounds[compound_count++] = tok;
        tok = strtok(NULL, " \t");
    }
    if (compound_count == 0) { memset(&rule->target, 0, sizeof(SimpleSelector)); return; }

    parse_simple_selector(compounds[compound_count - 1], &rule->target);
    /* nearest-first, matching match_selector_chain's walk order */
    for (int i = compound_count - 2; i >= 0; i--)
        parse_simple_selector(compounds[i], &rule->ancestors[rule->ancestor_count++]);
}

/* 1-based position of e among its non-internal siblings (same parent). */
static void element_sibling_info(const LunaElement* e, int* out_pos, int* out_count) {
    int self = (int)(e - elements);
    int parent = e->parent_idx;
    int pos = 0, count = 0;
    for (int i = 0; i < elem_count; i++) {
        if (elements[i].parent_idx != parent || elements[i].luna_internal) continue;
        count++;
        if (i == self) pos = count;
    }
    *out_pos = pos;
    *out_count = count;
}

/* Nearest preceding non-internal sibling of idx, or -1. */
static int element_prev_sibling(int idx) {
    int parent = elements[idx].parent_idx;
    for (int i = idx - 1; i >= 0; i--)
        if (elements[i].parent_idx == parent && !elements[i].luna_internal) return i;
    return -1;
}

int simple_selector_matches(SimpleSelector* s, LunaElement* e) {
    if (s->sel_type[0] && strcmp(s->sel_type, e->type) != 0) return 0;
    if (s->sel_id[0]   && strcmp(s->sel_id,   e->id)   != 0) return 0;
    for (int i = 0; i < s->sel_class_count; i++)
        if (!element_has_class(e, s->sel_classes[i])) return 0;
    if (s->is_first_child || s->is_last_child || s->has_nth) {
        int pos, count;
        element_sibling_info(e, &pos, &count);
        if (pos == 0) return 0;
        if (s->is_first_child && pos != 1) return 0;
        if (s->is_last_child && pos != count) return 0;
        if (s->has_nth) {
            /* pos == a*n + b for some integer n >= 0 */
            int a = s->nth_a, b = s->nth_b;
            if (a == 0) {
                if (pos != b) return 0;
            } else {
                int d = pos - b;
                if (d % a != 0 || d / a < 0) return 0;
            }
        }
    }
    if (s->has_not) {
        if (s->not_type[0] && strcmp(s->not_type, e->type) == 0) return 0;
        if (s->not_id[0] && strcmp(s->not_id, e->id) == 0) return 0;
        if (s->not_class[0] && element_has_class(e, s->not_class)) return 0;
    }
    if (!s->sel_type[0] && !s->sel_id[0] && s->sel_class_count == 0 &&
        !s->is_first_child && !s->is_last_child && !s->has_nth && !s->has_not)
        return s->is_universal ? 1 : 0;
    return 1;
}

/* Match ancestors[a..] against the chain above/before `node`, with
   backtracking so "A B", "A > B", "A + B" and "A ~ B" all resolve correctly.
   ancestors[] is stored nearest-first (target side first). */
static int match_selector_chain(StyleRule* r, int a, int node) {
    if (a >= r->ancestor_count) return 1;
    SimpleSelector* s = &r->ancestors[a];
    switch (s->rel) {
    case LUNA_REL_CHILD: {
        int p = elements[node].parent_idx;
        return p != -1 && simple_selector_matches(s, &elements[p]) &&
               match_selector_chain(r, a + 1, p);
    }
    case LUNA_REL_ADJ: {
        int p = element_prev_sibling(node);
        return p != -1 && simple_selector_matches(s, &elements[p]) &&
               match_selector_chain(r, a + 1, p);
    }
    case LUNA_REL_SIB: {
        for (int p = element_prev_sibling(node); p != -1; p = element_prev_sibling(p))
            if (simple_selector_matches(s, &elements[p]) &&
                match_selector_chain(r, a + 1, p)) return 1;
        return 0;
    }
    default: /* LUNA_REL_DESC */
        for (int p = elements[node].parent_idx; p != -1; p = elements[p].parent_idx)
            if (simple_selector_matches(s, &elements[p]) &&
                match_selector_chain(r, a + 1, p)) return 1;
        return 0;
    }
}

int selector_matches(StyleRule* r, LunaElement* e) {
    /* ::before / ::after rules target the HOST selector but apply to the
     * generated pseudo node (generated_pseudo == 1/2).  Regular DOM nodes
     * must never match those rules. */
    if (r->pseudo_elem) {
        if (e->generated_pseudo != r->pseudo_elem) return 0;
        if (e->parent_idx < 0 || e->parent_idx >= elem_count) return 0;
        LunaElement* host = &elements[e->parent_idx];
        if (!simple_selector_matches(&r->target, host)) return 0;
        return match_selector_chain(r, 0, e->parent_idx);
    }
    if (e->generated_pseudo) return 0;
    if (!simple_selector_matches(&r->target, e)) return 0;
    return match_selector_chain(r, 0, (int)(e - elements));
}

/* Like selector_matches but for pseudo-element rules against a HOST element
 * (used while spawning synthetic ::before / ::after nodes). */
static int selector_matches_pseudo(StyleRule* r, LunaElement* e) {
    if (!simple_selector_matches(&r->target, e)) return 0;
    return match_selector_chain(r, 0, (int)(e - elements));
}

// ============================================================
// CSS declaration parsing
// ============================================================

static int offsets_should_apply(const LunaElement* e) {
    if (e->position_fixed || e->position_sticky) return 1;
    if (e->position_mode == POS_STATIC) return 0;
    if (e->position_mode == POS_RELATIVE || e->position_mode == POS_ABSOLUTE) return 1;
    return 1;
}

/* Forward declaration — defined below */
void parse_declarations(char* declarations, StyleRule* rule);

/* Apply one pre-tokenised CSS key/value to a StyleRule.
   Wraps parse_declarations so all property logic stays in one place. */
static void apply_one_declaration(const char* key, const char* val, StyleRule* rule) {
    /* Skip CSS custom properties (--name: value) — resolved by cssparser.h */
    if (strncmp(key, "--", 2) == 0) return;
    char buf[CSS_MAX_VALUE + CSS_MAX_STR + 4];
    snprintf(buf, sizeof(buf), "%s: %s;", key, val);
    parse_declarations(buf, rule);
}

/* Apply a font-family value: only icon/brand faces select a dedicated atlas. */
static void apply_font_family_value(StyleRule* rule, const char* val) {
    rule->has_font_face = 1;
    if (strstr(val, "Luna Symbols")) rule->font_face = 1;
    else if (strstr(val, "Luna Brands")) rule->font_face = 2;
    else rule->font_face = 0;
}

/* Absolute / relative CSS font-size keywords → px (medium = 16). */
static int font_size_keyword_px(const char* tok, float* out_px) {
    static const struct { const char* name; float px; } kws[] = {
        {"xx-small", 9.0f}, {"x-small", 10.0f}, {"small", 13.0f},
        {"medium", 16.0f}, {"large", 18.0f}, {"x-large", 24.0f},
        {"xx-large", 32.0f}, {"xxx-large", 48.0f},
        /* relative sizes approximate against the initial medium */
        {"smaller", 13.0f}, {"larger", 19.0f},
    };
    for (size_t i = 0; i < sizeof(kws) / sizeof(kws[0]); i++) {
        if (strcasecmp(tok, kws[i].name) == 0) { *out_px = kws[i].px; return 1; }
    }
    return 0;
}

static int font_token_is_weight(const char* t, int* out_bold) {
    if (strcasecmp(t, "bold") == 0 || strcasecmp(t, "bolder") == 0) {
        if (out_bold) *out_bold = 1;
        return 1;
    }
    if (strcasecmp(t, "normal") == 0 || strcasecmp(t, "lighter") == 0) {
        if (out_bold) *out_bold = 0;
        return 1;
    }
    char* end = NULL;
    long n = strtol(t, &end, 10);
    if (end && end != t && *end == '\0' && n >= 1 && n <= 1000) {
        if (out_bold) *out_bold = (n >= 600);
        return 1;
    }
    return 0;
}

static int font_token_is_stretch(const char* t) {
    static const char* kws[] = {
        "ultra-condensed", "extra-condensed", "condensed", "semi-condensed",
        "normal", "semi-expanded", "expanded", "extra-expanded", "ultra-expanded"
    };
    for (size_t i = 0; i < sizeof(kws) / sizeof(kws[0]); i++)
        if (strcasecmp(t, kws[i]) == 0) return 1;
    /* font-stretch percentage, e.g. 75% — only accept when no length unit */
    char* end = NULL;
    strtof(t, &end);
    return end && end != t && *end == '%';
}

/* True when `tok` is a <font-size> (keyword or length/%); writes px when known. */
static int font_token_is_size(const char* tok, float* out_px) {
    if (font_size_keyword_px(tok, out_px)) return 1;
    if (!tok || !*tok) return 0;
    /* Bare 1–1000 integers are font-weight, not size. */
    {
        char* end = NULL;
        long n = strtol(tok, &end, 10);
        if (end && end != tok && *end == '\0' && n >= 1 && n <= 1000) return 0;
    }
    char* endp = NULL;
    float f = strtof(tok, &endp);
    if (!endp || endp == tok) return 0;
    while (isspace((unsigned char)*endp)) endp++;
    if (*endp == '\0') return 0; /* unitless number → not a size here */
    if (*endp == '%' || strncmp(endp, "px", 2) == 0 || strncmp(endp, "em", 2) == 0 ||
        strncmp(endp, "rem", 3) == 0 || strncmp(endp, "pt", 2) == 0 ||
        strncmp(endp, "vh", 2) == 0 || strncmp(endp, "vw", 2) == 0) {
        if (out_px) *out_px = parse_float_val(tok);
        return 1;
    }
    (void)f;
    return 0;
}

/* Parse <line-height>: normal | <number> | <length> | <percentage>. */
static float font_parse_line_height(const char* tok) {
    if (!tok || !*tok || strcasecmp(tok, "normal") == 0) return 0.0f;
    char* endp = NULL;
    float lh = strtof(tok, &endp);
    while (endp && isspace((unsigned char)*endp)) endp++;
    if (endp && *endp == '\0' && lh > 0.0f) return -lh; /* unitless multiplier */
    return parse_float_val(tok);
}

/*
 * CSS Fonts Level 3 `font` shorthand:
 *   [ [ <'font-style'> || <font-variant-css21> || <'font-weight'> || <'font-stretch'> ]?
 *     <'font-size'> [ / <'line-height'> ]? <'font-family'> ]
 *   | caption | icon | menu | message-box | small-caption | status-bar
 * On success, unspecified longhands reset to their initial values (required by CSS).
 * On failure the StyleRule is left unchanged (invalid declaration is ignored).
 */
static void parse_font_shorthand(const char* val, StyleRule* rule) {
    char buf[CSS_MAX_VALUE];
    strncpy(buf, val ? val : "", sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    trim_whitespace(buf);
    if (!buf[0]) return;

    /* System font keywords — approximate with a readable default face/size. */
    static const char* sys[] = {
        "caption", "icon", "menu", "message-box", "small-caption", "status-bar"
    };
    for (size_t i = 0; i < sizeof(sys) / sizeof(sys[0]); i++) {
        if (strcasecmp(buf, sys[i]) == 0) {
            rule->has_font_italic = 1; rule->font_italic = 0;
            rule->has_font_weight = 1; rule->font_bold = 0;
            rule->has_line_height = 1; rule->line_height = 0.0f;
            rule->has_font_size = 1;
            rule->font_size = (strcasecmp(buf, "small-caption") == 0 ||
                               strcasecmp(buf, "status-bar") == 0) ? 12.0f : 13.0f;
            apply_font_family_value(rule, "sans-serif");
            return;
        }
    }

    /* Tokenise on whitespace; keep quoted strings and var()/calc() intact. */
    char* toks[64];
    int ntok = 0;
    {
        char* p = buf;
        while (*p && ntok < 64) {
            while (isspace((unsigned char)*p)) p++;
            if (!*p) break;
            toks[ntok++] = p;
            int depth = 0; char quote = 0;
            while (*p) {
                if (quote) {
                    if (*p == '\\' && p[1]) { p += 2; continue; }
                    if (*p == quote) quote = 0;
                    p++; continue;
                }
                if (*p == '"' || *p == '\'') { quote = *p++; continue; }
                if (*p == '(') { depth++; p++; continue; }
                if (*p == ')' && depth > 0) { depth--; p++; continue; }
                if (isspace((unsigned char)*p) && depth == 0) break;
                p++;
            }
            if (*p) { *p = '\0'; p++; }
        }
    }
    if (ntok < 2) return; /* need at least <font-size> <font-family> */

    int italic = 0, bold = 0;
    float size_px = 0.0f, line_h = 0.0f;
    int have_size = 0, have_lh = 0;
    int fam_i = -1;

    for (int i = 0; i < ntok; i++) {
        char* tok = toks[i];
        char size_tok[128], lh_tok[128];
        size_tok[0] = lh_tok[0] = '\0';

        /* SIZE/LH glued together, e.g. 8px/14px or 12px/1.5 */
        {
            int depth = 0; char quote = 0; char* slash = NULL;
            for (char* q = tok; *q; q++) {
                if (quote) {
                    if (*q == '\\' && q[1]) { q++; continue; }
                    if (*q == quote) quote = 0;
                    continue;
                }
                if (*q == '"' || *q == '\'') { quote = *q; continue; }
                if (*q == '(') { depth++; continue; }
                if (*q == ')' && depth > 0) { depth--; continue; }
                if (*q == '/' && depth == 0) { slash = q; break; }
            }
            if (slash && slash != tok) {
                size_t sn = (size_t)(slash - tok);
                if (sn >= sizeof(size_tok)) sn = sizeof(size_tok) - 1;
                memcpy(size_tok, tok, sn); size_tok[sn] = '\0';
                strncpy(lh_tok, slash + 1, sizeof(lh_tok) - 1);
                lh_tok[sizeof(lh_tok) - 1] = '\0';
            }
        }

        const char* size_candidate = size_tok[0] ? size_tok : tok;
        float px = 0.0f;
        if (font_token_is_size(size_candidate, &px)) {
            /* A bare percentage can be font-stretch (rare) when a later
             * length/keyword size is present: `font: 75% 12px Arial`. */
            int pct_only = 0;
            if (!size_tok[0]) {
                char* ep = NULL;
                strtof(tok, &ep);
                while (ep && isspace((unsigned char)*ep)) ep++;
                pct_only = (ep && *ep == '%' && ep[1] == '\0');
            }
            if (pct_only) {
                int later = 0;
                for (int j = i + 1; j < ntok; j++) {
                    float dummy = 0.0f;
                    char* slash = strchr(toks[j], '/');
                    char tmp[128];
                    const char* cand = toks[j];
                    if (slash && slash != toks[j]) {
                        size_t sn = (size_t)(slash - toks[j]);
                        if (sn >= sizeof(tmp)) sn = sizeof(tmp) - 1;
                        memcpy(tmp, toks[j], sn); tmp[sn] = '\0';
                        cand = tmp;
                    }
                    if (font_token_is_size(cand, &dummy)) { later = 1; break; }
                    int wb = 0;
                    if (strcasecmp(toks[j], "italic") == 0 || strcasecmp(toks[j], "oblique") == 0 ||
                        strcasecmp(toks[j], "small-caps") == 0 ||
                        font_token_is_weight(toks[j], &wb) ||
                        font_token_is_stretch(toks[j]))
                        continue;
                    break;
                }
                if (later) {
                    /* consume as font-stretch percentage */
                    continue;
                }
            }
            size_px = px;
            have_size = 1;
            if (lh_tok[0]) {
                line_h = font_parse_line_height(lh_tok);
                have_lh = 1;
                fam_i = i + 1;
            } else if (i + 1 < ntok && toks[i + 1][0] == '/') {
                /* `12px / 1.5` or `12px /1.5` */
                const char* lh = toks[i + 1] + 1;
                while (isspace((unsigned char)*lh)) lh++;
                if (*lh) {
                    line_h = font_parse_line_height(lh);
                    have_lh = 1;
                    fam_i = i + 2;
                } else if (i + 2 < ntok) {
                    line_h = font_parse_line_height(toks[i + 2]);
                    have_lh = 1;
                    fam_i = i + 3;
                } else {
                    return; /* dangling slash — invalid */
                }
            } else {
                fam_i = i + 1;
            }
            break;
        }

        /* Optional prefixes before <font-size>. "normal" is shared — ignore. */
        int wbold = 0;
        if (strcasecmp(tok, "italic") == 0 || strcasecmp(tok, "oblique") == 0) {
            italic = 1;
        } else if (strcasecmp(tok, "small-caps") == 0) {
            /* font-variant: accepted for CSS parity; no dedicated atlas. */
        } else if (font_token_is_weight(tok, &wbold) && strcasecmp(tok, "normal") != 0) {
            bold = wbold;
        } else if (font_token_is_stretch(tok) && strcasecmp(tok, "normal") != 0) {
            /* font-stretch: accepted / ignored for layout. */
        } else if (strcasecmp(tok, "normal") == 0) {
            /* style|variant|weight|stretch initial — no-op */
        } else {
            return; /* unexpected token before size → invalid shorthand */
        }
    }

    if (!have_size || fam_i < 0 || fam_i >= ntok) return;

    /* Reassemble font-family from the remaining tokens (commas preserved). */
    char family[CSS_MAX_VALUE];
    family[0] = '\0';
    size_t fam_len = 0;
    for (int i = fam_i; i < ntok; i++) {
        size_t tl = strlen(toks[i]);
        if (fam_len && fam_len + 1 < sizeof(family)) family[fam_len++] = ' ';
        if (fam_len + tl >= sizeof(family)) break;
        memcpy(family + fam_len, toks[i], tl);
        fam_len += tl;
        family[fam_len] = '\0';
    }
    if (!family[0]) return;

    /* Commit: CSS requires resetting omitted longhands to initial values. */
    rule->has_font_italic = 1; rule->font_italic = italic;
    rule->has_font_weight = 1; rule->font_bold = bold;
    rule->has_font_size = 1;   rule->font_size = size_px;
    rule->has_line_height = 1; rule->line_height = have_lh ? line_h : 0.0f;
    apply_font_family_value(rule, family);
}

/* CSS inset is a four-sided shorthand, not an alias for a single offset.
 * Keep the expansion here, before layout sees the values, so percentage and
 * calc() offsets continue through the same fast path as their longhands. */
static void parse_inset_shorthand(const char* val, StyleRule* rule) {
    char values[4][CSS_MAX_VALUE];
    int count = 0, depth = 0;
    const char* p = val;

    while (*p && count < 4) {
        while (isspace((unsigned char)*p)) p++;
        if (!*p) break;
        const char* start = p;
        depth = 0;
        while (*p) {
            if (*p == '(') depth++;
            else if (*p == ')' && depth > 0) depth--;
            else if (isspace((unsigned char)*p) && depth == 0) break;
            p++;
        }
        size_t n = (size_t)(p - start);
        if (n >= sizeof(values[0])) n = sizeof(values[0]) - 1;
        memcpy(values[count], start, n);
        values[count][n] = '\0';
        if (n) count++;
    }
    if (!count) return;

    const char* top = values[0];
    const char* right = values[count == 1 ? 0 : 1];
    const char* bottom = values[count < 3 ? 0 : 2];
    const char* left = values[count == 1 ? 0 : (count == 2 ? 1 : 3)];
    apply_one_declaration("top", top, rule);
    apply_one_declaration("right", right, rule);
    apply_one_declaration("bottom", bottom, rule);
    apply_one_declaration("left", left, rule);
}

void parse_declarations(char* declarations, StyleRule* rule) {
    char* prop = declarations;
    while (prop && *prop) {
        char* semi  = strchr(prop, ';');
        if (semi) *semi = '\0';
        char* colon = strchr(prop, ':');
        if (colon) {
            *colon = '\0';
            char* key = prop; char* val = colon + 1;
            trim_whitespace(key); trim_whitespace(val);

            if      (strcmp(key, "background-color") == 0) { rule->has_bg = 1; parse_color(val, &rule->bg_r, &rule->bg_g, &rule->bg_b, &rule->bg_a); }
            else if (strcmp(key, "background") == 0)       { parse_background_shorthand(val, rule); }
            else if (strcmp(key, "background-image") == 0) {
                char path[256];
                if (strcasecmp(val, "none") == 0) {
                    rule->has_bg_image_reset = 1;
                    rule->has_bg_image = 0;
                    rule->bg_image_path[0] = '\0';
                } else if (parse_url(val, path, sizeof(path))) {
                    rule->has_bg_image = 1;
                    rule->has_bg_image_reset = 0;
                    strncpy(rule->bg_image_path, path, sizeof(rule->bg_image_path) - 1);
                    rule->bg_image_path[sizeof(rule->bg_image_path) - 1] = '\0';
                }
            }
            else if (strcmp(key, "color") == 0)            { rule->has_color = 1; parse_color(val, &rule->c_r, &rule->c_g, &rule->c_b, &rule->c_a); }
            else if (strcmp(key, "caret-color") == 0)      { rule->has_caret_color = 1; parse_color(val, &rule->caret_r, &rule->caret_g, &rule->caret_b, &rule->caret_a); }
            else if (strcmp(key, "border-radius") == 0)    { parse_border_radius_shorthand(val, rule); }
            else if (strcmp(key, "border-top-left-radius") == 0)     { rule->has_radius = 1; rule->has_rad_c[0] = 1; rule->rad_c[0] = parse_float_val(val); }
            else if (strcmp(key, "border-top-right-radius") == 0)    { rule->has_radius = 1; rule->has_rad_c[1] = 1; rule->rad_c[1] = parse_float_val(val); }
            else if (strcmp(key, "border-bottom-right-radius") == 0) { rule->has_radius = 1; rule->has_rad_c[2] = 1; rule->rad_c[2] = parse_float_val(val); }
            else if (strcmp(key, "border-bottom-left-radius") == 0)  { rule->has_radius = 1; rule->has_rad_c[3] = 1; rule->rad_c[3] = parse_float_val(val); }
            else if (strcmp(key, "border-width") == 0)     { rule->has_border = 1; rule->border_width = parse_float_val(val); }
            else if (strcmp(key, "outline-width") == 0)  { rule->has_outline = 1; rule->outline_width = parse_float_val(val); }
            else if (strcmp(key, "outline-color") == 0)  {
                rule->has_outline = 1;
                parse_color(val, &rule->ol_r, &rule->ol_g, &rule->ol_b, &rule->ol_a);
            }
            else if (strcmp(key, "outline-offset") == 0) { rule->has_outline = 1; rule->outline_offset = parse_float_val(val); }
            else if (strcmp(key, "outline") == 0) {
                rule->has_outline = 1;
                char obuf[96];
                strncpy(obuf, val, sizeof(obuf) - 1);
                obuf[sizeof(obuf) - 1] = '\0';
                char* sp = strchr(obuf, ' ');
                if (sp) {
                    *sp = '\0';
                    rule->outline_width = parse_float_val(obuf);
                    parse_color(sp + 1, &rule->ol_r, &rule->ol_g, &rule->ol_b, &rule->ol_a);
                } else if (strstr(obuf, "#") || strstr(obuf, "rgb")) {
                    parse_color(obuf, &rule->ol_r, &rule->ol_g, &rule->ol_b, &rule->ol_a);
                    rule->outline_width = 2.0f;
                } else {
                    rule->outline_width = parse_float_val(obuf);
                }
            }
            else if (strcmp(key, "border-color") == 0)     { rule->has_border = 1; parse_color(val, &rule->bd_r, &rule->bd_g, &rule->bd_b, &rule->bd_a); }
            else if (strcmp(key, "border") == 0)           { parse_border_shorthand(val, rule); }
            else if (strcmp(key, "border-top") == 0 || strcmp(key, "border-right") == 0 ||
                     strcmp(key, "border-bottom") == 0 || strcmp(key, "border-left") == 0) {
                float bw = 0.0f, cr = 0.0f, cg = 0.0f, cb = 0.0f, ca = 0.0f;
                int is_none = (strcmp(val, "none") == 0 || strcmp(val, "0") == 0);
                if (!is_none) {
                    char buf2[256]; strncpy(buf2, val, 255); buf2[255] = 0;
                    char* sp2 = buf2;
                    char* ep2;
                    bw = strtof(sp2, &ep2);
                    if (ep2 != sp2) sp2 = ep2;
                    while (*sp2 && !isspace((unsigned char)*sp2)) sp2++;
                    while (isspace((unsigned char)*sp2)) sp2++;
                    if (strncmp(sp2,"solid",5)==0||strncmp(sp2,"dashed",6)==0||strncmp(sp2,"dotted",6)==0) {
                        while (*sp2 && !isspace((unsigned char)*sp2)) sp2++;
                        while (isspace((unsigned char)*sp2)) sp2++;
                    }
                    ca = 1.0f;
                    if (*sp2) parse_color(sp2, &cr, &cg, &cb, &ca);
                }
                if (strcmp(key, "border-top") == 0) {
                    rule->has_border_top = 1; rule->border_top_w = bw;
                    rule->border_top_r = cr; rule->border_top_g = cg;
                    rule->border_top_b = cb; rule->border_top_a = ca;
                } else if (strcmp(key, "border-right") == 0) {
                    rule->has_border_right = 1; rule->border_right_w = bw;
                    rule->border_right_r = cr; rule->border_right_g = cg;
                    rule->border_right_b = cb; rule->border_right_a = ca;
                } else if (strcmp(key, "border-bottom") == 0) {
                    rule->has_border_bottom = 1; rule->border_bottom_w = bw;
                    rule->border_bottom_r = cr; rule->border_bottom_g = cg;
                    rule->border_bottom_b = cb; rule->border_bottom_a = ca;
                } else {
                    rule->has_border_left = 1; rule->border_left_w = bw;
                    rule->border_left_r = cr; rule->border_left_g = cg;
                    rule->border_left_b = cb; rule->border_left_a = ca;
                }
            }
            else if (strcmp(key, "width") == 0) {
                if (strcmp(val,"auto")==0 || strcmp(val,"fit-content")==0 || strcmp(val,"max-content")==0 || strcmp(val,"min-content")==0) {
                    /* leave has_css_width=0, let layout compute */
                } else {
                    rule->has_width = 1;
                    parse_length_calc(val, &rule->width, &rule->pct_w, &rule->raw_w_off);
                    if (rule->pct_w) rule->raw_w = rule->width;
                }
            }
            else if (strcmp(key, "height") == 0) {
                if (strcmp(val, "auto") != 0) {
                    rule->has_height = 1;
                    parse_length_calc(val, &rule->height, &rule->pct_h, &rule->raw_h_off);
                    if (rule->pct_h) rule->raw_h = rule->height;
                }
            }
            else if (strcmp(key, "padding") == 0)          { parse_padding_shorthand(val, rule); }
            else if (strcmp(key, "padding-top") == 0)      { rule->has_padding = 1; rule->pad_t = parse_float_val(val); }
            else if (strcmp(key, "padding-right") == 0)    { rule->has_padding = 1; rule->pad_r = parse_float_val(val); }
            else if (strcmp(key, "padding-bottom") == 0)   { rule->has_padding = 1; rule->pad_b = parse_float_val(val); }
            else if (strcmp(key, "padding-left") == 0)     { rule->has_padding = 1; rule->pad_l = parse_float_val(val); }
            else if (strcmp(key, "margin") == 0)           { parse_margin_shorthand(val, rule); }
            else if (strcmp(key, "margin-top") == 0)       { rule->has_margin = 1; if (strcmp(val,"auto")==0) rule->margin_top_auto=1; else rule->margin_top = parse_float_val(val); }
            else if (strcmp(key, "margin-right") == 0)     { rule->has_margin = 1; if (strcmp(val,"auto")==0) rule->margin_right_auto=1; else rule->margin_right = parse_float_val(val); }
            else if (strcmp(key, "margin-bottom") == 0)    { rule->has_margin = 1; if (strcmp(val,"auto")==0) rule->margin_bottom_auto=1; else rule->margin_bottom = parse_float_val(val); }
            else if (strcmp(key, "margin-left") == 0)      { rule->has_margin = 1; if (strcmp(val,"auto")==0) rule->margin_left_auto=1; else rule->margin_left = parse_float_val(val); }
            else if (strcmp(key, "inset") == 0)            { parse_inset_shorthand(val, rule); }
            else if (strcmp(key, "left") == 0) { rule->has_left = 1; parse_length_calc(val, &rule->left, &rule->pct_left, &rule->raw_left_off); if (rule->pct_left) rule->raw_left = rule->left; }
            else if (strcmp(key, "top") == 0)  { rule->has_top = 1; parse_length_calc(val, &rule->top, &rule->pct_top, &rule->raw_top_off); if (rule->pct_top) rule->raw_top = rule->top; }
            else if (strcmp(key, "bottom") == 0)           { rule->has_bottom = 1; parse_length_calc(val, &rule->bottom, &rule->pct_bottom, &rule->raw_bottom_off); if (rule->pct_bottom) rule->raw_bottom = rule->bottom; }
            else if (strcmp(key, "right") == 0)            { rule->has_right = 1; parse_length_calc(val, &rule->right, &rule->pct_right, &rule->raw_right_off); if (rule->pct_right) rule->raw_right = rule->right; }
            else if (strcmp(key, "position") == 0) {
                rule->has_position = 1;
                rule->position_fixed = (strcmp(val, "fixed") == 0);
                rule->position_sticky = (strcmp(val, "sticky") == 0);
                if (strcmp(val, "absolute") == 0) rule->position_mode = POS_ABSOLUTE;
                else if (strcmp(val, "relative") == 0) rule->position_mode = POS_RELATIVE;
                else if (strcmp(val, "static") == 0) rule->position_mode = POS_STATIC;
            }
            else if (strcmp(key, "opacity") == 0)          { rule->has_opacity = 1; rule->opacity = parse_float_val(val); }
            else if (strcmp(key, "cursor") == 0)           { rule->has_cursor = 1; rule->cursor_type = parse_cursor_type(val); rule->cursor_pointer = (rule->cursor_type == 1); }
            else if (strcmp(key, "display") == 0) {
                rule->has_display = 1;
                if (strcmp(val, "none") == 0) {
                    rule->display_none = 1;
                    rule->display_mode = DISPLAY_NONE;
                } else if (strcmp(val, "flex") == 0 || strcmp(val, "inline-flex") == 0) {
                    /* Inline-ness changes the outer formatting context only.
                       A flex item's inner layout is identical, and the Luna
                       renderer has no separate inline formatting path. */
                    rule->display_none = 0;
                    rule->display_mode = DISPLAY_FLEX;
                } else if (strcmp(val, "grid") == 0 || strcmp(val, "inline-grid") == 0) {
                    rule->display_none = 0;
                    rule->display_mode = DISPLAY_GRID;
                } else {
                    rule->display_none = 0;
                    rule->display_mode = DISPLAY_BLOCK;
                }
            }
            else if (strcmp(key, "flex-direction") == 0) {
                rule->has_flex_direction = 1;
                rule->flex_direction = parse_flex_direction(val);
            }
            else if (strcmp(key, "justify-content") == 0) {
                rule->has_justify_content = 1;
                rule->justify_content = parse_justify_content(val);
            }
            else if (strcmp(key, "align-items") == 0) {
                rule->has_align_items = 1;
                rule->align_items = parse_align_items(val);
            }
            else if (strcmp(key, "justify-items") == 0) {
                rule->has_justify_items = 1;
                rule->justify_items = parse_align_items(val);
            }
            else if (strcmp(key, "place-items") == 0) {
                int a = parse_align_items(val);
                rule->has_align_items = 1;
                rule->has_justify_items = 1;
                rule->align_items = a;
                rule->justify_items = a;
            }
            else if (strcmp(key, "align-content") == 0) {
                rule->has_align_content = 1;
                rule->align_content = parse_align_content(val);
            }
            else if (strcmp(key, "place-content") == 0) {
                char pcbuf[64];
                strncpy(pcbuf, val, sizeof(pcbuf) - 1);
                pcbuf[sizeof(pcbuf) - 1] = '\0';
                char* sp = strchr(pcbuf, ' ');
                if (sp) {
                    *sp = '\0';
                    rule->has_align_content = 1;
                    rule->has_justify_content = 1;
                    rule->align_content = parse_align_content(pcbuf);
                    rule->justify_content = parse_justify_content(sp + 1);
                } else {
                    rule->has_align_content = 1;
                    rule->has_justify_content = 1;
                    rule->align_content = parse_align_content(pcbuf);
                    rule->justify_content = parse_justify_content(pcbuf);
                }
            }
            else if (strcmp(key, "flex-wrap") == 0) {
                rule->has_flex_wrap = 1;
                rule->flex_wrap = parse_flex_wrap(val);
            }
            else if (strcmp(key, "justify-self") == 0) {
                rule->has_justify_self = 1;
                rule->justify_self = parse_align_self(val);
            }
            else if (strcmp(key, "place-self") == 0) {
                char psbuf[64];
                strncpy(psbuf, val, sizeof(psbuf) - 1);
                psbuf[sizeof(psbuf) - 1] = '\0';
                char* sp = strchr(psbuf, ' ');
                if (sp) {
                    *sp = '\0';
                    rule->has_align_self = 1;
                    rule->has_justify_self = 1;
                    rule->align_self = parse_align_self(psbuf);
                    rule->justify_self = parse_align_self(sp + 1);
                } else {
                    int v = parse_align_self(psbuf);
                    rule->has_align_self = 1;
                    rule->has_justify_self = 1;
                    rule->align_self = v;
                    rule->justify_self = v;
                }
            }
            else if (strcmp(key, "align-self") == 0) {
                rule->has_align_self = 1;
                rule->align_self = parse_align_self(val);
            }
            else if (strcmp(key, "box-sizing") == 0) {
                rule->has_box_sizing = 1;
                rule->box_sizing = parse_box_sizing(val);
            }
            else if (strcmp(key, "grid-template-columns") == 0) {
                rule->has_grid_template_columns = 1;
                parse_grid_tracks(val, rule->grid_col_track, rule->grid_col_type,
                                  rule->grid_col_min, &rule->grid_col_count);
            }
            else if (strcmp(key, "grid-template-rows") == 0) {
                rule->has_grid_template_rows = 1;
                parse_grid_tracks(val, rule->grid_row_track, rule->grid_row_type,
                                  rule->grid_row_min, &rule->grid_row_count);
            }
            else if (strcmp(key, "grid-template-areas") == 0) {
                parse_grid_template_areas(val, rule);
            }
            else if (strcmp(key, "column-gap") == 0) {
                rule->has_column_gap = 1;
                rule->grid_col_gap = parse_float_val(val);
            }
            else if (strcmp(key, "row-gap") == 0) {
                rule->has_row_gap = 1;
                rule->grid_row_gap = parse_float_val(val);
            }
            else if (strcmp(key, "grid-auto-flow") == 0) {
                rule->has_grid_auto_flow = 1;
                rule->grid_auto_flow = parse_grid_auto_flow(val);
            }
            else if (strcmp(key, "grid-auto-rows") == 0) {
                rule->has_grid_auto_rows = 1;
                parse_single_grid_track(val, &rule->grid_auto_row_track,
                                        &rule->grid_auto_row_type, &rule->grid_auto_row_min);
            }
            else if (strcmp(key, "grid-auto-columns") == 0) {
                rule->has_grid_auto_columns = 1;
                parse_single_grid_track(val, &rule->grid_auto_col_track,
                                        &rule->grid_auto_col_type, &rule->grid_auto_col_min);
            }
            else if (strcmp(key, "grid-column") == 0) {
                int span = 1;
                int col = parse_grid_line_val(val, &span);
                if (col == -2) { rule->has_grid_column_span = 1; rule->grid_col_span = span; }
                else { rule->has_grid_column = 1; rule->grid_col = col; rule->grid_col_span = span; }
            }
            else if (strcmp(key, "grid-row") == 0) {
                int span = 1;
                int row = parse_grid_line_val(val, &span);
                if (row == -2) { rule->has_grid_row_span = 1; rule->grid_row_span = span; }
                else { rule->has_grid_row = 1; rule->grid_row = row; rule->grid_row_span = span; }
            }
            else if (strcmp(key, "grid-area") == 0) {
                rule->has_grid_area = 1;
                strncpy(rule->grid_area_name, val, 31);
                rule->grid_area_name[31] = '\0';
                trim_whitespace(rule->grid_area_name);
            }
            else if (strcmp(key, "overflow") == 0) {
                int m = parse_overflow(val);
                rule->has_overflow_x = 1;
                rule->has_overflow_y = 1;
                rule->overflow_x = m;
                rule->overflow_y = m;
            }
            else if (strcmp(key, "overflow-x") == 0) {
                rule->has_overflow_x = 1;
                rule->overflow_x = parse_overflow(val);
            }
            else if (strcmp(key, "overflow-y") == 0) {
                rule->has_overflow_y = 1;
                rule->overflow_y = parse_overflow(val);
            }
            else if (strcmp(key, "scrollbar-width") == 0) {
                rule->has_scrollbar_width = 1;
                rule->scrollbar_width = parse_scrollbar_width(val);
            }
            else if (strcmp(key, "scrollbar-color") == 0) {
                parse_scrollbar_color(val, rule);
            }
            else if (strcmp(key, "scroll-behavior") == 0) {
                rule->has_scroll_behavior = 1;
                rule->scroll_smooth = (strstr(val, "smooth") != NULL);
            }
            else if (strcmp(key, "scroll-snap-type") == 0) {
                rule->has_scroll_snap_type = 1;
                if (strstr(val, "none"))
                    rule->scroll_snap_type = 0;
                else if (strstr(val, "proximity"))
                    rule->scroll_snap_type = 2;
                else
                    rule->scroll_snap_type = 1;
            }
            else if (strcmp(key, "scroll-snap-align") == 0) {
                rule->has_scroll_snap_align = 1;
                if (strstr(val, "center"))
                    rule->scroll_snap_align = 1;
                else if (strstr(val, "end"))
                    rule->scroll_snap_align = 2;
                else
                    rule->scroll_snap_align = 0;
            }
            else if (strcmp(key, "scroll-margin") == 0) {
                parse_scroll_margin_shorthand(val, rule);
            }
            else if (strcmp(key, "scroll-margin-top") == 0) {
                rule->has_scroll_margin = 1;
                rule->scroll_margin_top = parse_float_val(val);
            }
            else if (strcmp(key, "scroll-margin-right") == 0) {
                rule->has_scroll_margin = 1;
                rule->scroll_margin_right = parse_float_val(val);
            }
            else if (strcmp(key, "scroll-margin-bottom") == 0) {
                rule->has_scroll_margin = 1;
                rule->scroll_margin_bottom = parse_float_val(val);
            }
            else if (strcmp(key, "scroll-margin-left") == 0) {
                rule->has_scroll_margin = 1;
                rule->scroll_margin_left = parse_float_val(val);
            }
            else if (strcmp(key, "scroll-padding") == 0) {
                parse_scroll_padding_shorthand(val, rule);
            }
            else if (strcmp(key, "scroll-padding-top") == 0) {
                rule->has_scroll_padding = 1;
                rule->scroll_padding_top = parse_float_val(val);
            }
            else if (strcmp(key, "scroll-padding-right") == 0) {
                rule->has_scroll_padding = 1;
                rule->scroll_padding_right = parse_float_val(val);
            }
            else if (strcmp(key, "scroll-padding-bottom") == 0) {
                rule->has_scroll_padding = 1;
                rule->scroll_padding_bottom = parse_float_val(val);
            }
            else if (strcmp(key, "scroll-padding-left") == 0) {
                rule->has_scroll_padding = 1;
                rule->scroll_padding_left = parse_float_val(val);
            }
            else if (strcmp(key, "min-width") == 0) {
                rule->has_min_width = 1;
                rule->min_width = parse_float_val(val);
            }
            else if (strcmp(key, "min-height") == 0) {
                rule->has_min_height = 1;
                rule->min_height = parse_float_val(val);
            }
            else if (strcmp(key, "max-width") == 0) {
                /* `none` removes the constraint.  Parsing it as zero
                 * collapses flex items that clear a previous max-width. */
                if (strcmp(val, "none") != 0) {
                    rule->has_max_width = 1;
                    parse_length_calc(val, &rule->max_width,
                                      &rule->max_width_pct,
                                      &rule->raw_max_width_off);
                    if (rule->max_width_pct)
                        rule->raw_max_width = rule->max_width;
                }
            }
            else if (strcmp(key, "max-height") == 0) {
                /* Keep percentage max-height as a ratio.  Parsing `90%` with
                 * parse_float_val() produced 90px and collapsed modal panels. */
                if (strcmp(val, "none") != 0) {
                    rule->has_max_height = 1;
                    parse_length_calc(val, &rule->max_height,
                                      &rule->max_height_pct,
                                      &rule->raw_max_height_off);
                    if (rule->max_height_pct)
                        rule->raw_max_height = rule->max_height;
                }
            }
            else if (strcmp(key, "gap") == 0) {
                rule->has_gap = 1;
                char gbuf[64];
                strncpy(gbuf, val, sizeof(gbuf) - 1);
                gbuf[sizeof(gbuf) - 1] = '\0';
                char* g2 = strchr(gbuf, ' ');
                if (g2) {
                    *g2 = '\0';
                    rule->flex_gap = parse_float_val(gbuf);
                    rule->has_column_gap = 1;
                    rule->has_row_gap = 1;
                    rule->grid_col_gap = rule->flex_gap;
                    rule->grid_row_gap = parse_float_val(g2 + 1);
                } else {
                    rule->flex_gap = parse_float_val(gbuf);
                }
            }
            else if (strcmp(key, "flex-grow") == 0) {
                rule->has_flex_grow = 1;
                rule->flex_grow = parse_float_val(val);
            }
            else if (strcmp(key, "flex-shrink") == 0) {
                rule->has_flex_shrink = 1;
                rule->flex_shrink = parse_float_val(val);
            }
            else if (strcmp(key, "flex-basis") == 0) {
                rule->has_flex_basis = 1;
                if (strstr(val, "auto")) {
                    rule->flex_basis_auto = 1;
                } else {
                    rule->flex_basis = parse_float_val(val);
                    rule->flex_basis_auto = 0;
                }
            }
            else if (strcmp(key, "flex") == 0) {
                char fbuf[64];
                strncpy(fbuf, val, sizeof(fbuf) - 1);
                fbuf[sizeof(fbuf) - 1] = '\0';
                char* tok = strtok(fbuf, " \t/");
                int part = 0;
                while (tok) {
                    trim_whitespace(tok);
                    if (part == 0) {
                        rule->has_flex_grow = 1;
                        rule->flex_grow = parse_float_val(tok);
                    } else if (part == 1) {
                        rule->has_flex_shrink = 1;
                        rule->flex_shrink = parse_float_val(tok);
                    } else if (part == 2) {
                        rule->has_flex_basis = 1;
                        if (strstr(tok, "auto")) rule->flex_basis_auto = 1;
                        else {
                            rule->flex_basis = parse_float_val(tok);
                            rule->flex_basis_auto = 0;
                        }
                    }
                    part++;
                    tok = strtok(NULL, " \t/");
                }
            }
            else if (strcmp(key, "visibility") == 0)       { rule->has_visibility = 1; rule->visibility_hidden = (strcmp(val, "hidden") == 0); }
            else if (strcmp(key, "pointer-events") == 0) { rule->has_pointer_events = 1; rule->pointer_events_none = (strcmp(val, "none") == 0); }
            else if (strcmp(key, "z-index") == 0)          { rule->has_z_index = 1; rule->z_index = atoi(val); }
            else if (strcmp(key, "transform") == 0)        { parse_transform(val, rule); }
            else if (strcmp(key, "transition") == 0 || strcmp(key, "transition-duration") == 0) {
                const char* p = val; float sec = 0.0f; char numbuf[32] = {0};
                while (*p) {
                    if (isdigit((unsigned char)*p) || *p == '.') {
                        int i = 0;
                        while ((isdigit((unsigned char)*p) || *p == '.') && i < 31) numbuf[i++] = *p++;
                        numbuf[i] = 0;
                        if (strncmp(p, "ms", 2) == 0) sec = (float)atof(numbuf) / 1000.0f;
                        else sec = (float)atof(numbuf);
                        break;
                    }
                    p++;
                }
                if (sec > 0.0f) { rule->has_transition = 1; rule->transition_duration = sec; }
            }
            else if (strcmp(key, "animation") == 0 || strcmp(key, "animation-name") == 0) {
                if (strcmp(key, "animation-name") == 0) {
                    rule->has_animation = 1;
                    strncpy(rule->anim_name, val, sizeof(rule->anim_name) - 1);
                } else {
                    rule->has_animation = 1;
                    char abuf[128];
                    strncpy(abuf, val, sizeof(abuf) - 1);
                    abuf[sizeof(abuf) - 1] = '\0';
                    char* tok = strtok(abuf, " \t,");
                    int part = 0;
                    while (tok) {
                        trim_whitespace(tok);
                        if (part == 0 && strcmp(tok, "none") != 0)
                            strncpy(rule->anim_name, tok, sizeof(rule->anim_name) - 1);
                        else if (strchr(tok, 's') || isdigit((unsigned char)tok[0])) {
                            float sec = 0.0f;
                            if (strstr(tok, "ms")) sec = (float)atof(tok) / 1000.0f;
                            else sec = (float)atof(tok);
                            if (part == 0 && rule->anim_duration <= 0.0f) rule->anim_duration = sec;
                            else if (rule->anim_duration <= 0.0f) rule->anim_duration = sec;
                            else rule->anim_delay = sec;
                        } else if (strstr(tok, "ease-in-out")) rule->anim_easing = 1;
                        else if (strstr(tok, "linear")) rule->anim_easing = 0;
                        else if (strcmp(tok, "infinite") == 0) rule->anim_infinite = 1;
                        else if (strcmp(tok, "alternate") == 0) rule->anim_alternate = 1;
                        part++;
                        tok = strtok(NULL, " \t,");
                    }
                }
            }
            else if (strcmp(key, "animation-duration") == 0) {
                rule->has_animation = 1;
                if (strstr(val, "ms")) rule->anim_duration = (float)atof(val) / 1000.0f;
                else rule->anim_duration = (float)atof(val);
            }
            else if (strcmp(key, "animation-delay") == 0) {
                rule->has_animation = 1;
                if (strstr(val, "ms")) rule->anim_delay = (float)atof(val) / 1000.0f;
                else rule->anim_delay = (float)atof(val);
            }
            else if (strcmp(key, "animation-iteration-count") == 0) {
                rule->has_animation = 1;
                if (strcmp(val, "infinite") == 0) rule->anim_infinite = 1;
            }
            else if (strcmp(key, "animation-direction") == 0) {
                rule->has_animation = 1;
                if (strcmp(val, "alternate") == 0) rule->anim_alternate = 1;
            }
            else if (strcmp(key, "animation-timing-function") == 0) {
                rule->has_animation = 1;
                rule->anim_easing = (strstr(val, "ease-in-out") != NULL) ? 1 : 0;
            }
            else if (strcmp(key, "text-align") == 0) {
                rule->has_text_align = 1;
                if      (strcmp(val, "center") == 0) rule->text_align = 1;
                else if (strcmp(val, "right") == 0)  rule->text_align = 2;
                else                                 rule->text_align = 0;
            }
            else if (strcmp(key, "font") == 0)            { parse_font_shorthand(val, rule); }
            else if (strcmp(key, "font-size") == 0)   { rule->has_font_size = 1; rule->font_size = parse_float_val(val); }
            else if (strcmp(key, "font-weight") == 0) { rule->has_font_weight = 1; rule->font_bold = (strstr(val, "bold") != NULL || atoi(val) >= 600); }
            else if (strcmp(key, "line-height") == 0) {
                rule->has_line_height = 1;
                if (strcmp(val, "normal") == 0) rule->line_height = 0.0f;
                else {
                    char* endp = NULL;
                    float lh = strtof(val, &endp);
                    while (endp && isspace((unsigned char)*endp)) endp++;
                    /* A unitless line-height is a multiplier of font-size,
                     * not a pixel value.  Keep it negative until the final
                     * font size is known so inheritance stays correct. */
                    rule->line_height = (endp && *endp == '\0' && lh > 0.0f)
                        ? -lh : parse_float_val(val);
                }
            }
            else if (strcmp(key, "white-space") == 0) {
                rule->has_white_space = 1;
                rule->white_space = (strstr(val, "nowrap") != NULL) ? 1 : 0;
            }
            else if (strcmp(key, "text-overflow") == 0) {
                rule->has_text_overflow = 1;
                rule->text_overflow = (strstr(val, "ellipsis") != NULL) ? 1 : 0;
            }
            else if (strcmp(key, "overflow-wrap") == 0 || strcmp(key, "word-wrap") == 0 ||
                     strcmp(key, "word-break") == 0) {
                rule->has_overflow_wrap = 1;
                rule->overflow_wrap =
                    (strstr(val, "break") != NULL || strstr(val, "anywhere") != NULL) ? 1 : 0;
            }
            else if (strcmp(key, "letter-spacing") == 0) {
                rule->has_letter_spacing = 1;
                rule->letter_spacing = (strcmp(val, "normal") == 0) ? 0.0f : parse_float_val(val);
            }
            else if (strcmp(key, "text-transform") == 0) {
                rule->has_text_transform = 1;
                if      (strstr(val, "uppercase"))  rule->text_transform = 1;
                else if (strstr(val, "lowercase"))  rule->text_transform = 2;
                else if (strstr(val, "capitalize")) rule->text_transform = 3;
                else                                rule->text_transform = 0;
            }
            else if (strcmp(key, "text-decoration") == 0 || strcmp(key, "text-decoration-line") == 0) {
                rule->has_text_decoration = 1;
                rule->text_decoration = 0;
                if (strstr(val, "underline"))    rule->text_decoration |= 1;
                if (strstr(val, "line-through")) rule->text_decoration |= 2;
            }
            else if (strcmp(key, "text-shadow") == 0) {
                if (strcmp(val, "none") == 0) {
                    rule->has_text_shadow = 1;
                    rule->tsh_a = 0.0f;
                } else {
                    const char* sp = val;
                    float dx = read_css_length(&sp);
                    float dy = read_css_length(&sp);
                    float bl = 0.0f;
                    if (*sp && (*sp == '-' || *sp == '+' || isdigit((unsigned char)*sp) || *sp == '.'))
                        bl = read_css_length(&sp);
                    char colbuf[96] = {0};
                    strncpy(colbuf, sp, sizeof(colbuf) - 1);
                    trim_whitespace(colbuf);
                    if (colbuf[0]) {
                        rule->has_text_shadow = 1;
                        rule->tsh_dx = dx; rule->tsh_dy = dy; rule->tsh_blur = bl;
                        rule->tsh_a = 1.0f;
                        parse_color(colbuf, &rule->tsh_r, &rule->tsh_g, &rule->tsh_b, &rule->tsh_a);
                    }
                }
            }
            else if (strcmp(key, "box-shadow") == 0)  { parse_box_shadow(val, rule); }
            else if (strcmp(key, "letter-spacing") == 0) {
                rule->has_letter_spacing = 1;
                rule->letter_spacing = (strcmp(val, "normal") == 0) ? 0.0f : parse_float_val(val);
            }
            else if (strcmp(key, "text-transform") == 0) {
                rule->has_text_transform = 1;
                if (strstr(val, "upper")) rule->text_transform = 1;
                else if (strstr(val, "lower")) rule->text_transform = 2;
                else if (strstr(val, "capital")) rule->text_transform = 3;
                else rule->text_transform = 0;
            }
            else if (strcmp(key, "border-top-color") == 0 || strcmp(key, "border-right-color") == 0 ||
                     strcmp(key, "border-bottom-color") == 0 || strcmp(key, "border-left-color") == 0) {
                float cr = 0, cg = 0, cb = 0, ca = 1.0f;
                parse_color(val, &cr, &cg, &cb, &ca);
                if (strcmp(key, "border-top-color") == 0) {
                    rule->has_border_top = 1;
                    rule->border_top_r = cr; rule->border_top_g = cg;
                    rule->border_top_b = cb; rule->border_top_a = ca;
                } else if (strcmp(key, "border-right-color") == 0) {
                    rule->has_border_right = 1;
                    rule->border_right_r = cr; rule->border_right_g = cg;
                    rule->border_right_b = cb; rule->border_right_a = ca;
                } else if (strcmp(key, "border-bottom-color") == 0) {
                    rule->has_border_bottom = 1;
                    rule->border_bottom_r = cr; rule->border_bottom_g = cg;
                    rule->border_bottom_b = cb; rule->border_bottom_a = ca;
                } else {
                    rule->has_border_left = 1;
                    rule->border_left_r = cr; rule->border_left_g = cg;
                    rule->border_left_b = cb; rule->border_left_a = ca;
                }
            }
            else if (strcmp(key, "border-top-width") == 0 || strcmp(key, "border-right-width") == 0 ||
                     strcmp(key, "border-bottom-width") == 0 || strcmp(key, "border-left-width") == 0) {
                float bw2 = parse_float_val(val);
                if (strcmp(key, "border-top-width") == 0)    { rule->has_border_top = 1;    rule->border_top_w = bw2; }
                else if (strcmp(key, "border-right-width") == 0)  { rule->has_border_right = 1;  rule->border_right_w = bw2; }
                else if (strcmp(key, "border-bottom-width") == 0) { rule->has_border_bottom = 1; rule->border_bottom_w = bw2; }
                else                                          { rule->has_border_left = 1;   rule->border_left_w = bw2; }
            }
            else if (strcmp(key, "content") == 0) {
                rule->has_content = 1;
                rule->content[0] = '\0';
                if (val[0] == '"' || val[0] == '\'') {
                    char q = val[0];
                    const char* s = val + 1;
                    const char* e = strrchr(s, q);
                    if (e && e > s) {
                        int n = (int)(e - s);
                        if (n > (int)sizeof(rule->content) - 1) n = (int)sizeof(rule->content) - 1;
                        /* Decode CSS escapes (\XX.. / \char) into UTF-8. */
                        int wi = 0;
                        for (int ri = 0; ri < n && wi < (int)sizeof(rule->content) - 1; ) {
                            if (s[ri] == '\\' && ri + 1 < n) {
                                ri++;
                                if (isxdigit((unsigned char)s[ri])) {
                                    unsigned int cp = 0;
                                    int digits = 0;
                                    while (ri < n && digits < 6 &&
                                           isxdigit((unsigned char)s[ri])) {
                                        char ch = s[ri++];
                                        cp <<= 4;
                                        if (ch >= '0' && ch <= '9') cp |= (unsigned)(ch - '0');
                                        else if (ch >= 'a' && ch <= 'f') cp |= (unsigned)(ch - 'a' + 10);
                                        else if (ch >= 'A' && ch <= 'F') cp |= (unsigned)(ch - 'A' + 10);
                                        digits++;
                                    }
                                    if (ri < n && s[ri] == ' ') ri++; /* optional trailing space */
                                    if (cp == 0) continue; /* keep C-string safe */
                                    if (cp <= 0x7Fu && wi < (int)sizeof(rule->content) - 1) {
                                        rule->content[wi++] = (char)cp;
                                    } else if (cp <= 0x7FFu && wi + 1 < (int)sizeof(rule->content) - 1) {
                                        rule->content[wi++] = (char)(0xC0u | (cp >> 6));
                                        rule->content[wi++] = (char)(0x80u | (cp & 0x3Fu));
                                    } else if (cp <= 0xFFFFu && wi + 2 < (int)sizeof(rule->content) - 1) {
                                        rule->content[wi++] = (char)(0xE0u | (cp >> 12));
                                        rule->content[wi++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
                                        rule->content[wi++] = (char)(0x80u | (cp & 0x3Fu));
                                    } else if (cp <= 0x10FFFFu && wi + 3 < (int)sizeof(rule->content) - 1) {
                                        rule->content[wi++] = (char)(0xF0u | (cp >> 18));
                                        rule->content[wi++] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
                                        rule->content[wi++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
                                        rule->content[wi++] = (char)(0x80u | (cp & 0x3Fu));
                                    }
                                } else {
                                    /* \newline ignored; otherwise take next char literally */
                                    if (s[ri] == '\n' || s[ri] == '\r') { ri++; continue; }
                                    rule->content[wi++] = s[ri++];
                                }
                            } else {
                                rule->content[wi++] = s[ri++];
                            }
                        }
                        rule->content[wi] = '\0';
                    }
                } else if (strcmp(val, "none") != 0 && strcmp(val, "normal") != 0) {
                    strncpy(rule->content, val, sizeof(rule->content) - 1);
                }
            }
            else if (strcmp(key, "filter") == 0) {
                rule->has_filter = 1;
                rule->filter_brightness = 1.0f;
                rule->filter_contrast   = 1.0f;
                rule->filter_saturate   = 1.0f;
                rule->filter_hue        = 0.0f;
                const char* fp = val;
                while (*fp) {
                    while (isspace((unsigned char)*fp)) fp++;
                    if (strncmp(fp, "brightness(", 11) == 0) {
                        fp += 11;
                        rule->filter_brightness = strtof(fp, (char**)&fp);
                        while (*fp && *fp != ')') fp++;
                        if (*fp == ')') fp++;
                    } else if (strncmp(fp, "contrast(", 9) == 0) {
                        fp += 9;
                        rule->filter_contrast = strtof(fp, (char**)&fp);
                        if (rule->filter_contrast > 2.0f) rule->filter_contrast /= 100.0f; /* % form */
                        while (*fp && *fp != ')') fp++;
                        if (*fp == ')') fp++;
                    } else if (strncmp(fp, "saturate(", 9) == 0) {
                        fp += 9;
                        rule->filter_saturate = strtof(fp, (char**)&fp);
                        if (rule->filter_saturate > 5.0f) rule->filter_saturate /= 100.0f;
                        while (*fp && *fp != ')') fp++;
                        if (*fp == ')') fp++;
                    } else if (strncmp(fp, "hue-rotate(", 11) == 0) {
                        fp += 11;
                        float deg = strtof(fp, (char**)&fp);
                        rule->filter_hue = deg * (float)M_PI / 180.0f;
                        while (*fp && *fp != ')') fp++;
                        if (*fp == ')') fp++;
                    } else if (strncmp(fp, "opacity(", 8) == 0) {
                        fp += 8;
                        float op = strtof(fp, (char**)&fp);
                        rule->has_opacity = 1; rule->opacity = op;
                        while (*fp && *fp != ')') fp++;
                        if (*fp == ')') fp++;
                    } else if (strncmp(fp, "blur(", 5) == 0) {
                        fp += 5;
                        float bv = strtof(fp, (char**)&fp);
                        rule->filter_blur = bv > 0.0f ? bv : 0.0f;
                        while (*fp && *fp != ')') fp++;
                        if (*fp == ')') fp++;
                    } else if (strcmp(val, "none") == 0) {
                        rule->has_filter = 0; break;
                    } else { break; }
                }
            }
            else if (strcmp(key, "font-style") == 0) {
                rule->has_font_italic = 1;
                rule->font_italic = (strncmp(val, "italic", 6) == 0 || strncmp(val, "oblique", 7) == 0) ? 1 : 0;
            }
            else if (strcmp(key, "aspect-ratio") == 0) {
                rule->has_aspect_ratio = 1;
                float w = strtof(val, NULL);
                const char* sl = strchr(val, '/');
                float h = sl ? strtof(sl + 1, NULL) : 1.0f;
                rule->aspect_ratio = (h > 0.0f) ? w / h : w;
            }
            else if (strcmp(key, "background-size") == 0) {
                rule->has_bg_size = 1;
                if (strcmp(val, "cover") == 0)   { rule->bg_size_mode = 1; }
                else if (strcmp(val, "contain") == 0) { rule->bg_size_mode = 2; }
                else if (strcmp(val, "auto") == 0)    { rule->bg_size_mode = 0; }
                else {
                    rule->bg_size_mode = 3;
                    rule->bg_size_w = parse_float_val(val);
                    const char* sp2 = strchr(val, ' ');
                    rule->bg_size_h = sp2 ? parse_float_val(sp2 + 1) : rule->bg_size_w;
                }
            }
            else if (strcmp(key, "background-position") == 0) {
                rule->has_bg_pos = 1;
                if (strstr(val, "center")) { rule->bg_pos_x = 0.5f; rule->bg_pos_y = 0.5f; }
                else {
                    rule->bg_pos_x = parse_float_val(val) / 100.0f;
                    const char* sp2 = strchr(val, ' ');
                    rule->bg_pos_y = sp2 ? parse_float_val(sp2 + 1) / 100.0f : 0.5f;
                }
            }
            else if (strcmp(key, "backdrop-filter") == 0 ||
                     strcmp(key, "-webkit-backdrop-filter") == 0) {
                /* CSS filter functions compose left-to-right. Saturation and
                 * brightness are color-only operations, so applying them in
                 * the final backdrop sample costs no additional render pass. */
                rule->backdrop_saturate = 1.0f;
                rule->backdrop_brightness = 1.0f;
                const char* bp = strstr(val, "blur(");
                if (bp) {
                    bp += 5;
                    char* endp;
                    float radius = strtof(bp, &endp);
                    if (endp != bp) {
                        rule->has_backdrop_blur = 1;
                        rule->backdrop_blur_radius = radius > 0.0f ? radius : 8.0f;
                    }
                }
                bp = strstr(val, "saturate(");
                if (bp) {
                    bp += 9;
                    char* endp = NULL;
                    rule->backdrop_saturate = strtof(bp, &endp);
                    while (endp && isspace((unsigned char)*endp)) endp++;
                    if (endp && *endp == '%') rule->backdrop_saturate /= 100.0f;
                    if (rule->backdrop_saturate < 0.0f) rule->backdrop_saturate = 0.0f;
                }
                bp = strstr(val, "brightness(");
                if (bp) {
                    bp += 11;
                    char* endp = NULL;
                    rule->backdrop_brightness = strtof(bp, &endp);
                    while (endp && isspace((unsigned char)*endp)) endp++;
                    if (endp && *endp == '%') rule->backdrop_brightness /= 100.0f;
                    if (rule->backdrop_brightness < 0.0f) rule->backdrop_brightness = 0.0f;
                }
            }
            else if (strcmp(key, "background-clip") == 0 ||
                     strcmp(key, "-webkit-background-clip") == 0) {
                if (strcmp(val, "text") == 0) rule->has_bg_clip_text = 1;
            }
            else if (strcmp(key, "mix-blend-mode") == 0) {
                rule->has_mix_blend_mode = 1;
                if (strcmp(val, "screen") == 0)        rule->mix_blend_mode = 1;
                else if (strcmp(val, "multiply") == 0) rule->mix_blend_mode = 2;
                else if (strcmp(val, "add") == 0 ||
                         strcmp(val, "lighter") == 0)  rule->mix_blend_mode = 3;
                else if (strcmp(val, "overlay") == 0)  rule->mix_blend_mode = 4;
                else                                   rule->mix_blend_mode = 0;
            }
            else if (strcmp(key, "font-family") == 0) {
                /* Keep normal text on the fast pre-baked atlas.  Icon faces
                   select their matching dynamic atlas face, as browsers do. */
                apply_font_family_value(rule, val);
            }
            else if (strcmp(key, "user-select") == 0 ||
                     strcmp(key, "-webkit-font-smoothing") == 0) {
                /* Accepted for CSS parity; drawing stays CSS-box based. */
            }
            else if (strcmp(key, "inset") == 0) { parse_inset_shorthand(val, rule); }
        }
        if (!semi) break;
        prop = semi + 1;
    }
}

int cmp_rules_by_specificity(const void* a, const void* b) {
    const StyleRule* ra = (const StyleRule*)a;
    const StyleRule* rb = (const StyleRule*)b;
    if (ra->specificity != rb->specificity)
        return ra->specificity < rb->specificity ? -1 : 1;
    if (ra->source_order != rb->source_order)
        return ra->source_order < rb->source_order ? -1 : 1;
    return 0;
}

/* ── Build a StyleRule from one parsed CSSRule ──────────────────────── */

/* Parse an :nth-child() argument: odd, even, N, An+B, n, -n+B, ... */
static void parse_nth_arg(const char* arg, int* out_a, int* out_b) {
    char buf[64] = {0};
    snprintf(buf, sizeof(buf), "%.*s", (int)(sizeof(buf) - 1), arg ? arg : "");
    trim_whitespace(buf);
    *out_a = 0; *out_b = 0;
    if (strcasecmp(buf, "odd") == 0)  { *out_a = 2; *out_b = 1; return; }
    if (strcasecmp(buf, "even") == 0) { *out_a = 2; *out_b = 0; return; }
    char* n = strchr(buf, 'n');
    if (!n) n = strchr(buf, 'N');
    if (!n) { *out_b = atoi(buf); return; }
    /* An+B */
    *n = '\0';
    if (buf[0] == '\0' || strcmp(buf, "+") == 0) *out_a = 1;
    else if (strcmp(buf, "-") == 0) *out_a = -1;
    else *out_a = atoi(buf);
    char* rest = n + 1;
    while (isspace((unsigned char)*rest)) rest++;
    if (*rest == '+' || *rest == '-') {
        int sign = (*rest == '-') ? -1 : 1;
        rest++;
        *out_b = sign * atoi(rest);
    }
}

/* Fill structural pseudo-class state (:first-child, :nth-child, :not, ...) */
static void apply_structural_pseudo(SimpleSelector* ss, const CSSSelectorPart* p) {
    if      (strcmp(p->name, "first-child") == 0) ss->is_first_child = 1;
    else if (strcmp(p->name, "last-child")  == 0) ss->is_last_child = 1;
    else if (strcmp(p->name, "nth-child")   == 0) {
        ss->has_nth = 1;
        parse_nth_arg(p->pseudo_arg, &ss->nth_a, &ss->nth_b);
    }
    else if (strcmp(p->name, "not") == 0) {
        char buf[96] = {0};
        snprintf(buf, sizeof(buf), "%.*s", (int)(sizeof(buf) - 1), p->pseudo_arg);
        trim_whitespace(buf);
        if (buf[0] == '.')      snprintf(ss->not_class, sizeof(ss->not_class), "%.*s", (int)(sizeof(ss->not_class) - 1), buf + 1);
        else if (buf[0] == '#') snprintf(ss->not_id, sizeof(ss->not_id), "%.*s", (int)(sizeof(ss->not_id) - 1), buf + 1);
        else if (buf[0])        snprintf(ss->not_type, sizeof(ss->not_type), "%.*s", (int)(sizeof(ss->not_type) - 1), buf);
        if (buf[0]) ss->has_not = 1;
    }
}

/* Convert one cssparser.h CSSCompound's type/id/class/structural parts. */
static void convert_compound_parts(const CSSCompound* cmp, SimpleSelector* ss) {
    for (int pi = 0; pi < cmp->part_count; pi++) {
        const CSSSelectorPart* p = &cmp->parts[pi];
        switch (p->type) {
        case CSS_SEL_TYPE:
            strncpy(ss->sel_type, p->name, sizeof(ss->sel_type) - 1);
            break;
        case CSS_SEL_ID:
            strncpy(ss->sel_id, p->name[0] == '#' ? p->name + 1 : p->name, sizeof(ss->sel_id) - 1);
            break;
        case CSS_SEL_CLASS:
            if (ss->sel_class_count < MAX_SEL_CLASSES)
                strncpy(ss->sel_classes[ss->sel_class_count++], p->name, sizeof(ss->sel_classes[0]) - 1);
            break;
        case CSS_SEL_UNIVERSAL:
            ss->is_universal = 1;
            break;
        case CSS_SEL_PSEUDO_CLASS:
            apply_structural_pseudo(ss, p);
            break;
        default: break;
        }
    }
}

/* Convert cssparser.h CSSSelector into our SimpleSelector + pseudo flags. */
static void convert_selector(const CSSSelector *cs, SimpleSelector *ss,
                              int *is_hover, int *is_active, int *is_focus,
                              int *is_focus_visible, int *is_focus_within) {
    memset(ss, 0, sizeof(*ss));
    const CSSCompound* cmp = &cs->compounds[cs->compound_count > 0 ? cs->compound_count - 1 : 0];
    for (int pi = 0; pi < cmp->part_count; pi++) {
        const CSSSelectorPart *p = &cmp->parts[pi];
        switch (p->type) {
        case CSS_SEL_TYPE:
            strncpy(ss->sel_type, p->name, sizeof(ss->sel_type) - 1);
            break;
        case CSS_SEL_ID:
            /* strip leading '#' if present */
            strncpy(ss->sel_id, p->name[0] == '#' ? p->name + 1 : p->name, sizeof(ss->sel_id) - 1);
            break;
        case CSS_SEL_CLASS:
            if (ss->sel_class_count < MAX_SEL_CLASSES)
                strncpy(ss->sel_classes[ss->sel_class_count++], p->name, sizeof(ss->sel_classes[0]) - 1);
            break;
        case CSS_SEL_UNIVERSAL:
            ss->is_universal = 1;
            break;
        case CSS_SEL_PSEUDO_CLASS:
            if      (strcmp(p->name, "hover")        == 0) *is_hover = 1;
            else if (strcmp(p->name, "active")       == 0) *is_active = 1;
            else if (strcmp(p->name, "focus-visible") == 0) *is_focus_visible = 1;
            else if (strcmp(p->name, "focus-within") == 0) *is_focus_within = 1;
            else if (strcmp(p->name, "focus")        == 0) *is_focus = 1;
            else apply_structural_pseudo(ss, p);
            break;
        default: break;
        }
    }
}

/* Compute specificity for our internal format (100·id + 10·cls + 1·type).
   Structural pseudo-classes count as class-level, like real CSS. */
static int simple_selector_spec(const SimpleSelector *ss) {
    int spec = (ss->sel_id[0] ? 100 : 0)
             + ss->sel_class_count * 10
             + (ss->sel_type[0] ? 1 : 0);
    if (ss->is_first_child) spec += 10;
    if (ss->is_last_child)  spec += 10;
    if (ss->has_nth)        spec += 10;
    /* :not() contributes the specificity of its argument. */
    if (ss->has_not) {
        if (ss->not_id[0])         spec += 100;
        else if (ss->not_class[0]) spec += 10;
        else if (ss->not_type[0])  spec += 1;
    }
    return spec;
}

/* Ingest one cssparser.h CSSRule into the global css_rules[] array.
   For each selector in the rule a separate StyleRule entry is created. */
static void ingest_parsed_rule(const CSSRule *pr) {
    /* Build declaration-derived template once */
    StyleRule tmpl; memset(&tmpl, 0, sizeof(tmpl));
    int has_important = 0;
    for (int di = 0; di < pr->decl_count; di++) {
        apply_one_declaration(pr->decls[di].property, pr->decls[di].value, &tmpl);
        if (pr->decls[di].important) has_important = 1;
    }

    for (int si = 0; si < pr->selector_count && rule_count < MAX_RULES; si++) {
        const CSSSelector *cs = &pr->selectors[si];
        if (cs->compound_count == 0) continue;

        /* Check for pseudo-elements (::before, ::after, etc.) in last compound.
           ::before/::after are kept with pseudo_elem set; others (scrollbar etc.) dropped. */
        int pseudo_elem_type = 0; /* 0=none 1=before 2=after */
        {
            const CSSCompound *tgt = &cs->compounds[cs->compound_count - 1];
            for (int pi = 0; pi < tgt->part_count; pi++) {
                if (tgt->parts[pi].type == CSS_SEL_PSEUDO_ELEM) {
                    const char* pname = tgt->parts[pi].name;
                    if (strcmp(pname, "before") == 0 || strcmp(pname, "::before") == 0)
                        pseudo_elem_type = 1;
                    else if (strcmp(pname, "after") == 0 || strcmp(pname, "::after") == 0)
                        pseudo_elem_type = 2;
                    else
                        pseudo_elem_type = -1; /* unsupported pseudo-elem — drop */
                    break;
                }
            }
            if (pseudo_elem_type == -1) continue; /* drop unsupported pseudo-elements */
        }

        StyleRule rule = tmpl;
        rule.pseudo_elem = pseudo_elem_type;
        int is_hover = 0, is_active = 0, is_focus = 0, is_fvis = 0, is_fwithin = 0;

        /* Target = last compound */
        convert_selector(cs, &rule.target,
                         &is_hover, &is_active, &is_focus, &is_fvis, &is_fwithin);

        /* Ancestor compounds, stored NEAREST-FIRST (rightmost compound before
           the target comes first) so match_selector_chain can walk up/back
           from the target. Each entry records how the compound below it
           relates to it (descendant/child/sibling combinator). */
        rule.ancestor_count = 0;
        for (int ci = cs->compound_count - 2; ci >= 0 && rule.ancestor_count < MAX_SEL_ANCESTORS; ci--) {
            SimpleSelector *anc = &rule.ancestors[rule.ancestor_count++];
            memset(anc, 0, sizeof(*anc));
            switch (cs->compounds[ci + 1].combinator) {
            case CSS_COMB_CHILD:    anc->rel = LUNA_REL_CHILD; break;
            case CSS_COMB_ADJACENT: anc->rel = LUNA_REL_ADJ;   break;
            case CSS_COMB_SIBLING:  anc->rel = LUNA_REL_SIB;   break;
            default:                anc->rel = LUNA_REL_DESC;  break;
            }
            convert_compound_parts(&cs->compounds[ci], anc);
        }

        rule.is_hover        = is_hover;
        rule.is_active       = is_active;
        rule.is_focus        = is_focus && !is_fvis;
        rule.is_focus_visible = is_fvis;
        rule.is_focus_within  = is_fwithin;

        /* Specificity: use cssparser.h value scaled to our units */
        int spec = simple_selector_spec(&rule.target);
        for (int a = 0; a < rule.ancestor_count; a++)
            spec += simple_selector_spec(&rule.ancestors[a]);
        /* Dynamic pseudo-classes have class specificity. State matching is
           handled separately while applying the sorted rules. */
        if (is_hover)    spec += 10;
        if (is_active)   spec += 10;
        if (is_fvis)     spec += 10;
        if (is_fwithin)  spec += 10;
        if (is_focus && !is_fvis) spec += 10;
        if (has_important) spec += 10000; /* !important boosts over any selector specificity */
        rule.specificity = spec;

        /* Store selector string for debugging */
        snprintf(rule.selector, sizeof(rule.selector) - 1,
                 "[sel%d]", si);

        rule.source_order = rule_count;
        css_rules[rule_count++] = rule;
    }
}

static float parse_keyframe_stop_pos(const char* sel) {
    if (!sel) return 0.0f;
    if (strcasecmp(sel, "from") == 0 || strcasecmp(sel, "0%") == 0) return 0.0f;
    if (strcasecmp(sel, "to") == 0 || strcasecmp(sel, "100%") == 0) return 1.0f;
    if (strchr(sel, '%')) return (float)atof(sel) / 100.0f;
    return (float)atof(sel);
}

static void keyframe_stop_from_rule(const CSSRule* kr, KeyframeStop* stop) {
    memset(stop, 0, sizeof(*stop));
    for (int di = 0; di < kr->decl_count; di++) {
        StyleRule tmp;
        memset(&tmp, 0, sizeof(tmp));
        apply_one_declaration(kr->decls[di].property, kr->decls[di].value, &tmp);
        if (tmp.has_width) {
            stop->has_width = 1;
            stop->width = tmp.width;
            stop->width_pct = tmp.pct_w;
        }
        if (tmp.has_left) {
            stop->has_left = 1;
            stop->left = tmp.left;
            stop->left_pct = tmp.pct_left;
        }
        if (tmp.has_opacity) { stop->has_opacity = 1; stop->opacity = tmp.opacity; }
        if (tmp.has_transform) {
            if (tmp.transform_scale != 1.0f) {
                stop->has_transform_scale = 1;
                stop->transform_scale = tmp.transform_scale;
            }
            if (tmp.has_transform_tx) { stop->has_transform_tx = 1; stop->transform_tx = tmp.transform_tx; }
            if (tmp.has_transform_ty) { stop->has_transform_ty = 1; stop->transform_ty = tmp.transform_ty; }
        }
        if (tmp.has_shadow) {
            /* orb-pulse style animations use box-shadow blur */
            (void)tmp;
        }
    }
}

static void ingest_keyframes_from_sheet(const CSSStyleSheet* sheet) {
    if (!sheet) return;
    for (int ai = 0; ai < sheet->at_rule_count && g_keyframe_count < MAX_KF_ANIMS; ai++) {
        const CSSAtRule* at = &sheet->at_rules[ai];
        if (at->type != CSS_AT_KEYFRAMES) continue;
        CssKeyframe kf;
        memset(&kf, 0, sizeof(kf));
        char name[64] = {0};
        snprintf(name, sizeof(name), "%.*s", (int)(sizeof(name) - 1), at->prelude);
        trim_whitespace(name);
        snprintf(kf.name, sizeof(kf.name), "%s", name);
        for (int ri = 0; ri < at->nested_rule_count && kf.stop_count < MAX_KF_STOPS; ri++) {
            const CSSRule* kr = &at->nested_rules[ri];
            if (kr->selector_count == 0 || kr->selectors[0].compound_count == 0) continue;
            const CSSCompound* cmp = &kr->selectors[0].compounds[0];
            if (cmp->part_count == 0) continue;
            KeyframeStop stop;
            keyframe_stop_from_rule(kr, &stop);
            stop.position = parse_keyframe_stop_pos(cmp->parts[0].name);
            kf.stops[kf.stop_count++] = stop;
        }
        if (kf.stop_count > 0) {
            /* Sort stops and insert implicit 0% keyframe when only `to` is defined */
            for (int si = 0; si < kf.stop_count - 1; si++) {
                for (int sj = si + 1; sj < kf.stop_count; sj++) {
                    if (kf.stops[sj].position < kf.stops[si].position) {
                        KeyframeStop tmp = kf.stops[si];
                        kf.stops[si] = kf.stops[sj];
                        kf.stops[sj] = tmp;
                    }
                }
            }
            if (kf.stops[0].position > 0.001f && kf.stop_count < MAX_KF_STOPS) {
                memmove(&kf.stops[1], &kf.stops[0],
                        (size_t)kf.stop_count * sizeof(KeyframeStop));
                memset(&kf.stops[0], 0, sizeof(KeyframeStop));
                kf.stops[0].position = 0.0f;
                kf.stop_count++;
            }
            g_keyframes[g_keyframe_count++] = kf;
        }
    }
}

/* Evaluate the inexpensive, geometry-independent media features at stylesheet
 * load time.  Previously every @media block was ingested unconditionally;
 * that made e.g. a prefers-reduced-motion override permanently disable shell
 * animations even when the user had not requested reduced motion. */
static int css_media_matches(const char* prelude) {
    const char* reduced = getenv("LUNA_PREFERS_REDUCED_MOTION");
    int wants_reduced = reduced && (*reduced == '1' || strcasecmp(reduced, "true") == 0 ||
                                   strcasecmp(reduced, "reduce") == 0);
    char query[CSS_MAX_VALUE];
    snprintf(query, sizeof(query), "%s", prelude ? prelude : "");
    for (char* p = query; *p; ++p) *p = (char)tolower((unsigned char)*p);

    if (strstr(query, "prefers-reduced-motion")) {
        const char* motion = strstr(query, "prefers-reduced-motion");
        const char* colon = strchr(motion, ':');
        int asks_reduce = colon && strstr(colon + 1, "reduce") != NULL;
        if (asks_reduce != wants_reduced) return 0;
    }

    /* This compact evaluator covers the viewport predicates used by UI
     * stylesheets and leaves unknown preference tests false instead of
     * accidentally applying a browser-only override. */
    if (strstr(query, "min-width")) {
        const char* p = strchr(strstr(query, "min-width"), ':');
        float v = p ? strtof(p + 1, NULL) : 0.0f;
        if ((float)window_width < v) return 0;
    }
    if (strstr(query, "max-width")) {
        const char* p = strchr(strstr(query, "max-width"), ':');
        float v = p ? strtof(p + 1, NULL) : 0.0f;
        if ((float)window_width > v) return 0;
    }
    if (strstr(query, "min-height")) {
        const char* p = strchr(strstr(query, "min-height"), ':');
        float v = p ? strtof(p + 1, NULL) : 0.0f;
        if ((float)window_height < v) return 0;
    }
    if (strstr(query, "max-height")) {
        const char* p = strchr(strstr(query, "max-height"), ':');
        float v = p ? strtof(p + 1, NULL) : 0.0f;
        if ((float)window_height > v) return 0;
    }
    if (strstr(query, "prefers-") && !strstr(query, "prefers-reduced-motion"))
        return 0;
    return 1;
}

void parse_css(const char* css_text) {
    /* Stylesheets participate in one document-wide cascade.  Keep rules and
       keyframes loaded by earlier <link>, <style>, or luna_parse_css() calls;
       resetting here made the last stylesheet silently replace all previous
       ones, unlike browser CSSOM behaviour.  luna_init() starts with the
       zero-initialized tables, so parsing successive sheets can append safely. */
    CSSStyleSheet *sheet = css_parse(css_text, 0);
    if (!sheet) return;

    css_resolve_vars(sheet);

    /* Normal rules */
    for (int i = 0; i < sheet->rule_count && rule_count < MAX_RULES; i++)
        ingest_parsed_rule(&sheet->rules[i]);

    /* @media / @supports nested rules */
    for (int i = 0; i < sheet->at_rule_count; i++) {
        const CSSAtRule *at = &sheet->at_rules[i];
        if (at->type != CSS_AT_MEDIA && at->type != CSS_AT_SUPPORTS) continue;
        if (at->type == CSS_AT_MEDIA && !css_media_matches(at->prelude)) continue;
        for (int j = 0; j < at->nested_rule_count && rule_count < MAX_RULES; j++)
            ingest_parsed_rule(&at->nested_rules[j]);
    }

    ingest_keyframes_from_sheet(sheet);

    css_free(sheet);
    qsort(css_rules, rule_count, sizeof(StyleRule), cmp_rules_by_specificity);
}

// ============================================================
// Style application
// ============================================================

static int is_descendant_of(int idx, int ancestor);
void update_element_style(LunaElement* e);

static void update_focus_within_styles(int idx);

static int element_contains_focus(int idx) {
    if (g_focused_element_idx == -1 || idx == -1) return 0;
    if (g_focused_element_idx == idx) return 1;
    return is_descendant_of(g_focused_element_idx, idx);
}

static void update_focus_within_styles(int idx) {
    while (idx != -1) {
        update_element_style(&elements[idx]);
        idx = elements[idx].parent_idx;
    }
}

void update_element_style(LunaElement* e) {
    /* Preserve an unchanged animation timeline across unrelated hover/focus
     * style resolutions; restart only when its definition changes. */
    char prev_anim_name[64];
    snprintf(prev_anim_name, sizeof(prev_anim_name), "%s", e->anim_name);
    int prev_has_animation = e->has_css_animation;
    float prev_anim_duration = e->anim_duration;
    float prev_anim_delay = e->anim_delay;
    int prev_anim_infinite = e->anim_infinite;
    int prev_anim_alternate = e->anim_alternate;
    int prev_anim_easing = e->anim_easing;
    double prev_anim_start_time = e->anim_start_time;

    g_probe_prepared = 0;
    if (!e->has_custom_bg)     { e->r = 0.0f; e->g = 0.0f; e->b = 0.0f; e->a = 0.0f; e->has_gradient = 0; e->has_bg_image = 0; e->bg_image_path[0] = '\0'; e->bg_image_tex = 0; }
    if (!e->has_custom_color)  { e->t_r = 0.1f; e->t_g = 0.1f; e->t_b = 0.1f; e->t_a = 1.0f; }
    e->has_caret_color = 0;
    if (!e->has_custom_border) { e->border_width = 0; e->bd_r = 0; e->bd_g = 0; e->bd_b = 0; e->bd_a = 0; }
    e->has_border_top = e->has_border_right = e->has_border_bottom = e->has_border_left = 0;
    e->border_top_w = e->border_right_w = e->border_bottom_w = e->border_left_w = 0.0f;
    e->border_top_a = e->border_right_a = e->border_bottom_a = e->border_left_a = 0.0f;
    e->outline_width = 0.0f;
    e->outline_offset = 0.0f;
    e->has_outline = 0;
    e->ol_r = 0.39f; e->ol_g = 0.40f; e->ol_b = 0.95f; e->ol_a = 0.5f;
    e->border_radius = 0; e->padding = 0;
    e->rad_c[0] = e->rad_c[1] = e->rad_c[2] = e->rad_c[3] = 0.0f;
    e->shadow_count = 0;
    e->pad_t = e->pad_r = e->pad_b = e->pad_l = 0;
    e->margin_top = e->margin_right = e->margin_bottom = e->margin_left = 0.0f;
    e->margin_top_auto = e->margin_right_auto = e->margin_bottom_auto = e->margin_left_auto = 0;
    e->opacity = 1; e->display_none = 0; e->display_mode = DISPLAY_BLOCK;
    e->visibility_hidden = 0; e->cursor_pointer = 0;
    e->flex_direction = FLEX_DIR_ROW;
    e->justify_content = FLEX_JUSTIFY_START;
    e->align_items = FLEX_ALIGN_STRETCH;
    e->justify_items = FLEX_ALIGN_STRETCH;
    e->align_content = FLEX_ALIGN_START;
    e->flex_wrap = FLEX_WRAP_NOWRAP;
    e->align_self = ALIGN_SELF_AUTO;
    e->justify_self = ALIGN_SELF_AUTO;
    e->flex_gap = 0.0f;
    e->flex_grow = 0;
    e->flex_shrink = 1;
    e->has_flex_basis = 0;
    e->flex_basis = 0.0f;
    e->flex_basis_auto = 1;
    e->flex_child = 0;
    /* Browser UA styles size form controls with border-box semantics.  Keep
       ordinary elements content-box unless author CSS overrides box-sizing. */
    e->box_sizing = (strcmp(e->type, "button") == 0 ||
                     strcmp(e->type, "input") == 0 ||
                     strcmp(e->type, "select") == 0 ||
                     strcmp(e->type, "textarea") == 0)
                        ? BOX_BORDER : BOX_CONTENT;
    e->css_width = e->css_height = 0.0f;
    e->has_css_width = e->has_css_height = 0;
    e->has_min_width = e->has_min_height = 0;
    e->has_max_width = e->has_max_height = 0;
    e->max_width_pct = 0;
    e->raw_max_width = e->raw_max_width_off = 0.0f;
    e->max_height_pct = 0;
    e->raw_max_height = e->raw_max_height_off = 0.0f;
    e->css_min_width = e->css_min_height = 0.0f;
    e->css_max_width = e->css_max_height = 0.0f;
    e->grid_col_count = e->grid_row_count = 0;
    e->grid_col_gap = e->grid_row_gap = 0.0f;
    e->grid_auto_flow = GRID_AUTO_FLOW_ROW;
    e->grid_auto_row_track = 1.0f; e->grid_auto_row_type = GRID_TRACK_FR;
    e->grid_auto_col_track = 1.0f; e->grid_auto_col_type = GRID_TRACK_FR;
    e->scroll_top = e->scroll_left = 0.0f;
    e->scroll_dest_top = e->scroll_dest_left = 0.0f;
    e->scroll_smooth = 0;
    e->scroll_snap_type = 0;
    e->scroll_snap_align = 0;
    e->scroll_content_h = e->scroll_content_w = 0.0f;
    e->scrollbar_width = 5.0f;
    e->has_scrollbar_width = 0;
    e->has_scrollbar_color = 0;
    e->position_sticky = 0;
    e->position_mode = POS_UNSET;
    e->sticky_top = 0.0f;
    e->sticky_use_top = 0;
    e->sticky_use_bottom = 0;
    e->sticky_bottom = 0.0f;
    e->sticky_use_left = 0;
    e->sticky_left = 0.0f;
    e->sticky_use_right = 0;
    e->sticky_right = 0.0f;
    e->inert = 0;
    e->tabindex = -2;
    e->aria_label[0] = '\0';
    e->role[0] = '\0';
    e->aria_live = 0;
    e->aria_hidden = 0;
    e->aria_expanded = -1;
    e->scroll_margin_top = e->scroll_margin_right = 0.0f;
    e->scroll_margin_bottom = e->scroll_margin_left = 0.0f;
    e->scroll_padding_top = e->scroll_padding_right = 0.0f;
    e->scroll_padding_bottom = e->scroll_padding_left = 0.0f;
    e->overflow_x = OVERFLOW_VISIBLE;
    e->overflow_y = OVERFLOW_VISIBLE;
    e->grid_col = e->grid_row = -1;
    e->grid_col_span = e->grid_row_span = 1;
    e->has_grid_col = e->has_grid_row = 0;
    e->grid_child = 0;
    e->css_positioned = 0;
    e->cursor_type = 0; e->position_fixed = 0;
    e->has_bottom = 0; e->has_right = 0;
    e->has_top = 0; e->has_left = 0;
    /* CSS text properties inherit.  In particular, the small icon <span>s in
     * the menubar only set font-family; resetting them to the 16px engine
     * default made their metrics disagree with the browser and could push a
     * glyph outside its flex item's clip.  Copying this compact scalar set is
     * allocation-free and is only done when a style is (re)resolved. */
    /* Native browser buttons center their label even without an explicit
       text-align declaration.  Author CSS applied below can still override. */
    e->text_align = strcmp(e->type, "button") == 0 ? 1 : 0;
    e->has_text_align = strcmp(e->type, "button") == 0;
    e->font_size = 16; e->font_bold = 0; e->font_face = 0;
    e->line_height = 0.0f; e->white_space = 0; e->text_overflow = 0; e->overflow_wrap = 1;
    e->letter_spacing = 0.0f; e->text_transform = 0; e->text_decoration = 0;
    if (e->parent_idx >= 0 && e->parent_idx < elem_count) {
        const LunaElement* parent = &elements[e->parent_idx];
        e->font_size = parent->font_size;
        e->font_bold = parent->font_bold;
        e->font_face = parent->font_face;
        e->line_height = parent->line_height;
        e->white_space = parent->white_space;
        e->overflow_wrap = parent->overflow_wrap;
        e->letter_spacing = parent->letter_spacing;
        e->text_transform = parent->text_transform;
        e->text_decoration = parent->text_decoration;
        if (!e->has_custom_color) {
            e->t_r = parent->t_r; e->t_g = parent->t_g;
            e->t_b = parent->t_b; e->t_a = parent->t_a;
        }
    }
    e->has_text_shadow = 0;
    e->tsh_dx = e->tsh_dy = e->tsh_blur = 0.0f;
    e->tsh_r = e->tsh_g = e->tsh_b = 0.0f; e->tsh_a = 0.0f;
    e->has_shadow = 0; e->z_index = 0;
    e->transform_scale = 1.0f; e->transform_tx = 0.0f; e->transform_ty = 0.0f;
    e->transform_tx_pct = 0; e->transform_ty_pct = 0;
    e->raw_transform_tx = 0.0f; e->raw_transform_ty = 0.0f;
    e->anim_speed = 14.0f;
    e->pointer_events_none = 0;
    e->pct_w = 0; e->pct_h = 0; e->pct_left = 0; e->pct_top = 0;
    e->pct_bottom = 0; e->pct_right = 0;
    e->raw_w_off = 0.0f; e->raw_h_off = 0.0f;
    e->raw_left_off = 0.0f; e->raw_top_off = 0.0f;
    e->raw_bottom_off = 0.0f; e->raw_right_off = 0.0f;

    e->has_css_animation = 0;
    e->anim_name[0] = '\0';
    e->anim_duration = 0.0f;
    e->anim_delay = 0.0f;
    e->anim_infinite = 0;
    e->anim_alternate = 0;
    e->anim_easing = 0;
    e->anim_finished = 0;
    e->anim_frame_changed = 0;
    e->has_filter = 0;
    e->filter_brightness = 1.0f;
    e->filter_contrast   = 1.0f;
    e->filter_saturate   = 1.0f;
    e->filter_hue        = 0.0f;
    e->filter_blur       = 0.0f;
    e->has_bg_clip_text  = 0;
    e->mix_blend_mode    = 0;
    e->grad_rad_rx_pct   = 0;
    e->grad_rad_ry_pct   = 0;
    e->font_italic = 0;
    e->has_aspect_ratio = 0;
    e->aspect_ratio = 0.0f;
    e->bg_size_mode = 0;
    e->bg_size_w = e->bg_size_h = 0.0f;
    e->bg_pos_x = 0.5f;
    e->bg_pos_y = 0.5f;
    e->has_backdrop_blur = 0;
    e->backdrop_blur_radius = 0.0f;
    e->backdrop_saturate = 1.0f;
    e->backdrop_brightness = 1.0f;
    e->bg_layer_count = 0;
    e->grad_rad_rx = 0.0f;
    e->grad_rad_ry = 0.0f;

    for (int i = 0; i < rule_count; i++) {
        StyleRule* r = &css_rules[i];
        if (!selector_matches(r, e)) continue;
        if (r->is_hover  && !e->is_hovered) continue;
        if (r->is_active && !e->is_active)  continue;
        if (r->is_focus  && e->id_idx != g_focused_element_idx) continue;
        if (r->is_focus_visible &&
            (e->id_idx != g_focused_element_idx || !g_focus_via_keyboard)) continue;
        if (r->is_focus_within && !element_contains_focus(e->id_idx)) continue;

        if (r->has_bg_reset && !e->has_custom_bg) {
            e->has_bg_image = 0;
            e->bg_image_path[0] = '\0';
            e->bg_image_tex = 0;
            e->bg_layer_count = 0;
        }
        if (r->has_bg_image_reset && !e->has_custom_bg) {
            e->has_bg_image = 0;
            e->bg_image_path[0] = '\0';
            e->bg_image_tex = 0;
        }
        if (r->has_bg_image && !e->has_custom_bg) {
            e->has_bg_image = 1;
            strncpy(e->bg_image_path, r->bg_image_path, sizeof(e->bg_image_path) - 1);
            e->bg_image_path[sizeof(e->bg_image_path) - 1] = '\0';
            e->bg_image_tex = 0;
        }
        if (r->has_bg && !e->has_custom_bg) {
            e->r = r->bg_r; e->g = r->bg_g; e->b = r->bg_b; e->a = r->bg_a;
            if (r->has_gradient) {
                e->has_gradient = 1;
                e->grad_type = r->grad_type;
                e->grad_stop_count = r->grad_stop_count;
                e->grad_angle = r->grad_angle;
                e->grad_rad_cx = r->grad_rad_cx;
                e->grad_rad_cy = r->grad_rad_cy;
                e->grad_rad_r = r->grad_rad_r;
                e->grad_rad_rx = r->grad_rad_rx;
                e->grad_rad_ry = r->grad_rad_ry;
                for (int s = 0; s < r->grad_stop_count && s < MAX_GRAD_STOPS; s++) {
                    e->grad_stop_pos[s] = r->grad_stop_pos[s];
                    e->grad_stop_r[s] = r->grad_stop_r[s];
                    e->grad_stop_g[s] = r->grad_stop_g[s];
                    e->grad_stop_b[s] = r->grad_stop_b[s];
                    e->grad_stop_a[s] = r->grad_stop_a[s];
                }
            } else {
                e->has_gradient = 0;
                e->grad_type = GRAD_NONE;
                e->grad_rad_rx = 0.0f;
                e->grad_rad_ry = 0.0f;
            }
            /* Copy multiple background layers */
            if (r->bg_layer_count > 0) {
                e->bg_layer_count = r->bg_layer_count;
                for (int li = 0; li < r->bg_layer_count && li < LUNA_MAX_BG_LAYERS; li++)
                    e->bg_layers[li] = r->bg_layers[li];
            }
        }
        if (r->has_backdrop_blur) {
            e->has_backdrop_blur = 1;
            e->backdrop_blur_radius = r->backdrop_blur_radius;
            e->backdrop_saturate = r->backdrop_saturate;
            e->backdrop_brightness = r->backdrop_brightness;
        }
        if (r->has_color && !e->has_custom_color)  { e->t_r = r->c_r; e->t_g = r->c_g; e->t_b = r->c_b; e->t_a = r->c_a; }
        if (r->has_caret_color) {
            e->has_caret_color = 1;
            e->caret_r = r->caret_r; e->caret_g = r->caret_g;
            e->caret_b = r->caret_b; e->caret_a = r->caret_a;
        }
        if (r->has_border && !e->has_custom_border) { e->bd_r = r->bd_r; e->bd_g = r->bd_g; e->bd_b = r->bd_b; e->bd_a = r->bd_a; e->border_width = r->border_width; }
        if (r->has_border_top) {
            e->has_border_top = 1; e->border_top_w = r->border_top_w;
            e->border_top_r = r->border_top_r; e->border_top_g = r->border_top_g;
            e->border_top_b = r->border_top_b; e->border_top_a = r->border_top_a;
        }
        if (r->has_border_right) {
            e->has_border_right = 1; e->border_right_w = r->border_right_w;
            e->border_right_r = r->border_right_r; e->border_right_g = r->border_right_g;
            e->border_right_b = r->border_right_b; e->border_right_a = r->border_right_a;
        }
        if (r->has_border_bottom) {
            e->has_border_bottom = 1; e->border_bottom_w = r->border_bottom_w;
            e->border_bottom_r = r->border_bottom_r; e->border_bottom_g = r->border_bottom_g;
            e->border_bottom_b = r->border_bottom_b; e->border_bottom_a = r->border_bottom_a;
        }
        if (r->has_border_left) {
            e->has_border_left = 1; e->border_left_w = r->border_left_w;
            e->border_left_r = r->border_left_r; e->border_left_g = r->border_left_g;
            e->border_left_b = r->border_left_b; e->border_left_a = r->border_left_a;
        }
        if (r->has_outline) {
            e->has_outline = 1;
            e->outline_width = r->outline_width;
            e->outline_offset = r->outline_offset;
            e->ol_r = r->ol_r; e->ol_g = r->ol_g; e->ol_b = r->ol_b; e->ol_a = r->ol_a;
        }
        if (r->has_radius) {
            for (int c = 0; c < 4; c++)
                if (r->has_rad_c[c]) e->rad_c[c] = r->rad_c[c];
            float mx = e->rad_c[0];
            for (int c = 1; c < 4; c++) if (e->rad_c[c] > mx) mx = e->rad_c[c];
            e->border_radius = mx;
        }
        if (r->has_width) {
            e->pct_w = r->pct_w;
            if (r->pct_w) { e->raw_w = r->raw_w; e->raw_w_off = r->raw_w_off; }
            else { e->css_width = r->width; e->has_css_width = 1; e->w = r->width; }
        }
        if (r->has_height) {
            e->pct_h = r->pct_h;
            if (r->pct_h) { e->raw_h = r->raw_h; e->raw_h_off = r->raw_h_off; }
            else { e->css_height = r->height; e->has_css_height = 1; e->h = r->height; }
        }
        if (r->has_padding) {
            e->padding = r->padding;
            e->pad_t = r->pad_t; e->pad_r = r->pad_r;
            e->pad_b = r->pad_b; e->pad_l = r->pad_l;
        }
        if (r->has_margin) {
            e->margin_top = r->margin_top;
            e->margin_right = r->margin_right;
            e->margin_bottom = r->margin_bottom;
            e->margin_left = r->margin_left;
            e->margin_top_auto = r->margin_top_auto;
            e->margin_right_auto = r->margin_right_auto;
            e->margin_bottom_auto = r->margin_bottom_auto;
            e->margin_left_auto = r->margin_left_auto;
        }
        if (r->has_position) {
            e->position_fixed = r->position_fixed;
            e->position_sticky = r->position_sticky;
            if (r->position_mode != POS_UNSET) e->position_mode = r->position_mode;
        }
        /* Sticky and relative elements stay in normal/flex flow.  Their offsets
           are applied after normal-flow layout; only absolute/fixed elements
           are removed from the flow via css_positioned. */
        if (r->has_left && offsets_should_apply(e)) {
            if (e->position_fixed || e->position_mode == POS_ABSOLUTE)
                e->css_positioned |= 1;
            if (!e->pos_overridden_x) {
                e->has_left = 1; e->has_right = 0;
                if (e->position_sticky) {
                    e->sticky_use_left = 1;
                    e->sticky_left = r->pct_left ? r->raw_left : r->left;
                    if (r->pct_left) { e->pct_left = 1; e->raw_left = r->raw_left; e->raw_left_off = r->raw_left_off; }
                } else {
                    e->pct_left = r->pct_left;
                    e->raw_left = r->pct_left ? r->raw_left : r->left;
                    e->raw_left_off = r->raw_left_off;
                    if (e->position_mode != POS_RELATIVE) {
                        if (r->pct_left) { e->raw_left = r->raw_left; e->raw_left_off = r->raw_left_off; }
                        else e->rel_x = r->left;
                    }
                }
            }
        }
        if (r->has_top && offsets_should_apply(e)) {
            if (e->position_fixed || e->position_mode == POS_ABSOLUTE)
                e->css_positioned |= 2;
            if (!e->pos_overridden_y) {
                e->has_top = 1; e->has_bottom = 0;
                if (e->position_sticky) {
                    if (!e->sticky_use_bottom) {
                        e->sticky_use_top = 1;
                        e->sticky_top = r->pct_top ? r->raw_top : r->top;
                    }
                    if (r->pct_top) { e->pct_top = 1; e->raw_top = r->raw_top; e->raw_top_off = r->raw_top_off; }
                } else {
                    e->pct_top = r->pct_top;
                    e->raw_top = r->pct_top ? r->raw_top : r->top;
                    e->raw_top_off = r->raw_top_off;
                    if (e->position_mode != POS_RELATIVE) {
                        if (r->pct_top) { e->raw_top = r->raw_top; e->raw_top_off = r->raw_top_off; }
                        else e->rel_y = r->top;
                    }
                }
            }
        }
        if (r->has_bottom && !e->pos_overridden_y && offsets_should_apply(e)) {
            e->has_bottom = 1; e->pct_bottom = r->pct_bottom;
            if (r->pct_bottom) { e->raw_bottom = r->raw_bottom; e->raw_bottom_off = r->raw_bottom_off; }
            else e->bottom_val = r->bottom;
            if (e->position_sticky) {
                e->sticky_use_bottom = 1;
                e->sticky_bottom = r->pct_bottom ? e->raw_bottom : e->bottom_val;
            } else if (e->position_fixed || e->position_mode == POS_ABSOLUTE) {
                e->css_positioned |= 2;
            }
        }
        if (r->has_right && !e->pos_overridden_x && offsets_should_apply(e)) {
            e->has_right = 1; e->pct_right = r->pct_right;
            if (r->pct_right) { e->raw_right = r->raw_right; e->raw_right_off = r->raw_right_off; }
            else e->right_val = r->right;
            if (e->position_sticky) {
                e->sticky_use_right = 1;
                e->sticky_right = r->pct_right ? e->raw_right : e->right_val;
            } else if (e->position_fixed || e->position_mode == POS_ABSOLUTE) {
                e->css_positioned |= 1;
            }
        }
        if (r->has_opacity) e->opacity = r->opacity;
        if (r->has_cursor)  { e->cursor_pointer = r->cursor_pointer; e->cursor_type = r->cursor_type; }
        if (r->has_display) {
            e->display_none = r->display_none;
            e->display_mode = r->display_mode;
        }
        if (r->has_flex_direction) e->flex_direction = r->flex_direction;
        if (r->has_justify_content) e->justify_content = r->justify_content;
        if (r->has_align_items) e->align_items = r->align_items;
        if (r->has_justify_items) e->justify_items = r->justify_items;
        if (r->has_align_content) e->align_content = r->align_content;
        if (r->has_flex_wrap) e->flex_wrap = r->flex_wrap;
        if (r->has_align_self) e->align_self = r->align_self;
        if (r->has_justify_self) e->justify_self = r->justify_self;
        if (r->has_gap) e->flex_gap = r->flex_gap;
        if (r->has_flex_grow) e->flex_grow = r->flex_grow;
        if (r->has_flex_shrink) e->flex_shrink = r->flex_shrink;
        if (r->has_flex_basis) {
            e->has_flex_basis = 1;
            e->flex_basis = r->flex_basis;
            e->flex_basis_auto = r->flex_basis_auto;
        }
        if (r->has_min_width) { e->has_min_width = 1; e->css_min_width = r->min_width; }
        if (r->has_min_height) { e->has_min_height = 1; e->css_min_height = r->min_height; }
        if (r->has_max_width) {
            e->has_max_width = 1;
            e->css_max_width = r->max_width;
            e->max_width_pct = r->max_width_pct;
            e->raw_max_width = r->raw_max_width;
            e->raw_max_width_off = r->raw_max_width_off;
        }
        if (r->has_max_height) {
            e->has_max_height = 1;
            e->css_max_height = r->max_height;
            e->max_height_pct = r->max_height_pct;
            e->raw_max_height = r->raw_max_height;
            e->raw_max_height_off = r->raw_max_height_off;
        }
        if (r->has_box_sizing) e->box_sizing = r->box_sizing;
        if (r->has_overflow_x) e->overflow_x = r->overflow_x;
        if (r->has_overflow_y) e->overflow_y = r->overflow_y;
        if (r->has_scrollbar_width) {
            e->has_scrollbar_width = 1;
            e->scrollbar_width = r->scrollbar_width;
        }
        if (r->has_scrollbar_color) {
            e->has_scrollbar_color = 1;
            e->sb_thumb_r = r->sb_thumb_r; e->sb_thumb_g = r->sb_thumb_g;
            e->sb_thumb_b = r->sb_thumb_b; e->sb_thumb_a = r->sb_thumb_a;
            e->sb_track_r = r->sb_track_r; e->sb_track_g = r->sb_track_g;
            e->sb_track_b = r->sb_track_b; e->sb_track_a = r->sb_track_a;
        }
        if (r->has_scroll_behavior) e->scroll_smooth = r->scroll_smooth;
        if (r->has_scroll_snap_type) e->scroll_snap_type = r->scroll_snap_type;
        if (r->has_scroll_snap_align) e->scroll_snap_align = r->scroll_snap_align;
        if (r->has_scroll_margin) {
            e->scroll_margin_top = r->scroll_margin_top;
            e->scroll_margin_right = r->scroll_margin_right;
            e->scroll_margin_bottom = r->scroll_margin_bottom;
            e->scroll_margin_left = r->scroll_margin_left;
        }
        if (r->has_scroll_padding) {
            e->scroll_padding_top = r->scroll_padding_top;
            e->scroll_padding_right = r->scroll_padding_right;
            e->scroll_padding_bottom = r->scroll_padding_bottom;
            e->scroll_padding_left = r->scroll_padding_left;
        }
        if (r->has_grid_template_columns) {
            e->grid_col_count = r->grid_col_count;
            for (int t = 0; t < r->grid_col_count; t++) {
                e->grid_col_track[t] = r->grid_col_track[t];
                e->grid_col_type[t] = r->grid_col_type[t];
                e->grid_col_min[t] = r->grid_col_min[t];
            }
        }
        if (r->has_grid_template_rows) {
            e->grid_row_count = r->grid_row_count;
            for (int t = 0; t < r->grid_row_count; t++) {
                e->grid_row_track[t] = r->grid_row_track[t];
                e->grid_row_type[t] = r->grid_row_type[t];
                e->grid_row_min[t] = r->grid_row_min[t];
            }
        }
        if (r->has_grid_template_areas) {
            e->grid_area_rows = r->grid_area_rows;
            e->grid_area_cols = r->grid_area_cols;
            memcpy(e->grid_area_cell, r->grid_area_cell, sizeof(e->grid_area_cell));
            if (!r->has_grid_template_columns && r->grid_area_cols > 0) {
                e->grid_col_count = r->grid_area_cols;
                for (int t = 0; t < e->grid_col_count; t++) {
                    e->grid_col_track[t] = 1.0f;
                    e->grid_col_type[t] = GRID_TRACK_FR;
                    e->grid_col_min[t] = 0.0f;
                }
            }
            if (!r->has_grid_template_rows && r->grid_area_rows > 0) {
                e->grid_row_count = r->grid_area_rows;
                for (int t = 0; t < e->grid_row_count; t++) {
                    e->grid_row_track[t] = 1.0f;
                    e->grid_row_type[t] = GRID_TRACK_FR;
                    e->grid_row_min[t] = 0.0f;
                }
            }
            compile_grid_area_rects(e);
        }
        if (r->has_column_gap) e->grid_col_gap = r->grid_col_gap;
        if (r->has_row_gap) e->grid_row_gap = r->grid_row_gap;
        if (r->has_grid_auto_flow) e->grid_auto_flow = r->grid_auto_flow;
        if (r->has_grid_auto_rows) {
            e->has_grid_auto_rows = 1;
            e->grid_auto_row_track = r->grid_auto_row_track;
            e->grid_auto_row_type = r->grid_auto_row_type;
            e->grid_auto_row_min = r->grid_auto_row_min;
        }
        if (r->has_grid_auto_columns) {
            e->has_grid_auto_columns = 1;
            e->grid_auto_col_track = r->grid_auto_col_track;
            e->grid_auto_col_type = r->grid_auto_col_type;
            e->grid_auto_col_min = r->grid_auto_col_min;
        }
        if (r->has_grid_column) { e->has_grid_col = 1; e->grid_col = r->grid_col; }
        if (r->has_grid_row) { e->has_grid_row = 1; e->grid_row = r->grid_row; }
        if (r->has_grid_column_span) e->grid_col_span = r->grid_col_span;
        if (r->has_grid_row_span) e->grid_row_span = r->grid_row_span;
        if (r->has_grid_area) {
            e->has_grid_area = 1;
            strncpy(e->grid_area_name, r->grid_area_name, 31);
            e->grid_area_name[31] = '\0';
        }
        if (r->has_visibility) e->visibility_hidden = r->visibility_hidden;
        if (r->has_pointer_events) e->pointer_events_none = r->pointer_events_none;
        if (r->has_text_align)  { e->has_text_align = 1; e->text_align = r->text_align; }
        if (r->has_font_size)   e->font_size = r->font_size;
        if (r->has_font_weight) e->font_bold = r->font_bold;
        if (r->has_font_face)   e->font_face = r->font_face;
        if (r->has_line_height) e->line_height = r->line_height;
        if (r->has_white_space) e->white_space = r->white_space;
        if (r->has_text_overflow) e->text_overflow = r->text_overflow;
        if (r->has_overflow_wrap) e->overflow_wrap = r->overflow_wrap;
        if (r->has_letter_spacing) e->letter_spacing = r->letter_spacing;
        if (r->has_text_transform) e->text_transform = r->text_transform;
        if (r->has_text_decoration) e->text_decoration = r->text_decoration;
        if (r->has_text_shadow) {
            e->has_text_shadow = (r->tsh_a > 0.0f);
            e->tsh_dx = r->tsh_dx; e->tsh_dy = r->tsh_dy; e->tsh_blur = r->tsh_blur;
            e->tsh_r = r->tsh_r; e->tsh_g = r->tsh_g; e->tsh_b = r->tsh_b; e->tsh_a = r->tsh_a;
        }
        if (r->has_shadow) {
            e->has_shadow = (r->shadow_count > 0);
            e->shadow_count = r->shadow_count;
            for (int s = 0; s < r->shadow_count; s++) e->shadows[s] = r->shadows[s];
        }
        if (r->has_z_index) e->z_index = r->z_index;
        if (r->has_transform) e->transform_scale = r->transform_scale;
        if (r->has_transform_tx) {
            e->transform_tx = r->transform_tx;
            e->transform_tx_pct = r->transform_tx_pct;
            e->raw_transform_tx = r->raw_transform_tx;
        }
        if (r->has_transform_ty) {
            e->transform_ty = r->transform_ty;
            e->transform_ty_pct = r->transform_ty_pct;
            e->raw_transform_ty = r->raw_transform_ty;
        }
        if (r->has_transition) e->anim_speed = 1.0f / r->transition_duration;
        if (r->has_animation && r->anim_name[0]) {
            e->has_css_animation = 1;
            strncpy(e->anim_name, r->anim_name, sizeof(e->anim_name) - 1);
            if (r->anim_duration > 0.0f) e->anim_duration = r->anim_duration;
            e->anim_delay = r->anim_delay;
            e->anim_infinite = r->anim_infinite;
            e->anim_alternate = r->anim_alternate;
            e->anim_easing = r->anim_easing;
        }
        if (r->has_filter) {
            e->has_filter = 1;
            e->filter_brightness = r->filter_brightness;
            e->filter_contrast   = r->filter_contrast;
            e->filter_saturate   = r->filter_saturate;
            e->filter_hue        = r->filter_hue;
            e->filter_blur       = r->filter_blur;
        }
        if (r->has_bg_clip_text) e->has_bg_clip_text = 1;
        if (r->has_mix_blend_mode) e->mix_blend_mode = r->mix_blend_mode;
        if (r->has_gradient) {
            e->grad_rad_rx_pct = r->grad_rad_rx_pct;
            e->grad_rad_ry_pct = r->grad_rad_ry_pct;
        }
        if (r->has_font_italic) e->font_italic = r->font_italic;
        if (r->has_aspect_ratio) { e->has_aspect_ratio = 1; e->aspect_ratio = r->aspect_ratio; }
        if (r->has_bg_size) {
            e->bg_size_mode = r->bg_size_mode;
            e->bg_size_w = r->bg_size_w;
            e->bg_size_h = r->bg_size_h;
        }
        if (r->has_bg_pos) {
            e->bg_pos_x = r->bg_pos_x;
            e->bg_pos_y = r->bg_pos_y;
        }
    }

    apply_element_inline_style(e);

    if (e->has_css_animation) {
        int same_animation = prev_has_animation &&
            strcmp(prev_anim_name, e->anim_name) == 0 &&
            prev_anim_duration == e->anim_duration &&
            prev_anim_delay == e->anim_delay &&
            prev_anim_infinite == e->anim_infinite &&
            prev_anim_alternate == e->anim_alternate &&
            prev_anim_easing == e->anim_easing;
        if (same_animation) {
            e->anim_start_time = prev_anim_start_time;
            /* Style reset restored base properties. If the finite animation
             * had completed, reapply its final frame once on the next update. */
            e->anim_finished = 0;
        } else {
            e->anim_start_time = luna_now();
            e->anim_finished = 0;
            e->anim_base_captured = 0;
            e->anim_override_layout = 0;
        }
        if (!e->anim_base_captured) {
            e->anim_base_w = e->has_css_width ? e->css_width : e->w;
            e->anim_base_w_pct = e->pct_w;
            e->anim_base_left = e->has_left ? e->raw_left : e->rel_x;
            e->anim_base_left_pct = e->pct_left;
            e->anim_base_captured = 1;
        }
    } else {
        e->anim_start_time = -1.0;
        e->anim_finished = 0;
        e->anim_base_captured = 0;
        e->anim_override_layout = 0;
    }
    if (e->is_input && e->cursor_type == 0) e->cursor_type = 2;
    if (e->is_input && !e->input_multiline) e->white_space = 1;
    /* ::before/::after must not steal clicks; style reset clears the flag. */
    if (e->generated_pseudo) e->pointer_events_none = 1;
    g_activity_registry_dirty = 1;
    visual_activate_idx((int)(e - elements));
}

// ============================================================
// Pseudo-element generation (::before / ::after)
// ============================================================

/* Spawn synthetic LunaElement nodes for ::before and ::after pseudo-elements.
   Called once at the end of parse_html after all real DOM nodes exist. */
static void generate_pseudo_elements(void) {
    /* Iterate DOM elements (fixed count — we will append new ones) */
    int dom_count = elem_count;
    for (int ei = 0; ei < dom_count && elem_count < MAX_ELEMENTS; ei++) {
        LunaElement* host = &elements[ei];
        if (host->luna_internal) continue;
        if (host->generated_pseudo) continue;

        /* Check every pseudo-element rule to see if it matches this host */
        for (int ri = 0; ri < rule_count && elem_count < MAX_ELEMENTS; ri++) {
            StyleRule* r = &css_rules[ri];
            if (!r->pseudo_elem) continue; /* not a pseudo-element rule */
            /* Skip hover/focus/active-only pseudo rules for now */
            if (r->is_hover || r->is_active || r->is_focus) continue;

            if (!selector_matches_pseudo(r, host)) continue;

            /* Skip rules with no visible effect (e.g. CSS resets like
               `*, *::before, *::after { box-sizing: border-box; }`) */
            if (!r->has_content && !r->has_bg && !r->has_gradient &&
                !r->has_bg_image && !r->has_shadow && !r->has_width &&
                !r->has_height && !r->has_animation && !r->has_border &&
                !(r->has_display && !r->display_none))
                continue;

            /* We found a match — look for an existing pseudo node (including
             * ones created by earlier CSS reloads / parse passes). */
            int existing = -1;
            for (int xi = 0; xi < elem_count; xi++) {
                if (elements[xi].parent_idx == ei &&
                    elements[xi].generated_pseudo == r->pseudo_elem)
                    { existing = xi; break; }
            }
            if (existing >= 0) continue; /* already created */

            /* Spawn a synthetic element */
            int ni = elem_count++;
            LunaElement* pe = &elements[ni];
            memset(pe, 0, sizeof(*pe));
            pe->id_idx = ni;
            pe->parent_idx = ei;
            pe->generated_pseudo = r->pseudo_elem;
            pe->pointer_events_none = 1;
            strncpy(pe->type, "div", sizeof(pe->type) - 1);
            /* Set position to absolute so it doesn't affect host flow */
            pe->position_mode = POS_ABSOLUTE;
            pe->opacity = 1.0f;
            pe->transform_scale = 1.0f;
            pe->cur_scale = 1.0f;
            pe->anim_speed = 14.0f;
            pe->font_size = host->font_size > 0 ? host->font_size : 16;
            /* Copy content if specified */
            if (r->has_content) {
                char content[128];
                strncpy(content, r->content, sizeof(content) - 1);
                content[sizeof(content) - 1] = '\0';
                /* Strip quotes */
                int clen = (int)strlen(content);
                if (clen >= 2 && (content[0] == '"' || content[0] == '\'')) {
                    memmove(content, content + 1, (size_t)(clen - 1));
                    clen--;
                    if (clen > 0 && (content[clen-1] == '"' || content[clen-1] == '\''))
                        content[clen - 1] = '\0';
                }
                strncpy(pe->text, content, sizeof(pe->text) - 1);
            }
            update_element_style(pe);
            pe->pointer_events_none = 1; /* style reset clears this — restore */
            pe->cur_r = pe->r; pe->cur_g = pe->g; pe->cur_b = pe->b; pe->cur_a = pe->a;
            pe->cur_bd_r = pe->bd_r; pe->cur_bd_g = pe->bd_g;
            pe->cur_bd_b = pe->bd_b; pe->cur_bd_a = pe->bd_a;
        }
    }
    if (elem_count > dom_count) g_render_order_dirty = 1;
}

// ============================================================
// HTML parsing
// ============================================================

static void set_html_base_dir(const char* layout_path) {
    if (!layout_path || !layout_path[0]) {
        snprintf(g_html_base_dir, sizeof(g_html_base_dir), "ui");
        return;
    }
    strncpy(g_html_base_dir, layout_path, sizeof(g_html_base_dir) - 1);
    g_html_base_dir[sizeof(g_html_base_dir) - 1] = '\0';
    char* slash = strrchr(g_html_base_dir, '/');
    if (slash) {
        *slash = '\0';
    } else if (strchr(g_html_base_dir, '.')) {
        /* Bare filename like "demo.html" — resolve relative to cwd */
        g_html_base_dir[0] = '\0';
    }
    /* else: bare directory name like "ui" — keep as base */
}

static void resolve_resource_path(const char* href, char* out, size_t outsz) {
    if (!href || !href[0]) { out[0] = '\0'; return; }
    if (href[0] == '/' || strchr(href, ':')) {
        snprintf(out, outsz, "%s", href);
        return;
    }
    if (g_html_base_dir[0]) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
        snprintf(out, outsz, "%s/%s", g_html_base_dir, href);
#pragma GCC diagnostic pop
    }
    else
        snprintf(out, outsz, "%s", href);
}

static int tag_attr_equals(const char* tag_buf, const char* key, const char* val) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "%s=\"", key);
    const char* a = strstr(tag_buf, pattern);
    if (!a) return 0;
    char got[64] = {0};
    sscanf(a + (int)strlen(pattern), "%63[^\"]", got);
    return strcasecmp(got, val) == 0;
}

static int tag_is_stylesheet_link(const char* tag_buf) {
    if (!tag_attr_equals(tag_buf, "rel", "stylesheet")) {
        const char* rel = strstr(tag_buf, "rel=\"");
        if (!rel) return 0;
        char rval[32] = {0};
        sscanf(rel + 5, "%31[^\"]", rval);
        return strcasestr(rval, "stylesheet") != NULL;
    }
    return 1;
}

static void load_stylesheet_href(const char* href) {
    if (!href || !href[0]) return;
    char path[512];
    resolve_resource_path(href, path, sizeof(path));
    char* css = read_file(path);
    if (css) {
        parse_css(css);
        free(css);
        g_css_from_document = 1;
    }
}

static void ingest_inline_style(const char* css, int len) {
    if (!css || len <= 0) return;
    char* buf = (char*)malloc((size_t)len + 1);
    if (!buf) return;
    memcpy(buf, css, (size_t)len);
    buf[len] = '\0';
    parse_css(buf);
    free(buf);
    g_css_from_document = 1;
}

static int is_void_element(const char* t) {
    static const char* v[] = {
        "area","base","br","col","embed","hr","img","input",
        "link","meta","param","source","track","wbr",NULL
    };
    for (int i = 0; v[i]; i++) if (strcasecmp(t, v[i]) == 0) return 1;
    return 0;
}

static int is_ignored_element(const char* t) {
    static const char* ig[] = {"html","head","body","style","script","title",NULL};
    for (int i = 0; ig[i]; i++) if (strcasecmp(t, ig[i]) == 0) return 1;
    return 0;
}

static int is_semantic_shell_element(const char* t) {
    static const char* sh[] = {"html","head","body","meta","link","title",NULL};
    for (int i = 0; sh[i]; i++) if (strcasecmp(t, sh[i]) == 0) return 1;
    return 0;
}

/* Decode the HTML character references used by CSS-oriented documents before
 * text reaches the glyph atlas.  This is deliberately small and allocation
 * free: it covers numeric references (the reliable way to name icon-font
 * glyphs) plus the named entities that commonly occur in UI labels. */
static void decode_html_entities(char* s) {
    char* r = s;
    char* w = s;
    while (*r) {
        if (*r != '&') { *w++ = *r++; continue; }
        char* semi = strchr(r + 1, ';');
        if (!semi) { *w++ = *r++; continue; }
        int cp = 0;
        const char* n = r + 1;
        if (*n == '#') {
            char* end = NULL;
            int hex = (n[1] == 'x' || n[1] == 'X');
            cp = (int)strtol(n + (hex ? 2 : 1), &end, hex ? 16 : 10);
            if (end != semi || cp <= 0 || cp > 0x10ffff) { *w++ = *r++; continue; }
        } else if ((semi - n) == 3 && strncmp(n, "amp", 3) == 0) cp = '&';
        else if ((semi - n) == 2 && strncmp(n, "lt", 2) == 0) cp = '<';
        else if ((semi - n) == 2 && strncmp(n, "gt", 2) == 0) cp = '>';
        else if ((semi - n) == 4 && strncmp(n, "quot", 4) == 0) cp = '"';
        else if ((semi - n) == 4 && strncmp(n, "apos", 4) == 0) cp = '\'';
        else if ((semi - n) == 4 && strncmp(n, "nbsp", 4) == 0) cp = 0xA0;
        else { *w++ = *r++; continue; }
        w += utf8_encode(cp, w);
        r = semi + 1;
    }
    *w = '\0';
}

/* Append a direct text node to the current DOM element.  The compact parser
 * deliberately does not materialise text nodes; retaining their bytes on the
 * parent keeps the renderer's allocation-free paint path while covering the
 * common `<span>icon</span> Label` form used throughout desktop chrome. */
static void append_direct_text(int parent_idx, const char* start, const char* end) {
    if (parent_idx < 0 || parent_idx >= elem_count || !start || !end || end <= start) return;
    char text[512];
    size_t n = (size_t)(end - start);
    if (n >= sizeof(text)) n = sizeof(text) - 1;
    memcpy(text, start, n);
    text[n] = '\0';
    trim_whitespace(text);
    if (!text[0]) return;
    decode_html_entities(text);
    LunaElement* parent = &elements[parent_idx];
    size_t used = strlen(parent->text);
    /* Keep the collapsed whitespace that separated the inline element and
       this direct text node in HTML. */
    if (used < sizeof(parent->text) - 1)
        parent->text[used++] = ' ';
    if (used < sizeof(parent->text) - 1) {
        size_t room = sizeof(parent->text) - used - 1;
        size_t take = strlen(text);
        if (take > room) take = room;
        memcpy(parent->text + used, text, take);
        parent->text[used + take] = '\0';
    }
}

void parse_html(const char* html) {
    int parent_stack[64];
    int stack_ptr = 0;
    int current_parent = -1;
    const char* p = html;

    while (*p && elem_count < MAX_ELEMENTS) {
        const char* tag_start = strchr(p, '<');
        if (!tag_start) break;

        // Skip comments <!-- ... -->
        if (strncmp(tag_start + 1, "!--", 3) == 0) {
            const char* end_c = strstr(tag_start, "-->");
            p = end_c ? end_c + 3 : tag_start + strlen(tag_start);
            continue;
        }

        // Skip <!DOCTYPE ...> and other <!...> declarations
        if (tag_start[1] == '!') {
            const char* gt = strchr(tag_start, '>');
            p = gt ? gt + 1 : tag_start + strlen(tag_start);
            continue;
        }

        // Closing tag
        if (tag_start[1] == '/') {
            if (stack_ptr > 0) stack_ptr--;
            current_parent = (stack_ptr > 0) ? parent_stack[stack_ptr - 1] : -1;
            const char* gt = strchr(tag_start, '>');
            /* Text between this close tag and the next tag belongs to the
               enclosing element.  Previously it was skipped completely,
               which lost labels such as `<span>icon</span> Wi-Fi`. */
            if (gt && current_parent >= 0) {
                const char* text_end = strchr(gt + 1, '<');
                if (text_end) append_direct_text(current_parent, gt + 1, text_end);
            }
            p = gt ? gt + 1 : tag_start + strlen(tag_start);
            continue;
        }

        const char* tag_end = strchr(tag_start, '>');
        if (!tag_end) break;

        char tag_buf[512] = {0};
        int tblen = (int)(tag_end - tag_start - 1);
        if (tblen > 511) tblen = 511;
        strncpy(tag_buf, tag_start + 1, tblen);

        // Detect self-closing />
        int is_self_closing = (tblen > 0 && tag_buf[tblen - 1] == '/');

        char type[32] = {0};
        sscanf(tag_buf, "%31s", type);
        // Strip trailing '/' from type
        int tl = (int)strlen(type);
        while (tl > 0 && (type[tl-1] == '/' || isspace((unsigned char)type[tl-1]))) type[--tl] = '\0';

        if (strcasecmp(type, "link") == 0) {
            if (tag_is_stylesheet_link(tag_buf)) {
                char href[256] = {0};
                char* attr_href = strstr(tag_buf, "href=\"");
                if (attr_href) sscanf(attr_href + 6, "%255[^\"]", href);
                load_stylesheet_href(href);
            }
            p = tag_end + 1;
            continue;
        }

        if (strcasecmp(type, "meta") == 0) {
            p = tag_end + 1;
            continue;
        }

        if (strcasecmp(type, "title") == 0) {
            const char* text_start = tag_end + 1;
            const char* close_title = strcasestr(text_start, "</title>");
            if (close_title && close_title > text_start) {
                int tlen = (int)(close_title - text_start);
                if (tlen > (int)sizeof(g_doc_title) - 1) tlen = (int)sizeof(g_doc_title) - 1;
                strncpy(g_doc_title, text_start, (size_t)tlen);
                g_doc_title[tlen] = '\0';
                trim_whitespace(g_doc_title);
            }
            p = close_title ? strchr(close_title, '>') + 1 : tag_end + 1;
            continue;
        }

        // Skip style/script content (style ingests CSS; script is ignored)
        if (strcasecmp(type, "style") == 0) {
            const char* css_start = tag_end + 1;
            const char* close_pos = strcasestr(css_start, "</style>");
            if (close_pos && close_pos > css_start)
                ingest_inline_style(css_start, (int)(close_pos - css_start));
            const char* gt2 = close_pos ? strchr(close_pos, '>') : NULL;
            p = gt2 ? gt2 + 1 : tag_end + 1;
            continue;
        }
        if (strcasecmp(type, "script") == 0) {
            char close_tag[40]; snprintf(close_tag, sizeof(close_tag), "</%s", type);
            const char* close_pos = strcasestr(tag_end + 1, close_tag);
            const char* gt2 = close_pos ? strchr(close_pos, '>') : NULL;
            p = gt2 ? gt2 + 1 : tag_end + 1;
            continue;
        }

        /* <body> becomes a real window-sized container element so body-level
           CSS (display:flex/grid, padding, gap, ...) lays out its children. */
        if (strcasecmp(type, "body") == 0) {
            int existing = -1;
            for (int i = 0; i < elem_count; i++)
                if (strcmp(elements[i].type, "body") == 0) { existing = i; break; }
            if (existing == -1 && elem_count < MAX_ELEMENTS) {
                int bi = elem_count++;
                memset(&elements[bi], 0, sizeof(LunaElement));
                strncpy(elements[bi].type, "body", sizeof(elements[bi].type) - 1);
                char bcls[96] = {0};
                char* battr = strstr(tag_buf, "class=\"");
                if (battr) sscanf(battr + 7, "%95[^\"]", bcls);
                snprintf(elements[bi].class_name, sizeof(elements[bi].class_name), "%s", bcls);
                elements[bi].id_idx = bi;
                elements[bi].parent_idx = -1;
                elements[bi].opacity = 1.0f;
                elements[bi].transform_scale = 1.0f;
                elements[bi].cur_scale = 1.0f;
                elements[bi].anim_speed = 14.0f;
                update_element_style(&elements[bi]);
                elements[bi].z_index = -9999;
                elements[bi].pct_w = 1; elements[bi].raw_w = 1.0f;
                elements[bi].pct_h = 1; elements[bi].raw_h = 1.0f;
                elements[bi].w = window_width; elements[bi].h = window_height;
                elements[bi].cur_r = elements[bi].r; elements[bi].cur_g = elements[bi].g;
                elements[bi].cur_b = elements[bi].b; elements[bi].cur_a = elements[bi].a;
                if (stack_ptr < 63) {
                    parent_stack[stack_ptr++] = bi;
                    current_parent = bi;
                }
            } else if (existing != -1 && stack_ptr < 63) {
                parent_stack[stack_ptr++] = existing;
                current_parent = existing;
            }
            p = tag_end + 1;
            continue;
        }

        if (is_semantic_shell_element(type)) {
            p = tag_end + 1;
            continue;
        }

        if (is_ignored_element(type)) {
            if (!is_void_element(type) && !is_self_closing &&
                strcasecmp(type, "html") != 0 && strcasecmp(type, "head") != 0 &&
                strcasecmp(type, "body") != 0) {
                // Need to push so close tag pops correctly
                if (stack_ptr < 63) parent_stack[stack_ptr++] = -1;
            }
            p = tag_end + 1;
            continue;
        }

        char id[64] = {0}, class_name[96] = {0};
        int draggable = 0;
        char onclick_expr[96] = {0};
        char style_attr[256] = {0};
        char data_tab[32] = {0};
        char* attr_id    = strstr(tag_buf, "id=\"");
        if (attr_id) sscanf(attr_id + 4, "%63[^\"]", id);
        char* attr_class = strstr(tag_buf, "class=\"");
        if (attr_class) sscanf(attr_class + 7, "%95[^\"]", class_name);
        char* attr_drag  = strstr(tag_buf, "draggable=\"");
        if (attr_drag) sscanf(attr_drag + 11, "%d", &draggable);
        if (!extract_html_attr(tag_buf, "onclick", onclick_expr, sizeof(onclick_expr)))
            extract_html_attr(tag_buf, "onClick", onclick_expr, sizeof(onclick_expr));
        extract_html_attr(tag_buf, "style", style_attr, sizeof(style_attr));
        extract_html_attr(tag_buf, "data-tab", data_tab, sizeof(data_tab));
        int tabindex = -2;
        char* attr_tab = strstr(tag_buf, "tabindex=\"");
        if (attr_tab) sscanf(attr_tab + 10, "%d", &tabindex);
        int inert = (strstr(tag_buf, "inert") != NULL);
        char aria_label[128] = {0};
        char role[32] = {0};
        char* attr_aria = strstr(tag_buf, "aria-label=\"");
        if (attr_aria) sscanf(attr_aria + 12, "%127[^\"]", aria_label);
        char* attr_role = strstr(tag_buf, "role=\"");
        if (attr_role) sscanf(attr_role + 6, "%31[^\"]", role);
        int aria_live = 0;
        char* attr_live = strstr(tag_buf, "aria-live=\"");
        if (attr_live) {
            char live_val[16] = {0};
            sscanf(attr_live + 11, "%15[^\"]", live_val);
            if (strcasecmp(live_val, "polite") == 0) aria_live = 1;
            else if (strcasecmp(live_val, "assertive") == 0) aria_live = 2;
        }
        int aria_hidden = 0;
        char* attr_hidden = strstr(tag_buf, "aria-hidden=\"");
        if (attr_hidden) {
            char hidden_val[8] = {0};
            sscanf(attr_hidden + 13, "%7[^\"]", hidden_val);
            aria_hidden = (strcmp(hidden_val, "true") == 0 || strcmp(hidden_val, "1") == 0);
        } else if (strstr(tag_buf, "aria-hidden") != NULL) {
            aria_hidden = 1;
        }
        int aria_expanded = -1;
        char* attr_expanded = strstr(tag_buf, "aria-expanded=\"");
        if (attr_expanded) {
            char exp_val[8] = {0};
            sscanf(attr_expanded + 15, "%7[^\"]", exp_val);
            if (strcmp(exp_val, "true") == 0 || strcmp(exp_val, "1") == 0) aria_expanded = 1;
            else aria_expanded = 0;
        }

        char text[512] = {0};
        const char* next_tag = strchr(tag_end, '<');
        if (next_tag) {
            int text_len = (int)(next_tag - tag_end - 1);
            if (text_len > 0 && text_len < (int)sizeof(text) - 1) {
                strncpy(text, tag_end + 1, text_len);
                text[text_len] = '\0';
                trim_whitespace(text);
                decode_html_entities(text);
            }
        }

        LunaElement e = {0};
        e.id_idx = elem_count; e.parent_idx = current_parent;
        e.w = 100; e.h = 50;
        e.is_draggable = (draggable != 0);
        e.drag_mode = draggable;
        e.tabindex = tabindex;
        e.inert = inert;
        e.aria_live = aria_live;
        e.aria_hidden = aria_hidden;
        e.aria_expanded = aria_expanded;
        snprintf(e.aria_label, sizeof(e.aria_label), "%s", aria_label);
        snprintf(e.role, sizeof(e.role), "%s", role);
        snprintf(e.type, sizeof(e.type), "%s", type);
        snprintf(e.class_name, sizeof(e.class_name), "%s", class_name);
        snprintf(e.id, sizeof(e.id), "%s", id);
        snprintf(e.text, sizeof(e.text), "%s", text);
        if (onclick_expr[0]) parse_onclick_expr(onclick_expr, e.onclick, (int)sizeof(e.onclick));
        if (data_tab[0]) snprintf(e.data_tab, sizeof(e.data_tab), "%s", data_tab);
        if (style_attr[0]) {
            snprintf(e.inline_style, sizeof(e.inline_style), "%s", style_attr);
            e.has_inline_style = 1;
        }
        e.cur_scale = 1.0f;
        e.anim_start_time = -1.0;

        // <img src="..."> — treat src as background image
        if (strcasecmp(type, "img") == 0) {
            char src[256] = {0};
            char* attr_src = strstr(tag_buf, "src=\"");
            if (attr_src) sscanf(attr_src + 5, "%255[^\"]", src);
            if (src[0]) {
                e.has_bg_image = 1;
                strncpy(e.bg_image_path, src, sizeof(e.bg_image_path) - 1);
                e.bg_image_path[sizeof(e.bg_image_path) - 1] = '\0';
            }
            // alt attribute as fallback text
            char alt[256] = {0};
            char* attr_alt = strstr(tag_buf, "alt=\"");
            if (attr_alt) sscanf(attr_alt + 5, "%255[^\"]", alt);
            if (alt[0]) strncpy(e.text, alt, sizeof(e.text) - 1);
        }

        /* <input> / <textarea> — editable CSS-painted controls */
        if (strcasecmp(type, "input") == 0 || strcasecmp(type, "textarea") == 0) {
            e.is_input = 1;
            e.input_multiline = (strcasecmp(type, "textarea") == 0) ? 1 : 0;
            e.caret = 0;
            e.cursor_type = 2; /* text */
            if (e.tabindex == -2) e.tabindex = 0;
            char itype[32] = {0};
            extract_html_attr(tag_buf, "type", itype, sizeof(itype));
            if (itype[0] && strcasecmp(itype, "password") == 0) e.input_password = 1;
            if (itype[0] && (strcasecmp(itype, "submit") == 0 || strcasecmp(itype, "button") == 0 ||
                             strcasecmp(itype, "checkbox") == 0 || strcasecmp(itype, "radio") == 0 ||
                             strcasecmp(itype, "file") == 0 || strcasecmp(itype, "hidden") == 0 ||
                             strcasecmp(itype, "image") == 0 || strcasecmp(itype, "reset") == 0)) {
                e.is_input = 0; /* non-text inputs are not text editors */
            }
            char val[512] = {0};
            if (extract_html_attr(tag_buf, "value", val, sizeof(val))) {
                strncpy(e.text, val, sizeof(e.text) - 1);
                e.text[sizeof(e.text) - 1] = '\0';
                e.caret = (int)strlen(e.text);
            }
            extract_html_attr(tag_buf, "placeholder", e.placeholder, sizeof(e.placeholder));
            if (e.is_input && !e.input_multiline) e.white_space = 1; /* nowrap */
        }

        elements[elem_count] = e;
        update_element_style(&elements[elem_count]);

        LunaElement* ne = &elements[elem_count];
        ne->cur_r = ne->r; ne->cur_g = ne->g; ne->cur_b = ne->b; ne->cur_a = ne->a;
        ne->cur_bd_r = ne->bd_r; ne->cur_bd_g = ne->bd_g;
        ne->cur_bd_b = ne->bd_b; ne->cur_bd_a = ne->bd_a;

        if (!is_void_element(type) && !is_self_closing && stack_ptr < 63) {
            parent_stack[stack_ptr++] = elem_count;
            current_parent = elem_count;
        }
        elem_count++;
        p = tag_end + 1;
    }

    /* Structural pseudo-classes (:first-child, :last-child, :nth-child) depend
       on the FINAL sibling set — per-element resolution above ran while the
       tree was still growing, so re-resolve every element now. */
    for (int i = 0; i < elem_count; i++) {
        LunaElement* e = &elements[i];
        if (e->luna_internal) continue;
        update_element_style(e);
        e->cur_r = e->r; e->cur_g = e->g; e->cur_b = e->b; e->cur_a = e->a;
        e->cur_bd_r = e->bd_r; e->cur_bd_g = e->bd_g;
        e->cur_bd_b = e->bd_b; e->cur_bd_a = e->bd_a;
    }
    /* Generate ::before / ::after pseudo-element nodes */
    generate_pseudo_elements();
    g_layout_dirty = 1;
}

// ============================================================
// Layout & Animation
// ============================================================

FontAtlas* get_atlas(float size, int bold, int* out_is_fake_bold);
float measure_text_width(FontAtlas* atlas, const char* text);
static void text_metrics_begin(float css_px, int bold, int face, FontAtlas* atlas);
static void text_metrics_end(void);

static float css_outer_height(const LunaElement* e, float css_h) {
    return e->box_sizing == BOX_CONTENT
        ? css_h + e->pad_t + e->pad_b + e->border_width * 2.0f
        : css_h;
}

static float css_outer_width(const LunaElement* e, float css_w) {
    return e->box_sizing == BOX_CONTENT
        ? css_w + e->pad_l + e->pad_r + e->border_width * 2.0f
        : css_w;
}

static float resolved_max_width(const LunaElement* e, float containing_width) {
    float css_w = e->max_width_pct
        ? containing_width * e->raw_max_width + e->raw_max_width_off
        : e->css_max_width;
    if (css_w < 0.0f) css_w = 0.0f;
    return css_outer_width(e, css_w);
}

static float resolved_max_height(const LunaElement* e, float containing_height) {
    float css_h = e->max_height_pct
        ? containing_height * e->raw_max_height + e->raw_max_height_off
        : e->css_max_height;
    if (css_h < 0.0f) css_h = 0.0f;
    return css_outer_height(e, css_h);
}

/* Outer main-axis size of a flex item in a row container (used while packing
 * wrap lines during intrinsic height).  Mirrors flex_main_size's width path
 * without depending on it — that helper is defined later and calls us. */
static float flow_row_item_main(const LunaElement* ch) {
    float size;
    if (ch->has_flex_basis && !ch->flex_basis_auto)
        size = ch->flex_basis;
    else if (ch->has_css_width && !ch->pct_w)
        size = ch->css_width;
    else if (ch->w > 0.0f)
        size = ch->w;
    else
        size = 40.0f; /* last-resort; wrap measure prefers definite widths */
    float min_w = css_outer_width(ch, ch->css_min_width);
    if (ch->has_min_width && size < min_w) size = min_w;
    if (ch->has_max_width && !ch->max_width_pct && size > ch->css_max_width)
        size = ch->css_max_width;
    return size + ch->margin_left + ch->margin_right;
}

static float flow_content_height(LunaElement* e) {
    int idx = (int)(e - elements);

    /* Does this element have in-flow children? If so its auto height is derived
       from them (sum for column/block, max for nowrap row, packed lines for
       wrap row); otherwise fall back to a single text line. Without this, an
       auto-height container collapses to one line-height and then flex-shrink
       crushes its children (e.g. #sidebar_nav squashing its 34px nav items). */
    int kids[MAX_ELEMENTS];
    float kid_h[MAX_ELEMENTS];
    int n = 0;
    float total = 0.0f, maxh = 0.0f;
    for (int c = 0; c < elem_count; c++) {
        LunaElement* ch = &elements[c];
        if (ch->parent_idx != idx) continue;
        if (!is_visible(c)) continue;
        if (ch->position_mode == POS_ABSOLUTE || ch->position_fixed) continue;
        float chh;
        if (ch->has_css_height && !ch->pct_h) {
            chh = (ch->box_sizing == BOX_CONTENT)
                ? ch->css_height + ch->pad_t + ch->pad_b + ch->border_width * 2.0f
                : ch->css_height;
        } else if (ch->pct_h) {
            chh = 0.0f;              /* percentage height is not a definite size here */
        } else {
            chh = flow_content_height(ch);   /* recurse for auto-height children */
        }
        /* min/max constraints participate in intrinsic sizing.  In
           particular, buttons commonly express their control height with
           min-height; ignoring it collapses an auto-height flex row to its
           text line and centers the controls outside the container. */
        float min_h = css_outer_height(ch, ch->css_min_height);
        float max_h = css_outer_height(ch, ch->css_max_height);
        if (ch->has_min_height && chh < min_h) chh = min_h;
        /* A percentage max-height is indefinite during intrinsic sizing.
           It is resolved later once the containing block height is known. */
        if (ch->has_max_height && !ch->max_height_pct && chh > max_h) chh = max_h;
        chh += ch->margin_top + ch->margin_bottom;
        if (chh > maxh) maxh = chh;
        total += chh;
        if (n < MAX_ELEMENTS) {
            kids[n] = c;
            kid_h[n] = chh;
            n++;
        }
    }

    if (n > 0) {
        int row = (e->display_mode == DISPLAY_FLEX && e->flex_direction == FLEX_DIR_ROW);
        int wrap = row && (e->flex_wrap == FLEX_WRAP_WRAP);
        /* A flex container can have both element children and direct text.
           The latter becomes an anonymous flex item in CSS, so it must
           contribute its line box to the intrinsic cross size.  Omitting it
           made compact rows such as the About version badge collapse to the
           height of their tiny icon child. */
        if (row && e->text[0]) {
            float text_lh = e->line_height;
            if (text_lh < 0.0f) text_lh = -text_lh * e->font_size;
            if (text_lh <= 0.0f) {
                text_lh = (float)e->font_size;
                if (text_lh < 10.0f) text_lh = 12.0f;
                text_lh *= 1.5f;
            }
            if (text_lh > maxh) maxh = text_lh;
        }

        float inner;
        if (wrap) {
            /* Pack items into lines the same way layout_flex_container does.
             * Using only maxh (nowrap behaviour) made #skin_cards report a
             * one-row height while wrapping painted additional rows over the
             * wallpaper / cursor sections of the Appearance panel. */
            float avail = 0.0f;
            if (e->pct_w && e->parent_idx >= 0 && e->parent_idx < elem_count) {
                /* Percentage width must be resolved against the parent's
                 * current width — e->w may still hold a pre-stretch stale
                 * value when a column parent is measuring this child. */
                LunaElement* par = &elements[e->parent_idx];
                float pw = par->w - par->pad_l - par->pad_r - par->border_width * 2.0f
                         - e->margin_left - e->margin_right;
                if (pw < 0.0f) pw = 0.0f;
                avail = pw * e->raw_w + e->raw_w_off
                      - e->pad_l - e->pad_r - e->border_width * 2.0f;
            } else {
                avail = e->w - e->pad_l - e->pad_r - e->border_width * 2.0f;
            }
            if (avail <= 0.5f && e->parent_idx >= 0 && e->parent_idx < elem_count) {
                LunaElement* par = &elements[e->parent_idx];
                float pw = par->w - par->pad_l - par->pad_r - par->border_width * 2.0f
                         - e->margin_left - e->margin_right;
                avail = pw - e->pad_l - e->pad_r - e->border_width * 2.0f;
            }
            if (avail > 0.5f) {
                float gap = e->flex_gap;
                float line_main = 0.0f, line_cross = 0.0f, total_cross = 0.0f;
                int line_items = 0;
                for (int i = 0; i < n; i++) {
                    float item_main = flow_row_item_main(&elements[kids[i]]);
                    float item_cross = kid_h[i];
                    float need = item_main + (line_items > 0 ? gap : 0.0f);
                    if (line_items > 0 && line_main + need > avail + 0.5f) {
                        total_cross += line_cross + (total_cross > 0.0f ? gap : 0.0f);
                        line_main = item_main;
                        line_cross = item_cross;
                        line_items = 1;
                    } else {
                        line_main += need;
                        if (item_cross > line_cross) line_cross = item_cross;
                        line_items++;
                    }
                }
                if (line_items > 0)
                    total_cross += line_cross + (total_cross > 0.0f ? gap : 0.0f);
                inner = total_cross > 0.0f ? total_cross : maxh;
            } else {
                inner = maxh;
            }
        } else {
            inner = row ? maxh : (total + (n > 1 ? e->flex_gap * (float)(n - 1) : 0.0f));
        }
        return inner + e->pad_t + e->pad_b + e->border_width * 2.0f;
    }

    float lh = e->line_height;
    if (lh < 0.0f) lh = -lh * e->font_size; /* unitless CSS multiplier */
    if (lh <= 0.0f) {
        lh = (float)e->font_size;
        if (lh < 10.0f) lh = 12.0f;
        lh *= 1.5f;
    }
    return lh + e->pad_t + e->pad_b + e->border_width * 2.0f;
}

static float intrinsic_content_width(LunaElement* e) {
    int idx = (int)(e - elements);
    if (idx >= 0 && idx < elem_count && g_intrinsic_width_valid[idx])
        return g_intrinsic_width_cache[idx];

    float result;
    if (e->has_css_width && !e->pct_w) {
        result = e->css_width;
        goto done;
    }

    int n = 0;
    float total = 0.0f, maxw = 0.0f;
    for (int c = 0; c < elem_count; c++) {
        LunaElement* ch = &elements[c];
        if (ch->parent_idx != idx || !is_visible(c)) continue;
        if (ch->position_mode == POS_ABSOLUTE || ch->position_fixed) continue;
        float cw = ch->pct_w ? 0.0f : intrinsic_content_width(ch);
        if (ch->has_min_width && cw < ch->css_min_width) cw = ch->css_min_width;
        /* A percentage max-width is indefinite during intrinsic sizing.  It
           is resolved later by the containing flex/block formatting context. */
        if (ch->has_max_width && !ch->max_width_pct && cw > ch->css_max_width)
            cw = ch->css_max_width;
        cw += ch->margin_left + ch->margin_right;
        total += cw;
        if (cw > maxw) maxw = cw;
        n++;
    }
    if (n > 0) {
        int row = (e->display_mode == DISPLAY_FLEX && e->flex_direction == FLEX_DIR_ROW);
        float inner = row ? total + e->flex_gap * (float)(n - 1) : maxw;
        /* Direct text alongside flex children is an anonymous item, not an
           overlay.  Include it in intrinsic sizing so min-width does not
           clip icon labels such as Wi-Fi or AC power state. */
        if (row && e->text[0]) {
            float tw;
            if (font_loaded) {
                FontAtlas* atlas = get_atlas(e->font_size, e->font_bold, NULL);
                text_metrics_begin(e->font_size, e->font_bold, e->font_face, atlas);
                g_text_letter_spacing = e->letter_spacing;
                tw = measure_text_width(atlas, e->text);
                g_text_letter_spacing = 0.0f;
                text_metrics_end();
            } else {
                tw = strlen(e->text) * (float)e->font_size * 0.55f;
            }
            inner += tw;
        }
        result = inner + e->pad_l + e->pad_r + e->border_width * 2.0f;
        goto done;
    }

    if (e->text[0] && font_loaded) {
        FontAtlas* atlas = get_atlas(e->font_size, e->font_bold, NULL);
        text_metrics_begin(e->font_size, e->font_bold, e->font_face, atlas);
        g_text_letter_spacing = e->letter_spacing;
        /* measure what will actually be drawn: text-transform changes width */
        const char* txt = e->text;
        char tbuf[256];
        if (e->text_transform == 1 || e->text_transform == 2) {
            int n = 0;
            for (const char* s = e->text; *s && n < (int)sizeof(tbuf) - 1; s++, n++)
                tbuf[n] = (char)(e->text_transform == 1 ? toupper((unsigned char)*s)
                                                         : tolower((unsigned char)*s));
            tbuf[n] = '\0';
            txt = tbuf;
        }
        float tw = measure_text_width(atlas, txt) + e->pad_l + e->pad_r + 4.0f;
        g_text_letter_spacing = 0.0f;
        text_metrics_end();
        result = tw;
        goto done;
    }
    if (e->text[0]) {
        result = strlen(e->text) * (float)(e->font_size > 0 ? e->font_size : 12) * 0.55f +
                 e->pad_l + e->pad_r + 4.0f;
        goto done;
    }
    if (e->w > 0.0f && e->w < 200.0f && e->w != 100.0f)
        result = e->w;
    else
        result = 40.0f;

done:
    if (idx >= 0 && idx < elem_count) {
        g_intrinsic_width_cache[idx] = result;
        g_intrinsic_width_valid[idx] = 1;
    }
    return result;
}

static float flex_content_width(LunaElement* ch) {
    return intrinsic_content_width(ch);
}

static int is_out_of_flow(int idx) {
    LunaElement* e = &elements[idx];
    if (e->position_fixed || e->flex_child || e->grid_child) return 1;
    if (e->position_mode == POS_ABSOLUTE) return 1;
    if (e->position_sticky) return 0;
    if (e->position_mode == POS_RELATIVE) return 0;
    if (e->css_positioned & 3) return 1;
    return 0;
}

static void layout_block_container(int container_idx) {
    LunaElement* cont = &elements[container_idx];
    if (cont->display_mode == DISPLAY_FLEX || cont->display_mode == DISPLAY_GRID) return;
    if (cont->display_mode == DISPLAY_NONE) return;

    float inner_w = cont->w - cont->pad_l - cont->pad_r - cont->border_width * 2.0f;
    float inner_h = cont->h - cont->pad_t - cont->pad_b - cont->border_width * 2.0f;
    if (inner_w < 0.0f) inner_w = 0.0f;
    if (inner_h < 0.0f) inner_h = 0.0f;
    float y = cont->border_width + cont->pad_t;

    for (int c = 0; c < elem_count; c++) {
        if (elements[c].parent_idx != container_idx) continue;
        if (!is_visible(c) || elements[c].position_fixed) continue;
        if (is_out_of_flow(c)) continue;

        LunaElement* ch = &elements[c];
        ch->flow_child = 1;

        if (ch->pct_w) {
            ch->w = inner_w * ch->raw_w;
        } else if (!ch->has_css_width) {
            ch->w = inner_w;
        }

        if (!ch->has_css_height && !ch->pct_h) {
            ch->h = flow_content_height(ch);
            if (ch->h < 14.0f) ch->h = 14.0f;
        }
        if (ch->has_min_height) {
            float min_h = css_outer_height(ch, ch->css_min_height);
            if (ch->h < min_h) ch->h = min_h;
        }
        if (ch->has_max_height) {
            float max_h = resolved_max_height(ch, inner_h);
            if (ch->h > max_h) ch->h = max_h;
        }

        ch->rel_x = cont->border_width + cont->pad_l + ch->margin_left;
        ch->rel_y = y + ch->margin_top;
        y += ch->margin_top + ch->h + ch->margin_bottom;
    }

    /* height:auto on absolute/fixed → shrink-wrap in-flow children (browser parity).
       Without this, menus/panels keep a collapsed height and only paint a short
       background while children draw outside the panel. */
    if (!cont->has_css_height && !cont->pct_h &&
        !(cont->has_top && cont->has_bottom) &&
        (cont->position_mode == POS_ABSOLUTE || cont->position_fixed)) {
        float auto_h = y + cont->pad_b;
        if (cont->border_width > 0.0f && cont->box_sizing != BOX_CONTENT)
            auto_h += cont->border_width * 2.0f;
        cont->h = auto_h;
        if (cont->has_min_height && cont->h < cont->css_min_height)
            cont->h = cont->css_min_height;
        if (cont->has_max_height && !cont->max_height_pct &&
            cont->h > cont->css_max_height)
            cont->h = cont->css_max_height;
    }
}

static int is_positioned_element(const LunaElement* e) {
    return e->position_fixed || e->position_sticky ||
           e->position_mode == POS_ABSOLUTE || e->position_mode == POS_RELATIVE;
}

static int find_containing_block(int idx) {
    int p = elements[idx].parent_idx;
    while (p != -1) {
        if (is_positioned_element(&elements[p])) return p;
        p = elements[p].parent_idx;
    }
    return elements[idx].parent_idx;
}

static void containing_block_rect(int cb_idx, float* ox, float* oy, float* cw, float* ch) {
    LunaElement* cb = &elements[cb_idx];
    /* CSS abspos containing block is the padding box (inside the border),
     * not the content box.  Using content edges made `top`/`right` on dialog
     * chrome collide with padded in-flow children (About close vs hero). */
    *ox = cb->x + cb->border_width;
    *oy = cb->y + cb->border_width;
    *cw = cb->w - cb->border_width * 2.0f;
    *ch = cb->h - cb->border_width * 2.0f;
    if (*cw < 0.0f) *cw = 0.0f;
    if (*ch < 0.0f) *ch = 0.0f;
}

void update_layout() {
    for (int i = 0; i < elem_count; i++) {
        LunaElement* e = &elements[i];
        int par = e->parent_idx;
        float parent_w, parent_h;
        float cb_ox = 0.0f, cb_oy = 0.0f;
        int use_cb = (e->position_mode == POS_ABSOLUTE && par != -1);

        // body/html root element: always cover full window regardless of CSS sizes
        if (par == -1 && e->z_index <= -9000 &&
            (strcmp(e->type, "body") == 0 || strcmp(e->type, "html") == 0)) {
            e->x = 0.0f; e->y = 0.0f;
            e->w = window_width; e->h = window_height;
            e->rel_x = 0.0f; e->rel_y = 0.0f;
            continue;
        }

        if (e->position_fixed || par == -1) {
            parent_w = window_width;
            parent_h = window_height;
        } else if (use_cb) {
            int cb = find_containing_block(i);
            containing_block_rect(cb, &cb_ox, &cb_oy, &parent_w, &parent_h);
        } else {
            LunaElement* parent = &elements[par];
            parent_w = parent->w - parent->pad_l - parent->pad_r - parent->border_width * 2.0f;
            parent_h = parent->h - parent->pad_t - parent->pad_b - parent->border_width * 2.0f;
            if (parent_w < 0.0f) parent_w = 0.0f;
            if (parent_h < 0.0f) parent_h = 0.0f;
        }

        if (e->pct_w) e->w = parent_w * e->raw_w + e->raw_w_off;
        if (e->pct_h) e->h = parent_h * e->raw_h + e->raw_h_off;

        if (e->has_css_width && !e->pct_w) {
            if (e->box_sizing == BOX_CONTENT)
                e->w = e->css_width + e->pad_l + e->pad_r + e->border_width * 2.0f;
            else
                e->w = e->css_width;
        }
        if (e->has_css_height && !e->pct_h) {
            if (e->box_sizing == BOX_CONTENT)
                e->h = e->css_height + e->pad_t + e->pad_b + e->border_width * 2.0f;
            else
                e->h = e->css_height;
        } else if (!e->pct_h && !(e->has_top && e->has_bottom)) {
            /* CSS height:auto shrink-wraps an out-of-flow root and contributes
               the intrinsic height of flex/block descendants.  Previously
               only children were sized intrinsically inside their parent's
               layout pass, leaving a root column-flex at padding+border height
               and forcing every child through flex-shrink. */
            e->h = flow_content_height(e);
        }
        float min_w = css_outer_width(e, e->css_min_width);
        float min_h = css_outer_height(e, e->css_min_height);
        float max_w = resolved_max_width(e, parent_w);
        float max_h = resolved_max_height(e, parent_h);
        if (e->has_min_width && e->w < min_w) e->w = min_w;
        if (e->has_min_height && e->h < min_h) e->h = min_h;
        if (e->has_max_width && e->w > max_w) e->w = max_w;
        if (e->has_max_height && e->h > max_h) e->h = max_h;
        /* aspect-ratio: if one dimension known, compute the other */
        if (e->has_aspect_ratio && e->aspect_ratio > 0.0f) {
            if (e->has_css_width && !e->has_css_height)
                e->h = e->w / e->aspect_ratio;
            else if (e->has_css_height && !e->has_css_width)
                e->w = e->h * e->aspect_ratio;
        }

        if (!e->pos_overridden_x) {
            /* Both left+right set (no explicit width): stretch to fill (CSS inset). */
            if (e->has_left && e->has_right && !e->has_css_width && !e->pct_w) {
                float lv = e->pct_left ? (parent_w * e->raw_left + e->raw_left_off) : e->rel_x;
                float rv = e->pct_right ? (parent_w * e->raw_right + e->raw_right_off) : e->right_val;
                e->rel_x = lv;
                e->w = parent_w - lv - rv;
            } else if (e->has_right && !e->has_left) {
                /* Right-anchored only */
                float off = e->pct_right ? (parent_w * e->raw_right + e->raw_right_off) : e->right_val;
                e->rel_x = parent_w - e->w - off;
            } else if (e->pct_left) {
                e->rel_x = parent_w * e->raw_left + e->raw_left_off;
            }
            /* non-percentage left: rel_x already holds the left value */
        }

        if (!e->pos_overridden_y) {
            /* Both top+bottom set (no explicit height): stretch to fill. */
            if (e->has_top && e->has_bottom && !e->has_css_height && !e->pct_h) {
                float tv = e->pct_top ? (parent_h * e->raw_top + e->raw_top_off) : e->rel_y;
                float bv = e->pct_bottom ? (parent_h * e->raw_bottom + e->raw_bottom_off) : e->bottom_val;
                e->rel_y = tv;
                e->h = parent_h - tv - bv;
            } else if (e->has_bottom && !e->has_top) {
                /* Bottom-anchored only */
                float off = e->pct_bottom ? (parent_h * e->raw_bottom + e->raw_bottom_off) : e->bottom_val;
                e->rel_y = parent_h - e->h - off;
            } else if (e->pct_top) {
                e->rel_y = parent_h * e->raw_top + e->raw_top_off;
            }
            /* non-percentage top: rel_y already holds the top value */
        }

        if (e->position_fixed || par == -1) {
            e->x = e->rel_x + e->margin_left;
            e->y = e->rel_y + e->margin_top;
        } else if (use_cb) {
            e->x = cb_ox + e->rel_x + e->margin_left;
            e->y = cb_oy + e->rel_y + e->margin_top;
        } else {
            e->x = elements[par].x + e->rel_x + e->margin_left;
            e->y = elements[par].y + e->rel_y + e->margin_top;
        }
    }
}

static int child_cross_align(LunaElement* ch, LunaElement* cont, int row_mode) {
    (void)row_mode;
    int align = (ch->align_self >= 0) ? ch->align_self : cont->align_items;
    if (align == FLEX_ALIGN_START) return 0;
    if (align == FLEX_ALIGN_CENTER) return 1;
    if (align == FLEX_ALIGN_END) return 2;
    return 3;
}

static void place_flex_cross(LunaElement* ch, LunaElement* cont, int row_mode,
                             float pad, float inner_cross, float cross_len, int align) {
    (void)cont;
    if (row_mode) {
        /* CSS align-items:stretch only applies when the cross size is auto. */
        if (align == 3 && !(ch->css_positioned & 2) && !ch->has_css_height && !ch->pct_h) {
            ch->h = inner_cross; ch->rel_y = pad; return;
        }
        if (!(ch->css_positioned & 2)) {
            if (align == 1) ch->rel_y = pad + (inner_cross - cross_len) * 0.5f;
            else if (align == 2) ch->rel_y = pad + inner_cross - cross_len;
            else ch->rel_y = pad;
        }
    } else {
        if (align == 3 && !(ch->css_positioned & 1) && !ch->has_css_width && !ch->pct_w) {
            ch->w = inner_cross; ch->rel_x = pad; return;
        }
        if (!(ch->css_positioned & 1)) {
            if (align == 1) ch->rel_x = pad + (inner_cross - cross_len) * 0.5f;
            else if (align == 2) ch->rel_x = pad + inner_cross - cross_len;
            else ch->rel_x = pad;
        }
    }
}

static float flex_main_size(LunaElement* ch, int row_mode, float available_main) {
    float size;
    if (ch->has_flex_basis && !ch->flex_basis_auto)
        size = ch->flex_basis;
    else if (row_mode) {
        if (ch->has_css_width && !ch->pct_w) size = ch->css_width;
        else size = flex_content_width(ch);
    } else {
        if (ch->has_css_height && !ch->pct_h) size = ch->css_height;
        else size = flow_content_height(ch);
    }
    if (row_mode) {
        float min_w = css_outer_width(ch, ch->css_min_width);
        float max_w = resolved_max_width(ch, available_main);
        if (ch->has_min_width && size < min_w) size = min_w;
        if (ch->has_max_width && size > max_w) size = max_w;
    } else {
        float min_h = css_outer_height(ch, ch->css_min_height);
        float max_h = resolved_max_height(ch, available_main);
        if (ch->has_min_height && size < min_h) size = min_h;
        if (ch->has_max_height && size > max_h) size = max_h;
    }
    return size;
}

static float flex_min_main(LunaElement* ch, int row_mode) {
    if (row_mode && ch->has_min_width) return css_outer_width(ch, ch->css_min_width);
    if (!row_mode && ch->has_min_height) return css_outer_height(ch, ch->css_min_height);
    /* A definite main-axis size must survive flex shrink.  Treating every
       item without min-* as shrinkable to zero made fixed-height action rows
       disappear from constrained dialogs (notably alert buttons). */
    if (row_mode && ch->has_css_width && !ch->pct_w)
        return ch->box_sizing == BOX_CONTENT
            ? ch->css_width + ch->pad_l + ch->pad_r + ch->border_width * 2.0f
            : ch->css_width;
    if (!row_mode && ch->has_css_height && !ch->pct_h)
        return ch->box_sizing == BOX_CONTENT
            ? ch->css_height + ch->pad_t + ch->pad_b + ch->border_width * 2.0f
            : ch->css_height;
    /* CSS flexbox min-width/min-height:auto → content-based minimum when
     * overflow is visible.  Clipped/scrollable items may shrink to zero so a
     * constrained flex parent can still fit them.  Without the content
     * minimum, a wrapping row such as #skin_cards was flex-shrunk below its
     * packed height and then re-expanded during its own layout pass, leaving
     * wallpaper/cursor rows overlapped in the Appearance panel. */
    if (row_mode) {
        if (overflow_clips(ch->overflow_x)) return 0.0f;
        float content = flex_content_width(ch);
        if (ch->has_max_width && !ch->max_width_pct && content > ch->css_max_width)
            content = ch->css_max_width;
        return content;
    }
    if (overflow_clips(ch->overflow_y)) return 0.0f;
    return flow_content_height(ch);
}

/* CSS: a flex item's cross size defaults to its content size unless it is
   stretched. Resolve auto cross sizes from content so non-stretch alignment
   doesn't inherit stale/parse-default dimensions. Returns the cross length. */
static float resolve_flex_cross_len(LunaElement* ch, int row_mode, int align,
                                    float available_cross) {
    if (row_mode) {
        if (align != 3 && !ch->has_css_height && !ch->pct_h && !(ch->css_positioned & 2)) {
            float hh = flow_content_height(ch);
            if (available_cross >= 0.0f && hh > available_cross) hh = available_cross;
            float min_h = css_outer_height(ch, ch->css_min_height);
            float max_h = resolved_max_height(ch, available_cross);
            if (ch->has_min_height && hh < min_h) hh = min_h;
            if (ch->has_max_height && hh > max_h) hh = max_h;
            ch->h = hh;
        }
        return ch->h;
    }
    if (align != 3 && !ch->has_css_width && !ch->pct_w && !(ch->css_positioned & 1)) {
        float ww = flex_content_width(ch);
        if (available_cross >= 0.0f && ww > available_cross) ww = available_cross;
        float min_w = css_outer_width(ch, ch->css_min_width);
        float max_w = resolved_max_width(ch, available_cross);
        if (ch->has_min_width && ww < min_w) ww = min_w;
        if (ch->has_max_width && ww > max_w) ww = max_w;
        ch->w = ww;
    }
    return ch->w;
}

static void layout_flex_line(LunaElement* cont, int* kids, int n, int row_mode,
                             float pad_main, float pad_cross,
                             float inner_main, float inner_cross, float gap) {
    float main_sz[MAX_ELEMENTS];
    float fixed_main = 0.0f;
    int grow_n = 0;
    float grow_sum = 0.0f;

    /* Column flex: resolve the cross size (width) before measuring main size
     * (height).  Wrapping row children such as #skin_cards need a definite
     * width to pack lines; stretching after the height measure left them at a
     * one-row intrinsic height and overlapped later Appearance sections. */
    if (!row_mode) {
        for (int k = 0; k < n; k++) {
            LunaElement* ch = &elements[kids[k]];
            if (ch->css_positioned & 1) continue;
            int align = child_cross_align(ch, cont, 0);
            if (ch->pct_w) {
                ch->w = inner_cross * ch->raw_w + ch->raw_w_off;
            } else if (align == 3 && !ch->has_css_width) {
                ch->w = inner_cross;
            }
        }
    }

    for (int k = 0; k < n; k++) {
        LunaElement* ch = &elements[kids[k]];
        float ml = flex_main_size(ch, row_mode, inner_main);
        float margin_before = row_mode ? ch->margin_left : ch->margin_top;
        float margin_after = row_mode ? ch->margin_right : ch->margin_bottom;
        main_sz[k] = ml;
        /* Sticky stays in-flow even if left/top insets were declared. */
        int positioned = ch->position_sticky ? 0
            : (row_mode ? (ch->css_positioned & 1) : (ch->css_positioned & 2));
        if (!positioned && ch->flex_grow > 0) {
            grow_n++;
            grow_sum += ch->flex_grow;
            fixed_main += ml + margin_before + margin_after;
        } else {
            fixed_main += ml + margin_before + margin_after;
        }
    }
    fixed_main += gap * (float)(n > 0 ? n - 1 : 0);
    float free_main = inner_main - fixed_main;
    if (free_main > 0.0f) {
        for (int k = 0; k < n; k++) {
            LunaElement* ch = &elements[kids[k]];
            int positioned = ch->position_sticky ? 0
                : (row_mode ? (ch->css_positioned & 1) : (ch->css_positioned & 2));
            if (!positioned && ch->flex_grow > 0 && grow_n > 0)
                main_sz[k] += free_main * (ch->flex_grow / grow_sum);
        }
    } else if (free_main < 0.0f) {
        float overflow = -free_main;
        float shrink_sum = 0.0f;
        for (int k = 0; k < n; k++) {
            LunaElement* ch = &elements[kids[k]];
            int positioned = ch->position_sticky ? 0
                : (row_mode ? (ch->css_positioned & 1) : (ch->css_positioned & 2));
            if (positioned || ch->flex_shrink <= 0) continue;
            shrink_sum += ch->flex_shrink * main_sz[k];
        }
        if (shrink_sum > 0.0f) {
            for (int k = 0; k < n; k++) {
                LunaElement* ch = &elements[kids[k]];
                int positioned = ch->position_sticky ? 0
                    : (row_mode ? (ch->css_positioned & 1) : (ch->css_positioned & 2));
                if (positioned || ch->flex_shrink <= 0) continue;
                float factor = (ch->flex_shrink * main_sz[k]) / shrink_sum;
                float min_sz = flex_min_main(ch, row_mode);
                main_sz[k] -= overflow * factor;
                if (main_sz[k] < min_sz) main_sz[k] = min_sz;
            }
        }
    }

    float total = 0.0f;
    for (int k = 0; k < n; k++) {
        LunaElement* ch = &elements[kids[k]];
        total += main_sz[k] + (row_mode ? ch->margin_left + ch->margin_right
                                        : ch->margin_top + ch->margin_bottom);
    }
    total += gap * (float)(n > 1 ? n - 1 : 0);

    float start = pad_main;
    float use_gap = gap;
    if (cont->justify_content == FLEX_JUSTIFY_CENTER)
        start = pad_main + (inner_main - total) * 0.5f;
    else if (cont->justify_content == FLEX_JUSTIFY_END)
        start = pad_main + inner_main - total;
    else if (cont->justify_content == FLEX_JUSTIFY_SPACE_BETWEEN && n > 1) {
        float content = 0.0f;
        for (int k = 0; k < n; k++) {
            LunaElement* ch = &elements[kids[k]];
            content += main_sz[k] + (row_mode ? ch->margin_left + ch->margin_right
                                               : ch->margin_top + ch->margin_bottom);
        }
        use_gap = (inner_main - content) / (float)(n - 1);
        if (use_gap < 0.0f) use_gap = 0.0f;
        start = pad_main;
    }

    /* Auto-margin: find first item with margin_left_auto (row) or margin_top_auto (col) */
    /* This item absorbs all free space as its margin, pushing it to the end */
    int auto_margin_k = -1;
    for (int k = 0; k < n; k++) {
        LunaElement* ch = &elements[kids[k]];
        if (row_mode && ch->margin_left_auto) { auto_margin_k = k; break; }
        if (!row_mode && ch->margin_top_auto) { auto_margin_k = k; break; }
    }
    if (auto_margin_k >= 0 && free_main > 0.0f) {
        float cursor_a = pad_main;
        for (int k = 0; k < auto_margin_k; k++) {
            LunaElement* ch = &elements[kids[k]];
            int align = child_cross_align(ch, cont, row_mode);
            float cross_len = resolve_flex_cross_len(ch, row_mode, align, inner_cross);
            if (row_mode) {
                if (!(ch->css_positioned & 1)) { ch->w = main_sz[k]; ch->rel_x = cursor_a; }
                place_flex_cross(ch, cont, 1, pad_cross, inner_cross, cross_len, align);
                if (!(ch->css_positioned & 1))
                    cursor_a += ch->margin_left + main_sz[k] + ch->margin_right + use_gap;
            } else {
                if (!(ch->css_positioned & 2)) { ch->h = main_sz[k]; ch->rel_y = cursor_a; }
                place_flex_cross(ch, cont, 0, pad_cross, inner_cross, cross_len, align);
                if (!(ch->css_positioned & 2))
                    cursor_a += ch->margin_top + main_sz[k] + ch->margin_bottom + use_gap;
            }
        }
        float after_total = 0.0f;
        for (int k = auto_margin_k; k < n; k++) {
            LunaElement* ch = &elements[kids[k]];
            after_total += main_sz[k] + (row_mode ? ch->margin_left + ch->margin_right
                                                  : ch->margin_top + ch->margin_bottom);
        }
        after_total += use_gap * (float)(n - auto_margin_k - 1);
        float cursor_b = pad_main + inner_main - after_total;
        if (cursor_b < cursor_a) cursor_b = cursor_a;
        for (int k = auto_margin_k; k < n; k++) {
            LunaElement* ch = &elements[kids[k]];
            int align = child_cross_align(ch, cont, row_mode);
            float cross_len = resolve_flex_cross_len(ch, row_mode, align, inner_cross);
            if (row_mode) {
                if (!(ch->css_positioned & 1)) { ch->w = main_sz[k]; ch->rel_x = cursor_b; }
                place_flex_cross(ch, cont, 1, pad_cross, inner_cross, cross_len, align);
                if (!(ch->css_positioned & 1))
                    cursor_b += ch->margin_left + main_sz[k] + ch->margin_right + use_gap;
            } else {
                if (!(ch->css_positioned & 2)) { ch->h = main_sz[k]; ch->rel_y = cursor_b; }
                place_flex_cross(ch, cont, 0, pad_cross, inner_cross, cross_len, align);
                if (!(ch->css_positioned & 2))
                    cursor_b += ch->margin_top + main_sz[k] + ch->margin_bottom + use_gap;
            }
        }
        return; /* skip normal placement */
    }

    float cursor = start;
    for (int k = 0; k < n; k++) {
        LunaElement* ch = &elements[kids[k]];
        int align = child_cross_align(ch, cont, row_mode);
        float cross_len = resolve_flex_cross_len(ch, row_mode, align, inner_cross);
        if (row_mode) {
            if (!(ch->css_positioned & 1)) { ch->w = main_sz[k]; ch->rel_x = cursor; }
            place_flex_cross(ch, cont, 1, pad_cross, inner_cross, cross_len, align);
            if (!(ch->css_positioned & 1))
                cursor += ch->margin_left + main_sz[k] + ch->margin_right + use_gap;
        } else {
            if (!(ch->css_positioned & 2)) { ch->h = main_sz[k]; ch->rel_y = cursor; }
            place_flex_cross(ch, cont, 0, pad_cross, inner_cross, cross_len, align);
            if (!(ch->css_positioned & 2))
                cursor += ch->margin_top + main_sz[k] + ch->margin_bottom + use_gap;
        }
    }
}

/* The parser stores direct text after an inline child on the parent.  In a
 * row flex box that text is an anonymous final flex item.  Resolve it without
 * allocating a synthetic element, preserving the renderer's hot path. */
static void layout_inline_text_after_flex(LunaElement* cont, const int* kids, int n) {
    cont->has_inline_text_flow = 0;
    cont->inline_text_x = 0.0f;
    if (cont->flex_direction != FLEX_DIR_ROW || !cont->text[0] || n <= 0) return;

    FontAtlas* atlas = get_atlas(cont->font_size, cont->font_bold, NULL);
    if (font_loaded)
        text_metrics_begin(cont->font_size, cont->font_bold, cont->font_face, atlas);
    g_text_letter_spacing = cont->letter_spacing;
    float text_w = font_loaded ? measure_text_width(atlas, cont->text)
                              : strlen(cont->text) * cont->font_size * 0.55f;
    g_text_letter_spacing = 0.0f;
    if (font_loaded) text_metrics_end();
    if (text_w <= 0.0f) return;

    float shift = 0.0f;
    if (cont->justify_content == FLEX_JUSTIFY_CENTER) shift = -text_w * 0.5f;
    else if (cont->justify_content == FLEX_JUSTIFY_END) shift = -text_w;
    if (shift != 0.0f) {
        for (int k = 0; k < n; k++)
            if (!(elements[kids[k]].css_positioned & 1)) elements[kids[k]].rel_x += shift;
    }

    float right = cont->pad_l;
    for (int k = 0; k < n; k++) {
        LunaElement* ch = &elements[kids[k]];
        if (ch->css_positioned & 1) continue;
        float edge = ch->rel_x + ch->w + ch->margin_right;
        if (edge > right) right = edge;
    }
    cont->inline_text_x = right;
    cont->has_inline_text_flow = 1;
}

static void layout_flex_container(int container_idx) {
    LunaElement* cont = &elements[container_idx];
    int kids[MAX_ELEMENTS];
    int n = 0;
    for (int c = 0; c < elem_count; c++) {
        if (elements[c].parent_idx != container_idx) continue;
        if (!is_visible(c) || elements[c].position_fixed) continue;
        // Absolutely positioned children are out of flow — don't include in flex layout.
        if (elements[c].position_mode == POS_ABSOLUTE) continue;
        elements[c].flex_child = 1;
        kids[n++] = c;
    }
    if (n == 0) return;


    int row_mode = (cont->flex_direction == FLEX_DIR_ROW);
    float gap = cont->flex_gap;
    float inner_w = cont->w - cont->pad_l - cont->pad_r - cont->border_width * 2.0f;
    float inner_h = cont->h - cont->pad_t - cont->pad_b - cont->border_width * 2.0f;
    if (inner_w < 0.0f) inner_w = 0.0f;
    if (inner_h < 0.0f) inner_h = 0.0f;
    float inner_main = row_mode ? inner_w : inner_h;
    float inner_cross = row_mode ? inner_h : inner_w;
    /* Main axis runs along flex-direction; cross axis is perpendicular. */
    float pad_main  = cont->border_width + (row_mode ? cont->pad_l : cont->pad_t);
    float pad_cross = cont->border_width + (row_mode ? cont->pad_t : cont->pad_l);
    float pad_cross_end = row_mode ? cont->pad_b : cont->pad_r;

    if (cont->flex_wrap == FLEX_WRAP_NOWRAP || !row_mode) {
        layout_flex_line(cont, kids, n, row_mode, pad_main, pad_cross, inner_main, inner_cross, gap);
        layout_inline_text_after_flex(cont, kids, n);
        return;
    }

    typedef struct { int start, count; float cross_sz; } FlexLineInfo;
    FlexLineInfo lines[64];
    int num_lines = 0;

    int line_start = 0;
    float line_main = 0.0f;
    for (int k = 0; k <= n; k++) {
        int flush = 0;
        if (k < n) {
            LunaElement* ch = &elements[kids[k]];
            float item_main = flex_main_size(ch, 1, inner_main) +
                              (line_main > 0.0f ? gap : 0.0f);
            if (line_main > 0.0f && line_main + item_main > inner_main + 0.5f)
                flush = 1;
            else
                line_main += item_main;
        } else {
            flush = 1;
        }
        if (!flush) continue;

        int line_n = k - line_start;
        if (line_n > 0 && num_lines < 64) {
            float line_cross = 0.0f;
            for (int li = 0; li < line_n; li++) {
                LunaElement* ch = &elements[kids[line_start + li]];
                /* Hypothetical cross size before stretch — prefer definite
                 * height so wrap lines don't inherit a previous stretch. */
                float ch_cross;
                if (ch->has_css_height && !ch->pct_h)
                    ch_cross = css_outer_height(ch, ch->css_height);
                else if (ch->has_min_height)
                    ch_cross = css_outer_height(ch, ch->css_min_height);
                else
                    ch_cross = flow_content_height(ch);
                if (ch->has_min_height) {
                    float mh = css_outer_height(ch, ch->css_min_height);
                    if (ch_cross < mh) ch_cross = mh;
                }
                ch_cross += ch->margin_top + ch->margin_bottom;
                if (ch_cross > line_cross) line_cross = ch_cross;
            }
            lines[num_lines].start = line_start;
            lines[num_lines].count = line_n;
            lines[num_lines].cross_sz = line_cross;
            num_lines++;
        }
        line_start = k;
        line_main = (k < n) ? flex_main_size(&elements[kids[k]], 1, inner_main) : 0.0f;
    }

    float total_cross = 0.0f;
    for (int i = 0; i < num_lines; i++) {
        total_cross += lines[i].cross_sz;
        if (i > 0) total_cross += gap;
    }
    float cross_free = inner_cross - total_cross;
    if (cross_free < 0.0f) cross_free = 0.0f;

    float cross_start = pad_cross;
    float cross_gap = gap;
    if (cont->align_content == FLEX_ALIGN_CENTER)
        cross_start = pad_cross + cross_free * 0.5f;
    else if (cont->align_content == FLEX_ALIGN_END)
        cross_start = pad_cross + cross_free;
    else if (cont->align_content == FLEX_ALIGN_SPACE_BETWEEN && num_lines > 1) {
        float content = 0.0f;
        for (int i = 0; i < num_lines; i++) content += lines[i].cross_sz;
        cross_gap = (inner_cross - content) / (float)(num_lines - 1);
        if (cross_gap < 0.0f) cross_gap = 0.0f;
        cross_start = pad_cross;
    } else if (cont->align_content == FLEX_ALIGN_SPACE_AROUND && num_lines > 0) {
        float content = 0.0f;
        for (int i = 0; i < num_lines; i++) content += lines[i].cross_sz;
        float slack = inner_cross - content;
        if (slack < 0.0f) slack = 0.0f;
        cross_start = pad_cross + slack / (float)(num_lines * 2);
        cross_gap = gap + slack / (float)num_lines;
    } else if (cont->align_content == FLEX_ALIGN_STRETCH && num_lines > 0) {
        float extra = cross_free / (float)num_lines;
        for (int i = 0; i < num_lines; i++)
            lines[i].cross_sz += extra;
    }

    float cross_cursor = cross_start;
    for (int i = 0; i < num_lines; i++) {
        int line_kids[MAX_ELEMENTS];
        for (int li = 0; li < lines[i].count; li++)
            line_kids[li] = kids[lines[i].start + li];
        /* Stretch / align against THIS line's cross size, not the container. */
        float line_cross = lines[i].cross_sz;
        layout_flex_line(cont, line_kids, lines[i].count, 1, pad_main, pad_cross,
                         inner_main, line_cross, gap);
        if (cont->align_content == FLEX_ALIGN_STRETCH) {
            for (int li = 0; li < lines[i].count; li++) {
                LunaElement* ch = &elements[line_kids[li]];
                if (!(ch->css_positioned & 2) && !ch->has_css_height && !ch->pct_h)
                    ch->h = line_cross;
            }
        }
        for (int li = 0; li < lines[i].count; li++) {
            LunaElement* ch = &elements[line_kids[li]];
            if (!(ch->css_positioned & 2))
                ch->rel_y += cross_cursor - pad_cross;
        }
        cross_cursor += lines[i].cross_sz;
        if (i < num_lines - 1) cross_cursor += cross_gap;
    }

    if (cont->flex_wrap == FLEX_WRAP_WRAP && row_mode && !cont->has_css_height && !cont->pct_h) {
        float needed_h = cross_cursor + pad_cross_end;
        if (cont->has_min_height && needed_h < cont->css_min_height)
            needed_h = cont->css_min_height;
        if (cont->has_max_height && !cont->max_height_pct &&
            needed_h > cont->css_max_height)
            needed_h = cont->css_max_height;
        cont->h = needed_h;
    }
    layout_inline_text_after_flex(cont, kids, n);
}

static void resolve_grid_tracks(float inner, int count, const float* track, const int* types,
                                const float* mins, float gap, float* out_sizes) {
    float fixed = 0.0f, fr_sum = 0.0f;
    for (int i = 0; i < count; i++) {
        if (types[i] == GRID_TRACK_FR || types[i] == GRID_TRACK_MINMAX)
            fr_sum += track[i];
        else
            fixed += track[i];
    }
    fixed += gap * (float)(count > 0 ? count - 1 : 0);
    float fr_unit = (fr_sum > 0.0f) ? (inner - fixed) / fr_sum : 0.0f;
    if (fr_unit < 0.0f) fr_unit = 0.0f;
    for (int i = 0; i < count; i++) {
        if (types[i] == GRID_TRACK_FR)
            out_sizes[i] = track[i] * fr_unit;
        else if (types[i] == GRID_TRACK_MINMAX) {
            float sz = track[i] * fr_unit;
            if (sz < mins[i]) sz = mins[i];
            out_sizes[i] = sz;
        } else
            out_sizes[i] = track[i];
    }
}

static void grid_axis_align(float inner, int count, float* sizes, float gap, int mode,
                            float* offset, float* out_gap) {
    float total = 0.0f;
    for (int i = 0; i < count; i++) total += sizes[i];
    if (count > 1) total += gap * (float)(count - 1);
    float slack = inner - total;
    if (slack < 0.0f) slack = 0.0f;
    *offset = 0.0f;
    *out_gap = gap;
    if (mode == FLEX_ALIGN_CENTER)
        *offset = slack * 0.5f;
    else if (mode == FLEX_ALIGN_END)
        *offset = slack;
    else if (mode == FLEX_ALIGN_STRETCH && count > 0 && slack > 0.0f) {
        float extra = slack / (float)count;
        for (int i = 0; i < count; i++) sizes[i] += extra;
    } else if (mode == FLEX_ALIGN_SPACE_BETWEEN && count > 1) {
        float content = 0.0f;
        for (int i = 0; i < count; i++) content += sizes[i];
        *out_gap = (inner - content) / (float)(count - 1);
        if (*out_gap < 0.0f) *out_gap = 0.0f;
    } else if (mode == FLEX_ALIGN_SPACE_AROUND && count > 0) {
        float content = 0.0f;
        for (int i = 0; i < count; i++) content += sizes[i];
        float s = inner - content;
        if (s < 0.0f) s = 0.0f;
        *offset = s / (float)(count * 2);
        *out_gap = gap + s / (float)count;
    }
}

static int grid_can_place(int occ[MAX_GRID_AREA_ROWS][MAX_GRID_AREA_COLS],
                          int rows, int cols, int r, int c, int rs, int cs) {
    if (r < 0 || c < 0 || r + rs > rows || c + cs > cols) return 0;
    for (int rr = r; rr < r + rs; rr++)
        for (int cc = c; cc < c + cs; cc++)
            if (occ[rr][cc]) return 0;
    return 1;
}

static void grid_mark_cells(int occ[MAX_GRID_AREA_ROWS][MAX_GRID_AREA_COLS],
                            int r, int c, int rs, int cs, int val) {
    for (int rr = r; rr < r + rs && rr < MAX_GRID_AREA_ROWS; rr++)
        for (int cc = c; cc < c + cs && cc < MAX_GRID_AREA_COLS; cc++)
            occ[rr][cc] = val;
}

static int grid_find_auto_slot(int occ[MAX_GRID_AREA_ROWS][MAX_GRID_AREA_COLS],
                               int rows, int cols, int rs, int cs,
                               int col_flow, int dense, int* out_r, int* out_c) {
    (void)dense;
    if (col_flow) {
        for (int cc = 0; cc < cols; cc++)
            for (int rr = 0; rr < rows; rr++) {
                if (grid_can_place(occ, rows, cols, rr, cc, rs, cs)) {
                    *out_r = rr; *out_c = cc; return 1;
                }
            }
    } else {
        for (int rr = 0; rr < rows; rr++)
            for (int cc = 0; cc < cols; cc++) {
                if (grid_can_place(occ, rows, cols, rr, cc, rs, cs)) {
                    *out_r = rr; *out_c = cc; return 1;
                }
            }
    }
    return 0;
}

static void grid_advance_cursor(int* ac, int* ar, int rs, int cs,
                                int rows, int cols, int col_flow,
                                int occ[MAX_GRID_AREA_ROWS][MAX_GRID_AREA_COLS]) {
    for (int attempt = 0; attempt < rows * cols; attempt++) {
        if (grid_can_place(occ, rows, cols, *ar, *ac, rs, cs)) return;
        if (col_flow) {
            *ar += rs;
            if (*ar + rs > rows) { *ar = 0; *ac += cs; }
        } else {
            *ac += cs;
            if (*ac + cs > cols) { *ac = 0; *ar += rs; }
        }
        if (*ac >= cols) *ac = 0;
        if (*ar >= rows) *ar = 0;
    }
}

static void place_grid_item(LunaElement* ch, LunaElement* cont,
                            float cx, float cy, float cw, float chh) {
    int jalign = (ch->justify_self >= 0) ? ch->justify_self : cont->justify_items;
    int aalign = (ch->align_self >= 0) ? ch->align_self : cont->align_items;
    float item_w = ch->w;
    float item_h = ch->h;
    if (jalign == FLEX_ALIGN_STRETCH && !(ch->css_positioned & 1)) item_w = cw;
    if (aalign == FLEX_ALIGN_STRETCH && !(ch->css_positioned & 2)) item_h = chh;
    float off_x = 0.0f, off_y = 0.0f;
    if (jalign == FLEX_ALIGN_CENTER) off_x = (cw - item_w) * 0.5f;
    else if (jalign == FLEX_ALIGN_END) off_x = cw - item_w;
    if (aalign == FLEX_ALIGN_CENTER) off_y = (chh - item_h) * 0.5f;
    else if (aalign == FLEX_ALIGN_END) off_y = chh - item_h;
    if (off_x < 0.0f) off_x = 0.0f;
    if (off_y < 0.0f) off_y = 0.0f;
    if (!(ch->css_positioned & 1)) { ch->rel_x = cx + off_x; ch->w = item_w; }
    if (!(ch->css_positioned & 2)) { ch->rel_y = cy + off_y; ch->h = item_h; }
}

typedef struct {
    int idx, gc, gr, cspan, rspan;
} GridPlace;

static void layout_grid_container(int container_idx) {
    LunaElement* cont = &elements[container_idx];
    if (cont->grid_area_rect_count == 0 && cont->grid_area_rows > 0)
        compile_grid_area_rects(cont);

    int tmpl_cols = cont->grid_col_count;
    int tmpl_rows = cont->grid_row_count;
    if (tmpl_cols < 1) tmpl_cols = cont->has_grid_auto_columns ? 0 : 1;
    if (tmpl_rows < 1) tmpl_rows = cont->has_grid_auto_rows ? 0 : 1;

    float pad_l = cont->border_width + cont->pad_l;
    float pad_t = cont->border_width + cont->pad_t;
    float pad_r = cont->border_width + cont->pad_r;
    float pad_b = cont->border_width + cont->pad_b;
    float col_gap = cont->grid_col_gap > 0.0f ? cont->grid_col_gap : cont->flex_gap;
    float row_gap = cont->grid_row_gap > 0.0f ? cont->grid_row_gap : cont->flex_gap;

    int col_flow = (cont->grid_auto_flow & GRID_AUTO_FLOW_COLUMN) != 0;
    int dense = (cont->grid_auto_flow & GRID_AUTO_FLOW_DENSE) != 0;
    int occupied[MAX_GRID_AREA_ROWS][MAX_GRID_AREA_COLS];
    memset(occupied, 0, sizeof(occupied));

    GridPlace places[MAX_ELEMENTS];
    int place_n = 0;
    int auto_col = 0, auto_row = 0;
    int max_col_end = tmpl_cols > 0 ? tmpl_cols : 1;
    int max_row_end = tmpl_rows > 0 ? tmpl_rows : 1;

    for (int c = 0; c < elem_count; c++) {
        if (elements[c].parent_idx != container_idx) continue;
        if (!is_visible(c) || elements[c].position_fixed) continue;
        if (elements[c].position_mode == POS_ABSOLUTE) continue;
        LunaElement* ch = &elements[c];
        ch->grid_child = 1;

        int cspan = ch->grid_col_span > 0 ? ch->grid_col_span : 1;
        int rspan = ch->grid_row_span > 0 ? ch->grid_row_span : 1;
        int gc = ch->has_grid_col ? ch->grid_col : -1;
        int gr = ch->has_grid_row ? ch->grid_row : -1;
        if (ch->has_grid_area && ch->grid_area_name[0])
            grid_area_lookup(cont, ch->grid_area_name, &gc, &gr, &cspan, &rspan);

        int auto_place = !ch->has_grid_col && !ch->has_grid_row && !ch->has_grid_area;
        /* Normal-flow grid auto-placement grows implicit tracks.  Without
         * this, a one-column grid kept wrapping its cursor back to row zero,
         * so every About spec card occupied the same cell. */
        if (auto_place && !dense) {
            if (col_flow) {
                if (auto_col >= max_col_end && max_col_end < MAX_GRID_AREA_COLS)
                    max_col_end = auto_col + cspan;
            } else {
                if (auto_row >= max_row_end && max_row_end < MAX_GRID_AREA_ROWS)
                    max_row_end = auto_row + rspan;
            }
        }
        int work_cols = max_col_end > 0 ? max_col_end : MAX_GRID_AREA_COLS;
        int work_rows = max_row_end > 0 ? max_row_end : MAX_GRID_AREA_ROWS;
        if (work_cols > MAX_GRID_AREA_COLS) work_cols = MAX_GRID_AREA_COLS;
        if (work_rows > MAX_GRID_AREA_ROWS) work_rows = MAX_GRID_AREA_ROWS;

        if (auto_place) {
            if (dense) {
                int found = 0;
                for (int attempt = 0; attempt < 32 && !found; attempt++) {
                    work_cols = max_col_end > 0 ? max_col_end : 1;
                    work_rows = max_row_end > 0 ? max_row_end : 1;
                    if (work_cols > MAX_GRID_AREA_COLS) work_cols = MAX_GRID_AREA_COLS;
                    if (work_rows > MAX_GRID_AREA_ROWS) work_rows = MAX_GRID_AREA_ROWS;
                    if (grid_find_auto_slot(occupied, work_rows, work_cols, rspan, cspan, col_flow, 1, &gr, &gc)) {
                        found = 1;
                        break;
                    }
                    if (col_flow) {
                        max_row_end++;
                        if (max_row_end > MAX_GRID_AREA_ROWS) break;
                    } else {
                        max_col_end++;
                        if (max_col_end > MAX_GRID_AREA_COLS) break;
                    }
                }
                if (!found) continue;
            } else {
                grid_advance_cursor(&auto_col, &auto_row, rspan, cspan, work_rows, work_cols, col_flow, occupied);
                gc = auto_col;
                gr = auto_row;
            }
        }

        if (gc < 0) gc = 0;
        if (gr < 0) gr = 0;

        if (auto_place && !dense) {
            if (col_flow) {
                auto_row = gr + rspan;
                if (auto_row + rspan > work_rows) { auto_row = 0; auto_col = gc + cspan; }
            } else {
                auto_col = gc + cspan;
                if (auto_col + cspan > work_cols) { auto_col = 0; auto_row = gr + rspan; }
            }
        }

        int col_end = gc + cspan;
        int row_end = gr + rspan;
        if (col_end > max_col_end) max_col_end = col_end;
        if (row_end > max_row_end) max_row_end = row_end;

        int pc = col_end > MAX_GRID_AREA_COLS ? MAX_GRID_AREA_COLS : col_end;
        int pr = row_end > MAX_GRID_AREA_ROWS ? MAX_GRID_AREA_ROWS : row_end;
        grid_mark_cells(occupied, gr, gc, rspan, cspan, 1);

        if (place_n < MAX_ELEMENTS) {
            places[place_n].idx = c;
            places[place_n].gc = gc;
            places[place_n].gr = gr;
            places[place_n].cspan = cspan;
            places[place_n].rspan = rspan;
            place_n++;
        }
        (void)pc; (void)pr;
    }

    int cols = max_col_end;
    int rows = max_row_end;
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    if (cols > MAX_GRID_AREA_COLS) cols = MAX_GRID_AREA_COLS;
    if (rows > MAX_GRID_AREA_ROWS) rows = MAX_GRID_AREA_ROWS;

    float inner_w = cont->w - pad_l - pad_r;
    float inner_h = cont->h - pad_t - pad_b;
    if (inner_w < 0.0f) inner_w = 0.0f;
    if (inner_h < 0.0f) inner_h = 0.0f;

    float col_sz[MAX_GRID_TRACKS], row_sz[MAX_GRID_TRACKS];
    float col_tr[MAX_GRID_TRACKS], row_tr[MAX_GRID_TRACKS];
    int col_ty[MAX_GRID_TRACKS], row_ty[MAX_GRID_TRACKS];
    float col_mn[MAX_GRID_TRACKS], row_mn[MAX_GRID_TRACKS];

    for (int i = 0; i < cols; i++) {
        if (i < tmpl_cols && tmpl_cols > 0) {
            col_tr[i] = cont->grid_col_track[i];
            col_ty[i] = cont->grid_col_type[i];
            col_mn[i] = cont->grid_col_min[i];
        } else {
            col_tr[i] = cont->has_grid_auto_columns ? cont->grid_auto_col_track : 1.0f;
            col_ty[i] = cont->has_grid_auto_columns ? cont->grid_auto_col_type : GRID_TRACK_FR;
            col_mn[i] = cont->has_grid_auto_columns ? cont->grid_auto_col_min : 0.0f;
        }
        if (col_ty[i] == GRID_TRACK_PX && col_tr[i] <= 0.0f) col_tr[i] = inner_w / (float)cols;
    }
    for (int i = 0; i < rows; i++) {
        if (i < tmpl_rows && tmpl_rows > 0) {
            row_tr[i] = cont->grid_row_track[i];
            row_ty[i] = cont->grid_row_type[i];
            row_mn[i] = cont->grid_row_min[i];
        } else {
            row_tr[i] = cont->has_grid_auto_rows ? cont->grid_auto_row_track : 1.0f;
            row_ty[i] = cont->has_grid_auto_rows ? cont->grid_auto_row_type : GRID_TRACK_FR;
            row_mn[i] = cont->has_grid_auto_rows ? cont->grid_auto_row_min : 0.0f;
        }
        if (row_ty[i] == GRID_TRACK_PX && row_tr[i] <= 0.0f) row_tr[i] = inner_h / (float)rows;
    }

    resolve_grid_tracks(inner_w, cols, col_tr, col_ty, col_mn, col_gap, col_sz);
    resolve_grid_tracks(inner_h, rows, row_tr, row_ty, row_mn, row_gap, row_sz);

    float row_off = 0.0f, col_off = 0.0f;
    float use_row_gap = row_gap, use_col_gap = col_gap;
    grid_axis_align(inner_h, rows, row_sz, row_gap, cont->align_content, &row_off, &use_row_gap);
    grid_axis_align(inner_w, cols, col_sz, col_gap, cont->justify_content, &col_off, &use_col_gap);

    for (int pi = 0; pi < place_n; pi++) {
        GridPlace* pl = &places[pi];
        LunaElement* ch = &elements[pl->idx];
        int gc = pl->gc, gr = pl->gr;
        int cspan = pl->cspan, rspan = pl->rspan;
        if (gc + cspan > cols) gc = cols - cspan;
        if (gr + rspan > rows) gr = rows - rspan;
        if (gc < 0) gc = 0;
        if (gr < 0) gr = 0;

        float cx = pad_l + col_off, cy = pad_t + row_off, cw = 0.0f, chh = 0.0f;
        for (int i = 0; i < gc; i++) cx += col_sz[i] + use_col_gap;
        for (int i = 0; i < gr; i++) cy += row_sz[i] + use_row_gap;
        for (int i = gc; i < gc + cspan && i < cols; i++) cw += col_sz[i];
        for (int i = gr; i < gr + rspan && i < rows; i++) chh += row_sz[i];
        cw += use_col_gap * (float)(cspan - 1);
        chh += use_row_gap * (float)(rspan - 1);

        place_grid_item(ch, cont, cx, cy, cw, chh);
    }
}

static void layout_flex_containers(void) {
    for (int i = 0; i < elem_count; i++) {
        elements[i].flex_child = 0;
        elements[i].grid_child = 0;
        elements[i].flow_child = 0;
    }
    /* Single top-down pass in document order (parent index < child index),
       so a container's own width/height is finalized by its parent before its
       children are sized. Dispatch each container by display mode; mixing flex
       and block containers in one ordered pass ensures width propagates
       parent -> child (e.g. content(block) -> tab_panel(flex) -> toolbar). */
    for (int i = 0; i < elem_count; i++) {
        if (!is_visible(i)) continue;
        if (elements[i].display_mode == DISPLAY_FLEX)
            layout_flex_container(i);
        else if (elements[i].display_mode == DISPLAY_GRID)
            layout_grid_container(i);
        else
            layout_block_container(i);
    }
    for (int i = 0; i < elem_count; i++) {
        LunaElement* e = &elements[i];
        int par = e->parent_idx;
        if (par == -1 || e->position_fixed) continue;
        if (!e->flex_child && !e->grid_child && !e->flow_child) continue;
        e->x = elements[par].x + e->rel_x + e->margin_left;
        e->y = elements[par].y + e->rel_y + e->margin_top;
    }
}

/* CSS relative positioning keeps the element's normal-flow footprint, then
 * shifts its painted box (and its descendants) by the declared inset.  Doing
 * this after block/flex/grid placement is what distinguishes it from absolute
 * positioning: siblings retain the original space and alignment. */
static float relative_inset(const LunaElement* e, int horizontal) {
    int p = e->parent_idx;
    float span = horizontal ? window_width : window_height;
    if (p != -1) {
        const LunaElement* parent = &elements[p];
        span = horizontal
            ? parent->w - parent->pad_l - parent->pad_r - parent->border_width * 2.0f
            : parent->h - parent->pad_t - parent->pad_b - parent->border_width * 2.0f;
        if (span < 0.0f) span = 0.0f;
    }
    if (horizontal) {
        if (e->has_left && !e->pos_overridden_x)
            return e->pct_left ? span * e->raw_left + e->raw_left_off : e->raw_left;
        if (e->has_right && !e->pos_overridden_x)
            return -(e->pct_right ? span * e->raw_right + e->raw_right_off : e->right_val);
    } else {
        if (e->has_top && !e->pos_overridden_y)
            return e->pct_top ? span * e->raw_top + e->raw_top_off : e->raw_top;
        if (e->has_bottom && !e->pos_overridden_y)
            return -(e->pct_bottom ? span * e->raw_bottom + e->raw_bottom_off : e->bottom_val);
    }
    return 0.0f;
}

static int relative_is_descendant_of(int idx, int ancestor) {
    for (int p = idx; p != -1; p = elements[p].parent_idx)
        if (p == ancestor) return 1;
    return 0;
}

static void apply_relative_offsets(void) {
    for (int i = 0; i < elem_count; i++) {
        LunaElement* e = &elements[i];
        if (!is_visible(i) || e->position_mode != POS_RELATIVE) continue;
        float dx = relative_inset(e, 1);
        float dy = relative_inset(e, 0);
        if (fabsf(dx) < 0.001f && fabsf(dy) < 0.001f) continue;
        for (int j = i; j < elem_count; j++) {
            if (elements[j].position_fixed || !relative_is_descendant_of(j, i)) continue;
            elements[j].x += dx;
            elements[j].y += dy;
        }
    }
}

/* Absolute flex dialogs commonly use height:auto.  Their content size is only
 * known after the first flex pass; resolve it here and run one settling pass so
 * the footer stays inside the dialog instead of being clipped by overflow. */
static int size_auto_positioned_containers(void) {
    int changed = 0;
    for (int i = 0; i < elem_count; i++) {
        LunaElement* e = &elements[i];
        if (!is_visible(i) || e->has_css_height || e->pct_h) continue;
        if (e->position_mode != POS_ABSOLUTE && !e->position_fixed) continue;
        if (e->has_top && e->has_bottom) continue; /* inset stretches these */
        float h = flow_content_height(e);
        if (e->has_min_height && h < e->css_min_height) h = e->css_min_height;
        if (e->has_max_height) {
            float basis = window_height;
            if (!e->position_fixed && e->parent_idx >= 0) {
                LunaElement* p = &elements[e->parent_idx];
                basis = p->h - p->pad_t - p->pad_b - p->border_width * 2.0f;
                if (basis < 0.0f) basis = 0.0f;
            }
            float max_h = resolved_max_height(e, basis);
            if (h > max_h) h = max_h;
        }
        if (fabsf(e->h - h) > 0.5f) {
            e->h = h;
            changed = 1;
        }
    }
    return changed;
}

static void apply_scroll_offsets(void);
static void apply_scroll_metrics(void);
static void apply_sticky_positions(void);
static void sync_css_overlay_elements(void);

void update_layout_pass(void) {
    memset(g_intrinsic_width_valid, 0, sizeof(g_intrinsic_width_valid));
    update_layout();
    layout_flex_containers();
    /* Wrapping flex rows finalize their height after a definite width is known.
     * A second pass lets column parents (Appearance panel) re-pack siblings
     * against that height instead of leaving wallpaper/cursor sections overlapped. */
    layout_flex_containers();
    if (size_auto_positioned_containers()) {
        memset(g_intrinsic_width_valid, 0, sizeof(g_intrinsic_width_valid));
        update_layout();
        layout_flex_containers();
        layout_flex_containers();
    }
    apply_relative_offsets();
    apply_scroll_metrics();
    apply_scroll_offsets();
    apply_sticky_positions();
    /* Resolve percentage-based transforms after element sizes are known */
    for (int i = 0; i < elem_count; i++) {
        LunaElement* e = &elements[i];
        if (e->transform_tx_pct) e->transform_tx = e->raw_transform_tx * e->w;
        if (e->transform_ty_pct) e->transform_ty = e->raw_transform_ty * e->h;
    }
    sync_css_overlay_elements();
}

static void apply_sticky_positions(void) {
    for (int i = 0; i < elem_count; i++) {
        LunaElement* e = &elements[i];
        if (!e->position_sticky || !is_visible(i)) continue;

        int scroll_y = -1, scroll_x = -1;
        for (int p = e->parent_idx; p != -1; p = elements[p].parent_idx) {
            if (scroll_y == -1 && overflow_scrollable(elements[p].overflow_y))
                scroll_y = p;
            if (scroll_x == -1 && overflow_scrollable(elements[p].overflow_x))
                scroll_x = p;
        }

        if (scroll_y != -1) {
            LunaElement* par = &elements[scroll_y];
            float inner_top = par->y + par->border_width + par->pad_t;
            float inner_bottom = par->y + par->h - par->border_width - par->pad_b;
            if (e->sticky_use_bottom) {
                float stick_y = inner_bottom - e->h - e->sticky_bottom;
                if (e->y > stick_y) e->y = stick_y;
            } else if (e->sticky_use_top) {
                float min_y = inner_top + e->sticky_top;
                float max_y = inner_bottom - e->h;
                if (max_y < min_y) max_y = min_y;
                if (e->y < min_y) e->y = min_y;
                if (e->y > max_y) e->y = max_y;
            }
        }

        if (scroll_x != -1) {
            LunaElement* par = &elements[scroll_x];
            float inner_left = par->x + par->border_width + par->pad_l;
            if (e->sticky_use_left) {
                float min_x = inner_left + e->sticky_left;
                if (e->x < min_x) e->x = min_x;
            } else if (e->sticky_use_right) {
                float inner_right = par->x + par->w - par->border_width - par->pad_r;
                float stick_x = inner_right - e->w - e->sticky_right;
                if (e->x > stick_x) e->x = stick_x;
            }
        }
    }
}

static float scroll_offset_x(int idx) {
    float s = 0.0f;
    int p = elements[idx].parent_idx;
    while (p != -1) {
        if (overflow_scrollable(elements[p].overflow_x))
            s += elements[p].scroll_left;
        p = elements[p].parent_idx;
    }
    return s;
}

static float scroll_offset_y(int idx) {
    float s = 0.0f;
    int p = elements[idx].parent_idx;
    while (p != -1) {
        if (overflow_scrollable(elements[p].overflow_y))
            s += elements[p].scroll_top;
        p = elements[p].parent_idx;
    }
    return s;
}

static void apply_scroll_offsets(void) {
    for (int i = 0; i < elem_count; i++) {
        if (elements[i].position_fixed || elements[i].parent_idx == -1) continue;
        elements[i].x -= scroll_offset_x(i);
        elements[i].y -= scroll_offset_y(i);
    }
}

static void apply_scroll_metrics(void) {
    for (int i = 0; i < elem_count; i++) {
        LunaElement* c = &elements[i];
        if (!overflow_scrollable(c->overflow_y) && !overflow_scrollable(c->overflow_x)) continue;
        float inner_h = c->h - c->pad_t - c->pad_b;
        float inner_w = c->w - c->pad_l - c->pad_r;
        if (inner_h < 0.0f) inner_h = 0.0f;
        if (inner_w < 0.0f) inner_w = 0.0f;
        float content_bottom = 0.0f, content_right = 0.0f;
        for (int ch = 0; ch < elem_count; ch++) {
            if (elements[ch].parent_idx != i) continue;
            if (!is_visible(ch)) continue;
            float bottom = elements[ch].rel_y + elements[ch].h;
            float right  = elements[ch].rel_x + elements[ch].w;
            if (bottom > content_bottom) content_bottom = bottom;
            if (right > content_right) content_right = right;
        }
        c->scroll_content_h = content_bottom;
        c->scroll_content_w = content_right;
        float max_scroll_y = content_bottom - inner_h;
        float max_scroll_x = content_right - inner_w;
        if (max_scroll_y < 0.0f) max_scroll_y = 0.0f;
        if (max_scroll_x < 0.0f) max_scroll_x = 0.0f;
        if (c->scroll_top > max_scroll_y) { c->scroll_top = max_scroll_y; c->scroll_dest_top = max_scroll_y; }
        if (c->scroll_left > max_scroll_x) { c->scroll_left = max_scroll_x; c->scroll_dest_left = max_scroll_x; }
        if (c->scroll_top < 0.0f) { c->scroll_top = 0.0f; c->scroll_dest_top = 0.0f; }
        if (c->scroll_left < 0.0f) { c->scroll_left = 0.0f; c->scroll_dest_left = 0.0f; }
    }
}

static int overflow_scrolls_y(int idx) {
    return overflow_scrollable(elements[idx].overflow_y);
}

static int overflow_scrolls_x(int idx) {
    return overflow_scrollable(elements[idx].overflow_x);
}

static int find_scroll_target_y(int idx) {
    while (idx != -1) {
        if (overflow_scrolls_y(idx)) return idx;
        idx = elements[idx].parent_idx;
    }
    return -1;
}

static int find_scroll_target_x(int idx) {
    while (idx != -1) {
        if (overflow_scrolls_x(idx)) return idx;
        idx = elements[idx].parent_idx;
    }
    return -1;
}

static void scrollbar_geom_y(LunaElement* c, float* tx, float* ty, float* tw, float* th,
                             float* ux, float* uy, float* uw, float* uh, int* visible) {
    *visible = 0;
    float inner_h = c->h - c->pad_t - c->pad_b;
    if (!overflow_scrollable(c->overflow_y) || inner_h <= 0.0f) return;
    int needs_bar = (c->scroll_content_h > inner_h + 1.0f) || c->overflow_y == OVERFLOW_SCROLL;
    if (!needs_bar) return;
    float sbw = element_sb_width(c);
    if (sbw <= 0.0f) return;
    *visible = 1;
    *tw = sbw;
    *th = inner_h;
    *tx = c->x + c->w - c->pad_r - *tw - 2.0f;
    *ty = c->y + c->pad_t;
    float ratio = inner_h / c->scroll_content_h;
    *uh = *th * ratio;
    if (*uh < 14.0f) *uh = 14.0f;
    float max_scroll = c->scroll_content_h - inner_h;
    if (max_scroll < 0.0f) max_scroll = 0.0f;
    float scroll_range = *th - *uh;
    *uy = *ty + (max_scroll > 0.0f && scroll_range > 0.0f
                  ? (c->scroll_top / max_scroll) * scroll_range : 0.0f);
    *ux = *tx;
    *uw = *tw;
}

static void scrollbar_geom_x(LunaElement* c, float* tx, float* ty, float* tw, float* th,
                             float* ux, float* uy, float* uw, float* uh, int* visible) {
    *visible = 0;
    float inner_w = c->w - c->pad_l - c->pad_r;
    if (!overflow_scrollable(c->overflow_x) || inner_w <= 0.0f) return;
    int needs_bar = (c->scroll_content_w > inner_w + 1.0f) || c->overflow_x == OVERFLOW_SCROLL;
    if (!needs_bar) return;
    float sbw = element_sb_width(c);
    if (sbw <= 0.0f) return;
    *visible = 1;
    *th = sbw;
    *tw = inner_w;
    *tx = c->x + c->pad_l;
    *ty = c->y + c->h - c->pad_b - *th - 2.0f;
    float ratio = inner_w / c->scroll_content_w;
    *uw = *tw * ratio;
    if (*uw < 14.0f) *uw = 14.0f;
    float max_scroll = c->scroll_content_w - inner_w;
    if (max_scroll < 0.0f) max_scroll = 0.0f;
    float scroll_range = *tw - *uw;
    *ux = *tx + (max_scroll > 0.0f && scroll_range > 0.0f
                  ? (c->scroll_left / max_scroll) * scroll_range : 0.0f);
    *uy = *ty;
    *uh = *th;
}

/* Ensure internal overlay element exists; returns index or -1 */
static int ensure_overlay_element(const char* id, const char* classes, int host, int axis) {
    int idx = get_element_by_id(id);
    if (idx == -1) {
        if (elem_count >= MAX_ELEMENTS) return -1;
        idx = elem_count++;
        memset(&elements[idx], 0, sizeof(LunaElement));
        strncpy(elements[idx].id, id, sizeof(elements[idx].id) - 1);
        strncpy(elements[idx].class_name, classes, sizeof(elements[idx].class_name) - 1);
        strncpy(elements[idx].type, "div", sizeof(elements[idx].type) - 1);
        elements[idx].parent_idx = -1;
        elements[idx].luna_internal = 1;
        elements[idx].position_mode = POS_ABSOLUTE;
        elements[idx].css_positioned = 1;
        elements[idx].z_index = 9000 + axis;
        elements[idx].pointer_events_none = 1;
    }
    elements[idx].sb_host_idx = host;
    elements[idx].sb_axis = axis;
    return idx;
}

static void place_overlay_rect(int idx, float x, float y, float w, float h, int visible) {
    if (idx < 0) return;
    LunaElement* e = &elements[idx];
    if (!visible) {
        e->display_none = 1;
        e->pointer_events_none = 1;
        e->w = e->h = 0.0f;
        return;
    }
    e->display_none = 0;
    e->x = x;
    e->y = y;
    e->w = w;
    e->h = h;
    e->rel_x = x;
    e->rel_y = y;
    e->pos_overridden_x = 1;
    e->pos_overridden_y = 1;
    update_element_style(e);
    /* Preserve overlay behavior after style reset. */
    e->display_none = 0;
    e->pointer_events_none = 1;
    e->luna_internal = 1;
    e->position_mode = POS_ABSOLUTE;
    e->x = x; e->y = y; e->w = w; e->h = h;
    e->pos_overridden_x = 1;
    e->pos_overridden_y = 1;
}

static int element_aria_hidden(int idx);

/* Position scrollbar + a11y overlay nodes from layout geometry (CSS draws them). */
static void sync_css_overlay_elements(void) {
    static int sb_slots[MAX_ELEMENTS][4];
    memset(sb_slots, 0, sizeof(sb_slots));

    for (int i = 0; i < elem_count; i++) {
        LunaElement* c = &elements[i];
        if (c->luna_internal || !is_visible(i)) continue;

        float tx, ty, tw, th, ux, uy, uw, uh;
        int vis = 0;

        scrollbar_geom_y(c, &tx, &ty, &tw, &th, &ux, &uy, &uw, &uh, &vis);
        if (!sb_slots[i][0]) {
            char id[48];
            snprintf(id, sizeof(id), "luna_sb_vt_%d", i);
            sb_slots[i][0] = ensure_overlay_element(id, "luna_sb_track luna_sb_v", i, 0);
            snprintf(id, sizeof(id), "luna_sb_vh_%d", i);
            sb_slots[i][1] = ensure_overlay_element(id, "luna_sb_thumb luna_sb_v", i, 1);
        }
        place_overlay_rect(sb_slots[i][0], tx, ty, tw, th, vis);
        place_overlay_rect(sb_slots[i][1], ux, uy, uw, uh, vis);

        scrollbar_geom_x(c, &tx, &ty, &tw, &th, &ux, &uy, &uw, &uh, &vis);
        if (!sb_slots[i][2]) {
            char id[48];
            snprintf(id, sizeof(id), "luna_sb_ht_%d", i);
            sb_slots[i][2] = ensure_overlay_element(id, "luna_sb_track luna_sb_h", i, 2);
            snprintf(id, sizeof(id), "luna_sb_hh_%d", i);
            sb_slots[i][3] = ensure_overlay_element(id, "luna_sb_thumb luna_sb_h", i, 3);
        }
        place_overlay_rect(sb_slots[i][2], tx, ty, tw, th, vis);
        place_overlay_rect(sb_slots[i][3], ux, uy, uw, uh, vis);
    }

    int a11y = get_element_by_id("luna_a11y_bar");
    if (a11y == -1 && elem_count < MAX_ELEMENTS) {
        a11y = elem_count++;
        memset(&elements[a11y], 0, sizeof(LunaElement));
        strncpy(elements[a11y].id, "luna_a11y_bar", sizeof(elements[a11y].id) - 1);
        strncpy(elements[a11y].class_name, "luna_a11y hidden", sizeof(elements[a11y].class_name) - 1);
        strncpy(elements[a11y].type, "div", sizeof(elements[a11y].type) - 1);
        elements[a11y].parent_idx = -1;
        elements[a11y].luna_internal = 1;
        elements[a11y].position_mode = POS_ABSOLUTE;
        elements[a11y].css_positioned = 1;
        elements[a11y].z_index = 12000;
        elements[a11y].pointer_events_none = 1;
        elements[a11y].aria_live = 1;
        update_element_style(&elements[a11y]);
    }
    if (a11y != -1) {
        double now = luna_now();
        char buf[256] = {0};
        int show = 0;
        if (g_a11y_live_until > now && g_a11y_live_msg[0]) {
            snprintf(buf, sizeof(buf), "%s", g_a11y_live_msg);
            show = 1;
            if (g_a11y_live_assertive)
                strncpy(elements[a11y].class_name, "luna_a11y luna_a11y_assertive", sizeof(elements[a11y].class_name) - 1);
            else
                strncpy(elements[a11y].class_name, "luna_a11y", sizeof(elements[a11y].class_name) - 1);
        } else if (g_focus_via_keyboard && g_focused_element_idx != -1 &&
                   !element_aria_hidden(g_focused_element_idx)) {
            LunaElement* fe = &elements[g_focused_element_idx];
            if (fe->aria_label[0])
                snprintf(buf, sizeof(buf), "%s", fe->aria_label);
            else if (fe->role[0] && fe->text[0])
                snprintf(buf, sizeof(buf), "%.64s: %.180s", fe->role, fe->text);
            else if (fe->text[0])
                snprintf(buf, sizeof(buf), "%s", fe->text);
            if (buf[0]) show = 1;
            strncpy(elements[a11y].class_name, "luna_a11y", sizeof(elements[a11y].class_name) - 1);
        }
        if (show) {
            remove_class(&elements[a11y], "hidden");
            set_text(a11y, buf);
            elements[a11y].x = 12.0f;
            elements[a11y].y = window_height - 38.0f;
            elements[a11y].w = window_width - 24.0f;
            elements[a11y].h = 32.0f;
            elements[a11y].pos_overridden_x = 1;
            elements[a11y].pos_overridden_y = 1;
            update_element_style(&elements[a11y]);
        } else {
            add_class(&elements[a11y], "hidden");
            update_element_style(&elements[a11y]);
        }
    }
}

static int hit_scrollbar_thumb_y(int idx, double mx, double my) {
    LunaElement* c = &elements[idx];
    float tx, ty, tw, th, ux, uy, uw, uh;
    int vis = 0;
    scrollbar_geom_y(c, &tx, &ty, &tw, &th, &ux, &uy, &uw, &uh, &vis);
    if (!vis) return 0;
    return (mx >= ux && mx <= ux + uw && my >= uy && my <= uy + uh);
}

static int hit_scrollbar_thumb_x(int idx, double mx, double my) {
    LunaElement* c = &elements[idx];
    float tx, ty, tw, th, ux, uy, uw, uh;
    int vis = 0;
    scrollbar_geom_x(c, &tx, &ty, &tw, &th, &ux, &uy, &uw, &uh, &vis);
    if (!vis) return 0;
    return (mx >= ux && mx <= ux + uw && my >= uy && my <= uy + uh);
}

static int hit_scrollbar_track_y(int idx, double mx, double my) {
    LunaElement* c = &elements[idx];
    float tx, ty, tw, th, ux, uy, uw, uh;
    int vis = 0;
    scrollbar_geom_y(c, &tx, &ty, &tw, &th, &ux, &uy, &uw, &uh, &vis);
    if (!vis) return 0;
    if (mx < tx || mx > tx + tw || my < ty || my > ty + th) return 0;
    return !(mx >= ux && mx <= ux + uw && my >= uy && my <= uy + uh);
}

static int hit_scrollbar_track_x(int idx, double mx, double my) {
    LunaElement* c = &elements[idx];
    float tx, ty, tw, th, ux, uy, uw, uh;
    int vis = 0;
    scrollbar_geom_x(c, &tx, &ty, &tw, &th, &ux, &uy, &uw, &uh, &vis);
    if (!vis) return 0;
    if (mx < tx || mx > tx + tw || my < ty || my > ty + th) return 0;
    return !(mx >= ux && mx <= ux + uw && my >= uy && my <= uy + uh);
}

static void clamp_scroll_y(int idx) {
    LunaElement* sc = &elements[idx];
    float pad = sc->padding;
    float inner_h = sc->h - pad * 2.0f;
    float max_scroll = sc->scroll_content_h - inner_h;
    if (max_scroll < 0.0f) max_scroll = 0.0f;
    if (sc->scroll_top < 0.0f) sc->scroll_top = 0.0f;
    if (sc->scroll_top > max_scroll) sc->scroll_top = max_scroll;
    if (sc->scroll_dest_top < 0.0f) sc->scroll_dest_top = 0.0f;
    if (sc->scroll_dest_top > max_scroll) sc->scroll_dest_top = max_scroll;
}

static void clamp_scroll_x(int idx) {
    LunaElement* sc = &elements[idx];
    float pad = sc->padding;
    float inner_w = sc->w - pad * 2.0f;
    float max_scroll = sc->scroll_content_w - inner_w;
    if (max_scroll < 0.0f) max_scroll = 0.0f;
    if (sc->scroll_left < 0.0f) sc->scroll_left = 0.0f;
    if (sc->scroll_left > max_scroll) sc->scroll_left = max_scroll;
    if (sc->scroll_dest_left < 0.0f) sc->scroll_dest_left = 0.0f;
    if (sc->scroll_dest_left > max_scroll) sc->scroll_dest_left = max_scroll;
}

static void apply_scroll_snap_y(int idx);

static void set_scroll_top(int idx, float val, int instant) {
    LunaElement* sc = &elements[idx];
    sc->scroll_dest_top = val;
    if (instant || !sc->scroll_smooth) sc->scroll_top = val;
    clamp_scroll_y(idx);
    g_layout_dirty = 1;
}

static void set_scroll_left(int idx, float val, int instant) {
    LunaElement* sc = &elements[idx];
    sc->scroll_dest_left = val;
    if (instant || !sc->scroll_smooth) sc->scroll_left = val;
    clamp_scroll_x(idx);
    g_layout_dirty = 1;
}

static void add_scroll_top(int idx, float delta, int instant) {
    set_scroll_top(idx, elements[idx].scroll_dest_top + delta, instant);
    if (instant && elements[idx].scroll_snap_type)
        apply_scroll_snap_y(idx);
}

static void add_scroll_left(int idx, float delta, int instant) {
    set_scroll_left(idx, elements[idx].scroll_dest_left + delta, instant);
}

static void apply_scroll_snap_y(int idx) {
    LunaElement* sc = &elements[idx];
    if (!sc->scroll_snap_type) return;
    float pad = sc->padding;
    float inner_h = sc->h - pad * 2.0f;
    float max_scroll = sc->scroll_content_h - inner_h;
    if (max_scroll < 0.0f) max_scroll = 0.0f;
    float cur = sc->scroll_dest_top;
    float best = cur;
    float best_dist = 1e9f;
    for (int i = 0; i < elem_count; i++) {
        if (elements[i].parent_idx != idx || elements[i].display_none) continue;
        if (!is_visible(i)) continue;
        LunaElement* ch = &elements[i];
        float cy = ch->rel_y + ch->margin_top;
        float snap;
        if (ch->scroll_snap_align == 1)
            snap = cy + ch->h * 0.5f - inner_h * 0.5f;
        else if (ch->scroll_snap_align == 2)
            snap = cy + ch->h - inner_h;
        else
            snap = cy;
        if (snap < 0.0f) snap = 0.0f;
        if (snap > max_scroll) snap = max_scroll;
        float dist = fabsf(cur - snap);
        if (dist < best_dist) { best_dist = dist; best = snap; }
    }
    if (sc->scroll_snap_type == 2 && best_dist > 24.0f) return;
    if (fabsf(best - cur) > 0.5f)
        set_scroll_top(idx, best, 1);
}

void tick_smooth_scroll(double dt) {
    if (g_scroll_tick_count == 0) return;
    float k = 1.0f - expf(-(float)dt * 14.0f);
    if (k > 1.0f) k = 1.0f;
    for (int p = 0; p < g_scroll_tick_count; p++) {
        int i = g_scroll_tick_idx[p];
        if (i < 0 || i >= elem_count) continue;
        LunaElement* c = &elements[i];
        float dy = c->scroll_dest_top - c->scroll_top;
        float dx = c->scroll_dest_left - c->scroll_left;
        if (c->scroll_smooth) {
            if (fabsf(dy) > 0.001f || fabsf(dx) > 0.001f) g_layout_dirty = 1;
            if (fabsf(dy) > 0.25f) c->scroll_top += dy * k;
            else c->scroll_top = c->scroll_dest_top;
            if (fabsf(dx) > 0.25f) c->scroll_left += dx * k;
            else c->scroll_left = c->scroll_dest_left;
        }
        if (c->scroll_snap_type && fabsf(c->scroll_dest_top - c->scroll_top) <= 0.25f)
            apply_scroll_snap_y(i);
    }
}

static void scroll_track_click_y(int idx, double mx, double my) {
    (void)mx;
    LunaElement* sc = &elements[idx];
    float pad = sc->padding;
    float inner_h = sc->h - pad * 2.0f;
    float tx, ty, tw, th, ux, uy, uw, uh;
    int vis = 0;
    scrollbar_geom_y(sc, &tx, &ty, &tw, &th, &ux, &uy, &uw, &uh, &vis);
    if (!vis) return;
    float page = inner_h * 0.85f;
    float max_scroll = sc->scroll_content_h - inner_h;
    if (max_scroll < 0.0f) max_scroll = 0.0f;
    if ((float)my < uy)
        add_scroll_top(idx, -page, 0);
    else if ((float)my > uy + uh)
        add_scroll_top(idx, page, 0);
    else {
        float scroll_range = th - uh;
        if (scroll_range > 0.0f && max_scroll > 0.0f) {
            float ratio = ((float)my - uh * 0.5f - ty) / scroll_range;
            if (ratio < 0.0f) ratio = 0.0f;
            if (ratio > 1.0f) ratio = 1.0f;
            set_scroll_top(idx, ratio * max_scroll, 0);
        }
    }
}

static void scroll_track_click_x(int idx, double mx, double my) {
    (void)my;
    LunaElement* sc = &elements[idx];
    float pad = sc->padding;
    float inner_w = sc->w - pad * 2.0f;
    float tx, ty, tw, th, ux, uy, uw, uh;
    int vis = 0;
    scrollbar_geom_x(sc, &tx, &ty, &tw, &th, &ux, &uy, &uw, &uh, &vis);
    if (!vis) return;
    float page = inner_w * 0.85f;
    float max_scroll = sc->scroll_content_w - inner_w;
    if (max_scroll < 0.0f) max_scroll = 0.0f;
    if ((float)mx < ux)
        add_scroll_left(idx, -page, 0);
    else if ((float)mx > ux + uw)
        add_scroll_left(idx, page, 0);
    else {
        float scroll_range = tw - uw;
        if (scroll_range > 0.0f && max_scroll > 0.0f) {
            float ratio = ((float)mx - uw * 0.5f - tx) / scroll_range;
            if (ratio < 0.0f) ratio = 0.0f;
            if (ratio > 1.0f) ratio = 1.0f;
            set_scroll_left(idx, ratio * max_scroll, 0);
        }
    }
}

static int hit_test_at(double xpos, double ypos);

void scroll_callback(void* window, double xoffset, double yoffset) {
    (void)window; /* used via Luna mouse API / GLFW host */
    double mx = g_luna_mx, my = g_luna_my;
    int hit = hit_test_at(mx, my);
    int shift = g_luna_shift;
    if (fabs(yoffset) > 0.001 && shift) {
        int scroll_idx = find_scroll_target_x(hit);
        if (scroll_idx != -1)
            add_scroll_left(scroll_idx, -(float)yoffset * 18.0f, 0);
    } else if (fabs(yoffset) > 0.001) {
        int scroll_idx = find_scroll_target_y(hit);
        if (scroll_idx != -1) {
            add_scroll_top(scroll_idx, -(float)yoffset * 18.0f, 0);
        }
    }
    if (fabs(xoffset) > 0.001) {
        int scroll_idx = find_scroll_target_x(hit);
        if (scroll_idx != -1) {
            add_scroll_left(scroll_idx, -(float)xoffset * 18.0f, 0);
        }
    }
}

static float element_effective_opacity(int idx) {
    float op = 1.0f;
    while (idx != -1) {
        op *= elements[idx].opacity;
        idx = elements[idx].parent_idx;
    }
    return op;
}

/* Sum of cur_tx/cur_ty of all ANCESTORS (excluding idx itself). A CSS transform
   on an element establishes a coordinate system for its descendants, so a
   child's on-screen position must include every ancestor's translate. Layout
   x/y are transform-free (parent.x + rel_x); the transform offset is applied
   here at draw time so animated transforms don't require re-layout. */
static void accum_ancestor_transform(int idx, float* tx, float* ty) {
    float ax = 0.0f, ay = 0.0f;
    for (int p = elements[idx].parent_idx; p != -1; p = elements[p].parent_idx) {
        ax += elements[p].cur_tx;
        ay += elements[p].cur_ty;
    }
    *tx = ax; *ty = ay;
}

static void get_element_draw_bounds(LunaElement* e, float* out_x, float* out_y, float* out_w, float* out_h) {
    float scale = e->cur_scale;
    float dw = e->w * scale, dh = e->h * scale;
    float atx, aty;
    accum_ancestor_transform((int)(e - elements), &atx, &aty);
    *out_x = e->x + (e->w - dw) * 0.5f + e->cur_tx + atx;
    *out_y = e->y + (e->h - dh) * 0.5f + e->cur_ty + aty;
    *out_w = dw;
    *out_h = dh;
}

/* Hit testing must use layout boxes, not the press-scale draw shrink — otherwise
 * release often misses the same element that accepted the press. */
static void get_element_hit_bounds(LunaElement* e, float* out_x, float* out_y, float* out_w, float* out_h) {
    float atx, aty;
    accum_ancestor_transform((int)(e - elements), &atx, &aty);
    *out_x = e->x + e->cur_tx + atx;
    *out_y = e->y + e->cur_ty + aty;
    *out_w = e->w;
    *out_h = e->h;
}

/* Convert a screen-space box origin back to the element's layout coordinate.
 *
 * Layout coordinates deliberately exclude CSS transforms, while hit testing and
 * drawing use the transformed bounds above.  Keeping this conversion in one
 * place is important for dragging: an absolutely positioned child is relative
 * to its containing block's padding box, not necessarily to its direct
 * parent, and a transformed element's visual top-left is not e->x/e->y.
 */
static void drag_layout_origin(const LunaElement* e, float* ox, float* oy) {
    if (e->position_fixed || e->parent_idx == -1) {
        *ox = 0.0f;
        *oy = 0.0f;
        return;
    }
    if (e->position_mode == POS_ABSOLUTE) {
        float cw, ch;
        int cb = find_containing_block((int)(e - elements));
        containing_block_rect(cb, ox, oy, &cw, &ch);
        return;
    }
    *ox = elements[e->parent_idx].x;
    *oy = elements[e->parent_idx].y;
}

static void drag_set_screen_origin(LunaElement* e, float screen_x, float screen_y) {
    float parent_tx = 0.0f, parent_ty = 0.0f;
    float origin_x, origin_y;
    float scaled_w = e->w * e->cur_scale;
    float scaled_h = e->h * e->cur_scale;

    accum_ancestor_transform((int)(e - elements), &parent_tx, &parent_ty);
    drag_layout_origin(e, &origin_x, &origin_y);
    e->rel_x = screen_x - (e->w - scaled_w) * 0.5f - e->cur_tx - parent_tx - origin_x;
    e->rel_y = screen_y - (e->h - scaled_h) * 0.5f - e->cur_ty - parent_ty - origin_y;
    e->pos_overridden_x = 1;
    e->pos_overridden_y = 1;
}

static void apply_sticky_clip_bands(int idx, float* cx, float* cy, float* cw, float* ch) {
    (void)cx; (void)cw;
    LunaElement* e = &elements[idx];
    if (e->position_sticky) return;

    for (int p = e->parent_idx; p != -1; p = elements[p].parent_idx) {
        LunaElement* par = &elements[p];
        if (!overflow_scrollable(par->overflow_y)) continue;
        float st = par->scroll_top;
        if (par->scroll_dest_top > st) st = par->scroll_dest_top;
        if (st <= 0.5f) continue;

        float inner_top = par->y + par->border_width + par->pad_t;
        float inner_bottom = par->y + par->h - par->border_width - par->pad_b;

        for (int si = 0; si < elem_count; si++) {
            LunaElement* st = &elements[si];
            if (st->parent_idx != p || !st->position_sticky || st->display_none) continue;

            if ((st->sticky_use_top || st->has_top) && !st->sticky_use_bottom) {
                float stick_y = inner_top + (st->sticky_use_top ? st->sticky_top : 0.0f);
                if (fabsf(st->y - stick_y) > 1.5f || st->h <= 0.0f) continue;
                float band_bottom = st->y + st->h;
                if (band_bottom <= *cy) continue;
                if (band_bottom >= *cy + *ch) { *ch = 0.0f; return; }
                *ch = (*cy + *ch) - band_bottom;
                *cy = band_bottom;
            } else if (st->sticky_use_bottom && !st->sticky_use_top) {
                float stick_y = inner_bottom - st->h - st->sticky_bottom;
                if (fabsf(st->y - stick_y) > 1.5f || st->h <= 0.0f) continue;
                float band_top = st->y;
                if (band_top >= *cy + *ch) continue;
                if (band_top <= *cy) { *ch = 0.0f; return; }
                *ch = band_top - *cy;
            }
        }
    }
}

/* Returns nearest ancestor index with border_radius > 0 AND overflow clips, or -1. */
static int find_rounded_clip_ancestor(int idx) {
    int p = elements[idx].parent_idx;
    while (p != -1) {
        LunaElement* par = &elements[p];
        if (par->border_radius > 0.0f &&
            (overflow_clips(par->overflow_x) || overflow_clips(par->overflow_y)))
            return p;
        p = par->parent_idx;
    }
    return -1;
}

static int elem_is_self_or_descendant(int idx, int root) {
    for (int i = idx; i >= 0; i = elements[i].parent_idx)
        if (i == root) return 1;
    return 0;
}

/* ── Per-frame ancestor cache ───────────────────────────────────────────────
 *
 * Nearly every draw-time question about an element used to be answered by
 * walking its parent chain: effective opacity, the accumulated transform of
 * its ancestors, the nearest rounded clip ancestor, whether an ancestor's
 * overflow clips it away, and the scissor rect.  The scissor rect was the
 * worst of them — it walked the chain and, for every clipping ancestor it
 * found, walked the chain again to accumulate that ancestor's transform, so it
 * cost O(depth²) and the render loop asked for it two or three times per
 * element.
 *
 * All of those values are pure functions of the element tree, and a parent's
 * answer is exactly what its children need, so one linear pass computes them
 * all.  The pass is rebuilt at the top of every render instead of being
 * invalidated by hand: it is a few hundred O(1) steps, far cheaper than the
 * walks it replaces, and it can never go stale.  This matters most for the
 * Wayland shell, which renders a dozen layer surfaces per frame and therefore
 * paid the whole cost a dozen times over.
 *
 * Clip rects use ±LUNA_RC_INF on an axis the ancestor does not clip, which
 * reproduces the per-axis tests the walking versions performed. */
#define LUNA_RC_INF 1e7f

typedef struct {
    float eff_op;              /* opacity of self × every ancestor */
    float anc_tx, anc_ty;      /* summed cur_tx/cur_ty of ancestors only */
    int   clip_anc;            /* nearest rounded + clipping ancestor, or -1 */
    unsigned char vis;         /* no display:none anywhere up the chain */
    unsigned char clipped;     /* some ancestor clips overflow */
    unsigned char in_root;     /* self-or-descendant of g_render_root */
    /* Ancestor clip box in draw space (transforms applied) — the scissor. */
    float cx, cy, cw, ch;
    /* Ancestor clip box in layout space — the cheap "is it culled" test. */
    float lx, ly, lw, lh;
} LunaRenderCache;

static LunaRenderCache g_rc[LUNA_UI_MAX_ELEMENTS];

static void rc_rect_init(float* x, float* y, float* w, float* h) {
    *x = -LUNA_RC_INF; *y = -LUNA_RC_INF;
    *w = 2.0f * LUNA_RC_INF; *h = 2.0f * LUNA_RC_INF;
}

static void rc_rect_isect(float* x, float* y, float* w, float* h,
                          float ox, float oy, float ow, float oh) {
    float nx = *x > ox ? *x : ox;
    float ny = *y > oy ? *y : oy;
    float nr = (*x + *w < ox + ow) ? *x + *w : ox + ow;
    float nb = (*y + *h < oy + oh) ? *y + *h : oy + oh;
    *x = nx; *y = ny;
    *w = nr - nx > 0.0f ? nr - nx : 0.0f;
    *h = nb - ny > 0.0f ? nb - ny : 0.0f;
}

/* Fill one entry from scratch.  Only needed for the (never observed in the
 * shell's layouts) case of a parent that sits *after* its child in the element
 * array; the incremental path below cannot use a parent it has not built yet. */
static void rc_fill_slow(int i) {
    LunaRenderCache* c = &g_rc[i];
    c->eff_op = element_effective_opacity(i);
    accum_ancestor_transform(i, &c->anc_tx, &c->anc_ty);
    c->clip_anc = find_rounded_clip_ancestor(i);
    c->vis = (unsigned char)is_visible(i);
    c->in_root = (unsigned char)(g_render_root < 0 || elem_is_self_or_descendant(i, g_render_root));
    c->clipped = 0;
    rc_rect_init(&c->cx, &c->cy, &c->cw, &c->ch);
    rc_rect_init(&c->lx, &c->ly, &c->lw, &c->lh);
    for (int p = elements[i].parent_idx; p != -1; p = elements[p].parent_idx) {
        LunaElement* par = &elements[p];
        if (!overflow_clips(par->overflow_x) && !overflow_clips(par->overflow_y)) continue;
        float ptx, pty;
        accum_ancestor_transform(p, &ptx, &pty);
        ptx += par->cur_tx; pty += par->cur_ty;
        float pw = par->w - par->border_width * 2.0f - par->pad_l - par->pad_r;
        float ph = par->h - par->border_width * 2.0f - par->pad_t - par->pad_b;
        float px = par->x + par->border_width + par->pad_l;
        float py = par->y + par->border_width + par->pad_t;
        int cx_on = overflow_clips(par->overflow_x);
        int cy_on = overflow_clips(par->overflow_y);
        c->clipped = 1;
        rc_rect_isect(&c->cx, &c->cy, &c->cw, &c->ch,
                      cx_on ? px + ptx : -LUNA_RC_INF, cy_on ? py + pty : -LUNA_RC_INF,
                      cx_on ? pw : 2.0f * LUNA_RC_INF, cy_on ? ph : 2.0f * LUNA_RC_INF);
        rc_rect_isect(&c->lx, &c->ly, &c->lw, &c->lh,
                      cx_on ? px : -LUNA_RC_INF, cy_on ? py : -LUNA_RC_INF,
                      cx_on ? pw : 2.0f * LUNA_RC_INF, cy_on ? ph : 2.0f * LUNA_RC_INF);
    }
}

static void rc_build(void) {
    for (int i = 0; i < elem_count; i++) {
        LunaElement* e = &elements[i];
        LunaRenderCache* c = &g_rc[i];
        int p = e->parent_idx;
        if (p >= i || p < -1) {          /* out-of-order parent: rare/never */
            rc_fill_slow(i);
            continue;
        }
        if (p < 0) {
            c->eff_op  = e->opacity;
            c->anc_tx  = 0.0f;
            c->anc_ty  = 0.0f;
            c->clip_anc = -1;
            c->vis     = (unsigned char)!e->display_none;
            c->clipped = 0;
            c->in_root = (unsigned char)(g_render_root < 0 || i == g_render_root);
            rc_rect_init(&c->cx, &c->cy, &c->cw, &c->ch);
            rc_rect_init(&c->lx, &c->ly, &c->lw, &c->lh);
            continue;
        }
        const LunaRenderCache* pc = &g_rc[p];
        LunaElement* par = &elements[p];
        c->eff_op  = pc->eff_op * e->opacity;
        c->anc_tx  = pc->anc_tx + par->cur_tx;
        c->anc_ty  = pc->anc_ty + par->cur_ty;
        c->vis     = (unsigned char)(pc->vis && !e->display_none);
        c->in_root = (unsigned char)(g_render_root < 0 || i == g_render_root || pc->in_root);
        int cx_on = overflow_clips(par->overflow_x);
        int cy_on = overflow_clips(par->overflow_y);
        c->clip_anc = (par->border_radius > 0.0f && (cx_on || cy_on)) ? p : pc->clip_anc;
        c->clipped  = pc->clipped;
        c->cx = pc->cx; c->cy = pc->cy; c->cw = pc->cw; c->ch = pc->ch;
        c->lx = pc->lx; c->ly = pc->ly; c->lw = pc->lw; c->lh = pc->lh;
        if (cx_on || cy_on) {
            /* c->anc_tx already includes par->cur_tx, which is exactly the
             * transform that moves the parent's own padding box. */
            float pw = par->w - par->border_width * 2.0f - par->pad_l - par->pad_r;
            float ph = par->h - par->border_width * 2.0f - par->pad_t - par->pad_b;
            float px = par->x + par->border_width + par->pad_l;
            float py = par->y + par->border_width + par->pad_t;
            c->clipped = 1;
            rc_rect_isect(&c->cx, &c->cy, &c->cw, &c->ch,
                          cx_on ? px + c->anc_tx : -LUNA_RC_INF,
                          cy_on ? py + c->anc_ty : -LUNA_RC_INF,
                          cx_on ? pw : 2.0f * LUNA_RC_INF,
                          cy_on ? ph : 2.0f * LUNA_RC_INF);
            rc_rect_isect(&c->lx, &c->ly, &c->lw, &c->lh,
                          cx_on ? px : -LUNA_RC_INF, cy_on ? py : -LUNA_RC_INF,
                          cx_on ? pw : 2.0f * LUNA_RC_INF,
                          cy_on ? ph : 2.0f * LUNA_RC_INF);
        }
    }
}

/* Cached counterpart of is_rendered() for the draw loop. */
static int rc_is_rendered(int idx) {
    const LunaRenderCache* c = &g_rc[idx];
    if (!c->vis || elements[idx].visibility_hidden) return 0;
    if (!c->clipped) return 1;
    if (c->lw <= 0.0f || c->lh <= 0.0f) return 0;
    LunaElement* e = &elements[idx];
    return rects_intersect(e->x, e->y, e->w, e->h, c->lx, c->ly, c->lw, c->lh);
}

/* ── Render damage ──────────────────────────────────────────────────────────
 *
 * A host that repaints a surface because one label changed still hands the
 * compositor a whole new buffer, and a compositor with no damage information
 * has to assume every pixel of it is new.  On a full-screen wallpaper layer
 * that turns a clock tick into a whole-desktop recomposite, once a second,
 * forever — exactly the sort of regular hitch that is visible from across the
 * room.
 *
 * So the renderer remembers, per element, the rectangle it covered and a hash
 * of everything that decided its pixels, and reports the union of the rects
 * that no longer match.  The hash is taken over the element's whole struct, so
 * it cannot miss a field: it can only ever report *more* damage than strictly
 * necessary, which is always safe. */
typedef struct {
    float x, y, w, h;   /* surface-space bounds at the last render */
    uint64_t hash;
    unsigned char drawn;
} LunaDrawRec;

static LunaDrawRec g_draw_rec[LUNA_UI_MAX_ELEMENTS];
static float g_dmg_x0, g_dmg_y0, g_dmg_x1, g_dmg_y1;
static int   g_dmg_any;
static int   g_dmg_enabled = 0;

void luna_set_damage_tracking(int enabled) {
    if (!enabled == !g_dmg_enabled) return;
    g_dmg_enabled = enabled ? 1 : 0;
    /* Turning it on mid-session must not report "unchanged" against records
     * left over from before. */
    for (int i = 0; i < LUNA_UI_MAX_ELEMENTS; i++) g_draw_rec[i].drawn = 0;
}

static void damage_reset(void) {
    g_dmg_any = 0;
    g_dmg_x0 = g_dmg_y0 = 0.0f;
    g_dmg_x1 = g_dmg_y1 = 0.0f;
}

static void damage_add(float x, float y, float w, float h) {
    if (w <= 0.0f || h <= 0.0f) return;
    if (!g_dmg_any) {
        g_dmg_x0 = x; g_dmg_y0 = y;
        g_dmg_x1 = x + w; g_dmg_y1 = y + h;
        g_dmg_any = 1;
        return;
    }
    if (x < g_dmg_x0) g_dmg_x0 = x;
    if (y < g_dmg_y0) g_dmg_y0 = y;
    if (x + w > g_dmg_x1) g_dmg_x1 = x + w;
    if (y + h > g_dmg_y1) g_dmg_y1 = y + h;
}

static uint64_t damage_hash_element(int idx) {
    /* Hash only paint-relevant fields.  LunaElement carries multi-kilobyte
     * layout tables (grid areas, aria strings, unused trackers); FNV-ing the
     * whole struct on every draw of every element was measurable on the
     * console session whenever the wallpaper layer refreshed.  Omitting a
     * paint field can only over-report damage (safe); under-reporting is
     * avoided by covering every input the draw loop actually samples. */
    uint64_t h = 0xcbf29ce484222325ULL;
#define MIX_U64(v) do { h ^= (uint64_t)(v); h *= 0x100000001b3ULL; } while (0)
#define MIX_F32(f) do { \
        uint32_t bits_; \
        memcpy(&bits_, &(f), sizeof(bits_)); \
        MIX_U64(bits_); \
    } while (0)
    const LunaElement* e = &elements[idx];
    const LunaRenderCache* c = &g_rc[idx];
    MIX_F32(e->x); MIX_F32(e->y); MIX_F32(e->w); MIX_F32(e->h);
    MIX_F32(e->cur_tx); MIX_F32(e->cur_ty); MIX_F32(e->cur_scale);
    MIX_F32(e->cur_r); MIX_F32(e->cur_g); MIX_F32(e->cur_b); MIX_F32(e->cur_a);
    MIX_F32(e->cur_bd_r); MIX_F32(e->cur_bd_g); MIX_F32(e->cur_bd_b); MIX_F32(e->cur_bd_a);
    MIX_F32(e->r); MIX_F32(e->g); MIX_F32(e->b); MIX_F32(e->a);
    MIX_F32(e->t_r); MIX_F32(e->t_g); MIX_F32(e->t_b); MIX_F32(e->t_a);
    MIX_F32(e->bd_r); MIX_F32(e->bd_g); MIX_F32(e->bd_b); MIX_F32(e->bd_a);
    MIX_F32(e->opacity); MIX_F32(e->border_radius); MIX_F32(e->border_width);
    MIX_F32(e->font_size); MIX_F32(e->scroll_top); MIX_F32(e->scroll_left);
    MIX_U64(e->display_none); MIX_U64(e->font_bold); MIX_U64(e->font_face);
    MIX_U64(e->text_align); MIX_U64(e->has_shadow); MIX_U64(e->shadow_count);
    MIX_U64(e->has_gradient); MIX_U64(e->grad_type); MIX_U64(e->grad_stop_count);
    MIX_U64(e->has_backdrop_blur); MIX_U64(e->has_bg_image); MIX_U64(e->bg_image_tex);
    MIX_U64(e->has_outline); MIX_U64(e->overflow_x); MIX_U64(e->overflow_y);
    MIX_U64(e->is_hovered); MIX_U64(e->is_active); MIX_U64(e->text_decoration);
    MIX_F32(e->backdrop_blur_radius); MIX_F32(e->backdrop_saturate); MIX_F32(e->backdrop_brightness);
    MIX_F32(e->rad_c[0]); MIX_F32(e->rad_c[1]); MIX_F32(e->rad_c[2]); MIX_F32(e->rad_c[3]);
    for (int si = 0; si < e->shadow_count && si < LUNA_MAX_SHADOWS; si++) {
        MIX_F32(e->shadows[si].dx); MIX_F32(e->shadows[si].dy);
        MIX_F32(e->shadows[si].blur); MIX_F32(e->shadows[si].spread);
        MIX_F32(e->shadows[si].r); MIX_F32(e->shadows[si].g);
        MIX_F32(e->shadows[si].b); MIX_F32(e->shadows[si].a);
        MIX_U64(e->shadows[si].inset);
    }
    for (int gi = 0; gi < e->grad_stop_count && gi < MAX_GRAD_STOPS; gi++) {
        MIX_F32(e->grad_stop_pos[gi]);
        MIX_F32(e->grad_stop_r[gi]); MIX_F32(e->grad_stop_g[gi]);
        MIX_F32(e->grad_stop_b[gi]); MIX_F32(e->grad_stop_a[gi]);
    }
    /* Text: only the used prefix, not the whole 512-byte slot. */
    for (const unsigned char* p = (const unsigned char*)e->text; *p; p++)
        MIX_U64(*p);
    MIX_F32(c->eff_op); MIX_F32(c->anc_tx); MIX_F32(c->anc_ty);
    MIX_F32(c->cx); MIX_F32(c->cy); MIX_F32(c->cw); MIX_F32(c->ch);
    MIX_U64(c->clip_anc + 1);
    if (idx == g_focused_element_idx) {
        MIX_U64(0x9e3779b97f4a7c15ULL ^ (uint64_t)(g_focus_via_keyboard != 0));
        if (elements[idx].is_input)
            MIX_U64((uint64_t)(int64_t)(luna_now() * 2.0));
    }
#undef MIX_U64
#undef MIX_F32
    return h;
}

/* This element is being drawn at these surface-space bounds. */
static void damage_note(int idx, float x, float y, float w, float h) {
    if (!g_dmg_enabled) return;
    uint64_t hash = damage_hash_element(idx);
    LunaDrawRec* r = &g_draw_rec[idx];
    if (!r->drawn || r->hash != hash ||
        r->x != x || r->y != y || r->w != w || r->h != h) {
        if (r->drawn) damage_add(r->x, r->y, r->w, r->h);
        damage_add(x, y, w, h);
    }
    r->x = x; r->y = y; r->w = w; r->h = h;
    r->hash = hash;
    r->drawn = 1;
}

/* This element is no longer drawn; whatever it used to cover is now stale. */
static void damage_drop(int idx) {
    if (!g_dmg_enabled) return;
    LunaDrawRec* r = &g_draw_rec[idx];
    if (!r->drawn) return;
    damage_add(r->x, r->y, r->w, r->h);
    r->drawn = 0;
}

int luna_render_damage(float* x, float* y, float* w, float* h) {
    if (!g_dmg_enabled || !g_dmg_any) return 0;
    if (x) *x = g_dmg_x0;
    if (y) *y = g_dmg_y0;
    if (w) *w = g_dmg_x1 - g_dmg_x0;
    if (h) *h = g_dmg_y1 - g_dmg_y0;
    return 1;
}

/* Would drawing this element at these bounds change a pixel vs the last
 * committed frame?  Leaves g_draw_rec untouched so a subsequent real paint
 * still records the change. */
static int damage_would_change(int idx, float x, float y, float w, float h) {
    if (!g_dmg_enabled) return 1;
    uint64_t hash = damage_hash_element(idx);
    LunaDrawRec* r = &g_draw_rec[idx];
    if (!r->drawn) return 1;
    if (r->hash != hash) return 1;
    if (r->x != x || r->y != y || r->w != w || r->h != h) return 1;
    return 0;
}

static int damage_would_drop(int idx) {
    if (!g_dmg_enabled) return 0;
    return g_draw_rec[idx].drawn != 0;
}

static void rc_element_draw_bounds(int idx, float* out_x, float* out_y,
                                   float* out_w, float* out_h) {
    LunaElement* e = &elements[idx];
    const LunaRenderCache* c = &g_rc[idx];
    float scale = e->cur_scale;
    float dw = e->w * scale, dh = e->h * scale;
    *out_x = e->x + (e->w - dw) * 0.5f + e->cur_tx + c->anc_tx;
    *out_y = e->y + (e->h - dh) * 0.5f + e->cur_ty + c->anc_ty;
    *out_w = dw;
    *out_h = dh;
}

/* Same scissor, read out of the per-frame cache instead of re-walking the
 * parent chain once per clipping ancestor.  Only valid between rc_build() and
 * the end of the render pass that built it.  It also remembers the rect it
 * last programmed: the draw loop asks for the same scissor two or three times
 * per element (shadow pass, element pass, post-backdrop restore), and a
 * redundant glScissor still costs a driver state change. */
static int g_rc_scissor_on = -1;
static int g_rc_scissor[4] = {0, 0, 0, 0};

static void rc_scissor_reset(void) { g_rc_scissor_on = -1; }

static void rc_set_element_scissor(int idx, int fbw, int fbh) {
    const LunaRenderCache* c = &g_rc[idx];
    if (!c->clipped || c->cw <= 0.0f || c->ch <= 0.0f) return;
    float cx = c->cx, cy = c->cy, cw = c->cw, ch = c->ch;
    apply_sticky_clip_bands(idx, &cx, &cy, &cw, &ch);
    if (cw <= 0.0f || ch <= 0.0f) return;
    float res_x = LUNA_RRES_X, res_y = LUNA_RRES_Y;
    float sx = (float)fbw / res_x;
    float sy = (float)fbh / res_y;
    int sc[4];
    sc[0] = (int)((cx - g_render_off_x) * sx + 0.5f);
    sc[1] = (int)((res_y - (cy - g_render_off_y) - ch) * sy + 0.5f);
    sc[2] = (int)(cw * sx + 0.5f);
    sc[3] = (int)(ch * sy + 0.5f);
    if (sc[2] < 0) sc[2] = 0;
    if (sc[3] < 0) sc[3] = 0;
    if (g_rc_scissor_on == 1 && sc[0] == g_rc_scissor[0] && sc[1] == g_rc_scissor[1] &&
        sc[2] == g_rc_scissor[2] && sc[3] == g_rc_scissor[3])
        return;
    g_rc_scissor_on = 1;
    for (int i = 0; i < 4; i++) g_rc_scissor[i] = sc[i];
    glEnable(GL_SCISSOR_TEST);
    glScissor(sc[0], sc[1], sc[2], sc[3]);
}

static int anim_near(float a, float b) {
    float d = a - b;
    return d > -0.002f && d < 0.002f;
}

static int element_visual_unsettled(const LunaElement* e,
                                    float target_scale, float target_a) {
    return !anim_near(e->cur_r, e->r) || !anim_near(e->cur_g, e->g) ||
           !anim_near(e->cur_b, e->b) || !anim_near(e->cur_a, target_a) ||
           !anim_near(e->cur_bd_r, e->bd_r) ||
           !anim_near(e->cur_bd_g, e->bd_g) ||
           !anim_near(e->cur_bd_b, e->bd_b) ||
           !anim_near(e->cur_bd_a, e->bd_a) ||
           !anim_near(e->cur_scale, target_scale) ||
           !anim_near(e->cur_tx, e->transform_tx) ||
           !anim_near(e->cur_ty, e->transform_ty);
}

/* Update easing and optionally calculate per-root settling in the same pass. */
static unsigned visual_root_mask(int idx, const int* roots, int nroots) {
    unsigned mask = 0;
    for (int k = 0; k < nroots; k++) {
        if (roots[k] < 0 || roots[k] == idx ||
            (roots[k] >= 0 && elem_is_self_or_descendant(idx, roots[k])))
            mask |= 1u << k;
    }
    return mask;
}

/* Update only elements whose visual targets changed and retain them until
 * interpolation reaches the target.  A full-document first pass still occurs
 * after parsing/restyling because update_element_style() activates each node. */
static int update_animations_internal(double dt, const int* roots, int nroots,
                                      unsigned* out_mask) {
    unsigned mask = 0;
    int any = 0;
    if (nroots < 0) nroots = 0;
    if (nroots > 32) nroots = 32;

    float default_factor = 1.0f - expf(-(float)dt * 14.0f);
    if (default_factor > 1.0f) default_factor = 1.0f;
    if (default_factor < 0.0f) default_factor = 0.0f;

    /* Layout can alter percentage-based transform targets without touching
     * style state.  Detect those candidates once after a layout pass, then
     * return to the active-only list for subsequent frames. */
    if (g_visual_scan_needed) {
        for (int i = 0; i < elem_count; i++) {
            LunaElement* e = &elements[i];
            float press_scale =
                (e->cursor_pointer && e->is_active && e->drag_mode != 1) ? 0.96f : 1.0f;
            float target_scale = e->transform_scale * press_scale;
            float target_a = e->a <= 0.001f ? 0.0f :
                             (e->a >= 0.999f ? 1.0f : e->a);
            if (element_visual_unsettled(e, target_scale, target_a) ||
                (e->has_css_animation && e->anim_name[0] &&
                 !e->anim_finished && !e->anim_infinite))
                visual_activate_idx(i);
        }
        g_visual_scan_needed = 0;
    }

    int pos = 0;
    while (pos < g_visual_active_count) {
        int i = g_visual_active_idx[pos];
        if (i < 0 || i >= elem_count) { visual_remove_pos(pos); continue; }
        LunaElement* e = &elements[i];
        unsigned o = nroots ? visual_root_mask(i, roots, nroots) : 0;

        float press_scale =
            (e->cursor_pointer && e->is_active && e->drag_mode != 1) ? 0.96f : 1.0f;
        float target_scale = e->transform_scale * press_scale;
        float target_a = e->a <= 0.001f ? 0.0f :
                         (e->a >= 0.999f ? 1.0f : e->a);

        if (element_visual_unsettled(e, target_scale, target_a)) {
            float factor = default_factor;
            if (e->anim_speed > 0.0f && e->anim_speed != 14.0f) {
                factor = 1.0f - expf(-(float)dt * e->anim_speed);
                if (factor > 1.0f) factor = 1.0f;
                if (factor < 0.0f) factor = 0.0f;
            }
            e->cur_r += (e->r - e->cur_r) * factor;
            e->cur_g += (e->g - e->cur_g) * factor;
            e->cur_b += (e->b - e->cur_b) * factor;
            e->cur_a += (target_a - e->cur_a) * factor;
            e->cur_bd_r += (e->bd_r - e->cur_bd_r) * factor;
            e->cur_bd_g += (e->bd_g - e->cur_bd_g) * factor;
            e->cur_bd_b += (e->bd_b - e->cur_bd_b) * factor;
            e->cur_bd_a += (e->bd_a - e->cur_bd_a) * factor;
            e->cur_scale += (target_scale - e->cur_scale) * factor;
            e->cur_tx += (e->transform_tx - e->cur_tx) * factor;
            e->cur_ty += (e->transform_ty - e->cur_ty) * factor;

            if (anim_near(e->cur_r, e->r)) e->cur_r = e->r;
            if (anim_near(e->cur_g, e->g)) e->cur_g = e->g;
            if (anim_near(e->cur_b, e->b)) e->cur_b = e->b;
            if (anim_near(e->cur_a, target_a)) e->cur_a = target_a;
            if (anim_near(e->cur_bd_r, e->bd_r)) e->cur_bd_r = e->bd_r;
            if (anim_near(e->cur_bd_g, e->bd_g)) e->cur_bd_g = e->bd_g;
            if (anim_near(e->cur_bd_b, e->bd_b)) e->cur_bd_b = e->bd_b;
            if (anim_near(e->cur_bd_a, e->bd_a)) e->cur_bd_a = e->bd_a;
            if (anim_near(e->cur_scale, target_scale)) e->cur_scale = target_scale;
            if (anim_near(e->cur_tx, e->transform_tx)) e->cur_tx = e->transform_tx;
            if (anim_near(e->cur_ty, e->transform_ty)) e->cur_ty = e->transform_ty;
        }

        int keep = 0;
        if (!e->display_none) {
            int finite_keyframe_changed = e->anim_frame_changed && !e->anim_infinite;
            e->anim_frame_changed = 0;
            if (finite_keyframe_changed) { any = 1; mask |= o; keep = 1; }
            if (e->has_css_animation && e->anim_name[0] && !e->anim_finished) {
                if (!e->anim_infinite) { any = 1; mask |= o; keep = 1; }
            } else if (element_visual_unsettled(e, target_scale, target_a)) {
                any = 1;
                mask |= o;
                keep = 1;
            }
        } else {
            e->anim_frame_changed = 0;
        }

        if (!keep) visual_remove_pos(pos);
        else pos++;
    }
    if (out_mask) *out_mask = mask;
    return any;
}

void update_animations(double dt) {
    (void)update_animations_internal(dt, NULL, 0, NULL);
}

int luna_visuals_settling_under(int root_idx) {
    for (int i = 0; i < elem_count; i++) {
        LunaElement* e = &elements[i];
        if (e->display_none) continue;
        if (root_idx >= 0 && !elem_is_self_or_descendant(i, root_idx)) continue;
        if (e->has_css_animation && e->anim_name[0] && !e->anim_finished) {
            if (!e->anim_infinite) return 1;
            continue;
        }
        float press_scale =
            (e->cursor_pointer && e->is_active && e->drag_mode != 1) ? 0.96f : 1.0f;
        float target_scale = e->transform_scale * press_scale;
        float target_a = e->a <= 0.001f ? 0.0f :
                         (e->a >= 0.999f ? 1.0f : e->a);
        if (element_visual_unsettled(e, target_scale, target_a)) return 1;
    }
    return 0;
}

int luna_visuals_settling(void) { return luna_visuals_settling_under(-1); }

int luna_visuals_settling_mask(const int* roots, int nroots, unsigned* out_mask) {
    static unsigned own[LUNA_UI_MAX_ELEMENTS];
    unsigned mask = 0, any_bits = 0;
    int any = 0;
    if (nroots > 32) nroots = 32;
    for (int k = 0; k < nroots; k++)
        if (roots[k] < 0) any_bits |= 1u << k;
    for (int i = 0; i < elem_count; i++) {
        LunaElement* e = &elements[i];
        int parent = e->parent_idx;
        unsigned o = any_bits;
        if (parent >= 0 && parent < i) o |= own[parent];
        else if (parent >= 0) {
            for (int k = 0; k < nroots; k++)
                if (roots[k] >= 0 && elem_is_self_or_descendant(i, roots[k]))
                    o |= 1u << k;
        }
        for (int k = 0; k < nroots; k++) if (roots[k] == i) o |= 1u << k;
        own[i] = o;
        if (e->display_none) continue;
        if (e->has_css_animation && e->anim_name[0] && !e->anim_finished) {
            if (!e->anim_infinite) { any = 1; mask |= o; }
            continue;
        }
        float press_scale =
            (e->cursor_pointer && e->is_active && e->drag_mode != 1) ? 0.96f : 1.0f;
        float target_scale = e->transform_scale * press_scale;
        float target_a = e->a <= 0.001f ? 0.0f :
                         (e->a >= 0.999f ? 1.0f : e->a);
        if (element_visual_unsettled(e, target_scale, target_a)) {
            any = 1; mask |= o;
        }
    }
    if (out_mask) *out_mask = mask;
    return any;
}

static const CssKeyframe* find_keyframe_anim(const char* name);

/* Is any CSS @keyframes animation actually running under `root_idx`?
 *
 * The shell repaints its wallpaper layer on a timer so aurora/star keyframes
 * keep moving.  With a still wallpaper — a plain colour or a photo — there is
 * nothing to advance, and that timer was pure waste: a full-screen GL frame
 * plus a full-screen compositor recomposite, several times a second, forever.
 * `root_idx < 0` asks about the whole document. */
int luna_css_anim_running_under(int root_idx) {
    /* The registry already contains only live CSS-animation candidates.
     * Scanning the full DOM once a second caused a small but regular main-loop
     * spike on large shell layouts. */
    rebuild_activity_registries();
    for (int pos = 0; pos < g_css_anim_count; pos++) {
        int i = g_css_anim_idx[pos];
        if (i < 0 || i >= elem_count) continue;
        LunaElement* e = &elements[i];
        if (e->display_none || e->anim_finished) continue;
        if (!is_visible(i)) continue;
        if (root_idx >= 0 && !elem_is_self_or_descendant(i, root_idx)) continue;
        if (!find_keyframe_anim(e->anim_name)) continue;
        return 1;
    }
    return 0;
}

static float css_anim_ease(int easing, float t) {
    if (easing == 1) return t * t * (3.0f - 2.0f * t);
    return t;
}

static const CssKeyframe* find_keyframe_anim(const char* name) {
    if (!name || !name[0]) return NULL;
    for (int i = 0; i < g_keyframe_count; i++)
        if (strcmp(g_keyframes[i].name, name) == 0)
            return &g_keyframes[i];
    return NULL;
}

static float kf_stop_width_px(const LunaElement* e, const KeyframeStop* stop) {
    if (!stop->has_width) {
        if (e && e->anim_base_captured) {
            if (e->anim_base_w_pct) {
                int par = e->parent_idx;
                float pw = (par != -1) ? elements[par].w : window_width;
                return pw * e->raw_w;
            }
            return e->anim_base_w;
        }
        return 0.0f;
    }
    if (stop->width_pct) {
        int par = e ? e->parent_idx : -1;
        float pw = (par != -1) ? elements[par].w : window_width;
        return pw * stop->width;
    }
    return stop->width;
}

static float kf_stop_left_px(const LunaElement* e, const KeyframeStop* stop) {
    if (!stop->has_left) {
        if (e && e->anim_base_captured) {
            if (e->anim_base_left_pct) {
                int par = e->parent_idx;
                float pw = (par != -1) ? elements[par].w : window_width;
                return pw * e->anim_base_left;
            }
            return e->anim_base_left;
        }
        return 0.0f;
    }
    if (stop->left_pct) {
        int par = e ? e->parent_idx : -1;
        float pw = (par != -1) ? elements[par].w : window_width;
        return pw * stop->left;
    }
    return stop->left;
}

static void lerp_keyframe_stop(const KeyframeStop* a, const KeyframeStop* b, float u,
                               KeyframeStop* out, const LunaElement* e) {
    memset(out, 0, sizeof(*out));
    /* A base value is only an endpoint for a property the keyframes actually
     * animate.  Treating every captured base as animated made transform-only
     * animations rewrite `left`; for `calc(50% - 210px)` that lost the -210px
     * offset and moved the About dialog to x=640. */
    if (a->has_width || b->has_width) {
        out->has_width = 1;
        out->width_pct = 0;
        float av = kf_stop_width_px(e, a);
        float bv = kf_stop_width_px(e, b);
        out->width = av * (1.0f - u) + bv * u;
    }
    if (a->has_left || b->has_left) {
        out->has_left = 1;
        out->left_pct = 0;
        float av = kf_stop_left_px(e, a);
        float bv = kf_stop_left_px(e, b);
        out->left = av * (1.0f - u) + bv * u;
    }
    if (a->has_opacity || b->has_opacity) {
        out->has_opacity = 1;
        float av = a->has_opacity ? a->opacity : 1.0f;
        float bv = b->has_opacity ? b->opacity : 1.0f;
        out->opacity = av * (1.0f - u) + bv * u;
    }
    if (a->has_transform_scale || b->has_transform_scale) {
        out->has_transform_scale = 1;
        float av = a->has_transform_scale ? a->transform_scale : 1.0f;
        float bv = b->has_transform_scale ? b->transform_scale : 1.0f;
        out->transform_scale = av * (1.0f - u) + bv * u;
    }
    if (a->has_transform_tx || b->has_transform_tx) {
        out->has_transform_tx = 1;
        float av = a->has_transform_tx ? a->transform_tx : 0.0f;
        float bv = b->has_transform_tx ? b->transform_tx : 0.0f;
        out->transform_tx = av * (1.0f - u) + bv * u;
    }
    if (a->has_transform_ty || b->has_transform_ty) {
        out->has_transform_ty = 1;
        float av = a->has_transform_ty ? a->transform_ty : 0.0f;
        float bv = b->has_transform_ty ? b->transform_ty : 0.0f;
        out->transform_ty = av * (1.0f - u) + bv * u;
    }
}

static void sample_keyframe(const CssKeyframe* kf, float t, KeyframeStop* out, const LunaElement* e) {
    memset(out, 0, sizeof(*out));
    if (!kf || kf->stop_count <= 0) return;
    if (t <= kf->stops[0].position) {
        lerp_keyframe_stop(&kf->stops[0], &kf->stops[0], 0.0f, out, e);
        return;
    }
    if (t >= kf->stops[kf->stop_count - 1].position) {
        lerp_keyframe_stop(&kf->stops[kf->stop_count - 1], &kf->stops[kf->stop_count - 1], 1.0f, out, e);
        return;
    }
    for (int i = 0; i < kf->stop_count - 1; i++) {
        const KeyframeStop* a = &kf->stops[i];
        const KeyframeStop* b = &kf->stops[i + 1];
        if (t >= a->position && t <= b->position) {
            float span = b->position - a->position;
            float u = (span > 0.0001f) ? (t - a->position) / span : 0.0f;
            lerp_keyframe_stop(a, b, u, out, e);
            return;
        }
    }
    lerp_keyframe_stop(&kf->stops[kf->stop_count - 1], &kf->stops[kf->stop_count - 1], 1.0f, out, e);
}

static void apply_keyframe_stop_to_element(LunaElement* e, const KeyframeStop* stop) {
    int layout_changed = 0;
    if (stop->has_width) {
        if (!e->anim_override_layout || e->pct_w || !e->has_css_width ||
            fabsf(e->css_width - stop->width) > 0.01f)
            layout_changed = 1;
        e->anim_override_layout = 1;
        e->pct_w = 0;
        e->has_css_width = 1;
        e->css_width = stop->width;
        e->w = stop->width;
    }
    if (stop->has_left) {
        if (!e->anim_override_layout || !e->has_left || e->pct_left ||
            !e->pos_overridden_x || fabsf(e->rel_x - stop->left) > 0.01f)
            layout_changed = 1;
        e->anim_override_layout = 1;
        e->has_left = 1;
        e->pct_left = 0;
        e->rel_x = stop->left;
        e->pos_overridden_x = 1;
    }
    if (layout_changed) g_layout_dirty = 1;
    if (stop->has_opacity) e->opacity = stop->opacity;
    if (stop->has_transform_scale) e->transform_scale = stop->transform_scale;
    if (stop->has_transform_tx) { e->transform_tx = stop->transform_tx; e->has_custom_bg = 0; }
    if (stop->has_transform_ty) e->transform_ty = stop->transform_ty;
}

static void update_css_keyframe_animations(double now) {
    int pos = 0;
    while (pos < g_css_anim_count) {
        int i = g_css_anim_idx[pos];
        if (i < 0 || i >= elem_count) {
            g_css_anim_idx[pos] = g_css_anim_idx[--g_css_anim_count];
            continue;
        }
        LunaElement* e = &elements[i];
        if (!e->has_css_animation || !e->anim_name[0] || e->anim_finished) {
            g_css_anim_idx[pos] = g_css_anim_idx[--g_css_anim_count];
            continue;
        }
        const CssKeyframe* kf = find_keyframe_anim(e->anim_name);
        if (!kf) { pos++; continue; }
        if (e->anim_start_time < 0.0) e->anim_start_time = now;
        float duration = e->anim_duration > 0.0f ? e->anim_duration : 1.0f;
        float elapsed = (float)(now - e->anim_start_time) - e->anim_delay;
        if (elapsed < 0.0f) { pos++; continue; }
        float cycle_t = elapsed / duration;
        int completed = 0;
        if (e->anim_infinite) {
            if (e->anim_alternate) {
                int seg = (int)cycle_t;
                int rev = seg % 2;
                cycle_t -= (float)seg;
                if (rev) cycle_t = 1.0f - cycle_t;
            } else cycle_t -= floorf(cycle_t);
        } else {
            if (cycle_t >= 1.0f) { cycle_t = 1.0f; completed = 1; }
            if (e->anim_alternate && cycle_t > 0.5f)
                cycle_t = 1.0f - (cycle_t - 0.5f) * 2.0f;
        }
        cycle_t = css_anim_ease(e->anim_easing, cycle_t);
        KeyframeStop sampled;
        sample_keyframe(kf, cycle_t, &sampled, e);
        apply_keyframe_stop_to_element(e, &sampled);
        e->anim_frame_changed = 1;
        visual_activate_idx(i);
        if (completed) {
            e->anim_finished = 1;
            g_css_anim_idx[pos] = g_css_anim_idx[--g_css_anim_count];
            continue;
        }
        pos++;
    }
}

/* A screenshot is a comparison artifact, not an animation frame.  Capturing
 * the very first frame made a short entrance animation (such as the About
 * dialog's backdropFade) look like a rendering defect.  Keep this tiny scan
 * on the screenshot-only path so normal rendering stays O(elements). */
static int css_animations_are_settling(double now) {
    for (int i = 0; i < elem_count; i++) {
        const LunaElement* e = &elements[i];
        if (!e->has_css_animation || !e->anim_name[0] || e->anim_infinite ||
            e->anim_start_time < 0.0 || e->anim_duration <= 0.0f)
            continue;
        if (!e->anim_finished &&
            now < e->anim_start_time + e->anim_delay + e->anim_duration)
            return 1;
        /* Keyframes write the target transform; the renderer's inexpensive
         * smoothing pass reaches it a few frames later.  Include that tail in
         * a screenshot so a completed scale animation is not captured at
         * 0.99x. */
        if (fabsf(e->cur_scale - e->transform_scale) > 0.002f ||
            fabsf(e->cur_tx - e->transform_tx) > 0.05f ||
            fabsf(e->cur_ty - e->transform_ty) > 0.05f)
            return 1;
    }
    return 0;
}

static int take_screenshot(const char* path) {
    if (!path || !path[0]) return 0;
    int fbw = g_luna_fbw, fbh = g_luna_fbh;
    if (fbw <= 0 || fbh <= 0) return 0;
    size_t npix = (size_t)fbw * (size_t)fbh;
    unsigned char* pixels = (unsigned char*)malloc(npix * 4);
    unsigned char* flipped = (unsigned char*)malloc(npix * 3);
    if (!pixels || !flipped) { free(pixels); free(flipped); return 0; }
    glReadPixels(0, 0, fbw, fbh, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    for (int y = 0; y < fbh; y++) {
        const unsigned char* src = pixels + (size_t)(fbh - 1 - y) * (size_t)fbw * 4;
        unsigned char* dst = flipped + (size_t)y * (size_t)fbw * 3;
        for (int x = 0; x < fbw; x++) {
            dst[x * 3 + 0] = src[x * 4 + 0];
            dst[x * 3 + 1] = src[x * 4 + 1];
            dst[x * 3 + 2] = src[x * 4 + 2];
        }
    }
    int ok = stbi_write_png(path, fbw, fbh, 3, flipped, fbw * 3);
    free(pixels);
    free(flipped);
    if (ok) fprintf(stderr, "[vespera] Screenshot saved: %s (%dx%d)\n", path, fbw, fbh);
    else fprintf(stderr, "[vespera] Screenshot failed: %s\n", path);
    return ok != 0;
}

// ============================================================
// Render-order sorting by z-index
// ============================================================

// Effective z-index accumulates ancestor stacking contexts.  Taking max()
// discards a child's local z-index when its parent has a higher value, which
// makes a later drag overlay intercept traffic-light buttons in a dialog.
static int __attribute__((unused)) effective_z_index(int idx) {
    if (idx >= 0 && idx < elem_count && !g_render_order_dirty)
        return g_cached_eff_z[idx];
    int z = elements[idx].z_index;
    int p = elements[idx].parent_idx;
    while (p != -1) {
        z += elements[p].z_index;
        p = elements[p].parent_idx;
    }
    return z;
}

static int cmp_render_order(const void* a, const void* b) {
    int ia = *(const int*)a, ib = *(const int*)b;
    int za = g_cached_eff_z[ia], zb = g_cached_eff_z[ib];
    if (za != zb) return (za > zb) - (za < zb);
    /* Sticky headers/footers paint above scrolling siblings. */
    int sa = elements[ia].position_sticky ? 1 : 0;
    int sb = elements[ib].position_sticky ? 1 : 0;
    if (sa != sb) return sa - sb;
    return ia - ib; // preserve DOM order for same effective z-index
}

static void build_render_order() {
    if (!g_render_order_dirty) return;
    for (int i = 0; i < elem_count; i++) {
        int z = elements[i].z_index;
        int p = elements[i].parent_idx;
        while (p != -1) {
            z += elements[p].z_index;
            p = elements[p].parent_idx;
        }
        if (elements[i].position_sticky) z += 1;
        g_cached_eff_z[i] = z;
        render_order[i] = i;
    }
    qsort(render_order, elem_count, sizeof(int), cmp_render_order);
    g_render_order_dirty = 0;
}

// ============================================================
// Font loading
// ============================================================

int bake_font_set(const unsigned char* ttf_buffer, FontAtlas* atlases) {
    int offset = stbtt_GetFontOffsetForIndex(ttf_buffer, 0);
    if (offset < 0) offset = 0;
    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, ttf_buffer, offset)) return 0;
    for (int i = 0; i < NUM_FONT_SIZES; i++) {
        static unsigned char temp_bitmap[LUNA_ASCII_ATLAS_SIZE * LUNA_ASCII_ATLAS_SIZE];
        memset(temp_bitmap, 0, sizeof(temp_bitmap));
        memset(atlases[i].cdata, 0, sizeof(atlases[i].cdata));

        float raster_scale = stbtt_ScaleForMappingEmToPixels(&info, font_sizes[i]);
        int pack_x = 1, pack_y = 1, row_h = 0;
        for (int cp = 32; cp < 128; cp++) {
            int x0, y0, x1, y1, advance, lsb;
            stbtt_GetCodepointBitmapBox(&info, cp, raster_scale, raster_scale,
                                        &x0, &y0, &x1, &y1);
            stbtt_GetCodepointHMetrics(&info, cp, &advance, &lsb);
            int gw = x1 - x0, gh = y1 - y0;
            if (gw < 1) gw = 1;
            if (gh < 1) gh = 1;
            if (pack_x + gw + 2 >= LUNA_ASCII_ATLAS_SIZE) {
                pack_x = 1;
                pack_y += row_h + 2;
                row_h = 0;
            }
            if (pack_y + gh + 2 >= LUNA_ASCII_ATLAS_SIZE) return 0;

            int ax = pack_x + 1, ay = pack_y + 1;
            stbtt_MakeCodepointBitmap(&info,
                &temp_bitmap[ay * LUNA_ASCII_ATLAS_SIZE + ax], gw, gh,
                LUNA_ASCII_ATLAS_SIZE,
                raster_scale, raster_scale, cp);
            stbtt_bakedchar* bc = &atlases[i].cdata[cp - 32];
            bc->x0 = (unsigned short)ax;
            bc->y0 = (unsigned short)ay;
            bc->x1 = (unsigned short)(ax + gw);
            bc->y1 = (unsigned short)(ay + gh);
            bc->xoff = (float)x0;
            bc->yoff = (float)y0;
            bc->xadvance = (float)advance * raster_scale;
            pack_x += gw + 3;
            if (gh > row_h) row_h = gh;
            (void)lsb;
        }
        glGenTextures(1, &atlases[i].tex);
        glBindTexture(GL_TEXTURE_2D, atlases[i].tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED,
                     LUNA_ASCII_ATLAS_SIZE, LUNA_ASCII_ATLAS_SIZE, 0,
                     GL_RED, GL_UNSIGNED_BYTE, temp_bitmap);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        atlases[i].loaded = 1;
    }
    return 1;
}

// ---- font scanning helpers ----------------------------------------

// Returns 1 if the filename extension is a supported font format.
static int is_font_file(const char* name) {
    size_t n = strlen(name);
    if (n < 4) return 0;
    const char* ext = name + n - 4;
    return (strcasecmp(ext, ".ttf") == 0 ||
            strcasecmp(ext, ".otf") == 0 ||
            strcasecmp(ext + 1, "tc") == 0); // .ttc / .otc (n>=5 implied by ext check)
}

// Returns 1 if the filename looks like a bold variant.
static int is_bold_name(const char* name) {
    // Case-insensitive search for "bold" in the basename.
    char lower[512];
    size_t n = strlen(name);
    if (n >= sizeof(lower)) n = sizeof(lower) - 1;
    for (size_t i = 0; i < n; i++) lower[i] = (char)tolower((unsigned char)name[i]);
    lower[n] = '\0';
    return strstr(lower, "bold") != NULL;
}

// Dynamic font path list.
typedef struct {
    char** paths;
    int    count;
    int    cap;
} FontPathList;

static void fpl_add(FontPathList* fpl, const char* path) {
    if (fpl->count >= fpl->cap) {
        fpl->cap = fpl->cap ? fpl->cap * 2 : 64;
        fpl->paths = realloc(fpl->paths, sizeof(char*) * fpl->cap);
    }
    fpl->paths[fpl->count++] = strdup(path);
}

static void fpl_free(FontPathList* fpl) {
    for (int i = 0; i < fpl->count; i++) free(fpl->paths[i]);
    free(fpl->paths);
    fpl->paths = NULL; fpl->count = fpl->cap = 0;
}

// Recursively scan dir and collect font paths into reg/bold lists.
static void scan_fonts_dir(const char* dir, FontPathList* reg, FontPathList* bold) {
    DIR* d = opendir(dir);
    if (!d) return;
    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue; // skip . and ..
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);

        struct stat st;
        if (stat(path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            scan_fonts_dir(path, reg, bold); // recurse
        } else if (S_ISREG(st.st_mode) && is_font_file(ent->d_name)) {
            if (is_bold_name(ent->d_name))
                fpl_add(bold, path);
            else
                fpl_add(reg, path);
        }
    }
    closedir(d);
}

// Try each path in the list; return first successfully loaded buffer.
static unsigned char* try_load_font_list(FontPathList* fpl, int (*score_fn)(const char*), long* out_sz) {
    long dummy = 0;
    if (!out_sz) out_sz = &dummy;
    int best = -1, best_score = -999999;
    for (int i = 0; i < fpl->count; i++) {
        int sc = score_fn(fpl->paths[i]);
        if (sc > best_score) { best_score = sc; best = i; }
    }
    if (best >= 0 && best_score > 0) {
        unsigned char* buf = read_file_bytes(fpl->paths[best], out_sz);
        if (buf) {
            fprintf(stderr, "[vespera] Font loaded: %s (score=%d)\n", fpl->paths[best], best_score);
            return buf;
        }
    }
    return NULL;
}

// ------------------------------------------------------------------

void init_font() {
    FontPathList reg  = {0};
    FontPathList bold_list = {0};

    scan_fonts_dir("/usr/share/fonts", &reg, &bold_list);
    scan_fonts_dir("ui/fonts", &reg, &bold_list);
    scan_fonts_dir("skins/fonts", &reg, &bold_list);

    const char* env_ui = getenv("LUNA_FONT_REGULAR");
    unsigned char* reg_buf = NULL;
    if (env_ui && env_ui[0]) {
        reg_buf = read_file_bytes(env_ui, &g_font_ttf_sz);
        if (reg_buf) fprintf(stderr, "[vespera] Font loaded: %s (LUNA_FONT_REGULAR)\n", env_ui);
    }
    /* The shell CSS uses Inter.  Prefer the bundled, browser-identical face
     * before probing arbitrary system sans-serif fonts, which vary by host. */
    if (!reg_buf) {
        const char* inter_cands[] = {
            "skins/fonts/web/Inter-Regular.ttf",
            "apps/luna-ui/skins/fonts/web/Inter-Regular.ttf",
            "../skins/fonts/web/Inter-Regular.ttf",
        };
        for (size_t i = 0; i < sizeof(inter_cands) / sizeof(inter_cands[0]); i++) {
            reg_buf = read_file_bytes(inter_cands[i], &g_font_ttf_sz);
            if (reg_buf) {
                fprintf(stderr, "[vespera] Font loaded: %s (bundled Inter)\n", inter_cands[i]);
                break;
            }
        }
    }
    if (!reg_buf) reg_buf = try_load_font_list(&reg, font_path_score_ui, &g_font_ttf_sz);

    const char* env_cjk = getenv("LUNA_FONT_CJK");
    long cjk_sz = 0;
    if (env_cjk && env_cjk[0]) {
        g_cjk_font_ttf = read_file_bytes(env_cjk, &cjk_sz);
        if (g_cjk_font_ttf) fprintf(stderr, "[vespera] CJK font loaded: %s (LUNA_FONT_CJK)\n", env_cjk);
    }
    if (!g_cjk_font_ttf) g_cjk_font_ttf = try_load_font_list(&reg, font_path_score, &cjk_sz);

    const char* env_mono = getenv("LUNA_FONT_MONO");
    long mono_sz = 0;
    if (env_mono && env_mono[0]) {
        g_mono_font_ttf = read_file_bytes(env_mono, &mono_sz);
        if (g_mono_font_ttf) fprintf(stderr, "[vespera] Mono font loaded: %s (LUNA_FONT_MONO)\n", env_mono);
    }
    if (!g_mono_font_ttf) g_mono_font_ttf = try_load_font_list(&reg, font_path_score_mono, &mono_sz);

    if (!reg_buf) fprintf(stderr, "[vespera] Warning: no regular font found under /usr/share/fonts\n");
    else {
        bake_font_set(reg_buf, font_regular);
        g_font_ttf = reg_buf;
        int off = stbtt_GetFontOffsetForIndex(g_font_ttf, 0);
        if (off < 0) off = 0;
        g_font_info_ok = stbtt_InitFont(&g_font_info, g_font_ttf, off) ? 1 : 0;
        dyn_atlas_reset();
    }

    if (g_cjk_font_ttf) {
        int off = stbtt_GetFontOffsetForIndex(g_cjk_font_ttf, 0);
        if (off < 0) off = 0;
        g_cjk_font_info_ok = stbtt_InitFont(&g_cjk_font_info, g_cjk_font_ttf, off) ? 1 : 0;
    }
    if (g_mono_font_ttf) {
        int off = stbtt_GetFontOffsetForIndex(g_mono_font_ttf, 0);
        if (off < 0) off = 0;
        g_mono_font_info_ok = stbtt_InitFont(&g_mono_font_info, g_mono_font_ttf, off) ? 1 : 0;
    }

    /* LunaSymbols — env override, then /usr/share/fonts, then source-tree.
     * NOTE: candidate slots may be NULL; never stop the loop on a middle NULL. */
    {
        const char* env_icon = getenv("LUNA_FONT_ICONS");
        const char* icon_cands[] = {
            env_icon && env_icon[0] ? env_icon : NULL,
            /* Match the @font-face resource used by the document before a
             * system installation.  Distros can ship an older subset of the
             * same named font (notably without f1eb/Wi-Fi), which otherwise
             * makes browser and native output diverge. */
            "skins/fonts/LunaSymbols-Solid.otf",
            "../skins/fonts/LunaSymbols-Solid.otf",
            "apps/luna-ui/skins/fonts/LunaSymbols-Solid.otf",
            "/usr/share/fonts/luna/LunaSymbols-Solid.otf",
            "/usr/share/fonts/LunaSymbols-Solid.otf",
            "/usr/local/share/fonts/luna/LunaSymbols-Solid.otf",
        };
        long isz = 0;
        for (size_t i = 0; i < sizeof(icon_cands) / sizeof(icon_cands[0]); i++) {
            if (!icon_cands[i]) continue;
            g_icon_font_ttf = read_file_bytes(icon_cands[i], &isz);
            if (g_icon_font_ttf) {
                fprintf(stderr, "[vespera] Symbol font loaded: %s\n", icon_cands[i]);
                break;
            }
        }
        /* Fallback: any scanned path named LunaSymbols-Solid.* */
        if (!g_icon_font_ttf) {
            for (int i = 0; i < reg.count; i++) {
                const char* p = reg.paths[i];
                char lower[1024];
                size_t n = strlen(p);
                if (n >= sizeof(lower)) n = sizeof(lower) - 1;
                for (size_t k = 0; k < n; k++) lower[k] = (char)tolower((unsigned char)p[k]);
                lower[n] = 0;
                if (!strstr(lower, "lunasymbols-solid")) continue;
                g_icon_font_ttf = read_file_bytes(p, &isz);
                if (g_icon_font_ttf) {
                    fprintf(stderr, "[vespera] Symbol font loaded: %s\n", p);
                    break;
                }
            }
        }
        if (g_icon_font_ttf) {
            int off = stbtt_GetFontOffsetForIndex(g_icon_font_ttf, 0);
            if (off < 0) off = 0;
            g_icon_font_info_ok = stbtt_InitFont(&g_icon_font_info, g_icon_font_ttf, off) ? 1 : 0;
        } else {
            fprintf(stderr, "[vespera] Warning: LunaSymbols-Solid.otf not found (icon glyphs unavailable)\n");
            fprintf(stderr, "[vespera]   install: skins/fonts/LunaSymbols-*.otf → /usr/share/fonts/luna/\n");
        }

        const char* env_brand = getenv("LUNA_FONT_BRANDS");
        const char* brand_cands[] = {
            env_brand && env_brand[0] ? env_brand : NULL,
            "skins/fonts/LunaSymbols-Brands.otf",
            "../skins/fonts/LunaSymbols-Brands.otf",
            "apps/luna-ui/skins/fonts/LunaSymbols-Brands.otf",
            "/usr/share/fonts/luna/LunaSymbols-Brands.otf",
            "/usr/share/fonts/LunaSymbols-Brands.otf",
            "/usr/local/share/fonts/luna/LunaSymbols-Brands.otf",
        };
        long bsz = 0;
        for (size_t i = 0; i < sizeof(brand_cands) / sizeof(brand_cands[0]); i++) {
            if (!brand_cands[i]) continue;
            g_brand_font_ttf = read_file_bytes(brand_cands[i], &bsz);
            if (g_brand_font_ttf) {
                fprintf(stderr, "[vespera] Brand font loaded: %s\n", brand_cands[i]);
                break;
            }
        }
        if (!g_brand_font_ttf) {
            for (int i = 0; i < reg.count; i++) {
                const char* p = reg.paths[i];
                char lower[1024];
                size_t n = strlen(p);
                if (n >= sizeof(lower)) n = sizeof(lower) - 1;
                for (size_t k = 0; k < n; k++) lower[k] = (char)tolower((unsigned char)p[k]);
                lower[n] = 0;
                if (!strstr(lower, "lunasymbols-brands")) continue;
                g_brand_font_ttf = read_file_bytes(p, &bsz);
                if (g_brand_font_ttf) {
                    fprintf(stderr, "[vespera] Brand font loaded: %s\n", p);
                    break;
                }
            }
        }
        if (g_brand_font_ttf) {
            int off = stbtt_GetFontOffsetForIndex(g_brand_font_ttf, 0);
            if (off < 0) off = 0;
            g_brand_font_info_ok = stbtt_InitFont(&g_brand_font_info, g_brand_font_ttf, off) ? 1 : 0;
        }
    }

    long bold_sz = 0;
    g_bold_font_ttf = try_load_font_list(&bold_list, font_path_score_ui, &bold_sz);
    int bold_loaded = 0;
    if (g_bold_font_ttf) {
        bake_font_set(g_bold_font_ttf, font_bold_atlas);
        int off = stbtt_GetFontOffsetForIndex(g_bold_font_ttf, 0);
        if (off < 0) off = 0;
        g_bold_font_info_ok = stbtt_InitFont(&g_bold_font_info, g_bold_font_ttf, off) ? 1 : 0;
        bold_font_loaded = 1;
        bold_loaded = 1;
    }

    fpl_free(&reg);
    fpl_free(&bold_list);

    glCreateVertexArrays(1, &text_vao); glGenBuffers(1, &text_vbo);
    glBindVertexArray(text_vao); g_current_vao = text_vao;
    glBindBuffer(GL_ARRAY_BUFFER, text_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 4 * 6 * LUNA_TEXT_BATCH_GLYPHS, NULL, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
    glEnableVertexAttribArray(0);
    if (reg_buf) font_loaded = 1;
    (void)bold_loaded;
}

FontAtlas* get_atlas(float size, int bold, int* out_is_fake_bold) {
    int best = 0; float best_diff = 1e9f;
    for (int i = 0; i < NUM_FONT_SIZES; i++) {
        float d = fabsf(font_sizes[i] - size);
        if (d < best_diff) { best_diff = d; best = i; }
    }
    if (bold && bold_font_loaded) {
        if (out_is_fake_bold) *out_is_fake_bold = 0;
        return &font_bold_atlas[best];
    }
    if (out_is_fake_bold) *out_is_fake_bold = bold ? 1 : 0;
    return &font_regular[best];
}

static float atlas_nominal_size(FontAtlas* atlas) {
    for (int i = 0; i < NUM_FONT_SIZES; i++)
        if (atlas == &font_regular[i] || atlas == &font_bold_atlas[i])
            return font_sizes[i];
    return 16.0f;
}

static void text_metrics_begin(float css_px, int bold, int face, FontAtlas* atlas) {
    g_text_css_px = css_px > 0.0f ? css_px : 16.0f;
    g_font_face_hint = face;
    g_font_bold_hint = (bold && bold_font_loaded) ? 1 : 0;
    g_text_dynamic_ascii = (face == 3) ||
                           fabsf(atlas_nominal_size(atlas) - g_text_css_px) > 0.01f ||
                           text_device_scale() > 1.01f;
}

static void text_metrics_end(void) {
    g_text_css_px = 0.0f;
    g_font_face_hint = 0;
    g_font_bold_hint = 0;
    g_text_dynamic_ascii = 0;
}

float measure_text_width(FontAtlas* atlas, const char* text) {
    float w = 0.0f;
    float px = 16.0f;
    /* Infer pixel size from atlas pointer into font_regular/bold arrays */
    for (int i = 0; i < NUM_FONT_SIZES; i++) {
        if (atlas == &font_regular[i] || atlas == &font_bold_atlas[i]) {
            px = font_sizes[i];
            break;
        }
    }
    if (g_text_css_px > 0.0f) px = g_text_css_px;
    const char* p = text;
    while (*p) {
        int cp = utf8_decode(&p);
        if (cp == '\n' || cp == '\r') continue;
        w += glyph_advance(atlas, cp, px) + g_text_letter_spacing;
    }
    return w;
}

static float measure_text_range(FontAtlas* atlas, const char* start, int len) {
    float w = 0.0f;
    float px = 16.0f;
    for (int i = 0; i < NUM_FONT_SIZES; i++) {
        if (atlas == &font_regular[i] || atlas == &font_bold_atlas[i]) {
            px = font_sizes[i];
            break;
        }
    }
    if (g_text_css_px > 0.0f) px = g_text_css_px;
    const char* p = start;
    const char* end = start + len;
    while (p < end && *p) {
        int cp = utf8_decode(&p);
        if (cp == '\n' || cp == '\r') continue;
        w += glyph_advance(atlas, cp, px) + g_text_letter_spacing;
    }
    return w;
}

static int fit_text_chars(FontAtlas* atlas, const char* text, int len, float max_w) {
    float w = 0.0f;
    float px = 16.0f;
    for (int i = 0; i < NUM_FONT_SIZES; i++) {
        if (atlas == &font_regular[i] || atlas == &font_bold_atlas[i]) {
            px = font_sizes[i];
            break;
        }
    }
    if (g_text_css_px > 0.0f) px = g_text_css_px;
    const char* p = text;
    const char* end = text + len;
    const char* last = text;
    while (p < end && *p) {
        const char* before = p;
        int cp = utf8_decode(&p);
        if (cp == '\n' || cp == '\r') break;
        float adv = glyph_advance(atlas, cp, px) + g_text_letter_spacing;
        if (w + adv > max_w && last != text) return (int)(last - text);
        w += adv;
        last = p;
        (void)before;
    }
    return (int)(p - text);
}


// ============================================================
// Drawing
// ============================================================

void init_rect_geometry() {
    float vertices[] = { 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f };
    glCreateVertexArrays(1, &g_rect_vao);
    glGenBuffers(1, &g_rect_vbo);
    /* Setup path: bind unconditionally, then tell the tracker what is current. */
    glBindVertexArray(g_rect_vao);
    g_current_vao = g_rect_vao;
    glBindBuffer(GL_ARRAY_BUFFER, g_rect_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
}

// Load (or retrieve cached) texture from file path.
static GLuint __attribute__((unused)) load_or_get_texture(const char* path) {
    if (!path || !path[0]) return 0;
    for (int i = 0; i < g_tex_count; i++)
        if (strcmp(g_tex_cache[i].path, path) == 0) return g_tex_cache[i].tex;
    if (g_tex_count >= MAX_TEXTURES) return 0;

    char resolved[512];
    resolve_resource_path(path, resolved, sizeof(resolved));

    stbi_set_flip_vertically_on_load(1);
    int w, h, ch;
    unsigned char* data = stbi_load(resolved, &w, &h, &ch, 4);
    if (!data) data = stbi_load(path, &w, &h, &ch, 4);
    if (!data) return 0;

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);

    strncpy(g_tex_cache[g_tex_count].path, path, sizeof(g_tex_cache[0].path) - 1);
    g_tex_cache[g_tex_count].path[sizeof(g_tex_cache[0].path) - 1] = '\0';
    g_tex_cache[g_tex_count].tex = tex;
    g_tex_count++;
    return tex;
}

// Draw a texture cropped to a rounded rect element.
static void draw_image(float x, float y, float w, float h, float radius, GLuint tex, float alpha) {
    if (!img_program || !tex || alpha <= 0.004f || w <= 0.0f || h <= 0.0f) return;
    float half_min = (w < h ? w : h) * 0.5f;
    if (radius > half_min) radius = half_min;
    luna_use_program(img_program);
    glUniform2f(img_loc.uResolution, LUNA_RRES_X, LUNA_RRES_Y);
    glUniform2f(img_loc.uPos,  x - g_render_off_x, y - g_render_off_y);
    glUniform2f(img_loc.uSize, w, h);
    glUniform1f(img_loc.uRadius, radius);
    glUniform1f(img_loc.uAlpha, alpha);
    if (glActiveTexture_) glActiveTexture_(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i_(img_loc.uImage, 0);
    luna_bind_vao(g_rect_vao);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
}

/* Draw one LunaBgLayer on top of the current framebuffer at (x,y,w,h).
   Used by luna_render to render stacked background layers. */
static void draw_bg_layer(float x, float y, float w, float h,
                           const float* rad4, float eff_op,
                           const LunaBgLayer* layer) {
    if (!layer) return;
    float c4[4] = {0,0,0,0};
    float half_min = (w < h ? w : h) * 0.5f;
    if (rad4) {
        for (int i = 0; i < 4; i++) {
            c4[i] = rad4[i];
            if (c4[i] > half_min) c4[i] = half_min;
            if (c4[i] < 0.0f) c4[i] = 0.0f;
        }
    }
    int grad_mode = layer->has_gradient ? layer->grad_type : GRAD_NONE;
    float lr = 0, lg = 0, lb = 0, la = 0;
    if (layer->has_color) { lr=layer->r; lg=layer->g; lb=layer->b; la=layer->a * eff_op; }
    if (la <= 0.0f && grad_mode == GRAD_NONE) return;

    luna_use_program(bg_program);
    uni2f(bg_loc.uResolution, &bg_uni.uResolution, LUNA_RRES_X, LUNA_RRES_Y);
    uni2f(bg_loc.uPos,  &bg_uni.uPos, x - g_render_off_x, y - g_render_off_y);
    uni2f(bg_loc.uSize, &bg_uni.uSize, w, h);
    uni4f(bg_loc.uColor, &bg_uni.uColor, lr, lg, lb, la);
    uni4f(bg_loc.uBorderColor, &bg_uni.uBorderColor, 0,0,0,0);
    uni1f(bg_loc.uBorderWidth, &bg_uni.uBorderWidth, 0.0f);
    uni4f(bg_loc.uRadius4, &bg_uni.uRadius4, c4[0], c4[1], c4[2], c4[3]);
    uni1i(bg_loc.uGradient, &bg_uni.uGradient, grad_mode);
    if (layer->has_gradient) {
        int sc = layer->grad_stop_count;
        if (sc < 2) sc = 2;
        if (sc > MAX_GRAD_STOPS) sc = MAX_GRAD_STOPS;
        uni1i(bg_loc.uGradStopCount, &bg_uni.uGradStopCount, sc);
        for (int i = 0; i < MAX_GRAD_STOPS; i++) {
            float pr=0, pg=0, pb=0, pa=0, pp=(float)i/(float)(MAX_GRAD_STOPS-1);
            if (i < layer->grad_stop_count) {
                pr=layer->grad_stop_r[i]; pg=layer->grad_stop_g[i];
                pb=layer->grad_stop_b[i]; pa=layer->grad_stop_a[i];
                pp=layer->grad_stop_pos[i];
                pa *= eff_op;
            }
            uni4f(bg_loc.uGradColors[i], &bg_uni.uGradColors[i], pr, pg, pb, pa);
            uni1f(bg_loc.uGradStops[i],  &bg_uni.uGradStops[i],  pp);
        }
        uni1f(bg_loc.uGradAngle,  &bg_uni.uGradAngle,  layer->grad_angle);
        uni2f(bg_loc.uGradCenter, &bg_uni.uGradCenter, layer->grad_rad_cx, layer->grad_rad_cy);
        uni1f(bg_loc.uGradRadius, &bg_uni.uGradRadius, layer->grad_rad_r);
        /* Resolve percentage ellipse radii to pixels at draw time */
        float erx = layer->grad_rad_rx_pct ? layer->grad_rad_rx * w : layer->grad_rad_rx;
        float ery = layer->grad_rad_ry_pct ? layer->grad_rad_ry * h : layer->grad_rad_ry;
        uni1f(bg_loc.uGradRadRx, &bg_uni.uGradRadRx, erx);
        uni1f(bg_loc.uGradRadRy, &bg_uni.uGradRadRy, ery);
    } else {
        uni1f(bg_loc.uGradRadRx, &bg_uni.uGradRadRx, 0.0f);
        uni1f(bg_loc.uGradRadRy, &bg_uni.uGradRadRy, 0.0f);
    }
    uni1i(bg_loc.uFilterMode, &bg_uni.uFilterMode, 0);
    uni1f(bg_loc.uFilterBrightness, &bg_uni.uFilterBrightness, 1.0f);
    uni1f(bg_loc.uFilterContrast,   &bg_uni.uFilterContrast,   1.0f);
    uni1f(bg_loc.uFilterSaturate,   &bg_uni.uFilterSaturate,   1.0f);
    uni1f(bg_loc.uFilterHue,        &bg_uni.uFilterHue,        0.0f);
    uni1i(bg_loc.uClipEnabled, &bg_uni.uClipEnabled, g_bg_clip_enabled);
    if (g_bg_clip_enabled) {
        uni2f(bg_loc.uClipPos,  &bg_uni.uClipPos,  g_bg_clip_pos[0],  g_bg_clip_pos[1]);
        uni2f(bg_loc.uClipSize, &bg_uni.uClipSize, g_bg_clip_size[0], g_bg_clip_size[1]);
        uni4f(bg_loc.uClipRadius4, &bg_uni.uClipRadius4, g_bg_clip_rad4[0], g_bg_clip_rad4[1],
                    g_bg_clip_rad4[2], g_bg_clip_rad4[3]);
    }
    luna_bind_vao(g_rect_vao);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}

// Full-featured rect draw: solid color or gradient (linear/radial/conic/ellipse, multi-stop).
// rad4: per-corner radius {tl, tr, br, bl}; NULL means square corners.
void draw_rect_full(float x, float y, float w, float h,
                    float r, float g, float b, float a,
                    const float* rad4, float b_w,
                    float bd_r, float bd_g, float bd_b, float bd_a,
                    const LunaElement* ge) {
    int grad_mode = (ge && ge->has_gradient) ? ge->grad_type : GRAD_NONE;
    if (a <= 0.0f && bd_a <= 0.0f && grad_mode == GRAD_NONE) return;

    // CSS border-radius: 50% is stored as 50 (parse_float_val ignores %).
    // Cap to min(w,h)/2 so "50%" on small elements (e.g. 13px buttons) forms a circle.
    float half_min = (w < h ? w : h) * 0.5f;
    float c4[4] = { 0, 0, 0, 0 };
    if (rad4) {
        for (int i = 0; i < 4; i++) {
            c4[i] = rad4[i];
            if (c4[i] > half_min) c4[i] = half_min;
            if (c4[i] < 0.0f) c4[i] = 0.0f;
        }
    }

    luna_use_program(bg_program);
    uni2f(bg_loc.uResolution, &bg_uni.uResolution, LUNA_RRES_X, LUNA_RRES_Y);
    uni2f(bg_loc.uPos,  &bg_uni.uPos, x - g_render_off_x, y - g_render_off_y);
    uni2f(bg_loc.uSize, &bg_uni.uSize, w, h);
    uni4f(bg_loc.uColor,       &bg_uni.uColor,       r,    g,    b,    a);
    uni4f(bg_loc.uBorderColor, &bg_uni.uBorderColor, bd_r, bd_g, bd_b, bd_a);
    uni1f(bg_loc.uBorderWidth, &bg_uni.uBorderWidth, b_w);
    uni4f(bg_loc.uRadius4, &bg_uni.uRadius4, c4[0], c4[1], c4[2], c4[3]);
    uni1i(bg_loc.uGradient, &bg_uni.uGradient, grad_mode);
    if (ge && ge->has_gradient) {
        int sc = ge->grad_stop_count;
        if (sc < 2) sc = 2;
        if (sc > MAX_GRAD_STOPS) sc = MAX_GRAD_STOPS;
        uni1i(bg_loc.uGradStopCount, &bg_uni.uGradStopCount, sc);
        for (int i = 0; i < MAX_GRAD_STOPS; i++) {
            float pr = 0, pg = 0, pb = 0, pa = 0, pp = (float)i / (float)(MAX_GRAD_STOPS - 1);
            if (i < ge->grad_stop_count) {
                pr = ge->grad_stop_r[i]; pg = ge->grad_stop_g[i];
                pb = ge->grad_stop_b[i]; pa = ge->grad_stop_a[i];
                pp = ge->grad_stop_pos[i];
            }
            uni4f(bg_loc.uGradColors[i], &bg_uni.uGradColors[i], pr, pg, pb, pa);
            uni1f(bg_loc.uGradStops[i],  &bg_uni.uGradStops[i],  pp);
        }
        uni1f(bg_loc.uGradAngle,  &bg_uni.uGradAngle,  ge->grad_angle);
        uni2f(bg_loc.uGradCenter, &bg_uni.uGradCenter, ge->grad_rad_cx, ge->grad_rad_cy);
        uni1f(bg_loc.uGradRadius, &bg_uni.uGradRadius, ge->grad_rad_r);
        float erx = ge->grad_rad_rx_pct ? ge->grad_rad_rx * w : ge->grad_rad_rx;
        float ery = ge->grad_rad_ry_pct ? ge->grad_rad_ry * h : ge->grad_rad_ry;
        uni1f(bg_loc.uGradRadRx, &bg_uni.uGradRadRx, erx);
        uni1f(bg_loc.uGradRadRy, &bg_uni.uGradRadRy, ery);
    } else {
        uni1f(bg_loc.uGradRadRx, &bg_uni.uGradRadRx, 0.0f);
        uni1f(bg_loc.uGradRadRy, &bg_uni.uGradRadRy, 0.0f);
    }
    if (ge && ge->has_filter) {
        uni1i(bg_loc.uFilterMode,       &bg_uni.uFilterMode,       1);
        uni1f(bg_loc.uFilterBrightness, &bg_uni.uFilterBrightness, ge->filter_brightness);
        uni1f(bg_loc.uFilterContrast,   &bg_uni.uFilterContrast,   ge->filter_contrast);
        uni1f(bg_loc.uFilterSaturate,   &bg_uni.uFilterSaturate,   ge->filter_saturate);
        uni1f(bg_loc.uFilterHue,        &bg_uni.uFilterHue,        ge->filter_hue);
    } else {
        uni1i(bg_loc.uFilterMode,       &bg_uni.uFilterMode,       0);
        uni1f(bg_loc.uFilterBrightness, &bg_uni.uFilterBrightness, 1.0f);
        uni1f(bg_loc.uFilterContrast,   &bg_uni.uFilterContrast,   1.0f);
        uni1f(bg_loc.uFilterSaturate,   &bg_uni.uFilterSaturate,   1.0f);
        uni1f(bg_loc.uFilterHue,        &bg_uni.uFilterHue,        0.0f);
    }
    uni1i(bg_loc.uClipEnabled, &bg_uni.uClipEnabled, g_bg_clip_enabled);
    if (g_bg_clip_enabled) {
        uni2f(bg_loc.uClipPos,  &bg_uni.uClipPos,  g_bg_clip_pos[0],  g_bg_clip_pos[1]);
        uni2f(bg_loc.uClipSize, &bg_uni.uClipSize, g_bg_clip_size[0], g_bg_clip_size[1]);
        uni4f(bg_loc.uClipRadius4, &bg_uni.uClipRadius4, g_bg_clip_rad4[0], g_bg_clip_rad4[1],
                    g_bg_clip_rad4[2], g_bg_clip_rad4[3]);
    }

    luna_bind_vao(g_rect_vao);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}

// Simplified solid-color rect (no gradient, uniform corner radius).
void draw_rect(float x, float y, float w, float h,
               float r, float g, float b, float a,
               float radius, float b_w,
               float bd_r, float bd_g, float bd_b, float bd_a) {
    float rad4[4] = { radius, radius, radius, radius };
    draw_rect_full(x, y, w, h, r, g, b, a, rad4, b_w, bd_r, bd_g, bd_b, bd_a, NULL);
}

// CSS box-shadow: Gaussian soft shadow using dedicated SDF shader.
// rad4: per-corner element radius {tl, tr, br, bl}.
static void draw_shadow(float ex, float ey, float ew, float eh,
                        float sh_dx, float sh_dy, float sh_blur, float sh_spread, int sh_inset,
                        float sh_r, float sh_g, float sh_b, float sh_a,
                        const float* rad4, float eff_op) {
    if (!shadow_program || sh_a * eff_op <= 0.004f) return;
    float blur = sh_blur > 0.0f ? sh_blur : 0.0f;
    // Gaussian tail is visible up to ~1.65*blur from the shadow shape edge (discard < 0.004).
    // Inset shadows never paint outside the element, so no halo padding needed.
    float pad  = sh_inset ? 1.0f : blur * 1.75f + (sh_spread > 0.0f ? sh_spread : 0.0f) + 2.0f;

    // Shadow rect covers the full blurred area including offset:
    //   left edge  : min(0, sh_dx) - pad
    //   top edge   : min(0, sh_dy) - pad
    //   right edge : max(0, sh_dx) + pad
    //   bottom edge: max(0, sh_dy) + pad
    float left  = (sh_dx < 0.0f ? sh_dx : 0.0f) - pad;
    float top   = (sh_dy < 0.0f ? sh_dy : 0.0f) - pad;
    float right = (sh_dx > 0.0f ? sh_dx : 0.0f) + pad;
    float bot   = (sh_dy > 0.0f ? sh_dy : 0.0f) + pad;

    float sx   = ex + left;
    float sy   = ey + top;
    float sw   = ew + (right - left);
    float sh_h = eh + (bot  - top);

    // FragPos = vec2(aPos.x, 1-aPos.y)*uSize: x is right, y is UP (flipped from screen y).
    // inset = distance from shadow-rect corner to element corner, in FragPos space.
    // For x (no flip): inset_x = -left = pad + max(0, -sh_dx).
    // For y (flipped): the shadow rect y already encodes sh_dy via sh_h, so inset_y = -top.
    float inset_x = -left;
    float inset_y = -top;

    // uOffset in FragPos space: x needs explicit sh_dx shift because the inset only places
    // the element (not the shadow shape).  y is already baked in by the y-flip in the rect
    // height, so off_y must be 0 to avoid double-counting sh_dy.
    float off_x = sh_dx;
    float off_y = 0.0f;

    luna_use_program(shadow_program);
    uni2f(sh_loc.uResolution, &sh_uni.uResolution, LUNA_RRES_X, LUNA_RRES_Y);
    uni2f(sh_loc.uPos,  &sh_uni.uPos, sx - g_render_off_x, sy - g_render_off_y);
    uni2f(sh_loc.uSize, &sh_uni.uSize, sw, sh_h);
    uni4f(sh_loc.uShadowColor, &sh_uni.uShadowColor, sh_r, sh_g, sh_b, sh_a * eff_op);
    uni2f(sh_loc.uElemSize, &sh_uni.uElemSize, ew, eh);
    float half_min_s = (ew < eh ? ew : eh) * 0.5f;
    float c4[4] = { 0, 0, 0, 0 };
    if (rad4) {
        for (int i = 0; i < 4; i++) {
            c4[i] = rad4[i];
            if (c4[i] > half_min_s) c4[i] = half_min_s;
            if (c4[i] < 0.0f) c4[i] = 0.0f;
        }
    }
    uni4f(sh_loc.uRadius4, &sh_uni.uRadius4, c4[0], c4[1], c4[2], c4[3]);
    uni1f(sh_loc.uBlur,   &sh_uni.uBlur,   blur);
    uni1f(sh_loc.uSpread, &sh_uni.uSpread, sh_spread);
    uni1i(sh_loc.uInsetMode, &sh_uni.uInsetMode, sh_inset ? 1 : 0);
    uni2f(sh_loc.uShadowInset, &sh_uni.uShadowInset, inset_x, inset_y);
    uni2f(sh_loc.uOffset,      &sh_uni.uOffset,      off_x, off_y);

    luna_bind_vao(g_rect_vao);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}

/* Flush accumulated glyph quads for the given texture. */
static void flush_text_batch(GLuint tex, float* batch_buf, int* batch_count) {
    if (*batch_count == 0) return;
    glBindTexture(GL_TEXTURE_2D, tex);
    glBindBuffer(GL_ARRAY_BUFFER, text_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(sizeof(float) * 4 * 6 * (*batch_count)),
                    batch_buf);
    glDrawArrays(GL_TRIANGLES, 0, (*batch_count) * 6);
    *batch_count = 0;
}

void render_text_pass(FontAtlas* atlas, const char* text,
                      float start_x, float baseline_y,
                      float r, float g, float b, float a) {
    float css_px = 16.0f;
    for (int i = 0; i < NUM_FONT_SIZES; i++) {
        if (atlas == &font_regular[i] || atlas == &font_bold_atlas[i]) {
            css_px = font_sizes[i];
            break;
        }
    }
    if (g_text_css_px > 0.0f) css_px = g_text_css_px;

    /* Pre-bake every glyph that uses the exact-size/device-scale atlas so the
     * texture upload happens once per string rather than once per character. */
    {
        const char* q = text;
        while (*q) {
            int cp = utf8_decode(&q);
            if (cp >= 128 || g_text_dynamic_ascii)
                (void)dyn_bake_glyph(cp, css_px);
        }
    }
    dyn_flush_atlas();
    luna_use_program(text_program);
    uni2f(tx_loc.uResolution, &tx_uni.uResolution, LUNA_RRES_X, LUNA_RRES_Y);
    uni4f(tx_loc.textColor,   &tx_uni.textColor,   r, g, b, a);
    uni1i(tx_loc.uGradMode,   &tx_uni.uGradMode,   0);
    if (glActiveTexture_) glActiveTexture_(GL_TEXTURE0);
    luna_bind_vao(text_vao);

    /* Batch buffer: 6 verts × 4 floats × LUNA_TEXT_BATCH_GLYPHS glyphs */
    static float batch_buf[LUNA_TEXT_BATCH_GLYPHS * 6 * 4];
    int batch_count = 0;
    GLuint batch_tex = 0;

    /* Snap the line origin to the actual framebuffer pixel grid.  The old
     * fractional origin made GL_LINEAR blend two neighbouring atlas texels,
     * which looked like a faint blur even at scale 1. */
    float draw_x = text_snap_x(start_x - g_render_off_x);
    float draw_y = text_snap_y(baseline_y - g_render_off_y);
    const char* p = text;
    while (*p) {
        int cp = utf8_decode(&p);
        if (cp < 32) continue;

        float x0, y0, x1, y1, s0, t0, s1, t1;
        GLuint glyph_tex;

        if (cp < 128 && !g_text_dynamic_ascii) {
            glyph_tex = atlas->tex;
            const stbtt_bakedchar* bc = &atlas->cdata[cp - 32];
            x0 = text_snap_x(draw_x + bc->xoff);
            y0 = text_snap_y(draw_y + bc->yoff);
            x1 = x0 + (float)(bc->x1 - bc->x0);
            y1 = y0 + (float)(bc->y1 - bc->y0);
            s0 = (float)bc->x0 / (float)LUNA_ASCII_ATLAS_SIZE;
            t0 = (float)bc->y0 / (float)LUNA_ASCII_ATLAS_SIZE;
            s1 = (float)bc->x1 / (float)LUNA_ASCII_ATLAS_SIZE;
            t1 = (float)bc->y1 / (float)LUNA_ASCII_ATLAS_SIZE;
            draw_x += bc->xadvance + g_text_letter_spacing;
        } else {
            LunaDynGlyph* gd = dyn_bake_glyph(cp, css_px);
            if (!gd) continue;
            glyph_tex = g_dyn_tex;
            x0 = text_snap_x(draw_x + gd->xoff);
            y0 = text_snap_y(draw_y + gd->yoff);
            x1 = x0 + gd->draw_w;
            y1 = y0 + gd->draw_h;
            s0 = gd->x0 / (float)LUNA_DYN_ATLAS_W;
            t0 = gd->y0 / (float)LUNA_DYN_ATLAS_H;
            s1 = gd->x1 / (float)LUNA_DYN_ATLAS_W;
            t1 = gd->y1 / (float)LUNA_DYN_ATLAS_H;
            draw_x += gd->xadvance + g_text_letter_spacing;
        }

        /* Flush on texture boundary or full buffer */
        if (batch_tex != 0 && (glyph_tex != batch_tex || batch_count >= LUNA_TEXT_BATCH_GLYPHS)) {
            flush_text_batch(batch_tex, batch_buf, &batch_count);
        }
        batch_tex = glyph_tex;

        /* Append 6 verts (2 triangles) for this glyph */
        float* v = batch_buf + batch_count * 6 * 4;
        /* Triangle 1 */
        v[ 0]=x0; v[ 1]=y0; v[ 2]=s0; v[ 3]=t0;
        v[ 4]=x1; v[ 5]=y0; v[ 6]=s1; v[ 7]=t0;
        v[ 8]=x1; v[ 9]=y1; v[10]=s1; v[11]=t1;
        /* Triangle 2 */
        v[12]=x0; v[13]=y0; v[14]=s0; v[15]=t0;
        v[16]=x1; v[17]=y1; v[18]=s1; v[19]=t1;
        v[20]=x0; v[21]=y1; v[22]=s0; v[23]=t1;
        batch_count++;
    }
    /* Flush remaining glyphs */
    if (batch_count > 0) flush_text_batch(batch_tex, batch_buf, &batch_count);
    glBindTexture(GL_TEXTURE_2D, 0);
}


static int count_text_lines(FontAtlas* atlas, const char* text, float box_w,
                            float line_h, int max_lines,
                            int white_space, int text_overflow, int overflow_wrap) {
    (void)line_h;
    const char* p = text;
    int line_no = 0;
    while (*p && line_no < max_lines) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        int remaining = (int)strlen(p);
        int hard_break = -1;
        for (int i = 0; i < remaining; i++) {
            if (p[i] == '\n' || p[i] == '\r') { hard_break = i; break; }
        }
        int para_len = hard_break >= 0 ? hard_break : remaining;
        if (para_len <= 0) { p += 1; continue; }

        int take = para_len;
        if (white_space == 1) {
            take = fit_text_chars(atlas, p, para_len, box_w);
            if (take < para_len && text_overflow == 1 && box_w > 0.0f) {
                float ell_w = measure_text_width(atlas, "…");
                int base = fit_text_chars(atlas, p, para_len, box_w - ell_w);
                if (base < 0) base = 0;
                take = base;
            }
        } else {
            take = fit_text_chars(atlas, p, para_len, box_w);
            if (take <= 0 && para_len > 0) take = 1;
            if (take < para_len && !overflow_wrap) {
                int last_space = -1;
                for (int i = 0; i < take; i++)
                    if (p[i] == ' ' || p[i] == '\t') last_space = i;
                if (last_space > 0) take = last_space;
            } else if (take < para_len) {
                int last_space = -1;
                for (int i = 0; i < take; i++)
                    if (p[i] == ' ' || p[i] == '\t') last_space = i;
                if (last_space > 0 && measure_text_range(atlas, p, take) > box_w * 0.65f)
                    take = last_space;
            }
        }

        int is_last_visible_line = (line_no == max_lines - 1);
        int has_more = (take < para_len) || (hard_break >= 0 && p[hard_break + 1]);
        if (white_space == 1 || is_last_visible_line) {
            line_no++;
            break;
        }
        (void)has_more;
        line_no++;
        p += take;
        while (*p == ' ' || *p == '\t') p++;
        if (hard_break >= 0 && take >= hard_break) p++;
    }
    return line_no;
}

static void apply_text_transform_buf(char* buf, int transform) {
    if (!buf || !transform) return;
    int cap_next = 1;
    for (char* p = buf; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (transform == 1) { if (c >= 'a' && c <= 'z') *p = (char)(c - 32); }
        else if (transform == 2) { if (c >= 'A' && c <= 'Z') *p = (char)(c + 32); }
        else if (transform == 3) {
            if (cap_next && c >= 'a' && c <= 'z') *p = (char)(c - 32);
            else if (!cap_next && c >= 'A' && c <= 'Z') *p = (char)(c + 32);
            cap_next = (c == ' ' || c == '\t' || c == '\n' || c == '-');
        }
    }
}

/* fx: optional element supplying letter-spacing / text-transform /
   text-decoration / text-shadow (NULL for plain text). */
void render_text_fx(const char* text, float x, float y, float box_w, float box_h, int align,
                    int v_align, float r, float g, float b, float a, float fsize, int bold,
                    float css_line_height, int white_space, int text_overflow, int overflow_wrap,
    const LunaElement* fx) {
    g_text_letter_spacing = fx ? fx->letter_spacing : 0.0f;
    if (!font_loaded || !text || !*text) {
        g_text_letter_spacing = 0.0f;
        return;
    }
    /* background-clip: text may use a == 0 (color: transparent) intentionally */
    int bg_clip_text = (fx && fx->has_bg_clip_text && fx->has_gradient);
    if (a <= 0.0f && !bg_clip_text) {
        g_text_letter_spacing = 0.0f;
        return;
    }
    if (box_w <= 0.0f || box_h <= 0.0f) {
        g_text_letter_spacing = 0.0f;
        return;
    }
    if (css_line_height < 0.0f) css_line_height = -css_line_height * fsize;

    /* CSS text-transform */
    char tbuf[512];
    if (fx && fx->text_transform) {
        int n = 0, word_start = 1;
        for (const char* s = text; *s && n < (int)sizeof(tbuf) - 1; s++, n++) {
            unsigned char c = (unsigned char)*s;
            if (fx->text_transform == 1)      c = (unsigned char)toupper(c);
            else if (fx->text_transform == 2) c = (unsigned char)tolower(c);
            else { /* capitalize */
                if (word_start && isalpha(c)) c = (unsigned char)toupper(c);
                word_start = !isalnum(c);
            }
            tbuf[n] = (char)c;
        }
        tbuf[n] = '\0';
        text = tbuf;
    }

    int is_fake_bold = 0;
    FontAtlas* atlas = get_atlas(fsize, bold, &is_fake_bold);
    if (!atlas->loaded) {
        g_text_letter_spacing = 0.0f;
        return;
    }
    text_metrics_begin(fsize, bold, fx ? fx->font_face : 0, atlas);

    /* Line metrics must follow the requested CSS size, not the nearest static
     * atlas size.  Otherwise 17px text laid out as 16px and was then visibly
     * scaled or vertically misplaced. */
    float cap_h   = fsize * 0.72f;
    float line_h = css_line_height > 0.0f ? css_line_height : fsize * 1.25f;
    if (line_h < cap_h + 1.0f) line_h = cap_h + 1.0f;

    int max_lines = (int)floorf(box_h / line_h);
    if (max_lines < 1) max_lines = (box_h >= cap_h) ? 1 : 0;
    if (max_lines <= 0) {
        g_text_letter_spacing = 0.0f;
        text_metrics_end();
        return;
    }

    int line_count = count_text_lines(atlas, text, box_w, line_h, max_lines,
                                      white_space, text_overflow, overflow_wrap);
    if (line_count < 1) line_count = 1;
    float used_h = (float)line_count * line_h;
    float y_off = 0.0f;
    if (used_h < box_h) {
        if (v_align == 2)
            y_off = box_h - used_h;
        else if (v_align != 0)
            y_off = (box_h - used_h) * 0.5f;
    }

    const char* p = text;
    int line_no = 0;
    while (*p && line_no < max_lines) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        int remaining = (int)strlen(p);
        int hard_break = -1;
        for (int i = 0; i < remaining; i++) {
            if (p[i] == '\n' || p[i] == '\r') { hard_break = i; break; }
        }
        int para_len = hard_break >= 0 ? hard_break : remaining;
        if (para_len <= 0) { p += 1; continue; }

        int take = para_len;
        if (white_space == 1) {
            take = fit_text_chars(atlas, p, para_len, box_w);
            if (take < para_len && text_overflow == 1 && box_w > 0.0f) {
                float ell_w = measure_text_width(atlas, "...");
                int base = fit_text_chars(atlas, p, para_len, box_w - ell_w);
                if (base < 0) base = 0;
                take = base;
            }
        } else {
            take = fit_text_chars(atlas, p, para_len, box_w);
            if (take <= 0 && para_len > 0) take = 1;
            if (take < para_len && !overflow_wrap) {
                int last_space = -1;
                for (int i = 0; i < take; i++)
                    if (p[i] == ' ' || p[i] == '\t') last_space = i;
                if (last_space > 0) take = last_space;
            } else if (take < para_len) {
                int last_space = -1;
                for (int i = 0; i < take; i++)
                    if (p[i] == ' ' || p[i] == '\t') last_space = i;
                if (last_space > 0 && measure_text_range(atlas, p, take) > box_w * 0.65f)
                    take = last_space;
            }
        }

        int is_last_visible_line = (line_no == max_lines - 1);
        int has_more = (take < para_len) || (hard_break >= 0 && p[hard_break + 1]);
        char line[512];
        int n = take;
        if (n > (int)sizeof(line) - 8) n = (int)sizeof(line) - 8;
        memcpy(line, p, (size_t)n);
        line[n] = '\0';
        while (n > 0 && (line[n - 1] == ' ' || line[n - 1] == '\t')) line[--n] = '\0';

        if ((white_space == 1 || is_last_visible_line) && text_overflow == 1 && has_more) {
            float ell_w = measure_text_width(atlas, "…");
            while (n > 0 && measure_text_range(atlas, line, n) + ell_w > box_w)
                line[--n] = '\0';
            if (n + 3 < (int)sizeof(line)) strcat(line, "…");
        }

        float line_w = measure_text_width(atlas, line);
        float start_x = x;
        if      (align == 1) start_x = x + (box_w - line_w) / 2.0f;
        else if (align == 2) start_x = x + (box_w - line_w);
        if (start_x < x) start_x = x;

        int single_line = (white_space == 1 || line_count == 1);
        float baseline;
        if (single_line) {
            if (v_align == 2)
                baseline = y + box_h - (line_h - cap_h) * 0.5f;
            else if (v_align == 0)
                baseline = y + (line_h - cap_h) * 0.5f + cap_h;
            else
                baseline = y + (box_h - cap_h) * 0.5f + cap_h;
        } else {
            baseline = y + y_off + line_no * line_h + (line_h - cap_h) * 0.5f + cap_h;
        }
        if (baseline < y + cap_h) baseline = y + cap_h;
        if (baseline + (line_h - cap_h) * 0.5f > y + box_h + 0.5f) break;
        /* CSS text-shadow: offset pass(es) under the main glyphs */
        if (fx && fx->has_text_shadow && fx->tsh_a > 0.004f) {
            float sa = fx->tsh_a * a;
            if (fx->tsh_blur >= 2.0f) {
                float o = fx->tsh_blur * 0.5f;
                float qa = sa * 0.35f;
                render_text_pass(atlas, line, start_x + fx->tsh_dx - o, baseline + fx->tsh_dy, fx->tsh_r, fx->tsh_g, fx->tsh_b, qa);
                render_text_pass(atlas, line, start_x + fx->tsh_dx + o, baseline + fx->tsh_dy, fx->tsh_r, fx->tsh_g, fx->tsh_b, qa);
                render_text_pass(atlas, line, start_x + fx->tsh_dx, baseline + fx->tsh_dy - o, fx->tsh_r, fx->tsh_g, fx->tsh_b, qa);
                render_text_pass(atlas, line, start_x + fx->tsh_dx, baseline + fx->tsh_dy + o, fx->tsh_r, fx->tsh_g, fx->tsh_b, qa);
            } else {
                render_text_pass(atlas, line, start_x + fx->tsh_dx, baseline + fx->tsh_dy, fx->tsh_r, fx->tsh_g, fx->tsh_b, sa);
            }
        }
        if (bg_clip_text) {
            /* background-clip: text: set gradient uniforms so the text shader
               samples the element's gradient at each fragment's screen position. */
            luna_use_program(text_program);
            uni1i(tx_loc.uGradMode, &tx_uni.uGradMode, 1);
            int sc = fx->grad_stop_count;
            if (sc < 2) sc = 2;
            if (sc > MAX_GRAD_STOPS) sc = MAX_GRAD_STOPS;
            uni1i(tx_loc.uGradStopCount, &tx_uni.uGradStopCount, sc);
            for (int gi = 0; gi < MAX_GRAD_STOPS; gi++) {
                float pr=0, pg=0, pb=0, pa=1, pp=(float)gi/(float)(MAX_GRAD_STOPS-1);
                if (gi < fx->grad_stop_count) {
                    pr=fx->grad_stop_r[gi]; pg=fx->grad_stop_g[gi];
                    pb=fx->grad_stop_b[gi]; pa=fx->grad_stop_a[gi];
                    pp=fx->grad_stop_pos[gi];
                }
                uni4f(tx_loc.uGradColors[gi], &tx_uni.uGradColors[gi], pr, pg, pb, pa);
                uni1f(tx_loc.uGradStops[gi],  &tx_uni.uGradStops[gi],  pp);
            }
            uni1f(tx_loc.uGradAngle, &tx_uni.uGradAngle, fx->grad_angle);
            /* elem bounds in shader coordinates (g_render_off_x already subtracted) */
            float ex = x - g_render_off_x;
            float ey = y - g_render_off_y;
            uni4f(tx_loc.uElemBounds, &tx_uni.uElemBounds, ex, ey, box_w, box_h);
            render_text_pass(atlas, line, start_x, baseline, 1.0f, 1.0f, 1.0f, 1.0f);
            if (is_fake_bold) render_text_pass(atlas, line, start_x + 1.0f, baseline, 1.0f, 1.0f, 1.0f, 1.0f);
            luna_use_program(text_program);
            uni1i(tx_loc.uGradMode, &tx_uni.uGradMode, 0);
        } else {
            render_text_pass(atlas, line, start_x, baseline, r, g, b, a);
            if (is_fake_bold) render_text_pass(atlas, line, start_x + 1.0f, baseline, r, g, b, a);
        }
        /* CSS text-decoration: underline / line-through */
        if (fx && fx->text_decoration) {
            float th = (float)fsize / 14.0f;
            if (th < 1.0f) th = 1.0f;
            if (fx->text_decoration & 1)
                draw_rect(start_x, baseline + 2.0f, line_w, th, r, g, b, a, 0, 0, 0, 0, 0, 0);
            if (fx->text_decoration & 2)
                draw_rect(start_x, baseline - cap_h * 0.38f, line_w, th, r, g, b, a, 0, 0, 0, 0, 0, 0);
        }

        line_no++;
        if (white_space == 1 || is_last_visible_line) break;
        p += take;
        while (*p == ' ' || *p == '\t') p++;
        if (hard_break >= 0 && take >= hard_break) p++;
    }
    g_text_letter_spacing = 0.0f;
    text_metrics_end();
}

void render_text(const char* text, float x, float y, float box_w, float box_h, int align,
                 int v_align, float r, float g, float b, float a, float fsize, int bold,
                 float css_line_height, int white_space, int text_overflow, int overflow_wrap) {
    render_text_fx(text, x, y, box_w, box_h, align, v_align, r, g, b, a, fsize, bold,
                   css_line_height, white_space, text_overflow, overflow_wrap, NULL);
}

/* Normal-flow inline content starts at the top of its line box.  Centering
 * every element (the legacy renderer behaviour) moves ordinary block text —
 * notably the clock widget — below the browser baseline.  CSS flex controls
 * explicitly center/end their anonymous text item on the cross axis, and
 * native text inputs retain their platform-like centered line. */
static int css_text_vertical_align(const LunaElement* e) {
    if (strcmp(e->type, "button") == 0) return 1;
    if (e->is_input) return 1;
    if (e->display_mode != DISPLAY_FLEX) return 0;
    if (e->flex_direction == FLEX_DIR_ROW) {
        if (e->align_items == FLEX_ALIGN_END) return 2;
        return e->align_items == FLEX_ALIGN_CENTER ? 1 : 0;
    }
    if (e->justify_content == FLEX_JUSTIFY_END) return 2;
    return e->justify_content == FLEX_JUSTIFY_CENTER ? 1 : 0;
}

// (scrollbars, focus ring, a11y bar are CSS overlay elements — see sync_css_overlay_elements)

// ============================================================
// Event callbacks
// ============================================================

static void focus_element(int idx);
static void focus_element_ex(int idx, int via_keyboard);
static int element_is_inert(int idx);
static int element_aria_hidden(int idx);
static void input_set_caret_from_x(LunaElement* e, float local_x);
static void char_callback_impl(unsigned int codepoint);
static float measure_prefix_width(LunaElement* e, int byte_len);
static void input_update_scroll(LunaElement* e, float inner_w);

static int element_aria_hidden(int idx) {
    while (idx != -1) {
        if (elements[idx].aria_hidden) return 1;
        idx = elements[idx].parent_idx;
    }
    return 0;
}

static int is_descendant_of(int idx, int ancestor) {
    if (ancestor == -1) return 0;
    while (idx != -1) {
        if (idx == ancestor) return 1;
        idx = elements[idx].parent_idx;
    }
    return 0;
}

static int get_focus_trap_root(void) {
    for (int i = g_focus_trap_count - 1; i >= 0; i--) {
        int idx = g_focus_traps[i].idx;
        if (idx != -1 && is_visible(idx) &&
            !element_has_class(&elements[idx], "hidden"))
            return idx;
    }
    return -1;
}

static LunaTrapDismissFn get_trap_dismiss_fn(int trap_idx) {
    for (int i = g_focus_trap_count - 1; i >= 0; i--)
        if (g_focus_traps[i].idx == trap_idx)
            return g_focus_traps[i].on_dismiss;
    return NULL;
}

static float content_offset_y(int target, int ancestor) {
    float y = 0.0f;
    for (int c = target; c != -1 && c != ancestor; c = elements[c].parent_idx)
        y += elements[c].rel_y + elements[c].margin_top;
    return y;
}

static float content_offset_x(int target, int ancestor) {
    float x = 0.0f;
    for (int c = target; c != -1 && c != ancestor; c = elements[c].parent_idx)
        x += elements[c].rel_x + elements[c].margin_left;
    return x;
}

static void scroll_into_view(int target) {
    if (target == -1) return;
    LunaElement* e = &elements[target];
    for (int pass = 0; pass < 3; pass++) {
        for (int p = e->parent_idx; p != -1; p = elements[p].parent_idx) {
            LunaElement* sc = &elements[p];
            float pad = sc->padding;
            float smt = e->scroll_margin_top;
            float smb = e->scroll_margin_bottom;
            float sml = e->scroll_margin_left;
            float smr = e->scroll_margin_right;
            float spt = sc->scroll_padding_top;
            float spb = sc->scroll_padding_bottom;
            float spl = sc->scroll_padding_left;
            float spr = sc->scroll_padding_right;
            if (overflow_scrollable(sc->overflow_y)) {
                float inner_h = sc->h - pad * 2.0f;
                float cy = content_offset_y(target, p);
                float ct = cy - smt;
                float cb = cy + e->h + smb;
                float vis_top = sc->scroll_top + spt;
                float vis_bot = sc->scroll_top + inner_h - spb;
                if (ct < vis_top)
                    set_scroll_top(p, ct - spt, 0);
                else if (cb > vis_bot)
                    set_scroll_top(p, cb - inner_h + spb, 0);
            }
            if (overflow_scrollable(sc->overflow_x)) {
                float inner_w = sc->w - pad * 2.0f;
                float cx = content_offset_x(target, p);
                float cl = cx - sml;
                float cr = cx + e->w + smr;
                float vis_left = sc->scroll_left + spl;
                float vis_right = sc->scroll_left + inner_w - spr;
                if (cl < vis_left)
                    set_scroll_left(p, cl - spl, 0);
                else if (cr > vis_right)
                    set_scroll_left(p, cr - inner_w + spr, 0);
            }
        }
    }
}

static int find_drag_window_ptr(int idx) {
    int found = idx;
    while (idx != -1) {
        if (element_has_class(&elements[idx], "window")) found = idx;
        idx = elements[idx].parent_idx;
    }
    return found;
}

static void bring_window_ptr_to_front(int idx) {
    int win = find_drag_window_ptr(idx);
    if (win == -1) return;
    if (!element_has_class(&elements[win], "window")) return;
    int old_focus = g_focused_idx;
    if (old_focus != -1 && old_focus != win)
        remove_class(&elements[old_focus], "focused");
    elements[win].z_index = ++g_top_z;
    g_focused_idx = win;
    add_class(&elements[win], "focused");
    update_element_style(&elements[win]);
    if (old_focus != -1 && old_focus != win)
        update_element_style(&elements[old_focus]);
}

static int hit_test_at(double xpos, double ypos) {
    build_render_order();
    for (int ri = elem_count - 1; ri >= 0; ri--) {
        int i = render_order[ri];
        LunaElement* e = &elements[i];
        if (!is_rendered(i) || e->pointer_events_none || element_is_inert(i)) continue;
        float bx, by, bw, bh;
        get_element_hit_bounds(e, &bx, &by, &bw, &bh);
        if (xpos >= bx && xpos <= bx + bw &&
            ypos >= by && ypos <= by + bh) return i;
    }
    return -1;
}

static void set_window_cursor(void* window, int cursor_type) {
    (void)window;
    if (cursor_type == g_current_cursor) return;
    g_current_cursor = cursor_type;
    if (g_luna_platform.set_cursor) g_luna_platform.set_cursor(cursor_type);
#ifdef LUNA_UI_GLFW
    else if (g_luna_glfw_window) {
        GLFWcursor* cur = NULL;
        extern GLFWcursor *g_hand_cursor, *g_cursor_ibeam, *g_cursor_crosshair;
        extern GLFWcursor *g_cursor_hresize, *g_cursor_vresize;
        switch (cursor_type) {
            case 1: cur = g_hand_cursor; break; case 2: cur = g_cursor_ibeam; break;
            case 3: cur = g_cursor_crosshair; break; case 4: cur = g_cursor_hresize; break;
            case 5: cur = g_cursor_vresize; break;
        }
        glfwSetCursor((GLFWwindow*)g_luna_glfw_window, cur);
    }
#endif
}

void recompute_hover(void* window, double xpos, double ypos) {
    int old_scroll_hover_idx = g_scroll_hover_idx;
    int old_scroll_hover_axis = g_scroll_hover_axis;
    g_scroll_hover_idx = -1;
    g_scroll_hover_axis = -1;
    for (int si = 0; si < elem_count; si++) {
        if (hit_scrollbar_thumb_y(si, xpos, ypos) || hit_scrollbar_track_y(si, xpos, ypos)) {
            g_scroll_hover_idx = si;
            g_scroll_hover_axis = 0;
            break;
        }
        if (hit_scrollbar_thumb_x(si, xpos, ypos) || hit_scrollbar_track_x(si, xpos, ypos)) {
            g_scroll_hover_idx = si;
            g_scroll_hover_axis = 1;
            break;
        }
    }

    if (old_scroll_hover_idx != g_scroll_hover_idx ||
        old_scroll_hover_axis != g_scroll_hover_axis)
        g_pointer_visual_revision++;

    int hit = hit_test_at(xpos, ypos);

    int best_cursor = 0;
    if (g_scroll_hover_axis == 0) best_cursor = 5;
    else if (g_scroll_hover_axis == 1) best_cursor = 4;
    for (int i = 0; i < elem_count; i++) {
        LunaElement* e = &elements[i];
        int should_hover = 0;
        for (int a = hit; a != -1; a = elements[a].parent_idx) {
            if (a == i) { should_hover = 1; break; }
        }
        if (should_hover != e->is_hovered) {
            e->is_hovered = should_hover;
            update_element_style(e);
            g_pointer_visual_revision++;
        }
        if (should_hover && e->cursor_type > best_cursor)
            best_cursor = e->cursor_type;
    }

    set_window_cursor(window, best_cursor);
}

void cursor_position_callback(void* window, double xpos, double ypos) {
    g_luna_mx = xpos;
    g_luna_my = ypos;
    /* dragging mutates element positions / scroll offsets directly */
    if (g_scroll_drag_idx != -1 || drag_target_idx != -1) g_layout_dirty = 1;
    if (g_scroll_drag_idx != -1) {
        LunaElement* sc = &elements[g_scroll_drag_idx];
        float pad = sc->padding;
        if (g_scroll_drag_axis == 0) {
            float inner_h = sc->h - pad * 2.0f;
            float tx, ty, tw, th, ux, uy, uw, uh;
            int vis = 0;
            scrollbar_geom_y(sc, &tx, &ty, &tw, &th, &ux, &uy, &uw, &uh, &vis);
            if (vis) {
                float max_scroll = sc->scroll_content_h - inner_h;
                if (max_scroll < 0.0f) max_scroll = 0.0f;
                float scroll_range = th - uh;
                if (scroll_range > 0.0f && max_scroll > 0.0f) {
                    float thumb_y = (float)ypos - g_scroll_drag_off;
                    float ratio = (thumb_y - ty) / scroll_range;
                    if (ratio < 0.0f) ratio = 0.0f;
                    if (ratio > 1.0f) ratio = 1.0f;
                    float val = ratio * max_scroll;
                    sc->scroll_top = val;
                    sc->scroll_dest_top = val;
                }
            }
        } else {
            float inner_w = sc->w - pad * 2.0f;
            float tx, ty, tw, th, ux, uy, uw, uh;
            int vis = 0;
            scrollbar_geom_x(sc, &tx, &ty, &tw, &th, &ux, &uy, &uw, &uh, &vis);
            if (vis) {
                float max_scroll = sc->scroll_content_w - inner_w;
                if (max_scroll < 0.0f) max_scroll = 0.0f;
                float scroll_range = tw - uw;
                if (scroll_range > 0.0f && max_scroll > 0.0f) {
                    float thumb_x = (float)xpos - g_scroll_drag_off;
                    float ratio = (thumb_x - tx) / scroll_range;
                    if (ratio < 0.0f) ratio = 0.0f;
                    if (ratio > 1.0f) ratio = 1.0f;
                    float val = ratio * max_scroll;
                    sc->scroll_left = val;
                    sc->scroll_dest_left = val;
                }
            }
        }
        return;
    }
    if (drag_target_idx != -1) {
        if (fabs(xpos - g_press_x) > DRAG_THRESHOLD || fabs(ypos - g_press_y) > DRAG_THRESHOLD)
            g_drag_moved = 1;

        LunaElement* d = &elements[drag_target_idx];
        if (g_drag_mode == 2) {
            int p = d->parent_idx;
            float parent_w = (p != -1) ? elements[p].w : window_width;
            float parent_x = (p != -1) ? elements[p].x : 0.0f;
            float new_rel_x = (float)xpos - drag_offset_x - parent_x;
            if (new_rel_x < 0.0f) new_rel_x = 0.0f;
            if (new_rel_x > parent_w - d->w) new_rel_x = parent_w - d->w;
            d->rel_x = new_rel_x;
            d->pos_overridden_x = 1;
        } else {
            /* drag_offset is measured from the visual box, so invert the
               same CSS transform/containing-block mapping used for paint. */
            drag_set_screen_origin(d, (float)xpos - drag_offset_x,
                                    (float)ypos - drag_offset_y);
        }
        return;
    }
    recompute_hover(window, xpos, ypos);
}

void mouse_button_callback(void* window, int button, int action, int mods) {
    (void)window;
    double mx = g_luna_mx, my = g_luna_my;
    if (action == LUNA_PRESS)
        g_luna_last_click_mods = mods;

    if (action == LUNA_PRESS && button == LUNA_MOUSE_BUTTON_LEFT) {
        g_luna_last_click_button = LUNA_MOUSE_BUTTON_LEFT;
        g_drag_moved = 0;
        g_press_x = mx;
        g_press_y = my;

        for (int si = 0; si < elem_count; si++) {
            if (hit_scrollbar_thumb_y(si, mx, my)) {
                g_scroll_drag_idx = si;
                g_scroll_drag_axis = 0;
                float tx, ty, tw, th, ux, uy, uw, uh;
                int vis = 0;
                scrollbar_geom_y(&elements[si], &tx, &ty, &tw, &th, &ux, &uy, &uw, &uh, &vis);
                g_scroll_drag_off = (float)my - uy;
                return;
            }
            if (hit_scrollbar_thumb_x(si, mx, my)) {
                g_scroll_drag_idx = si;
                g_scroll_drag_axis = 1;
                float tx, ty, tw, th, ux, uy, uw, uh;
                int vis = 0;
                scrollbar_geom_x(&elements[si], &tx, &ty, &tw, &th, &ux, &uy, &uw, &uh, &vis);
                g_scroll_drag_off = (float)mx - ux;
                return;
            }
            if (hit_scrollbar_track_y(si, mx, my)) {
                scroll_track_click_y(si, mx, my);
                return;
            }
            if (hit_scrollbar_track_x(si, mx, my)) {
                scroll_track_click_x(si, mx, my);
                return;
            }
        }

        int hit = hit_test_at(mx, my);
        if (hit != -1) {
            LunaElement* e = &elements[hit];
            if (g_focused_element_idx != -1 && g_focused_element_idx != hit)
                update_element_style(&elements[g_focused_element_idx]);
            focus_element(hit);
            bring_window_ptr_to_front(hit);
            if (e->is_input) {
                float bx, by, bw, bh;
                get_element_draw_bounds(e, &bx, &by, &bw, &bh);
                input_set_caret_from_x(e, (float)mx - bx - e->pad_l);
            }
            e->is_active = 1;
            update_element_style(e);
            if (e->is_draggable) {
                g_drag_mode = e->drag_mode;
                if (e->drag_mode == 2) {
                    drag_target_idx = hit;
                    float bx, by, bw, bh;
                    get_element_draw_bounds(e, &bx, &by, &bw, &bh);
                    drag_offset_x = (float)mx - bx;
                    drag_offset_y = (float)my - by;
                } else {
                    int root = (e->parent_idx != -1) ? find_drag_window_ptr(e->parent_idx) : hit;
                    drag_target_idx = root;
                    float bx, by, bw, bh;
                    get_element_draw_bounds(&elements[drag_target_idx], &bx, &by, &bw, &bh);
                    drag_offset_x = (float)mx - bx;
                    drag_offset_y = (float)my - by;
                }
            }
        }
    } else if (action == LUNA_RELEASE && button == LUNA_MOUSE_BUTTON_LEFT) {
        int hit = hit_test_at(mx, my);

        for (int i = 0; i < elem_count; i++) {
            if (elements[i].is_active) {
                elements[i].is_active = 0;
                int still_over = hit == i;
                if (still_over && elements[i].on_click && !g_drag_moved) {
                    g_luna_last_click_button = LUNA_MOUSE_BUTTON_LEFT;
                    elements[i].on_click(&elements[i]);
                }
                update_element_style(&elements[i]);
            }
        }

        if (!g_drag_moved) {
            int trap = get_focus_trap_root();
            if (trap != -1 && hit == trap) {
                for (int ti = g_focus_trap_count - 1; ti >= 0; ti--) {
                    if (g_focus_traps[ti].idx != trap) continue;
                    if (!g_focus_traps[ti].backdrop_dismiss) break;
                    if (g_focus_traps[ti].on_dismiss)
                        g_focus_traps[ti].on_dismiss(trap);
                    break;
                }
            }
        }

        if (g_mouse_release_hook)
            g_mouse_release_hook(hit, g_drag_moved);

        drag_target_idx = -1;
        g_drag_mode = 0;
        g_scroll_drag_idx = -1;
        g_scroll_drag_axis = 0;
        recompute_hover(window, mx, my);
    } else if (action == LUNA_RELEASE && button == LUNA_MOUSE_BUTTON_RIGHT) {
        /* Right-click: fire on_click on the hit element (or an ancestor that
         * has a handler) so shells can open context menus. */
        int hit = hit_test_at(mx, my);
        g_luna_last_click_button = LUNA_MOUSE_BUTTON_RIGHT;
        for (int i = hit; i != -1; i = elements[i].parent_idx) {
            if (elements[i].on_click) {
                elements[i].on_click(&elements[i]);
                break;
            }
        }
        if (g_mouse_release_hook)
            g_mouse_release_hook(hit, 0);
        recompute_hover(window, mx, my);
    }
}

static void __attribute__((unused)) glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

static void __attribute__((unused)) framebuffer_size_callback(void* window, int width, int height) {
    (void)window;
    (void)width;
    (void)height;
    g_layout_dirty = 1; g_render_order_dirty = 1;
}

static int element_is_inert(int idx) {
    if (idx == -1) return 0;
    if (elements[idx].inert) return 1;
    for (int p = elements[idx].parent_idx; p != -1; p = elements[p].parent_idx) {
        if (elements[p].inert) return 1;
    }
    int trap = get_focus_trap_root();
    if (trap != -1 && !is_descendant_of(idx, trap)) return 1;
    return 0;
}

static void focus_element(int idx) {
    focus_element_ex(idx, 0);
}

static void focus_element_ex(int idx, int via_keyboard) {
    if (idx != -1 && element_is_inert(idx)) return;
    int old = g_focused_element_idx;
    if (idx == g_focused_element_idx) {
        g_focus_via_keyboard = via_keyboard ? 1 : 0;
        if (idx != -1) update_element_style(&elements[idx]);
        return;
    }
    g_focused_element_idx = idx;
    g_focus_via_keyboard = via_keyboard ? 1 : 0;
    if (old != -1) update_focus_within_styles(old);
    if (idx != -1) {
        update_focus_within_styles(idx);
        scroll_into_view(idx);
    }
}

static int element_is_focusable(int idx) {
    if (element_is_inert(idx) || element_aria_hidden(idx)) return 0;
    LunaElement* e = &elements[idx];
    if (!is_visible(idx) || e->display_none || e->pointer_events_none) return 0;
    if (e->tabindex == -1) return 0;
    if (e->is_input) return 1;
    if (e->tabindex >= 0) return 1;
    if (e->role[0] && strcmp(e->role, "button") == 0) return 1;
    if (e->role[0] && strcmp(e->role, "combobox") == 0) return 1;
    if (strcasecmp(e->type, "button") == 0) return 1;
    if (strcasecmp(e->type, "input") == 0 || strcasecmp(e->type, "textarea") == 0) return 1;
    return e->on_click || e->cursor_pointer;
}

static int tab_order_cmp(const void* a, const void* b) {
    int ia = *(const int*)a;
    int ib = *(const int*)b;
    int ta = elements[ia].tabindex;
    int tb = elements[ib].tabindex;
    if (ta < 0) ta = 0;
    if (tb < 0) tb = 0;
    int pa = (elements[ia].tabindex > 0) ? 0 : 1;
    int pb = (elements[ib].tabindex > 0) ? 0 : 1;
    if (pa != pb) return pa - pb;
    if (pa == 0 && ta != tb) return ta - tb;
    return ia - ib;
}

static void focus_move_tab(int backward) {
    int trap = get_focus_trap_root();
    int order[MAX_ELEMENTS];
    int n = 0;
    for (int i = 0; i < elem_count; i++) {
        if (!element_is_focusable(i)) continue;
        if (trap != -1 && !is_descendant_of(i, trap)) continue;
        order[n++] = i;
    }
    if (n == 0) return;
    qsort(order, (size_t)n, sizeof(int), tab_order_cmp);

    int cur = -1;
    for (int i = 0; i < n; i++) {
        if (order[i] == g_focused_element_idx) { cur = i; break; }
    }
    int next;
    if (cur == -1) next = backward ? n - 1 : 0;
    else next = backward ? (cur - 1 + n) % n : (cur + 1) % n;
    focus_element_ex(order[next], 1);
}

static void focus_select_option_step(int backward) {
    int trap = get_focus_trap_root();
    if (trap == -1) return;
    if (element_has_class(&elements[trap], "hidden")) return;
    int options[MAX_ELEMENTS];
    int n = 0;
    for (int i = 0; i < elem_count; i++) {
        if (elements[i].parent_idx != trap) continue;
        if (!element_has_class(&elements[i], "select_option")) continue;
        if (!element_is_focusable(i)) continue;
        options[n++] = i;
    }
    if (n == 0) return;
    int cur = -1;
    for (int i = 0; i < n; i++)
        if (options[i] == g_focused_element_idx) { cur = i; break; }
    int next = (cur == -1) ? 0 : backward ? (cur - 1 + n) % n : (cur + 1) % n;
    focus_element_ex(options[next], 1);
}


// ============================================================
// Text input editing (IME commits arrive via luna_char)
// ============================================================

static int focused_is_input(void) {
    return g_focused_element_idx >= 0 &&
           g_focused_element_idx < elem_count &&
           elements[g_focused_element_idx].is_input;
}

static void input_ensure_caret(LunaElement* e) {
    int n = (int)strlen(e->text);
    if (e->caret < 0) e->caret = 0;
    if (e->caret > n) e->caret = n;
    /* Snap to UTF-8 boundary */
    while (e->caret > 0 && ((unsigned char)e->text[e->caret] & 0xC0) == 0x80)
        e->caret--;
}

static void input_insert_utf8(LunaElement* e, const char* bytes, int blen) {
    if (!e || !bytes || blen <= 0) return;
    input_ensure_caret(e);
    int n = (int)strlen(e->text);
    int cap = (int)sizeof(e->text) - 1;
    if (n + blen > cap) blen = cap - n;
    if (blen <= 0) return;
    memmove(e->text + e->caret + blen, e->text + e->caret, (size_t)(n - e->caret) + 1);
    memcpy(e->text + e->caret, bytes, (size_t)blen);
    e->caret += blen;
}

static void input_backspace(LunaElement* e) {
    input_ensure_caret(e);
    if (e->caret <= 0) return;
    int prev = utf8_prev_boundary(e->text, e->caret);
    int n = (int)strlen(e->text);
    memmove(e->text + prev, e->text + e->caret, (size_t)(n - e->caret) + 1);
    e->caret = prev;
}

static void input_delete_forward(LunaElement* e) {
    input_ensure_caret(e);
    int n = (int)strlen(e->text);
    if (e->caret >= n) return;
    int next = utf8_next_boundary(e->text, e->caret);
    memmove(e->text + e->caret, e->text + next, (size_t)(n - next) + 1);
}

static float measure_prefix_width(LunaElement* e, int byte_len) {
    if (!font_loaded || byte_len <= 0) return 0.0f;
    float css_px = e->font_size > 0.0f ? e->font_size : 16.0f;
    FontAtlas* atlas = get_atlas(css_px, e->font_bold, NULL);
    text_metrics_begin(css_px, e->font_bold, e->font_face, atlas);
    float result = 0.0f;
    if (e->input_password) {
        int n = 0;
        const char* p = e->text;
        const char* end = e->text + byte_len;
        while (p < end && *p) { (void)utf8_decode(&p); n++; }
        result = glyph_advance(atlas, (int)'*', css_px) * (float)n;
    } else {
        char tmp[512];
        if (byte_len >= (int)sizeof(tmp)) byte_len = (int)sizeof(tmp) - 1;
        memcpy(tmp, e->text, (size_t)byte_len);
        tmp[byte_len] = '\0';
        result = measure_text_width(atlas, tmp);
    }
    text_metrics_end();
    return result;
}

static void input_set_caret_from_x(LunaElement* e, float local_x) {
    if (!font_loaded) { e->caret = (int)strlen(e->text); return; }
    float css_px = e->font_size > 0.0f ? e->font_size : 16.0f;
    FontAtlas* atlas = get_atlas(css_px, e->font_bold, NULL);
    text_metrics_begin(css_px, e->font_bold, e->font_face, atlas);
    float x = local_x + e->input_scroll_x;
    const char* p = e->text;
    int best = 0;
    float best_d = 1e9f;
    float cx = 0.0f;
    int off = 0;
    while (*p) {
        float d = fabsf(cx - x);
        if (d < best_d) { best_d = d; best = off; }
        int cp = utf8_decode(&p);
        float adv = e->input_password
            ? glyph_advance(atlas, (int)'*', css_px)
            : glyph_advance(atlas, cp, css_px);
        cx += adv;
        off = (int)(p - e->text);
    }
    if (fabsf(cx - x) < best_d) best = off;
    e->caret = best;
    text_metrics_end();
}

static void input_update_scroll(LunaElement* e, float inner_w) {
    if (e->input_multiline) { e->input_scroll_x = 0.0f; return; }
    float caret_x = measure_prefix_width(e, e->caret);
    float pad = 2.0f;
    if (caret_x - e->input_scroll_x > inner_w - pad)
        e->input_scroll_x = caret_x - inner_w + pad;
    if (caret_x - e->input_scroll_x < pad)
        e->input_scroll_x = caret_x - pad;
    if (e->input_scroll_x < 0.0f) e->input_scroll_x = 0.0f;
}

static void char_callback_impl(unsigned int codepoint) {
    if (!focused_is_input()) return;
    if (codepoint < 32 && codepoint != '\n' && codepoint != '\t') return;
    LunaElement* e = &elements[g_focused_element_idx];
    if (!e->input_multiline && (codepoint == '\n' || codepoint == '\r')) return;
    char buf[8];
    int n = utf8_encode((int)codepoint, buf);
    input_insert_utf8(e, buf, n);
}

static void key_callback(void* window, int key, int scancode, int action, int mods) {
    (void)window; (void)scancode;
    if (action == LUNA_PRESS && key == LUNA_KEY_F12) {
        char path[512];
        time_t t = time(NULL);
        struct tm* tm_info = localtime(&t);
        if (tm_info && strftime(path, sizeof(path), "screenshot_%Y%m%d_%H%M%S.png", tm_info) > 0)
            take_screenshot(path);
        else
            take_screenshot("screenshot.png");
        return;
    }

    if (action == LUNA_PRESS && key == LUNA_KEY_ESCAPE) {
        int trap = get_focus_trap_root();
        if (trap != -1) {
            LunaTrapDismissFn fn = get_trap_dismiss_fn(trap);
            if (fn) fn(trap);
            return;
        }
        /* ESC dismisses traps/overlays only — quit is Ctrl+Alt+Backspace. */
        return;
    }

    /* Classic Linux graphical-session quit chord (was Xorg's Zap). */
    if (action == LUNA_PRESS && key == LUNA_KEY_BACKSPACE &&
        (mods & LUNA_MOD_CONTROL) && (mods & LUNA_MOD_ALT)) {
        if (g_luna_platform.request_close) g_luna_platform.request_close();
        return;
    }

    if (action == LUNA_PRESS && key == LUNA_KEY_TAB) {
        focus_move_tab((mods & LUNA_MOD_SHIFT) != 0);
        return;
    }

    if ((action == LUNA_PRESS || action == LUNA_REPEAT) &&
        (key == LUNA_KEY_UP || key == LUNA_KEY_DOWN)) {
        int trap = get_focus_trap_root();
        if (trap != -1) {
            int has_options = 0;
            for (int i = 0; i < elem_count; i++) {
                if (elements[i].parent_idx != trap) continue;
                if (!element_has_class(&elements[i], "select_option")) continue;
                has_options = 1;
                break;
            }
            if (has_options) {
                focus_select_option_step(key == LUNA_KEY_UP);
                return;
            }
        }
    }

    /* Text field editing — consume keys so scroll/button shortcuts don't fight IME */
    if (focused_is_input() && (action == LUNA_PRESS || action == LUNA_REPEAT)) {
        LunaElement* fe = &elements[g_focused_element_idx];
        if (key == LUNA_KEY_BACKSPACE) { input_backspace(fe); return; }
        if (key == LUNA_KEY_DELETE) { input_delete_forward(fe); return; }
        if (key == LUNA_KEY_LEFT) {
            fe->caret = utf8_prev_boundary(fe->text, fe->caret);
            input_ensure_caret(fe); return;
        }
        if (key == LUNA_KEY_RIGHT) {
            fe->caret = utf8_next_boundary(fe->text, fe->caret);
            input_ensure_caret(fe); return;
        }
        if (key == LUNA_KEY_HOME) { fe->caret = 0; return; }
        if (key == LUNA_KEY_END) { fe->caret = (int)strlen(fe->text); return; }
        if (key == LUNA_KEY_ENTER || key == LUNA_KEY_KP_ENTER) {
            if (fe->input_multiline) { char_callback_impl('\n'); return; }
            if (fe->on_click) fe->on_click(fe);
            return;
        }
        if (key == LUNA_KEY_SPACE) return; /* composition / char callback handles space */
        /* Let other keys fall through only for non-editable combos */
    }

    if (action == LUNA_PRESS &&
        (key == LUNA_KEY_ENTER || key == LUNA_KEY_KP_ENTER || key == LUNA_KEY_SPACE)) {
        if (g_focused_element_idx != -1) {
            LunaElement* fe = &elements[g_focused_element_idx];
            if (fe->is_input) return;
            if (fe->on_click) {
                fe->is_active = 1;
                update_element_style(fe);
                fe->on_click(fe);
                fe->is_active = 0;
                update_element_style(fe);
            }
        }
        return;
    }

    if (action != LUNA_PRESS && action != LUNA_REPEAT) return;

    int start = g_focused_element_idx;
    if (start == -1) {
        double mx = g_luna_mx, my = g_luna_my;
        start = hit_test_at(mx, my);
    }
    if (start == -1) return;

    int sy = find_scroll_target_y(start);
    int sx = find_scroll_target_x(start);
    float line = 20.0f;

    if (sy != -1 && (key == LUNA_KEY_UP || key == LUNA_KEY_DOWN ||
                     key == LUNA_KEY_PAGE_UP || key == LUNA_KEY_PAGE_DOWN ||
                     key == LUNA_KEY_HOME || key == LUNA_KEY_END)) {
        LunaElement* sc = &elements[sy];
        float pad = sc->padding;
        float inner_h = sc->h - pad * 2.0f;
        float max_scroll = sc->scroll_content_h - inner_h;
        if (max_scroll < 0.0f) max_scroll = 0.0f;
        float page = inner_h * 0.85f;
        if (key == LUNA_KEY_UP) add_scroll_top(sy, -line, 0);
        else if (key == LUNA_KEY_DOWN) add_scroll_top(sy, line, 0);
        else if (key == LUNA_KEY_PAGE_UP) add_scroll_top(sy, -page, 0);
        else if (key == LUNA_KEY_PAGE_DOWN) add_scroll_top(sy, page, 0);
        else if (key == LUNA_KEY_HOME) set_scroll_top(sy, 0.0f, 0);
        else if (key == LUNA_KEY_END) set_scroll_top(sy, max_scroll, 0);
        return;
    }

    if (sx != -1 && (key == LUNA_KEY_LEFT || key == LUNA_KEY_RIGHT ||
                     ((mods & LUNA_MOD_SHIFT) &&
                      (key == LUNA_KEY_PAGE_UP || key == LUNA_KEY_PAGE_DOWN)))) {
        LunaElement* sc = &elements[sx];
        float pad = sc->padding;
        float inner_w = sc->w - pad * 2.0f;
        float max_scroll = sc->scroll_content_w - inner_w;
        if (max_scroll < 0.0f) max_scroll = 0.0f;
        float page = inner_w * 0.85f;
        if (key == LUNA_KEY_LEFT) add_scroll_left(sx, -line, 0);
        else if (key == LUNA_KEY_RIGHT) add_scroll_left(sx, line, 0);
        else if (key == LUNA_KEY_PAGE_UP) add_scroll_left(sx, -page, 0);
        else if (key == LUNA_KEY_PAGE_DOWN) add_scroll_left(sx, page, 0);
        (void)mods;
    }
}

// ============================================================
// GL setup
// ============================================================

static GLuint compile_shader(const char* src, GLenum type) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    return shader;
}

static void load_gl_functions() {
#define LOAD(T, name) name = (T)g_luna_platform.get_proc(#name)
    LOAD(PFNGLCREATEVERTEXARRAYSPROC, glCreateVertexArrays);
    /* GL 4.5 DSA fallback: glGenVertexArrays has identical signature */
    if (!glCreateVertexArrays)
        glCreateVertexArrays = (PFNGLCREATEVERTEXARRAYSPROC)
            g_luna_platform.get_proc("glGenVertexArrays");
    LOAD(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays);
    LOAD(PFNGLGENBUFFERSPROC,         glGenBuffers);
    LOAD(PFNGLBINDBUFFERPROC,         glBindBuffer);
    LOAD(PFNGLBUFFERDATAPROC,         glBufferData);
    LOAD(PFNGLBUFFERSUBDATAPROC,      glBufferSubData);
    LOAD(PFNGLVERTEXATTRIBPOINTERPROC,    glVertexAttribPointer);
    LOAD(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray);
    LOAD(PFNGLDELETEBUFFERSPROC,      glDeleteBuffers);
    LOAD(PFNGLBINDVERTEXARRAYPROC,    glBindVertexArray);
    LOAD(PFNGLCREATESHADERPROC,       glCreateShader);
    LOAD(PFNGLSHADERSOURCEPROC,       glShaderSource);
    LOAD(PFNGLCOMPILESHADERPROC,      glCompileShader);
    LOAD(PFNGLCREATEPROGRAMPROC,      glCreateProgram);
    LOAD(PFNGLATTACHSHADERPROC,       glAttachShader);
    LOAD(PFNGLLINKPROGRAMPROC,        glLinkProgram);
    LOAD(PFNGLUSEPROGRAMPROC,         glUseProgram);
    LOAD(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation);
    LOAD(PFNGLUNIFORM4FPROC,          glUniform4f);
    LOAD(PFNGLUNIFORM2FPROC,          glUniform2f);
    LOAD(PFNGLUNIFORM1FPROC,          glUniform1f);
    glUniform1i_ = (PFNGLUNIFORM1IPROC)g_luna_platform.get_proc("glUniform1i");
    glActiveTexture_ = (PFNGLACTIVETEXTUREPROC)g_luna_platform.get_proc("glActiveTexture");
    glGenFramebuffers_       = (PFNGLGENFRAMEBUFFERSPROC)g_luna_platform.get_proc("glGenFramebuffers");
    glBindFramebuffer_       = (PFNGLBINDFRAMEBUFFERPROC)g_luna_platform.get_proc("glBindFramebuffer");
    glFramebufferTexture2D_  = (PFNGLFRAMEBUFFERTEXTURE2DPROC)g_luna_platform.get_proc("glFramebufferTexture2D");
    glDeleteFramebuffers_    = (PFNGLDELETEFRAMEBUFFERSPROC)g_luna_platform.get_proc("glDeleteFramebuffers");
    glCheckFramebufferStatus_ = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)g_luna_platform.get_proc("glCheckFramebufferStatus");

    /* GL 1.0/1.1 core entry points.  The macros above redirect each name to a
     * luna_p_* pointer, so `name` on the LHS resolves to the pointer while the
     * stringized #name still passes the real GL symbol name to get_proc(). */
    LOAD(LUNA_PFNGLVIEWPORTPROC,          glViewport);
    LOAD(LUNA_PFNGLCLEARPROC,             glClear);
    LOAD(LUNA_PFNGLCLEARCOLORPROC,        glClearColor);
    LOAD(LUNA_PFNGLENABLEPROC,            glEnable);
    LOAD(LUNA_PFNGLDISABLEPROC,           glDisable);
    LOAD(LUNA_PFNGLBLENDFUNCPROC,         glBlendFunc);
    LOAD(LUNA_PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate);
    LOAD(LUNA_PFNGLSCISSORPROC,           glScissor);
    LOAD(LUNA_PFNGLDRAWARRAYSPROC,        glDrawArrays);
    LOAD(LUNA_PFNGLGENTEXTURESPROC,       glGenTextures);
    LOAD(LUNA_PFNGLDELETETEXTURESPROC,    glDeleteTextures);
    LOAD(LUNA_PFNGLBINDTEXTUREPROC,       glBindTexture);
    LOAD(LUNA_PFNGLTEXPARAMETERIPROC,     glTexParameteri);
    LOAD(LUNA_PFNGLTEXIMAGE2DPROC,        glTexImage2D);
    LOAD(LUNA_PFNGLTEXSUBIMAGE2DPROC,     glTexSubImage2D);
    LOAD(LUNA_PFNGLCOPYTEXSUBIMAGE2DPROC, glCopyTexSubImage2D);
    LOAD(LUNA_PFNGLREADPIXELSPROC,        glReadPixels);
#undef LOAD

    /* If the core entry points could not be resolved through the EGL/GLX
     * dispatch, fail loudly here instead of letting the first draw call jump
     * through a NULL pointer (or, worse, through libGL's un-current dispatch
     * and crash inside the driver with a confusing backtrace). */
    if (!luna_p_glClear || !luna_p_glViewport || !luna_p_glTexImage2D ||
        !luna_p_glGenTextures || !luna_p_glDrawArrays ||
        !luna_p_glBlendFuncSeparate) {
        fprintf(stderr, "[luna-ui] fatal: could not resolve core GL 1.x entry points "
                        "via get_proc(); the GL/EGL library stack is inconsistent\n");
        return;
    }
}

/* ── Public wrappers ── */
int luna_element_count(void) { return elem_count; }
LunaElement* luna_element_at(int i) { return (i >= 0 && i < elem_count) ? &elements[i] : NULL; }
int luna_get_element_by_id(const char* id) { return get_element_by_id(id); }
int luna_focused_element(void) { return g_focused_element_idx; }
void luna_focus_element(int idx) { if (idx >= 0 && idx < elem_count) focus_element(idx); }
int luna_element_visible(int idx) { return (idx >= 0 && idx < elem_count) ? is_visible(idx) : 0; }
void luna_set_text(int i, const char* t) { g_probe_prepared = 0; set_text(i, t); }
void luna_add_class(int i, const char* c) { g_probe_prepared = 0; if (i >= 0 && i < elem_count) add_class(&elements[i], c); }
void luna_remove_class(int i, const char* c) { g_probe_prepared = 0; if (i >= 0 && i < elem_count) remove_class(&elements[i], c); }
int luna_update_classes(int i, const char* remove_classes, const char* add_classes) {
    if (i < 0 || i >= elem_count) return 0;
    g_probe_prepared = 0;
    return update_classes_internal(&elements[i], remove_classes, add_classes);
}
void luna_mark_visual_dirty(int i) {
    if (i < 0 || i >= elem_count) return;
    g_probe_prepared = 0;
    visual_activate_idx(i);
}
void luna_update_element_style(int i) { g_probe_prepared = 0; if (i >= 0 && i < elem_count) update_element_style(&elements[i]); }
void luna_register_js_handler(const char* n, LunaEventHandler fn) { register_js_handler(n, fn); }
void luna_set_on_click(int i, LunaEventHandler fn) { set_on_click(i, fn); }
void luna_set_html_base_dir(const char* p) { set_html_base_dir(p); }
int luna_load_html_file(const char* p) { char* s = read_file(p); if (!s) return 0; parse_html(s); free(s); return 1; }
int luna_load_css_file(const char* p) {
    char* s = read_file(p);
    if (!s) return 0;
    parse_css(s);
    free(s);
    if (elem_count > 0) {
        for (int i = 0; i < elem_count; i++) update_element_style(&elements[i]);
        generate_pseudo_elements();
        g_layout_dirty = 1; g_render_order_dirty = 1;
    }
    return 1;
}
void luna_reset_css(void) {
    rule_count = 0;
    g_keyframe_count = 0;
    g_css_from_document = 0;
    memset(css_rules, 0, sizeof(css_rules));
    memset(g_keyframes, 0, sizeof(g_keyframes));
}
void luna_parse_html(const char* h) { g_probe_prepared = 0; parse_html(h); }
void luna_parse_css(const char* c) {
    g_probe_prepared = 0;
    parse_css(c);
    if (elem_count > 0) {
        for (int i = 0; i < elem_count; i++) update_element_style(&elements[i]);
        generate_pseudo_elements();
        g_layout_dirty = 1; g_render_order_dirty = 1;
    }
}
void luna_wire_onclick_handlers(void) { wire_element_onclick_handlers(); }

void luna_push_focus_trap(int idx, LunaTrapDismissFn on_dismiss, int backdrop_dismiss) {
    if (idx < 0 || g_focus_trap_count >= LUNA_MAX_FOCUS_TRAPS) return;
    if (g_focus_trap_count == 0)
        g_focus_before_trap = g_focused_element_idx;
    g_focus_traps[g_focus_trap_count].idx = idx;
    g_focus_traps[g_focus_trap_count].on_dismiss = on_dismiss;
    g_focus_traps[g_focus_trap_count].backdrop_dismiss = backdrop_dismiss;
    g_focus_trap_count++;
}

void luna_pop_focus_trap(int idx) {
    for (int i = g_focus_trap_count - 1; i >= 0; i--) {
        if (g_focus_traps[i].idx != idx) continue;
        for (int j = i; j < g_focus_trap_count - 1; j++)
            g_focus_traps[j] = g_focus_traps[j + 1];
        g_focus_trap_count--;
        if (g_focus_trap_count == 0) {
            if (g_focus_before_trap != -1) {
                focus_element(g_focus_before_trap);
                g_focus_before_trap = -1;
            }
        }
        return;
    }
}

void luna_set_mouse_release_hook(LunaMouseReleaseHook fn) { g_mouse_release_hook = fn; }
int luna_last_click_button(void) { return g_luna_last_click_button; }
int luna_last_click_mods(void) { return g_luna_last_click_mods; }
void luna_resize(float w, float h) {
    if (w == luna_window_width && h == luna_window_height) return;
    g_probe_prepared = 0;
    luna_window_width = w; luna_window_height = h; g_layout_dirty = 1; g_render_order_dirty = 1;
    g_visual_scan_needed = 1;
}
void luna_mark_layout_dirty(void) {
    g_probe_prepared = 0; g_layout_dirty = 1; g_render_order_dirty = 1;
    g_visual_scan_needed = 1;
}
void luna_framebuffer_resized(void) {
    g_probe_prepared = 0; g_layout_dirty = 1; g_render_order_dirty = 1;
    g_visual_scan_needed = 1;
}
void luna_take_screenshot(const char* path) { take_screenshot(path); }
void luna_flush_pending_screenshot(void) {
    if (g_screenshot_pending && g_screenshot_path[0]) {
        /* Let finite CSS entrance animations reach their browser-equivalent
         * final state.  Infinite animations deliberately do not delay a shot. */
        if (css_animations_are_settling(luna_now())) return;
        take_screenshot(g_screenshot_path);
        g_screenshot_pending = 0;
    }
}
void luna_request_screenshot(const char* p) { strncpy(g_screenshot_path, p, sizeof(g_screenshot_path)-1); g_screenshot_pending = 1; }

void luna_inject_body_background(void) {
    if (elem_count >= MAX_ELEMENTS) return;
    /* parse_html may already have materialized <body> as a real element */
    for (int i = 0; i < elem_count; i++)
        if (strcmp(elements[i].type, "body") == 0) return;
    LunaElement body_e;
    memset(&body_e, 0, sizeof(body_e));
    strncpy(body_e.type, "body", 31);
    body_e.id_idx = elem_count;
    body_e.parent_idx = -1;
    body_e.z_index = -9999;
    body_e.pct_w = 1; body_e.raw_w = 1.0f;
    body_e.pct_h = 1; body_e.raw_h = 1.0f;
    body_e.w = luna_window_width; body_e.h = luna_window_height;
    body_e.opacity = 1.0f;
    body_e.transform_scale = 1.0f;
    body_e.cur_scale = 1.0f;
    body_e.anim_speed = 14.0f;
    body_e.pointer_events_none = 1;
    elements[elem_count] = body_e;
    update_element_style(&elements[elem_count]);
    /* update_element_style resets engine fields — restore backdrop role. */
    elements[elem_count].pct_w = 1; elements[elem_count].raw_w = 1.0f;
    elements[elem_count].pct_h = 1; elements[elem_count].raw_h = 1.0f;
    elements[elem_count].z_index = -9999;
    elements[elem_count].pointer_events_none = 1;
    LunaElement* ne = &elements[elem_count];
    /* update_element_style resets z_index to 0, which would paint the body
       OVER earlier-parsed root elements (body is appended last). Keep it
       behind everything. */
    ne->z_index = -9999;
    ne->cur_r = ne->r; ne->cur_g = ne->g; ne->cur_b = ne->b; ne->cur_a = ne->a;
    elem_count++;
    g_render_order_dirty = 1;
}

static void luna_update_prepare(double now, double dt) {
    g_probe_prepared = 0;
    rebuild_activity_registries();
    tick_smooth_scroll(dt);
    if (g_layout_dirty) {
        update_layout_pass();
        g_layout_dirty = 0;
        g_visual_scan_needed = 1;
    }
    update_css_keyframe_animations(now);
}

void luna_update(double now, double dt) {
    luna_update_prepare(now, dt);
    (void)update_animations_internal(dt, NULL, 0, NULL);
}

int luna_update_settling(double now, double dt) {
    luna_update_prepare(now, dt);
    return update_animations_internal(dt, NULL, 0, NULL);
}

int luna_update_settling_mask(double now, double dt,
                              const int* roots, int nroots, unsigned* out_mask) {
    luna_update_prepare(now, dt);
    return update_animations_internal(dt, roots, nroots, out_mask);
}

/* Ensure backdrop-blur FBOs/textures can hold the current framebuffer.
 *
 * Sizing these to match the framebuffer *exactly* was a per-frame disaster on
 * the Wayland shell: every layer surface has its own size, so rendering the
 * menubar after the wallpaper tore down two full-screen RGBA textures and two
 * FBOs and allocated them again — and then the wallpaper's next repaint did it
 * all over again, in the opposite direction.  At 1920x1080 that is 16 MB of
 * driver allocation twice per frame, produced on a fixed timer by the clock and
 * the wallpaper tick: exactly the regular hitch it looked like.
 *
 * Grow-only sizing removes it.  A texture larger than the framebuffer costs
 * nothing to sample from — the capture is anchored at texture y=0 and the
 * shaders normalise by the real texture size — so the allocation happens once,
 * for the largest surface, and is reused by every smaller one. */
static void ensure_blur_fbos(int w, int h) {
    if (!glGenFramebuffers_ || !glBindFramebuffer_ || !glFramebufferTexture2D_) return;
    if (g_blur_fbo[0] && g_blur_tex_w >= w && g_blur_tex_h >= h) return;
    if (w < g_blur_tex_w) w = g_blur_tex_w;
    if (h < g_blur_tex_h) h = g_blur_tex_h;
    /* Clean up old resources */
    if (g_blur_fbo[0] && glDeleteFramebuffers_) {
        glDeleteFramebuffers_(2, g_blur_fbo);
        g_blur_fbo[0] = g_blur_fbo[1] = 0;
    }
    if (g_blur_tex[0]) { glDeleteTextures(2, g_blur_tex); g_blur_tex[0] = g_blur_tex[1] = 0; }
    g_blur_tex_w = w; g_blur_tex_h = h;
    /* Create two ping-pong textures */
    glGenTextures(2, g_blur_tex);
    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, g_blur_tex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    /* Create two FBOs, each backed by one texture */
    glGenFramebuffers_(2, g_blur_fbo);
    for (int i = 0; i < 2; i++) {
        glBindFramebuffer_(GL_FRAMEBUFFER, g_blur_fbo[i]);
        glFramebufferTexture2D_(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_blur_tex[i], 0);
    }
    glBindFramebuffer_(GL_FRAMEBUFFER, 0);
    g_current_program = 0; /* state was disrupted */
}

/* Apply backdrop blur for one element: capture current FBO, blur it, draw result. */
static void apply_backdrop_blur(float ex, float ey, float ew, float eh,
                                 float blur_radius, float saturate, float brightness,
                                 const float* rad4, int fbw, int fbh) {
    if (!blur_program || !backdrop_program) return;
    if (!glActiveTexture_) return;
    /* Allocated here rather than at the top of every render: a surface with no
     * backdrop-filter element never needs the capture textures at all. */
    ensure_blur_fbos(fbw, fbh);
    if (!g_blur_fbo[0]) return;

    /* fw/fh drive the viewport and the screen→GL y flip; tw/th normalise UVs
     * into the (possibly larger) capture texture. */
    float fw = (float)fbw, fh = (float)fbh;
    float tw = (float)g_blur_tex_w, th = (float)g_blur_tex_h;
    float sx = ex - g_render_off_x, sy = ey - g_render_off_y;

    /* Every stage below works on the element rect grown by the blur reach
     * instead of the whole screen.  A menubar with backdrop-filter used to
     * cost a full-framebuffer texture copy plus two full-framebuffer blur
     * passes *per blurred element, per frame*; the visible result is identical
     * because nothing outside this region can influence the element's pixels. */
    float r   = blur_radius > 0.5f ? blur_radius : 0.5f;
    float pad = r + 2.0f;

    /* Capture region in document coordinates (y grows downward), clipped. */
    int cx0 = (int)floorf(sx - pad);            if (cx0 < 0) cx0 = 0;
    int cx1 = (int)ceilf(sx + ew + pad);        if (cx1 > fbw) cx1 = fbw;
    int cy0 = (int)floorf(sy - pad);            if (cy0 < 0) cy0 = 0;
    int cy1 = (int)ceilf(sy + eh + pad);        if (cy1 > fbh) cy1 = fbh;
    if (cx1 <= cx0 || cy1 <= cy0) return;

    /* Step 1: Copy just that region of the default framebuffer into
     * g_blur_tex[0].  Both the copy and the sample anchor the framebuffer at
     * texture y=0, so the sub-rect lands at the texel coordinates the shaders
     * read — GL window space counts y upward, hence the flip. */
    glBindTexture(GL_TEXTURE_2D, g_blur_tex[0]);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, cx0, fbh - cy1, cx0, fbh - cy1,
                        cx1 - cx0, cy1 - cy0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glDisable(GL_SCISSOR_TEST);
    rc_scissor_reset();
    glDisable(GL_BLEND);
    /* Step 2: Horizontal blur: read g_blur_tex[0] → write g_blur_fbo[1]/g_blur_tex[1].
     * The vertical pass samples r pixels above and below the element, so the
     * horizontal pass must produce that taller strip.  It used to write only
     * the element rect, leaving the vertical pass to read whatever an earlier
     * element or frame had left in g_blur_tex[1] — visible as a wrong band
     * along the top and bottom edge of every backdrop-filter surface. */
    float hy0 = sy - pad;           if (hy0 < 0.0f)        hy0 = 0.0f;
    float hy1 = sy + eh + pad;      if (hy1 > (float)fbh)  hy1 = (float)fbh;
    glBindFramebuffer_(GL_FRAMEBUFFER, g_blur_fbo[1]);
    glViewport(0, 0, fbw, fbh);
    luna_use_program(blur_program);
    glUniform2f(blur_loc.uResolution, fw, fh);
    glUniform2f(blur_loc.uPos,  sx, hy0);
    glUniform2f(blur_loc.uSize, ew, hy1 - hy0);
    if (glActiveTexture_) glActiveTexture_(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_blur_tex[0]);
    glUniform1i_(blur_loc.uSrc, 0);
    glUniform2f(blur_loc.uBlurDir, 1.0f / tw, 0.0f);
    glUniform1f(blur_loc.uBlurRadius, blur_radius);
    glUniform2f(blur_loc.uBlurTexSize, tw, th);
    glUniform2f(blur_loc.uFbSize, fw, fh);
    glUniform2f(blur_loc.uBlurOrigin, sx, hy0);
    luna_bind_vao(g_rect_vao);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    /* Step 3: Vertical blur over the element rect only: read g_blur_tex[1] →
     * write g_blur_fbo[0]/g_blur_tex[0] */
    glBindFramebuffer_(GL_FRAMEBUFFER, g_blur_fbo[0]);
    glBindTexture(GL_TEXTURE_2D, g_blur_tex[1]);
    glUniform2f(blur_loc.uPos,  sx, sy);
    glUniform2f(blur_loc.uSize, ew, eh);
    glUniform2f(blur_loc.uBlurOrigin, sx, sy);
    glUniform2f(blur_loc.uBlurDir, 0.0f, 1.0f / th);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindTexture(GL_TEXTURE_2D, 0);

    /* Step 4: Restore default FBO */
    glBindFramebuffer_(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, fbw, fbh);
    glEnable(GL_BLEND);
    /* EGL/Wayland color buffers use premultiplied alpha.  RGB needs the
     * ordinary straight-source factors to produce premultiplied output, while
     * alpha must use SRC + DST*(1-SRC), not SRC*SRC. */
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                        GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    g_current_program = 0; /* framebuffer switch may reset state */

    /* Step 5: Draw the blurred texture clipped to the element's rounded rect */
    float c4[4] = {0,0,0,0};
    float half_min = (ew < eh ? ew : eh) * 0.5f;
    if (rad4) {
        for (int i = 0; i < 4; i++) {
            c4[i] = rad4[i];
            if (c4[i] > half_min) c4[i] = half_min;
            if (c4[i] < 0.0f) c4[i] = 0.0f;
        }
    }
    luna_use_program(backdrop_program);
    glUniform2f(backdrop_loc.uResolution, LUNA_RRES_X, LUNA_RRES_Y);
    glUniform2f(backdrop_loc.uPos,  ex - g_render_off_x, ey - g_render_off_y);
    glUniform2f(backdrop_loc.uSize, ew, eh);
    glUniform4f(backdrop_loc.uRadius4, c4[0], c4[1], c4[2], c4[3]);
    if (glActiveTexture_) glActiveTexture_(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_blur_tex[0]);
    glUniform1i_(backdrop_loc.uSrc, 0);
    glUniform2f(backdrop_loc.uBlurTexSize, tw, th);
    glUniform2f(backdrop_loc.uFbSize, fw, fh);
    glUniform2f(backdrop_loc.uBlurOrigin, ex - g_render_off_x, ey - g_render_off_y);
    glUniform1f(backdrop_loc.uSaturate, saturate);
    glUniform1f(backdrop_loc.uBrightness, brightness);
    luna_bind_vao(g_rect_vao);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
}

static int sticky_is_stuck_in_scroll(int idx);
static void repaint_stuck_sticky_layers(int fbw, int fbh);

int luna_probe_damage(int root_idx, int fbw, int fbh,
                      float origin_x, float origin_y,
                      float region_w, float region_h) {
    g_probe_prepared = 0;
    if (!g_dmg_enabled) return 1;
    if (fbw <= 0 || fbh <= 0) return 0;
    int saved_root = g_render_root;
    float saved_ox = g_render_off_x, saved_oy = g_render_off_y;
    float saved_rx = g_render_res_x, saved_ry = g_render_res_y;
    int saved_fbw = g_luna_fbw, saved_fbh = g_luna_fbh;
    g_render_root = root_idx;
    g_render_off_x = origin_x;
    g_render_off_y = origin_y;
    g_render_res_x = region_w > 0.0f ? region_w : (float)fbw;
    g_render_res_y = region_h > 0.0f ? region_h : (float)fbh;
    g_luna_fbw = fbw;
    g_luna_fbh = fbh;
    build_render_order();
    rc_build();
    int changed = 0;
    /* Elements that were drawn last time in this root but are gone now still
     * count.  Records belonging to another Wayland layer root must be ignored:
     * treating !in_root as damage made every multi-surface probe report a
     * change after any other layer had rendered, defeating identical-frame
     * skipping and reintroducing periodic full-surface redraws. */
    for (int i = 0; i < elem_count; i++) {
        if (!g_draw_rec[i].drawn || !g_rc[i].in_root) continue;
        if (!rc_is_rendered(i) || g_rc[i].eff_op <= 0.004f) {
            changed = 1;
            break;
        }
    }
    if (!changed) {
        for (int ri = 0; ri < elem_count; ri++) {
            int i = render_order[ri];
            if (!g_rc[i].in_root) continue;
            if (!rc_is_rendered(i)) {
                if (damage_would_drop(i)) { changed = 1; break; }
                continue;
            }
            float eff_op = g_rc[i].eff_op;
            if (eff_op <= 0.004f) {
                if (damage_would_drop(i)) { changed = 1; break; }
                continue;
            }
            float dx, dy, dw, dh;
            rc_element_draw_bounds(i, &dx, &dy, &dw, &dh);
            if (dw <= 0.0f || dh <= 0.0f) {
                if (damage_would_drop(i)) { changed = 1; break; }
                continue;
            }
            LunaElement* e = &elements[i];
            float pad = 4.0f;
            if (e->has_shadow) {
                for (int s = 0; s < e->shadow_count; s++) {
                    float ext = e->shadows[s].blur * 1.75f +
                                fabsf(e->shadows[s].dx) + fabsf(e->shadows[s].dy) +
                                e->shadows[s].spread + 2.0f;
                    if (ext > pad) pad = ext;
                }
            }
            float sdx = dx - g_render_off_x, sdy = dy - g_render_off_y;
            if (sdx - pad > LUNA_RRES_X || sdy - pad > LUNA_RRES_Y ||
                sdx + dw + pad < 0.0f || sdy + dh + pad < 0.0f) {
                if (damage_would_drop(i)) { changed = 1; break; }
                continue;
            }
            float dmg_pad = pad;
            if (e->has_outline) {
                float ring = e->outline_width + e->outline_offset + 2.0f;
                if (ring > dmg_pad) dmg_pad = ring;
            }
            if (damage_would_change(i, sdx - dmg_pad, sdy - dmg_pad,
                                    dw + dmg_pad * 2.0f, dh + dmg_pad * 2.0f)) {
                changed = 1;
                break;
            }
        }
    }
    g_render_root = saved_root;
    g_render_off_x = saved_ox;
    g_render_off_y = saved_oy;
    g_render_res_x = saved_rx;
    g_render_res_y = saved_ry;
    g_luna_fbw = saved_fbw;
    g_luna_fbh = saved_fbh;
    if (changed) {
        g_probe_prepared = 1;
        g_probe_root = root_idx;
        g_probe_fbw = fbw; g_probe_fbh = fbh;
        g_probe_ox = origin_x; g_probe_oy = origin_y;
        g_probe_rw = region_w > 0.0f ? region_w : (float)fbw;
        g_probe_rh = region_h > 0.0f ? region_h : (float)fbh;
    }
    return changed;
}

void luna_invalidate_gl_state(void) {
    /* A host may paint with its own shaders/VAOs between Luna passes.  The
     * cached IDs then describe Luna's last request rather than the driver's
     * current state.  Force the next primitive to bind its real objects. */
    g_current_program = 0;
    g_current_vao = 0;

    /* These states are intentionally reasserted instead of cached.  In
     * particular, a leaked scissor would make the following glClear() or the
     * first UI boxes affect only the editor rectangle, producing a text-only
     * frame on the next caret-blink repaint. */
    glDisable(GL_SCISSOR_TEST);
    rc_scissor_reset();
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                        GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

void luna_render(int fbw, int fbh) {
    luna_invalidate_gl_state();
    g_luna_fbw = fbw;
    g_luna_fbh = fbh;
    /* The backdrop-blur capture textures are allocated by the first element
     * that actually uses backdrop-filter — see apply_backdrop_blur().  Sizing
     * them here forced a full reallocation every time a differently sized layer
     * surface was rendered, which on the Wayland shell is every frame. */
    int reuse_probe = g_probe_prepared && !g_render_order_dirty &&
        g_probe_root == g_render_root && g_probe_fbw == fbw && g_probe_fbh == fbh &&
        g_probe_ox == g_render_off_x && g_probe_oy == g_render_off_y &&
        g_probe_rw == LUNA_RRES_X && g_probe_rh == LUNA_RRES_Y;
    g_probe_prepared = 0;
    build_render_order();
    /* Consume the exact cache built by an immediately preceding probe. */
    if (!reuse_probe) rc_build();
    damage_reset();
    glDisable(GL_SCISSOR_TEST);
    rc_scissor_reset();
    g_bg_clip_enabled = 0;
    for (int ri = 0; ri < elem_count; ri++) {
        int i = render_order[ri];
        LunaElement* e = &elements[i];
        if (!g_rc[i].in_root) continue;
        if (!rc_is_rendered(i)) { damage_drop(i); continue; }
        float eff_op = g_rc[i].eff_op;
        if (eff_op <= 0.004f) { damage_drop(i); continue; }
        float dx, dy, dw, dh;
        rc_element_draw_bounds(i, &dx, &dy, &dw, &dh);
        if (dw <= 0.0f || dh <= 0.0f) { damage_drop(i); continue; }
        float scale = e->cur_scale;
        /* Viewport culling: skip elements fully off the surface region. */
        {
            float pad = 4.0f;
            if (e->has_shadow) {
                for (int s = 0; s < e->shadow_count; s++) {
                    float ext = e->shadows[s].blur * 1.75f +
                                fabsf(e->shadows[s].dx) + fabsf(e->shadows[s].dy) +
                                e->shadows[s].spread + 2.0f;
                    if (ext > pad) pad = ext;
                }
            }
            float sdx = dx - g_render_off_x, sdy = dy - g_render_off_y;
            if (sdx - pad > LUNA_RRES_X || sdy - pad > LUNA_RRES_Y ||
                sdx + dw + pad < 0.0f || sdy + dh + pad < 0.0f) {
                damage_drop(i);
                continue;
            }
            /* Record what this element covers, so the host can tell the
             * compositor which slice of the new buffer is actually new.  The
             * margin has to cover everything drawn outside the border box:
             * shadow reach (already in `pad`) and the keyboard focus ring,
             * which sits `outline-offset + outline-width` beyond it. */
            float dmg_pad = pad;
            if (e->has_outline) {
                float ring = e->outline_width + e->outline_offset + 2.0f;
                if (ring > dmg_pad) dmg_pad = ring;
            }
            damage_note(i, sdx - dmg_pad, sdy - dmg_pad,
                        dw + dmg_pad * 2.0f, dh + dmg_pad * 2.0f);
        }
        /* Set smooth rounded-clip via bg_fs shader for nearest overflow-hidden ancestor. */
        {
            int clip_anc = g_rc[i].clip_anc;
            if (clip_anc != -1) {
                LunaElement* anc = &elements[clip_anc];
                float adx, ady, adw, adh;
                rc_element_draw_bounds(clip_anc, &adx, &ady, &adw, &adh);
                float asc = anc->cur_scale;
                g_bg_clip_enabled = 1;
                g_bg_clip_pos[0]  = adx; g_bg_clip_pos[1]  = ady;
                g_bg_clip_size[0] = adw; g_bg_clip_size[1] = adh;
                g_bg_clip_rad4[0] = anc->rad_c[0]*asc; g_bg_clip_rad4[1] = anc->rad_c[1]*asc;
                g_bg_clip_rad4[2] = anc->rad_c[2]*asc; g_bg_clip_rad4[3] = anc->rad_c[3]*asc;
            } else {
                g_bg_clip_enabled = 0;
            }
        }
        float rad4[4] = { e->rad_c[0] * scale, e->rad_c[1] * scale,
                          e->rad_c[2] * scale, e->rad_c[3] * scale };
        if (e->has_shadow) {
            rc_set_element_scissor(i, fbw, fbh);
            for (int s = 0; s < e->shadow_count; s++) {
                const LunaShadow* sh = &e->shadows[s];
                if (sh->inset || sh->a <= 0.0f) continue;
                draw_shadow(dx, dy, dw, dh, sh->dx, sh->dy, sh->blur, sh->spread, 0,
                            sh->r, sh->g, sh->b, sh->a, rad4, eff_op);
            }
        }
        rc_set_element_scissor(i, fbw, fbh);
        /* mix-blend-mode: switch GL blend equation before drawing element */
        if (e->mix_blend_mode == 1) {
            /* screen: result = src + dst - src*dst */
            glBlendFuncSeparate(GL_ONE_MINUS_DST_COLOR, GL_ONE,
                                GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        } else if (e->mix_blend_mode == 3) {
            /* add/lighter: result = src*a + dst */
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE,
                                GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        } else if (e->mix_blend_mode == 2) {
            /* multiply: result = src*dst (approx via ONE_MINUS_SRC_ALPHA for semi-transparent) */
            glBlendFuncSeparate(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA,
                                GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        }
        /* backdrop-filter: blur() — capture + blur the region under this element.
         * Skip on region/layer-surface renders: the FBO only holds this chrome
         * layer (cleared transparent), so CopyTexSubImage feeds the previous
         * menubar/dock frame back into itself → shimmer every swap. */
        if (e->has_backdrop_blur && e->backdrop_blur_radius > 0.0f &&
            !e->position_sticky && g_render_root < 0) {
            glDisable(GL_SCISSOR_TEST);
            rc_scissor_reset();
            apply_backdrop_blur(dx, dy, dw, dh, e->backdrop_blur_radius,
                                e->backdrop_saturate, e->backdrop_brightness,
                                rad4, fbw, fbh);
            rc_set_element_scissor(i, fbw, fbh);
        }
        /* Multiple background layers (bottom to top = last to first in CSS order) */
        if (e->bg_layer_count > 1) {
            for (int li = e->bg_layer_count - 1; li >= 0; li--)
                draw_bg_layer(dx, dy, dw, dh, rad4, eff_op, &e->bg_layers[li]);
            /* After drawing layers, still let draw_rect_full handle border + text bg */
            draw_rect_full(dx, dy, dw, dh, 0, 0, 0, 0,
                           rad4, e->border_width,
                           e->cur_bd_r, e->cur_bd_g, e->cur_bd_b, e->cur_bd_a * eff_op, NULL);
        } else {
        draw_rect_full(dx, dy, dw, dh, e->cur_r, e->cur_g, e->cur_b, e->cur_a * eff_op,
                       rad4, e->border_width,
                       e->cur_bd_r, e->cur_bd_g, e->cur_bd_b, e->cur_bd_a * eff_op, e);
        }
        if (e->has_shadow) {
            for (int s = 0; s < e->shadow_count; s++) {
                const LunaShadow* sh = &e->shadows[s];
                if (!sh->inset || sh->a <= 0.0f) continue;
                draw_shadow(dx, dy, dw, dh, sh->dx, sh->dy, sh->blur, sh->spread, 1,
                            sh->r, sh->g, sh->b, sh->a, rad4, eff_op);
            }
        }
        /* Per-side borders: draw as thin solid rects on each active side */
        if (e->has_border_top && e->border_top_w > 0.0f && e->border_top_a * eff_op > 0.004f)
            draw_rect(dx, dy, dw, e->border_top_w * scale,
                      e->border_top_r, e->border_top_g, e->border_top_b, e->border_top_a * eff_op,
                      0, 0, 0,0,0,0);
        if (e->has_border_bottom && e->border_bottom_w > 0.0f && e->border_bottom_a * eff_op > 0.004f)
            draw_rect(dx, dy + dh - e->border_bottom_w * scale, dw, e->border_bottom_w * scale,
                      e->border_bottom_r, e->border_bottom_g, e->border_bottom_b, e->border_bottom_a * eff_op,
                      0, 0, 0,0,0,0);
        if (e->has_border_left && e->border_left_w > 0.0f && e->border_left_a * eff_op > 0.004f)
            draw_rect(dx, dy, e->border_left_w * scale, dh,
                      e->border_left_r, e->border_left_g, e->border_left_b, e->border_left_a * eff_op,
                      0, 0, 0,0,0,0);
        if (e->has_border_right && e->border_right_w > 0.0f && e->border_right_a * eff_op > 0.004f)
            draw_rect(dx + dw - e->border_right_w * scale, dy, e->border_right_w * scale, dh,
                      e->border_right_r, e->border_right_g, e->border_right_b, e->border_right_a * eff_op,
                      0, 0, 0,0,0,0);
        if (e->has_bg_image && e->bg_image_path[0]) {
            if (!e->bg_image_tex) {
                e->bg_image_tex = load_or_get_texture(e->bg_image_path);
                if (!e->bg_image_tex) e->has_bg_image = 0; /* don't retry every frame */
            }
            if (e->bg_image_tex) {
                float bw = e->border_width;
                draw_image(dx+bw, dy+bw, dw-2*bw, dh-2*bw, e->border_radius*scale, e->bg_image_tex, eff_op);
            }
        }
        {
            float pad_l = e->pad_l * scale;
            float pad_r = e->pad_r * scale;
            float pad_t = e->pad_t * scale;
            float pad_b = e->pad_b * scale;
            float inner_w = dw - pad_l - pad_r;
            float inner_h = dh - pad_t - pad_b;
            int show_ph = e->is_input && !e->text[0] && e->placeholder[0];
            const char* src = show_ph ? e->placeholder : e->text;
            if (src[0] || e->is_input) {
                char tbuf[512];
                if (e->is_input && e->input_password && e->text[0] && !show_ph) {
                    /* Mask password with '*' per codepoint */
                    int ti = 0;
                    const char* pp = e->text;
                    while (*pp && ti < (int)sizeof(tbuf) - 1) {
                        (void)utf8_decode(&pp);
                        tbuf[ti++] = '*';
                    }
                    tbuf[ti] = '\0';
                } else {
                    strncpy(tbuf, src, sizeof(tbuf) - 1);
                    tbuf[sizeof(tbuf) - 1] = '\0';
                    if (!show_ph) apply_text_transform_buf(tbuf, e->text_transform);
                }
                float tr = e->t_r, tg = e->t_g, tb = e->t_b, ta = e->t_a * eff_op;
                /* background-clip: text with color: transparent — use full alpha for gradient fill */
                if (!show_ph && e->has_bg_clip_text && e->has_gradient && ta <= 0.0f)
                    ta = eff_op;
                if (show_ph) ta *= 0.45f;
                int align;
                if (e->has_text_align) {
                    align = e->text_align;
                } else if (e->display_mode == DISPLAY_FLEX || e->display_mode == DISPLAY_GRID) {
                    if (e->justify_content == FLEX_JUSTIFY_CENTER) align = 1;
                    else if (e->justify_content == FLEX_JUSTIFY_END) align = 2;
                    else align = 0; /* flex-start → left, matches CSS */
                } else {
                    align = 0; /* CSS default text-align: start */
                }
                int ws = e->is_input && !e->input_multiline ? 1 : e->white_space;
                if (e->is_input && !e->input_multiline)
                    input_update_scroll(e, inner_w);
                float tx = dx + pad_l - (e->is_input && !e->input_multiline ? e->input_scroll_x : 0.0f);
                /* Direct text after flex children has already been positioned
                   as an anonymous flex item during layout. */
                if (e->has_inline_text_flow) {
                    tx = dx + e->inline_text_x * scale;
                    /* The element scissor already clips to the border box.
                       Do not subtract padding a second time here: doing so
                       clipped the last glyph in short status labels. */
                    inner_w = dw - e->inline_text_x * scale;
                    align = 0;
                }
                if (tbuf[0])
                    render_text_fx(tbuf, tx, dy + pad_t, inner_w + (e->is_input ? e->input_scroll_x : 0.0f),
                                   inner_h, align, css_text_vertical_align(e), tr, tg, tb, ta, e->font_size, e->font_bold,
                                   e->line_height, ws, e->text_overflow, e->overflow_wrap,
                                   show_ph ? NULL : e);
                /* Caret — CSS caret-color (falls back to text color) */
                if (e->is_input && g_focused_element_idx == i) {
                    double blink = luna_now();
                    if (fmod(blink, 1.0) < 0.55) {
                        float cx = measure_prefix_width(e, e->caret) - e->input_scroll_x;
                        float cr = e->has_caret_color ? e->caret_r : e->t_r;
                        float cg = e->has_caret_color ? e->caret_g : e->t_g;
                        float cb = e->has_caret_color ? e->caret_b : e->t_b;
                        float ca = e->has_caret_color ? e->caret_a : e->t_a;
                        float cw = 1.5f;
                        float ch = (e->font_size > 0 ? (float)e->font_size : 16.0f) * 1.15f;
                        if (ch > inner_h) ch = inner_h;
                        float cy = dy + pad_t + (inner_h - ch) * 0.5f;
                        draw_rect(dx + pad_l + cx, cy, cw, ch, cr, cg, cb, ca * eff_op, 0, 0, 0,0,0,0);
                    }
                }
            }
        }
        if (g_focus_via_keyboard && g_focused_element_idx == i && e->has_outline && e->outline_width > 0) {
            float ow = e->outline_width, off = e->outline_offset, p = off + ow;
            draw_rect(dx-p, dy-p, dw+p*2, dh+p*2, 0,0,0,0, e->border_radius*scale+off,
                      ow, e->ol_r, e->ol_g, e->ol_b, e->ol_a*eff_op);
        }
        /* Restore default blend mode after mix-blend-mode element */
        if (e->mix_blend_mode != 0)
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                                GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_SCISSOR_TEST);
        rc_scissor_reset();
    }
    g_bg_clip_enabled = 0;
    repaint_stuck_sticky_layers(fbw, fbh);
}

static int sticky_is_stuck_in_scroll(int idx) {
    LunaElement* e = &elements[idx];
    if (!e->position_sticky || e->display_none) return 0;
    if (e->sticky_use_bottom && !e->sticky_use_top) {
        int scroll_p = -1;
        for (int p = e->parent_idx; p != -1; p = elements[p].parent_idx) {
            if (overflow_scrollable(elements[p].overflow_y)) { scroll_p = p; break; }
        }
        if (scroll_p == -1 || elements[scroll_p].scroll_top <= 0.5f) return 0;
        LunaElement* par = &elements[scroll_p];
        float inner_bottom = par->y + par->h - par->border_width - par->pad_b;
        float stick = inner_bottom - e->h - e->sticky_bottom;
        return fabsf(e->y - stick) <= 1.5f;
    }
    for (int p = e->parent_idx; p != -1; p = elements[p].parent_idx) {
        if (!overflow_scrollable(elements[p].overflow_y)) continue;
        float st = elements[p].scroll_top;
        if (elements[p].scroll_dest_top > st) st = elements[p].scroll_dest_top;
        if (st <= 0.5f) return 0;
        return 1;
    }
    return 0;
}

/* Runs at the tail of luna_render(), so the per-frame ancestor cache built
 * there is still current and every ancestor query below is an array read. */
static void repaint_stuck_sticky_layers(int fbw, int fbh) {
    for (int i = 0; i < elem_count; i++) {
        if (!sticky_is_stuck_in_scroll(i) || !g_rc[i].vis) continue;
        LunaElement* e = &elements[i];
        float eff_op = g_rc[i].eff_op;
        if (eff_op <= 0.004f) continue;
        float dx, dy, dw, dh;
        rc_element_draw_bounds(i, &dx, &dy, &dw, &dh);
        if (dw <= 0.0f || dh <= 0.0f) continue;
        float scale = e->cur_scale;
        float rad4[4] = { e->rad_c[0]*scale, e->rad_c[1]*scale,
                          e->rad_c[2]*scale, e->rad_c[3]*scale };
        rc_set_element_scissor(i, fbw, fbh);
        float sr = e->cur_r, sg = e->cur_g, sb = e->cur_b;
        if (e->has_gradient && e->grad_stop_count > 0) {
            sr = e->grad_stop_r[0]; sg = e->grad_stop_g[0]; sb = e->grad_stop_b[0];
        } else if (sr + sg + sb < 0.03f) {
            sr = 0.969f; sg = 0.969f; sb = 0.992f;
        }
        draw_rect(dx, dy, dw, dh, sr, sg, sb, eff_op, 0, 0, 0,0,0,0);
        draw_rect_full(dx, dy, dw, dh, e->cur_r, e->cur_g, e->cur_b, e->cur_a * eff_op,
                       rad4, e->border_width,
                       e->cur_bd_r, e->cur_bd_g, e->cur_bd_b, e->cur_bd_a * eff_op, e);
        if (e->has_border_bottom && e->border_bottom_w > 0.0f && e->border_bottom_a * eff_op > 0.004f)
            draw_rect(dx, dy + dh - e->border_bottom_w * scale, dw, e->border_bottom_w * scale,
                      e->border_bottom_r, e->border_bottom_g, e->border_bottom_b, e->border_bottom_a * eff_op,
                      0, 0, 0,0,0,0);
        if (e->has_border_top && e->border_top_w > 0.0f && e->border_top_a * eff_op > 0.004f)
            draw_rect(dx, dy, dw, e->border_top_w * scale,
                      e->border_top_r, e->border_top_g, e->border_top_b, e->border_top_a * eff_op,
                      0, 0, 0,0,0,0);
        if (e->text[0]) {
            char tbuf[512];
            strncpy(tbuf, e->text, sizeof(tbuf) - 1);
            tbuf[sizeof(tbuf) - 1] = '\0';
            apply_text_transform_buf(tbuf, e->text_transform);
            float pad_l = e->pad_l * scale, pad_t = e->pad_t * scale;
            float pad_r = e->pad_r * scale, pad_b = e->pad_b * scale;
            float inner_w = dw - pad_l - pad_r, inner_h = dh - pad_t - pad_b;
            int align = e->has_text_align ? e->text_align : 0;
            render_text_fx(tbuf, dx + pad_l, dy + pad_t, inner_w, inner_h, align, css_text_vertical_align(e),
                           e->t_r, e->t_g, e->t_b, e->t_a * eff_op, e->font_size, e->font_bold,
                           e->line_height, e->white_space, e->text_overflow, e->overflow_wrap, e);
        }
        glDisable(GL_SCISSOR_TEST);
        rc_scissor_reset();
    }
}

int luna_mouse_move_changed(double x, double y) {
    unsigned before = g_pointer_visual_revision;
    int dragging = (g_scroll_drag_idx != -1 || drag_target_idx != -1);
    g_luna_mx = x; g_luna_my = y;
    cursor_position_callback(NULL, x, y);
    return dragging || before != g_pointer_visual_revision;
}
void luna_mouse_move(double x, double y) {
    (void)luna_mouse_move_changed(x, y);
}
void luna_mouse_button(int b, int a, int m, double x, double y) {
    g_luna_mx = x; g_luna_my = y;
    mouse_button_callback(NULL, b, a, m);
}
void luna_scroll(double xo, double yo) { scroll_callback(NULL, xo, yo); }
void luna_key(int k, int sc, int a, int m) {
    g_luna_shift = (m & LUNA_MOD_SHIFT) ? 1 : 0;
    key_callback(NULL, k, sc, a, m);
}
void luna_char(unsigned int codepoint) { char_callback_impl(codepoint); }
const char* luna_get_value(int idx) {
    if (idx < 0 || idx >= elem_count) return "";
    return elements[idx].text;
}
void luna_set_value(int idx, const char* value) { g_probe_prepared = 0; set_text(idx, value ? value : ""); }

static void cache_uniform_locations(void) {
    bg_loc.uResolution  = glGetUniformLocation(bg_program, "uResolution");
    bg_loc.uPos         = glGetUniformLocation(bg_program, "uPos");
    bg_loc.uSize        = glGetUniformLocation(bg_program, "uSize");
    bg_loc.uColor       = glGetUniformLocation(bg_program, "uColor");
    bg_loc.uBorderColor = glGetUniformLocation(bg_program, "uBorderColor");
    bg_loc.uBorderWidth = glGetUniformLocation(bg_program, "uBorderWidth");
    bg_loc.uRadius4     = glGetUniformLocation(bg_program, "uRadius4");
    bg_loc.uGradient    = glGetUniformLocation(bg_program, "uGradient");
    bg_loc.uGradStopCount = glGetUniformLocation(bg_program, "uGradStopCount");
    bg_loc.uGradAngle   = glGetUniformLocation(bg_program, "uGradAngle");
    bg_loc.uGradCenter  = glGetUniformLocation(bg_program, "uGradCenter");
    bg_loc.uGradRadius  = glGetUniformLocation(bg_program, "uGradRadius");
    bg_loc.uGradRadRx   = glGetUniformLocation(bg_program, "uGradRadRx");
    bg_loc.uGradRadRy   = glGetUniformLocation(bg_program, "uGradRadRy");
    for (int i = 0; i < MAX_GRAD_STOPS; i++) {
        char uname[32];
        snprintf(uname, sizeof(uname), "uGradColors[%d]", i);
        bg_loc.uGradColors[i] = glGetUniformLocation(bg_program, uname);
        snprintf(uname, sizeof(uname), "uGradStops[%d]", i);
        bg_loc.uGradStops[i]  = glGetUniformLocation(bg_program, uname);
    }
    bg_loc.uFilterMode       = glGetUniformLocation(bg_program, "uFilterMode");
    bg_loc.uFilterBrightness = glGetUniformLocation(bg_program, "uFilterBrightness");
    bg_loc.uFilterContrast   = glGetUniformLocation(bg_program, "uFilterContrast");
    bg_loc.uFilterSaturate   = glGetUniformLocation(bg_program, "uFilterSaturate");
    bg_loc.uFilterHue        = glGetUniformLocation(bg_program, "uFilterHue");
    bg_loc.uClipEnabled  = glGetUniformLocation(bg_program, "uClipEnabled");
    bg_loc.uClipPos      = glGetUniformLocation(bg_program, "uClipPos");
    bg_loc.uClipSize     = glGetUniformLocation(bg_program, "uClipSize");
    bg_loc.uClipRadius4  = glGetUniformLocation(bg_program, "uClipRadius4");
    sh_loc.uResolution  = glGetUniformLocation(shadow_program, "uResolution");
    sh_loc.uPos         = glGetUniformLocation(shadow_program, "uPos");
    sh_loc.uSize        = glGetUniformLocation(shadow_program, "uSize");
    sh_loc.uShadowColor = glGetUniformLocation(shadow_program, "uShadowColor");
    sh_loc.uElemSize    = glGetUniformLocation(shadow_program, "uElemSize");
    sh_loc.uRadius4     = glGetUniformLocation(shadow_program, "uRadius4");
    sh_loc.uBlur        = glGetUniformLocation(shadow_program, "uBlur");
    sh_loc.uSpread      = glGetUniformLocation(shadow_program, "uSpread");
    sh_loc.uInsetMode   = glGetUniformLocation(shadow_program, "uInsetMode");
    sh_loc.uShadowInset = glGetUniformLocation(shadow_program, "uShadowInset");
    sh_loc.uOffset      = glGetUniformLocation(shadow_program, "uOffset");
    tx_loc.uResolution    = glGetUniformLocation(text_program, "uResolution");
    tx_loc.textColor      = glGetUniformLocation(text_program, "textColor");
    tx_loc.uGradMode      = glGetUniformLocation(text_program, "uGradMode");
    tx_loc.uGradStopCount = glGetUniformLocation(text_program, "uGradStopCount");
    tx_loc.uGradAngle     = glGetUniformLocation(text_program, "uGradAngle");
    tx_loc.uElemBounds    = glGetUniformLocation(text_program, "uElemBounds");
    for (int i = 0; i < MAX_GRAD_STOPS; i++) {
        char tbuf_[32];
        snprintf(tbuf_, sizeof(tbuf_), "uGradColors[%d]", i);
        tx_loc.uGradColors[i] = glGetUniformLocation(text_program, tbuf_);
        snprintf(tbuf_, sizeof(tbuf_), "uGradStops[%d]", i);
        tx_loc.uGradStops[i]  = glGetUniformLocation(text_program, tbuf_);
    }
    img_loc.uResolution = glGetUniformLocation(img_program, "uResolution");
    img_loc.uPos        = glGetUniformLocation(img_program, "uPos");
    img_loc.uSize       = glGetUniformLocation(img_program, "uSize");
    img_loc.uRadius     = glGetUniformLocation(img_program, "uRadius");
    img_loc.uAlpha      = glGetUniformLocation(img_program, "uAlpha");
    img_loc.uImage      = glGetUniformLocation(img_program, "uImage");
    if (blur_program) {
        blur_loc.uResolution  = glGetUniformLocation(blur_program, "uResolution");
        blur_loc.uPos         = glGetUniformLocation(blur_program, "uPos");
        blur_loc.uSize        = glGetUniformLocation(blur_program, "uSize");
        blur_loc.uSrc         = glGetUniformLocation(blur_program, "uSrc");
        blur_loc.uBlurDir     = glGetUniformLocation(blur_program, "uBlurDir");
        blur_loc.uBlurRadius  = glGetUniformLocation(blur_program, "uBlurRadius");
        blur_loc.uBlurTexSize = glGetUniformLocation(blur_program, "uBlurTexSize");
        blur_loc.uFbSize      = glGetUniformLocation(blur_program, "uFbSize");
        blur_loc.uBlurOrigin  = glGetUniformLocation(blur_program, "uBlurOrigin");
    }
    if (backdrop_program) {
        backdrop_loc.uResolution  = glGetUniformLocation(backdrop_program, "uResolution");
        backdrop_loc.uPos         = glGetUniformLocation(backdrop_program, "uPos");
        backdrop_loc.uSize        = glGetUniformLocation(backdrop_program, "uSize");
        backdrop_loc.uRadius4     = glGetUniformLocation(backdrop_program, "uRadius4");
        backdrop_loc.uSrc         = glGetUniformLocation(backdrop_program, "uSrc");
        backdrop_loc.uBlurTexSize = glGetUniformLocation(backdrop_program, "uBlurTexSize");
        backdrop_loc.uFbSize      = glGetUniformLocation(backdrop_program, "uFbSize");
        backdrop_loc.uBlurOrigin  = glGetUniformLocation(backdrop_program, "uBlurOrigin");
        backdrop_loc.uSaturate    = glGetUniformLocation(backdrop_program, "uSaturate");
        backdrop_loc.uBrightness  = glGetUniformLocation(backdrop_program, "uBrightness");
    }
    /* Freshly linked programs hold none of the values the shadow remembers. */
    luna_uni_invalidate((LunaUniShadow*)&bg_uni,
                        (int)(sizeof(bg_uni) / sizeof(LunaUniShadow)));
    luna_uni_invalidate((LunaUniShadow*)&sh_uni,
                        (int)(sizeof(sh_uni) / sizeof(LunaUniShadow)));
    luna_uni_invalidate((LunaUniShadow*)&tx_uni,
                        (int)(sizeof(tx_uni) / sizeof(LunaUniShadow)));
}

int luna_init(const LunaInitConfig* cfg) {
    if (!cfg || !cfg->get_proc) return 0;
#ifdef LUNA_UI_GLFW
    if (g_luna_glfw_window && cfg->frameless)
        glfwSetWindowAttrib((GLFWwindow*)g_luna_glfw_window,
                            GLFW_DECORATED, GLFW_FALSE);
#endif
    if (!g_luna_platform.get_proc) g_luna_platform.get_proc = cfg->get_proc;
    luna_window_width = cfg->width;
    luna_window_height = cfg->height;
    load_gl_functions();
    GLuint vs = compile_shader(bg_vs, GL_VERTEX_SHADER);
    GLuint fs = compile_shader(bg_fs, GL_FRAGMENT_SHADER);
    bg_program = glCreateProgram();
    glAttachShader(bg_program, vs); glAttachShader(bg_program, fs);
    glLinkProgram(bg_program);
    GLuint tvs = compile_shader(text_vs, GL_VERTEX_SHADER);
    GLuint tfs = compile_shader(text_fs, GL_FRAGMENT_SHADER);
    text_program = glCreateProgram();
    glAttachShader(text_program, tvs); glAttachShader(text_program, tfs);
    glLinkProgram(text_program);
    GLuint svs = compile_shader(bg_vs, GL_VERTEX_SHADER);
    GLuint sfs = compile_shader(shadow_fs, GL_FRAGMENT_SHADER);
    shadow_program = glCreateProgram();
    glAttachShader(shadow_program, svs); glAttachShader(shadow_program, sfs);
    glLinkProgram(shadow_program);
    GLuint ivs = compile_shader(bg_vs, GL_VERTEX_SHADER);
    GLuint ifs = compile_shader(img_fs, GL_FRAGMENT_SHADER);
    img_program = glCreateProgram();
    glAttachShader(img_program, ivs); glAttachShader(img_program, ifs);
    glLinkProgram(img_program);
    /* Blur shader programs */
    {
        GLuint bvs = compile_shader(bg_vs, GL_VERTEX_SHADER);
        GLuint bfs = compile_shader(blur_fs, GL_FRAGMENT_SHADER);
        blur_program = glCreateProgram();
        glAttachShader(blur_program, bvs); glAttachShader(blur_program, bfs);
        glLinkProgram(blur_program);

        GLuint dvs = compile_shader(bg_vs, GL_VERTEX_SHADER);
        GLuint dfs = compile_shader(backdrop_fs, GL_FRAGMENT_SHADER);
        backdrop_program = glCreateProgram();
        glAttachShader(backdrop_program, dvs); glAttachShader(backdrop_program, dfs);
        glLinkProgram(backdrop_program);
    }
    cache_uniform_locations();
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                        GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    init_rect_geometry();
    init_font();
    return 1;
}
void luna_shutdown(void) {}

void luna_render_region(int root_idx, int fbw, int fbh,
                        float origin_x, float origin_y,
                        float region_w, float region_h) {
    g_render_root  = root_idx;
    g_render_off_x = origin_x;
    g_render_off_y = origin_y;
    g_render_res_x = region_w;
    g_render_res_y = region_h;
    luna_render(fbw, fbh);
    g_render_root  = -1;
    g_render_off_x = 0.0f;
    g_render_off_y = 0.0f;
    g_render_res_x = 0.0f;
    g_render_res_y = 0.0f;
}

LunaContext* luna_context_create(void) {
    LunaContext* ctx = (LunaContext*)calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;
    ctx->root_idx = -1;
    /* g_rect_vbo belongs to the GL share group.  VAOs are context-local, so
     * reproduce only the attribute binding in each native GL context. */
    glCreateVertexArrays(1, &ctx->rect_vao);
    if (!ctx->rect_vao) {
        free(ctx);
        return NULL;
    }
    glBindVertexArray(ctx->rect_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_rect_vbo);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glCreateVertexArrays(1, &ctx->text_vao);
    if (!ctx->text_vao) {
        glDeleteVertexArrays(1, &ctx->rect_vao);
        free(ctx);
        return NULL;
    }
    glBindVertexArray(ctx->text_vao);
    glBindBuffer(GL_ARRAY_BUFFER, text_vbo);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                        GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    g_current_vao = ctx->rect_vao;
    g_current_program = 0;
    return ctx;
}

void luna_context_destroy(LunaContext* ctx) {
    if (!ctx) return;
    if (ctx->rect_vao) glDeleteVertexArrays(1, &ctx->rect_vao);
    if (ctx->text_vao) glDeleteVertexArrays(1, &ctx->text_vao);
    free(ctx);
}

void luna_context_set_region(LunaContext* ctx, int root_idx,
                             float origin_x, float origin_y,
                             float region_w, float region_h) {
    if (!ctx) return;
    ctx->root_idx = root_idx;
    ctx->origin_x = origin_x;
    ctx->origin_y = origin_y;
    ctx->region_w = region_w;
    ctx->region_h = region_h;
}

void luna_context_render(LunaContext* ctx, int fbw, int fbh) {
    if (!ctx) return;
    /* Binding is unconditional because another native context may have been
     * current during the previous Luna render. */
    glBindVertexArray(ctx->rect_vao);
    g_rect_vao = ctx->rect_vao;
    text_vao = ctx->text_vao;
    g_current_vao = ctx->rect_vao;
    g_current_program = 0;
    luna_render_region(ctx->root_idx, fbw, fbh,
                       ctx->origin_x, ctx->origin_y,
                       ctx->region_w, ctx->region_h);
}

void luna_context_mouse_move(LunaContext* ctx, double x, double y) {
    if (!ctx) return;
    luna_mouse_move(x + ctx->origin_x, y + ctx->origin_y);
}

void luna_context_mouse_button(LunaContext* ctx, int button, int action,
                               int mods, double x, double y) {
    if (!ctx) return;
    luna_mouse_button(button, action, mods,
                      x + ctx->origin_x, y + ctx->origin_y);
}

void luna_context_scroll(LunaContext* ctx, double xoffset, double yoffset) {
    if (!ctx) return;
    luna_scroll(xoffset, yoffset);
}

#endif /* LUNA_UI_IMPLEMENTATION */
#ifdef __cplusplus
}
#endif
#endif /* LUNA_UI_H */
