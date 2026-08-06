#define _POSIX_C_SOURCE 200809L
#include "GLFW/glfw3.h"
#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Minimal EGL declarations. The screenshot runner deliberately avoids a GLFW
 * development package and creates shared OpenGL pbuffer contexts directly. */
typedef void *EGLDisplay;
typedef void *EGLConfig;
typedef void *EGLContext;
typedef void *EGLSurface;
typedef void *EGLNativeDisplayType;
typedef int EGLint;
typedef unsigned int EGLBoolean;
typedef unsigned int EGLenum;
#define EGL_FALSE 0
#define EGL_TRUE 1
#define EGL_NONE 0x3038
#define EGL_SURFACE_TYPE 0x3033
#define EGL_PBUFFER_BIT 0x0001
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_OPENGL_BIT 0x0008
#define EGL_RED_SIZE 0x3024
#define EGL_GREEN_SIZE 0x3023
#define EGL_BLUE_SIZE 0x3022
#define EGL_ALPHA_SIZE 0x3021
#define EGL_DEPTH_SIZE 0x3025
#define EGL_STENCIL_SIZE 0x3026
#define EGL_WIDTH 0x3057
#define EGL_HEIGHT 0x3056
#define EGL_OPENGL_API 0x30A2
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#define EGL_CONTEXT_MAJOR_VERSION_KHR 0x3098
#define EGL_CONTEXT_MINOR_VERSION_KHR 0x30FB
#define EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR 0x30FD
#define EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR 0x00000001
#define EGL_NO_DISPLAY ((EGLDisplay)0)
#define EGL_NO_CONTEXT ((EGLContext)0)
#define EGL_NO_SURFACE ((EGLSurface)0)

extern EGLBoolean eglInitialize(EGLDisplay,EGLint*,EGLint*);
extern EGLBoolean eglTerminate(EGLDisplay);
extern EGLBoolean eglChooseConfig(EGLDisplay,const EGLint*,EGLConfig*,EGLint,EGLint*);
extern EGLBoolean eglBindAPI(EGLenum);
extern EGLContext eglCreateContext(EGLDisplay,EGLConfig,EGLContext,const EGLint*);
extern EGLBoolean eglDestroyContext(EGLDisplay,EGLContext);
extern EGLSurface eglCreatePbufferSurface(EGLDisplay,EGLConfig,const EGLint*);
extern EGLBoolean eglDestroySurface(EGLDisplay,EGLSurface);
extern EGLBoolean eglMakeCurrent(EGLDisplay,EGLSurface,EGLSurface,EGLContext);
extern EGLBoolean eglSwapBuffers(EGLDisplay,EGLSurface);
extern void *eglGetProcAddress(const char*);
extern EGLint eglGetError(void);

typedef EGLDisplay (*PFNEGLGETPLATFORMDISPLAYEXTPROC)(EGLenum,void*,const EGLint*);
extern int stbi_write_png(char const *filename, int w, int h, int comp, const void *data, int stride_in_bytes);

struct GLFWmonitor { int dummy; };
struct GLFWcursor { int shape; };
struct GLFWwindow {
    int index;
    int width, height;
    int fbw, fbh;
    int x, y;
    int visible;
    int should_close;
    void *user;
    double cursor_x, cursor_y;
    EGLContext context;
    EGLSurface surface;
    unsigned char *pixels;
    unsigned long capture_serial;
    GLFWframebuffersizefun framebuffer_cb;
    GLFWwindowsizefun size_cb;
    GLFWcursorposfun cursor_cb;
    GLFWmousebuttonfun mouse_cb;
    GLFWscrollfun scroll_cb;
    GLFWkeyfun key_cb;
    GLFWcharfun char_cb;
};

static EGLDisplay g_display = EGL_NO_DISPLAY;
static EGLConfig g_config = NULL;
static GLFWwindow *g_current = NULL;
static GLFWwindow *g_windows[32];
static int g_window_count = 0;
static GLFWerrorfun g_error_cb = NULL;
static struct timespec g_start;
static int g_hint_visible = 0;
static int g_hidpi = 2;
static char g_output[PATH_MAX] = "aplay-screenshot.png";
static int g_background_variant = 0;
static int g_finish_after_frames = 2;
static int g_render_cycles = 0;
static int g_wait_calls = 0;
static int g_menu_injected = 0;
static unsigned long g_serial = 1;
static void *g_libgl = NULL;
typedef void (*RawReadPixelsFn)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);
static RawReadPixelsFn g_raw_read_pixels = NULL;

