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

#ifndef MWINDOWPROPERTYCACHE_H
#define MWINDOWPROPERTYCACHE_H

#include <X11/Xutil.h>
#include "mcompatoms_p.h"

/*!
 * This is a class for caching window property values for a window.
 */
class MWindowPropertyCache
{
public:

    /*! Construct a MWindowPropertyCache
     * \param window id to the window whose properties are cached
     */
    MWindowPropertyCache(Window window, XWindowAttributes *attrs = 0);
    virtual ~MWindowPropertyCache();

    Atom windowTypeAtom() const { return window_type_atom; }

    void setWindowTypeAtom(Atom atom) { window_type_atom = atom; }

    void setRequestedGeometry(const QRect &rect) {
        req_geom = rect;
    }
    const QRect requestedGeometry() const {
        return req_geom;
    }

    void setRealGeometry(const QRect &rect) {
        real_geom = rect;
    }
    const QRect realGeometry() const {
        return real_geom;
    }

    /*!
     * Returns value of TRANSIENT_FOR property.
     */
    Window transientFor();

    /*!
     * Returns true if we should give focus to this window.
     */
    bool wantsFocus();

    /*!
     * Returns the window group or 0.
     */
    XID windowGroup();

    /*!
     * Returns list of WM_PROTOCOLS of the window.
     */
    const QList<Atom>& supportedProtocols();

    /*!
     * Returns list of _NET_WM_STATE of the window.
     */
    const QList<Atom>& netWmState() const { return net_wm_state; }

    void setNetWmState(const QList<Atom>& s) {
            net_wm_state = s;
    }

    /*!
     * Returns true if this window has received MapRequest but not
     * the MapNotify yet.
     */
    bool beingMapped() const { return being_mapped; }
    void setBeingMapped(bool s) { being_mapped = s; }

    bool isMapped() const { return attrs->map_state == IsViewable; }

    void setIsMapped(bool s) {
        // a bit ugly but avoids a round trip to X...
        if (s)
            attrs->map_state = IsViewable;
        else
            attrs->map_state = IsUnmapped;
    }

    /*!
     * Returns the WM_STATE of this window
     */
    int windowState() const { return window_state; }

    void setWindowState(int state) { window_state = state; }
    
    /*!
     * Returns whether override_redirect flag was in XWindowAttributes at
     * object creation time.
     */
    bool isOverrideRedirect() const { return attrs->override_redirect; }

    const XWMHints &getWMHints();

    const XWindowAttributes* windowAttributes() const { return attrs; };

    const QRectF &iconGeometry();

    /*!
     * Returns value of _MEEGO_STACKING_LAYER. The value is between [0, 6].
     */
    unsigned int meegoStackingLayer();

    /*!
     * Called on PropertyNotify for this window.
     * Returns true if we should re-check stacking order.
     */
    bool propertyEvent(XPropertyEvent *e);

    MCompAtoms::Type windowType() const { return window_type; }

    bool hasAlpha() const { return has_alpha; }

    bool isDecorator() const { return is_decorator; }

    int globalAlpha();

    bool is_valid;

private:
    Atom window_type_atom;
    Window transient_for;
    QList<Atom> wm_protocols;
    bool wm_protocols_valid;
    bool icon_geometry_valid;
    QRectF icon_geometry;
    bool has_alpha;
    int global_alpha;
    bool is_decorator;
    QList<Atom> net_wm_state;
    QRect req_geom, real_geom;
    XWMHints *wmhints;
    XWindowAttributes *attrs;
    int meego_layer, window_state;
    MCompAtoms::Type window_type;
    Window window;
    bool being_mapped;
};

#endif
