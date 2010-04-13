/* OpenGLES2 test program.
 *
 * Copyright 2010 (c) Nokia Corporation.
 *
 * Kimmo Hämäläinen <kimmo.hamalainen@nokia.com>
 */

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <math.h>
/* #include <X11/extensions/Xrender.h> */
#include <GLES2/gl2.h>
#include <EGL/egl.h>

#ifdef N900
#define WIN_W 800
#else
#define WIN_W 864
#endif

#define VERTEX_SHADER "\
precision lowp float;\n\
attribute vec4 vertex_attrib;\n\
attribute vec4 tex_coord_attrib;\n\
attribute vec4 color_attrib;\n\
uniform mat4 mvp_matrix;\n\
uniform mat4 pers_matrix;\n\
uniform mat4 trans_matrix;\n\
varying lowp vec4 frag_color;\n\
varying mediump vec2 tex_coord;\n\
void main()\n\
{\n\
  const mat4 tex_matrix = mat4(1.0, 0.0, 0.0, 0.0,\n\
                               0.0, 1.0, 0.0, 0.0,\n\
                               0.0, 0.0, 1.0, 0.0,\n\
                               0.0, 0.0, 0.0, 1.0);\n\
  /*vec4 transformed_tex_coord = tex_matrix * tex_coord_attrib;\n\
  tex_coord = transformed_tex_coord.st / transformed_tex_coord.q;*/\n\
  gl_Position = ((mvp_matrix * trans_matrix) * pers_matrix) * vertex_attrib;\n\
  frag_color = color_attrib;\n\
}\n"

#define FRAGMENT_SHADER "\
varying lowp vec4 frag_color;\n\
varying mediump vec2 tex_coord;\n\
/*uniform lowp sampler2D texture_unit;*/\n\
void main()\n\
{\n\
  gl_FragColor = frag_color;\n\
  /*gl_FragColor = texture2D (texture_unit, tex_coord);*/\n\
}\n"

static GLint mvp_matrix_uniform, pers_matrix_uniform, trans_matrix_uniform;
static Atom ping_atom, wm_protocols_atom;

static EGLint egl_config_attribs[] = {
    EGL_BUFFER_SIZE, 16,
    EGL_DEPTH_SIZE, 0,
    EGL_STENCIL_SIZE, 0,
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_NONE
};

static void set_fullscreen (Display *dpy, Window w)
{
  Atom wm_state, state_fs;

  wm_state = XInternAtom (dpy, "_NET_WM_STATE", False);
  state_fs = XInternAtom (dpy, "_NET_WM_STATE_FULLSCREEN", False);

  XChangeProperty (dpy, w, wm_state,
                   XA_ATOM, 32, PropModeReplace,
                   (unsigned char *) &state_fs, 1);
}

static void set_support_wm_ping (Display *dpy, Window w)
{
  XChangeProperty (dpy, w, wm_protocols_atom,
                   XA_ATOM, 32, PropModeReplace,
                   (unsigned char *) &ping_atom, 1);
}

static void set_window_type (Display *dpy, Window w)
{
  Atom w_type, normal;

  w_type = XInternAtom (dpy, "_NET_WM_WINDOW_TYPE", False);
  normal = XInternAtom (dpy, "_NET_WM_WINDOW_TYPE_NORMAL", False);

  XChangeProperty (dpy, w, w_type,
                   XA_ATOM, 32, PropModeReplace,
                   (unsigned char *) &normal, 1);
}

static GLuint create_shader (GLenum type, const char *source)
{
  GLuint shader;
  GLint compiled;

  shader = glCreateShader (type);
  glShaderSource (shader, 1, &source, NULL);
  glCompileShader (shader);
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (!compiled) {
    char infolog[256];
    glGetShaderInfoLog(shader, 256, NULL, infolog);
    printf("shader compilation failed: %s\n", infolog);
    printf("failed shader: '\n%s'\n", source);
  }

  return shader;
}

#ifdef N900  /* accelerometer reading is N900-specific */
#define XA_ERR 36
#define YA_ERR 54