static void report_error(int code, const char *message) {
    if (g_error_cb) g_error_cb(code, message);
    else fprintf(stderr, "screenshot-glfw: %s\n", message);
}

static void blend(unsigned char *dst, int r, int g, int b, int a) {
    int ia = 255 - a;
    dst[0] = (unsigned char)((r*a + dst[0]*ia) / 255);
    dst[1] = (unsigned char)((g*a + dst[1]*ia) / 255);
    dst[2] = (unsigned char)((b*a + dst[2]*ia) / 255);
    dst[3] = 255;
}

static void rounded_rect(unsigned char *img, int iw, int ih, int x, int y, int w, int h,
                         int radius, int r, int g, int b, int a) {
    if (w <= 0 || h <= 0) return;
    for (int yy = y; yy < y+h; yy++) {
        if (yy < 0 || yy >= ih) continue;
        for (int xx = x; xx < x+w; xx++) {
            if (xx < 0 || xx >= iw) continue;
            int dx = 0, dy = 0;
            if (xx < x+radius) dx = x+radius-xx;
            else if (xx >= x+w-radius) dx = xx-(x+w-radius-1);
            if (yy < y+radius) dy = y+radius-yy;
            else if (yy >= y+h-radius) dy = yy-(y+h-radius-1);
            if (dx > 0 && dy > 0 && dx*dx + dy*dy > radius*radius) continue;
            blend(img + ((size_t)yy*iw + xx)*4, r,g,b,a);
        }
    }
}

static void compose_and_write(void) {
    int cw = 1280, ch = 1120;
    unsigned char *canvas = (unsigned char*)malloc((size_t)cw*ch*4);
    if (!canvas) return;
    for (int y = 0; y < ch; y++) {
        float t = (float)y / (float)(ch-1);
        int r0,g0,b0,r1,g1,b1;
        if (g_background_variant == 1) {
            r0=26; g0=31; b0=48; r1=10; g1=13; b1=24;
        } else if (g_background_variant == 2) {
            r0=49; g0=31; b0=30; r1=18; g1=15; b1=19;
        } else {
            r0=36; g0=34; b0=43; r1=14; g1=16; b1=23;
        }
        int r=(int)(r0*(1-t)+r1*t), g=(int)(g0*(1-t)+g1*t), b=(int)(b0*(1-t)+b1*t);
        for (int x = 0; x < cw; x++) {
            float glow = 1.0f - (float)((x-cw/2)*(x-cw/2)) / (float)((cw/2)*(cw/2));
            if (glow < 0) glow = 0;
            unsigned char *p=canvas+((size_t)y*cw+x)*4;
            p[0]=(unsigned char)(r + (int)(glow*10));
            p[1]=(unsigned char)(g + (int)(glow*8));
            p[2]=(unsigned char)(b + (int)(glow*14));
            p[3]=255;
        }
    }

    const int base_x = 365;
    const int base_y = 86;
    const int logical_origin_x = 100;
    const int logical_origin_y = 100;
    int max_right = 0, max_bottom = 0;
    for (int i=0;i<g_window_count;i++) {
        GLFWwindow *w=g_windows[i];
        if (!w || !w->visible || !w->pixels || w->capture_serial==0) continue;
        int dx = base_x + (w->x-logical_origin_x)*g_hidpi;
        int dy = base_y + (w->y-logical_origin_y)*g_hidpi;
        if (i==0) { dx=base_x; dy=base_y; }
        if (i==1) { dx=base_x; dy=base_y+116*g_hidpi; }
        if (i==2) { dx=base_x; dy=base_y+(116+116)*g_hidpi; }
        if (dx+w->fbw>max_right) max_right=dx+w->fbw;
        if (dy+w->fbh>max_bottom) max_bottom=dy+w->fbh;
        for (int s=24;s>=4;s-=4)
            rounded_rect(canvas,cw,ch,dx-s/2,dy-s/3,w->fbw+s,w->fbh+s,16+s/3,0,0,0,5+(24-s));
        rounded_rect(canvas,cw,ch,dx-2,dy-2,w->fbw+4,w->fbh+4,8,0,0,0,110);
        for (int yy=0;yy<w->fbh;yy++) {
            int cy=dy+yy;
            if (cy<0 || cy>=ch) continue;
            for (int xx=0;xx<w->fbw;xx++) {
                int cx=dx+xx;
                if (cx<0 || cx>=cw) continue;
                const unsigned char *src=w->pixels+((size_t)yy*w->fbw+xx)*4;
                unsigned char *dst=canvas+((size_t)cy*cw+cx)*4;
                int a=src[3];
                dst[0]=(unsigned char)((src[0]*a+dst[0]*(255-a))/255);
                dst[1]=(unsigned char)((src[1]*a+dst[1]*(255-a))/255);
                dst[2]=(unsigned char)((src[2]*a+dst[2]*(255-a))/255);
                dst[3]=255;
            }
        }
    }

    rounded_rect(canvas,cw,ch,40,36,82,40,20,255,255,255,18);
    rounded_rect(canvas,cw,ch,55,50,12,12,6,244,110,98,210);
    rounded_rect(canvas,cw,ch,75,50,12,12,6,244,190,78,210);
    rounded_rect(canvas,cw,ch,95,50,12,12,6,92,204,120,210);

    if (!stbi_write_png(g_output,cw,ch,4,canvas,cw*4))
        fprintf(stderr,"screenshot-glfw: failed to write %s\n",g_output);
    else
        fprintf(stderr,"screenshot-glfw: wrote %s (%dx%d)\n",g_output,cw,ch);
    free(canvas);
}

