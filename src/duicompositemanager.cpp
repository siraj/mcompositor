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

        // window types
        _NET_WM_WINDOW_TYPE,
        _NET_WM_WINDOW_TYPE_DESKTOP,
        _NET_WM_WINDOW_TYPE_NORMAL,
        _NET_WM_WINDOW_TYPE_DOCK,
        _NET_WM_WINDOW_TYPE_INPUT,
        _NET_WM_WINDOW_TYPE_NOTIFICATION,
        _NET_WM_STATE_ABOVE,
        _NET_WM_STATE_SKIP_TASKBAR,
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

        ATOMS_TOTAL
    };

    DuiCompAtoms(Display *d);

    Type windowType(Window w);
    bool isDecorator(Window w);
    int getPid(Window w);
    long getWmState(Window w);
    Atom getState(Window w);
    QRectF iconGeometry(Window w);
    QVector<Atom> netWmStates(Window w);
    unsigned int get_opacity_prop(Display *dpy, Window w, unsigned int def);
    double get_opacity_percent(Display *dpy, Window w, double def);

    Atom getAtom(const unsigned int name);

private:
    int intValueProperty(Window w, Atom property);
    Atom getType(Window w);
    Atom getAtom(Window w, Atoms atomtype);

    static Atom atoms[ATOMS_TOTAL];

    Display *dpy;
};

#define ATOM(t) atom->getAtom(DuiCompAtoms::t)
Atom DuiCompAtoms::atoms[DuiCompAtoms::ATOMS_TOTAL];
Window DuiCompositeManagerPrivate::stack[TOTAL_LAYERS];

static bool hasDock  = false;

// temporary launch indicator. will get replaced later
static QGraphicsTextItem *launchIndicator = 0;

DuiCompAtoms::DuiCompAtoms(Display *d)
    : dpy(d)
{
    atoms[WM_PROTOCOLS]                = XInternAtom(dpy, "WM_PROTOCOLS", False);
    atoms[WM_DELETE_WINDOW]            = XInternAtom(dpy, "WM_DELETE_WINDOW", False);

    // fetch all the atoms
    atoms[_NET_WM_PID]                 = XInternAtom(dpy, "_NET_WM_PID", False);
    atoms[_NET_WM_PING]                = XInternAtom(dpy, "_NET_WM_PING", False);
    atoms[_NET_WM_WINDOW_TYPE]         = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    atoms[_NET_WM_WINDOW_OPACITY]      = XInternAtom(dpy, "_NET_WM_WINDOW_OPACITY", False);
    atoms[_NET_WM_WINDOW_TYPE_DESKTOP] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    atoms[_NET_WM_WINDOW_TYPE_NORMAL]  = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_NORMAL", False);
    atoms[_NET_WM_WINDOW_TYPE_DOCK]    = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
    atoms[_NET_WM_WINDOW_TYPE_INPUT]   = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_INPUT", False);
    atoms[_NET_WM_WINDOW_TYPE_NOTIFICATION] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_NOTIFICATION", False);

    // window states
    atoms[_NET_WM_STATE]               = XInternAtom(dpy, "_NET_WM_STATE", False);
    atoms[_NET_WM_STATE_ABOVE]         = XInternAtom(dpy, "_NET_WM_STATE_ABOVE", False);
    atoms[_NET_WM_STATE_SKIP_TASKBAR]  = XInternAtom(dpy, "_NET_WM_STATE_SKIP_TASKBAR", False);
    // uses the KDE standard for frameless windows
    atoms[_KDE_NET_WM_WINDOW_TYPE_OVERRIDE]
    = XInternAtom(dpy, "_KDE_NET_WM_WINDOW_TYPE_OVERRIDE", False);
    atoms[_NET_CLIENT_LIST]            = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
    atoms[_NET_CLIENT_LIST_STACKING]   = XInternAtom(dpy, "_NET_CLIENT_LIST_STACKING", False);
    atoms[_NET_WM_ICON_GEOMETRY]       = XInternAtom(dpy, "_NET_WM_ICON_GEOMETRY", False);

    atoms[WM_CHANGE_STATE]             = XInternAtom(dpy, "WM_CHANGE_STATE", False);
    atoms[WM_STATE]                    = XInternAtom(dpy, "WM_STATE", False);

    atoms[_NET_ACTIVE_WINDOW]          = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
    atoms[_NET_CLOSE_WINDOW]           = XInternAtom(dpy, "_NET_CLOSE_WINDOW", False);
    atoms[_DUI_DECORATOR_WINDOW]       = XInternAtom(dpy, "_DUI_DECORATOR_WINDOW", False);
}

