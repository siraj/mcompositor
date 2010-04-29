/***************************************************************************
**
** Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (directui@nokia.com)
**
** This file is part of mcompositor.
**
** If you have questions regarding the use of this file, please contact
** Nokia at directui@nokia.com.
**
** This library is free software; you can redistribute it and/or
** modify it under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation
** and appearing in the file LICENSE.LGPL included in the packaging
** of this file.
**
****************************************************************************/

#include "mtexturepixmapitem.h"
#include "mtexturepixmapitem_p.h"
#include "mcompositemanager.h"
#include "mcompositemanager_p.h"
#include "mcompositescene.h"
#include "msimplewindowframe.h"
#include "mdecoratorframe.h"
#include "mdevicestate.h"
#include <mrmiserver.h>

#include <QX11Info>
#include <QByteArray>
#include <QVector>

#include <X11/Xutil.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/Xfixes.h>
#include <X11/Xatom.h>
#include <X11/Xmd.h>

#define TRANSLUCENT 0xe0000000
#define OPAQUE      0xffffffff

/*
  The reason why we have to look at the entire redirected buffers is that we
  never know if a window is physically visible or not when compositing mode is
  toggled  e.g. What if a window is partially translucent and how do we know
  that the window beneath it can be physically seen by the user and the window
  beneath that window and so on?

  But this is mitigated anyway by the fact that the items representing those
  buffers know whether they are redirected or not and will not switch to
  another state if they are already in that state. So the overhead of freeing
  and allocating EGL resources for the entire buffers is low.
 */

// own log for catching bugs
#define _log(txt, args... ) { FILE *out; out = fopen("/tmp/mcompositor.log", "a"); if(out) { fprintf(out, "" txt, ##args ); fclose(out); } }

#define COMPOSITE_WINDOW(X) windows.value(X, 0)
#define FULLSCREEN_WINDOW(X) \
        ((X)->netWmState().indexOf(ATOM(_NET_WM_STATE_FULLSCREEN)) != -1)

class MCompAtoms
{
public:

    // note that this enum is ordered and presents
    // the depth ordering of different window types
    enum Type {
        DESKTOP = 0,
        NORMAL,
        FRAMELESS,
        DOCK,
        INPUT,
        ABOVE,
        NOTIFICATION,
        DECORATOR,
        UNKNOWN
    };

    enum Atoms {
        // window manager
        WM_PROTOCOLS,
        WM_DELETE_WINDOW,
        WM_TAKE_FOCUS,

        // window types
        _NET_SUPPORTED,
        _NET_SUPPORTING_WM_CHECK,
        _NET_WM_NAME,
        _NET_WM_WINDOW_TYPE,
        _NET_WM_WINDOW_TYPE_DESKTOP,
        _NET_WM_WINDOW_TYPE_NORMAL,
        _NET_WM_WINDOW_TYPE_DOCK,
        _NET_WM_WINDOW_TYPE_INPUT,
        _NET_WM_WINDOW_TYPE_NOTIFICATION,
        _NET_WM_WINDOW_TYPE_DIALOG,
        _NET_WM_STATE_ABOVE,
        _NET_WM_STATE_SKIP_TASKBAR,
        _NET_WM_STATE_FULLSCREEN,
        _KDE_NET_WM_WINDOW_TYPE_OVERRIDE,

        // window properties
        _NET_WM_WINDOW_OPACITY,
        _NET_WM_STATE,
        _NET_WM_ICON_GEOMETRY,
        WM_STATE,

        // misc
        _NET_WM_PID,
        _NET_WM_PING,

        // root messages
        _NET_ACTIVE_WINDOW,
        _NET_CLOSE_WINDOW,
        _NET_CLIENT_LIST,
        _NET_CLIENT_LIST_STACKING,
        WM_CHANGE_STATE,

        // DUI-specific
        _MEEGOTOUCH_DECORATOR_WINDOW,
        _DUI_STATUSBAR_OVERLAY,
        _DUI_GLOBAL_ALPHA,

#ifdef WINDOW_DEBUG
        _DUI_WM_INFO,
        _DUI_WM_WINDOW_ZVALUE,
        _DUI_WM_WINDOW_COMPOSITED_VISIBLE,
        _DUI_WM_WINDOW_COMPOSITED_INVISIBLE,
        _DUI_WM_WINDOW_DIRECT_VISIBLE,
        _DUI_WM_WINDOW_DIRECT_INVISIBLE,
#endif

        ATOMS_TOTAL
    };
    static MCompAtoms *instance();
    Type windowType(Window w);
    bool isDecorator(Window w);
    bool statusBarOverlayed(Window w);
    int getPid(Window w);
    long getWmState(Window w);
    bool hasState(Window w, Atom a);
    QRectF iconGeometry(Window w);
    QVector<Atom> netWmStates(Window w);
    unsigned int get_opacity_prop(Display *dpy, Window w, unsigned int def);
    double get_opacity_percent(Display *dpy, Window w, double def);
    int globalAlphaFromWindow(Window w);

    Atom getAtom(const unsigned int name);
    Atom getType(Window w);

    static Atom atoms[ATOMS_TOTAL];

private:
    explicit MCompAtoms();
    static MCompAtoms *d;

    int intValueProperty(Window w, Atom property);
    Atom getAtom(Window w, Atoms atomtype);

    Display *dpy;
};

#define ATOM(t) MCompAtoms::instance()->getAtom(MCompAtoms::t)
Atom MCompAtoms::atoms[MCompAtoms::ATOMS_TOTAL];
Window MCompositeManagerPrivate::stack[TOTAL_LAYERS];
MCompAtoms *MCompAtoms::d = 0;
static bool hasDock  = false;
static QRect availScreenRect = QRect();

// temporary launch indicator. will get replaced later
static QGraphicsTextItem *launchIndicator = 0;

static Window transient_for(Window window);

MCompAtoms *MCompAtoms::instance()
{
    if (!d)
        d = new MCompAtoms();
    return d;
}

MCompAtoms::MCompAtoms()
{
    const char *atom_names[] = {
        "WM_PROTOCOLS",
        "WM_DELETE_WINDOW",
        "WM_TAKE_FOCUS",

        "_NET_SUPPORTED",
        "_NET_SUPPORTING_WM_CHECK",
        "_NET_WM_NAME",
        "_NET_WM_WINDOW_TYPE",
        "_NET_WM_WINDOW_TYPE_DESKTOP",
        "_NET_WM_WINDOW_TYPE_NORMAL",
        "_NET_WM_WINDOW_TYPE_DOCK",
        "_NET_WM_WINDOW_TYPE_INPUT",
        "_NET_WM_WINDOW_TYPE_NOTIFICATION",
        "_NET_WM_WINDOW_TYPE_DIALOG",

        // window states
        "_NET_WM_STATE_ABOVE",
        "_NET_WM_STATE_SKIP_TASKBAR",
        "_NET_WM_STATE_FULLSCREEN",
        // uses the KDE standard for frameless windows
        "_KDE_NET_WM_WINDOW_TYPE_OVERRIDE",

        "_NET_WM_WINDOW_OPACITY",
        "_NET_WM_STATE",
        "_NET_WM_ICON_GEOMETRY",
        "WM_STATE",

        // misc
        "_NET_WM_PID",
        "_NET_WM_PING",

        // root messages
        "_NET_ACTIVE_WINDOW",
        "_NET_CLOSE_WINDOW",
        "_NET_CLIENT_LIST",
        "_NET_CLIENT_LIST_STACKING",
        "WM_CHANGE_STATE",

        "_MEEGOTOUCH_DECORATOR_WINDOW",
        // TODO: remove this when statusbar in-scene approach is done
        "_DUI_STATUSBAR_OVERLAY",
        "_DUI_GLOBAL_ALPHA",

#ifdef WINDOW_DEBUG
        // custom properties for CITA
        "_DUI_WM_INFO",
        "_DUI_WM_WINDOW_ZVALUE",
        "_DUI_WM_WINDOW_COMPOSITED_VISIBLE",
        "_DUI_WM_WINDOW_COMPOSITED_INVISIBLE",
        "_DUI_WM_WINDOW_DIRECT_VISIBLE",
        "_DUI_WM_WINDOW_DIRECT_INVISIBLE",
#endif
    };

    Q_ASSERT(sizeof(atom_names) == ATOMS_TOTAL);

    dpy = QX11Info::display();

    if (!XInternAtoms(dpy, (char **)atom_names, ATOMS_TOTAL, False, atoms))
        qCritical("XInternAtoms failed");

    XChangeProperty(dpy, QX11Info::appRootWindow(), atoms[_NET_SUPPORTED],
                    XA_ATOM, 32, PropModeReplace, (unsigned char *)atoms,
                    ATOMS_TOTAL);
}

/* FIXME Workaround for bug NB#161282 */
static bool is_desktop_window(Window w, Atom type = 0)
{
    Atom a;
    if (!type)
        a = MCompAtoms::instance()->getType(w);
    else
        a = type;
    if (a == ATOM(_NET_WM_WINDOW_TYPE_DESKTOP))
        return true;
    XTextProperty textp;
    if (!XGetWMName(QX11Info::display(), w, &textp))
        return false;
    if (strcmp((const char *)textp.value, "duihome") == 0
            && a == ATOM(_NET_WM_WINDOW_TYPE_NORMAL)) {
        return true;
    }
    return false;
}

/* FIXME: workaround for bug NB#161629 */
static bool is_desktop_dock(Window w, Atom type = 0)
{
    Atom a;
    if (!type)
        a = MCompAtoms::instance()->getType(w);
    else
        a = type;
    if (a != ATOM(_NET_WM_WINDOW_TYPE_DOCK))
        return false;
    /*  // WMName of the dock is unstable, match all docks...
    XTextProperty textp;
    if (!XGetWMName(QX11Info::display(), w, &textp))
        return false;
    if (strcmp((const char *)textp.value, "duihome") == 0) {
        return true;
    }
    */
    return true;
}

MCompAtoms::Type MCompAtoms::windowType(Window w)
{
    // freedesktop.org window type
    Atom a = getType(w);
    if (is_desktop_window(w, a))
        return DESKTOP;
    else if (a == atoms[_NET_WM_WINDOW_TYPE_NORMAL])
        return NORMAL;
    else if (a == atoms[_NET_WM_WINDOW_TYPE_DOCK])
        return DOCK;
    else if (a == atoms[_NET_WM_WINDOW_TYPE_INPUT])
        return INPUT;
    else if (a == atoms[_NET_WM_WINDOW_TYPE_NOTIFICATION])
        return NOTIFICATION;
    else if (a == atoms[_KDE_NET_WM_WINDOW_TYPE_OVERRIDE])
        return FRAMELESS;

    if (transient_for(w))
        return UNKNOWN;
    else // fdo spec suggests unknown non-transients must be normal
        return NORMAL;
}

bool MCompAtoms::isDecorator(Window w)
{
    return (intValueProperty(w, atoms[_MEEGOTOUCH_DECORATOR_WINDOW]) == 1);
}

// Remove this when statusbar in-scene approach is done
bool MCompAtoms::statusBarOverlayed(Window w)
{
    return (intValueProperty(w, atoms[_DUI_STATUSBAR_OVERLAY]) == 1);
}

int MCompAtoms::getPid(Window w)
{
    return intValueProperty(w, atoms[_NET_WM_PID]);
}

bool MCompAtoms::hasState(Window w, Atom a)
{
    QVector<Atom> states = netWmStates(w);
    return states.indexOf(a) != -1;
}

