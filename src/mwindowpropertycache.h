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

#include <QRegion>
#include <X11/Xutil.h>
#include <X11/Xlib-xcb.h>
#include <xcb/render.h>
#include <xcb/shape.h>
#include <X11/extensions/shape.h>
#include "mcompatoms_p.h"

/*!
 * This is a class for caching window property values for a window.
 */
class MWindowPropertyCache: public QObject
{
    Q_OBJECT
public:

    /*! Construct a MWindowPropertyCache
     * \param window id to the window whose properties are cached
     */
    MWindowPropertyCache(Window window,
                         xcb_get_window_attributes_reply_t *attrs = 0,
                         xcb_get_geometry_reply_t *geom = 0);
    virtual ~MWindowPropertyCache();

    Atom windowTypeAtom() const { return window_type_atom; }

    void setWindowTypeAtom(Atom atom) { window_type_atom = atom; }

    void setRequestedGeometry(const QRect &rect) {
        req_geom = rect;
    }
    const QRect requestedGeometry() const {
        return req_geom;
    }

    // this is called on ConfigureNotify
    void setRealGeometry(const QRect &rect) {
        if (!is_valid)
            return;
        if (!xcb_real_geom)
            xcb_real_geom = xcb_get_geometry_reply(xcb_conn,
                                                   xcb_real_geom_cookie, 0);
        real_geom_valid = true;
        real_geom = rect;

        // shape needs to be refreshed in case it was the default value
        // (i.e. the same as geometry), because there is no ShapeNotify
        if ((shape_rects_valid && QRegion(real_geom) != shape_region)
            || !shape_rects_valid)
            shapeRefresh();
    }
    const QRect realGeometry() {
        if (is_valid && !xcb_real_geom) {
            xcb_real_geom = xcb_get_geometry_reply(xcb_conn,
                                                   xcb_real_geom_cookie, 0);
            if (xcb_real_geom && !real_geom_valid) {
                real_geom = QRect(xcb_real_geom->x, xcb_real_geom->y,
                                  xcb_real_geom->width, xcb_real_geom->height);
                real_geom_valid = true;
            }
        }
        return real_geom;
    }
    const QRegion &shapeRegion();
    void shapeRefresh() {
        if (!is_valid)
            return;
        if (!shape_rects_valid)
            shapeRegion();
        shape_rects_valid = false;
        xcb_shape_rects_cookie = xcb_shape_get_rectangles(xcb_conn, window,
                                                          ShapeBounding);
    }

    Window winId() const { return window; }
    Window parentWindow() const { return parent_window; }
    void setParentWindow(Window w) { parent_window = w; }

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
    const QList<Atom>& netWmState();

    /*!
     * Returns list of transients of the window.
     */
    const QList<Window>& transientWindows() const { return transients; }

    // used to set the atom list now, for immediate effect in e.g. stacking
    void setNetWmState(const QList<Atom>& s) {
        if (!is_valid)
            return;
        if (!net_wm_state_valid)
            // receive the obsolete query
            netWmState();
        net_wm_state_valid = true;
        net_wm_state = s;
    }

    /*!
     * Returns true if this window has received MapRequest but not
     * the MapNotify yet.
     */
    bool beingMapped() const { return being_mapped; }
    void setBeingMapped(bool s) { being_mapped = s; }

    bool isMapped() const {
        if (!is_valid || !attrs)
            return false;
        return attrs->map_state == XCB_MAP_STATE_VIEWABLE;
    }

    void setIsMapped(bool s) {
        if (!is_valid || !attrs)
            return;
        // a bit ugly but avoids a round trip to X...
        if (s)
            attrs->map_state = XCB_MAP_STATE_VIEWABLE;
        else
            attrs->map_state = XCB_MAP_STATE_UNMAPPED;
    }

    /*!
     * Returns the first cardinal of WM_STATE of this window
     */
    int windowState();

    void setWindowState(int state) { window_state = state; }
    
    /*!
     * Returns whether override_redirect flag was in XWindowAttributes at
     * object creation time.
     */
    bool isOverrideRedirect() const {
        if (!is_valid || !attrs)
            return false;
        return attrs->override_redirect;
    }
    bool isInputOnly() const {
        if (!is_valid || !attrs)
            return false;
        return attrs->_class == XCB_WINDOW_CLASS_INPUT_ONLY;
    }

    const XWMHints &getWMHints();

    const xcb_get_window_attributes_reply_t* windowAttributes() const {
            return attrs; };

    const QRectF &iconGeometry();

    const QRect &homeButtonGeometry();
    const QRect &closeButtonGeometry();

    /*!
     * Returns value of _MEEGO_STACKING_LAYER. The value is between [0, 6].
     */
    unsigned int meegoStackingLayer();

    /*!
     * Called on PropertyNotify for this window.
     * Returns true if we should re-check stacking order.
     */
    bool propertyEvent(XPropertyEvent *e);

    MCompAtoms::Type windowType();

    bool hasAlpha();
    bool isDecorator();
    int globalAlpha();
    int videoGlobalAlpha();
    bool is_valid;

    static void set_xcb_connection(xcb_connection_t *c) {
        MWindowPropertyCache::xcb_conn = c;
    }

signals:
    void iconGeometryUpdated();

private:
    int alphaValue(xcb_get_property_cookie_t c);
    void buttonGeometryHelper();

    Atom window_type_atom;
    Window transient_for;
    QList<Window> transients;
    QList<Atom> wm_protocols;
    bool wm_protocols_valid;
    bool icon_geometry_valid;
    bool decor_buttons_valid;
    bool shape_rects_valid;
    bool real_geom_valid;
    bool net_wm_state_valid;
    bool wm_state_query;
    QRectF icon_geometry;
    int has_alpha;
    int global_alpha;
    int video_global_alpha;
    int is_decorator;
    QList<Atom> net_wm_state;
    QRect req_geom, real_geom;
    QRect home_button_geom, close_button_geom;
    XWMHints *wmhints;
    xcb_get_window_attributes_reply_t *attrs;
    int meego_layer, window_state;
    MCompAtoms::Type window_type;
    Window window, parent_window;
    bool being_mapped;
    // geometry is requested only once in the beginning, after that, we
    // use ConfigureNotifys to update the size through setRealGeometry()
    xcb_get_geometry_reply_t *xcb_real_geom;
    xcb_get_geometry_cookie_t xcb_real_geom_cookie;
    xcb_get_property_cookie_t xcb_transient_for_cookie;
    xcb_get_property_cookie_t xcb_meego_layer_cookie;
    xcb_get_property_cookie_t xcb_is_decorator_cookie;
    xcb_get_property_cookie_t xcb_window_type_cookie;
    xcb_get_property_cookie_t xcb_decor_buttons_cookie;
    xcb_get_property_cookie_t xcb_wm_protocols_cookie;
    xcb_get_property_cookie_t xcb_wm_state_cookie;
    xcb_get_property_cookie_t xcb_wm_hints_cookie;
    xcb_get_property_cookie_t xcb_icon_geom_cookie;
    xcb_get_property_cookie_t xcb_global_alpha_cookie;
    xcb_get_property_cookie_t xcb_video_global_alpha_cookie;
    xcb_get_property_cookie_t xcb_net_wm_state_cookie;
    xcb_render_query_pict_formats_cookie_t xcb_pict_formats_cookie;
    xcb_shape_get_rectangles_cookie_t xcb_shape_rects_cookie;
    QRegion shape_region;

    static xcb_connection_t *xcb_conn;
};

#endif