static void read_accel (GLfloat *xa, GLfloat *ya)
{
  FILE *fd;
  int xi, yi;
  if (!(fd = fopen("/sys/class/i2c-adapter/i2c-3/3-001d/coord", "r"))) {
    *xa = *ya = 0;
    return;
  }
  if (fscanf(fd, "%d %d", &xi, &yi) != 2) xi = yi = 0;
  fclose(fd);
  /* correct for error */
  if (xi <= XA_ERR && xi >= -XA_ERR) xi = 0;
  else if (xi > XA_ERR || xi < -XA_ERR) xi /= XA_ERR;
  if (yi <= YA_ERR && yi >= -YA_ERR) yi = 0;
  else if (yi > YA_ERR || yi < -YA_ERR) yi /= YA_ERR;
  *xa = (float)xi;
  *ya = (float)yi;
}
#else
#include <time.h>
/* returns bogus values based on the time */
static void read_accel (GLfloat *xa, GLfloat *ya)
{
  switch (time(NULL) % 4) {
  case 0:
          *xa = *ya = 1.0 * rand() / RAND_MAX;
          break;
  case 1:
          *xa = *ya = -1.0 * rand() / RAND_MAX;
          break;
  case 2:
          *xa = 1.0 * rand() / RAND_MAX;
          *ya = -1.0 * rand() / RAND_MAX;
          break;
  case 3:
          *xa = -1.0 * rand() / RAND_MAX;
          *ya = 1.0 * rand() / RAND_MAX;
          break;
  }
}
#endif

#define ASPECT (480.0 / WIN_W)

#define NEAR -1.0
#define FAR 20.0 
#define LEFT -1.0
#define RIGHT 1.0
#define TOP (1.0 * ASPECT)
#define BOT (-1.0 * ASPECT)

/*
static GLfloat pers_matrix[] = {
    2 * NEAR / (RIGHT - LEFT), 0, (RIGHT + LEFT) / (RIGHT - LEFT), 0,
    0, 2 * NEAR / (TOP - BOTTOM), (TOP + BOTTOM) / (TOP - BOTTOM), 0,
    0, 0, -(FAR + NEAR) / (FAR - NEAR), -2 * FAR * NEAR / (FAR - NEAR),
    0, 0, -1, 0};
static GLfloat pers_matrix[] = {
 2 * NEAR / (RIGHT - LEFT), 0, 0, 0,
 0, 2 * NEAR / (TOP - BOTTOM), 0, 0,
 (RIGHT + LEFT) / (RIGHT - LEFT), (TOP + BOTTOM) / (TOP - BOTTOM), -(FAR + NEAR) / (FAR - NEAR), -1,
 0, 0, -2 * FAR * NEAR / (FAR - NEAR), 0};
    */

/* orthographic projection */
static GLfloat pers_matrix[] = {
  2.0 / (RIGHT - LEFT), 0, 0, 0,
  0, 2.0 / (TOP - BOT), 0, 0,
  0, 0, -2.0 / (FAR - NEAR), 0,
  (RIGHT+LEFT)/(RIGHT-LEFT), (TOP+BOT)/(TOP-BOT), (FAR+NEAR)/(FAR-NEAR), 1};

static GLfloat color[] = {1.0, 1.0, 1.0, 1.0,
                          0.0, 0.0, 1.0, 1.0,
                          0.0, 1.0, 0.0, 1.0};
#define RECT_W 0.2
#define RECT_H 0.2
static GLfloat rect_verts[] = {0, 0, 0,
                               RECT_W, 0, 0,
                               RECT_W, -RECT_H, 0,
                               0, -RECT_H, 0,
                               0, 0, 0};
static GLfloat mvp_matrix[] = {1, 0, 0, 0,
                               0, 1, 0, 0,
                               0, 0, 1, 0,
                               0, 0, 0, 1};

#define FRICTION 0.03
#define GRAVITY 0.5
/* negligible velocity: */
#define N_VELO 0.1

static GLfloat trans_matrix[] = {1, 0, 0, 0,
                                 0, 1, 0, 0,
                                 0, 0, 1, 0,
                                 0, 0, 0, 1};