QRectF MCompAtoms::iconGeometry(Window w)
{
    Atom actual;
    int format;
    unsigned long n, left;
    unsigned char *data;
    int result = XGetWindowProperty(QX11Info::display(), w, atoms[_NET_WM_ICON_GEOMETRY], 0L, 4L, False,
                                    XA_CARDINAL, &actual, &format,
                                    &n, &left, &data);
    if (result == Success && data != NULL) {
        unsigned long *geom = (unsigned long *) data;
        QRectF r(geom[0], geom[1], geom[2], geom[3]);
        XFree((void *) data);
        return r;

    }
    return QRectF(); // empty
}

QVector<Atom> MCompAtoms::netWmStates(Window w)
{
    QVector<Atom> ret;

    Atom actual;
    int format;
    unsigned long n, left;
    unsigned char *data;
    int result = XGetWindowProperty(QX11Info::display(), w, atoms[_NET_WM_STATE], 0, 0,
                                    False, XA_ATOM, &actual, &format,
                                    &n, &left, &data);
    if (result == Success && actual == XA_ATOM && format == 32) {
        ret.resize(left / 4);
        XFree((void *) data);

        if (XGetWindowProperty(QX11Info::display(), w, atoms[_NET_WM_STATE], 0,
                               ret.size(), False, XA_ATOM, &actual, &format,
                               &n, &left, &data) != Success) {
            ret.clear();
        } else if (n != (ulong)ret.size())
            ret.resize(n);

        if (!ret.isEmpty())
            memcpy(ret.data(), data, ret.size() * sizeof(Atom));

        XFree((void *) data);
    }

    return ret;
}

long MCompAtoms::getWmState(Window w)
{
    Atom actual;
    int format;
    unsigned long n, left;
    unsigned char *data = 0, state = WithdrawnState;
    int result = XGetWindowProperty(QX11Info::display(), w,
                                    atoms[WM_STATE], 0L, 2L, False,
                                    atoms[WM_STATE], &actual, &format,
                                    &n, &left, &data);
    if (result == Success && data != NULL) {
        state = *data;
        if (data)
            XFree((void *)data);
    }

    return state;
}

unsigned int MCompAtoms::get_opacity_prop(Display *dpy, Window w, unsigned int def)
{
    Q_UNUSED(dpy);
    Atom actual;
    int format;
    unsigned long n, left;

    unsigned char *data;
    int result = XGetWindowProperty(QX11Info::display(), w, atoms[_NET_WM_WINDOW_OPACITY], 0L, 1L, False,
                                    XA_CARDINAL, &actual, &format,
                                    &n, &left, &data);
    if (result == Success && data != NULL) {
        unsigned int i;
        memcpy(&i, data, sizeof(unsigned int));
        XFree((void *) data);
        return i;
    }
    return def;
}

double MCompAtoms::get_opacity_percent(Display *dpy, Window w, double def)
{
    unsigned int opacity = get_opacity_prop(dpy, w,
                                            (unsigned int)(OPAQUE * def));
    return opacity * 1.0 / OPAQUE;
}

int MCompAtoms::globalAlphaFromWindow(Window w)
{
    Atom actual;
    int format;
    unsigned long n, left;

    unsigned char *data;
    int result = XGetWindowProperty(QX11Info::display(), w, atoms[_DUI_GLOBAL_ALPHA], 0L, 1L, False,
                                    XA_CARDINAL, &actual, &format,
                                    &n, &left, &data);
    if (result == Success && data != NULL) {
        unsigned int i;
        memcpy(&i, data, sizeof(unsigned int));
        XFree((void *) data);
        double opacity = i * 1.0 / OPAQUE;
        return (opacity * 255);
    }

    return 255;
}

Atom MCompAtoms::getAtom(const unsigned int name)
{
    return atoms[name];
}

int MCompAtoms::intValueProperty(Window w, Atom property)
{
    Atom actual;
    int format;
    unsigned long n, left;
    unsigned char *data;

    int result = XGetWindowProperty(QX11Info::display(), w, property, 0L, 1L, False,
                                    XA_CARDINAL, &actual, &format,
                                    &n, &left, &data);
    if (result == Success && data != None) {
        int p = *((unsigned long *)data);
        XFree((void *)data);
        return p;
    }

    return 0;
}

Atom MCompAtoms::getType(Window w)
{
    Atom t = getAtom(w, _NET_WM_WINDOW_TYPE);
    if (t)
        return t;
    return atoms[_NET_WM_WINDOW_TYPE_NORMAL];
}

Atom MCompAtoms::getAtom(Window w, Atoms atomtype)
{
    Atom actual;
    int format;
    unsigned long n, left;
    unsigned char *data;

    int result = XGetWindowProperty(QX11Info::display(), w, atoms[atomtype], 0L, 1L, False,
                                    XA_ATOM, &actual, &format,
                                    &n, &left, &data);
    if (result == Success && data != None) {
        Atom a;
        memcpy(&a, data, sizeof(Atom));
        XFree((void *) data);
        return a;
    }
    return 0;
}

static Window transient_for(Window window)
{
    Window transient_for = 0;
    XGetTransientForHint(QX11Info::display(), window, &transient_for);
    return transient_for;
}

static void skiptaskbar_wm_state(int toggle, Window window)
{
    Atom skip = ATOM(_NET_WM_STATE_SKIP_TASKBAR);
    MCompAtoms *atom = MCompAtoms::instance();
    QVector<Atom> states = atom->netWmStates(window);
    bool update_root = false;
    int i = states.indexOf(skip);

    switch (toggle) {
    case 0: {
        if (i != -1) {
            do {
                states.remove(i);
                i = states.indexOf(skip);
            } while (i != -1);
            XChangeProperty(QX11Info::display(), window,
                            ATOM(_NET_WM_STATE), XA_ATOM, 32, PropModeReplace,
                            (unsigned char *) states.data(), states.size());
            update_root = true;
        }
    } break;
    case 1: {
        if (i == -1) {
            states.append(skip);
            XChangeProperty(QX11Info::display(), window,
                            ATOM(_NET_WM_STATE), XA_ATOM, 32, PropModeReplace,
                            (unsigned char *) states.data(), states.size());
            update_root = true;
        }
    } break;
    case 2: {
        if (i == -1)
            skiptaskbar_wm_state(1, window);
        else
            skiptaskbar_wm_state(0, window);
    } break;
    default: break;
    }

    if (update_root) {
        XPropertyEvent p;
        p.send_event = True;
        p.display = QX11Info::display();
        p.type   = PropertyNotify;
        p.window = RootWindow(QX11Info::display(), 0);
        p.atom   = ATOM(_NET_CLIENT_LIST);
        p.state  = PropertyNewValue;
        p.time   = CurrentTime;
        XSendEvent(QX11Info::display(), p.window, False, PropertyChangeMask,
                   (XEvent *)&p);
    }
}

static bool need_geometry_modify(Window window)
{
    MCompAtoms *atom = MCompAtoms::instance();

    if (atom->hasState(window, ATOM(_NET_WM_STATE_FULLSCREEN)) ||
            (atom->statusBarOverlayed(window)))
        return false;

    return true;
}

static void fullscreen_wm_state(MCompositeManagerPrivate *priv,
                                int toggle, Window window)
{
    Atom fullscreen = ATOM(_NET_WM_STATE_FULLSCREEN);
    Display *dpy = QX11Info::display();
    MCompAtoms *atom = MCompAtoms::instance();
    QVector<Atom> states = atom->netWmStates(window);
    int i = states.indexOf(fullscreen);

    switch (toggle) {
    case 0: /* remove */ {
        if (i != -1) {
            do {
                states.remove(i);
                i = states.indexOf(fullscreen);
            } while (i != -1);
            XChangeProperty(dpy, window,
                            ATOM(_NET_WM_STATE), XA_ATOM, 32, PropModeReplace,
                            (unsigned char *) states.data(), states.size());
        }

        MCompositeWindow *win = MCompositeWindow::compositeWindow(window);
        if (win)
            win->setNetWmState(states.toList());
        if (win && !MDecoratorFrame::instance()->managedWindow()
            && priv->needDecoration(window, win)) {
            win->setDecorated(true);
            if (MCompAtoms::instance()->statusBarOverlayed(window))
                MDecoratorFrame::instance()->setManagedWindow(win, 28);
            else
                MDecoratorFrame::instance()->setManagedWindow(win);
            MDecoratorFrame::instance()->setOnlyStatusbar(false);
            MDecoratorFrame::instance()->raise();
        } else if (win && need_geometry_modify(window) &&
                   !availScreenRect.isEmpty()) {
            QRect r = availScreenRect;
            XMoveResizeWindow(dpy, window, r.x(), r.y(), r.width(), r.height());
        }
        priv->checkStacking(false);
    } break;
    case 1: /* add */ {
        if (i == -1) {
            states.append(fullscreen);
            XChangeProperty(dpy, window,
                            ATOM(_NET_WM_STATE), XA_ATOM, 32, PropModeReplace,
                            (unsigned char *) states.data(), states.size());
        }

        int xres = ScreenOfDisplay(dpy, DefaultScreen(dpy))->width;
        int yres = ScreenOfDisplay(dpy, DefaultScreen(dpy))->height;
        XMoveResizeWindow(dpy, window, 0, 0, xres, yres);
        MCompositeWindow *win = priv->windows.value(window, 0);
        if (win) {
            win->setRequestedGeometry(QRect(0, 0, xres, yres));
            win->setNetWmState(states.toList());
        }
        if (!priv->device_state->ongoingCall()
            && MDecoratorFrame::instance()->managedWindow() == window) {
            if (win) win->setDecorated(false);
            MDecoratorFrame::instance()->lower();
            MDecoratorFrame::instance()->setManagedWindow(0);
        }
        priv->checkStacking(false);
    } break;
    case 2: /* toggle */ {
        if (i == -1)
            fullscreen_wm_state(priv, 1, window);
        else
            fullscreen_wm_state(priv, 0, window);
    } break;
    default: break;
    }
}

#ifdef GLES2_VERSION
// This is a Harmattan hardware-specific feature to maniplute the graphics overlay
static void toggle_global_alpha_blend(unsigned int state, int manager = 0)
{
    FILE *out;
    char path[256];

    snprintf(path, 256, "/sys/devices/platform/omapdss/manager%d/alpha_blending_enabled", manager);

    out = fopen(path, "w");

    if (out) {
        fprintf(out, "%d", state);
        fclose(out);
    }
}

static void set_global_alpha(unsigned int plane, unsigned int level)
{
    FILE *out;
    char path[256];

    snprintf(path, 256, "/sys/devices/platform/omapdss/overlay%d/global_alpha", plane);

    out = fopen(path, "w");

    if (out) {
        fprintf(out, "%d", level);
        fclose(out);
    }
}
#endif

MCompositeManagerPrivate::MCompositeManagerPrivate(QObject *p)
    : QObject(p),
      glwidget(0),
      damage_cache(0),
      arranged(false),
      compositing(true)
{
    watch = new MCompositeScene(this);
    atom = MCompAtoms::instance();

    device_state = new MDeviceState();
    connect(device_state, SIGNAL(displayStateChange(bool)),
            this, SLOT(displayOff(bool)));
    connect(device_state, SIGNAL(callStateChange(bool)),
            this, SLOT(callOngoing(bool)));
}

