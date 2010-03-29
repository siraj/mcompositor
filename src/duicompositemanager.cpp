/***************************************************************************
**
** Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (directui@nokia.com)
**
** This file is part of duicompositor.
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

#include "duitexturepixmapitem.h"
#include "duitexturepixmapitem_p.h"
#include "duicompositemanager.h"
#include "duicompositemanager_p.h"
#include "duicompositescene.h"
#include "duisimplewindowframe.h"
#include "duidecoratorframe.h"
#include <duirmiserver.h>

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
#define _log(txt, args... ) { FILE *out; out = fopen("/tmp/duicompositor.log", "a"); if(out) { fprintf(out, "" txt, ##args ); fclose(out); } }

class DuiCompAtoms
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
        _DUI_DECORATOR_WINDOW,
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
    static DuiCompAtoms* instance();
    Type windowType(Window w);
    bool isDecorator(Window w);
    bool statusBarOverlayed(Window w);
    int getPid(Window w);
    long getWmState(Window w);
    Atom getState(Window w);
    QRectF iconGeometry(Window w);
    QVector<Atom> netWmStates(Window w);
    unsigned int get_opacity_prop(Display *dpy, Window w, unsigned int def);
    double get_opacity_percent(Display *dpy, Window w, double def);
    int globalAlphaFromWindow(Window w);

    Atom getAtom(const unsigned int name);
    Atom getType(Window w);

    static Atom atoms[ATOMS_TOTAL];

private:
    explicit DuiCompAtoms();
    static DuiCompAtoms* d;

    int intValueProperty(Window w, Atom property);
    Atom getAtom(Window w, Atoms atomtype);

    Display *dpy;
};

#define ATOM(t) DuiCompAtoms::instance()->getAtom(DuiCompAtoms::t)
Atom DuiCompAtoms::atoms[DuiCompAtoms::ATOMS_TOTAL];
Window DuiCompositeManagerPrivate::stack[TOTAL_LAYERS];
DuiCompAtoms* DuiCompAtoms::d = 0;
static bool hasDock  = false;
static QRect availScreenRect = QRect();

// temporary launch indicator. will get replaced later
static QGraphicsTextItem *launchIndicator = 0;

DuiCompAtoms* DuiCompAtoms::instance()
{    
    if (!d)
        d = new DuiCompAtoms();
    return d;
}

DuiCompAtoms::DuiCompAtoms()
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

	    "_DUI_DECORATOR_WINDOW",
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
    
    if (!XInternAtoms(dpy, (char**)atom_names, ATOMS_TOTAL, False, atoms))
      qCritical("XInternAtoms failed");

    XChangeProperty(dpy, QX11Info::appRootWindow(), atoms[_NET_SUPPORTED], 
                    XA_ATOM, 32, PropModeReplace,(unsigned char *)atoms, 
                    ATOMS_TOTAL);
}

/* FIXME Workaround for bug NB#161282 */
static bool is_desktop_window(Window w, Atom type = 0)
{
    Atom a;
    if (!type)
        a = DuiCompAtoms::instance()->getType(w);
    else
	a = type;
    if (a == ATOM(_NET_WM_WINDOW_TYPE_DESKTOP))
	return true;
    XTextProperty textp;
    if (!XGetWMName(QX11Info::display(), w, &textp))
	return false;
    if (strcmp((const char*)textp.value, "duihome") == 0
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
        a = DuiCompAtoms::instance()->getType(w);
    else
	a = type;
    if (a != ATOM(_NET_WM_WINDOW_TYPE_DOCK))
	return false;
    XTextProperty textp;
    if (!XGetWMName(QX11Info::display(), w, &textp))
	return false;
    if (strcmp((const char*)textp.value, "duihome") == 0) {
	return true;
    }
    return false;
}

DuiCompAtoms::Type DuiCompAtoms::windowType(Window w)
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

    // fix this later. this value always gets returned for transient_for
    // windows such as menus and dialogs which shouldn't be the case
    return UNKNOWN;
}

bool DuiCompAtoms::isDecorator(Window w)
{
    return (intValueProperty(w, atoms[_DUI_DECORATOR_WINDOW]) == 1);
}
 
// Remove this when statusbar in-scene approach is done
bool DuiCompAtoms::statusBarOverlayed(Window w)
{
    return (intValueProperty(w, atoms[_DUI_STATUSBAR_OVERLAY]) == 1);
}

int DuiCompAtoms::getPid(Window w)
{
    return intValueProperty(w, atoms[_NET_WM_PID]);
}

Atom DuiCompAtoms::getState(Window w)
{
    return getAtom(w, _NET_WM_STATE);
}

QRectF DuiCompAtoms::iconGeometry(Window w)
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

QVector<Atom> DuiCompAtoms::netWmStates(Window w)
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

long DuiCompAtoms::getWmState(Window w)
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

unsigned int DuiCompAtoms::get_opacity_prop(Display *dpy, Window w, unsigned int def)
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

double DuiCompAtoms::get_opacity_percent(Display *dpy, Window w, double def)
{
    unsigned int opacity = get_opacity_prop(dpy, w,
                                            (unsigned int)(OPAQUE * def));
    return opacity * 1.0 / OPAQUE;
}

int DuiCompAtoms::globalAlphaFromWindow(Window w)
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

Atom DuiCompAtoms::getAtom(const unsigned int name)
{
    return atoms[name];
}

int DuiCompAtoms::intValueProperty(Window w, Atom property)
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

Atom DuiCompAtoms::getType(Window w)
{
    Atom t = getAtom(w, _NET_WM_WINDOW_TYPE);
    if (t)
        return t;
    return atoms[_NET_WM_WINDOW_TYPE_NORMAL];
}