static void draw_frame (EGLDisplay edpy, EGLSurface egl_surface, int frame)
{
        /*
  float f = frame / ((total_frames - 1) * 1.0);
  float deg = 360 * f;
  */
  static int first_call = 1;
  GLfloat xa, ya;
  static GLfloat tx = 0, ty = 0; /* cumulative translation */
  static GLfloat vx = 0, vy = 0; /* cumulative velocity */
  static GLfloat prev_tx = 0, prev_ty = 0; /* previous translation */

  /*GLfloat rad = deg * M_PI / 180.0;*/
  /*
  GLfloat mvp_matrix[] = {cosf(rad), sinf(rad), 0.0, 0.0,
                          -sinf(rad), cosf(rad), 0.0, 0.0,
                          0.0, 0.0, 1.0, 0.0,
                          0.0, 0.0, 0.0, 1.0};
                          */

  glClear(GL_COLOR_BUFFER_BIT);

  if (first_call) {
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 0, color);
    glUniformMatrix4fv(pers_matrix_uniform, 1, GL_FALSE, pers_matrix);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, rect_verts);
    glUniformMatrix4fv(mvp_matrix_uniform, 1, GL_FALSE, mvp_matrix);
    glUniformMatrix4fv(trans_matrix_uniform, 1, GL_FALSE, trans_matrix);
  }

  /* acceleration */
  read_accel(&xa, &ya);
  if (xa) vx += xa * GRAVITY;
  if (ya) vy += ya * GRAVITY;

  /* too small velocity => no velocity */
  if (vx < N_VELO && vx > -N_VELO) vx = 0;
  if (vy < N_VELO && vy > -N_VELO) vy = 0;

  /* friction */
  if (vx) vx -= vx * FRICTION;
  if (vy) vy -= vy * FRICTION;

  /*
  if (vx || vy)
    printf ("velocity: %f %f\n", -vx, vy);
    */

  if (vx) tx = tx - (0.03 * vx);
  if (vy) ty = ty + (0.03 * vy);

  /* check for collision with screen boundaries */
  if (tx < LEFT) {
    /* left side of the screen */
    vx = -vx;
    tx = LEFT;
  } else if (tx > RIGHT - RECT_W) {
    /* right side */
    vx = -vx;
    tx = RIGHT - RECT_W;
  }
  if (ty > TOP / ASPECT) {
    /* top of the screen */
    vy = -vy;
    ty = TOP / ASPECT;
  } else if (ty < (BOT + RECT_H) / ASPECT) {
    /* bottom */
    vy = -vy;
    ty = (BOT + RECT_H) / ASPECT;
  }

  /*
  if (tx || ty)
    printf ("translation: %f %f\n", tx, ty);
    */

  if (prev_tx != tx || prev_ty != ty) {
    prev_tx = trans_matrix[12] = tx;
    prev_ty = trans_matrix[13] = ty;
    glUniformMatrix4fv(trans_matrix_uniform, 1, GL_FALSE, trans_matrix);
  }

  glDrawArrays(GL_TRIANGLE_STRIP, 0, 5);

  eglSwapBuffers(edpy, egl_surface);
  first_call = 0;
}

static void reply_ping(Display *dpy, XEvent *xev)
{
        ((XAnyEvent*)xev)->window = DefaultRootWindow(dpy);
        XSendEvent(dpy, DefaultRootWindow(dpy), False,
                   SubstructureRedirectMask | SubstructureNotifyMask, xev);
}

