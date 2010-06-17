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
#include "mcompatoms_p.h"

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
#define MODAL_WINDOW(X) \
        ((X)->netWmState().indexOf(ATOM(_NET_WM_STATE_MODAL)) != -1)

Atom MCompAtoms::atoms[MCompAtoms::ATOMS_TOTAL];
Window MCompositeManagerPrivate::stack[TOTAL_LAYERS];
MCompAtoms *MCompAtoms::d = 0;
static bool hasDock  = false;
static QRect availScreenRect = QRect();

// temporary launch indicator. will get replaced later
static QGraphicsTextItem *launchIndicator = 0;

static Window transient_for(Window window);

#ifdef WINDOW_DEBUG
static QTime overhead_measure;
#endif

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
        "WM_TRANSIENT_FOR",
        "WM_HINTS",

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
        "_NET_WM_WINDOW_TYPE_MENU",

        // window states
        "_NET_WM_STATE_ABOVE",
        "_NET_WM_STATE_SKIP_TASKBAR",
        "_NET_WM_STATE_FULLSCREEN",
        "_NET_WM_STATE_MODAL",
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
        "_MEEGOTOUCH_GLOBAL_ALPHA",
        "_MEEGO_STACKING_LAYER",

#ifdef WINDOW_DEBUG
        // custom properties for CITA
        "_M_WM_INFO",
        "_M_WM_WINDOW_ZVALUE",
        "_M_WM_WINDOW_COMPOSITED_VISIBLE",
        "_M_WM_WINDOW_COMPOSITED_INVISIBLE",
        "_M_WM_WINDOW_DIRECT_VISIBLE",
        "_M_WM_WINDOW_DIRECT_INVISIBLE",
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

MCompAtoms::Type MCompAtoms::windowType(Window w)
{
    // freedesktop.org window type
    QVector<Atom> a = getAtomArray(w, atoms[_NET_WM_WINDOW_TYPE]);
    if (!a.size())
        return NORMAL;
    if (a[0] == atoms[_NET_WM_WINDOW_TYPE_DESKTOP])
        return DESKTOP;
    else if (a[0] == atoms[_NET_WM_WINDOW_TYPE_NORMAL])
        return NORMAL;
    else if (a[0] == atoms[_NET_WM_WINDOW_TYPE_DIALOG]) {
        if (a.indexOf(atoms[_KDE_NET_WM_WINDOW_TYPE_OVERRIDE]) != -1)
            return NO_DECOR_DIALOG;
        else
            return DIALOG;
    }
    else if (a[0] == atoms[_NET_WM_WINDOW_TYPE_DOCK])
        return DOCK;
    else if (a[0] == atoms[_NET_WM_WINDOW_TYPE_INPUT])
        return INPUT;
    else if (a[0] == atoms[_NET_WM_WINDOW_TYPE_NOTIFICATION])
        return NOTIFICATION;
    else if (a[0] == atoms[_KDE_NET_WM_WINDOW_TYPE_OVERRIDE] ||
             a[0] == atoms[_NET_WM_WINDOW_TYPE_MENU])
        return FRAMELESS;

    if (transient_for(w))
        return UNKNOWN;
    else // fdo spec suggests unknown non-transients must be normal
        return NORMAL;
}

bool MCompAtoms::isDecorator(Window w)
{
    return (cardValueProperty(w, atoms[_MEEGOTOUCH_DECORATOR_WINDOW]) == 1);
}

// Remove this when statusbar in-scene approach is done
bool MCompAtoms::statusBarOverlayed(Window w)
{
    return (cardValueProperty(w, atoms[_DUI_STATUSBAR_OVERLAY]) == 1);
}

int MCompAtoms::getPid(Window w)
{
    return cardValueProperty(w, atoms[_NET_WM_PID]);
}