MCompositeManagerPrivate::~MCompositeManagerPrivate()
{
    delete watch;
    delete atom;
    watch   = 0;
    atom = 0;
}

Window MCompositeManagerPrivate::parentWindow(Window child)
{
    uint children = 0;
    Window r, p, *kids = 0;

    XQueryTree(QX11Info::display(), child, &r, &p, &kids, &children);
    if (kids)
        XFree(kids);
    return p;
}

void MCompositeManagerPrivate::disableInput()
{
    watch->setupOverlay(xoverlay, QRect(0, 0, 0, 0), true);
    watch->setupOverlay(localwin, QRect(0, 0, 0, 0), true);
}

void MCompositeManagerPrivate::enableInput()
{
    watch->setupOverlay(xoverlay, QRect(0, 0, 0, 0));
    watch->setupOverlay(localwin, QRect(0, 0, 0, 0));

    emit inputEnabled();
}

void MCompositeManagerPrivate::prepare()
{
    watch->prepareRoot();
    Window w;
    QString wm_name = "MCompositor";

    w = XCreateSimpleWindow(QX11Info::display(),
                            RootWindow(QX11Info::display(), 0),
                            0, 0, 1, 1, 0,
                            None, None);
    XChangeProperty(QX11Info::display(), RootWindow(QX11Info::display(), 0),
                    ATOM(_NET_SUPPORTING_WM_CHECK), XA_WINDOW, 32,
                    PropModeReplace, (unsigned char *)&w, 1);
    XChangeProperty(QX11Info::display(), w, ATOM(_NET_SUPPORTING_WM_CHECK),
                    XA_WINDOW, 32, PropModeReplace, (unsigned char *)&w, 1);
    XChangeProperty(QX11Info::display(), w, ATOM(_NET_WM_NAME),
                    XInternAtom(QX11Info::display(), "UTF8_STRING", 0), 8,
                    PropModeReplace, (unsigned char *) wm_name.toUtf8().data(),
                    wm_name.size());


    Xutf8SetWMProperties(QX11Info::display(), w, "MCompositor",
                         "MCompositor", NULL, 0, NULL, NULL,
                         NULL);
    Atom a = XInternAtom(QX11Info::display(), "_NET_WM_CM_S0", False);
    XSetSelectionOwner(QX11Info::display(), a, w, 0);

    xoverlay = XCompositeGetOverlayWindow(QX11Info::display(),
                                          RootWindow(QX11Info::display(), 0));
    XReparentWindow(QX11Info::display(), localwin, xoverlay, 0, 0);
    enableInput();

    XDamageQueryExtension(QX11Info::display(), &damage_event, &damage_error);
}

bool MCompositeManagerPrivate::needDecoration(Window window,
                                              MCompositeWindow *cw)
{
    bool fs;
    if (!cw)
        fs = atom->hasState(window, ATOM(_NET_WM_STATE_FULLSCREEN));
    else
        fs = FULLSCREEN_WINDOW(cw);
    if (device_state->ongoingCall() && fs)
        // fullscreen window is decorated during call
        return true;
    if (fs)
        return false;
    MCompAtoms::Type t = atom->windowType(window);
    return (t != MCompAtoms::FRAMELESS
            && t != MCompAtoms::DESKTOP
            && t != MCompAtoms::NOTIFICATION
            && t != MCompAtoms::INPUT
            && t != MCompAtoms::DOCK
            && !transient_for(window));
}

void MCompositeManagerPrivate::damageEvent(XDamageNotifyEvent *e)
{
    if (device_state->displayOff())
        return;
    XserverRegion r = XFixesCreateRegion(QX11Info::display(), 0, 0);
    int num;
    XDamageSubtract(QX11Info::display(), e->damage, None, r);

    XRectangle *rects = 0;
    rects = XFixesFetchRegion(QX11Info::display(), r, &num);
    XFixesDestroyRegion(QX11Info::display(), r);

    if (damage_cache && damage_cache->window() == e->drawable) {
        if (rects) {
            damage_cache->updateWindowPixmap(rects, num);
            XFree(rects);
        }
        return;
    }
    MCompositeWindow *item = COMPOSITE_WINDOW(e->drawable);
    damage_cache = item;
    if (item)
        item->updateWindowPixmap(rects, num);

    if (rects)
        XFree(rects);
}

void MCompositeManagerPrivate::destroyEvent(XDestroyWindowEvent *e)
{
    MCompositeWindow *item = COMPOSITE_WINDOW(e->window);
    if (item) {
        scene()->removeItem(item);
        delete item;
        if (!removeWindow(e->window))
            qWarning("destroyEvent(): Error removing window");
        glwidget->update();
        if (damage_cache && damage_cache->window() == e->window)
            damage_cache = 0;
    } else {
        // We got a destroy event from a framed window (or a window that was
        // never mapped)
        FrameData fd = framed_windows.value(e->window);
        if (!fd.frame)
            return;
        framed_windows.remove(e->window);
        delete fd.frame;
    }
}

/*
  This doesn't really do anything for now. It will be used to get
  the NETWM hints in the future like window opacity and stuff
*/
void MCompositeManagerPrivate::propertyEvent(XPropertyEvent *e)
{
    Q_UNUSED(e);
    /*
      Atom opacityAtom = ATOM(_NET_WM_WINDOW_OPACITY);
      if(e->atom == opacityAtom)
        ;
    */
}

Window MCompositeManagerPrivate::getLastVisibleParent(MCompositeWindow *cw)
{
    Window last = 0, parent;
    while (cw && (parent = cw->transientFor())) {
       cw = COMPOSITE_WINDOW(parent);
       if (cw && cw->isMapped())
           last = parent;
       else // no-good parent, bail out
           break;
    }
    return last;
}

bool MCompositeManagerPrivate::isAppWindow(MCompositeWindow *cw)
{
    if (cw && !getLastVisibleParent(cw) &&
            (cw->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_NORMAL) ||
             cw->windowTypeAtom() == ATOM(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE))
            && !cw->isDecorator())
        return true;
    return false;
}

Window MCompositeManagerPrivate::getTopmostApp(int *index_in_stacking_list,
                                               Window ignore_window)
{
    Window topmost_app = 0;
    for (int i = stacking_list.size() - 1; i >= 0; --i) {
        Window w = stacking_list.at(i);
        if (w == ignore_window) continue;
        if (w == stack[DESKTOP_LAYER])
            /* desktop is above all applications */
            break;
        MCompositeWindow *cw = COMPOSITE_WINDOW(w);
        if (cw && cw->isMapped() && isAppWindow(cw)) {
            topmost_app = w;
            if (index_in_stacking_list)
                *index_in_stacking_list = i;
            break;
        }
    }
    return topmost_app;
}

// TODO: merge this with disableCompositing() so that in the end we have
// stacking order sensitive logic
bool MCompositeManagerPrivate::possiblyUnredirectTopmostWindow()
{
    bool ret = false;
    Window top = 0;
    int win_i = -1;
    MCompositeWindow *cw = 0;
    for (int i = stacking_list.size() - 1; i >= 0; --i) {
        Window w = stacking_list.at(i);
        if (!(cw = COMPOSITE_WINDOW(w)))
            continue;
        if (w == stack[DESKTOP_LAYER]) {
            top = w;
            win_i = i;
            break;
        }
        if (cw->isMapped() &&
            (cw->hasAlpha() || cw->needDecoration() || cw->isDecorator()))
            // this window prevents direct rendering
            return false;
        if (cw->isMapped() && isAppWindow(cw)) {
            top = w;
            win_i = i;
            break;
        }
    }
    if (top && cw && !MCompositeWindow::isTransitioning()) {
        // unredirect the chosen window and any docks above it
        ((MTexturePixmapItem *)cw)->enableDirectFbRendering();
        setWindowDebugProperties(top);
        // make sure unobscured event is sent when compositing again
        cw->setWindowObscured(true, true);
        for (int i = win_i + 1; i < stacking_list.size(); ++i) {
            Window w = stacking_list.at(i);
            if ((cw = COMPOSITE_WINDOW(w)) && cw->isMapped() &&
                cw->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_DOCK)) {
                ((MTexturePixmapItem *)cw)->enableDirectFbRendering();
                setWindowDebugProperties(w);
                cw->setWindowObscured(true, true);
            }
        }
        if (compositing) {
            scene()->views()[0]->setUpdatesEnabled(false);
            XUnmapWindow(QX11Info::display(), xoverlay);
            compositing = false;
        }
        ret = true;
    }
    return ret;
}

void MCompositeManagerPrivate::unmapEvent(XUnmapEvent *e)
{
    if (e->window == xoverlay)
        return;

    Window topmost_win = 0;
    for (int i = stacking_list.size() - 1; i >= 0; --i) {
        Window w = stacking_list.at(i);
        MCompositeWindow *cw = COMPOSITE_WINDOW(w);
        if (cw && cw->isMapped() && !cw->isDecorator() &&
            cw->windowTypeAtom() != ATOM(_NET_WM_WINDOW_TYPE_DOCK)) {
            topmost_win = w;
            break;
        }
    }

    MCompositeWindow *item = COMPOSITE_WINDOW(e->window);
    if (item) {
        item->setIsMapped(false);
        setWindowState(e->window, IconicState);
        if (item->isVisible()) {
            item->setVisible(false);
            item->clearTexture();
            glwidget->update();
        }
        if (MDecoratorFrame::instance()->managedWindow() == e->window)
            MDecoratorFrame::instance()->lower();
    } else {
        // We got an unmap event from a framed window
        FrameData fd = framed_windows.value(e->window);
        if (!fd.frame)
            return;
        // make sure we reparent first before deleting the window
        XGrabServer(QX11Info::display());
        XReparentWindow(QX11Info::display(), e->window,
                        RootWindow(QX11Info::display(), 0), 0, 0);
        setWindowState(e->window, IconicState);
        XRemoveFromSaveSet(QX11Info::display(), e->window);
        framed_windows.remove(e->window);
        XUngrabServer(QX11Info::display());
        delete fd.frame;
    }
    updateWinList();

    for (int i = 0; i < TOTAL_LAYERS; ++i)
        if (stack[i] == e->window) stack[i] = 0;

    if (topmost_win == e->window) {
#ifdef GLES2_VERSION
        toggle_global_alpha_blend(0);
        set_global_alpha(0, 255);
#endif

        // activate next mapped window
        // FIXME: this is flawed because the unmapped window is already
        // moved below home in stacking_list at this point. Needs to be fixed
        // so that the chained window case does not break.
        for (int i = stacking_list.indexOf(e->window) - 1; i >= 0; --i) {
             MCompositeWindow *cw = COMPOSITE_WINDOW(stacking_list.at(i));
             if (cw && cw->isMapped()) {
                 /* either lower window of the application (in chained window
                  * case), or duihome is activated */
                 activateWindow(stacking_list.at(i), CurrentTime, true);
                 return;
             }
        }
        // workaround for the flawedness...
        if (stack[DESKTOP_LAYER])
            activateWindow(stack[DESKTOP_LAYER], CurrentTime, true);
    }
}

