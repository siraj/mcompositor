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
#include "mcompositemanagerextension.h"
#include "mcompmgrextensionfactory.h"
#include <mrmiserver.h>

#include <QX11Info>
#include <QByteArray>
#include <QVector>
#include <QtPlugin>

#include <X11/Xutil.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xrandr.h>
#include <X11/Xatom.h>
#include <X11/Xmd.h>
#include <X11/XKBlib.h>
#include "mcompatoms_p.h"

#include <sys/types.h>
#include <signal.h>

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

#define COMPOSITE_WINDOW(X) MCompositeWindow::compositeWindow(X)
#define FULLSCREEN_WINDOW(X) \
        ((X)->propertyCache()->netWmState().indexOf(ATOM(_NET_WM_STATE_FULLSCREEN)) != -1)
#define MODAL_WINDOW(X) \
        ((X)->propertyCache()->netWmState().indexOf(ATOM(_NET_WM_STATE_MODAL)) != -1)

Atom MCompAtoms::atoms[MCompAtoms::ATOMS_TOTAL];
Window MCompositeManagerPrivate::stack[TOTAL_LAYERS];
MCompAtoms *MCompAtoms::d = 0;
static bool hasDock  = false;
static QRect availScreenRect = QRect();
static KeyCode switcher_key = 0;

// temporary launch indicator. will get replaced later
static QGraphicsTextItem *launchIndicator = 0;

