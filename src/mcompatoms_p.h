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

class MCompAtoms
{
public:

    // note that this enum is ordered and presents
    // the depth ordering of different window types
    enum Type {
        INVALID = 0,
        DESKTOP,
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
        // The following atoms are added to the _NET_SUPPORTED list.
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
        _NET_WM_USER_TIME_WINDOW,
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

        // MEEGO(TOUCH)-specific
        _MEEGOTOUCH_DECORATOR_WINDOW,
        _MEEGOTOUCH_GLOBAL_ALPHA,
        _MEEGOTOUCH_VIDEO_ALPHA,
        _MEEGO_STACKING_LAYER,
        _MEEGOTOUCH_DECORATOR_BUTTONS,
        _MEEGOTOUCH_CURRENT_APP_WINDOW,
        _MEEGOTOUCH_ALWAYS_MAPPED,
        _MEEGOTOUCH_DESKTOP_VIEW,
        _MEEGOTOUCH_CANNOT_MINIMIZE,
        _MEEGOTOUCH_MSTATUSBAR_GEOMETRY,
        _MEEGOTOUCH_CUSTOM_REGION,
        _MEEGOTOUCH_ORIENTATION_ANGLE,

#ifdef WINDOW_DEBUG
        _M_WM_INFO,
        _M_WM_WINDOW_ZVALUE,
        _M_WM_WINDOW_COMPOSITED_VISIBLE,
        _M_WM_WINDOW_COMPOSITED_INVISIBLE,
        _M_WM_WINDOW_DIRECT_VISIBLE,
        _M_WM_WINDOW_DIRECT_INVISIBLE,
#endif

        // The rest of the atoms are not added to _NET_SUPPORTED.
        END_OF_NET_SUPPORTED,

        // RROutput properties
        RROUTPUT_CTYPE = END_OF_NET_SUPPORTED,
        RROUTPUT_PANEL,
        RROUTPUT_ALPHA_MODE,
        RROUTPUT_GRAPHICS_ALPHA,
        RROUTPUT_VIDEO_ALPHA,

        ATOMS_TOTAL
    };
    static MCompAtoms *instance();
    Type windowType(Window w);
    bool isDecorator(Window w);
    int getPid(Window w);
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