Atom DuiCompAtoms::getAtom(Window w, Atoms atomtype)
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
    DuiCompAtoms* atom = DuiCompAtoms::instance();
    QVector<Atom> states = atom->netWmStates(window);
    bool update_root = false;

    switch (toggle) {
    case 0: {
        int i = states.indexOf(skip);
        if (i != -1) {
            states.remove(i);
            XChangeProperty(QX11Info::display(), window,
                            ATOM(_NET_WM_STATE), XA_ATOM, 32, PropModeReplace,
                            (unsigned char *) states.data(), states.size());
            update_root = true;
        }
    } break;
    case 1: {
        states.append(skip);
        if (!states.isEmpty()) {
            XChangeProperty(QX11Info::display(), window,
                            ATOM(_NET_WM_STATE), XA_ATOM, 32, PropModeReplace,
                            (unsigned char *) states.data(), states.size());
            update_root = true;
        }
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
    DuiCompAtoms* atom = DuiCompAtoms::instance();

    if ((atom->getState(window) == ATOM(_NET_WM_STATE_FULLSCREEN)) || 
        (atom->statusBarOverlayed(window)))
        return false;

    return true;
}

static void fullscreen_wm_state(DuiCompositeManagerPrivate *priv,
		                int toggle, Window window)
{
    Atom fullscreen = ATOM(_NET_WM_STATE_FULLSCREEN);
    Display* dpy = QX11Info::display();
    DuiCompAtoms* atom = DuiCompAtoms::instance();
    QVector<Atom> states = atom->netWmStates(window);
    int i = states.indexOf(fullscreen);
    
    switch (toggle) {
    case 0: {        
        if (i != -1) {
            states.remove(i);
            XChangeProperty(QX11Info::display(), window,
                            ATOM(_NET_WM_STATE), XA_ATOM, 32, PropModeReplace,
                            (unsigned char *) states.data(), states.size());
        }

        DuiCompositeWindow* win = DuiCompositeWindow::compositeWindow(window);
        if(win && need_geometry_modify(window) && !availScreenRect.isEmpty()) {
            QRect r = availScreenRect;
            XMoveResizeWindow(dpy, window, r.x(), r.y(), r.width(), r.height());
        }
        
        priv->checkStacking();
    } break;
    case 1: {        
        if (i != -1 || states.isEmpty()) {
            states.append(fullscreen);
            XChangeProperty(QX11Info::display(), window,
                            ATOM(_NET_WM_STATE), XA_ATOM, 32, PropModeReplace,
                            (unsigned char *) states.data(), states.size());
        } 

        int xres = ScreenOfDisplay(dpy, DefaultScreen(dpy))->width;
        int yres = ScreenOfDisplay(dpy, DefaultScreen(dpy))->height;
        XMoveResizeWindow(dpy, window, 0,0, xres, yres);        
	/* FIXME: is raising a fullscreen window necessary? We could
	 * have several fullscreen applications open at the same time. */
	priv->activateWindow(window, CurrentTime);
    } break;
    default: break;
    }
}

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

DuiCompositeManagerPrivate::DuiCompositeManagerPrivate(QObject *p)
    : QObject(p),
      glwidget(0),
      damage_cache(0),
      arranged(false),
      compositing(true)
{
    watch = new DuiCompositeScene(this);
    atom = DuiCompAtoms::instance();
}

DuiCompositeManagerPrivate::~DuiCompositeManagerPrivate()
{
    delete watch;
    delete atom;
    watch   = 0;
    atom = 0;
}

Window DuiCompositeManagerPrivate::parentWindow(Window child)
{
    uint children = 0;
    Window r, p, *kids = 0;

    XQueryTree(QX11Info::display(), child, &r, &p, &kids, &children);
    if (kids)
        XFree(kids);
    return p;
}

void DuiCompositeManagerPrivate::disableInput()
{
    watch->setupOverlay(xoverlay, QRect(0, 0, 0, 0), true);
    watch->setupOverlay(localwin, QRect(0, 0, 0, 0), true);
}

void DuiCompositeManagerPrivate::enableInput()
{
    watch->setupOverlay(xoverlay, QRect(0, 0, 0, 0));
    watch->setupOverlay(localwin, QRect(0, 0, 0, 0));

    emit inputEnabled();
}

void DuiCompositeManagerPrivate::prepare()
{
    watch->prepareRoot();
    Window w;
    QString wm_name = "DuiCompositor";

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
                    PropModeReplace, (unsigned char*) wm_name.toUtf8().data(), 
                    wm_name.size());


    Xutf8SetWMProperties(QX11Info::display(), w, "DuiCompositor",
                         "DuiCompositor", NULL, 0, NULL, NULL,
                         NULL);
    Atom a = XInternAtom(QX11Info::display(), "_NET_WM_CM_S0", False);
    XSetSelectionOwner(QX11Info::display(), a, w, 0);

    xoverlay = XCompositeGetOverlayWindow(QX11Info::display(),
                                          RootWindow(QX11Info::display(), 0));
    XReparentWindow(QX11Info::display(), localwin, xoverlay, 0, 0);
    enableInput();

    XDamageQueryExtension(QX11Info::display(), &damage_event, &damage_error);
}

bool DuiCompositeManagerPrivate::needDecoration(Window window)
{
    DuiCompAtoms::Type t = atom->windowType(window);
    return (t != DuiCompAtoms::FRAMELESS
            && t != DuiCompAtoms::DESKTOP
            && t != DuiCompAtoms::NOTIFICATION
            && t != DuiCompAtoms::INPUT
            && t != DuiCompAtoms::DOCK
            && !transient_for(window));
}

void DuiCompositeManagerPrivate::damageEvent(XDamageNotifyEvent *e)
{
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
    DuiCompositeWindow *item = texturePixmapItem(e->drawable);
    damage_cache = item;
    if (item)
        item->updateWindowPixmap(rects, num);

    if (rects)
        XFree(rects);
}

void DuiCompositeManagerPrivate::destroyEvent(XDestroyWindowEvent *e)
{
    bool already_unredirected = false;
    DuiCompositeWindow *item = texturePixmapItem(e->window);
    if (item) {
        if (item->isDirectRendered())
            already_unredirected = true;
        scene()->removeItem(item);
        delete item;
        if (!removeWindow(e->window))
            qWarning("destroyEvent(): Error removing window");
        glwidget->update();
        damage_cache = 0;
    } else {
        // We got a destroy event from a framed window
        FrameData fd = framed_windows.value(e->window);
        if (!fd.frame)
            return;

        XGrabServer(QX11Info::display());
        XReparentWindow(QX11Info::display(), e->window,
                        RootWindow(QX11Info::display(), 0), 0, 0);
        XRemoveFromSaveSet(QX11Info::display(), e->window);
        framed_windows.remove(e->window);
        XUngrabServer(QX11Info::display());
        delete fd.frame;
    }
    if (!already_unredirected)
        XCompositeUnredirectWindow(QX11Info::display(), e->window,
                                   CompositeRedirectAutomatic);
    XUngrabButton(QX11Info::display(), AnyButton, AnyModifier, e->window);
    XSync(QX11Info::display(), False);
}