static Window transient_for(Window window);
static bool should_be_pinged(MCompositeWindow *cw);

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
    static const char *atom_names[] = {
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
        "_MEEGOTOUCH_VIDEO_ALPHA",
        "_MEEGO_STACKING_LAYER",
        "_MEEGOTOUCH_DECORATOR_BUTTONS",
        "_MEEGOTOUCH_CURRENT_APP_WINDOW",
        "_MEEGOTOUCH_ALWAYS_MAPPED",
        "_MEEGOTOUCH_DESKTOP_VIEW",

        /* RROutput properties */
        RR_PROPERTY_CONNECTOR_TYPE,
        "Panel",
        "AlphaMode",
        "GraphicsAlpha",
        "VideoAlpha",

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

    Q_ASSERT((sizeof(atom_names) / sizeof(atom_names[0])) == ATOMS_TOTAL);

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

QVector<Atom> MCompAtoms::getAtomArray(Window w, Atom array_atom)
{
    QVector<Atom> ret;

    Atom actual;
    int format;
    unsigned long n, left;
    unsigned char *data = 0;
    int result = XGetWindowProperty(QX11Info::display(), w, array_atom, 0, 0,
                                    False, XA_ATOM, &actual, &format,
                                    &n, &left, &data);
    if (data && result == Success && actual == XA_ATOM && format == 32) {
        ret.resize(left / 4);
        XFree((void *) data);
        data = 0;
        
        if (XGetWindowProperty(QX11Info::display(), w, array_atom, 0,
                               ret.size(), False, XA_ATOM, &actual, &format,
                               &n, &left, &data) != Success) {
            ret.clear();
        } else if (n != (ulong)ret.size())
            ret.resize(n);

        if (!ret.isEmpty())
            memcpy(ret.data(), data, ret.size() * sizeof(Atom));

        if (data) XFree((void *) data);
    }

    return ret;
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

class MapRequesterPrivate: public QObject
{
    Q_OBJECT
public:
    static MapRequesterPrivate* instance(QObject* parent = 0)
    {
        if (!d)
            d = new MapRequesterPrivate(parent);
        return d;
    }
    
    void requestMap(MWindowPropertyCache *window)
    {
        if (!((MCompositeManager *) qApp)->isCompositing()
            // if something is already queueing, add to the queue, otherwise
            // the mapping order goes wrong
            || !map_requests.isEmpty())
            map_requests.push_back(window);
        else {
            // create the damage object before mapping to get 'em all
            window->damageTracking(true);
            window->setBeingMapped(true); // don't disable compositing
            XMapWindow(QX11Info::display(), window->winId());
        }
    }

public slots:
    void grantMapRequests()
    {
        while (!map_requests.isEmpty()) {
            // first come first served
            MWindowPropertyCache *w = map_requests.takeFirst();
            // create the damage object before mapping to get 'em all
            w->damageTracking(true);
            w->setBeingMapped(true); // don't disable compositing
            XMapWindow(QX11Info::display(), w->winId());
        }
    }
    
private:
    QList<MWindowPropertyCache*> map_requests;
    explicit MapRequesterPrivate(QObject* parent = 0)
        :QObject(parent)
    {}
    
    static MapRequesterPrivate *d;
};

MapRequesterPrivate* MapRequesterPrivate::d = 0;

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
                                int toggle, Window window,
                                QVector<Atom> *net_wm_state = 0)
{
    Atom fullscreen = ATOM(_NET_WM_STATE_FULLSCREEN);
    Display *dpy = QX11Info::display();
    MCompAtoms *atom = MCompAtoms::instance();
    QVector<Atom> states;
    if (net_wm_state)
        states = *net_wm_state;
    else
        states = atom->getAtomArray(window, ATOM(_NET_WM_STATE));
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
            win->propertyCache()->setNetWmState(states.toList());
        if (win && !MDecoratorFrame::instance()->managedWindow()
            && priv->needDecoration(window, win->propertyCache())) {
            win->setDecorated(true);
            MDecoratorFrame::instance()->setManagedWindow(win);
            MDecoratorFrame::instance()->setOnlyStatusbar(false);
            MDecoratorFrame::instance()->raise();
        } else if (win && need_geometry_modify(window) &&
                   !availScreenRect.isEmpty()) {
            QRect r = availScreenRect;
            XMoveResizeWindow(dpy, window, r.x(), r.y(), r.width(), r.height());
        }
        if (win && win->propertyCache()->isMapped())
            priv->dirtyStacking(false);
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
            win->propertyCache()->setRequestedGeometry(QRect(0, 0, xres, yres));
            win->propertyCache()->setNetWmState(states.toList());
        }
        if (!priv->device_state->ongoingCall()
            && MDecoratorFrame::instance()->managedWindow() == window) {
            if (win) win->setDecorated(false);
            MDecoratorFrame::instance()->lower();
            MDecoratorFrame::instance()->setManagedWindow(0);
        }
        if (win && win->propertyCache()->isMapped())
            priv->dirtyStacking(false);
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

/* Finds, caches and returns the primary (Panel) RROutput.
 * Returns None if it cannot be found or it doesn't support
 * alpha blending. */
static RROutput find_primary_output()
{
    static bool been_here = false;
    static RROutput primary = None;
    int i;
    Display *dpy;
    int major, minor;
    bool has_alpha_mode;
    XRRScreenResources *scres;

    /* Initialize only once. */
    if (been_here != None)
        return primary;
    been_here = true;

    /* Check RandR, who knows what kind of X server we ride. */
    dpy = QX11Info::display();
    if (!XRRQueryVersion(dpy, &major, &minor)
        || !(major > 1 || (major == 1 && minor >= 3)))
      return None;

    /* Enumerate all the outputs X knows about and find the one
     * whose connector type is "Panel". */
    if (!(scres = XRRGetScreenResources(dpy, DefaultRootWindow(dpy))))
        return None;

    has_alpha_mode = false;
    for (i = 0, primary = None; i < scres->noutput && primary == None; i++) {
        Atom t;
        int fmt;
        unsigned char *contype;
        unsigned long nitems, rem;

        if (XRRGetOutputProperty(dpy, scres->outputs[i], ATOM(RROUTPUT_CTYPE),
                                 0, 1, False, False, AnyPropertyType, &t,
                                 &fmt, &nitems, &rem, &contype) == Success) {
            if (t == XA_ATOM && fmt == 32 && nitems == 1
                && *(Atom *)contype == ATOM(RROUTPUT_PANEL)) {
                unsigned char *alpha_mode;

                /* Does the primary output support alpha blending? */
                primary = scres->outputs[i];
                if (XRRGetOutputProperty(dpy, primary,
                          ATOM(RROUTPUT_ALPHA_MODE), 0, 1, False, False,
                          AnyPropertyType, &t, &fmt, &nitems, &rem,
                          &alpha_mode) == Success) {
                    has_alpha_mode = t == XA_INTEGER && fmt == 32
                      && nitems == 1;
                    XFree(alpha_mode);
                }
            }
            XFree(contype);
        }
    }
    XRRFreeScreenResources(scres);

    /* If the primary output doesn't support alpha blending, don't bother. */
    if (!has_alpha_mode)
        primary = None;

    return primary;
}

/* Set GraphicsAlpha and/or VideoAlpha of the primary output
 * and enable/disable alpha blending if necessary. */
static void set_global_alpha(int new_gralpha, int new_vidalpha)
{
    static int blending = -1, gralpha = -1, vidalpha = -1;
    RROutput output;
    Display *dpy;
    int blend;

    Q_ASSERT(-1 <= new_gralpha  && new_gralpha  <= 255);
    Q_ASSERT(-1 <= new_vidalpha && new_vidalpha <= 255);
    if ((output = find_primary_output()) == None)
        return;
    dpy = QX11Info::display();

    /* Only set changed properties. */
    if (new_gralpha < 0)
        new_gralpha = gralpha;
    if (new_vidalpha < 0)
        new_vidalpha = vidalpha;
    if (new_gralpha < 0 && new_vidalpha < 0)
        /* There must have been an error getting the properties. */
        return;

    blend = new_gralpha < 255 || new_vidalpha < 255;
    if (blend != blending && !blend)
        /* Disable blending first. */
        XRRChangeOutputProperty(dpy, output, ATOM(RROUTPUT_ALPHA_MODE),
                                XA_INTEGER, 32, PropModeReplace,
                                (unsigned char *)&blend, 1);

    if (new_gralpha >= 0 && new_gralpha != gralpha) {
        /* Change or reset graphics alpha. */
        XRRChangeOutputProperty(dpy, output, ATOM(RROUTPUT_GRAPHICS_ALPHA),
                                XA_INTEGER, 32, PropModeReplace,
                                (unsigned char *)&new_gralpha, 1);
        gralpha = new_gralpha;
    }
    if (new_vidalpha >= 0 && new_vidalpha != vidalpha) {
        /* Change or reset video alpha. */
        XRRChangeOutputProperty(dpy, output, ATOM(RROUTPUT_VIDEO_ALPHA),
                                XA_INTEGER, 32, PropModeReplace,
                                (unsigned char *)&new_vidalpha, 1);
        vidalpha = new_vidalpha;
    }

    if (blend != blending && blend)
        /* Enable blending last. */
        XRRChangeOutputProperty(dpy, output, ATOM(RROUTPUT_ALPHA_MODE),
                                XA_INTEGER, 32, PropModeReplace,
                                (unsigned char *)&blend, 1);
    blending = blend;
}

/* Turn off global alpha blending on both planes. */
static void reset_global_alpha()
{
    set_global_alpha(255, 255);
}

static Bool map_predicate(Display *display, XEvent *xevent, XPointer arg)
{
    Q_UNUSED(display);
    Window window = (Window)arg;
    if (xevent->type == MapNotify && xevent->xmap.window == window)
        return True;
    return False;
}

static void kill_window(Window window)
{
    int pid = MCompAtoms::instance()->getPid(window);
    if (pid != 0) {
        // negative PID to kill the whole process group
        ::kill(-pid, SIGKILL);
        ::kill(pid, SIGKILL);
    }
    XKillClient(QX11Info::display(), window);
}

// -1: cw_a is cw_b's ancestor; 1: cw_b is cw_a's ancestor; 0: no relation
static int transiencyRelation(MCompositeWindow *cw_a, MCompositeWindow *cw_b)
{
    Window parent;
    MCompositeWindow *tmp, *cw_p;
    for (tmp = cw_b; (parent = tmp->propertyCache()->transientFor())
                     && (cw_p = COMPOSITE_WINDOW(parent)); tmp = cw_p)
       if (cw_p == cw_a)
           return -1;
    for (tmp = cw_a; (parent = tmp->propertyCache()->transientFor())
                     && (cw_p = COMPOSITE_WINDOW(parent)); tmp = cw_p)
       if (cw_p == cw_b)
           return 1;
    return 0;
}

static void safe_move(QList<Window>& winlist, int from, int to)
{            
    int slsize = winlist.size();
    if( (from == to) || (from < 0) || (from >= slsize) ||
        (to < 0) || (to >= slsize) )
        return;
    
    winlist.move(from,to);
}

MCompositeManagerPrivate::MCompositeManagerPrivate(QObject *p)
    : QObject(p),
      prev_focus(0),
      buttoned_win(0),
      glwidget(0),
      compositing(true),
      stacking_timeout_check_visibility(false)
{
    xcb_conn = XGetXCBConnection(QX11Info::display());
    MWindowPropertyCache::set_xcb_connection(xcb_conn);

    watch = new MCompositeScene(this);
    atom = MCompAtoms::instance();

    device_state = new MDeviceState(this);
    connect(device_state, SIGNAL(displayStateChange(bool)),
            this, SLOT(displayOff(bool)));
    connect(device_state, SIGNAL(callStateChange(bool)),
            this, SLOT(callOngoing(bool)));
    stacking_timer.setSingleShot(true);
    connect(&stacking_timer, SIGNAL(timeout()), this, SLOT(stackingTimeout()));
    connect(this, SIGNAL(compositingEnabled()), MapRequesterPrivate::instance(this),
            SLOT(grantMapRequests()));
    connect(this, SIGNAL(currentAppChanged(Window)), this,
            SLOT(setupButtonWindows(Window)));
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

static void setup_key_grabs()
{
    Display* dpy = QX11Info::display();
    static bool ignored_mod = false;
    if (!switcher_key) {
        switcher_key = XKeysymToKeycode(dpy, XStringToKeysym("BackSpace"));
        XGrabKey(dpy, switcher_key, Mod5Mask,
                 RootWindow(QX11Info::display(), 0), True,
                 GrabModeSync, GrabModeSync);
    }
    
    if (!ignored_mod) {
        XkbDescPtr xkb_t;
        
        if ((xkb_t = XkbAllocKeyboard()) == NULL)
            return;
        
        if (XkbGetControls(dpy, XkbAllControlsMask, xkb_t) == Success) 
            XkbSetIgnoreLockMods(dpy, xkb_t->device_spec, Mod5Mask, Mod5Mask, 
                                 0, 0);    
        XkbFreeControls(xkb_t, 0, True);
        ignored_mod = true;
    }
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
    setup_key_grabs();

    Xutf8SetWMProperties(QX11Info::display(), w, "MCompositor",
                         "MCompositor", NULL, 0, NULL, NULL,
                         NULL);
    Atom a = XInternAtom(QX11Info::display(), "_NET_WM_CM_S0", False);
    XSetSelectionOwner(QX11Info::display(), a, w, 0);

    xoverlay = XCompositeGetOverlayWindow(QX11Info::display(),
                                          RootWindow(QX11Info::display(), 0));
    overlay_mapped = false; // make sure we try to map it in startup
    XReparentWindow(QX11Info::display(), localwin, xoverlay, 0, 0);
    localwin_parent = xoverlay;
    enableInput();

    XDamageQueryExtension(QX11Info::display(), &damage_event, &damage_error);

    // create InputOnly windows for close and Home button handling
    close_button_win = XCreateWindow(QX11Info::display(),
                                     RootWindow(QX11Info::display(), 0),
                                     -1, -1, 1, 1, 0, CopyFromParent,
                                     InputOnly, CopyFromParent, 0, 0);
    XSelectInput(QX11Info::display(), close_button_win,
                 ButtonReleaseMask | ButtonPressMask);
    XMapWindow(QX11Info::display(), close_button_win);
    home_button_win = XCreateWindow(QX11Info::display(),
                                    RootWindow(QX11Info::display(), 0),
                                    -1, -1, 1, 1, 0, CopyFromParent,
                                    InputOnly, CopyFromParent, 0, 0);
    XSelectInput(QX11Info::display(), home_button_win,
                 ButtonReleaseMask | ButtonPressMask);
    XMapWindow(QX11Info::display(), home_button_win);
}

void MCompositeManagerPrivate::loadPlugins()
{
    // hard-coded for now. move this to plugindir later
#define PDIR "/usr/lib/mcompositor"
    QDir pluginsDir = QDir(PDIR);
   
    foreach (QString fileName, pluginsDir.entryList(QDir::Files)) {
         QPluginLoader loader(pluginsDir.absoluteFilePath(fileName));
         QObject *plugin = loader.instance();
         if (plugin) {
             MCompmgrExtensionFactory* p = qobject_cast<MCompmgrExtensionFactory *>(plugin);
             if (p) 
                 p->create();
         } else {
             QString msg;
             QDebug dbg(&msg);
             dbg << "didnt load" << loader.fileName() << loader.errorString();
             ((MCompositeManager*)qApp)->debug(msg);
         }
     }
}

bool MCompositeManagerPrivate::needDecoration(Window window,
                                              MWindowPropertyCache *pc)
{
    bool fs;
    if (pc && pc->isInputOnly())
        return false;
    if (!pc)
        fs = atom->hasState(window, ATOM(_NET_WM_STATE_FULLSCREEN));
    else
        fs = pc->netWmState().indexOf(ATOM(_NET_WM_STATE_FULLSCREEN)) != -1;
    if (device_state->ongoingCall() && fs && ((pc &&
        pc->windowTypeAtom() != ATOM(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE) &&
        pc->windowTypeAtom() != ATOM(_NET_WM_WINDOW_TYPE_MENU)) ||
        (!pc && atom->windowType(window) != MCompAtoms::FRAMELESS)))
        // fullscreen window is decorated during call
        return true;
    if (fs)
        return false;
    bool transient;
    if (!pc)
        transient = transient_for(window);
    else
        transient = (getLastVisibleParent(pc) ? true : false);
    if (!pc && atom->isDecorator(window))
        return false;
    else if (pc && (pc->isDecorator() || pc->isOverrideRedirect()))
        return false;
    else if (!pc) {
        XWindowAttributes a;
        if (!XGetWindowAttributes(QX11Info::display(), window, &a)
            || a.override_redirect || a.c_class == InputOnly)
            return false;
    }
    MCompAtoms::Type t;
    if (!pc)
        t = atom->windowType(window);
    else
        t = pc->windowType();
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
    if (item && rects) {
        item->updateWindowPixmap(rects, num);
        if (item->waitingForDamage())
            item->damageReceived(false);
    }

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
        item->deleteLater();
        removeWindow(item->window());
        // PC deleted with the MCompositeWindow
    } else {
        // We got a destroy event from a framed window (or a window that was
        // never mapped)
        FrameData fd = framed_windows.value(e->window);
        if (fd.frame) {
            framed_windows.remove(e->window);
            delete fd.frame;
        }
        if (prop_caches.contains(e->window)) {
            delete prop_caches.value(e->window);
            prop_caches.remove(e->window);
        }
    }
}

void MCompositeManagerPrivate::propertyEvent(XPropertyEvent *e)
{
    MWindowPropertyCache *pc;

    if (!prop_caches.contains(e->window))
        return;
    pc = prop_caches.value(e->window);

    if (pc->propertyEvent(e) && pc->isMapped()) {
        dirtyStacking(false);
        MCompositeWindow *cw = COMPOSITE_WINDOW(e->window);
        if (cw && !cw->isNewlyMapped()) {
            checkStacking(false);
            // window on top could have changed
            if (!possiblyUnredirectTopmostWindow())
                enableCompositing(false);
        }
    }

    // global alpha events here. TODO: property cache class could handle this
    // but it is straightforward to manipulate it from here
    if (e->atom == ATOM(_MEEGOTOUCH_GLOBAL_ALPHA))
        set_global_alpha(pc->globalAlpha(), -1);
    else if (e->atom == ATOM(_MEEGOTOUCH_VIDEO_ALPHA))
        set_global_alpha(-1, pc->videoGlobalAlpha());
}

Window MCompositeManagerPrivate::getLastVisibleParent(MWindowPropertyCache *pc)
{
    Window last = 0, parent;
    MWindowPropertyCache *orig_pc = pc;
    while (pc && (parent = pc->transientFor())) {
       pc = prop_caches.value(parent, 0);
       if (pc == orig_pc) {
           qWarning("%s(): window 0x%lx belongs to a transiency loop!",
                    __func__, orig_pc->winId());
           break;
       }
       if (pc && pc->isMapped())
           last = parent;
       else // no-good parent, bail out
           break;
    }
    return last;
}

Window MCompositeManagerPrivate::getTopmostApp(int *index_in_stacking_list,
                                               Window ignore_window)
{
    for (int i = stacking_list.size() - 1; i >= 0; --i) {
        Window w = stacking_list.at(i);        
        if (w == ignore_window || !w) continue;
        if (w == stack[DESKTOP_LAYER])
            /* desktop is above all applications */
            return 0;
        MCompositeWindow *cw = COMPOSITE_WINDOW(w);
        MWindowPropertyCache *pc;
        if (cw && cw->isMapped() && (pc = cw->propertyCache()) &&
            (cw->isAppWindow(true) ||
            /* non-transient TYPE_MENU is on the same stacking layer */
            (!getLastVisibleParent(pc) &&
            pc->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_MENU))) &&
            pc->windowState() == NormalState && !cw->isWindowTransitioning()) {
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
        MWindowPropertyCache *pc;
        if (cw && cw->isMapped() && (pc = cw->propertyCache()) &&
            !pc->isOverrideRedirect() &&
            (cw->needDecoration() || cw->status() == MCompositeWindow::Hung
             || (FULLSCREEN_WINDOW(cw) &&
                 pc->windowTypeAtom() != ATOM(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE)
                 && pc->windowTypeAtom() != ATOM(_NET_WM_WINDOW_TYPE_MENU)
                 && device_state->ongoingCall())))
            return cw;
    }
    return 0;
}

// TODO: merge this with disableCompositing() so that in the end we have
// stacking order sensitive logic
bool MCompositeManagerPrivate::possiblyUnredirectTopmostWindow()
{
    static const QRegion fs_r(0, 0,
                    ScreenOfDisplay(QX11Info::display(),
                        DefaultScreen(QX11Info::display()))->width,
                    ScreenOfDisplay(QX11Info::display(),
                        DefaultScreen(QX11Info::display()))->height);
    bool ret = false;
    Window top = 0;
    int win_i = -1;
    MCompositeWindow *cw = 0;
    for (int i = stacking_list.size() - 1; i >= 0; --i) {
        Window w = stacking_list.at(i);
        if (!(cw = COMPOSITE_WINDOW(w)) || cw->propertyCache()->isInputOnly())
            continue;
        if (w == stack[DESKTOP_LAYER]) {
            top = w;
            win_i = i;
            break;
        }
        if (cw->isMapped() && (cw->propertyCache()->hasAlpha()
                               || cw->needDecoration()
                               || cw->propertyCache()->isDecorator()
            // FIXME: implement direct rendering for shaped windows
            || !fs_r.subtracted(cw->propertyCache()->shapeRegion()).isEmpty()))
            // this window prevents direct rendering
            return false;
        // it is a fullscreen, non-transparent window of any type
        if (cw->isMapped()) {
            top = w;
            win_i = i;
            break;
        }
    }

    // this code prevents us disabling compositing when we have a window
    // that has XMapWindow() called but we have not yet received the MapNotify
    for (int i = stacking_list.size() - 1; i >= 0; --i) {
        Window w = stacking_list.at(i);        
        if (w == stack[DESKTOP_LAYER]) break;
        MWindowPropertyCache *pc = prop_caches.value(w, 0);
        if (pc && pc->is_valid && pc->beingMapped())
            return false;
    }

    if (top && cw && !MCompositeWindow::hasTransitioningWindow()) {
        // unredirect the chosen window and any docks and OR windows above it
        // TODO: what else should be unredirected?
        if (!((MTexturePixmapItem *)cw)->isDirectRendered()) {
            ((MTexturePixmapItem *)cw)->enableDirectFbRendering();
            setWindowDebugProperties(top);
        }
        for (int i = win_i + 1; i < stacking_list.size(); ++i) {
            Window w = stacking_list.at(i);
            if ((cw = COMPOSITE_WINDOW(w)) && cw->isMapped() &&
                (cw->propertyCache()->windowTypeAtom()
                                   == ATOM(_NET_WM_WINDOW_TYPE_DOCK)
                 || cw->propertyCache()->isOverrideRedirect())) {
                if (!((MTexturePixmapItem *)cw)->isDirectRendered()) {
                    ((MTexturePixmapItem *)cw)->enableDirectFbRendering();
                    setWindowDebugProperties(w);
                }
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
    MWindowPropertyCache *wpc = 0;
    if (prop_caches.contains(e->window)) {
        wpc = prop_caches.value(e->window);
        wpc->setBeingMapped(false);
        wpc->setIsMapped(false);
        if (!wpc->isInputOnly())
            XRemoveFromSaveSet(QX11Info::display(), e->window);
    }

    // do not keep unmapped windows in windows_as_mapped list
    windows_as_mapped.removeAll(e->window);

    if (e->window == xoverlay) {
        overlay_mapped = false;
        return;
    }

    MCompositeWindow *item = COMPOSITE_WINDOW(e->window);
    if (item) {
        item->stopPing();
        item->setIsMapped(false);
        setWindowState(e->window, WithdrawnState);
        if (item->isVisible() && !item->isClosing())
            item->setVisible(false);
        if (!item->isClosing())
            // mark it direct-rendered so we create damage object etc.
            // in case it is re-mapped
            ((MTexturePixmapItem *)item)->enableDirectFbRendering();

        if (MDecoratorFrame::instance()->managedWindow() == e->window) {
            // decorate next window in the stack if any
            MCompositeWindow *cw = getHighestDecorated();
            if (!cw) {
                MDecoratorFrame::instance()->lower();
                MDecoratorFrame::instance()->setManagedWindow(0);
                positionWindow(MDecoratorFrame::instance()->winId(), false);
            } else {
                if (cw->status() == MCompositeWindow::Hung) {
                    MDecoratorFrame::instance()->setManagedWindow(cw, true);
                    MDecoratorFrame::instance()->setOnlyStatusbar(false);
                } else if (FULLSCREEN_WINDOW(cw) && device_state->ongoingCall()) {
                    MDecoratorFrame::instance()->setManagedWindow(cw, true);
                    MDecoratorFrame::instance()->setOnlyStatusbar(true);
                } else {
                    MDecoratorFrame::instance()->setManagedWindow(cw);
                    MDecoratorFrame::instance()->setOnlyStatusbar(false);
                }
            }
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
        framed_windows.remove(e->window);
        XUngrabServer(QX11Info::display());
        delete fd.frame;
    }
    updateWinList();

    for (int i = 0; i < TOTAL_LAYERS; ++i)
        if (stack[i] == e->window) stack[i] = 0;

    dirtyStacking(false);

    // Set the global alpha if the window beneath this window has one
    MCompositeWindow *newtop = MCompositeWindow::compositeWindow(
                                         getTopmostApp(0, e->window));
    if (newtop)
        set_global_alpha(newtop->propertyCache()->globalAlpha(),
                         newtop->propertyCache()->videoGlobalAlpha());
    else
        reset_global_alpha();
}

void MCompositeManagerPrivate::configureEvent(XConfigureEvent *e)
{
    if (e->window == xoverlay || e->window == localwin
        || e->window == close_button_win || e->window == home_button_win)
        return;

    MCompositeWindow *item = COMPOSITE_WINDOW(e->window);
    if (item) {
        bool check_visibility = false;
        QRect g = item->propertyCache()->realGeometry();
        if (e->x != g.x() || e->y != g.y() || e->width != g.width() ||
            e->height != g.height()) {
            QRect r(e->x, e->y, e->width, e->height);
            item->propertyCache()->setRealGeometry(r);
            check_visibility = true;
        }
        if (item->propertyCache()->windowState() != IconicState) {
            item->setPos(e->x, e->y);
            item->resize(e->width, e->height);
        }
        if (e->override_redirect == True) {
            if (check_visibility)
                dirtyStacking(true);
            return;
        }

        Window above = e->above;
        if (above != None) {
            if (item->needDecoration() &&
                MDecoratorFrame::instance()->decoratorItem() &&
                MDecoratorFrame::instance()->managedWindow() == e->window) {
                if (FULLSCREEN_WINDOW(item) &&
                    item->status() != MCompositeWindow::Hung) {
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
                dirtyStacking(check_visibility);
                check_visibility = false;
            } else
                dirtyStacking(check_visibility);
        } else {
            // FIXME: seems that this branch is never executed?
            if (e->window == MDecoratorFrame::instance()->managedWindow())
                MDecoratorFrame::instance()->lower();
            // item->setIconified(true);
            // ensure ZValue is set only after the animation is done
            item->requestZValue(0);

            MCompositeWindow *desktop = COMPOSITE_WINDOW(stack[DESKTOP_LAYER]);
            if (desktop)
#if (QT_VERSION >= 0x040600)
                item->stackBefore(desktop);
#endif
        }
        if (check_visibility)
            dirtyStacking(true);
    } else if (prop_caches.contains(e->window)) {
        MWindowPropertyCache *pc = prop_caches.value(e->window);
        QRect r(e->x, e->y, e->width, e->height);
        pc->setRealGeometry(r);
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
            QRect r = cw->propertyCache()->requestedGeometry();
            if (e->value_mask & CWX)
                r.setX(e->x);
            if (e->value_mask & CWY)
                r.setY(e->y);
            if (e->value_mask & CWWidth)
                r.setWidth(e->width);
            if (e->value_mask & CWHeight)
                r.setHeight(e->height);
            cw->propertyCache()->setRequestedGeometry(r);
        }
    }

    /* modify stacking_list if stacking order should be changed */
    int win_i = stacking_list.indexOf(e->window);
    if (win_i >= 0 && e->detail == Above && (e->value_mask & CWStackMode)) {
        if (e->value_mask & CWSibling) {
            int above_i = stacking_list.indexOf(e->above);
            if (above_i >= 0) {
                if (above_i > win_i)
                    safe_move(stacking_list, win_i, above_i);
                else
                    safe_move(stacking_list, win_i, above_i + 1);
                dirtyStacking(false);
            }
        } else {
            Window parent = transient_for(e->window);
            if (parent)
                positionWindow(parent, true);
            else
                positionWindow(e->window, true);
        }
    } else if (win_i >= 0 && e->detail == Below
               && (e->value_mask & CWStackMode)) {
        if (e->value_mask & CWSibling) {
            int above_i = stacking_list.indexOf(e->above);
            if (above_i >= 0) {
                if (above_i > win_i)
                    safe_move(stacking_list, win_i, above_i - 1);
                else
                    safe_move(stacking_list, win_i, above_i);
                dirtyStacking(false);
            }
        } else {
            Window parent = transient_for(e->window);
            if (parent)
                positionWindow(parent, false);
            else
                positionWindow(e->window, false);
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

    MWindowPropertyCache *pc = 0;
    if (prop_caches.contains(e->window))
        pc = prop_caches.value(e->window);

    // sandbox these windows. we own them
    if ((pc && pc->isDecorator()) || atom->isDecorator(e->window))
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
        QList<XConfigureRequestEvent*> def;
        QList<XConfigureRequestEvent*> l = configure_reqs.value(e->window, def);
        XConfigureRequestEvent *e_copy =
                (XConfigureRequestEvent*)malloc(sizeof(*e));
        memcpy(e_copy, e, sizeof(*e));
        l.append(e_copy);
        configure_reqs[e->window] = l;
        return;
    }

    MCompAtoms::Type wtype = i->propertyCache()->windowType();
    if ((e->detail == Above) && (e->above == None) &&
        (wtype != MCompAtoms::INPUT) && (wtype != MCompAtoms::DOCK)) {
        setWindowState(e->window, NormalState);
        setExposeDesktop(false);

        // selective compositing support:
        // since we call disable compositing immediately
        // we don't see the animated transition
        if (!i->propertyCache()->hasAlpha() && !i->needDecoration()) {
            i->setIconified(false);        
            disableCompositing(FORCED);
        } else if (MDecoratorFrame::instance()->managedWindow() == e->window)
            enableCompositing();
    }
}

void MCompositeManagerPrivate::mapRequestEvent(XMapRequestEvent *e)
{
    Display *dpy = QX11Info::display();
    MWindowPropertyCache *pc;
    if (prop_caches.contains(e->window))
        pc = prop_caches.value(e->window);
    else {
        pc = new MWindowPropertyCache(e->window);
        if (!pc->is_valid) {
            delete pc;
            return;
        }
        prop_caches[e->window] = pc;
        // we know the parent due to SubstructureRedirectMask on root window
        pc->setParentWindow(RootWindow(dpy, 0));
    }
    if(!pc->isInputOnly())
        XAddToSaveSet(QX11Info::display(), e->window);

    MCompAtoms::Type wtype = pc->windowType();
    QRect a = pc->realGeometry();
    if (!hasDock) {
        hasDock = (wtype == MCompAtoms::DOCK);
        if (hasDock)
            dock_region = QRegion(a.x(), a.y(), a.width(), a.height());
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
        } else if (a.width() != xres && a.height() != yres) {
            XResizeWindow(dpy, e->window, xres, yres);
        }
    }

    // Composition is enabled by default because we are introducing animations
    // on window map. It will be turned off once transitions are done
    enableCompositing(true);
    
    if (pc->isDecorator()) {
        MDecoratorFrame::instance()->setDecoratorWindow(e->window);
        MDecoratorFrame::instance()->setManagedWindow(0);
        MCompositeWindow *cw;
        if ((cw = getHighestDecorated())) {
            if (cw->status() == MCompositeWindow::Hung) {
                MDecoratorFrame::instance()->setManagedWindow(cw, true);
            } else if (FULLSCREEN_WINDOW(cw) && device_state->ongoingCall()) {
                MDecoratorFrame::instance()->setManagedWindow(cw, true);
                MDecoratorFrame::instance()->setOnlyStatusbar(true);
            } else
                MDecoratorFrame::instance()->setManagedWindow(cw);
        }
        return;
    }

#ifdef WINDOW_DEBUG
    overhead_measure.start();
#endif

    const QList<Atom> &states = pc->netWmState();
    if (states.indexOf(ATOM(_NET_WM_STATE_FULLSCREEN)) != -1) {
        QVector<Atom> v = states.toVector();
        fullscreen_wm_state(this, 1, e->window, &v);
    }

    const XWMHints &h = pc->getWMHints();
    if ((h.flags & StateHint) && (h.initial_state == IconicState))
        setWindowState(e->window, IconicState);
    else
        setWindowState(e->window, NormalState);
    if (needDecoration(e->window, pc)) {
        if (MDecoratorFrame::instance()->decoratorItem()) {
            enableCompositing();
            MapRequesterPrivate::instance()->requestMap(pc);
            // initially visualize decorator item so selective compositing
            // checks won't disable compositing
            MDecoratorFrame::instance()->decoratorItem()->setVisible(true);
        } else {
#if 0 /* FIXME/TODO: this does NOT work when mdecorator starts after the first
         decorated window is shown. See NB#196194 */
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
                if (pc->hasAlpha())
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
            MapRequesterPrivate::instance()->requestMap(pc);
            frame->show();

            XSync(QX11Info::display(), False);
#else
            MapRequesterPrivate::instance()->requestMap(pc);
            qWarning("%s: mdecorator hasn't started yet", __func__);
#endif
        }
    } else
        MapRequesterPrivate::instance()->requestMap(pc);
}

/* recursion is needed to handle transients that are transient for other
 * transients */
void MCompositeManagerPrivate::raiseTransientsOf(MWindowPropertyCache *pc,
                                                 int last_i, bool recursion)
{
    static MWindowPropertyCache *orig_pc = 0;
    if (!recursion)
        orig_pc = pc;
    for (QList<Window>::const_iterator it = pc->transientWindows().begin();
         it != pc->transientWindows().end(); ++it) {
        int i = stacking_list.indexOf(*it);
        if (i != -1) {
            stacking_list.move(i, last_i);
            MWindowPropertyCache *p = prop_caches.value(*it, 0);
            if (p == orig_pc && orig_pc) {
                qWarning("%s(): window 0x%lx belongs to a transiency loop!",
                         __func__, orig_pc->winId());
                break;
            }
            if (p && !p->transientWindows().isEmpty())
                raiseTransientsOf(p, last_i, true);
        }
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
        MWindowPropertyCache *pc;
        if (cw) pc = cw->propertyCache();
        if (!cw || !cw->isMapped() || !pc->wantsFocus() || pc->isDecorator()
            || pc->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_DOCK))
            continue;
        if (!pc->isOverrideRedirect() &&
            pc->windowTypeAtom() != ATOM(_NET_WM_WINDOW_TYPE_INPUT)) {
            w = iw;
            break;
        }
        /* FIXME: do this based on geometry to cope with TYPE_NORMAL dialogs */
        /* don't focus a window that is obscured (assumes that NORMAL
         * and DESKTOP cover the whole screen) */
        if (pc->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_NORMAL) ||
            pc->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_DESKTOP))
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

    XChangeProperty(QX11Info::display(), RootWindow(QX11Info::display(), 0),
                    ATOM(_NET_ACTIVE_WINDOW),
                    XA_WINDOW, 32, PropModeReplace, (unsigned char *)&w, 1);
}

void MCompositeManagerPrivate::dirtyStacking(bool force_visibility_check)
{
    if (force_visibility_check)
        stacking_timeout_check_visibility = true;
    if (!stacking_timer.isActive())
        stacking_timer.start();
}

void MCompositeManagerPrivate::pingTopmost()
{
    bool found = false, saw_desktop = false;
    // find out highest application window
    for (int i = stacking_list.size() - 1; i >= 0; --i) {
         MCompositeWindow *cw;
         Window w = stacking_list.at(i);
         if (w == stack[DESKTOP_LAYER]) {
             saw_desktop = true;
             continue;
         }
         if (!(cw = COMPOSITE_WINDOW(w)))
             continue;
         if (!found && !saw_desktop && cw->isMapped() && should_be_pinged(cw)) {
             cw->startPing();
             found = true;
             continue;
         }
         cw->stopPing();
    }
}

void MCompositeManagerPrivate::setupButtonWindows(Window curr_app)
{
    Q_UNUSED(curr_app);
    MCompositeWindow *topmost = 0;
    // find out highest application window
    for (int i = stacking_list.size() - 1; i >= 0; --i) {
         MCompositeWindow *cw;
         Window w = stacking_list.at(i);
         if (w == stack[DESKTOP_LAYER])
             break;
         if (!(cw = COMPOSITE_WINDOW(w)))
             continue;
         if (cw->isMapped() && cw->isAppWindow(true)) {
             topmost = cw;
             break;
         }
    }
    bool home_set = false, close_set = false;
    static bool home_lowered = true, close_lowered = true;
    if (topmost) {
        XWindowChanges wc = {0, 0, 0, 0, 0, topmost->window(), Above};
        int mask = CWX | CWY | CWWidth | CWHeight | CWSibling | CWStackMode;
        const QRect &h = topmost->propertyCache()->homeButtonGeometry();
        if (h.width() > 1 && h.height() > 1) {
            wc.x = h.x(); wc.y = h.y();
            wc.width = h.width(); wc.height = h.height();
            XConfigureWindow(QX11Info::display(), home_button_win, mask, &wc);
            home_button_geom = h;
            home_set = true;
            home_lowered = false;
        }
        const QRect &c = topmost->propertyCache()->closeButtonGeometry();
        if (c.width() > 1 && c.height() > 1) {
            wc.x = c.x(); wc.y = c.y();
            wc.width = c.width(); wc.height = c.height();
            XConfigureWindow(QX11Info::display(), close_button_win, mask, &wc);
            close_button_geom = c;
            close_set = true;
            close_lowered = false;
        }
    }
    if ((home_set || close_set) && topmost) {
        buttoned_win = topmost->window();
        if (!home_set && !home_lowered) {
            XLowerWindow(QX11Info::display(), home_button_win);
            home_lowered = true;
        }
        if (!close_set && !close_lowered) {
            XLowerWindow(QX11Info::display(), close_button_win);
            close_lowered = true;
        }
    } else if (buttoned_win) {
        buttoned_win = 0;
        if (!close_lowered) {
            XLowerWindow(QX11Info::display(), close_button_win);
            close_lowered = true;
        }
        if (!home_lowered) {
            XLowerWindow(QX11Info::display(), home_button_win);
            home_lowered = true;
        }
    }
}

void MCompositeManagerPrivate::setCurrentApp(Window w,
                                             bool stacking_order_changed)
{
    static Window prev = (Window)-1;
    if (prev == w) {
        if (stacking_order_changed)
            // signal listener could be interested in transients of
            // the current application (could be also different signal?)
            emit currentAppChanged(current_app);
        return;
    }
    XChangeProperty(QX11Info::display(), RootWindow(QX11Info::display(), 0),
                    ATOM(_MEEGOTOUCH_CURRENT_APP_WINDOW),
                    XA_WINDOW, 32, PropModeReplace, (unsigned char *)&w, 1);
    current_app = w;
    emit currentAppChanged(current_app);
    prev = w;
}

#define RAISE_MATCHING(X) { \
    first_moved = 0; \
    for (int i = 0; i < last_i;) { \
        Window w = stacking_list.at(i); \
        if (w == first_moved) break; \
        MCompositeWindow *cw = COMPOSITE_WINDOW(w); \
        if (cw && cw->propertyCache() && cw->isMapped() && (X)) { \
            MCompositeWindow *orig_cw = cw; \
            /* find the next window to move */ \
            Window next = 0; \
            for (int next_i = i + 1; next_i <= last_i; ++next_i) { \
                next = stacking_list.at(next_i); \
                cw = COMPOSITE_WINDOW(next); \
                if (cw && cw->propertyCache() && cw->isMapped() && (X)) \
                    break; \
            } \
            stacking_list.move(i, last_i); \
	    raiseTransientsOf(orig_cw->propertyCache(), last_i); \
            if (!first_moved) first_moved = w; \
            if (!next || (i = stacking_list.indexOf(next)) < 0) \
                break; \
        } else ++i; \
    } }

/* Go through stacking_list and verify that it is in order.
 * If it isn't, reorder it and call XRestackWindows.
 * NOTE: stacking_list needs to be reversed before giving it to
 * XRestackWindows.*/
void MCompositeManagerPrivate::checkStacking(bool force_visibility_check,
                                             Time timestamp)
{
    if (stacking_timer.isActive()) {
        if (stacking_timeout_check_visibility) {
            force_visibility_check = true;
            stacking_timeout_check_visibility = false;
        }
        stacking_timer.stop();
    }
    Window active_app = 0, duihome = stack[DESKTOP_LAYER], first_moved,
           set_as_current_app = 0;
    int last_i = stacking_list.size() - 1;    
    bool desktop_up = false, fs_app = false;
    int app_i = -1;
    MDecoratorFrame *deco = MDecoratorFrame::instance();
    MCompositeWindow *aw = 0;

    active_app = getTopmostApp(&app_i);
    if (!active_app || app_i < 0) {
        desktop_up = true;
        if (duihome)
            set_as_current_app = duihome;
    } else {
        aw = COMPOSITE_WINDOW(active_app);
        set_as_current_app = active_app;
        if (aw) {
            // getTopmostApp() can return a transient now
            Window parent = getLastVisibleParent(aw->propertyCache());
            if (parent) {
                active_app = parent;
                aw = COMPOSITE_WINDOW(parent);
            }
            fs_app = FULLSCREEN_WINDOW(aw);
        }
    }

    /* raise active app with its transients, or duihome if
     * there is no active application */
    if (!desktop_up && active_app && app_i >= 0 && aw) {
	/* raise application windows belonging to the same group */
	XID group;
	if ((group = aw->propertyCache()->windowGroup())) {
	    for (int i = 0; i < app_i; ) {
            MCompositeWindow *cw = COMPOSITE_WINDOW(stacking_list.at(i));
            if (cw->propertyCache()->windowState() == NormalState
                && cw->isAppWindow()
                && cw->propertyCache()->windowGroup() == group) {
                safe_move(stacking_list, i, last_i);
	             /* active_app was moved, update the index */
	             app_i = stacking_list.indexOf(active_app);
		     /* TODO: transients */
		 } else ++i;
	    }
	}
    
	safe_move(stacking_list, app_i, last_i);
	/* raise transients recursively */
        if (aw->propertyCache())
	    raiseTransientsOf(aw->propertyCache(), last_i);
    } else if (duihome) {
        //qDebug() << "raising home window" << duihome;
        safe_move(stacking_list, stacking_list.indexOf(duihome), last_i);
    }

    /* raise docks if either the desktop is up or the application is
     * non-fullscreen */
    if (desktop_up || !active_app || app_i < 0 || !aw || !fs_app)
        RAISE_MATCHING(!getLastVisibleParent(cw->propertyCache()) &&
                cw->propertyCache()->windowTypeAtom()
                                        == ATOM(_NET_WM_WINDOW_TYPE_DOCK))
    else if (active_app && aw && deco->decoratorItem() &&
             deco->managedWindow() == active_app) {
        // no dock => decorator starts from (0,0)
        XMoveWindow(QX11Info::display(), deco->decoratorItem()->window(), 0, 0);
    }
    /* Meego layers 1-3: lock screen, ongoing call etc. */
    for (unsigned int level = 1; level < 4; ++level)
         RAISE_MATCHING(!getLastVisibleParent(cw->propertyCache()) &&
                        cw->propertyCache()->windowState() == NormalState
                        && cw->propertyCache()->meegoStackingLayer() == level)
    /* raise all system-modal dialogs */
    RAISE_MATCHING(!getLastVisibleParent(cw->propertyCache())
                    && MODAL_WINDOW(cw) &&
                    cw->propertyCache()->windowTypeAtom()
                                        == ATOM(_NET_WM_WINDOW_TYPE_DIALOG))
    /* raise all keep-above flagged, input methods and Meego layer 4
     * (incoming call), at the same time preserving their mapping order */
    RAISE_MATCHING(!getLastVisibleParent(cw->propertyCache()) &&
                    !cw->propertyCache()->isDecorator() &&
        cw->iconifyState() == MCompositeWindow::NoIconifyState &&
        (cw->propertyCache()->windowTypeAtom()
                                  == ATOM(_NET_WM_WINDOW_TYPE_INPUT) ||
         cw->propertyCache()->meegoStackingLayer() == 4
         || cw->propertyCache()->isOverrideRedirect() ||
         cw->propertyCache()->netWmState().indexOf(ATOM(_NET_WM_STATE_ABOVE)) != -1))
    // Meego layer 5
    RAISE_MATCHING(!getLastVisibleParent(cw->propertyCache()) &&
                   cw->propertyCache()->meegoStackingLayer() == 5
                   && cw->iconifyState() == MCompositeWindow::NoIconifyState)
    /* raise all non-transient notifications (transient ones were already
     * handled above) */
    RAISE_MATCHING(!getLastVisibleParent(cw->propertyCache()) &&
                   cw->propertyCache()->windowTypeAtom()
                           == ATOM(_NET_WM_WINDOW_TYPE_NOTIFICATION))
    // Meego layer 6
    RAISE_MATCHING(!getLastVisibleParent(cw->propertyCache()) &&
                   cw->propertyCache()->meegoStackingLayer() == 6
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
         if (cw->isMapped() && cw->isAppWindow(true)) {
             topmost = cw;
             top_i = i;
             break;
         }
    }
    /* raise decorator */
    if (highest_d && highest_d == topmost && deco->decoratorItem()
        && deco->managedWindow() == highest_d->window()
        && (!FULLSCREEN_WINDOW(highest_d)
            || highest_d->status() == MCompositeWindow::Hung
            || device_state->ongoingCall())) {
        Window deco_w = deco->decoratorItem()->window();
        int deco_i = stacking_list.indexOf(deco_w);
        if (deco_i >= 0) {
            if (deco_i < top_i)
                safe_move(stacking_list, deco_i, top_i);
            else
                safe_move(stacking_list, deco_i, top_i + 1);
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
         if (witem && witem->isMapped() &&
             !witem->isNewlyMapped() && !witem->isClosing())
             only_mapped.append(stacking_list.at(i));
    }
    static QList<Window> prev_only_mapped;

    // fix Z-values always to make sure we do it after an animation
    for (int i = 0; i <= last_i; ++i) {
         MCompositeWindow *witem = COMPOSITE_WINDOW(stacking_list.at(i));
         if (witem && witem->hasTransitioningWindow())
             // don't change Z values until animation is over
             break;
         if (witem)
             witem->requestZValue(i);
    }
    bool order_changed = prev_only_mapped != only_mapped;
    if (order_changed) {
        QList<Window> reverse;
        for (int i = last_i; i >= 0; --i)
            reverse.append(stacking_list.at(i));
        XRestackWindows(QX11Info::display(), reverse.toVector().data(),
                        reverse.size());

        // decorator and OR windows are not included to the property
        QList<Window> no_decors = only_mapped;
        for (int i = 0; i <= last_i; ++i) {
             MCompositeWindow *witem = COMPOSITE_WINDOW(stacking_list.at(i));
             if (witem && witem->isMapped() &&
                 (witem->propertyCache()->isOverrideRedirect()
                  || witem->propertyCache()->isDecorator()))
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
        if (!device_state->displayOff())
            pingTopmost();
    }
    if (order_changed || force_visibility_check) {
        static int xres = ScreenOfDisplay(QX11Info::display(),
                                   DefaultScreen(QX11Info::display()))->width;
        static int yres = ScreenOfDisplay(QX11Info::display(),
                                   DefaultScreen(QX11Info::display()))->height;
        int covering_i = 0;
        static const QRegion fs_r(0, 0, xres, yres);
        for (int i = stacking_list.size() - 1; i >= 0; --i) {
             Window w = stacking_list.at(i);
             if (w == stack[DESKTOP_LAYER]) {
                 covering_i = i;
                 break;
             }
             MCompositeWindow *cw = COMPOSITE_WINDOW(w);
             MWindowPropertyCache *pc = 0;
             if (cw && cw->isMapped())
                 pc = cw->propertyCache();
             if (cw && cw->isMapped() && !pc->hasAlpha() &&
                 !pc->isDecorator() && !cw->hasTransitioningWindow() &&
                 /* FIXME: decorated window is assumed to be fullscreen */
                 (cw->needDecoration() ||
                  fs_r.subtracted(pc->shapeRegion()).isEmpty())) {
                 covering_i = i;
                 break;
             }
        }
        /* Send synthetic visibility events for our babies */
        int home_i = stacking_list.indexOf(duihome);
        for (int i = 0; i <= last_i; ++i) {
            MCompositeWindow *cw = COMPOSITE_WINDOW(stacking_list.at(i));
            if (!cw || !cw->isMapped()) continue;
            if (device_state->displayOff()) {
                cw->setWindowObscured(true);
                // setVisible(false) is not needed because updates are frozen
                // and for avoiding NB#174346
                if (!duihome || (duihome && i >= home_i))
                    setWindowState(cw->window(), NormalState);
                continue;
            }
            if (i >= covering_i) {
                cw->setWindowObscured(false);
                cw->setVisible(true);
            } else {
                if (i < home_i &&
                    ((MTexturePixmapItem *)cw)->isDirectRendered()) {
                    // make sure window below duihome is redirected
                    ((MTexturePixmapItem *)cw)->enableRedirectedRendering();
                    setWindowDebugProperties(cw->window());
                }
                cw->setWindowObscured(true);
                if (cw->window() != duihome)
                    cw->setVisible(false);
            }
            if (!duihome || (duihome && i >= home_i))
                setWindowState(cw->window(), NormalState);
        }
    }
    setCurrentApp(set_as_current_app, order_changed);
}

void MCompositeManagerPrivate::stackingTimeout()
{
    checkStacking(stacking_timeout_check_visibility);
    stacking_timeout_check_visibility = false;
    if (!device_state->displayOff() && !possiblyUnredirectTopmostWindow()) 
        enableCompositing(true);
}

void MCompositeManagerPrivate::mapEvent(XMapEvent *e)
{
    Window win = e->window;

    if (win == xoverlay) {
        overlay_mapped = true;
        enableRedirection();
        return;
    }
    if (win == localwin || win == localwin_parent || win == close_button_win
        || win == home_button_win)
        return;

    MWindowPropertyCache *wpc;
    if (prop_caches.contains(win))
        wpc = prop_caches.value(win);
    else {
        wpc = new MWindowPropertyCache(win);
        if (!wpc->is_valid) {
            delete wpc;
            return;
        }
        prop_caches[win] = wpc;
    }
    wpc->setBeingMapped(false);

    FrameData fd = framed_windows.value(win);
    if (fd.frame) {
        QRect a = wpc->realGeometry();
        XConfigureEvent c;
        c.type = ConfigureNotify;
        c.send_event = True;
        c.event = win;
        c.window = win;
        c.x = 0;
        c.y = 0;
        c.width = a.width();
        c.height = a.height();
        c.border_width = 0;
        c.above = stack[DESKTOP_LAYER];
        c.override_redirect = 0;
        XSendEvent(QX11Info::display(), c.event, True, StructureNotifyMask,
                   (XEvent *)&c);
    }

    MCompAtoms::Type wtype = wpc->windowType();
    // simple stacking model legacy code...
    if (wtype == MCompAtoms::DESKTOP) {
        stack[DESKTOP_LAYER] = win;
    } else if (wtype == MCompAtoms::INPUT) {
        stack[INPUT_LAYER] = win;
    } else if (wtype == MCompAtoms::DOCK) {
        stack[DOCK_LAYER] = win;
    } else {
        if ((wtype == MCompAtoms::FRAMELESS || wtype == MCompAtoms::NORMAL)
                && !wpc->isDecorator()
                && (wpc->parentWindow() == RootWindow(QX11Info::display(), 0))
                && (e->event == QX11Info::appRootWindow())) {
            hideLaunchIndicator();

            setExposeDesktop(false);
        }
    }

    set_global_alpha(wpc->globalAlpha(), wpc->videoGlobalAlpha());

    MWindowPropertyCache *pc = 0;
    MCompositeWindow *item = COMPOSITE_WINDOW(win);
    if (item) {
        item->setIsMapped(true);
        pc = item->propertyCache();
        if (!pc) return;
        if (!pc->isDecorator() && !pc->isOverrideRedirect()
            && windows_as_mapped.indexOf(win) == -1)
            windows_as_mapped.append(win);
    }
    // Compositing is always assumed to be enabled at this point if a window
    // has alpha channels
    if (!compositing && (pc && pc->hasAlpha())) {
        qWarning("mapEvent(): compositing not enabled!");
        enableCompositing(true);
    }
    
    if (item && pc) {
        if (wtype == MCompAtoms::NORMAL)
            pc->setWindowTypeAtom(ATOM(_NET_WM_WINDOW_TYPE_NORMAL));
        else
            pc->setWindowTypeAtom(atom->getType(win));
#ifdef WINDOW_DEBUG
        qDebug() << "Composition overhead (existing pixmap):" 
                 << overhead_measure.elapsed();
#endif
        if (((MTexturePixmapItem *)item)->isDirectRendered()) {
            ((MTexturePixmapItem *)item)->enableRedirectedRendering();
            setWindowDebugProperties(item->window());
        } else
            item->saveBackingStore();
        // TODO: don't show the animation if the window is not stacked on top
        const XWMHints &h = pc->getWMHints();
        if ((!(h.flags & StateHint) || h.initial_state != IconicState)
            && !pc->alwaysMapped() && e->send_event == False
            && !pc->isInputOnly()) {
            // remapped/prestarted apps should also have startup animation
            item->requestZValue(scene()->items().count() + 1);
            item->setNewlyMapped(true);
            if (!item->showWindow())
                item->setNewlyMapped(false);
        } else {
            item->setNewlyMapped(false);
            item->setVisible(true);
        }
        goto stack_and_return;
    }

    // only composite top-level windows
    if ((wpc->parentWindow() == RootWindow(QX11Info::display(), 0))
            && (e->event == QX11Info::appRootWindow())) {
        item = bindWindow(win);
        if (!item)
            return;
        pc = item->propertyCache();
#ifdef WINDOW_DEBUG
        if (pc->hasAlpha())
            qDebug() << "Composition overhead (new pixmap):"
                     << overhead_measure.elapsed();
#endif
        const XWMHints &h = pc->getWMHints();
        if ((!(h.flags & StateHint) || h.initial_state != IconicState)
            && !pc->alwaysMapped() && e->send_event == False
            && !pc->isInputOnly() && item->isAppWindow())
            item->showWindow();
        else {
            item->setNewlyMapped(false);
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
    if (!pc || (e->event != QX11Info::appRootWindow()) || !item)
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
            dirtyStacking(false);
            return;
        }
    }

    /* do this after bindWindow() so that the window is in stacking_list */
    if (pc->windowState() == NormalState &&
        (stack[DESKTOP_LAYER] != win || !getTopmostApp(0, win)))
        activateWindow(win, CurrentTime, false);
    else
        // desktop is stacked below the active application
        positionWindow(win, false);
    
    dirtyStacking(false);
}

static bool should_be_pinged(MCompositeWindow *cw)
{
    MWindowPropertyCache *pc = cw->propertyCache();
    if (pc->supportedProtocols().indexOf(ATOM(_NET_WM_PING)) != -1
        && pc->windowTypeAtom() != ATOM(_NET_WM_WINDOW_TYPE_NOTIFICATION)
        && pc->windowTypeAtom() != ATOM(_NET_WM_WINDOW_TYPE_DOCK)
        && pc->windowTypeAtom() != ATOM(_NET_WM_WINDOW_TYPE_MENU)
        && !pc->isDecorator() && !pc->isOverrideRedirect()
        && pc->windowTypeAtom() != ATOM(_NET_WM_WINDOW_TYPE_DESKTOP))
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

        if (!getTopmostApp()) {
            // Not necessary to animate if not in desktop view.
            Window raise = event->window;
            MCompositeWindow *d_item = COMPOSITE_WINDOW(stack[DESKTOP_LAYER]);
            bool needComp = false;
            if (d_item && d_item->isDirectRendered()
                && raise != stack[DESKTOP_LAYER]) {
                needComp = true;
                enableCompositing(true);
            }
            if (i && i->propertyCache()->windowState() == IconicState) {
                i->setZValue(windows.size() + 1);
                QRectF iconGeometry = i->propertyCache()->iconGeometry();
                i->restore(iconGeometry, needComp);
                set_global_alpha(i->propertyCache()->globalAlpha(),
                                 i->propertyCache()->videoGlobalAlpha());
            }
        }

        if (fd.frame)
            setWindowState(fd.frame->managedWindow(), NormalState);
        else
            setWindowState(event->window, NormalState);
        if (event->window == stack[DESKTOP_LAYER]) {
            // Mark normal applications on top of home Iconic to make our
            // qsort() function to work
            for (int wi = stacking_list.size() - 1; wi >= 0; --wi) {
                 Window w = stacking_list.at(wi);
                 if (w == stack[DESKTOP_LAYER])
                     break;
                 MCompositeWindow *cw = COMPOSITE_WINDOW(w);
                 if (cw && cw->isMapped() &&
                     !cw->propertyCache()->meegoStackingLayer()
                     && cw->isAppWindow(true))
                     setWindowState(cw->window(), IconicState);
            }
            activateWindow(event->window, CurrentTime, true);
        } else
            // use composition due to the transition effect
            activateWindow(event->window, CurrentTime, false);
    } else if (i && event->message_type == ATOM(_NET_CLOSE_WINDOW)) {
        
        i->closeWindow();
        // update stacking list to remove window from switcher
        if (i->propertyCache()->windowState() != IconicState)
            checkStacking(false);
        
    } else if (event->message_type == ATOM(WM_PROTOCOLS)) {
        if (event->data.l[0] == (long) ATOM(_NET_WM_PING)) {
            MCompositeWindow *ping_source = COMPOSITE_WINDOW(event->data.l[2]);
            if (ping_source) {
                bool was_hung = ping_source->status() == MCompositeWindow::Hung;
                ping_source->receivedPing(event->data.l[1]);
                Q_ASSERT(ping_source->status() != MCompositeWindow::Hung);
                Window managed = MDecoratorFrame::instance()->managedWindow();
                if (was_hung && ping_source->window() == managed
                    && !ping_source->needDecoration()) {
                    MDecoratorFrame::instance()->lower();
                    MDecoratorFrame::instance()->setManagedWindow(0);
                    MDecoratorFrame::instance()->setAutoRotation(false);
                    if(!ping_source->propertyCache()->hasAlpha()) 
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
                i->propertyCache()->setNetWmState(states.toList());
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
            if (d_item && i && i->status() != MCompositeWindow::Minimizing) {
                d_item->setZValue(i->zValue() - 1);

                Window lower, topmost = getTopmostApp();
                if (i->window() != topmost) {
                    /* Request from a background app.  Don't do anything,
                     * just make sure the states are not screwed. */
                    i->stopPing();
                    setWindowState(i->window(), IconicState);
                    return;
                }
                if (topmost)
                    lower = topmost;
                else
                    lower = event->window;
                setExposeDesktop(false);

                bool needComp = false;
                if (i->isDirectRendered() || d_item->isDirectRendered()) {
                    d_item->setVisible(true);
                    enableCompositing(true);
                    needComp = true;
                }

                // mark other applications on top of the desktop Iconified and
                // raise the desktop above them to make the animation end onto
                // the desktop
                int wi, lower_i = -1;
                for (wi = stacking_list.size() - 1; wi >= 0; --wi) {
                    Window w = stacking_list.at(wi);
                    if (!w)
                        continue;
                    
                    if (w == lower) {
                        lower_i = wi;
                        continue;
                    }
                    if (w == stack[DESKTOP_LAYER])
                        break;
                    MCompositeWindow *cw = COMPOSITE_WINDOW(w);
                    if (cw && cw->isMapped() && (cw->isAppWindow(true)
                        // mark transient dialogs Iconic too, so that
                        // restoreHandler() is called when they are maximised
                        || (cw->propertyCache()->windowTypeAtom()
                               == ATOM(_NET_WM_WINDOW_TYPE_DIALOG)
                            && getLastVisibleParent(cw->propertyCache()))) &&
                        // skip devicelock and screenlock windows
                        !cw->propertyCache()->dontIconify())
                        setWindowState(cw->window(), IconicState);
                }
                Q_ASSERT(lower_i > 0);
                safe_move(stacking_list, stacking_list.indexOf(stack[DESKTOP_LAYER]),
                                   lower_i - 1);

                // Delayed transition is only available on platforms
                // that have selective compositing. This is triggered
                // when windows are rendered off-screen
                i->iconify(i->propertyCache()->iconGeometry(), needComp);
                if (i->needDecoration())
                    i->startTransition();
                i->stopPing();
            }
            return;
        }

    // Handle root messages
    rootMessageEvent(event);
}

void MCompositeManagerPrivate::closeHandler(MCompositeWindow *window)
{    
    bool delete_sent = false;
    if ((window->propertyCache()->supportedProtocols().indexOf(
                                                               ATOM(WM_DELETE_WINDOW)) != -1) && window->status() != MCompositeWindow::Hung) {
        // send WM_DELETE_WINDOW message to the window that needs to close
        XEvent ev;
        memset(&ev, 0, sizeof(ev));
        
        ev.xclient.type = ClientMessage;
        ev.xclient.window = window->window();
        ev.xclient.message_type = ATOM(WM_PROTOCOLS);
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = ATOM(WM_DELETE_WINDOW);
        ev.xclient.data.l[1] = CurrentTime;
        
        XSendEvent(QX11Info::display(), window->window(), False,
                   NoEventMask, &ev);
        // FIXME: we should check if desktop is exposed or not
        setExposeDesktop(true);
        delete_sent = true;
    }
    
    if ((!delete_sent || window->status() == MCompositeWindow::Hung)) {
        kill_window(window->window());
        MDecoratorFrame::instance()->lower();
    }
    /* DO NOT deleteLater() this window yet because
       a) it can remove a mapped window from stacking_list
       b) delete can be ignored (e.g. "Do you want to exit?" dialog)
       c) _NET_WM_PID could be wrong (i.e. the window does not go away)
       d) we get UnmapNotify/DestroyNotify anyway when it _really_ closes */
}

void MCompositeManagerPrivate::lowerHandler(MCompositeWindow *window)
{    
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
    // set for roughSort() before raising duihome 
    setWindowState(window->window(), IconicState);

    if (stack[DESKTOP_LAYER]) {
        // redirect windows for the switcher
        enableCompositing();
        positionWindow(stack[DESKTOP_LAYER], true);
        dirtyStacking(false);
    }

    // Reset the global alpha on minimize
    reset_global_alpha();
}

void MCompositeManagerPrivate::restoreHandler(MCompositeWindow *window)
{
    Window last = getLastVisibleParent(window->propertyCache());
    MCompositeWindow *to_stack;
    if (last)
        to_stack = COMPOSITE_WINDOW(last);
    else
        to_stack = window;
    setWindowState(to_stack->window(), NormalState);

    // FIXME: call these for the whole transiency chain
    window->setNewlyMapped(false);
    if (to_stack != window)
        to_stack->setNewlyMapped(false);
    window->setUntransformed();
    if (window != to_stack)
        to_stack->setUntransformed();

    positionWindow(to_stack->window(), true);

    /* the animation is finished, compositing needs to be reconsidered */
    dirtyStacking(false);
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
    MWindowPropertyCache *pc = cw->propertyCache();

    if (pc->windowTypeAtom() != ATOM(_NET_WM_WINDOW_TYPE_DESKTOP) &&
        pc->windowTypeAtom() != ATOM(_NET_WM_WINDOW_TYPE_DOCK) &&
        !pc->isDecorator()) {
        setExposeDesktop(false);
        // if this is a transient window, raise the "parent" instead
        Window last = getLastVisibleParent(pc);
        MCompositeWindow *to_stack = cw;
        if (last) to_stack = COMPOSITE_WINDOW(last);
        // move it to the correct position in the stack
        positionWindow(to_stack->window(), true);
        // possibly set decorator
        if (cw == getHighestDecorated() || cw->status() == MCompositeWindow::Hung) {
            if (FULLSCREEN_WINDOW(cw)) {
                // fullscreen window has decorator above it during ongoing call
                // and when it's jammed
                MDecoratorFrame::instance()->setManagedWindow(cw, true);
                if (cw->status() == MCompositeWindow::Hung)
                    MDecoratorFrame::instance()->setOnlyStatusbar(false);
                else
                    MDecoratorFrame::instance()->setOnlyStatusbar(true);
            } else if (cw->status() == MCompositeWindow::Hung) {
                MDecoratorFrame::instance()->setManagedWindow(cw, true);
                MDecoratorFrame::instance()->setOnlyStatusbar(false);
            } else {
                MDecoratorFrame::instance()->setManagedWindow(cw);
                MDecoratorFrame::instance()->setOnlyStatusbar(false);
            }
        }
    } else if (pc->isDecorator()) {
        // if decorator crashes and reappears, stack it to bottom, raise later
        positionWindow(w, false);
    } else if (w == stack[DESKTOP_LAYER]) {
        setExposeDesktop(true);
        positionWindow(w, true);
    } else
        checkInputFocus(timestamp);

    /* do this after possibly reordering the window stack */
    if (disableCompositing)
        dirtyStacking(false);
}

void MCompositeManagerPrivate::displayOff(bool display_off)
{
    if (display_off) {
        // keep compositing to have synthetic events to obscure all windows
        enableCompositing(true);
        scene()->views()[0]->setUpdatesEnabled(false);
        /* stop pinging to save some battery */
        for (QHash<Window, MCompositeWindow *>::iterator it = windows.begin();
             it != windows.end(); ++it) {
             MCompositeWindow *i  = it.value();
             i->stopPing();
        }
    } else {
        scene()->views()[0]->setUpdatesEnabled(true);
        if (!possiblyUnredirectTopmostWindow())
            enableCompositing(false);
        /* start pinging again */
        pingTopmost();
    }
    dirtyStacking(true);  // VisibilityNotify generation
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
        dirtyStacking(false);
    } else {
        // remove decoration from fullscreen windows
        for (QHash<Window, MCompositeWindow *>::iterator it = windows.begin();
             it != windows.end(); ++it) {
            MCompositeWindow *i = it.value();
            if (FULLSCREEN_WINDOW(i) && i->needDecoration())
                i->setDecorated(false);
        }
        MDecoratorFrame::instance()->setOnlyStatusbar(false);
        dirtyStacking(false);
    }
}

void MCompositeManagerPrivate::setWindowState(Window w, int state)
{
    MCompositeWindow* i = COMPOSITE_WINDOW(w);
    if(i && i->propertyCache()->windowState() == state)
        return;
    else if (i)
        // cannot wait for the property change notification
        i->propertyCache()->setWindowState(state);

    CARD32 d[2];
    d[0] = state;
    d[1] = None;
    XChangeProperty(QX11Info::display(), w, ATOM(WM_STATE), ATOM(WM_STATE),
                    32, PropModeReplace, (unsigned char *)d, 2);
}

void MCompositeManager::setWindowState(Window w, int state)
{
   d->setWindowState(w, state);
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
    // Core non-subclassable events
    static const int damage_ev = damage_event + XDamageNotify;
    static int shape_event_base = 0;
    if (!shape_event_base) {
        int i;
        if (!XShapeQueryExtension(QX11Info::display(), &shape_event_base, &i))
            qWarning("%s: no Shape extension!", __func__);
    }

    if (event->type == damage_ev) {
        XDamageNotifyEvent *e = reinterpret_cast<XDamageNotifyEvent *>(event);
        damageEvent(e);
        return true;
    }
    if (event->type == shape_event_base + ShapeNotify) {
        XShapeEvent *ev = (XShapeEvent*)event;
        if (ev->kind == ShapeBounding && prop_caches.contains(ev->window)) {
            MWindowPropertyCache *pc = prop_caches.value(ev->window);
            pc->shapeRefresh();
            glwidget->update();
            checkStacking(true); // re-check visibility
        }
        return true;
    }
    
    if (processX11EventFilters(event, false))
        return true;
    
    bool ret = true;
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
        buttonEvent(&event->xbutton);
        // Qt needs to handle this event for the window frame buttons
        ret = false;
        break;
    case KeyPress:
    case KeyRelease:
        XAllowEvents(QX11Info::display(), ReplayKeyboard, event->xkey.time);
        keyEvent(&event->xkey); break;
    case ReparentNotify: 
        // TODO: handle if one of our top-levels is reparented away
        // Prevent this event from internally cascading inside Qt. Causing some
        // random crashes in XCheckTypedWindowEvent
        if (prop_caches.contains(((XReparentEvent*)event)->window)) {
            MWindowPropertyCache *pc =
                    prop_caches.value(((XReparentEvent*)event)->window);
            if (((XReparentEvent*)event)->parent != pc->parentWindow())
                pc->setParentWindow(((XReparentEvent*)event)->parent);
        }
        break;
    default:
        ret = false;
        break;
    }
    processX11EventFilters(event, true);
    return ret;
}

bool MCompositeManagerPrivate::processX11EventFilters(XEvent *event, bool after)
{
    if (!m_extensions.contains(event->type))
        return false;
    
    QList<MCompositeManagerExtension*> evlist = m_extensions.values(event->type);
    bool processed = false;
    if (after)
        for (int i = 0; i < evlist.size(); ++i) 
            evlist[i]->afterX11Event(event);
    else
        for (int i = 0; i < evlist.size(); ++i) 
            processed = evlist[i]->x11Event(event);
    
    return processed;
}

void MCompositeManagerPrivate::keyEvent(XKeyEvent* e)
{    
    if (e->state & Mod5Mask && e->keycode == switcher_key)
        exposeSwitcher();
}

void MCompositeManagerPrivate::buttonEvent(XButtonEvent* e)
{   
    if (e->type == ButtonRelease && e->window == home_button_win
        && e->x >= 0 && e->y >= 0 && e->x <= home_button_geom.width()
        && e->y <= home_button_geom.height())
        exposeSwitcher();
    else if (e->type == ButtonRelease && buttoned_win
             && e->window == close_button_win && e->x >= 0 && e->y >= 0
             && e->x <= close_button_geom.width()
             && e->y <= close_button_geom.height()) {
        XClientMessageEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = ClientMessage;
        ev.window = buttoned_win;
        ev.message_type = ATOM(WM_PROTOCOLS);
        ev.format = 32;
        ev.data.l[0] = ATOM(WM_DELETE_WINDOW);
        ev.data.l[1] = CurrentTime;
        XSendEvent(QX11Info::display(), buttoned_win, False, NoEventMask,
                   (XEvent*)&ev);
        return;
    }
    if (buttoned_win) {
        XButtonEvent ev = *e;
        // synthetise the event to the application below
        ev.window = buttoned_win;
        XSendEvent(QX11Info::display(), buttoned_win, False,
                   e->type == ButtonPress ? ButtonPressMask
                                          : ButtonReleaseMask, (XEvent*)&ev);
    }
}

QGraphicsScene *MCompositeManagerPrivate::scene()
{
    return watch;
}

void MCompositeManagerPrivate::redirectWindows()
{
    uint children = 0, i = 0;
    Window r, p, *kids = 0;

    XMapWindow(QX11Info::display(), xoverlay);
    QDesktopWidget *desk = QApplication::desktop();

    if (!XQueryTree(QX11Info::display(), desk->winId(),
                    &r, &p, &kids, &children)) {
        qCritical("XQueryTree failed");
        return;
    }
    int xres = ScreenOfDisplay(QX11Info::display(),
                               DefaultScreen(QX11Info::display()))->width;
    int yres = ScreenOfDisplay(QX11Info::display(),
                               DefaultScreen(QX11Info::display()))->height;
    for (i = 0; i < children; ++i)  {
        xcb_get_window_attributes_reply_t *attr;
        attr = xcb_get_window_attributes_reply(xcb_conn,
                     xcb_get_window_attributes(xcb_conn, kids[i]), 0);
        if (!attr || attr->_class == XCB_WINDOW_CLASS_INPUT_ONLY)
            continue;
        xcb_get_geometry_reply_t *geom;
        geom = xcb_get_geometry_reply(xcb_conn,
                        xcb_get_geometry(xcb_conn, kids[i]), 0);
        if (!geom)
            continue;
        // Pre-create MWindowPropertyCache for likely application windows
        if (localwin != kids[i] && (attr->map_state == XCB_MAP_STATE_VIEWABLE
            || (geom->width == xres && geom->height == yres))
            && !prop_caches.contains(kids[i])) {
            // attr and geom are freed later
            MWindowPropertyCache *p = new MWindowPropertyCache(kids[i],
                                                               attr, geom);
            if (!p->is_valid) {
                delete p;
                continue;
            }
            prop_caches[kids[i]] = p;
            p->setParentWindow(RootWindow(QX11Info::display(), 0));
        } else {
            free(attr); attr = 0;
            free(geom); geom = 0;
        }
        if (attr && attr->map_state == XCB_MAP_STATE_VIEWABLE &&
            localwin != kids[i] && geom &&
            (geom->width > 1 && geom->height > 1)) {
            // TODO: remove this bindWindow call -- shouldn't be needed
            MCompositeWindow* window = bindWindow(kids[i]);
            if (window) {
                window->setNewlyMapped(false);
                window->setVisible(true);
                // synthetise MapNotify, to use the usual code path for plugins
                XMapEvent e;
                e.type = MapNotify;
                e.serial = 0;
                e.send_event = True;
                e.event = RootWindow(QX11Info::display(), 0);
                e.window = kids[i];
                e.override_redirect = False;
                XSendEvent(QX11Info::display(),
                           RootWindow(QX11Info::display(), 0),
                           False, SubstructureNotifyMask, (XEvent*)&e);
            }
        }
    }
    if (kids)
        XFree(kids);
    scene()->views()[0]->setUpdatesEnabled(true);

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

void MCompositeManagerPrivate::removeWindow(Window w)
{
    // Item is already removed from scene when it is deleted
    int removed = 0;

    removed += windows_as_mapped.removeAll(w);
    removed += windows.remove(w);
    removed += stacking_list.removeAll(w);

    for (int i = 0; i < TOTAL_LAYERS; ++i)
        if (stack[i] == w) stack[i] = 0;

    if (removed > 0) updateWinList();
}

static QList<Window> orig_list;

// internal qSort comparator
// TODO: before this can replace checkStacking(), we need to handle at least
// the decorator, possibly also window groups and dock windows.
bool MCompositeManagerPrivate::compareWindows(Window w_a, Window w_b)
{
    MCompositeWindow *cw_a = MCompositeWindow::compositeWindow(w_a);
    MCompositeWindow *cw_b = MCompositeWindow::compositeWindow(w_b);
    MDecoratorFrame *deco = MDecoratorFrame::instance();
    
    if (!cw_a->propertyCache()) // a is already destroyed
        return true;
    if (!cw_b->propertyCache()) // b is already destroyed
        return false;

    // a is unused decorator?
    if (cw_a->propertyCache()->isDecorator() &&
        (!deco->managedClient() ||
         deco->managedClient()->propertyCache()->windowState() != NormalState))
        return true;
    // b is unused decorator?
    if (cw_b->propertyCache()->isDecorator() &&
        (!deco->managedClient() ||
         deco->managedClient()->propertyCache()->windowState() != NormalState))
        return false;
    // a iconified, or a is desktop and b not iconified?
    if (cw_a->propertyCache()->windowState() == IconicState ||
        (cw_a->propertyCache()->windowTypeAtom()
                                    == ATOM(_NET_WM_WINDOW_TYPE_DESKTOP)
         && cw_b->propertyCache()->windowState() == NormalState))
        return true;
    // b iconified or desktop?
    if (cw_b->propertyCache()->windowState() == IconicState
        || cw_b->propertyCache()->windowTypeAtom()
                                    == ATOM(_NET_WM_WINDOW_TYPE_DESKTOP))
        return false;
    // b has higher meego layer?
    if (cw_b->propertyCache()->meegoStackingLayer() >
                    cw_a->propertyCache()->meegoStackingLayer())
        return true;
    // b has lower meego layer?
    if (cw_b->propertyCache()->meegoStackingLayer() <
                    cw_a->propertyCache()->meegoStackingLayer())
        return false;
    Window trans_b = cw_b->lastVisibleParent();
    // b is a notification?
    if (cw_a->propertyCache()->meegoStackingLayer() < 6 && !trans_b &&
        cw_b->propertyCache()->windowTypeAtom()
                                 == ATOM(_NET_WM_WINDOW_TYPE_NOTIFICATION))
        return true;
    // b is an input or keep-above window?
    if (cw_a->propertyCache()->meegoStackingLayer() < 5 && !trans_b &&
        (cw_b->propertyCache()->windowTypeAtom()
                                  == ATOM(_NET_WM_WINDOW_TYPE_INPUT) ||
         cw_b->propertyCache()->isOverrideRedirect() ||
         cw_b->propertyCache()->netWmState().indexOf(ATOM(_NET_WM_STATE_ABOVE)) != -1))
        return true;
    // b is a system-modal dialog?
    if (cw_a->propertyCache()->meegoStackingLayer() < 4 &&
        MODAL_WINDOW(cw_b) && !trans_b &&
        cw_b->propertyCache()->windowTypeAtom()
                                 == ATOM(_NET_WM_WINDOW_TYPE_DIALOG))
        return true;
    // transiency relation?
    int trans_rel = transiencyRelation(cw_a, cw_b);
    // don't do any conclusions when trans_rel == 0
    if (trans_rel)
        return (trans_rel == 1) ? false : true;

    // the last resort: keep the old order
    // NOTE: this is required because w_a and w_b are not necessary in
    // the same order as in the stacking_list
    if (orig_list.indexOf(w_a) < orig_list.indexOf(w_b))
        return true;
    return false;
}

void MCompositeManagerPrivate::roughSort()
{
    orig_list = stacking_list;
    qSort(stacking_list.begin(), stacking_list.end(), compareWindows);
}

MCompositeWindow *MCompositeManagerPrivate::bindWindow(Window window)
{
    Display *display = QX11Info::display();

    // no need for StructureNotifyMask because of root's SubstructureNotifyMask
    XSelectInput(display, window, PropertyChangeMask);
    XShapeSelectInput(display, window, ShapeNotifyMask);
    XCompositeRedirectWindow(display, window, CompositeRedirectManual);

    MWindowPropertyCache *wpc;
    if (prop_caches.contains(window)) {
        wpc = prop_caches.value(window);
    } else {
        wpc = new MWindowPropertyCache(window);
        if (!wpc->is_valid) {
            delete wpc;
            return 0;
        }
        prop_caches[window] = wpc;
    }
    wpc->setIsMapped(true);
    MCompositeWindow *item = new MTexturePixmapItem(window, wpc);
    if (!item->isValid()) {
        item->deleteLater();
        return 0;
    }
    MWindowPropertyCache *pc = item->propertyCache();

    item->saveState();
    windows[window] = item;

    const XWMHints &h = pc->getWMHints();
    if ((h.flags & StateHint) && (h.initial_state == IconicState)) {
        setWindowState(window, IconicState);
        item->setZValue(-1);
    } else {
        setWindowState(window, NormalState);
        item->setZValue(pc->windowType());
    }

    QRect a = pc->realGeometry();
    int fs_i = pc->netWmState().indexOf(ATOM(_NET_WM_STATE_FULLSCREEN));
    if (fs_i == -1) {
        pc->setRequestedGeometry(QRect(a.x(), a.y(), a.width(), a.height()));
    } else {
        int xres = ScreenOfDisplay(display, DefaultScreen(display))->width;
        int yres = ScreenOfDisplay(display, DefaultScreen(display))->height;
        pc->setRequestedGeometry(QRect(0, 0, xres, yres));
    }

    if (!pc->isDecorator() && !pc->isOverrideRedirect()
        && windows_as_mapped.indexOf(window) == -1)
        windows_as_mapped.append(window);

    if (needDecoration(window, pc))
        item->setDecorated(true);

    item->updateWindowPixmap();

    int i = stacking_list.indexOf(window);
    if (i == -1)
        stacking_list.append(window);
    else
        safe_move(stacking_list, i, stacking_list.size() - 1);
    roughSort();

    addItem(item);
    
    if (pc->windowType() == MCompAtoms::INPUT) {
        dirtyStacking(false);
        return item;
    } else if (pc->windowType() == MCompAtoms::DESKTOP) {
        // just in case startup sequence changes
        stack[DESKTOP_LAYER] = window;
        connect(this, SIGNAL(inputEnabled()), item,
                SLOT(setUnBlurred()));
        dirtyStacking(false);
        return item;
    }

    item->manipulationEnabled(true);

    // the decorator got mapped. this is here because the compositor
    // could be restarted at any point
    if (pc->isDecorator() && !MDecoratorFrame::instance()->decoratorItem()) {
        // texture was already updated above
        item->setVisible(false);
        MDecoratorFrame::instance()->setDecoratorItem(item);
    } else if (!device_state->displayOff())
        item->setVisible(true);

    dirtyStacking(false);

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
    connect(item, SIGNAL(itemRestored(MCompositeWindow *)), SLOT(restoreHandler(MCompositeWindow *)));
    connect(item, SIGNAL(itemIconified(MCompositeWindow *)), SLOT(lowerHandler(MCompositeWindow *)));
    connect(item, SIGNAL(windowClosed(MCompositeWindow *)), SLOT(closeHandler(MCompositeWindow *)));


    // ping protocol
    connect(item, SIGNAL(windowHung(MCompositeWindow *)),
            SLOT(gotHungWindow(MCompositeWindow *)));
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
    dirtyStacking(false);
}

/*!
   Helper function to arrange arrange the order of the windows
   in the _NET_CLIENT_LIST_STACKING
*/
void MCompositeManagerPrivate::positionWindow(Window w, bool on_top)
{
    if (stacking_list.isEmpty())
        return;

    int wp = stacking_list.indexOf(w);
    if (wp == -1 || wp >= stacking_list.size())
        return;

    if (on_top) {
        //qDebug() << __func__ << "to top:" << w;
        setWindowState(w, NormalState);
        safe_move(stacking_list, wp, stacking_list.size() - 1);
        // needed so that checkStacking() finds the current application
        roughSort();
    } else {
        //qDebug() << __func__ << "to bottom:" << w;
        safe_move(stacking_list, wp, 0);
    }
    updateWinList();
}

void MCompositeManager::positionWindow(Window w,
                                       MCompositeManager::StackPosition pos)
{
    d->positionWindow(w, pos == MCompositeManager::STACK_TOP ? true : false);
}

void MCompositeManagerPrivate::enableCompositing(bool forced)
{
    if (compositing && !forced)
        return;

    if (!overlay_mapped)
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
    XFlush(QX11Info::display());
    compositing = true;
    // no delay: application does not need to redraw when maximizing it
    scene()->views()[0]->setUpdatesEnabled(true);
    glwidget->update();
    // At this point everything should be rendered off-screen
    emit compositingEnabled();
}

void MCompositeManagerPrivate::disableCompositing(ForcingLevel forced)
{
    if (device_state->displayOff() || MCompositeWindow::hasTransitioningWindow())
        return;
    if (!compositing && forced == NO_FORCED)
        return;
    
    // we could still have existing decorator on-screen.
    // ensure we don't accidentally disturb it
    for (QHash<Window, MCompositeWindow *>::iterator it = windows.begin();
         it != windows.end(); ++it) {
        MCompositeWindow *i  = it.value();
        if (i->propertyCache()->isDecorator())
            continue;
        if (i->windowVisible() && (i->propertyCache()->hasAlpha()
                                   || i->needDecoration())) 
            return;
    }

    scene()->views()[0]->setUpdatesEnabled(false);

    for (QHash<Window, MCompositeWindow *>::iterator it = windows.begin();
            it != windows.end(); ++it) {
        MCompositeWindow *tp  = it.value();
        // checks above fail. somehow decorator got in. stop it at this point
        if (!tp->propertyCache()->isDecorator() && !tp->isIconified()
            && !tp->propertyCache()->hasAlpha())
            ((MTexturePixmapItem *)tp)->enableDirectFbRendering();
        setWindowDebugProperties(it.key());
    }

    XUnmapWindow(QX11Info::display(), xoverlay);
    XFlush(QX11Info::display());

    if (MDecoratorFrame::instance()->decoratorItem())
        MDecoratorFrame::instance()->lower();
    
    compositing = false;
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
    dirtyStacking(false);
    MDecoratorFrame::instance()->raise();
    
    // We need to activate the window as well with instructions to decorate
    // the hung window. Above call just seems to mark the window as needing
    // decoration
    activateWindow(w->window(), CurrentTime, false);
}

void MCompositeManagerPrivate::exposeSwitcher()
{    
    Display* dpy =  QX11Info::display();

    for (QHash<Window, MCompositeWindow *>::iterator it = windows.begin();
         it != windows.end(); ++it) {
        MCompositeWindow *i  = it.value();
        if (!i->isAppWindow(true) ||
            i->propertyCache()->windowState() == IconicState ||
            // skip devicelock and screenlock windows
            i->propertyCache()->dontIconify() ||

            i->propertyCache()->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_DESKTOP))
            continue;
        
        XEvent e;
        e.xclient.type = ClientMessage;
        e.xclient.message_type = ATOM(WM_CHANGE_STATE);
        e.xclient.display = dpy;
        e.xclient.window = i->window();
        e.xclient.format = 32;
        e.xclient.data.l[0] = IconicState;
        e.xclient.data.l[1] = 0;
        e.xclient.data.l[2] = 0;
        e.xclient.data.l[3] = 0;
        e.xclient.data.l[4] = 0;
        XSendEvent(dpy, RootWindow(dpy, 0),
                   False, (SubstructureNotifyMask|SubstructureRedirectMask), &e);
    }
}

void MCompositeManagerPrivate::installX11EventFilter(long xevent, 
                                                     MCompositeManagerExtension* extension)
{  
    m_extensions.insert(xevent, extension);
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

const QHash<Window, MWindowPropertyCache*>& MCompositeManager::propCaches() const
{
    return d->prop_caches;
}

void MCompositeManager::setGLWidget(QGLWidget *glw)
{
    d->glwidget = glw;
}

QGLWidget *MCompositeManager::glWidget() const
{
    return d->glwidget;
}

QGraphicsScene *MCompositeManager::scene()
{
    return d->scene();
}

void MCompositeManager::prepareEvents()
{
    d->prepare();
}

void MCompositeManager::loadPlugins()
{
    d->loadPlugins();
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

void MCompositeManager::enableCompositing(bool forced)
{
    d->enableCompositing(forced);
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

bool MCompositeManager::isCompositing()
{
    return d->compositing;
}

void MCompositeManager::debug(const QString& d)
{
    const char* msg = d.toAscii();
    _log("%s\n", msg);
}

bool MCompositeManager::displayOff()
{
    return d->device_state->displayOff();
}

bool MCompositeManager::possiblyUnredirectTopmostWindow()
{
    return d->possiblyUnredirectTopmostWindow();
}

void MCompositeManager::exposeSwitcher()
{
    d->exposeSwitcher();
}

#include "mcompositemanager.moc"