int main()
{
        Display *dpy;
        EGLDisplay edpy;
        Window w;
        GLuint program, v_shader, f_shader;
        EGLConfig configs[20], chosen_config;
        EGLint config_count;
        EGLSurface egl_surface = EGL_NO_SURFACE;
        EGLContext egl_context;
        int i;
        const EGLint context_attribs[]
            = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
        GLint linked;
        XWMHints *wmhints;
        int ping_reply_enabled = 1;
        KeySym space_bar = XStringToKeysym("space");

        if (!(dpy = XOpenDisplay(NULL))) {
                printf("XOpenDisplay failed\n");
                return 1;
        }
        w = XCreateWindow(dpy, DefaultRootWindow(dpy), 0, 0,
                          WIN_W, 480, 0, CopyFromParent, InputOutput,
                          CopyFromParent,
                          0, NULL);
        set_fullscreen(dpy, w);
        set_window_type(dpy, w);
        ping_atom = XInternAtom (dpy, "_NET_WM_PING", False);
        wm_protocols_atom = XInternAtom (dpy, "WM_PROTOCOLS", False);
        set_support_wm_ping(dpy, w);

        /* tell the window manager that we want keyboard focus */
        wmhints = XAllocWMHints();
        wmhints->input = True;
        wmhints->flags = InputHint;
        XSetWMHints(dpy, w, wmhints);
        XFree(wmhints);

        edpy = eglGetDisplay((NativeDisplayType)dpy);
        if (!eglInitialize(edpy, NULL, NULL)) {
                printf("eglInitialize failed\n");
                return 1;
        }

        if (!eglChooseConfig (edpy, egl_config_attribs, configs,
                              20, &config_count) || !config_count) {
                printf("eglChooseConfig failed\n");
                return 1;
        }

        for (i = 0; i < config_count; ++i) {
          egl_surface = eglCreateWindowSurface (edpy, configs[i],
                                              (NativeWindowType)w, NULL);
          if (egl_surface != EGL_NO_SURFACE) {
            chosen_config = configs[i];
            break;
          }
        }
        if (egl_surface == EGL_NO_SURFACE) {
                printf("eglCreateWindowSurface failed\n");
                return 1;
        }

        egl_context = eglCreateContext (edpy, chosen_config,
                                        EGL_NO_CONTEXT,
                                        context_attribs);
        eglMakeCurrent(edpy, egl_surface, egl_surface, egl_context);

        program = glCreateProgram();
        v_shader = create_shader(GL_VERTEX_SHADER, VERTEX_SHADER);
        glAttachShader(program, v_shader);
        f_shader = create_shader(GL_FRAGMENT_SHADER, FRAGMENT_SHADER);
        glAttachShader(program, f_shader);
        glBindAttribLocation(program, 0, "vertex_attrib");
        glBindAttribLocation(program, 1, "tex_coord_attrib");
        glBindAttribLocation(program, 2, "color_attrib");
        glLinkProgram(program);
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked) {
          char infolog[256];
          infolog[0] = '\0';
          glGetProgramInfoLog(program, 256, NULL, infolog);
          printf ("glLinkProgram failed: '%s'\n", infolog);
        }
        glUseProgram(program);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glEnableVertexAttribArray(2);
        if ((mvp_matrix_uniform
                = glGetUniformLocation(program, "mvp_matrix")) < 0)
          printf("uniform was not found\n");
        if ((pers_matrix_uniform
                = glGetUniformLocation(program, "pers_matrix")) < 0)
          printf("uniform was not found\n");
        if ((trans_matrix_uniform
                = glGetUniformLocation(program, "trans_matrix")) < 0)
          printf("uniform was not found\n");

        XSelectInput (dpy, w, ExposureMask | ButtonReleaseMask
                              | ButtonPressMask | KeyReleaseMask);

        XMapWindow(dpy, w);
        glClearColor(0.1, 0.1, 0.1, 1.0);

        XEvent last_ping;
        last_ping.type = 0;
        while (1) {
                XEvent xev;

                if (XEventsQueued (dpy, QueuedAfterFlush))
                    XNextEvent(dpy, &xev);
                else {
                    draw_frame(edpy, egl_surface, 0);
                    usleep(50 * 1000);
                    continue;
                }

                switch (xev.type) {
                  XKeyEvent *kev;
                  XClientMessageEvent *cev;
                  case KeyRelease:
                        kev = (XKeyEvent *)&xev;
                        if (XLookupKeysym(kev, 0) == space_bar) {
                            ping_reply_enabled = !ping_reply_enabled;
                            if (ping_reply_enabled) {
                                printf("ping replies enabled\n");
                                if (last_ping.type)
                                    reply_ping(dpy, &last_ping);
                            } else
                                printf("ping replies disabled\n");
                        }
                        break;
                  case ClientMessage:
                        cev = (XClientMessageEvent *)&xev;
                        if (cev->message_type == wm_protocols_atom
                            && cev->data.l[0] == ping_atom) {
                            if (ping_reply_enabled) {
                                reply_ping(dpy, &xev);
                                last_ping.type = 0;
                            } else
                                last_ping = xev;
                        }
                        break;
                }
        }

        return 0;
}