void MCompositeManagerPrivate::configureEvent(XConfigureEvent *e)
{
    if (e->window == xoverlay)
        return;

    MCompositeWindow *item = COMPOSITE_WINDOW(e->window);
    if (item) {
        item->setPos(e->x, e->y);
        item->resize(e->width, e->height);
        if (e->override_redirect == True)
            return;

        Window above = e->above;
        if (above != None) {
            /* FIXME: this is flawed because it assumes the window is on top,
             * which will break when we have one decorated window
             * on top of this window */
            if (item->needDecoration() && MDecoratorFrame::instance()->decoratorItem()) {
                MDecoratorFrame::instance()->setManagedWindow(item);
                if (FULLSCREEN_WINDOW(item) &&
                    item->status() != MCompositeWindow::HUNG)
                    MDecoratorFrame::instance()->setOnlyStatusbar(true);
                else
                    MDecoratorFrame::instance()->setOnlyStatusbar(false);
                MDecoratorFrame::instance()->decoratorItem()->setVisible(true);
                MDecoratorFrame::instance()->raise();
                MDecoratorFrame::instance()->decoratorItem()->setZValue(item->zValue() + 1);
                item->update();
            }
        } else {
            // FIXME: seems that this branch is never executed?
            if (e->window == MDecoratorFrame::instance()->managedWindow())
                MDecoratorFrame::instance()->lower();
            item->setIconified(true);
            // ensure ZValue is set only after the animation is done
            item->requestZValue(0);

            MCompositeWindow *desktop = COMPOSITE_WINDOW(stack[DESKTOP_LAYER]);
            if (desktop)
#if (QT_VERSION >= 0x040600)
                item->stackBefore(desktop);
#endif
        }
    }
}

void MCompositeManagerPrivate::configureRequestEvent(XConfigureRequestEvent *e)
{
    if (e->parent != RootWindow(QX11Info::display(), 0))
        return;

    MCompAtoms::Type wtype = atom->windowType(e->window);
    bool isInput = (wtype == MCompAtoms::INPUT);
    // sandbox these windows. we own them
    if (atom->isDecorator(e->window))
        return;

    /*qDebug() << __func__ << "CONFIGURE REQUEST FOR:" << e->window
        << e->x << e->y << e->width << e->height << "above/mode:"
        << e->above << e->detail;*/

    // dock changed
    if (hasDock && (wtype == MCompAtoms::DOCK)) {
        dock_region = QRegion(e->x, e->y, e->width, e->height);
        QRect r = (QRegion(QApplication::desktop()->screenGeometry()) - dock_region).boundingRect();
        if (stack[DESKTOP_LAYER] && need_geometry_modify(stack[DESKTOP_LAYER]))
            XMoveResizeWindow(QX11Info::display(), stack[DESKTOP_LAYER], r.x(), r.y(), r.width(), r.height());

        if (stack[APPLICATION_LAYER]) {
            if (need_geometry_modify(stack[APPLICATION_LAYER]))
                XMoveResizeWindow(QX11Info::display(), stack[APPLICATION_LAYER],
                                  r.x(), r.y(), r.width(), r.height());
            positionWindow(stack[APPLICATION_LAYER], STACK_TOP);
        }

        if (stack[INPUT_LAYER] && need_geometry_modify(stack[INPUT_LAYER]))
            XMoveResizeWindow(QX11Info::display(), stack[INPUT_LAYER], r.x(), r.y(), r.width(), r.height());
    }

    XWindowChanges wc;
    wc.border_width = e->border_width;
    wc.x = e->x;
    wc.y = e->y;
    wc.width = e->width;
    wc.height = e->height;
    wc.sibling =  e->above;
    wc.stack_mode = e->detail;

    if (e->value_mask & (CWX | CWY | CWWidth | CWHeight)) {
        MCompositeWindow *i = COMPOSITE_WINDOW(e->window);
        if (i) {
            QRect r = i->requestedGeometry();
            if (e->value_mask & CWX)
                r.setX(e->x);
            if (e->value_mask & CWY)
                r.setY(e->y);
            if (e->value_mask & CWWidth)
                r.setWidth(e->width);
            if (e->value_mask & CWHeight)
                r.setHeight(e->height);
            i->setRequestedGeometry(r);
        }
    }

    if ((e->detail == Above) && (e->above == None) && !isInput) {
        XWindowAttributes a;
        if (!XGetWindowAttributes(QX11Info::display(), e->window, &a)) {
            qWarning("XGetWindowAttributes for 0x%lx failed", e->window);
            return;
        }
        if ((a.map_state == IsViewable) && (wtype != MCompAtoms::DOCK)) {
            setWindowState(e->window, NormalState);
            setExposeDesktop(false);
            stack[APPLICATION_LAYER] = e->window;

            // selective compositing support
            MCompositeWindow *i = COMPOSITE_WINDOW(e->window);
            if (i) {
                // since we call disable compositing immediately
                // we don't see the animated transition
                if (!i->hasAlpha() && !i->needDecoration()) {
                    i->setIconified(false);        
                    disableCompositing(FORCED);
                } else if (MDecoratorFrame::instance()->managedWindow() == e->window)
                    enableCompositing();
                i->restore();
            }
        }
    } else if ((e->detail == Below) && (e->above == None) && !isInput)
        setWindowState(e->window, IconicState);

    /* modify stacking_list if stacking order should be changed */
    int win_i = stacking_list.indexOf(e->window);
    if (win_i >= 0 && e->detail == Above) {
        if (e->above != None) {
            int above_i = stacking_list.indexOf(e->above);
            if (above_i >= 0) {
                if (above_i > win_i)
                    stacking_list.move(win_i, above_i);
                else
                    stacking_list.move(win_i, above_i + 1);
                checkStacking(false);
            }
        } else {
            Window parent = transient_for(e->window);
            if (parent)
                positionWindow(parent, STACK_TOP);
            else
                positionWindow(e->window, STACK_TOP);
        }
    } else if (win_i >= 0 && e->detail == Below) {
        if (e->above != None) {
            int above_i = stacking_list.indexOf(e->above);
            if (above_i >= 0) {
                if (above_i > win_i)
                    stacking_list.move(win_i, above_i - 1);
                else
                    stacking_list.move(win_i, above_i);
                checkStacking(false);
            }
        } else {
            Window parent = transient_for(e->window);
            if (parent)
                positionWindow(parent, STACK_BOTTOM);
            else
                positionWindow(e->window, STACK_BOTTOM);
        }
    }

    /* stacking is done in checkStacking(), based on stacking_list */
    unsigned int value_mask = e->value_mask & ~(CWSibling | CWStackMode);
    if (value_mask)
        XConfigureWindow(QX11Info::display(), e->window, value_mask, &wc);
}

void MCompositeManagerPrivate::mapRequestEvent(XMapRequestEvent *e)
{
    XWindowAttributes a;
    Display *dpy  = QX11Info::display();
    bool hasAlpha = false;

    if (!XGetWindowAttributes(QX11Info::display(), e->window, &a))
        return;
    if (!hasDock) {
        hasDock = (atom->windowType(e->window) == MCompAtoms::DOCK);
        if (hasDock)
            dock_region = QRegion(a.x, a.y, a.width, a.height);
    }
    int xres = ScreenOfDisplay(dpy, DefaultScreen(dpy))->width;
    int yres = ScreenOfDisplay(dpy, DefaultScreen(dpy))->height;

    if ((atom->windowType(e->window) == MCompAtoms::FRAMELESS
            || atom->windowType(e->window) == MCompAtoms::DESKTOP
            || atom->windowType(e->window) == MCompAtoms::INPUT)
            && (atom->windowType(e->window) != MCompAtoms::DOCK)) {
        if (hasDock) {
            QRect r = (QRegion(QApplication::desktop()->screenGeometry()) - dock_region).boundingRect();
            if (availScreenRect != r)
                availScreenRect = r;
            if (need_geometry_modify(e->window))
                XMoveResizeWindow(dpy, e->window, r.x(), r.y(), r.width(), r.height());
        } else if ((a.width != xres) && (a.height != yres)) {
            XResizeWindow(dpy, e->window, xres, yres);
        }
    }

    if (atom->isDecorator(e->window)) {
        enableCompositing();
        MDecoratorFrame::instance()->setDecoratorWindow(e->window);
        return;
    }

    XRenderPictFormat *format = XRenderFindVisualFormat(QX11Info::display(),
                                a.visual);
    if (format && (format->type == PictTypeDirect && format->direct.alphaMask)) {
        enableCompositing();
        hasAlpha = true;
    }

    if (atom->hasState(e->window, ATOM(_NET_WM_STATE_FULLSCREEN)))
        fullscreen_wm_state(this, 1, e->window);

    if (needDecoration(e->window)) {
        XSelectInput(dpy, e->window,
                     StructureNotifyMask | ColormapChangeMask |
                     PropertyChangeMask);
        XAddToSaveSet(QX11Info::display(), e->window);

        if (MDecoratorFrame::instance()->decoratorItem()) {
            enableCompositing();
            XMapWindow(QX11Info::display(), e->window);
            // initially visualize decorator item so selective compositing
            // checks won't disable compositing
            MDecoratorFrame::instance()->decoratorItem()->setVisible(true);
        } else {
            MSimpleWindowFrame *frame = 0;
            FrameData f = framed_windows.value(e->window);
            frame = f.frame;
            if (!frame) {
                frame = new MSimpleWindowFrame(e->window);
                Window trans;
                XGetTransientForHint(QX11Info::display(), e->window, &trans);
                if (trans)
                    frame->setDialogDecoration(true);

                // TEST: a framed translucent window
                if (hasAlpha)
                    frame->setAttribute(Qt::WA_TranslucentBackground);
                QSize s = frame->suggestedWindowSize();
                XResizeWindow(QX11Info::display(), e->window, s.width(), s.height());

                XReparentWindow(QX11Info::display(), frame->winId(),
                                RootWindow(QX11Info::display(), 0), 0, 0);

                // associate e->window with frame and its parent
                FrameData fd;
                fd.frame = frame;
                fd.mapped = true;
                fd.parentWindow = frame->winId();
                framed_windows[e->window] = fd;

                if (trans) {
                    FrameData f = framed_windows.value(trans);
                    if (f.frame) {
                        XSetTransientForHint(QX11Info::display(), frame->winId(),
                                             f.frame->winId());
                    }
                }
            }

            XReparentWindow(QX11Info::display(), e->window,
                            frame->windowArea(), 0, 0);
            setWindowState(e->window, NormalState);
            XMapWindow(QX11Info::display(), e->window);
            frame->show();

            XSync(QX11Info::display(), False);
        }
    } else {
        setWindowState(e->window, NormalState);
        XMapWindow(QX11Info::display(), e->window);
    }
}

/* recursion is needed to handle transients that are transient for other
 * transients */
static void raise_transients(MCompositeManagerPrivate *priv,
                             Window w, int last_i)
{
    Window first_moved = 0;
    for (int i = 0; i < last_i;) {
        Window iw = priv->stacking_list.at(i);
        if (iw == first_moved)
            /* each window is only considered once */
            break;
        MCompositeWindow *cw = priv->windows.value(iw, 0);
        if (cw && cw->transientFor() == w) {
            priv->stacking_list.move(i, last_i);
            if (!first_moved) first_moved = iw;
            raise_transients(priv, iw, last_i);
        } else ++i;
    }
}

#if 0 // disabled due to bugs in applications (e.g. widgetsgallery)
static Bool
timestamp_predicate(Display *display,
                    XEvent  *xevent,
                    XPointer arg)
{
    Q_UNUSED(arg);
    if (xevent->type == PropertyNotify &&
            xevent->xproperty.window == RootWindow(display, 0) &&
            xevent->xproperty.atom == ATOM(_NET_CLIENT_LIST))
        return True;

    return False;
}