DuiCompAtoms::Type DuiCompAtoms::windowType(Window w)
{
    // freedesktop.org window type
    Atom a = getType(w);
    if (a == atoms[_NET_WM_WINDOW_TYPE_DESKTOP])
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

DuiCompositeManagerPrivate::DuiCompositeManagerPrivate(QObject *p)
    : QObject(p),
      glwidget(0),
      damage_cache(0),
      arranged(false),
      compositing(true)
{
    watch   = new DuiCompositeScene(this);
    atom = new DuiCompAtoms(QX11Info::display());
    connect(this, SIGNAL(compositingEnabled()), SLOT(iconifyOnComposite()));
    connect(this, SIGNAL(compositingEnabled()), SLOT(raiseOnComposite()));
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
    Atom a;

    w = XCreateSimpleWindow(QX11Info::display(),
                            RootWindow(QX11Info::display(), 0),
                            0, 0, 1, 1, 0,
                            None, None);
    Xutf8SetWMProperties(QX11Info::display(), w, "DuiCompositor",
                         "DuiCompositor", NULL, 0, NULL, NULL,
                         NULL);
    a = XInternAtom(QX11Info::display(), "_NET_WM_CM_S0", False);
    XSetSelectionOwner(QX11Info::display(), a, w, 0);

    xoverlay = XCompositeGetOverlayWindow(QX11Info::display(),
                                          RootWindow(QX11Info::display(), 0));
    XReparentWindow(QX11Info::display(), localwin, xoverlay, 0, 0);
    enableInput();

    XDamageQueryExtension(QX11Info::display(), &damage_event, &damage_error);
    iconify_window = 0;
}

bool DuiCompositeManagerPrivate::needDecoration(Window window)
{
    return (atom->windowType(window)    != DuiCompAtoms::FRAMELESS
            && atom->windowType(window) != DuiCompAtoms::DESKTOP
            && atom->windowType(window) != DuiCompAtoms::NOTIFICATION
            && atom->windowType(window) != DuiCompAtoms::INPUT
            && atom->windowType(window) != DuiCompAtoms::DOCK
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
        damage_cache->updateWindowPixmap(rects, num);
        if (rects)
            XFree(rects);
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

void DuiCompositeManagerPrivate::unmapEvent(XUnmapEvent *e)
{
    if (e->window == xoverlay)
        return;

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

    bool need_comp = false;

    // Check if existing items on the screen still needs to be composited,
    // otherwise, do direct rendering

    // TODO: move this in disableCompositing(). generalize this function
    // into needCompositing(). Apply here and in disableCompositing()
    // as well
    for (QHash<Window, DuiCompositeWindow *>::iterator it = windows.begin();
            it != windows.end(); ++it) {
        DuiCompositeWindow *i  = it.value();

        if (i->isDecorator())
            continue;
        bool need_decor = i->needDecoration();
        if (!i->isIconified() && (i->hasAlpha() || need_decor)) {
            enableCompositing();
            if (need_decor && DuiDecoratorFrame::instance()->decoratorItem())
                DuiDecoratorFrame::instance()->decoratorItem()->setVisible(true);
            need_comp = true;
            break;
        }
    }
    if (!need_comp)
        disableCompositing();
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
            if (item->needDecoration()) {
                DuiDecoratorFrame::instance()->setManagedWindow(e->window);
                DuiDecoratorFrame::instance()->raise();
                item->update();
            }

            item->setIconified(false);
            int z = atom->windowType(e->window);
            item->setZValue(z);
            positionWindow(e->window, DuiCompositeManagerPrivate::STACK_TOP);
            if ((atom->getState(e->window)   != ATOM(_NET_WM_STATE_ABOVE)) &&
                    (atom->windowType(e->window) != DuiCompAtoms::DOCK))
                topmostWindowsRaise();
        } else {
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
            positionWindow(e->window, DuiCompositeManagerPrivate::STACK_BOTTOM);
        }
    }
}

void DuiCompositeManagerPrivate::configureRequestEvent(XConfigureRequestEvent *e)
{
    if (e->parent != RootWindow(QX11Info::display(), 0))
        return;

    // sandbox these windows. we own them
    if ((atom->windowType(e->window) == DuiCompAtoms::INPUT) ||
            atom->isDecorator(e->window))
        return;

    // dock changed
    if (hasDock && (atom->windowType(e->window) == DuiCompAtoms::DOCK)) {
        dock_region = QRegion(e->x, e->y, e->width, e->height);
        QRect r = (QRegion(QApplication::desktop()->screenGeometry()) - dock_region).boundingRect();
        if (stack[DESKTOP_LAYER])
            XMoveResizeWindow(QX11Info::display(), stack[DESKTOP_LAYER], r.x(), r.y(), r.width(), r.height());

        if (stack[APPLICATION_LAYER])
            XMoveResizeWindow(QX11Info::display(), stack[APPLICATION_LAYER], r.x(), r.y(), r.width(), r.height());

        if (stack[INPUT_LAYER])
            XMoveResizeWindow(QX11Info::display(), stack[INPUT_LAYER], r.x(), r.y(), r.width(), r.height());
    }

    // just forward requests for now
    XWindowChanges wc;
    wc.border_width = e->border_width;
    wc.x = e->x;
    wc.y = e->y;
    wc.width = e->width;
    wc.height = e->height;
    wc.sibling =  e->above;
    wc.stack_mode = e->detail;

    if ((e->detail == Above) && (e->above == None)) {
        // enforce DUI guidelines. lower previous application window
        // that was on the screen
        XWindowAttributes a;
        XGetWindowAttributes(QX11Info::display(), e->window, &a);
        if ((a.map_state == IsViewable) && (atom->windowType(e->window) != DuiCompAtoms::DOCK)) {
            setWindowState(e->window, NormalState);
            exposeDesktop(false);
            if (e->window != stack[APPLICATION_LAYER])
                XLowerWindow(QX11Info::display(), stack[APPLICATION_LAYER]);
            stack[APPLICATION_LAYER] = e->window;

            // selective compositing support
            DuiCompositeWindow *i = texturePixmapItem(e->window);
            if (i) {
                i->setZValue(windows.size() + 1);
                i->restore();

                // since we call disable compositing immediately
                // we don't see the animated transition
                if (!i->hasAlpha() && !i->needDecoration())
                    disableCompositing();
                else if (DuiDecoratorFrame::instance()->managedWindow() == e->window)
                    enableCompositing();
            }
        }
    } else if ((e->detail == Below) && (e->above == None))
        setWindowState(e->window, IconicState);

    unsigned int value_mask = e->value_mask;
    XConfigureWindow(QX11Info::display(), e->window, value_mask, &wc);
}

void DuiCompositeManagerPrivate::mapRequestEvent(XMapRequestEvent *e)
{
    XWindowAttributes a;
    Display *dpy  = QX11Info::display();
    bool hasAlpha = false;

    XGetWindowAttributes(QX11Info::display(), e->window, &a);
    if (!hasDock) {
        hasDock = (atom->windowType(e->window) == DuiCompAtoms::DOCK);
        if (hasDock)
            dock_region = QRegion(a.x, a.y, a.width, a.height);
    }
    int xres = ScreenOfDisplay(dpy, DefaultScreen(dpy))->width;
    int yres = ScreenOfDisplay(dpy, DefaultScreen(dpy))->height;

    if ((atom->windowType(e->window)    == DuiCompAtoms::FRAMELESS
            || atom->windowType(e->window) == DuiCompAtoms::DESKTOP
            || atom->windowType(e->window) == DuiCompAtoms::INPUT)
            && (atom->windowType(e->window) != DuiCompAtoms::DOCK)) {
        if (hasDock && ((dock_region.boundingRect().width()  <= a.width) &&
                        (dock_region.boundingRect().height() <= a.height))) {
            QRect r = (QRegion(a.x, a.y, a.width, a.height) - dock_region).boundingRect();
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
    if (format && (format->type == PictTypeDirect && format->direct.alphaMask)) {
        hasAlpha = true;
        // map overlay here ahead
        enableCompositing();
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

void DuiCompositeManagerPrivate::topmostWindowsRaise()
{
    // Raise to the topmost the _NET_WM_STATE_ABOVE
    // and DOCK windows
    for (QHash<Window, DuiCompositeWindow *>::iterator it = windows.begin();
            it != windows.end(); ++it) {
        Window w = it.key();

        if (atom->isDecorator(w))
            continue;

        if ((atom->getState(w)   == ATOM(_NET_WM_STATE_ABOVE)) ||
                (atom->windowType(w) == DuiCompAtoms::DOCK)) {
            XRaiseWindow(QX11Info::display(), w);
            positionWindow(w, DuiCompositeManagerPrivate::STACK_TOP);
            it.value()->setZValue(DuiCompAtoms::ABOVE);
        }
    }
}

void DuiCompositeManagerPrivate::mapEvent(XMapEvent *e)
{
    Window win = e->window;
    Window transient_for = 0;
    DuiCompositeWindow *tf_item = 0;

    if (win == xoverlay) {
        enableRedirection();
        return;
    }

    // For menus and popups
    XGetTransientForHint(QX11Info::display(), win, &transient_for);
    if (transient_for)
        tf_item = texturePixmapItem(transient_for);

    FrameData fd = framed_windows.value(win);
    if (fd.frame) {
        XWindowAttributes a;
        XGetWindowAttributes(QX11Info::display(), e->window, &a);

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
            DuiCompositeWindow *applayer = texturePixmapItem(stack[APPLICATION_LAYER]);
            if ((applayer && (applayer->childItems().count() == 0))
                    // ensure this is not the previous application layer
                    && (e->window != stack[APPLICATION_LAYER]))
                // lower previous app window
                XLowerWindow(QX11Info::display(), stack[APPLICATION_LAYER]);

            hideLaunchIndicator();
            stack[APPLICATION_LAYER] = e->window; // between

            if (stack[DESKTOP_LAYER])
                exposeDesktop(false);
        }
    }

    DuiCompositeWindow *item = texturePixmapItem(win);
    // Compositing is assumed to be enabled at this point if a window
    // has alpha channels
    if (!compositing && (item && item->hasAlpha())) {
        qWarning("mapEvent(): compositing not enabled!");
        return;
    }
    if (item) {
        if (!item->hasAlpha())
            disableCompositing();
        else {
            ((DuiTexturePixmapItem *)item)->enableRedirectedRendering();
            item->saveBackingStore(true);
            item->delayShow(100);
        }
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
        item = bindWindow(win, tf_item);
        if (!item->hasAlpha() && !atom->isDecorator(win)) {
            compositing = true;
            if (DuiDecoratorFrame::instance()->decoratorItem() &&
                    !item->needDecoration())
                DuiDecoratorFrame::instance()->decoratorItem()->setVisible(false);
            disableCompositing();
        } else
            item->delayShow(500);

        // the current decorated window got mapped
        if (e->window == DuiDecoratorFrame::instance()->managedWindow()) {
            connect(item, SIGNAL(visualized(bool)),
                    DuiDecoratorFrame::instance(),
                    SLOT(visualizeDecorator(bool)));
            compositing = false;
            enableCompositing();
            DuiDecoratorFrame::instance()->raise();
            stack[APPLICATION_LAYER] = e->window;
        }
    }
}

void DuiCompositeManagerPrivate::rootMessageEvent(XClientMessageEvent *event)
{
    Q_UNUSED(event);
    DuiCompositeWindow *i = texturePixmapItem(event->window);
    FrameData fd = framed_windows.value(event->window);

    // raise event->window ...
    if (event->message_type == ATOM(_NET_ACTIVE_WINDOW)) {
        got_active_window = true;

        // Visibility notification to desktop window. Ensure this is sent
        // before transitions are started
        if (stack[DESKTOP_LAYER])
            exposeDesktop(false);

        Window raise = event->window;
        compositing = false;
        enableCompositing();
        XSync(QX11Info::display(), False);
        DuiCompositeWindow *d_item = texturePixmapItem(stack[DESKTOP_LAYER]);
        bool needComp = false;
        if (d_item && d_item->isDirectRendered()) {
            needComp = true;
            raise_window = raise;
        } else
            XRaiseWindow(QX11Info::display(), raise);

        if (i) {
            i->setZValue(windows.size() + 1);
            i->restore(atom->iconGeometry(raise), needComp);
            i->startPing();
        }
        if (fd.frame)
            setWindowState(fd.frame->managedWindow(), NormalState);
        else
            setWindowState(event->window, NormalState);
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
        exposeDesktop(true);
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
                ping_source->receivedPing();
                Window managed = DuiDecoratorFrame::instance()->managedWindow();
                if (ping_source->window() == managed && !ping_source->needDecoration())
                    DuiDecoratorFrame::instance()->lower();
            }
        }
    } else if (event->message_type == ATOM(_NET_WM_STATE)) {
        bool update_root = false;
        Atom skip = ATOM(_NET_WM_STATE_SKIP_TASKBAR);
        QVector<Atom> states = atom->netWmStates(event->window);
        switch (event->data.l[0]) {
        case 0:
            if (event->data.l[1] == (long) skip) {
                int i = states.indexOf(skip);
                if (i != -1) {
                    states.remove(i);
                    XChangeProperty(QX11Info::display(), event->window,
                                    ATOM(_NET_WM_STATE), XA_ATOM, 32, PropModeReplace,
                                    (unsigned char *) states.data(), states.size());
                    update_root = true;
                }
            }
            break;
        case 1:
            if (event->data.l[1] == (long) skip) {
                states.append(skip);
                if (!states.isEmpty()) {
                    XChangeProperty(QX11Info::display(), event->window,
                                    ATOM(_NET_WM_STATE), XA_ATOM, 32, PropModeReplace,
                                    (unsigned char *) states.data(), states.size());
                    update_root = true;
                }
            }
            break;
        default:
            break;
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
}

void DuiCompositeManagerPrivate::clientMessageEvent(XClientMessageEvent *event)
{
    // Handle iconify requests
    if (event->message_type == ATOM(WM_CHANGE_STATE))
        if (event->data.l[0] == IconicState && event->format == 32) {
            setWindowState(event->window, IconicState);

            DuiCompositeWindow *i = texturePixmapItem(event->window);
            Window lower = event->window;
            exposeDesktop(false);

            iconify_window = lower;
            compositing = false;
            enableCompositing();
            if (i) {
                // Delayed transition is only available on platforms
                // which has selective comppositing. This is triggered
                // when windows are rendered off-screen
                i->iconify(atom->iconGeometry(lower), true);
                if (i->needDecoration())
                    i->startTransition();
                i->stopPing();
            }
            return;
        }

    // Handle root messages
    rootMessageEvent(event);
}

void DuiCompositeManagerPrivate::iconifyOnComposite()
{
    if (!iconify_window)
        return;

    // TODO: (work for more)
    // Handle minimize request coming from a managed window itself,
    // if there are any
    FrameData fd = framed_windows.value(iconify_window);
    if (fd.frame) {
        setWindowState(fd.frame->managedWindow(), IconicState);
        DuiCompositeWindow *i = texturePixmapItem(fd.frame->winId());
        iconify_window = fd.frame->winId();
        if (i)
            i->iconify();
    }

    XLowerWindow(QX11Info::display(), iconify_window);
}


void DuiCompositeManagerPrivate::raiseOnComposite()
{
    if (!raise_window)
        return;

    XRaiseWindow(QX11Info::display(), raise_window);
    raise_window = 0;
}

void DuiCompositeManagerPrivate::exposeDesktop(bool exposed)
{
    if (stack[DESKTOP_LAYER]) {
        XVisibilityEvent desk_notify;
        desk_notify.type       = VisibilityNotify;
        desk_notify.send_event = True;
        desk_notify.window     = stack[DESKTOP_LAYER];
        desk_notify.state      = exposed ? VisibilityUnobscured : VisibilityFullyObscured;
        XSendEvent(QX11Info::display(), stack[DESKTOP_LAYER], true,
                   VisibilityChangeMask, (XEvent *)&desk_notify);
    }
    if (stack[DOCK_LAYER]) {
        XVisibilityEvent desk_notify;
        desk_notify.type       = VisibilityNotify;
        desk_notify.send_event = True;
        desk_notify.window     = stack[DOCK_LAYER];
        desk_notify.state      = exposed ? VisibilityUnobscured : VisibilityFullyObscured;
        XSendEvent(QX11Info::display(), stack[DESKTOP_LAYER], true,
                   VisibilityChangeMask, (XEvent *)&desk_notify);
    }
    if (iconify_window) {
        XVisibilityEvent c;
        c.type       = VisibilityNotify;
        c.send_event = True;
        c.window     = iconify_window;
        c.state      = VisibilityFullyObscured;
        XSendEvent(QX11Info::display(), iconify_window, true,
                   VisibilityChangeMask, (XEvent *)&c);

        iconify_window = 0;
    }
}

// Visibility notification to desktop window. Ensure this is called once
// transitions are done
void DuiCompositeManagerPrivate::exposeDesktop()
{
    exposeDesktop(true);
}

bool DuiCompositeManagerPrivate::isSelfManagedFocus(Window w)
{
    XWindowAttributes attr;
    XGetWindowAttributes(QX11Info::display(), w, &attr);
    if (attr.override_redirect || atom->windowType(w) == DuiCompAtoms::INPUT)
        return false;

    bool ret = true;
    XWMHints *h = 0;
    h = XGetWMHints(QX11Info::display(), w);
    if (h) {
        if ((h->flags & InputHint) && (h->input == False))
            ret = false;
        XFree(h);
    }

    return ret;
}

void DuiCompositeManagerPrivate::activateWindow(Window w)
{
    if (!isSelfManagedFocus(w))
        return;

    if (DuiDecoratorFrame::instance()->managedWindow() == w)
        DuiDecoratorFrame::instance()->activate();
    if (atom->windowType(w) == DuiCompAtoms::DESKTOP) {
        got_active_window = false;
        QTimer::singleShot(100, this, SLOT(directRenderDesktop()));
    }
    /* already focused */
    if (prev_focus == w)
        return;

    prev_focus = w;

    XSetInputFocus(QX11Info::display(), w, RevertToPointerRoot, CurrentTime);
    if ((atom->windowType(w) != DuiCompAtoms::DESKTOP) &&
            (atom->windowType(w) != DuiCompAtoms::DOCK)) {
        XRaiseWindow(QX11Info::display(), w);
        stack[APPLICATION_LAYER] = w;
    }
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
        XAllowEvents(QX11Info::display(), ReplayPointer, CurrentTime);
        activateWindow(event->xbutton.window);
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

    XQueryTree(QX11Info::display(), desk->winId(),
               &r, &p, &kids, &children);

    for (i = 0; i < children; ++i)  {
        XGetWindowAttributes(QX11Info::display(), kids[i], &attr);
        if (attr.map_state == IsViewable &&
                localwin != kids[i] &&
                (attr.width > 1 && attr.height > 1)) {
            bindWindow(kids[i], 0);
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
    if (windows.remove(w) == 0)
        return false;

    stacking_list.removeAll(w);

    if (stack[APPLICATION_LAYER] == w) stack[APPLICATION_LAYER] = 0;
    updateWinList();
    return true;
}

DuiCompositeWindow *DuiCompositeManagerPrivate::bindWindow(Window window,
        DuiCompositeWindow *tf)
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
    if (needDecoration(window)) {
        item->setDecorated(true);
        DuiDecoratorFrame::instance()->setManagedWindow(window);
    }

    item->updateWindowPixmap();
    // _NET_CLIENT_LIST_STACKING. Oldest windows first
    if (!is_decorator && (atom->windowType(window) != DuiCompAtoms::DOCK))
        stacking_list.append(window);
    addItem(item);
    if (wtype == DuiCompAtoms::INPUT) {
        item->updateWindowPixmap();
        return item;
    } if (wtype == DuiCompAtoms::DESKTOP) {
        // just in case startup sequence changes
        stack[DESKTOP_LAYER] = window;
        connect(this, SIGNAL(inputEnabled()), item,
                SLOT(setUnBlurred()));
        return item;
    }
    if (tf)
        item->setParentItem(tf);

    item->manipulationEnabled(true);
    topmostWindowsRaise();

    // the decorator got mapped. this is here because the compositor
    // could be restarted at any point
    if (is_decorator && !DuiDecoratorFrame::instance()->decoratorItem()) {
        XLowerWindow(QX11Info::display(), window);
        // initially update the texture for this window because this
        // will be hidden on first show
        item->updateWindowPixmap();
        item->setVisible(false);
        DuiDecoratorFrame::instance()->setDecoratorItem(item);
    } else
        item->setVisible(true);

    XWMHints *h = 0;
    h = XGetWMHints(display, window);
    if (h) {
        if ((h->flags & StateHint) && (h->initial_state == IconicState)) {
            setWindowState(window, IconicState);
            XLowerWindow(display, window);
        }
        XFree(h);
    }

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
    connect(item, SIGNAL(acceptingInput()), SLOT(enableInput()));
    connect(item, SIGNAL(itemIconified()), SLOT(exposeDesktop()));
    connect(this, SIGNAL(compositingEnabled()), item, SLOT(startTransition()));
    if (!item->hasAlpha() && !item->needDecoration())
        connect(item, SIGNAL(itemRestored()), SLOT(disableCompositing()));

    // ping protocol
    connect(item, SIGNAL(windowHung(DuiCompositeWindow *)),
            SLOT(gotHungWindow(DuiCompositeWindow *)));

    connect(item, SIGNAL(pingTriggered(DuiCompositeWindow *)),
            SLOT(sendPing(DuiCompositeWindow *)));

    updateWinList();
}

void DuiCompositeManagerPrivate::updateWinList(bool stackingOnly)
{
    if (!stackingOnly) {
        Window *w = 0;
        w = (Window *) malloc(sizeof(Window) * windows.size());

        unsigned int i = 0;
        Window win;

        for (QHash<Window, DuiCompositeWindow *>::const_iterator it = windows.constBegin();
                it != windows.constEnd(); ++it) {

            win = it.key();
            DuiCompositeWindow *d = it.value();
            if (!d->isOverrideRedirect() && !atom->isDecorator(it.key()))
                w[i++] = it.key();
        }

        XChangeProperty(QX11Info::display(), RootWindow(QX11Info::display(), 0),
                        ATOM(_NET_CLIENT_LIST),
                        XA_WINDOW, 32, PropModeReplace,
                        (unsigned char *)w, windows.size());

        topmostWindowsRaise();
        if (w)
            free(w);
    }
    // Use the safer and easier to manipulate separate QList to handle
    // the _NET_CLIENT_LIST_STACKING.
    XChangeProperty(QX11Info::display(), RootWindow(QX11Info::display(), 0),
                    ATOM(_NET_CLIENT_LIST_STACKING),
                    XA_WINDOW, 32, PropModeReplace,
                    (unsigned char *)stacking_list.toVector().data(),
                    stacking_list.size());
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
    case DuiCompositeManagerPrivate::STACK_BOTTOM: {
        stacking_list.move(wp, 0);
        break;
    }
    case DuiCompositeManagerPrivate::STACK_TOP: {
        stacking_list.move(wp, stacking_list.size() - 1);
        break;
    }
    default:
        break;

    }

    // Force the inputmethod window to be really on top of the rest
    wp = stacking_list.indexOf(stack[INPUT_LAYER]);
    if (wp >= 0 && wp < stacking_list.size())
        stacking_list.move(wp, stacking_list.size() - 1);
    updateWinList(true);
}

void DuiCompositeManagerPrivate::enableCompositing()
{
    if (compositing)
        return;

    XWindowAttributes a;
    XGetWindowAttributes(QX11Info::display(), xoverlay, &a);
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

    glwidget->setAttribute(Qt::WA_PaintOutsidePaintEvent);
    QPainter m(glwidget);
    m.drawPixmap(0, 0, QPixmap::grabWindow(QX11Info::appRootWindow()));
    glwidget->update();
    QCoreApplication::flush();
    XSync(QX11Info::display(), False);

    // Freeze painting of framebuffer as of this point
    scene()->views()[0]->setUpdatesEnabled(false);
    XMoveWindow(QX11Info::display(), localwin, -2, -2);
    XMapWindow(QX11Info::display(), xoverlay);
    XSync(QX11Info::display(), False);
}

void DuiCompositeManagerPrivate::enableRedirection()
{
    for (QHash<Window, DuiCompositeWindow *>::iterator it = windows.begin();
            it != windows.end(); ++it) {
        DuiCompositeWindow *tp  = it.value();

        ((DuiTexturePixmapItem *)tp)->enableRedirectedRendering();

        // Hide if really not visible
        if (tp->isIconified())
            tp->hide();
    }
    glwidget->update();
    compositing = true;

    scene()->views()[0]->setUpdatesEnabled(true);

    usleep(50000);
    // At this point everything should be rendered off-screen
    emit compositingEnabled();
}

void DuiCompositeManagerPrivate::disableCompositing()
{
    // we could still have exisisting decorator on-screen.
    // ensure we don't accidentally disturb it
    if (!compositing || (DuiDecoratorFrame::instance()->decoratorItem() &&
                         DuiDecoratorFrame::instance()->decoratorItem()->isVisible()))
        return;

    scene()->views()[0]->setUpdatesEnabled(false);

    for (QHash<Window, DuiCompositeWindow *>::iterator it = windows.begin();
            it != windows.end(); ++it) {
        DuiCompositeWindow *tp  = it.value();
        // checks above fail. somehow decorator got in. stop it at this point
        if (!tp->isDecorator() && !tp->isIconified())
            ((DuiTexturePixmapItem *)tp)->enableDirectFbRendering();
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

    XEvent ev;
    memset(&ev, 0, sizeof(ev));

    ev.xclient.type = ClientMessage;
    ev.xclient.window = window;
    ev.xclient.message_type = ATOM(WM_PROTOCOLS);
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = ATOM(_NET_WM_PING);
    ev.xclient.data.l[1] = CurrentTime;
    ev.xclient.data.l[2] = window;

    XSendEvent(QX11Info::display(), window, False, NoEventMask, &ev);
    XSync(QX11Info::display(), False);
}

void DuiCompositeManagerPrivate::gotHungWindow(DuiCompositeWindow *w)
{
    Window window = w->window();
    compositing = false;
    enableCompositing();

    // own the window so we could kill it if we want to.
    // raise the items manually because at this time it is no longer
    // responding to any X messages
    DuiDecoratorFrame::instance()->setManagedWindow(window);
    DuiDecoratorFrame::instance()->raise();
    DuiCompositeWindow *p = DuiDecoratorFrame::instance()->decoratorItem();
    if (p) {
        int z = atom->windowType(window);
        w->setZValue(z);
        p->setZValue(z);
#if (QT_VERSION >= 0x040600)
        w->stackBefore(p);
#endif

        w->updateWindowPixmap();
    }
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