/*
  This doesn't really do anything for now. It will be used to get
  the NETWM hints in the future like window opacity and stuff
*/
void DuiCompositeManagerPrivate::propertyEvent(XPropertyEvent *e)
{
    Q_UNUSED(e);
    /*
      Atom opacityAtom = ATOM(_NET_WM_WINDOW_OPACITY);
      if(e->atom == opacityAtom)
        ;
    */
}

static bool is_app_window(DuiCompositeWindow *cw)
{
    if (cw && !cw->transientFor() &&
        (cw->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_NORMAL) ||
	 cw->windowTypeAtom() == ATOM(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE))
        && !DuiCompAtoms::instance()->isDecorator(cw->window()))
	return true;
    return false;
}

void DuiCompositeManagerPrivate::unmapEvent(XUnmapEvent *e)
{
    if (e->window == xoverlay)
        return;

    /* find the topmost application to see if it closed */
    Window topmost_app = 0;
    for (int i = stacking_list.size() - 1; i >= 0; --i) {
	 Window w = stacking_list.at(i);
	 if (w == stack[DESKTOP_LAYER])
	     /* desktop is above all applications */
             break;
	 DuiCompositeWindow *cw = windows.value(w);
	 if (cw && cw->isVisible() && is_app_window(cw)) {
             topmost_app = w;
             break;
         }
    }

    DuiCompositeWindow *item = texturePixmapItem(e->window);
    if (item) {
        setWindowState(e->window, IconicState);
        if (item->isVisible()) {
            item->setVisible(false);
            item->clearTexture();
            glwidget->update();
        }
        if (DuiDecoratorFrame::instance()->managedWindow() == e->window)
            DuiDecoratorFrame::instance()->lower();
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
    disableCompositing();

    toggle_global_alpha_blend(0);
    set_global_alpha(0,255);

    for (int i = 0; i < TOTAL_LAYERS; ++i)
         if (stack[i] == e->window) stack[i] = 0;

    if (topmost_app == e->window) {
        int wp = stacking_list.indexOf(e->window) - 1;
        if (wp < 0)
	    return;
        /* either lower window of the application (in chained window case),
         * or duihome is activated */
        activateWindow(stacking_list.at(wp), CurrentTime, false);
    }
}

void DuiCompositeManagerPrivate::configureEvent(XConfigureEvent *e)
{
    if (e->window == xoverlay)
        return;

    DuiCompositeWindow *item = texturePixmapItem(e->window);
    if (item) {
        item->setPos(e->x, e->y);
        item->resize(e->width, e->height);
        if (e->override_redirect == True)
            return;

        Window above = e->above;
        if (above != None) {
            if (item->needDecoration() && DuiDecoratorFrame::instance()->decoratorItem()) {
                DuiDecoratorFrame::instance()->setManagedWindow(e->window);
                DuiDecoratorFrame::instance()->decoratorItem()->setVisible(true);
                DuiDecoratorFrame::instance()->raise();
                DuiDecoratorFrame::instance()->decoratorItem()->setZValue(item->zValue()+1);
                item->update();
            }
            item->setIconified(false);            
        } else {
	    // FIXME: seems that this branch is never executed?
            if (e->window == DuiDecoratorFrame::instance()->managedWindow())
                DuiDecoratorFrame::instance()->lower();
            item->setIconified(true);
            // ensure ZValue is set only after the animation is done
            item->requestZValue(0);

            DuiCompositeWindow *desktop = texturePixmapItem(stack[DESKTOP_LAYER]);
            if (desktop)
#if (QT_VERSION >= 0x040600)
                item->stackBefore(desktop);
#endif
        }
    }
}

void DuiCompositeManagerPrivate::configureRequestEvent(XConfigureRequestEvent *e)
{
    if (e->parent != RootWindow(QX11Info::display(), 0))
        return;

    bool isInput = (atom->windowType(e->window) == DuiCompAtoms::INPUT);
    // sandbox these windows. we own them
    if (atom->isDecorator(e->window))
        return;

    /*qDebug() << __func__ << "CONFIGURE REQUEST FOR:" << e->window
	    << e->x << e->y << e->width << e->height << "above/mode:"
	    << e->above << e->detail;*/

    // dock changed
    if (hasDock && (atom->windowType(e->window) == DuiCompAtoms::DOCK)) {
        dock_region = QRegion(e->x, e->y, e->width, e->height);
        QRect r = (QRegion(QApplication::desktop()->screenGeometry()) - dock_region).boundingRect();
        if (stack[DESKTOP_LAYER] && need_geometry_modify(stack[DESKTOP_LAYER]))
            XMoveResizeWindow(QX11Info::display(), stack[DESKTOP_LAYER], r.x(), r.y(), r.width(), r.height());

        if (stack[APPLICATION_LAYER]) {
            if(need_geometry_modify(stack[APPLICATION_LAYER]))
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

    if ((e->detail == Above) && (e->above == None) && !isInput ) {
        XWindowAttributes a;
        if (!XGetWindowAttributes(QX11Info::display(), e->window, &a)) {
	    qWarning("XGetWindowAttributes for 0x%lx failed", e->window);
	    return;
	}
        if ((a.map_state == IsViewable) && (atom->windowType(e->window) != DuiCompAtoms::DOCK)) {
            setWindowState(e->window, NormalState);
            setExposeDesktop(false);
            stack[APPLICATION_LAYER] = e->window;

            // selective compositing support
            DuiCompositeWindow *i = texturePixmapItem(e->window);
            if (i) {
                // since we call disable compositing immediately
                // we don't see the animated transition
                if (!i->hasAlpha() && !i->needDecoration()) {
                    i->setIconified(false);        
                    disableCompositing(true);
                } else if (DuiDecoratorFrame::instance()->managedWindow() == e->window)
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
	        checkStacking();
	    }
	} else {
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
	        checkStacking();
	    }
	} else {
	    positionWindow(e->window, STACK_BOTTOM);
	}
    }

    /* stacking is done in checkStacking(), based on stacking_list */
    unsigned int value_mask = e->value_mask & ~(CWSibling | CWStackMode);
    if (value_mask)
        XConfigureWindow(QX11Info::display(), e->window, value_mask, &wc);
}

void DuiCompositeManagerPrivate::mapRequestEvent(XMapRequestEvent *e)
{
    XWindowAttributes a;
    Display *dpy  = QX11Info::display();
    bool hasAlpha = false;

    if (!XGetWindowAttributes(QX11Info::display(), e->window, &a))
	return;
    if (!hasDock) {
        hasDock = (atom->windowType(e->window) == DuiCompAtoms::DOCK);
        if (hasDock)
            dock_region = QRegion(a.x, a.y, a.width, a.height);
    }
    int xres = ScreenOfDisplay(dpy, DefaultScreen(dpy))->width;
    int yres = ScreenOfDisplay(dpy, DefaultScreen(dpy))->height;

    if ((atom->windowType(e->window) == DuiCompAtoms::FRAMELESS
            || atom->windowType(e->window) == DuiCompAtoms::DESKTOP
            || atom->windowType(e->window) == DuiCompAtoms::INPUT)
            && (atom->windowType(e->window) != DuiCompAtoms::DOCK)) {
        if (hasDock) {
            QRect r = (QRegion(QApplication::desktop()->screenGeometry()) - dock_region).boundingRect();
            if(availScreenRect != r)
                availScreenRect = r;
            if(need_geometry_modify(e->window))
                XMoveResizeWindow(dpy, e->window, r.x(), r.y(), r.width(), r.height());
        } else if ((a.width != xres) && (a.height != yres)) {
            XResizeWindow(dpy, e->window, xres, yres);
        }
    }
    
    if (atom->isDecorator(e->window)) {
        enableCompositing();
        DuiDecoratorFrame::instance()->setDecoratorWindow(e->window);
        return;
    }
    
    XRenderPictFormat *format = XRenderFindVisualFormat(QX11Info::display(),
                                                        a.visual);
    if (format && (format->type == PictTypeDirect && format->direct.alphaMask)){
        enableCompositing();
        hasAlpha = true;
    }
    
    if (needDecoration(e->window)) {
        XSelectInput(dpy, e->window,
                     StructureNotifyMask | ColormapChangeMask |
                     PropertyChangeMask);
        XAddToSaveSet(QX11Info::display(), e->window);

        if (DuiDecoratorFrame::instance()->decoratorItem()) {
            enableCompositing();
            XMapWindow(QX11Info::display(), e->window);
            // initially visualize decorator item so selective compositing
            // checks won't disable compositing
            DuiDecoratorFrame::instance()->decoratorItem()->setVisible(true);
        } else {
            DuiSimpleWindowFrame *frame = 0;
            FrameData f = framed_windows.value(e->window);
            frame = f.frame;
            if (!frame) {
                frame = new DuiSimpleWindowFrame(e->window);
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

#if 0
static void send_window_obscured(Window w, bool obscured)
{
	 XVisibilityEvent c;
	 c.type       = VisibilityNotify;
	 c.send_event = True;
	 c.window     = w;
	 c.state      = obscured ? VisibilityFullyObscured :
		                   VisibilityUnobscured;
	 XSendEvent(QX11Info::display(), w, true,
		    VisibilityChangeMask, (XEvent *)&c);
}
#endif

/* recursion is needed to handle transients that are transient for other
 * transients */
static void raise_transients(DuiCompositeManagerPrivate *priv,
		             Window w, int last_i)
{
    Window first_moved = 0;
    for (int i = 0; i < last_i; ) {
	 Window iw = priv->stacking_list.at(i);
	 if (iw == first_moved)
	     /* each window is only considered once */
	     break;
	 DuiCompositeWindow *cw = priv->windows.value(iw);
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
  XChangeProperty (QX11Info::display(), RootWindow(QX11Info::display(), 0), 
                   ATOM(_NET_CLIENT_LIST),
                   XA_WINDOW, 32, PropModeAppend,
                   (unsigned char*)&data, 0);

  XIfEvent (QX11Info::display(), &xevent, timestamp_predicate, NULL);

  return xevent.xproperty.time;
}
#endif

/* NOTE: this assumes that stacking is correct */
void DuiCompositeManagerPrivate::checkInputFocus(Time timestamp)
{
    Window w = None;

    /* find topmost window wanting the input focus */
    for (int i = stacking_list.size() - 1; i >= 0; --i) {
	 Window iw = stacking_list.at(i);
	 DuiCompositeWindow *cw = windows.value(iw);
	 if (!cw)
	     continue;
	 /* FIXME: store mapping state somewhere to avoid this round trip */
         XWindowAttributes a;
         if (!XGetWindowAttributes(QX11Info::display(), iw, &a)
	     || a.map_state != IsViewable)
	     continue;
	 /* workaround for NB#161629 */
	 if (is_desktop_dock(iw))
	     continue;
	 if (isSelfManagedFocus(iw)) {
	     w = iw;
	     break;
	 }
	 /* FIXME: do this based on geometry when geometry setting works well
	  * (now it doesn't) */
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
    DuiCompositeWindow *cw = windows.value(w);
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
void DuiCompositeManagerPrivate::checkStacking(Time timestamp)
{
    static QList<Window> prev_list;
    Window active_app = 0, duihome = stack[DESKTOP_LAYER], first_moved;
    int last_i = stacking_list.size() - 1;
    bool desktop_up = false;
    int app_i = -1;

    /* find the topmost application, stack[APPLICATION_LAYER] is currently
     * not reliable enough */
    for (int i = last_i; i >= 0; --i) {
	 Window w = stacking_list.at(i);
	 if (w == duihome) {
	     /* desktop is above all applications */
             desktop_up = true;
             break;
	 }
	 DuiCompositeWindow *cw = windows.value(w);
	 if (cw && cw->isVisible() && is_app_window(cw)) {
             active_app = w;
	     app_i = i;
             break;
         }
    }
    if (!active_app || app_i < 0)
        desktop_up = true;

    /* raise active app with its transients, or duihome if
     * there is no active application */
    if (!desktop_up && active_app && app_i >= 0) {
	if (duihome) {
            /* stack duihome right below the application */
	    stacking_list.move(stacking_list.indexOf(duihome), last_i);
	    app_i = stacking_list.indexOf(active_app);
	}
	/* raise application windows belonging to the same group */
	DuiCompositeWindow *cw = windows.value(active_app);
	XID group;
	if (cw && (group = cw->windowGroup())) {
	    for (int i = 0; i < app_i; ) {
	         cw = windows.value(stacking_list.at(i));
		 if (is_app_window(cw) && cw->windowGroup() == group) {
	             stacking_list.move(i, last_i);
	             /* active_app was moved, update the index */
	             app_i = stacking_list.indexOf(active_app);
		     /* TODO: transients */
		 } else ++i;
	    }
	}
	stacking_list.move(app_i, last_i);
	/* raise decorator above the application */
	if (DuiDecoratorFrame::instance()->decoratorItem() &&
	    DuiDecoratorFrame::instance()->managedWindow() == active_app) {
	    int deco_i = stacking_list.indexOf(
	           DuiDecoratorFrame::instance()->decoratorItem()->window());
	    if (deco_i >= 0)
	        stacking_list.move(deco_i, last_i);
	}
	/* raise transients recursively */
	raise_transients(this, active_app, last_i);
    } else if (duihome) {
	//qDebug() << "raising home window" << duihome;
	stacking_list.move(stacking_list.indexOf(duihome), last_i);
    }

    /* raise docks if either the desktop is up or the application is
     * non-fullscreen */
    if (desktop_up || !active_app || app_i < 0 ||
        !(atom->getState(active_app) == ATOM(_NET_WM_STATE_FULLSCREEN))) {
	first_moved = 0;
	for (int i = 0; i < last_i; ) {
	     Window w = stacking_list.at(i);
	     if (w == first_moved) break;
	     DuiCompositeWindow *cw = windows.value(w);
	     if (cw && cw->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_DOCK)){
		 stacking_list.move(i, last_i);
		 if (!first_moved) first_moved = w;
	     } else ++i;
	}
    }
    /* raise all system-modal dialogs */
    first_moved = 0;
    for (int i = 0; i < last_i; ) {
	 /* TODO: transients */
	 Window w = stacking_list.at(i);
	 if (w == first_moved) break;
     	 DuiCompositeWindow *cw = windows.value(w);
	 if (cw && !cw->transientFor()
	     && cw->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_DIALOG)) {
	     stacking_list.move(i, last_i);
	     if (!first_moved) first_moved = w;
	 } else ++i;
    }
    /* raise all keep-above flagged and input methods, at the same time
     * preserving their mutual stacking order */
    first_moved = 0;
    for (int i = 0; i < last_i; ) {
         /* TODO: transients */
	 Window w = stacking_list.at(i);
	 if (w == first_moved) break;
	 /* FIXME: this causes 1-2 round trips to the X server for each window,
	  * the properties should be kept up-to-date by PropertyNotifys. */
         if (atom->isDecorator(w)) {
	     ++i;
             continue;
	 }
         DuiCompositeWindow *cw = windows.value(w);
	 if ((cw && cw->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_INPUT)) ||
	     atom->getState(w) == ATOM(_NET_WM_STATE_ABOVE)) {
	     stacking_list.move(i, last_i);
	     if (!first_moved) first_moved = w;
	 } else ++i;
    }
    /* raise all non-transient notifications (transient ones were already
     * handled above) */
    first_moved = 0;
    for (int i = 0; i < last_i; ) {
	 Window w = stacking_list.at(i);
	 if (w == first_moved) break;
     	 DuiCompositeWindow *cw = windows.value(w);
	 if (cw && !cw->transientFor() &&
	     cw->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_NOTIFICATION)) {
	     stacking_list.move(i, last_i);
	     if (!first_moved) first_moved = w;
	 } else ++i;
    }

    /* check if the stacking order changed */
    if (prev_list != stacking_list) {
	/* fix Z-values */
        for (int i = 0; i <= last_i; ++i) {
            DuiCompositeWindow* witem = texturePixmapItem(stacking_list.at(i));
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
	// FIXME: handle errors
	XSync(QX11Info::display(), False);

#if 0
        /* Send synthetic visibility events. Note: obscuring is done
	 * elsewhere */
	if (compositing) {
	    int home_i = stacking_list.indexOf(duihome);
	    /* FIXME: don't send this to already visible windows */
	    for (int i = 0; i <= last_i; ++i) {
		 if (duihome && i > home_i)
		     send_window_obscured(stacking_list.at(i), false);
		 else if (!duihome)
		     send_window_obscured(stacking_list.at(i), false);
	    }
	}
#endif
    }
}

void DuiCompositeManagerPrivate::mapEvent(XMapEvent *e)
{
    Window win = e->window;
    Window transient_for = 0;

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
    if (atom->windowType(e->window) == DuiCompAtoms::DESKTOP) {
        stack[DESKTOP_LAYER] = e->window; // below topmost
    } else if (atom->windowType(e->window) == DuiCompAtoms::INPUT) {
        stack[INPUT_LAYER] = e->window; // topmost
    } else if (atom->windowType(e->window) == DuiCompAtoms::DOCK) {
        stack[DOCK_LAYER] = e->window; // topmost
    } else {
        if ((atom->windowType(e->window)    == DuiCompAtoms::FRAMELESS ||
                (atom->windowType(e->window)    == DuiCompAtoms::NORMAL))
                && !atom->isDecorator(e->window)
                && (parentWindow(win) == RootWindow(QX11Info::display(), 0))
                && (e->event == QX11Info::appRootWindow())) {
            hideLaunchIndicator();
            stack[APPLICATION_LAYER] = e->window; // between
            
            setExposeDesktop(false);
        }
    }

    // TODO: this should probably be done on the focus level. Rewrite this
    // once new stacking code from Kimmo is done
    int g_alpha = atom->globalAlphaFromWindow(win);
    if (g_alpha == 255) 
        toggle_global_alpha_blend(0);
    else if (g_alpha < 255) 
        toggle_global_alpha_blend(1);
    set_global_alpha(0, g_alpha);
    
    DuiCompositeWindow *item = texturePixmapItem(win);
    // Compositing is assumed to be enabled at this point if a window
    // has alpha channels
    if (!compositing && (item && item->hasAlpha())) {
        qWarning("mapEvent(): compositing not enabled!");
        return;
    }
    if (item) {
	item->setWindowTypeAtom(atom->getType(win));
        item->saveBackingStore(true);
        if (!item->hasAlpha()) {
            item->setVisible(true);
            item->updateWindowPixmap();
	    disableCompositing();
        } else {
            ((DuiTexturePixmapItem *)item)->enableRedirectedRendering();
            item->delayShow(100);
        }
        /* do this after bindWindow() so that the window is in
	 * stacking_list */
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
        if(transient_for)
            item->setWindowType(DuiCompositeWindow::Transient);
        else
            item->setWindowType(DuiCompositeWindow::Normal);
        if (!item->hasAlpha())
	    disableCompositing(true);
        else
            item->delayShow(500);
        
        // the current decorated window got mapped
        if (e->window == DuiDecoratorFrame::instance()->managedWindow() &&
            DuiDecoratorFrame::instance()->decoratorItem()) {
            connect(item, SIGNAL(visualized(bool)),
                    DuiDecoratorFrame::instance(),
                    SLOT(visualizeDecorator(bool)));
            DuiDecoratorFrame::instance()->decoratorItem()->setVisible(true);
            DuiDecoratorFrame::instance()->raise();
            DuiDecoratorFrame::instance()->decoratorItem()->setZValue(item->zValue()+1);
            stack[APPLICATION_LAYER] = e->window;
        }
        setWindowDebugProperties(win);
    }
    /* do this after bindWindow() so that the window is in stacking_list */
    activateWindow(win, CurrentTime, false);
}

void DuiCompositeManagerPrivate::rootMessageEvent(XClientMessageEvent *event)
{
    DuiCompositeWindow *i = texturePixmapItem(event->window);
    FrameData fd = framed_windows.value(event->window);

    if (event->message_type == ATOM(_NET_ACTIVE_WINDOW)) {
        got_active_window = true;

	//qDebug() << "_NET_ACTIVE_WINDOW for" << event->window;

        // Visibility notification to desktop window. Ensure this is sent
        // before transitions are started
	if (event->window != stack[DESKTOP_LAYER])
            setExposeDesktop(false);

        Window raise = event->window;
        DuiCompositeWindow *d_item = texturePixmapItem(stack[DESKTOP_LAYER]);
        bool needComp = false;
        if (d_item && d_item->isDirectRendered()) {
            needComp = true;
            enableCompositing(true);
        }
        if (i) {
            i->setZValue(windows.size() + 1);
            QRectF iconGeometry = atom->iconGeometry(raise);
            i->setPos(iconGeometry.topLeft());
            i->restore(iconGeometry, needComp);
            if(event->window != stack[DESKTOP_LAYER])
                i->startPing();
        }
        if (fd.frame)
            setWindowState(fd.frame->managedWindow(), NormalState);
        else
            setWindowState(event->window, NormalState);
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

        DuiCompositeWindow *check_hung = texturePixmapItem(close_window);
        if (check_hung) {
            if (check_hung->status() == DuiCompositeWindow::HUNG) {
                // destroy at the server level
                XKillClient(QX11Info::display(), close_window);
                delete check_hung;
                DuiDecoratorFrame::instance()->lower();
                removeWindow(close_window);
                return;
            }
        }
    } else if (event->message_type == ATOM(WM_PROTOCOLS)) {
        if (event->data.l[0] == (long) ATOM(_NET_WM_PING)) {
            DuiCompositeWindow *ping_source = texturePixmapItem(event->data.l[2]);
            if (ping_source) {
                ping_source->receivedPing(event->data.l[1]);
                Window managed = DuiDecoratorFrame::instance()->managedWindow();
                if (ping_source->window() == managed && !ping_source->needDecoration()) {
                    DuiDecoratorFrame::instance()->lower();
                    DuiDecoratorFrame::instance()->setManagedWindow(0);
                    if(!ping_source->hasAlpha()) 
                        disableCompositing(true);
                }
            }
        }
    } else if (event->message_type == ATOM(_NET_WM_STATE)) {
        if (event->data.l[1] == (long)  ATOM(_NET_WM_STATE_SKIP_TASKBAR))
            skiptaskbar_wm_state(event->data.l[0], event->window);
        else if(event->data.l[1] == (long) ATOM(_NET_WM_STATE_FULLSCREEN))
            fullscreen_wm_state(this, event->data.l[0], event->window);
    }
}

void DuiCompositeManagerPrivate::clientMessageEvent(XClientMessageEvent *event)
{
    // Handle iconify requests
    if (event->message_type == ATOM(WM_CHANGE_STATE))
        if (event->data.l[0] == IconicState && event->format == 32) {
            got_active_window = true;
            setWindowState(event->window, IconicState);
            
            DuiCompositeWindow *i = texturePixmapItem(event->window);
            DuiCompositeWindow *d_item = texturePixmapItem(stack[DESKTOP_LAYER]);
            if (d_item && i) {
                d_item->setZValue(i->zValue()-1);
            
                Window lower = event->window;
                setExposeDesktop(false);

                bool needComp = false;
                if(i->isDirectRendered()) {
                    enableCompositing();
                    needComp = true;
                }
                // Delayed transition is only available on platforms
                // which has selective comppositing. This is triggered
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

void DuiCompositeManagerPrivate::iconifyOnLower(DuiCompositeWindow *window)
{
    if(window->iconifyState() != DuiCompositeWindow::TransitionIconifyState)
        return;

    // TODO: (work for more)
    // Handle minimize request coming from a managed window itself,
    // if there are any
    FrameData fd = framed_windows.value(window->window());
    if (fd.frame) {
        setWindowState(fd.frame->managedWindow(), IconicState);
        DuiCompositeWindow *i = texturePixmapItem(fd.frame->winId());
        if (i)
            i->iconify();
    }

    enableCompositing();
    if (stack[DESKTOP_LAYER])
        positionWindow(stack[DESKTOP_LAYER], STACK_TOP);

    for (QHash<Window, DuiCompositeWindow *>::iterator it = windows.begin();
         it != windows.end(); ++it) {
        DuiCompositeWindow *i  = it.value();
        
        XVisibilityEvent c;
        c.type       = VisibilityNotify;
        c.send_event = True;
        c.window     = i->window();
        c.state      = VisibilityFullyObscured;
        XSendEvent(QX11Info::display(), i->window(), true,
                   VisibilityChangeMask, (XEvent *)&c);
    }
}

void DuiCompositeManagerPrivate::raiseOnRestore(DuiCompositeWindow *window)
{
    stack[APPLICATION_LAYER] = window->window();
    positionWindow(window->window(), STACK_TOP);
}

void DuiCompositeManagerPrivate::setExposeDesktop(bool exposed)
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
void DuiCompositeManagerPrivate::exposeDesktop()
{
    setExposeDesktop(true);
}

bool DuiCompositeManagerPrivate::isSelfManagedFocus(Window w)
{
    /* FIXME: store these to the object */
    XWindowAttributes attr;
    if (!XGetWindowAttributes(QX11Info::display(), w, &attr))
	return false;
    if (attr.override_redirect || atom->windowType(w) == DuiCompAtoms::INPUT)
        return false;

    DuiCompositeWindow *cw = windows.value(w);
    return cw && cw->wantsFocus();
}

void DuiCompositeManagerPrivate::activateWindow(Window w, Time timestamp,
		                                bool disableCompositing)
{
    if (DuiDecoratorFrame::instance()->managedWindow() == w)
        DuiDecoratorFrame::instance()->activate();
    got_active_window = false;
    
    if(disableCompositing) {
        DuiCompositeWindow* i = texturePixmapItem(w);
        if (i && !i->isDirectRendered() && !i->hasAlpha() && !i->needDecoration())
            QTimer::singleShot(100, this, SLOT(directRenderDesktop()));
    }
    
    if ((atom->windowType(w) != DuiCompAtoms::DESKTOP) &&
            (atom->windowType(w) != DuiCompAtoms::DOCK)) {
        stack[APPLICATION_LAYER] = w;
        setExposeDesktop(false);
        positionWindow(w, STACK_TOP);
    } else if (w == stack[DESKTOP_LAYER]) {
        setExposeDesktop(true);
        positionWindow(w, STACK_TOP);
    } else
        checkInputFocus(timestamp);
}

void DuiCompositeManagerPrivate::directRenderDesktop()
{    
    if (got_active_window)
        return;
    disableCompositing();
}

void DuiCompositeManagerPrivate::setWindowState(Window w, int state)
{
    CARD32 d[2];
    d[0] = state;
    d[1] = None;
    XChangeProperty(QX11Info::display(), w, ATOM(WM_STATE), ATOM(WM_STATE),
                    32, PropModeReplace, (unsigned char *)d, 2);
}

void DuiCompositeManagerPrivate::setWindowDebugProperties(Window w)
{
#ifdef WINDOW_DEBUG
    DuiCompositeWindow* i = texturePixmapItem(w);
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

bool DuiCompositeManagerPrivate::x11EventFilter(XEvent *event)
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

QGraphicsScene *DuiCompositeManagerPrivate::scene()
{
    return watch;
}

void DuiCompositeManagerPrivate::redirectWindows()
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
            bindWindow(kids[i]);
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
}

bool DuiCompositeManagerPrivate::isRedirected(Window w)
{
    return (texturePixmapItem(w) != 0);
}

DuiCompositeWindow *DuiCompositeManagerPrivate::texturePixmapItem(Window w)
{
    return windows.value(w, 0);
}

bool DuiCompositeManagerPrivate::removeWindow(Window w)
{
    windows_as_mapped.removeAll(w);
    if (windows.remove(w) == 0)
        return false;

    stacking_list.removeAll(w);

    for (int i = 0; i < TOTAL_LAYERS; ++i)
         if (stack[i] == w) stack[i] = 0;

    updateWinList();
    return true;
}

DuiCompositeWindow *DuiCompositeManagerPrivate::bindWindow(Window window)
{
    Display *display = QX11Info::display();
    bool is_decorator = atom->isDecorator(window);

    XSelectInput(display, window,  StructureNotifyMask | PropertyChangeMask | ButtonPressMask);
    XCompositeRedirectWindow(display, window, CompositeRedirectManual);

    DuiCompAtoms::Type wtype = atom->windowType(window);
    DuiTexturePixmapItem *item = new DuiTexturePixmapItem(window, glwidget);
    item->setZValue(wtype);
    item->saveState();
    windows[window] = item;

    if (!is_decorator && !item->isOverrideRedirect())
        windows_as_mapped.append(window);

    if (needDecoration(window)) {
        item->setDecorated(true);
        DuiDecoratorFrame::instance()->setManagedWindow(window);
    }

    item->updateWindowPixmap();
    if (!is_decorator) {
        stacking_list.append(window);
    }
    addItem(item);
    item->setWindowTypeAtom(atom->getType(window));

    if (wtype == DuiCompAtoms::INPUT) {
        item->updateWindowPixmap();
        return item;
    } else if (wtype == DuiCompAtoms::DESKTOP) {
        // just in case startup sequence changes
        stack[DESKTOP_LAYER] = window;
        connect(this, SIGNAL(inputEnabled()), item,
                SLOT(setUnBlurred()));
        return item;
    }

    item->manipulationEnabled(true);

    // the decorator got mapped. this is here because the compositor
    // could be restarted at any point
    if (is_decorator && !DuiDecoratorFrame::instance()->decoratorItem()) {
        // initially update the texture for this window because this
        // will be hidden on first show
        item->updateWindowPixmap();
        item->setVisible(false);
        DuiDecoratorFrame::instance()->setDecoratorItem(item);
    } else
        item->setVisible(true);

    XWMHints *h = XGetWMHints(display, window);
    if (h) {
        if ((h->flags & StateHint) && (h->initial_state == IconicState)) {
            setWindowState(window, IconicState);
        }
        XFree(h);
    }

    checkStacking();

    Atom *ping = 0;
    int   num;
    if (XGetWMProtocols(QX11Info::display(), window, &ping, &num)) {
        for (int i = 0; i < num; i++) {
            if (ping[i] == ATOM(_NET_WM_PING) && !is_decorator) {
                item->startPing();
                break;
            }
        }
        if (ping)
            XFree(ping);
    }

    return item;
}

void DuiCompositeManagerPrivate::addItem(DuiCompositeWindow *item)
{
    watch->addItem(item);
    updateWinList();
    setWindowDebugProperties(item->window());
    connect(item, SIGNAL(acceptingInput()), SLOT(enableInput()));
    
    if (atom->windowType(item->window()) == DuiCompAtoms::DESKTOP) 
        return;
    
    connect(item, SIGNAL(itemIconified(DuiCompositeWindow*)), SLOT(exposeDesktop()));
    connect(this, SIGNAL(compositingEnabled()), item, SLOT(startTransition()));
    connect(item, SIGNAL(itemRestored(DuiCompositeWindow*)), SLOT(raiseOnRestore(DuiCompositeWindow*)));
    connect(item, SIGNAL(itemIconified(DuiCompositeWindow*)), SLOT(iconifyOnLower(DuiCompositeWindow*)));
    
    // ping protocol
    connect(item, SIGNAL(windowHung(DuiCompositeWindow *)),
            SLOT(gotHungWindow(DuiCompositeWindow *)));
    
    connect(item, SIGNAL(pingTriggered(DuiCompositeWindow *)),
            SLOT(sendPing(DuiCompositeWindow *)));
}

void DuiCompositeManagerPrivate::updateWinList(bool stackingOnly)
{
    if (!stackingOnly) {
    	static QList<Window> prev_list;
	/* windows_as_mapped may contain invisible windows, so we need to
	 * make a new list without them */
	QList<Window> new_list;
        for (int i = 0; i < windows_as_mapped.size(); ++i) {
            Window w = windows_as_mapped.at(i);
            DuiCompositeWindow *d = windows.value(w);
            if (d->windowVisible()) new_list.append(w);
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
    checkStacking();
}

/*!
   Helper function to arrange arrange the order of the windows
   in the _NET_CLIENT_LIST_STACKING
*/
void
DuiCompositeManagerPrivate::positionWindow(Window w,
        DuiCompositeManagerPrivate::StackPosition pos)
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

void DuiCompositeManagerPrivate::enableCompositing(bool forced)
{
    if (compositing && !forced)
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

void DuiCompositeManagerPrivate::mapOverlayWindow()
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

void DuiCompositeManagerPrivate::enableRedirection()
{
    for (QHash<Window, DuiCompositeWindow *>::iterator it = windows.begin();
            it != windows.end(); ++it) {
        DuiCompositeWindow *tp  = it.value();
        if(tp->windowVisible())
            ((DuiTexturePixmapItem *)tp)->enableRedirectedRendering();

        // Hide if really not visible
        if (tp->isIconified())
            tp->hide();
        setWindowDebugProperties(it.key());
    }
    glwidget->update();
    compositing = true;

    scene()->views()[0]->setUpdatesEnabled(true);

    usleep(50000);
    // At this point everything should be rendered off-screen
    emit compositingEnabled();
}

void DuiCompositeManagerPrivate::disableCompositing(bool forced)
{
    if (DuiCompositeWindow::isTransitioning())
        return;
    if (!compositing && !forced)
        return;
    
    // we could still have exisisting decorator on-screen.
    // ensure we don't accidentally disturb it
    for (QHash<Window, DuiCompositeWindow *>::iterator it = windows.begin();
         it != windows.end(); ++it) {
        DuiCompositeWindow *i  = it.value();
        if (i->isDecorator())
            continue;
        if (i->windowVisible() && (i->hasAlpha() || i->needDecoration())) 
            return;
    }

    scene()->views()[0]->setUpdatesEnabled(false);

    for (QHash<Window, DuiCompositeWindow *>::iterator it = windows.begin();
            it != windows.end(); ++it) {
        DuiCompositeWindow *tp  = it.value();
        // checks above fail. somehow decorator got in. stop it at this point
        if (!tp->isDecorator() && !tp->isIconified() && !tp->hasAlpha())
            ((DuiTexturePixmapItem *)tp)->enableDirectFbRendering();
        setWindowDebugProperties(it.key());
    }

    XSync(QX11Info::display(), False);
    usleep(50000);

    XUnmapWindow(QX11Info::display(), xoverlay);
    XFlush(QX11Info::display());

    if (DuiDecoratorFrame::instance()->decoratorItem())
        DuiDecoratorFrame::instance()->lower();

    compositing = false;
}

void DuiCompositeManagerPrivate::sendPing(DuiCompositeWindow *w)
{
    Window window = ((DuiCompositeWindow *) w)->window();
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

void DuiCompositeManagerPrivate::gotHungWindow(DuiCompositeWindow *w)
{
    Window window = w->window();
    if (!DuiDecoratorFrame::instance()->decoratorItem())
        return;
    
    enableCompositing(true);

    // own the window so we could kill it if we want to.
    // raise the items manually because at this time it is no longer
    // responding to any X messages
    DuiDecoratorFrame::instance()->setManagedWindow(window);
    DuiDecoratorFrame::instance()->raise();
    checkStacking();
    DuiCompositeWindow *p = DuiDecoratorFrame::instance()->decoratorItem();
    int z = atom->windowType(window);
    w->setZValue(z);
    p->setZValue(z);
#if (QT_VERSION >= 0x040600)
    w->stackBefore(p);
#endif
    
    w->updateWindowPixmap();
}

void DuiCompositeManagerPrivate::showLaunchIndicator(int timeout)
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

void DuiCompositeManagerPrivate::hideLaunchIndicator()
{
    if (launchIndicator)
        launchIndicator->hide();
}

DuiCompositeManager::DuiCompositeManager(int &argc, char **argv)
    : QApplication(argc, argv)
{
    if (QX11Info::isCompositingManagerRunning()) {
        qCritical("Compositing manager already running.");
        ::exit(0);
    }

    d = new DuiCompositeManagerPrivate(this);
    DuiRmiServer *s = new DuiRmiServer(".duicompositor", this);
    s->exportObject(this);
}

DuiCompositeManager::~DuiCompositeManager()
{
    delete d;
    d = 0;
}

void DuiCompositeManager::setGLWidget(QGLWidget *glw)
{
    glw->setAttribute(Qt::WA_PaintOutsidePaintEvent);
    d->glwidget = glw;
}

QGraphicsScene *DuiCompositeManager::scene()
{
    return d->scene();
}

void DuiCompositeManager::prepareEvents()
{
    d->prepare();
}

bool DuiCompositeManager::x11EventFilter(XEvent *event)
{
    return d->x11EventFilter(event);
}

void DuiCompositeManager::setSurfaceWindow(Qt::HANDLE window)
{
    d->localwin = window;
}

void DuiCompositeManager::redirectWindows()
{
    d->redirectWindows();
}

bool DuiCompositeManager::isRedirected(Qt::HANDLE w)
{
    return d->isRedirected(w);
}

void DuiCompositeManager::enableCompositing()
{
    d->enableCompositing();
}

void DuiCompositeManager::disableCompositing()
{
    d->disableCompositing();
}

void DuiCompositeManager::showLaunchIndicator(int timeout)
{
    d->showLaunchIndicator(timeout);
}

void DuiCompositeManager::hideLaunchIndicator()
{
    d->hideLaunchIndicator();
}

void DuiCompositeManager::topmostWindowsRaise()
{
    d->checkStacking();
}