static Time get_server_time()
{
    XEvent xevent;
    long data = 0;

    /* zero-length append to get timestamp in the PropertyNotify */
    XChangeProperty(QX11Info::display(), RootWindow(QX11Info::display(), 0),
                    ATOM(_NET_CLIENT_LIST),
                    XA_WINDOW, 32, PropModeAppend,
                    (unsigned char *)&data, 0);

    XIfEvent(QX11Info::display(), &xevent, timestamp_predicate, NULL);

    return xevent.xproperty.time;
}
#endif

/* NOTE: this assumes that stacking is correct */
void MCompositeManagerPrivate::checkInputFocus(Time timestamp)
{
    Window w = None;

    /* find topmost window wanting the input focus */
    for (int i = stacking_list.size() - 1; i >= 0; --i) {
        Window iw = stacking_list.at(i);
        MCompositeWindow *cw = COMPOSITE_WINDOW(iw);
        if (!cw || !cw->isMapped() || !cw->wantsFocus() || cw->isDecorator())
            continue;
        /* workaround for NB#161629 */
        if (is_desktop_dock(iw))
            continue;
        if (isSelfManagedFocus(iw)) {
            w = iw;
            break;
        }
        /* FIXME: do this based on geometry to cope with TYPE_NORMAL dialogs */
        /* don't focus a window that is obscured (assumes that NORMAL
         * and DESKTOP cover the whole screen) */
        if (cw->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_NORMAL) ||
                cw->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_DESKTOP))
            break;
    }

    if (prev_focus == w)
        return;
    prev_focus = w;

#if 0 // disabled due to bugs in applications (e.g. widgetsgallery)
    MCompositeWindow *cw = windows.value(w);
    if (cw && cw->supportedProtocols().indexOf(ATOM(WM_TAKE_FOCUS)) != -1) {
        /* CurrentTime for WM_TAKE_FOCUS brings trouble
         * (a lesson learned from Fremantle) */
        if (timestamp == CurrentTime)
            timestamp = get_server_time();

        XEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.xclient.type = ClientMessage;
        ev.xclient.window = w;
        ev.xclient.message_type = ATOM(WM_PROTOCOLS);
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = ATOM(WM_TAKE_FOCUS);
        ev.xclient.data.l[1] = timestamp;

        XSendEvent(QX11Info::display(), w, False, NoEventMask, &ev);
    } else
#endif
        XSetInputFocus(QX11Info::display(), w, RevertToPointerRoot, timestamp);
}

/* Go through stacking_list and verify that it is in order.
 * If it isn't, reorder it and call XRestackWindows.
 * NOTE: stacking_list needs to be reversed before giving it to
 * XRestackWindows.*/
void MCompositeManagerPrivate::checkStacking(bool force_visibility_check,
                                               Time timestamp)
{
    static QList<Window> prev_list;
    Window active_app = 0, duihome = stack[DESKTOP_LAYER], first_moved;
    int last_i = stacking_list.size() - 1;
    bool desktop_up = false, fs_app = false;
    int app_i = -1;
    MDecoratorFrame *deco = MDecoratorFrame::instance();
    MCompositeWindow *aw = 0;

    active_app = getTopmostApp(&app_i);
    if (!active_app || app_i < 0)
        desktop_up = true;
    else {
        aw = COMPOSITE_WINDOW(active_app);
        fs_app = FULLSCREEN_WINDOW(aw);
    }

    /* raise active app with its transients, or duihome if
     * there is no active application */
    if (!desktop_up && active_app && app_i >= 0 && aw) {
        if (duihome) {
            /* stack duihome right below the application */
	    stacking_list.move(stacking_list.indexOf(duihome), last_i);
	    app_i = stacking_list.indexOf(active_app);
	}
	/* raise application windows belonging to the same group */
	XID group;
	if ((group = aw->windowGroup())) {
	    for (int i = 0; i < app_i; ) {
	         MCompositeWindow *cw = COMPOSITE_WINDOW(stacking_list.at(i));
		 if (isAppWindow(cw) && cw->windowGroup() == group) {
	             stacking_list.move(i, last_i);
	             /* active_app was moved, update the index */
	             app_i = stacking_list.indexOf(active_app);
		     /* TODO: transients */
		 } else ++i;
	    }
	}
	stacking_list.move(app_i, last_i);
	/* raise decorator above the application */
	if (deco->decoratorItem() && deco->managedWindow() == active_app &&
            (!fs_app || aw->status() == MCompositeWindow::HUNG
             || device_state->ongoingCall())) {
            Window deco_w = deco->decoratorItem()->window();
	    int deco_i = stacking_list.indexOf(deco_w);
	    if (deco_i >= 0) {
	        stacking_list.move(deco_i, last_i);
                if (!compositing)
                    // decor requires compositing
                    enableCompositing(true);
	        MCompositeWindow *cw = COMPOSITE_WINDOW(deco_w);
                cw->updateWindowPixmap();
                cw->setVisible(true);
            }
	}
	/* raise transients recursively */
	raise_transients(this, active_app, last_i);
    } else if (duihome) {
        //qDebug() << "raising home window" << duihome;
        stacking_list.move(stacking_list.indexOf(duihome), last_i);
    }

    /* raise docks if either the desktop is up or the application is
     * non-fullscreen */
    if (desktop_up || !active_app || app_i < 0 || !aw || !fs_app) {
        first_moved = 0;
        for (int i = 0; i < last_i;) {
            Window w = stacking_list.at(i);
            if (w == first_moved) break;
            MCompositeWindow *cw = COMPOSITE_WINDOW(w);
            if (cw && cw->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_DOCK)) {
                stacking_list.move(i, last_i);
                if (!first_moved) first_moved = w;
                /* possibly reposition the decorator so that it does not
                 * go below the dock window */
                if (active_app && deco->decoratorItem() &&
                    deco->managedWindow() == active_app) {
                    int h = (int)cw->boundingRect().height();
                    XMoveWindow(QX11Info::display(),
                                deco->decoratorItem()->window(), 0, h);
                    deco->updateManagedWindowGeometry(h);
                }
	        /* raise transients recursively */
	        raise_transients(this, w, last_i);
            } else ++i;
        }
    } else if (active_app && aw && deco->decoratorItem() &&
               deco->managedWindow() == active_app &&
               (fs_app || aw->status() == MCompositeWindow::HUNG)) {
        // no dock => decorator starts from (0,0)
        XMoveWindow(QX11Info::display(), deco->decoratorItem()->window(), 0, 0);
    }
    /* raise all system-modal dialogs */
    first_moved = 0;
    for (int i = 0; i < last_i;) {
        /* TODO: transients */
        Window w = stacking_list.at(i);
        if (w == first_moved) break;
        MCompositeWindow *cw = COMPOSITE_WINDOW(w);
        if (cw && !cw->transientFor()
                && cw->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_DIALOG)) {
            stacking_list.move(i, last_i);
            if (!first_moved) first_moved = w;
        } else ++i;
    }
    /* raise all keep-above flagged and input methods, at the same time
     * preserving their mutual stacking order */
    first_moved = 0;
    for (int i = 0; i < last_i;) {
        /* TODO: transients */
        Window w = stacking_list.at(i);
        if (w == first_moved) break;
        MCompositeWindow *cw = COMPOSITE_WINDOW(w);
        if (cw && (cw->isDecorator() || getLastVisibleParent(cw))) {
            // skip decorators and transients (raised after the parent)
            ++i;
            continue;
        }
        if ((cw && cw->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_INPUT)) ||
            atom->hasState(w, ATOM(_NET_WM_STATE_ABOVE))) {
            stacking_list.move(i, last_i);
	    raise_transients(this, w, last_i);
            if (!first_moved) first_moved = w;
        } else ++i;
    }
    /* raise all non-transient notifications (transient ones were already
     * handled above) */
    first_moved = 0;
    for (int i = 0; i < last_i;) {
        Window w = stacking_list.at(i);
        if (w == first_moved) break;
        MCompositeWindow *cw = COMPOSITE_WINDOW(w);
        if (cw && !cw->transientFor() &&
                cw->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_NOTIFICATION)) {
            stacking_list.move(i, last_i);
            if (!first_moved) first_moved = w;
        } else ++i;
    }

    bool order_changed = prev_list != stacking_list;
    if (order_changed) {
        /* fix Z-values */
        for (int i = 0; i <= last_i; ++i) {
            MCompositeWindow *witem = COMPOSITE_WINDOW(stacking_list.at(i));
            if (witem)
                witem->requestZValue(i);
        }

        QList<Window> reverse;
        for (int i = last_i; i >= 0; --i)
            reverse.append(stacking_list.at(i));
        //qDebug() << __func__ << "stack:" << reverse.toVector();

        XRestackWindows(QX11Info::display(), reverse.toVector().data(),
                        reverse.size());
        XChangeProperty(QX11Info::display(),
                        RootWindow(QX11Info::display(), 0),
                        ATOM(_NET_CLIENT_LIST_STACKING),
                        XA_WINDOW, 32, PropModeReplace,
                        (unsigned char *)stacking_list.toVector().data(),
                        stacking_list.size());
        prev_list = QList<Window>(stacking_list);

        checkInputFocus(timestamp);
        XSync(QX11Info::display(), False);
    }
    if (order_changed || force_visibility_check) {
        /* Send synthetic visibility events for composited windows */
        int home_i = stacking_list.indexOf(duihome);
        for (int i = 0; i <= last_i; ++i) {
            MCompositeWindow *cw = COMPOSITE_WINDOW(stacking_list.at(i));
            if (!cw || cw->isDirectRendered() || !cw->isMapped()) continue;
            if (duihome && i > home_i) {
                cw->setWindowObscured(false);
                cw->setVisible(true);
                setWindowState(cw->window(), NormalState);
            } else if (i == home_i && desktop_up) {
                cw->setWindowObscured(false);
                cw->setVisible(true);
                setWindowState(cw->window(), NormalState);
            } else if (!duihome) {
                cw->setWindowObscured(false);
                cw->setVisible(true);
                setWindowState(cw->window(), NormalState);
            } else {
                cw->setWindowObscured(true);
                if (cw->window() != duihome)
                    cw->setVisible(false);
            }
        }
    }
}