bool MCompAtoms::hasState(Window w, Atom a)
{
    QVector<Atom> states = getAtomArray(w, atoms[_NET_WM_STATE]);
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

QVector<Atom> MCompAtoms::getAtomArray(Window w, Atom array_atom)
{
    QVector<Atom> ret;

    Atom actual;
    int format;
    unsigned long n, left;
    unsigned char *data;
    int result = XGetWindowProperty(QX11Info::display(), w, array_atom, 0, 0,
                                    False, XA_ATOM, &actual, &format,
                                    &n, &left, &data);
    if (result == Success && actual == XA_ATOM && format == 32) {
        ret.resize(left / 4);
        XFree((void *) data);

        if (XGetWindowProperty(QX11Info::display(), w, array_atom, 0,
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

    unsigned char *data = 0;
    int result = XGetWindowProperty(QX11Info::display(), w, atoms[_MEEGOTOUCH_GLOBAL_ALPHA], 0L, 1L, False,
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

int MCompAtoms::cardValueProperty(Window w, Atom property)
{
    Atom actual;
    int format;
    unsigned long n, left;
    unsigned char *data = 0;

    int result = XGetWindowProperty(QX11Info::display(), w, property, 0L, 1L, False,
                                    XA_CARDINAL, &actual, &format,
                                    &n, &left, &data);
    if (result == Success && data) {
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
    if (transient_for == window)
        transient_for = 0;
    return transient_for;
}

static void skiptaskbar_wm_state(int toggle, Window window)
{
    Atom skip = ATOM(_NET_WM_STATE_SKIP_TASKBAR);
    MCompAtoms *atom = MCompAtoms::instance();
    QVector<Atom> states = atom->getAtomArray(window, ATOM(_NET_WM_STATE));
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
    QVector<Atom> states = atom->getAtomArray(window, ATOM(_NET_WM_STATE));
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

static Bool map_predicate(Display *display, XEvent *xevent, XPointer arg)
{
    Q_UNUSED(display);
    Window window = (Window)arg;
    if (xevent->type == MapNotify && xevent->xmap.window == window)
        return True;
    return False;
}

MCompositeManagerPrivate::MCompositeManagerPrivate(QObject *p)
    : QObject(p),
      glwidget(0),
      compositing(true)
{
    watch = new MCompositeScene(this);
    atom = MCompAtoms::instance();

    device_state = new MDeviceState(this);
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
    MDecoratorFrame::instance();
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
    if (device_state->ongoingCall() && fs && ((cw &&
        cw->windowTypeAtom() != ATOM(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE) &&
        cw->windowTypeAtom() != ATOM(_NET_WM_WINDOW_TYPE_MENU)) ||
        (!cw && atom->windowType(window) != MCompAtoms::FRAMELESS)))
        // fullscreen window is decorated during call
        return true;
    if (fs)
        return false;
    bool transient;
    if (!cw)
        transient = transient_for(window);
    else
        transient = (getLastVisibleParent(cw) ? true : false);
    if (!cw && atom->isDecorator(window))
        return false;
    else if (cw && (cw->isDecorator() || cw->isOverrideRedirect()))
        return false;
    else if (!cw) {
        XWindowAttributes a;
        if (!XGetWindowAttributes(QX11Info::display(), window, &a)
            || a.override_redirect)
            return false;
    }
    MCompAtoms::Type t = atom->windowType(window);
    return (t != MCompAtoms::FRAMELESS
            && t != MCompAtoms::DESKTOP
            && t != MCompAtoms::NOTIFICATION
            && t != MCompAtoms::INPUT
            && t != MCompAtoms::DOCK
            && t != MCompAtoms::NO_DECOR_DIALOG
            && !transient);
}

void MCompositeManagerPrivate::damageEvent(XDamageNotifyEvent *e)
{
    if (device_state->displayOff())
        return;
    XserverRegion r = XFixesCreateRegion(QX11Info::display(), 0, 0);
    int num;
    XDamageSubtract(QX11Info::display(), e->damage, None, r);

    XRectangle *rects = XFixesFetchRegion(QX11Info::display(), r, &num);
    XFixesDestroyRegion(QX11Info::display(), r);

    MCompositeWindow *item = COMPOSITE_WINDOW(e->drawable);
    if (item && rects)
        item->updateWindowPixmap(rects, num);

    if (rects)
        XFree(rects);
}

void MCompositeManagerPrivate::destroyEvent(XDestroyWindowEvent *e)
{
    if (configure_reqs.contains(e->window)) {
        QList<XConfigureRequestEvent*> l = configure_reqs.value(e->window);
        while (!l.isEmpty()) {
            XConfigureRequestEvent *p = l.takeFirst();
            free(p);
        }
        configure_reqs.remove(e->window);
    }

    MCompositeWindow *item = COMPOSITE_WINDOW(e->window);
    if (item) {
        scene()->removeItem(item);
        item->deleteLater();
        if (!removeWindow(e->window))
            qWarning("destroyEvent(): Error removing window");
        glwidget->update();
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

void MCompositeManagerPrivate::propertyEvent(XPropertyEvent *e)
{
    MCompositeWindow *cw = COMPOSITE_WINDOW(e->window);
    if (cw && cw->propertyEvent(e))
        checkStacking(false);
}

// -1: cw_a is cw_b's ancestor; 1: cw_b is cw_a's ancestor; 0: no relation
int MCompositeManagerPrivate::transiencyRelation(MCompositeWindow *cw_a,
                                                 MCompositeWindow *cw_b)
{
    Window parent;
    MCompositeWindow *tmp, *cw_p;
    for (tmp = cw_b; (parent = tmp->transientFor())
                     && (cw_p = COMPOSITE_WINDOW(parent)); tmp = cw_p)
       if (cw_p == cw_a)
           return -1;
    for (tmp = cw_a; (parent = tmp->transientFor())
                     && (cw_p = COMPOSITE_WINDOW(parent)); tmp = cw_p)
       if (cw_p == cw_b)
           return 1;
    return 0;
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

bool MCompositeManagerPrivate::isAppWindow(MCompositeWindow *cw,
                                           bool include_transients)
{
    if (!include_transients && cw && getLastVisibleParent(cw))
        return false;
    if (cw && !cw->isOverrideRedirect() &&
            (cw->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_NORMAL) ||
             cw->windowTypeAtom() == ATOM(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE) ||
             /* non-modal, non-transient dialogs behave like applications */
             (cw->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_DIALOG) &&
              !MODAL_WINDOW(cw)))
            && !cw->isDecorator())
        return true;
    return false;
}

Window MCompositeManagerPrivate::getTopmostApp(int *index_in_stacking_list,
                                               Window ignore_window)
{
    for (int i = stacking_list.size() - 1; i >= 0; --i) {
        Window w = stacking_list.at(i);
        if (w == ignore_window) continue;
        if (w == stack[DESKTOP_LAYER])
            /* desktop is above all applications */
            return 0;
        MCompositeWindow *cw = COMPOSITE_WINDOW(w);
        if (cw && cw->isMapped() && isAppWindow(cw) &&
            cw->iconifyState() == MCompositeWindow::NoIconifyState &&
            cw->windowState() == NormalState && !cw->isTransitioning()) {
            if (index_in_stacking_list)
                *index_in_stacking_list = i;
            return w;
        }
    }
    return 0;
}

MCompositeWindow *MCompositeManagerPrivate::getHighestDecorated()
{
    for (int i = stacking_list.size() - 1; i >= 0; --i) {
        Window w = stacking_list.at(i);
        if (w == stack[DESKTOP_LAYER])
            return 0;
        MCompositeWindow *cw = COMPOSITE_WINDOW(w);
        if (cw && cw->isMapped() && !cw->isOverrideRedirect() &&
            (cw->needDecoration() || cw->status() == MCompositeWindow::HUNG
             || (FULLSCREEN_WINDOW(cw) &&
                 cw->windowTypeAtom() != ATOM(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE)
                 && cw->windowTypeAtom() != ATOM(_NET_WM_WINDOW_TYPE_MENU)
                 && device_state->ongoingCall())))
            return cw;
    }
    return 0;
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
        if (cw->isMapped() && isAppWindow(cw, true)) {
            top = w;
            win_i = i;
            break;
        }
    }
    if (top && cw && !MCompositeWindow::isTransitioning()) {
        // unredirect the chosen window and any docks and OR windows above it
        // TODO: what else should be unredirected?
        ((MTexturePixmapItem *)cw)->enableDirectFbRendering();
        setWindowDebugProperties(top);
        for (int i = win_i + 1; i < stacking_list.size(); ++i) {
            Window w = stacking_list.at(i);
            if ((cw = COMPOSITE_WINDOW(w)) && cw->isMapped() &&
                (cw->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_DOCK)
                 || cw->isOverrideRedirect())) {
                ((MTexturePixmapItem *)cw)->enableDirectFbRendering();
                setWindowDebugProperties(w);
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
    if (configure_reqs.contains(e->window)) {
        QList<XConfigureRequestEvent*> l = configure_reqs.value(e->window);
        while (!l.isEmpty()) {
            XConfigureRequestEvent *p = l.takeFirst();
            free(p);
        }
        configure_reqs.remove(e->window);
    }

    // do not keep unmapped windows in windows_as_mapped list
    windows_as_mapped.removeAll(e->window);

    if (e->window == xoverlay)
        return;

#ifdef GLES2_VERSION
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
#endif

    MCompositeWindow *item = COMPOSITE_WINDOW(e->window);
    bool call_updateWinList = true;
    if (item) {
        item->setIsMapped(false);
        setWindowState(e->window, IconicState);
        if (item->isVisible()) {
            item->setVisible(false);
            item->clearTexture();
            glwidget->update();
        }
        if (MDecoratorFrame::instance()->managedWindow() == e->window) {
            MDecoratorFrame::instance()->lower();
            MDecoratorFrame::instance()->setManagedWindow(0);
            positionWindow(MDecoratorFrame::instance()->winId(), STACK_BOTTOM);
            call_updateWinList = false;
        }
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
    if (call_updateWinList)
        updateWinList();

    for (int i = 0; i < TOTAL_LAYERS; ++i)
        if (stack[i] == e->window) stack[i] = 0;

#ifdef GLES2_VERSION
    if (topmost_win == e->window) {
        toggle_global_alpha_blend(0);
        set_global_alpha(0, 255);
    }
#endif
    if (!possiblyUnredirectTopmostWindow())
        enableCompositing();
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
            if (item->needDecoration() &&
                MDecoratorFrame::instance()->decoratorItem() &&
                MDecoratorFrame::instance()->managedWindow() == e->window) {
                if (FULLSCREEN_WINDOW(item) &&
                    item->status() != MCompositeWindow::HUNG) {
                    // ongoing call case
                    MDecoratorFrame::instance()->setManagedWindow(item, true);
                    MDecoratorFrame::instance()->setOnlyStatusbar(true);
                } else {
                    MDecoratorFrame::instance()->setManagedWindow(item);
                    MDecoratorFrame::instance()->setOnlyStatusbar(false);
                }
                MDecoratorFrame::instance()->decoratorItem()->setVisible(true);
                MDecoratorFrame::instance()->raise();
                item->update();
                checkStacking(false);
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

// used to handle ConfigureRequest when we have the object for the window
void MCompositeManagerPrivate::configureWindow(MCompositeWindow *cw,
                                               XConfigureRequestEvent *e)
{
    if (e->value_mask & (CWX | CWY | CWWidth | CWHeight)) {
        if (FULLSCREEN_WINDOW(cw))
            // do not allow resizing of fullscreen window
            e->value_mask &= ~(CWX | CWY | CWWidth | CWHeight);
        else {
            QRect r = cw->requestedGeometry();
            if (e->value_mask & CWX)
                r.setX(e->x);
            if (e->value_mask & CWY)
                r.setY(e->y);
            if (e->value_mask & CWWidth)
                r.setWidth(e->width);
            if (e->value_mask & CWHeight)
                r.setHeight(e->height);
            cw->setRequestedGeometry(r);
        }
    }

    /* modify stacking_list if stacking order should be changed */
    int win_i = stacking_list.indexOf(e->window);
    if (win_i >= 0 && e->detail == Above && (e->value_mask & CWStackMode)) {
        if (e->value_mask & CWSibling) {
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
    } else if (win_i >= 0 && e->detail == Below
               && (e->value_mask & CWStackMode)) {
        if (e->value_mask & CWSibling) {
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

    /* Resize and/or reposition the X window if requested. Stacking changes
     * were done above. */
    unsigned int value_mask = e->value_mask & ~(CWSibling | CWStackMode);
    if (value_mask) {
        XWindowChanges wc;
        wc.border_width = e->border_width;
        wc.x = e->x;
        wc.y = e->y;
        wc.width = e->width;
        wc.height = e->height;
        wc.sibling =  e->above;
        wc.stack_mode = e->detail;
        XConfigureWindow(QX11Info::display(), e->window, value_mask, &wc);
    }
}

void MCompositeManagerPrivate::configureRequestEvent(XConfigureRequestEvent *e)
{
    if (e->parent != RootWindow(QX11Info::display(), 0))
        return;

    // sandbox these windows. we own them
    if (atom->isDecorator(e->window))
        return;

    /*qDebug() << __func__ << "CONFIGURE REQUEST FOR:" << e->window
        << e->x << e->y << e->width << e->height << "above/mode:"
        << e->above << e->detail;*/

    MCompositeWindow *i = COMPOSITE_WINDOW(e->window);
    if (i && i->isMapped())
        configureWindow(i, e);
    else {
        // resize/reposition before it is mapped
        unsigned int value_mask = e->value_mask & ~(CWSibling | CWStackMode);
        if (value_mask) {
            XWindowChanges wc;
            wc.border_width = e->border_width;
            wc.x = e->x;
            wc.y = e->y;
            wc.width = e->width;
            wc.height = e->height;
            wc.sibling =  e->above;
            wc.stack_mode = e->detail;
            XConfigureWindow(QX11Info::display(), e->window, value_mask, &wc);
        }
        // store configure request for handling it at window mapping time
        QList<XConfigureRequestEvent*> l = configure_reqs.value(e->window);
        XConfigureRequestEvent *e_copy =
                (XConfigureRequestEvent*)malloc(sizeof(*e));
        memcpy(e_copy, e, sizeof(*e));
        l.append(e_copy);
        configure_reqs[e->window] = l;
    }

    MCompAtoms::Type wtype = atom->windowType(e->window);
    if (i && (e->detail == Above) && (e->above == None) && i->isMapped() &&
        (wtype != MCompAtoms::INPUT) && (wtype != MCompAtoms::DOCK)) {
        setWindowState(e->window, NormalState);
        setExposeDesktop(false);

        // selective compositing support:
        // since we call disable compositing immediately
        // we don't see the animated transition
        if (!i->hasAlpha() && !i->needDecoration()) {
            i->setIconified(false);        
            disableCompositing(FORCED);
        } else if (MDecoratorFrame::instance()->managedWindow() == e->window)
            enableCompositing();
    }
}

void MCompositeManagerPrivate::mapRequestEvent(XMapRequestEvent *e)
{
    XWindowAttributes a;
    Display *dpy  = QX11Info::display();
    bool hasAlpha = false;
    MCompAtoms::Type wtype;

    if (!XGetWindowAttributes(QX11Info::display(), e->window, &a))
        return;
    wtype = atom->windowType(e->window);
    if (!hasDock) {
        hasDock = (wtype == MCompAtoms::DOCK);
        if (hasDock)
            dock_region = QRegion(a.x, a.y, a.width, a.height);
    }
    int xres = ScreenOfDisplay(dpy, DefaultScreen(dpy))->width;
    int yres = ScreenOfDisplay(dpy, DefaultScreen(dpy))->height;

    if (wtype == MCompAtoms::FRAMELESS || wtype == MCompAtoms::DESKTOP
        || wtype == MCompAtoms::INPUT) {
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
        MDecoratorFrame::instance()->setManagedWindow(0);
        MCompositeWindow *cw;
        if ((cw = getHighestDecorated())) {
            if (cw->status() == MCompositeWindow::HUNG) {
                MDecoratorFrame::instance()->setManagedWindow(cw, true);
            } else if (FULLSCREEN_WINDOW(cw) && device_state->ongoingCall()) {
                MDecoratorFrame::instance()->setManagedWindow(cw, true);
                MDecoratorFrame::instance()->setOnlyStatusbar(true);
            } else
                MDecoratorFrame::instance()->setManagedWindow(cw);
        }
        return;
    }

    XRenderPictFormat *format = XRenderFindVisualFormat(QX11Info::display(),
                                a.visual);
    if (format && (format->type == PictTypeDirect && format->direct.alphaMask)) {
#ifdef WINDOW_DEBUG
        overhead_measure.start();
#endif
        enableCompositing();
        
        hasAlpha = true;
    }

    if (atom->hasState(e->window, ATOM(_NET_WM_STATE_FULLSCREEN)))
        fullscreen_wm_state(this, 1, e->window);

    if (needDecoration(e->window)) {
        XAddToSaveSet(QX11Info::display(), e->window);

        if (MDecoratorFrame::instance()->decoratorItem()) {
            enableCompositing();
            XMapWindow(QX11Info::display(), e->window);
            // initially visualize decorator item so selective compositing
            // checks won't disable compositing
            MDecoratorFrame::instance()->decoratorItem()->setVisible(true);
        } else {
            // it will be non-toplevel, so mask needs to be set here
            XSelectInput(dpy, e->window,
                         StructureNotifyMask | ColormapChangeMask |
                         PropertyChangeMask);
            MSimpleWindowFrame *frame = 0;
            FrameData f = framed_windows.value(e->window);
            frame = f.frame;
            if (!frame) {
                frame = new MSimpleWindowFrame(e->window);
                Window trans = transient_for(e->window);
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
        if (!cw || !cw->isMapped() || !cw->wantsFocus() || cw->isDecorator()
            || cw->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_DOCK))
            continue;
        if (!cw->isOverrideRedirect() &&
            cw->windowTypeAtom() != ATOM(_NET_WM_WINDOW_TYPE_INPUT)) {
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

#define RAISE_MATCHING(X) { \
    first_moved = 0; \
    for (int i = 0; i < last_i;) { \
        Window w = stacking_list.at(i); \
        if (w == first_moved) break; \
        MCompositeWindow *cw = COMPOSITE_WINDOW(w); \
        if (cw && (X)) { \
            stacking_list.move(i, last_i); \
	    raise_transients(this, w, last_i); \
            if (!first_moved) first_moved = w; \
        } else ++i; \
    } }

/* Go through stacking_list and verify that it is in order.
 * If it isn't, reorder it and call XRestackWindows.
 * NOTE: stacking_list needs to be reversed before giving it to
 * XRestackWindows.*/
void MCompositeManagerPrivate::checkStacking(bool force_visibility_check,
                                               Time timestamp)
{
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
	/* raise application windows belonging to the same group */
	XID group;
	if ((group = aw->windowGroup())) {
	    for (int i = 0; i < app_i; ) {
	         MCompositeWindow *cw = COMPOSITE_WINDOW(stacking_list.at(i));
		 if (cw->windowState() == NormalState
                     && isAppWindow(cw) && cw->windowGroup() == group) {
	             stacking_list.move(i, last_i);
	             /* active_app was moved, update the index */
	             app_i = stacking_list.indexOf(active_app);
		     /* TODO: transients */
		 } else ++i;
	    }
	}
	stacking_list.move(app_i, last_i);
	/* raise transients recursively */
	raise_transients(this, active_app, last_i);
    } else if (duihome) {
        //qDebug() << "raising home window" << duihome;
        stacking_list.move(stacking_list.indexOf(duihome), last_i);
    }

    /* raise docks if either the desktop is up or the application is
     * non-fullscreen */
    if (desktop_up || !active_app || app_i < 0 || !aw || !fs_app)
        RAISE_MATCHING(!getLastVisibleParent(cw) &&
                       cw->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_DOCK))
    else if (active_app && aw && deco->decoratorItem() &&
               deco->managedWindow() == active_app &&
               (fs_app || aw->status() == MCompositeWindow::HUNG)) {
        // no dock => decorator starts from (0,0)
        XMoveWindow(QX11Info::display(), deco->decoratorItem()->window(), 0, 0);
    }
    /* Meego layers 1-3: lock screen, ongoing call etc. */
    for (unsigned int level = 1; level < 4; ++level)
         RAISE_MATCHING(!getLastVisibleParent(cw) &&
                        cw->iconifyState() == MCompositeWindow::NoIconifyState
                        && cw->meegoStackingLayer() == level)
    /* raise all system-modal dialogs */
    RAISE_MATCHING(!getLastVisibleParent(cw) && MODAL_WINDOW(cw) &&
                   cw->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_DIALOG))
    /* raise all keep-above flagged, input methods and Meego layer 4
     * (incoming call), at the same time preserving their mapping order */
    RAISE_MATCHING(!getLastVisibleParent(cw) && !cw->isDecorator() &&
                   cw->iconifyState() == MCompositeWindow::NoIconifyState &&
                   (cw->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_INPUT) ||
                    cw->meegoStackingLayer() == 4 || cw->isOverrideRedirect() ||
                    cw->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_MENU) ||
                    cw->netWmState().indexOf(ATOM(_NET_WM_STATE_ABOVE)) != -1))
    // Meego layer 5
    RAISE_MATCHING(!getLastVisibleParent(cw) && cw->meegoStackingLayer() == 5
                   && cw->iconifyState() == MCompositeWindow::NoIconifyState)
    /* raise all non-transient notifications (transient ones were already
     * handled above) */
    RAISE_MATCHING(!getLastVisibleParent(cw) &&
        cw->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_NOTIFICATION))
    // Meego layer 6
    RAISE_MATCHING(!getLastVisibleParent(cw) && cw->meegoStackingLayer() == 6
                   && cw->iconifyState() == MCompositeWindow::NoIconifyState)

    MCompositeWindow *topmost = 0, *highest_d = getHighestDecorated();
    int top_i = -1;
    // find out highest application window
    for (int i = stacking_list.size() - 1; i >= 0; --i) {
         MCompositeWindow *cw;
         Window w = stacking_list.at(i);
         if (w == stack[DESKTOP_LAYER])
             break;
         if (!(cw = COMPOSITE_WINDOW(w)))
             continue;
         if (cw->isMapped() && isAppWindow(cw, true)) {
             topmost = cw;
             top_i = i;
             break;
         }
    }
    /* raise decorator */
    if (highest_d && highest_d == topmost && deco->decoratorItem()
        && deco->managedWindow() == highest_d->window()
        && (!FULLSCREEN_WINDOW(highest_d)
            || highest_d->status() == MCompositeWindow::HUNG
            || device_state->ongoingCall())) {
        Window deco_w = deco->decoratorItem()->window();
        int deco_i = stacking_list.indexOf(deco_w);
        if (deco_i >= 0) {
            if (deco_i < top_i)
                stacking_list.move(deco_i, top_i);
            else
                stacking_list.move(deco_i, top_i + 1);
            if (!compositing)
                // decor requires compositing
                enableCompositing(true);
            MCompositeWindow *cw = COMPOSITE_WINDOW(deco_w);
            cw->updateWindowPixmap();
            cw->setVisible(true);
        }
    }

    // properties and focus are updated only if there was a change in the
    // order of mapped windows or mappedness FIXME: would make sense to
    // stack unmapped windows to the bottom of the stack to avoid them
    // "flashing" before we had the chance to stack them
    QList<Window> only_mapped;
    for (int i = 0; i <= last_i; ++i) {
         MCompositeWindow *witem = COMPOSITE_WINDOW(stacking_list.at(i));
         if (witem && witem->isMapped() && !witem->isOverrideRedirect())
             only_mapped.append(stacking_list.at(i));
    }
    static QList<Window> prev_only_mapped;
    bool order_changed = prev_only_mapped != only_mapped;
    if (order_changed) {
        /* fix Z-values */
        for (int i = 0; i <= last_i; ++i) {
            MCompositeWindow *witem = COMPOSITE_WINDOW(stacking_list.at(i));
            if (witem && witem->isTransitioning())
                // don't change Z values until animation is over
                break;
            if (witem)
                witem->requestZValue(i);
        }

        QList<Window> reverse;
        for (int i = last_i; i >= 0; --i)
            reverse.append(stacking_list.at(i));
        //qDebug() << __func__ << "stack:" << reverse.toVector();

        XRestackWindows(QX11Info::display(), reverse.toVector().data(),
                        reverse.size());

        // decorator is not included to the property
        QList<Window> no_decors = only_mapped;
        for (int i = 0; i <= last_i; ++i) {
             MCompositeWindow *witem = COMPOSITE_WINDOW(stacking_list.at(i));
             if (witem && witem->isMapped() && witem->isDecorator())
                 no_decors.removeOne(stacking_list.at(i));
        }
        XChangeProperty(QX11Info::display(),
                        RootWindow(QX11Info::display(), 0),
                        ATOM(_NET_CLIENT_LIST_STACKING),
                        XA_WINDOW, 32, PropModeReplace,
                        (unsigned char *)no_decors.toVector().data(),
                        no_decors.size());
        prev_only_mapped = QList<Window>(only_mapped);

        checkInputFocus(timestamp);
        XSync(QX11Info::display(), False);
    }
    if (order_changed || force_visibility_check) {
        /* Send synthetic visibility events for our babies */
        int home_i = stacking_list.indexOf(duihome);
        for (int i = 0; i <= last_i; ++i) {
            MCompositeWindow *cw = COMPOSITE_WINDOW(stacking_list.at(i));
            if (!cw || !cw->isMapped()) continue;
            if (device_state->displayOff()) {
                cw->setWindowObscured(true);
                if (cw->window() != duihome)
                    cw->setVisible(false);
                continue;
            }
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
    MCompAtoms::Type wtype;

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

    wtype = atom->windowType(e->window);
    // simple stacking model legacy code...
    if (wtype == MCompAtoms::DESKTOP) {
        stack[DESKTOP_LAYER] = e->window; // below topmost
    } else if (wtype == MCompAtoms::INPUT) {
        stack[INPUT_LAYER] = e->window; // topmost
    } else if (wtype == MCompAtoms::DOCK) {
        stack[DOCK_LAYER] = e->window; // topmost
    } else {
        if ((wtype == MCompAtoms::FRAMELESS || wtype == MCompAtoms::NORMAL)
                && !atom->isDecorator(e->window)
                && (parentWindow(win) == RootWindow(QX11Info::display(), 0))
                && (e->event == QX11Info::appRootWindow())) {
            hideLaunchIndicator();

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
    if (item) {
        item->setIsMapped(true);
        if (windows_as_mapped.indexOf(win) == -1)
            windows_as_mapped.append(win);
    }
    // Compositing is assumed to be enabled at this point if a window
    // has alpha channels
    if (!compositing && (item && item->hasAlpha())) {
        qWarning("mapEvent(): compositing not enabled!");
        return;
    }
    bool check_compositing = false;
    if (item) {
        if (wtype == MCompAtoms::NORMAL)
            item->setWindowTypeAtom(ATOM(_NET_WM_WINDOW_TYPE_NORMAL));
        else
            item->setWindowTypeAtom(atom->getType(win));
        
        if (!device_state->displayOff() ) {
            
            MCompositeWindow* tf = COMPOSITE_WINDOW(item->transientFor());
            if (!item->hasAlpha() && !item->needDecoration() && (tf && !tf->needDecoration())) {
                item->setVisible(true);
                item->updateWindowPixmap();
                check_compositing = true;
            } else {
#ifdef WINDOW_DEBUG
                qDebug() << "Composition overhead (existing pixmap):" 
                         << overhead_measure.elapsed();
#endif
                if (((MTexturePixmapItem *)item)->isDirectRendered())
                    ((MTexturePixmapItem *)item)->enableRedirectedRendering();
                else
                    item->saveBackingStore(true);
                item->setVisible(true);
            }
        }
        goto stack_and_return;
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
        if (!item)
            return;
        if (!item->hasAlpha())
            check_compositing = true;
        else {
#ifdef WINDOW_DEBUG
            if (item->hasAlpha())
                qDebug() << "Composition overhead (new pixmap):"
                         << overhead_measure.elapsed();
#endif
            item->setVisible(true);
        }

        // the current decorated window got mapped
        if (e->window == MDecoratorFrame::instance()->managedWindow() &&
                MDecoratorFrame::instance()->decoratorItem()) {
            connect(item, SIGNAL(visualized(bool)),
                    MDecoratorFrame::instance(),
                    SLOT(visualizeDecorator(bool)));
            MDecoratorFrame::instance()->decoratorItem()->setVisible(true);
            MDecoratorFrame::instance()->raise();
            MDecoratorFrame::instance()->decoratorItem()->setZValue(item->zValue() + 1);
        }
        setWindowDebugProperties(win);
    }

stack_and_return:
    if (e->event != QX11Info::appRootWindow())
        // only handle the MapNotify sent for the root window
        return;

    if (configure_reqs.contains(win)) {
        bool stacked = false;
        QList<XConfigureRequestEvent*> l = configure_reqs.value(win);
        while (!l.isEmpty()) {
            XConfigureRequestEvent *p = l.takeFirst();
            configureWindow(item, p);
            if (p->value_mask & CWStackMode)
                stacked = true;
            free(p);
        }
        configure_reqs.remove(win);
        if (stacked) {
            if (!possiblyUnredirectTopmostWindow())
                enableCompositing(true);
            return;
        }
    }

    /* do this after bindWindow() so that the window is in stacking_list */
    if (item->windowState() == NormalState &&
        (stack[DESKTOP_LAYER] != win || !getTopmostApp(0, win)))
        activateWindow(win, CurrentTime, false);
    else
        // desktop is stacked below the active application
        positionWindow(win, STACK_BOTTOM);

    // needs to be done after stacking the new window
    if (check_compositing && !possiblyUnredirectTopmostWindow())
        enableCompositing(true);
}

static bool should_be_pinged(MCompositeWindow *cw)
{
    if (cw && cw->supportedProtocols().indexOf(ATOM(_NET_WM_PING)) != -1
        && cw->windowTypeAtom() != ATOM(_NET_WM_WINDOW_TYPE_DOCK)
        && cw->windowTypeAtom() != ATOM(_NET_WM_WINDOW_TYPE_MENU)
        && cw->iconifyState() == MCompositeWindow::NoIconifyState
        && !cw->isDecorator() && !cw->isOverrideRedirect()
        && cw->windowTypeAtom() != ATOM(_NET_WM_WINDOW_TYPE_DESKTOP))
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
        if (i && i->windowState() == IconicState) {
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
        // FIXME/TODO: if the window does not support delete, we need to kill
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
                bool was_hung = ping_source->status() == MCompositeWindow::HUNG;
                ping_source->receivedPing(event->data.l[1]);
                Q_ASSERT(ping_source->status() != MCompositeWindow::HUNG);
                Window managed = MDecoratorFrame::instance()->managedWindow();
                if (was_hung && ping_source->window() == managed
                    && !ping_source->needDecoration()) {
                    MDecoratorFrame::instance()->lower();
                    MDecoratorFrame::instance()->setManagedWindow(0);
                    MDecoratorFrame::instance()->setAutoRotation(false);
                    if(!ping_source->hasAlpha()) 
                        disableCompositing(FORCED);
                } else if (was_hung && ping_source->window() == managed
                           && FULLSCREEN_WINDOW(ping_source)) {
                    // ongoing call decorator
                    MDecoratorFrame::instance()->setAutoRotation(false);
                    MDecoratorFrame::instance()->setOnlyStatusbar(true);
                }
            }
        }
    } else if (event->message_type == ATOM(_NET_WM_STATE)) {
        if (event->data.l[1] == (long)  ATOM(_NET_WM_STATE_SKIP_TASKBAR)) {
            skiptaskbar_wm_state(event->data.l[0], event->window);
            if (i) {
                QVector<Atom> states = atom->getAtomArray(event->window,
                                                          ATOM(_NET_WM_STATE));
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

                // mark other applications on top of the desktop Iconified and
                // raise the desktop above them to make the animation end onto
                // the desktop
                int wi, lower_i = -1;
                for (wi = stacking_list.size() - 1; wi >= 0; --wi) {
                     Window w = stacking_list.at(wi);
                     if (w == lower) {
                         lower_i = wi;
                         continue;
                     }
                     if (w == stack[DESKTOP_LAYER])
                         break;
                     MCompositeWindow *cw = COMPOSITE_WINDOW(w);
                     if (cw && cw->isMapped() && isAppWindow(cw))
                         setWindowState(cw->window(), IconicState);
                }
                Q_ASSERT(lower_i > 0);
                stacking_list.move(stacking_list.indexOf(stack[DESKTOP_LAYER]),
                                   lower_i - 1);

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
        // redirect windows for the switcher
        enableCompositing();
        positionWindow(stack[DESKTOP_LAYER], STACK_TOP);
        if (compositing)
            possiblyUnredirectTopmostWindow();
    }
    setWindowState(window->window(), IconicState);
}

void MCompositeManagerPrivate::raiseOnRestore(MCompositeWindow *window)
{
    Window last = getLastVisibleParent(window);
    MCompositeWindow *to_stack;
    if (last)
        to_stack = COMPOSITE_WINDOW(last);
    else
        to_stack = window;
    setWindowState(to_stack->window(), NormalState);
    positionWindow(to_stack->window(), STACK_TOP);

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

void MCompositeManagerPrivate::activateWindow(Window w, Time timestamp,
                                              bool disableCompositing)
{
    MCompositeWindow *cw = COMPOSITE_WINDOW(w);
    if (!cw || !cw->isMapped()) return;

    if (cw->windowTypeAtom() != ATOM(_NET_WM_WINDOW_TYPE_DESKTOP) &&
        cw->windowTypeAtom() != ATOM(_NET_WM_WINDOW_TYPE_DOCK) &&
        !cw->isDecorator()) {
        setExposeDesktop(false);
        // if this is a transient window, raise the "parent" instead
        Window last = getLastVisibleParent(cw);
        MCompositeWindow *to_stack = cw;
        if (last) to_stack = COMPOSITE_WINDOW(last);
        // move it to the correct position in the stack
        positionWindow(to_stack->window(), STACK_TOP);
        // possibly set decorator
        if (cw == getHighestDecorated()) {
            if (FULLSCREEN_WINDOW(cw)) {
                // fullscreen window has decorator above it during ongoing call
                MDecoratorFrame::instance()->setManagedWindow(cw, true);
                MDecoratorFrame::instance()->setOnlyStatusbar(true);
            } else {
                MDecoratorFrame::instance()->setManagedWindow(cw);
                MDecoratorFrame::instance()->setOnlyStatusbar(false);
            }
        }
    } else if (cw->isDecorator()) {
        // if decorator crashes and reappears, stack it to bottom, raise later
        positionWindow(w, STACK_BOTTOM);
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
        // keep compositing to have synthetic events to obscure all windows
        enableCompositing(true, true);
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
        MCompositeWindow *cw = getHighestDecorated();
        if (cw && FULLSCREEN_WINDOW(cw)) {
            cw->setDecorated(true);
            MDecoratorFrame::instance()->setManagedWindow(cw, true);
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
    MCompositeWindow* i = COMPOSITE_WINDOW(w);
    if(i && i->windowState() == state)
        return;
    else if (i)
        // cannot wait for the property change notification
        i->setWindowState(state);

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
               ATOM(_M_WM_WINDOW_DIRECT_VISIBLE) : ATOM(_M_WM_WINDOW_COMPOSITED_VISIBLE);
    else
        d[0] = i->isDirectRendered() ?
               ATOM(_M_WM_WINDOW_DIRECT_INVISIBLE) : ATOM(_M_WM_WINDOW_COMPOSITED_INVISIBLE);

    XChangeProperty(QX11Info::display(), w, ATOM(_M_WM_INFO), XA_ATOM,
                    32, PropModeReplace, (unsigned char *)d, 1);
    long z = i->zValue();
    XChangeProperty(QX11Info::display(), w, ATOM(_M_WM_WINDOW_ZVALUE), XA_CARDINAL, 32, PropModeReplace,
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
            if (bindWindow(kids[i], &attr)) {
                if (kids[i] == localwin || kids[i] == parentWindow(localwin))
                    continue;
                XGrabButton(QX11Info::display(), AnyButton, AnyModifier, kids[i],
                            True,
                            ButtonPressMask | ButtonReleaseMask | ButtonMotionMask,
                            GrabModeSync, GrabModeSync, None, None);
            }
        }
    }
    if (kids)
        XFree(kids);
    scene()->views()[0]->setUpdatesEnabled(true);
    checkStacking(false);

    // Wait for the MapNotify for the overlay (show() of the graphicsview
    // in main() causes it even if we don't map it explicitly)
    XEvent xevent;
    XIfEvent(QX11Info::display(), &xevent, map_predicate, (XPointer)xoverlay);
    XUnmapWindow(QX11Info::display(), xoverlay);
    if (!possiblyUnredirectTopmostWindow())
        enableCompositing(true);
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

static MCompositeManagerPrivate *comp_man_priv;
// crude sort function
static int cmp_windows(const void *a, const void *b)
{
    Window w_a = *((Window*)a);
    Window w_b = *((Window*)b);
    MCompositeWindow *cw_a = comp_man_priv->windows.value(w_a, 0),
                     *cw_b = comp_man_priv->windows.value(w_b, 0);
    // a is unused decorator?
    if (cw_a->isDecorator() && !MDecoratorFrame::instance()->managedWindow())
        return -1;
    // b is unused decorator?
    if (cw_b->isDecorator() && !MDecoratorFrame::instance()->managedWindow())
        return 1;
    // a iconified, or a is desktop and b not iconified?
    if (cw_a->windowState() == IconicState ||
        (cw_a->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_DESKTOP)
         && cw_b->windowState() == NormalState))
        return -1;
    // b iconified or desktop?
    if (cw_b->windowState() == IconicState
        || cw_b->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_DESKTOP))
        return 1;
    // b has higher meego layer?
    if (cw_b->meegoStackingLayer() > cw_a->meegoStackingLayer())
        return -1;
    // b has lower meego layer?
    if (cw_b->meegoStackingLayer() < cw_a->meegoStackingLayer())
        return 1;
    Window trans_b = comp_man_priv->getLastVisibleParent(cw_b);
    // b is a notification?
    if (cw_a->meegoStackingLayer() < 6 && !trans_b &&
        cw_b->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_NOTIFICATION))
        return -1;
    // b is an input or keep-above window?
    if (cw_a->meegoStackingLayer() < 5 && !trans_b &&
        (cw_b->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_INPUT) ||
         cw_b->isOverrideRedirect() ||
         cw_b->netWmState().indexOf(ATOM(_NET_WM_STATE_ABOVE)) != -1))
        return -1;
    // b is a system-modal dialog?
    if (cw_a->meegoStackingLayer() < 4 && MODAL_WINDOW(cw_b) && !trans_b &&
        cw_b->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_DIALOG))
        return -1;
    // transiency relation?
    int trans_rel = comp_man_priv->transiencyRelation(cw_a, cw_b);
    if (trans_rel)
        return trans_rel;
    // the last resort: keep the old order
    int a_i = comp_man_priv->stacking_list.indexOf(w_a);
    int b_i = comp_man_priv->stacking_list.indexOf(w_b);
    if (a_i < b_i)
        return -1;
    if (a_i > b_i)
        return 1;
    // TODO: before this can replace checkStacking(), we need to handle at least
    // the decorator, possibly also window groups and dock windows.
    return 0;
}

void MCompositeManagerPrivate::roughSort()
{
    comp_man_priv = this;
    QVector<Window> v = stacking_list.toVector();
    qsort(v.data(), v.size(), sizeof(Window), cmp_windows);
    stacking_list = QList<Window>::fromVector(v);
}

MCompositeWindow *MCompositeManagerPrivate::bindWindow(Window window,
                                                       XWindowAttributes *wa)
{
    Display *display = QX11Info::display();
    bool is_decorator = atom->isDecorator(window);

    // no need for StructureNotifyMask because of root's SubstructureNotifyMask
    XSelectInput(display, window, PropertyChangeMask | ButtonPressMask);
    XCompositeRedirectWindow(display, window, CompositeRedirectManual);

    MCompAtoms::Type wtype = atom->windowType(window);
    MCompositeWindow *item = new MTexturePixmapItem(window, glwidget);
    if (!item->isValid()) {
        item->deleteLater();
        return 0;
    }

    item->saveState();
    item->setIsMapped(true);
    windows[window] = item;

    const XWMHints &h = item->getWMHints();
    if ((h.flags & StateHint) && (h.initial_state == IconicState)) {
        setWindowState(window, IconicState);
        item->setZValue(-1);
    } else {
        setWindowState(window, NormalState);
        item->setZValue(wtype);
    }

    QVector<Atom> states = atom->getAtomArray(window, ATOM(_NET_WM_STATE));
    item->setNetWmState(states.toList());
    int fs_i = states.indexOf(ATOM(_NET_WM_STATE_FULLSCREEN));
    if (wa && fs_i == -1) {
        item->setRequestedGeometry(QRect(wa->x, wa->y, wa->width, wa->height));
    } else if (fs_i == -1) {
        XWindowAttributes a;
        if (!XGetWindowAttributes(display, window, &a)) {
            qWarning("XGetWindowAttributes for 0x%lx failed", window);
            windows.remove(window);
            item->deleteLater();
            return 0;
        }
        item->setRequestedGeometry(QRect(a.x, a.y, a.width, a.height));
    } else {
        int xres = ScreenOfDisplay(display, DefaultScreen(display))->width;
        int yres = ScreenOfDisplay(display, DefaultScreen(display))->height;
        item->setRequestedGeometry(QRect(0, 0, xres, yres));
    }

    if (!is_decorator && !item->isOverrideRedirect()
        && windows_as_mapped.indexOf(window) == -1)
        windows_as_mapped.append(window);

    if (needDecoration(window, item))
        item->setDecorated(true);
    item->setIsDecorator(is_decorator);

    if (!device_state->displayOff())
        item->updateWindowPixmap();

    int i = stacking_list.indexOf(window);
    if (i == -1)
        stacking_list.append(window);
    else
        stacking_list.move(i, stacking_list.size() - 1);
    roughSort();

    addItem(item);
    if (wtype == MCompAtoms::NORMAL)
        item->setWindowTypeAtom(ATOM(_NET_WM_WINDOW_TYPE_NORMAL));
    else
        item->setWindowTypeAtom(atom->getType(window));

    if (wtype == MCompAtoms::INPUT) {
        if (!device_state->displayOff())
            item->updateWindowPixmap();
        checkStacking(false);
        return item;
    } else if (wtype == MCompAtoms::DESKTOP) {
        // just in case startup sequence changes
        stack[DESKTOP_LAYER] = window;
        connect(this, SIGNAL(inputEnabled()), item,
                SLOT(setUnBlurred()));
        checkStacking(false);
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

    checkStacking(false);

    if (item == getHighestDecorated()) {
        if (fs_i != -1) {
            // fullscreen window has decorator above it during ongoing call
            MDecoratorFrame::instance()->setManagedWindow(item, true);
            MDecoratorFrame::instance()->setOnlyStatusbar(true);
        } else {
            MDecoratorFrame::instance()->setManagedWindow(item);
            MDecoratorFrame::instance()->setOnlyStatusbar(false);
        }
    }

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

void MCompositeManagerPrivate::updateWinList()
{
    static QList<Window> prev_list;
    if (windows_as_mapped != prev_list) {
        XChangeProperty(QX11Info::display(),
                        RootWindow(QX11Info::display(), 0),
                        ATOM(_NET_CLIENT_LIST),
                        XA_WINDOW, 32, PropModeReplace,
                        (unsigned char *)windows_as_mapped.toVector().data(),
                        windows_as_mapped.size());

        prev_list = windows_as_mapped;
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
        // needed so that checkStacking() finds the current application
        roughSort();
        break;
    }
    default:
        break;

    }
    updateWinList();
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
    XSync(QX11Info::display(), False);
    
    compositing = true;
    /* send VisibilityNotifies */
    checkStacking(true);

    QTimer::singleShot(100, this, SLOT(enablePaintedCompositing()));
}

void MCompositeManagerPrivate::enablePaintedCompositing()
{
    scene()->views()[0]->setUpdatesEnabled(true);
    glwidget->update();
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
    }

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
}

void MCompositeManagerPrivate::gotHungWindow(MCompositeWindow *w)
{
    if (!MDecoratorFrame::instance()->decoratorItem())
        return;

    enableCompositing(true);

    // own the window so we could kill it if we want to.
    MDecoratorFrame::instance()->setManagedWindow(w, true);
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