int glfwInit(void) {
    clock_gettime(CLOCK_MONOTONIC,&g_start);
    const char *scale=getenv("APLAY_SCREENSHOT_SCALE");
    if (scale && atoi(scale)>=1 && atoi(scale)<=4) g_hidpi=atoi(scale);
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (!get_platform) { report_error(1,"eglGetPlatformDisplayEXT unavailable"); return GLFW_FALSE; }
    g_display=get_platform(EGL_PLATFORM_SURFACELESS_MESA,NULL,NULL);
    EGLint major=0,minor=0;
    if (!g_display || !eglInitialize(g_display,&major,&minor)) { report_error(2,"surfaceless EGL initialization failed"); return GLFW_FALSE; }
    if (!eglBindAPI(EGL_OPENGL_API)) { report_error(3,"eglBindAPI(OpenGL) failed"); return GLFW_FALSE; }
    const EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_DEPTH_SIZE,0,EGL_STENCIL_SIZE,8,EGL_NONE};
    EGLint n=0;
    if (!eglChooseConfig(g_display,attrs,&g_config,1,&n) || n<1) { report_error(4,"no EGL OpenGL pbuffer config"); return GLFW_FALSE; }
    g_libgl=dlopen("libGL.so.1",RTLD_LAZY|RTLD_GLOBAL);
    g_raw_read_pixels=(RawReadPixelsFn)eglGetProcAddress("glReadPixels");
    if (!g_raw_read_pixels && g_libgl) g_raw_read_pixels=(RawReadPixelsFn)dlsym(g_libgl,"glReadPixels");
    if (!g_raw_read_pixels) { report_error(5,"glReadPixels unavailable"); return GLFW_FALSE; }
    return GLFW_TRUE;
}
void glfwTerminate(void) { if (g_display) eglTerminate(g_display); g_display=EGL_NO_DISPLAY; if (g_libgl) dlclose(g_libgl); g_libgl=NULL; }
GLFWerrorfun glfwSetErrorCallback(GLFWerrorfun cbfun){GLFWerrorfun old=g_error_cb;g_error_cb=cbfun;return old;}
void glfwWindowHint(int hint,int value){if(hint==GLFW_VISIBLE)g_hint_visible=value;}
GLFWwindow* glfwCreateWindow(int width,int height,const char* title,GLFWmonitor* monitor,GLFWwindow* share){
    (void)title;(void)monitor;
    if(g_window_count>=32)return NULL;
    GLFWwindow*w=(GLFWwindow*)calloc(1,sizeof(*w)); if(!w)return NULL;
    w->index=g_window_count; w->width=width; w->height=height; w->fbw=width*g_hidpi; w->fbh=height*g_hidpi; w->visible=g_hint_visible;
    const EGLint pb[]={EGL_WIDTH,w->fbw,EGL_HEIGHT,w->fbh,EGL_NONE};
    w->surface=eglCreatePbufferSurface(g_display,g_config,pb);
    const EGLint ctx[]={EGL_CONTEXT_MAJOR_VERSION_KHR,3,EGL_CONTEXT_MINOR_VERSION_KHR,3,
        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,EGL_NONE};
    w->context=eglCreateContext(g_display,g_config,share?share->context:EGL_NO_CONTEXT,ctx);
    if(!w->context)w->context=eglCreateContext(g_display,g_config,share?share->context:EGL_NO_CONTEXT,NULL);
    if(!w->surface||!w->context){fprintf(stderr,"screenshot-glfw: EGL window %d failed, error=0x%x\n",w->index,eglGetError());free(w);return NULL;}
    w->pixels=(unsigned char*)calloc((size_t)w->fbw*w->fbh,4);
    g_windows[g_window_count++]=w; return w;
}
void glfwDestroyWindow(GLFWwindow*w){if(!w)return;if(g_current==w)g_current=NULL;eglDestroySurface(g_display,w->surface);eglDestroyContext(g_display,w->context);free(w->pixels);for(int i=0;i<g_window_count;i++)if(g_windows[i]==w)g_windows[i]=NULL;free(w);}
void glfwMakeContextCurrent(GLFWwindow*w){g_current=w;if(w)eglMakeCurrent(g_display,w->surface,w->surface,w->context);else eglMakeCurrent(g_display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);}
GLFWwindow*glfwGetCurrentContext(void){return g_current;}
void glfwSwapBuffers(GLFWwindow*w){
    if (!w) return;
    g_raw_read_pixels(0,0,w->fbw,w->fbh,GL_RGBA,GL_UNSIGNED_BYTE,w->pixels);
    size_t row=(size_t)w->fbw*4; unsigned char*tmp=(unsigned char*)malloc(row);
    if(tmp){for(int y=0;y<w->fbh/2;y++){unsigned char*a=w->pixels+(size_t)y*row;unsigned char*b=w->pixels+(size_t)(w->fbh-1-y)*row;memcpy(tmp,a,row);memcpy(a,b,row);memcpy(b,tmp,row);}free(tmp);}
    w->capture_serial=g_serial++;
    eglSwapBuffers(g_display,w->surface);
    if(w->index==2){g_render_cycles++;compose_and_write();}
    if(w->index>=3 && w->visible)compose_and_write();
}
void glfwSwapInterval(int interval){(void)interval;}
void glfwWaitEventsTimeout(double timeout){
    g_wait_calls++;
    if(timeout>0){struct timespec ts={(time_t)timeout,(long)((timeout-(time_t)timeout)*1e9)};nanosleep(&ts,NULL);}
    const char*menu=getenv("APLAY_SCREENSHOT_MENU");
    if(menu&&*menu&&!g_menu_injected&&g_render_cycles>=1&&g_windows[0]&&g_windows[0]->mouse_cb){
        GLFWwindow*w=g_windows[0];w->cursor_x=245;w->cursor_y=24;
        if(w->cursor_cb)w->cursor_cb(w,w->cursor_x,w->cursor_y);
        w->mouse_cb(w,GLFW_MOUSE_BUTTON_RIGHT,GLFW_PRESS,0);
        w->mouse_cb(w,GLFW_MOUSE_BUTTON_RIGHT,GLFW_RELEASE,0);
        g_menu_injected=1;
    }
}
double glfwGetTime(void){struct timespec n;clock_gettime(CLOCK_MONOTONIC,&n);return(n.tv_sec-g_start.tv_sec)+(n.tv_nsec-g_start.tv_nsec)/1e9;}
int glfwWindowShouldClose(GLFWwindow*w){if(!w)return 1;if(w->index==0&&(g_render_cycles>=g_finish_after_frames || g_wait_calls>=8))return 1;return w->should_close;}
void glfwSetWindowShouldClose(GLFWwindow*w,int v){if(w)w->should_close=v;}
void glfwShowWindow(GLFWwindow*w){if(w)w->visible=1;}
void glfwHideWindow(GLFWwindow*w){if(w)w->visible=0;}
void glfwFocusWindow(GLFWwindow*w){(void)w;}
void glfwGetWindowSize(GLFWwindow*w,int*a,int*b){if(a)*a=w?w->width:0;if(b)*b=w?w->height:0;}
void glfwSetWindowSize(GLFWwindow*w,int width,int height){if(!w)return;w->width=width;w->height=height;if(w->size_cb)w->size_cb(w,width,height);if(w->framebuffer_cb)w->framebuffer_cb(w,w->fbw,w->fbh);}
void glfwGetFramebufferSize(GLFWwindow*w,int*a,int*b){if(a)*a=w?w->fbw:0;if(b)*b=w?w->fbh:0;}
void glfwGetWindowPos(GLFWwindow*w,int*a,int*b){if(a)*a=w?w->x:0;if(b)*b=w?w->y:0;}
void glfwSetWindowPos(GLFWwindow*w,int x,int y){if(w){w->x=x;w->y=y;}}
void glfwSetWindowSizeLimits(GLFWwindow*w,int a,int b,int c,int d){(void)w;(void)a;(void)b;(void)c;(void)d;}
void glfwSetWindowAspectRatio(GLFWwindow*w,int a,int b){(void)w;(void)a;(void)b;}
void glfwSetWindowAttrib(GLFWwindow*w,int a,int b){(void)w;(void)a;(void)b;}
void glfwSetWindowUserPointer(GLFWwindow*w,void*p){if(w)w->user=p;}
void*glfwGetWindowUserPointer(GLFWwindow*w){return w?w->user:NULL;}
void glfwGetCursorPos(GLFWwindow*w,double*a,double*b){if(a)*a=w?w->cursor_x:0;if(b)*b=w?w->cursor_y:0;}
void glfwSetCursor(GLFWwindow*w,GLFWcursor*c){(void)w;(void)c;}
GLFWcursor*glfwCreateStandardCursor(int shape){GLFWcursor*c=(GLFWcursor*)malloc(sizeof(*c));if(c)c->shape=shape;return c;}
void glfwDestroyCursor(GLFWcursor*c){free(c);}
GLFWframebuffersizefun glfwSetFramebufferSizeCallback(GLFWwindow*w,GLFWframebuffersizefun f){GLFWframebuffersizefun o=w?w->framebuffer_cb:NULL;if(w)w->framebuffer_cb=f;return o;}
GLFWwindowsizefun glfwSetWindowSizeCallback(GLFWwindow*w,GLFWwindowsizefun f){GLFWwindowsizefun o=w?w->size_cb:NULL;if(w)w->size_cb=f;return o;}
GLFWcursorposfun glfwSetCursorPosCallback(GLFWwindow*w,GLFWcursorposfun f){GLFWcursorposfun o=w?w->cursor_cb:NULL;if(w)w->cursor_cb=f;return o;}
GLFWmousebuttonfun glfwSetMouseButtonCallback(GLFWwindow*w,GLFWmousebuttonfun f){GLFWmousebuttonfun o=w?w->mouse_cb:NULL;if(w)w->mouse_cb=f;return o;}
GLFWscrollfun glfwSetScrollCallback(GLFWwindow*w,GLFWscrollfun f){GLFWscrollfun o=w?w->scroll_cb:NULL;if(w)w->scroll_cb=f;return o;}
GLFWkeyfun glfwSetKeyCallback(GLFWwindow*w,GLFWkeyfun f){GLFWkeyfun o=w?w->key_cb:NULL;if(w)w->key_cb=f;return o;}
GLFWcharfun glfwSetCharCallback(GLFWwindow*w,GLFWcharfun f){GLFWcharfun o=w?w->char_cb:NULL;if(w)w->char_cb=f;return o;}
GLFWmonitor**glfwGetMonitors(int*count){static GLFWmonitor m;static GLFWmonitor*list[]={&m};if(count)*count=1;return list;}
void glfwGetMonitorWorkarea(GLFWmonitor*m,int*x,int*y,int*w,int*h){(void)m;if(x)*x=0;if(y)*y=0;if(w)*w=1920;if(h)*h=1080;}
GLFWglproc glfwGetProcAddress(const char*n){void*p=eglGetProcAddress(n);if(!p&&g_libgl)p=dlsym(g_libgl,n);return(GLFWglproc)p;}
void screenshot_glfw_set_output(const char*path,int variant){if(path&&*path)snprintf(g_output,sizeof(g_output),"%s",path);g_background_variant=variant;}
void screenshot_glfw_finish_after_frames(int frames){if(frames>0)g_finish_after_frames=frames;}