void MCompositeManagerPrivate::mapEvent(XMapEvent *e)
{
    Window win = e->window;

    if (win == xoverlay) {
        enableRedirection();
        return;
    }

    FrameData fd = framed_windows.value(win);
    if (fd.frame) {
        XWindowAttributes a;
        if (!XGetWindowAttributes(QX11Info::display(), e->window, &a))
            return;
        XConfigureEvent c;
        c.type = ConfigureNotify;
        c.send_event = True;
        c.event = e->window;
        c.window = e->window;
        c.x = 0;
        c.y = 0;
        c.width = a.width;
        c.height = a.height;
        c.border_width = 0;
        c.above = stack[DESKTOP_LAYER];
        c.override_redirect = 0;
        XSendEvent(QX11Info::display(), c.event, true, StructureNotifyMask, (XEvent *)&c);
    }

    // simple stacking model fulfills the current DUI concept,
    if (atom->windowType(e->window) == MCompAtoms::DESKTOP) {
        stack[DESKTOP_LAYER] = e->window; // below topmost
    } else if (atom->windowType(e->window) == MCompAtoms::INPUT) {
        stack[INPUT_LAYER] = e->window; // topmost
    } else if (atom->windowType(e->window) == MCompAtoms::DOCK) {
        stack[DOCK_LAYER] = e->window; // topmost
    } else {
        if ((atom->windowType(e->window)    == MCompAtoms::FRAMELESS ||
                (atom->windowType(e->window)    == MCompAtoms::NORMAL))
                && !atom->isDecorator(e->window)
                && (parentWindow(win) == RootWindow(QX11Info::display(), 0))
                && (e->event == QX11Info::appRootWindow())) {
            hideLaunchIndicator();
            stack[APPLICATION_LAYER] = e->window; // between

            setExposeDesktop(false);
        }
    }

#ifdef GLES2_VERSION
    // TODO: this should probably be done on the focus level. Rewrite this
    // once new stacking code from Kimmo is done
    int g_alpha = atom->globalAlphaFromWindow(win);
    if (g_alpha == 255)
        toggle_global_alpha_blend(0);
    else if (g_alpha < 255)
        toggle_global_alpha_blend(1);
    set_global_alpha(0, g_alpha);
#endif

    MCompositeWindow *item = COMPOSITE_WINDOW(win);
    if (item) item->setIsMapped(true);
    // Compositing is assumed to be enabled at this point if a window
    // has alpha channels
    if (!compositing && (item && item->hasAlpha())) {
        qWarning("mapEvent(): compositing not enabled!");
        return;
    }
    if (item) {
        if (atom->windowType(e->window) == MCompAtoms::NORMAL)
            item->setWindowTypeAtom(ATOM(_NET_WM_WINDOW_TYPE_NORMAL));
        else
            item->setWindowTypeAtom(atom->getType(win));
        item->saveBackingStore(true);
        if (!device_state->displayOff() && !item->hasAlpha()
            && !item->needDecoration()) {
            item->setVisible(true);
            item->updateWindowPixmap();
	    disableCompositing();
        } else if (!device_state->displayOff()) {
            ((MTexturePixmapItem *)item)->enableRedirectedRendering();
            item->delayShow(100);
        }
        /* do this after bindWindow() so that the window is in stacking_list */
        activateWindow(win, CurrentTime, false);
        return;
    }

    if (win == localwin || win == parentWindow(localwin))
        return;

    XGrabButton(QX11Info::display(), AnyButton, AnyModifier, win, True,
                ButtonPressMask | ButtonReleaseMask | ButtonMotionMask,
                GrabModeSync, GrabModeSync, None, None);

    // only composite top-level windows
    if ((parentWindow(win) == RootWindow(QX11Info::display(), 0))
            && (e->event == QX11Info::appRootWindow())) {
        item = bindWindow(win);
        Window transient_for = item->transientFor();
        if (transient_for)
            item->setWindowType(MCompositeWindow::Transient);
        else
            item->setWindowType(MCompositeWindow::Normal);
        if (!item->hasAlpha())
	    disableCompositing(FORCED);
        else
            item->delayShow(500);

        // the current decorated window got mapped
        if (e->window == MDecoratorFrame::instance()->managedWindow() &&
                MDecoratorFrame::instance()->decoratorItem()) {
            connect(item, SIGNAL(visualized(bool)),
                    MDecoratorFrame::instance(),
                    SLOT(visualizeDecorator(bool)));
            MDecoratorFrame::instance()->decoratorItem()->setVisible(true);
            MDecoratorFrame::instance()->raise();
            MDecoratorFrame::instance()->decoratorItem()->setZValue(item->zValue() + 1);
            stack[APPLICATION_LAYER] = e->window;
        }
        setWindowDebugProperties(win);
    }
    /* do this after bindWindow() so that the window is in stacking_list */
    if (stack[DESKTOP_LAYER] != win || !getTopmostApp(0, win))
        activateWindow(win, CurrentTime, false);
    else
        // desktop is stacked below the active application
        positionWindow(win, STACK_BOTTOM);
}

static bool should_be_pinged(MCompositeWindow *cw)
{
    if (cw && cw->supportedProtocols().indexOf(ATOM(_NET_WM_PING)) != -1
        && cw->windowTypeAtom() != ATOM(_NET_WM_WINDOW_TYPE_DOCK)
        && cw->iconifyState() == MCompositeWindow::NoIconifyState
        && !cw->isDecorator()
        && !is_desktop_window(cw->window()))
        return true;
    return false;
}

void MCompositeManagerPrivate::rootMessageEvent(XClientMessageEvent *event)
{
    MCompositeWindow *i = COMPOSITE_WINDOW(event->window);
    FrameData fd = framed_windows.value(event->window);

    if (event->message_type == ATOM(_NET_ACTIVE_WINDOW)) {
        // Visibility notification to desktop window. Ensure this is sent
        // before transitions are started
        if (event->window != stack[DESKTOP_LAYER])
            setExposeDesktop(false);

        Window raise = event->window;
        MCompositeWindow *d_item = COMPOSITE_WINDOW(stack[DESKTOP_LAYER]);
        bool needComp = false;
        if (d_item && d_item->isDirectRendered()) {
            needComp = true;
            // _NET_ACTIVE_WINDOW comes from duihome when tapping on thumbnail
            // so display will be on soon if it's not already
            enableCompositing(true, true);
        }
        if (i) {
            i->setZValue(windows.size() + 1);
            QRectF iconGeometry = atom->iconGeometry(raise);
            i->setPos(iconGeometry.topLeft());
            i->restore(iconGeometry, needComp);
            if (!device_state->displayOff() && should_be_pinged(i))
                i->startPing();
        }
        if (fd.frame)
            setWindowState(fd.frame->managedWindow(), NormalState);
        else
            setWindowState(event->window, NormalState);
        if (event->window == stack[DESKTOP_LAYER])
            // raising home does not have a transition
            activateWindow(event->window, CurrentTime, true);
        else
            activateWindow(event->window, CurrentTime, false);
    } else if (event->message_type == ATOM(_NET_CLOSE_WINDOW)) {
        Window close_window = event->window;

        // send WM_DELETE_WINDOW message to actual window that needs to close
        XEvent ev;
        memset(&ev, 0, sizeof(ev));

        ev.xclient.type = ClientMessage;
        ev.xclient.window = close_window;
        ev.xclient.message_type = ATOM(WM_PROTOCOLS);
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = ATOM(WM_DELETE_WINDOW);
        ev.xclient.data.l[1] = CurrentTime;

        XSendEvent(QX11Info::display(), close_window, False, NoEventMask, &ev);
        setExposeDesktop(true);
        XSync(QX11Info::display(), False);

        MCompositeWindow *check_hung = COMPOSITE_WINDOW(close_window);
        if (check_hung) {
            if (check_hung->status() == MCompositeWindow::HUNG) {
                // destroy at the server level
                XKillClient(QX11Info::display(), close_window);
                delete check_hung;
                MDecoratorFrame::instance()->lower();
                removeWindow(close_window);
                return;
            }
        }
    } else if (event->message_type == ATOM(WM_PROTOCOLS)) {
        if (event->data.l[0] == (long) ATOM(_NET_WM_PING)) {
            MCompositeWindow *ping_source = COMPOSITE_WINDOW(event->data.l[2]);
            if (ping_source) {
                ping_source->receivedPing(event->data.l[1]);
                Window managed = MDecoratorFrame::instance()->managedWindow();
                if (ping_source->window() == managed && !ping_source->needDecoration()) {
                    MDecoratorFrame::instance()->lower();
                    MDecoratorFrame::instance()->setManagedWindow(0);
                    if(!ping_source->hasAlpha()) 
                        disableCompositing(FORCED);
                }
            }
        }
    } else if (event->message_type == ATOM(_NET_WM_STATE)) {
        if (event->data.l[1] == (long)  ATOM(_NET_WM_STATE_SKIP_TASKBAR)) {
            skiptaskbar_wm_state(event->data.l[0], event->window);
            if (i) {
                QVector<Atom> states = atom->netWmStates(event->window);
                i->setNetWmState(states.toList());
            }
        } else if (event->data.l[1] == (long) ATOM(_NET_WM_STATE_FULLSCREEN))
            fullscreen_wm_state(this, event->data.l[0], event->window);
    }
}

void MCompositeManagerPrivate::clientMessageEvent(XClientMessageEvent *event)
{
    // Handle iconify requests
    if (event->message_type == ATOM(WM_CHANGE_STATE))
        if (event->data.l[0] == IconicState && event->format == 32) {

            MCompositeWindow *i = COMPOSITE_WINDOW(event->window);
            MCompositeWindow *d_item = COMPOSITE_WINDOW(stack[DESKTOP_LAYER]);
            if (d_item && i) {
                d_item->setZValue(i->zValue() - 1);

                Window lower = event->window;
                setExposeDesktop(false);

                bool needComp = false;
                if (i->isDirectRendered() || d_item->isDirectRendered()) {
                    d_item->setVisible(true);
                    enableCompositing(FORCED);
                    needComp = true;
                }
                // Delayed transition is only available on platforms
                // that have selective compositing. This is triggered
                // when windows are rendered off-screen
                i->iconify(atom->iconGeometry(lower), needComp);
                if (i->needDecoration())
                    i->startTransition();
                i->stopPing();
            }
            return;
        }

    // Handle root messages
    rootMessageEvent(event);
}

void MCompositeManagerPrivate::iconifyOnLower(MCompositeWindow *window)
{
    if (window->iconifyState() != MCompositeWindow::TransitionIconifyState)
        return;

    // TODO: (work for more)
    // Handle minimize request coming from a managed window itself,
    // if there are any
    FrameData fd = framed_windows.value(window->window());
    if (fd.frame) {
        setWindowState(fd.frame->managedWindow(), IconicState);
        MCompositeWindow *i = COMPOSITE_WINDOW(fd.frame->winId());
        if (i)
            i->iconify();
    }

    if (stack[DESKTOP_LAYER]) {
        enableCompositing();
        positionWindow(stack[DESKTOP_LAYER], STACK_TOP);
        if (compositing)
            possiblyUnredirectTopmostWindow();
    }
    setWindowState(window->window(), IconicState);
}

void MCompositeManagerPrivate::raiseOnRestore(MCompositeWindow *window)
{
    stack[APPLICATION_LAYER] = window->window();
    positionWindow(window->window(), STACK_TOP);

    /* the animation is finished, compositing needs to be reconsidered */
    possiblyUnredirectTopmostWindow();
}

void MCompositeManagerPrivate::onDesktopActivated(MCompositeWindow *window)
{
    Q_UNUSED(window);
    /* desktop is on top, direct render it */
    possiblyUnredirectTopmostWindow();
}

void MCompositeManagerPrivate::setExposeDesktop(bool exposed)
{
    if (stack[DESKTOP_LAYER]) {
        XVisibilityEvent desk_notify;
        desk_notify.type       = VisibilityNotify;
        desk_notify.send_event = True;
        desk_notify.window     = stack[DESKTOP_LAYER];
        desk_notify.state      = exposed ? VisibilityUnobscured :
                                 VisibilityFullyObscured;
        XSendEvent(QX11Info::display(), stack[DESKTOP_LAYER], true,
                   VisibilityChangeMask, (XEvent *)&desk_notify);
    }
    if (stack[DOCK_LAYER]) {
        XVisibilityEvent desk_notify;
        desk_notify.type       = VisibilityNotify;
        desk_notify.send_event = True;
        desk_notify.window     = stack[DOCK_LAYER];
        desk_notify.state      = exposed ? VisibilityUnobscured :
                                 VisibilityFullyObscured;
        XSendEvent(QX11Info::display(), stack[DESKTOP_LAYER], true,
                   VisibilityChangeMask, (XEvent *)&desk_notify);
    }
}

