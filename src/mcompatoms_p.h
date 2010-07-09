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

#ifndef MCOMPATOMS_P_H
#define MCOMPATOMS_P_H

#include <QRectF>

class MCompAtoms
{
public:

    // note that this enum is ordered and presents
    // the depth ordering of different window types
    enum Type {
        DESKTOP = 0,
        NORMAL,
        DIALOG,
        NO_DECOR_DIALOG,
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
        WM_TRANSIENT_FOR,
        WM_HINTS,

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
        _NET_WM_WINDOW_TYPE_MENU,
        _NET_WM_STATE_ABOVE,
        _NET_WM_STATE_SKIP_TASKBAR,
        _NET_WM_STATE_FULLSCREEN,
        _NET_WM_STATE_MODAL,
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
        _MEEGOTOUCH_GLOBAL_ALPHA,
        _MEEGO_STACKING_LAYER,

#ifdef WINDOW_DEBUG
        _M_WM_INFO,
        _M_WM_WINDOW_ZVALUE,
        _M_WM_WINDOW_COMPOSITED_VISIBLE,
        _M_WM_WINDOW_COMPOSITED_INVISIBLE,
        _M_WM_WINDOW_DIRECT_VISIBLE,
        _M_WM_WINDOW_DIRECT_INVISIBLE,
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
    QVector<Atom> getAtomArray(Window w, Atom array_atom);
    unsigned int get_opacity_prop(Display *dpy, Window w, unsigned int def);
    double get_opacity_percent(Display *dpy, Window w, double def);
    int globalAlphaFromWindow(Window w);

    Atom getAtom(const unsigned int name);
    Atom getType(Window w);

    static Atom atoms[ATOMS_TOTAL];
    int cardValueProperty(Window w, Atom property);

private:
    explicit MCompAtoms();
    static MCompAtoms *d;

    Atom getAtom(Window w, Atoms atomtype);

    Display *dpy;
};

#define ATOM(t) MCompAtoms::instance()->getAtom(MCompAtoms::t)

#endif