// Visibility notification to desktop window. Ensure this is called once
// transitions are done
void MCompositeManagerPrivate::exposeDesktop()
{
    setExposeDesktop(true);
}

bool MCompositeManagerPrivate::isSelfManagedFocus(Window w)
{
    /* FIXME: store these to the object */
    XWindowAttributes attr;
    if (!XGetWindowAttributes(QX11Info::display(), w, &attr))
        return false;
    if (attr.override_redirect || atom->windowType(w) == MCompAtoms::INPUT)
        return false;
    return true;
}

void MCompositeManagerPrivate::activateWindow(Window w, Time timestamp,
        bool disableCompositing)
{
    if (MDecoratorFrame::instance()->managedWindow() == w)
        MDecoratorFrame::instance()->activate();

    if ((atom->windowType(w) != MCompAtoms::DESKTOP) &&
            (atom->windowType(w) != MCompAtoms::DOCK)) {
        stack[APPLICATION_LAYER] = w;
        setExposeDesktop(false);
        // if this is a transient window, raise the "parent" instead
        MCompositeWindow *cw = COMPOSITE_WINDOW(w);
        // possibly set decorator
        if (cw && needDecoration(w, cw)) {
            cw->setDecorated(true);
            if (FULLSCREEN_WINDOW(cw)) {
                // fullscreen window has decorator above it during ongoing call
                MDecoratorFrame::instance()->setManagedWindow(cw, 0, true);
                MDecoratorFrame::instance()->setOnlyStatusbar(true);
            } else if (atom->statusBarOverlayed(w)) {
                MDecoratorFrame::instance()->setManagedWindow(cw, 28);
                MDecoratorFrame::instance()->setOnlyStatusbar(false);
            } else {
                MDecoratorFrame::instance()->setManagedWindow(cw);
                MDecoratorFrame::instance()->setOnlyStatusbar(false);
            }
        }
        Window last = getLastVisibleParent(cw);
        if (last)
            positionWindow(last, STACK_TOP);
        else
            positionWindow(w, STACK_TOP);
    } else if (w == stack[DESKTOP_LAYER]) {
        setExposeDesktop(true);
        positionWindow(w, STACK_TOP);
    } else
        checkInputFocus(timestamp);

    /* do this after possibly reordering the window stack */
    if (disableCompositing)
        possiblyUnredirectTopmostWindow();
}

void MCompositeManagerPrivate::displayOff(bool display_off)
{
    if (display_off) {
        disableCompositing(REALLY_FORCED);
        /* stop pinging to save some battery */
        for (QHash<Window, MCompositeWindow *>::iterator it = windows.begin();
             it != windows.end(); ++it) {
             MCompositeWindow *i  = it.value();
             i->stopPing();
        }
    } else {
        if (!possiblyUnredirectTopmostWindow())
            enableCompositing(false);
        /* start pinging again */
        for (QHash<Window, MCompositeWindow *>::iterator it = windows.begin();
             it != windows.end(); ++it) {
             MCompositeWindow *i  = it.value();
             if (should_be_pinged(i))
                 i->startPing();
        }
    }
}

void MCompositeManagerPrivate::callOngoing(bool ongoing_call)
{
    if (ongoing_call) {
        // if we have fullscreen app on top, set it decorated without resizing
        Window w = getTopmostApp();
        MCompositeWindow *cw;
        if (w && (cw = COMPOSITE_WINDOW(w)) && FULLSCREEN_WINDOW(cw)) {
            cw->setDecorated(true);
            MDecoratorFrame::instance()->setManagedWindow(cw, 0, true);
            MDecoratorFrame::instance()->setOnlyStatusbar(true);
        }
        checkStacking(false);
    } else {
        // remove decoration from fullscreen windows
        for (QHash<Window, MCompositeWindow *>::iterator it = windows.begin();
             it != windows.end(); ++it) {
            MCompositeWindow *i = it.value();
            if (FULLSCREEN_WINDOW(i) && i->needDecoration())
                i->setDecorated(false);
        }
        MDecoratorFrame::instance()->setOnlyStatusbar(false);
        checkStacking(false);
        possiblyUnredirectTopmostWindow();
    }
}

void MCompositeManagerPrivate::setWindowState(Window w, int state)
{
    CARD32 d[2];
    d[0] = state;
    d[1] = None;
    XChangeProperty(QX11Info::display(), w, ATOM(WM_STATE), ATOM(WM_STATE),
                    32, PropModeReplace, (unsigned char *)d, 2);
}

void MCompositeManagerPrivate::setWindowDebugProperties(Window w)
{
#ifdef WINDOW_DEBUG
    MCompositeWindow *i = COMPOSITE_WINDOW(w);
    if (!i)
        return;

    CARD32 d[1];
    if (i->windowVisible())
        d[0] = i->isDirectRendered() ?
               ATOM(_DUI_WM_WINDOW_DIRECT_VISIBLE) : ATOM(_DUI_WM_WINDOW_COMPOSITED_VISIBLE);
    else
        d[0] = i->isDirectRendered() ?
               ATOM(_DUI_WM_WINDOW_DIRECT_INVISIBLE) : ATOM(_DUI_WM_WINDOW_COMPOSITED_INVISIBLE);

    XChangeProperty(QX11Info::display(), w, ATOM(_DUI_WM_INFO), XA_ATOM,
                    32, PropModeReplace, (unsigned char *)d, 1);
    long z = i->zValue();
    XChangeProperty(QX11Info::display(), w, ATOM(_DUI_WM_WINDOW_ZVALUE), XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *) &z, 1);

#else
    Q_UNUSED(w);
#endif
}

bool MCompositeManagerPrivate::x11EventFilter(XEvent *event)
{
    static const int damage_ev = damage_event + XDamageNotify;

    if (event->type == damage_ev) {
        XDamageNotifyEvent *e = reinterpret_cast<XDamageNotifyEvent *>(event);
        damageEvent(e);
        return true;
    }
    switch (event->type) {

    case DestroyNotify:
        destroyEvent(&event->xdestroywindow); break;
    case PropertyNotify:
        propertyEvent(&event->xproperty); break;
    case UnmapNotify:
        unmapEvent(&event->xunmap); break;
    case ConfigureNotify:
        configureEvent(&event->xconfigure); break;
    case ConfigureRequest:
        configureRequestEvent(&event->xconfigurerequest); break;
    case MapNotify:
        mapEvent(&event->xmap); break;
    case MapRequest:
        mapRequestEvent(&event->xmaprequest); break;
    case ClientMessage:
        clientMessageEvent(&event->xclient); break;
    case ButtonRelease:
    case ButtonPress:
        XAllowEvents(QX11Info::display(), ReplayPointer, event->xbutton.time);
        activateWindow(event->xbutton.window, event->xbutton.time);
        // Qt needs to handle this event for the window frame buttons
        return false;
    default:
        return false;
    }
    return true;
}

QGraphicsScene *MCompositeManagerPrivate::scene()
{
    return watch;
}

void MCompositeManagerPrivate::redirectWindows()
{
    uint children = 0, i = 0;
    Window r, p, *kids = 0;
    XWindowAttributes attr;

    XMapWindow(QX11Info::display(), xoverlay);
    QDesktopWidget *desk = QApplication::desktop();

    if (!XQueryTree(QX11Info::display(), desk->winId(),
                    &r, &p, &kids, &children)) {
        qCritical("XQueryTree failed");
        return;
    }

    for (i = 0; i < children; ++i)  {
        if (!XGetWindowAttributes(QX11Info::display(), kids[i], &attr))
            continue;
        if (attr.map_state == IsViewable &&
                localwin != kids[i] &&
                (attr.width > 1 && attr.height > 1)) {
            bindWindow(kids[i], &attr);
            if (kids[i] == localwin || kids[i] == parentWindow(localwin))
                continue;
            XGrabButton(QX11Info::display(), AnyButton, AnyModifier, kids[i],
                        True,
                        ButtonPressMask | ButtonReleaseMask | ButtonMotionMask,
                        GrabModeSync, GrabModeSync, None, None);
        }
    }
    if (kids)
        XFree(kids);
    scene()->views()[0]->setUpdatesEnabled(true);
    checkStacking(false);
}

bool MCompositeManagerPrivate::isRedirected(Window w)
{
    return (COMPOSITE_WINDOW(w) != 0);
}

bool MCompositeManagerPrivate::removeWindow(Window w)
{
    bool ret = true;
    windows_as_mapped.removeAll(w);
    if (windows.remove(w) == 0)
        ret = false;

    stacking_list.removeAll(w);

    for (int i = 0; i < TOTAL_LAYERS; ++i)
        if (stack[i] == w) stack[i] = 0;

    updateWinList();
    return ret;
}

MCompositeWindow *MCompositeManagerPrivate::bindWindow(Window window,
                XWindowAttributes *wa)
{
    Display *display = QX11Info::display();
    bool is_decorator = atom->isDecorator(window);

    XSelectInput(display, window,  StructureNotifyMask | PropertyChangeMask | ButtonPressMask);
    XCompositeRedirectWindow(display, window, CompositeRedirectManual);

    MCompAtoms::Type wtype = atom->windowType(window);
    MTexturePixmapItem *item = new MTexturePixmapItem(window, glwidget);
    item->setZValue(wtype);
    item->saveState();
    item->setIsMapped(true);
    windows[window] = item;

    QVector<Atom> states = atom->netWmStates(window);
    item->setNetWmState(states.toList());
    int fs_i = states.indexOf(ATOM(_NET_WM_STATE_FULLSCREEN));
    if (wa && fs_i == -1) {
        item->setRequestedGeometry(QRect(wa->x, wa->y, wa->width, wa->height));
    } else if (fs_i == -1) {
        XWindowAttributes a;
        if (!XGetWindowAttributes(display, window, &a)) {
            qWarning("XGetWindowAttributes for 0x%lx failed", window);
            windows.remove(window);
            delete item;
            return 0;
        }
        item->setRequestedGeometry(QRect(a.x, a.y, a.width, a.height));
    } else {
        int xres = ScreenOfDisplay(display, DefaultScreen(display))->width;
        int yres = ScreenOfDisplay(display, DefaultScreen(display))->height;
        item->setRequestedGeometry(QRect(0, 0, xres, yres));
    }

    if (!is_decorator && !item->isOverrideRedirect())
        windows_as_mapped.append(window);

    if (needDecoration(window, item)) {
        item->setDecorated(true);
        if (fs_i != -1) {
            // fullscreen window has decorator above it during ongoing call
            MDecoratorFrame::instance()->setManagedWindow(item, 0, true);
            MDecoratorFrame::instance()->setOnlyStatusbar(true);
        } else if (atom->statusBarOverlayed(window)) {
            MDecoratorFrame::instance()->setManagedWindow(item, 28);
            MDecoratorFrame::instance()->setOnlyStatusbar(false);
        } else {
            MDecoratorFrame::instance()->setManagedWindow(item);
            MDecoratorFrame::instance()->setOnlyStatusbar(false);
        }
    }
    item->setIsDecorator(is_decorator);

    if (!device_state->displayOff())
        item->updateWindowPixmap();
    stacking_list.append(window);
    addItem(item);
    if (wtype == MCompAtoms::NORMAL)
        item->setWindowTypeAtom(ATOM(_NET_WM_WINDOW_TYPE_NORMAL));
    else
        item->setWindowTypeAtom(atom->getType(window));

    if (wtype == MCompAtoms::INPUT) {
        if (!device_state->displayOff())
            item->updateWindowPixmap();
        return item;
    } else if (wtype == MCompAtoms::DESKTOP) {
        // just in case startup sequence changes
        stack[DESKTOP_LAYER] = window;
        connect(this, SIGNAL(inputEnabled()), item,
                SLOT(setUnBlurred()));
        return item;
    }

    item->manipulationEnabled(true);

    // the decorator got mapped. this is here because the compositor
    // could be restarted at any point
    if (is_decorator && !MDecoratorFrame::instance()->decoratorItem()) {
        // initially update the texture for this window because this
        // will be hidden on first show
        if (!device_state->displayOff())
            item->updateWindowPixmap();
        item->setVisible(false);
        MDecoratorFrame::instance()->setDecoratorItem(item);
    } else
        item->setVisible(true);

    XWMHints *h = XGetWMHints(display, window);
    if (h) {
        if ((h->flags & StateHint) && (h->initial_state == IconicState)) {
            setWindowState(window, IconicState);
        }
        XFree(h);
    }

    checkStacking(false);

    if (!device_state->displayOff() && should_be_pinged(item))
        item->startPing();

    return item;
}

void MCompositeManagerPrivate::addItem(MCompositeWindow *item)
{
    watch->addItem(item);
    updateWinList();
    setWindowDebugProperties(item->window());
    connect(item, SIGNAL(acceptingInput()), SLOT(enableInput()));

    if (atom->windowType(item->window()) == MCompAtoms::DESKTOP) {
        connect(item, SIGNAL(desktopActivated(MCompositeWindow *)),
                SLOT(onDesktopActivated(MCompositeWindow *)));
        return;
    }

    connect(item, SIGNAL(itemIconified(MCompositeWindow *)), SLOT(exposeDesktop()));
    connect(this, SIGNAL(compositingEnabled()), item, SLOT(startTransition()));
    connect(item, SIGNAL(itemRestored(MCompositeWindow *)), SLOT(raiseOnRestore(MCompositeWindow *)));
    connect(item, SIGNAL(itemIconified(MCompositeWindow *)), SLOT(iconifyOnLower(MCompositeWindow *)));

    // ping protocol
    connect(item, SIGNAL(windowHung(MCompositeWindow *)),
            SLOT(gotHungWindow(MCompositeWindow *)));

    connect(item, SIGNAL(pingTriggered(MCompositeWindow *)),
            SLOT(sendPing(MCompositeWindow *)));
}

void MCompositeManagerPrivate::updateWinList(bool stackingOnly)
{
    if (!stackingOnly) {
        static QList<Window> prev_list;
        /* windows_as_mapped may contain invisible windows, so we need to
         * make a new list without them */
        QList<Window> new_list;
        for (int i = 0; i < windows_as_mapped.size(); ++i) {
            Window w = windows_as_mapped.at(i);
            MCompositeWindow *d = COMPOSITE_WINDOW(w);
            if (d->isMapped()) new_list.append(w);
        }

        if (new_list != prev_list) {
            XChangeProperty(QX11Info::display(),
                            RootWindow(QX11Info::display(), 0),
                            ATOM(_NET_CLIENT_LIST),
                            XA_WINDOW, 32, PropModeReplace,
                            (unsigned char *)new_list.toVector().data(),
                            new_list.size());

            prev_list = QList<Window>(new_list);
        }
    }
    checkStacking(false);
}

/*!
   Helper function to arrange arrange the order of the windows
   in the _NET_CLIENT_LIST_STACKING
*/
void
MCompositeManagerPrivate::positionWindow(Window w,
        MCompositeManagerPrivate::StackPosition pos)
{
    int wp = stacking_list.indexOf(w);
    if (wp == -1 || wp >= stacking_list.size())
        return;

    switch (pos) {
    case STACK_BOTTOM: {
        //qDebug() << __func__ << "to bottom:" << w;
        stacking_list.move(wp, 0);
        break;
    }
    case STACK_TOP: {
        //qDebug() << __func__ << "to top:" << w;
        stacking_list.move(wp, stacking_list.size() - 1);
        break;
    }
    default:
        break;

    }
    updateWinList(true);
}

void MCompositeManagerPrivate::enableCompositing(bool forced,
                                                   bool ignore_display_off)
{
    if ((!ignore_display_off && device_state->displayOff())
        || (compositing && !forced))
        return;

    XWindowAttributes a;
    if (!XGetWindowAttributes(QX11Info::display(), xoverlay, &a)) {
        qCritical("XGetWindowAttributes for the overlay failed");
        return;
    }
    if (a.map_state == IsUnmapped)
        mapOverlayWindow();
    else
        enableRedirection();
}

void MCompositeManagerPrivate::mapOverlayWindow()
{
    // Render the previous contents of the framebuffer
    // here exactly before compositing was enabled
    // Ensure the changes are visualized immediately

    QPainter m(glwidget);
    m.drawPixmap(0, 0, QPixmap::grabWindow(QX11Info::appRootWindow()));
    glwidget->update();
    QCoreApplication::flush();

    // Freeze painting of framebuffer as of this point
    scene()->views()[0]->setUpdatesEnabled(false);
    XMoveWindow(QX11Info::display(), localwin, -2, -2);
    XMapWindow(QX11Info::display(), xoverlay);
}

void MCompositeManagerPrivate::enableRedirection()
{
    for (QHash<Window, MCompositeWindow *>::iterator it = windows.begin();
            it != windows.end(); ++it) {
        MCompositeWindow *tp  = it.value();
        if (tp->windowVisible())
            ((MTexturePixmapItem *)tp)->enableRedirectedRendering();
        setWindowDebugProperties(it.key());
    }
    glwidget->update();
    compositing = true;
    /* send VisibilityNotifies */
    checkStacking(true);

    scene()->views()[0]->setUpdatesEnabled(true);

    usleep(50000);
    // At this point everything should be rendered off-screen
    emit compositingEnabled();
}

void MCompositeManagerPrivate::disableCompositing(ForcingLevel forced)
{
    if (MCompositeWindow::isTransitioning() && forced != REALLY_FORCED)
        return;
    if (!compositing && forced == NO_FORCED)
        return;
    
    if (forced != REALLY_FORCED)
        // we could still have existing decorator on-screen.
        // ensure we don't accidentally disturb it
        for (QHash<Window, MCompositeWindow *>::iterator it = windows.begin();
             it != windows.end(); ++it) {
            MCompositeWindow *i  = it.value();
            if (i->isDecorator())
                continue;
            if (i->windowVisible() && (i->hasAlpha() || i->needDecoration())) 
                return;
        }

    scene()->views()[0]->setUpdatesEnabled(false);

    for (QHash<Window, MCompositeWindow *>::iterator it = windows.begin();
            it != windows.end(); ++it) {
        MCompositeWindow *tp  = it.value();
        // checks above fail. somehow decorator got in. stop it at this point
        if (forced == REALLY_FORCED ||
            (!tp->isDecorator() && !tp->isIconified() && !tp->hasAlpha()))
            ((MTexturePixmapItem *)tp)->enableDirectFbRendering();
        setWindowDebugProperties(it.key());

        /* Mark all windows obscured so that Unobscured events are sent when
         * compositing is back on and we synthesize them. */
        tp->setWindowObscured(true, true);
    }

    XSync(QX11Info::display(), False);
    if (forced != REALLY_FORCED)
        usleep(50000);

    XUnmapWindow(QX11Info::display(), xoverlay);
    XFlush(QX11Info::display());

    if (MDecoratorFrame::instance()->decoratorItem())
        MDecoratorFrame::instance()->lower();
    
    compositing = false;
}

void MCompositeManagerPrivate::sendPing(MCompositeWindow *w)
{
    Window window = ((MCompositeWindow *) w)->window();
    ulong t = QDateTime::currentDateTime().toTime_t();
    w->setClientTimeStamp(t);

    XEvent ev;
    memset(&ev, 0, sizeof(ev));

    ev.xclient.type = ClientMessage;
    ev.xclient.window = window;
    ev.xclient.message_type = ATOM(WM_PROTOCOLS);
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = ATOM(_NET_WM_PING);
    ev.xclient.data.l[1] = t;
    ev.xclient.data.l[2] = window;

    XSendEvent(QX11Info::display(), window, False, NoEventMask, &ev);
    XSync(QX11Info::display(), False);
}

void MCompositeManagerPrivate::gotHungWindow(MCompositeWindow *w)
{
    if (!MDecoratorFrame::instance()->decoratorItem())
        return;

    enableCompositing(true);

    // own the window so we could kill it if we want to.
    MDecoratorFrame::instance()->setManagedWindow(w, 0, true);
    MDecoratorFrame::instance()->setOnlyStatusbar(false);
    MDecoratorFrame::instance()->setAutoRotation(true);
    checkStacking(false);
    MDecoratorFrame::instance()->raise();
    w->updateWindowPixmap();
}

void MCompositeManagerPrivate::showLaunchIndicator(int timeout)
{
    if (!launchIndicator) {
        launchIndicator = new QGraphicsTextItem("launching...");
        scene()->addItem(launchIndicator);
        launchIndicator->setPos((scene()->sceneRect().width() / 2) -
                                - (launchIndicator->boundingRect().width() / 2),
                                (scene()->sceneRect().height() / 2) -
                                (launchIndicator->boundingRect().width() / 2));
    }
    launchIndicator->show();
    QTimer::singleShot(timeout * 1000, this, SLOT(hideLaunchIndicator()));
}

void MCompositeManagerPrivate::hideLaunchIndicator()
{
    if (launchIndicator)
        launchIndicator->hide();
}

MCompositeManager::MCompositeManager(int &argc, char **argv)
    : QApplication(argc, argv)
{
    if (QX11Info::isCompositingManagerRunning()) {
        qCritical("Compositing manager already running.");
        ::exit(0);
    }

    d = new MCompositeManagerPrivate(this);
    MRmiServer *s = new MRmiServer(".mcompositor", this);
    s->exportObject(this);
}

MCompositeManager::~MCompositeManager()
{
    delete d;
    d = 0;
}

void MCompositeManager::setGLWidget(QGLWidget *glw)
{
    glw->setAttribute(Qt::WA_PaintOutsidePaintEvent);
    d->glwidget = glw;
}

QGraphicsScene *MCompositeManager::scene()
{
    return d->scene();
}

void MCompositeManager::prepareEvents()
{
    d->prepare();
}

bool MCompositeManager::x11EventFilter(XEvent *event)
{
    return d->x11EventFilter(event);
}

void MCompositeManager::setSurfaceWindow(Qt::HANDLE window)
{
    d->localwin = window;
}

void MCompositeManager::redirectWindows()
{
    d->redirectWindows();
}

bool MCompositeManager::isRedirected(Qt::HANDLE w)
{
    return d->isRedirected(w);
}

void MCompositeManager::enableCompositing()
{
    d->enableCompositing();
}

void MCompositeManager::disableCompositing()
{
    d->disableCompositing();
}

void MCompositeManager::showLaunchIndicator(int timeout)
{
    d->showLaunchIndicator(timeout);
}

void MCompositeManager::hideLaunchIndicator()
{
    d->hideLaunchIndicator();
}

void MCompositeManager::topmostWindowsRaise()
{
    d->checkStacking(false);
}
