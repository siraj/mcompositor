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

#ifndef DUICOMPOSITEWINDOW_H
#define DUICOMPOSITEWINDOW_H

#include <QGraphicsItem>
#include <QtOpenGL>
#include <X11/Xutil.h>
#include "mcompatoms_p.h"

class MCompWindowAnimator;

/*!
 * This is the base class for composited window items. It provided general
 * functions to animate and save the state of individual items on the
 * composited desktop. Also provides functionality for thumbnailed view of
 * the contents.
 */
class MCompositeWindow: public QObject, public QGraphicsItem
{
    Q_OBJECT
#if QT_VERSION >= 0x040600
    Q_INTERFACES(QGraphicsItem)
#endif

public:

    enum ProcessStatus {
        NORMAL = 0,
        HUNG
    };
    enum IconifyState {
        NoIconifyState = 0,
        ManualIconifyState,
        TransitionIconifyState
    };

    /*! Construct a MCompositeWindow
     *
     * \param window id to the window represented by this item
     * \param windowType internal window type representation of this window
     * \param item QGraphicsItem parent, defaults to none
     */
    MCompositeWindow(Qt::HANDLE window, MCompAtoms::Type windowType,
                     QGraphicsItem *item = 0);

    /*! Destroys this MCompositeWindow and frees resources
     *
     */
    virtual ~MCompositeWindow();

    /*!
     * Overriden QObject::deleteLater()
     */
    void deleteLater();

    /*!
     * Saves the global state of this item. Possibly transformations and
     * location
     */
    void saveState();

    /*!
     * Saves the local state of this item. Possibly transformations and
     * location
     */
    void localSaveState();

    /*!
     * Sets a scale a point
     */
    void setScalePoint(qreal from, qreal to);

    /*!
     * Iconify window with animation. If deferAnimation is set to true
     * call startTransition() manually.
    */
    void iconify(const QRectF &iconGeometry = QRectF(),
                 bool deferAnimation = false);

    /*!
     * Sets whether or not the window is obscured and generates the
     * proper VisibilityNotify for the window.
     */
    void setWindowObscured(bool obscured, bool no_notify = false);

    /*!
     * Returns whether this item is iconified or not
     */
    bool isIconified() const;

    /*!
     * Destroy window with animation
     */
    void prettyDestroy();

    /*!
     * Turns the contents displayed in this item into a small thimbnail view
     *
     * \param mode Set to thumnail view if true; otherwise set as normal view
     */
    void setThumbMode(bool mode);

    /*!
     * Returns whether this item is scaled or not
     */
    bool isScaled() const;

    /*!
     * Set this item as scaled
     *
     * \param s Set to scaled if true; otherwise set as normal size
     */
    void setScaled(bool s);

    /*!
     * Request a Z value for this item. Useful if this window is still animating
     * and setting the zValue prevents the animation from being displayed.
     * This function defers the setting of the zValue until the animation for
     * this item is done.
     *
     * \param zValue the requested z-value
     *
     * TODO: this might be redundant. Move to overriden setZValue in the future
     */
    void requestZValue(int zValue);

    /*!
     * Overriden setVisible so we have complete control of the item
     */
    void setVisible(bool visible);

    /*!
     * Own visibility getter. We can't rely on QGraphicsItem's visibility
     * information because of rendering optimizations.
     */
    bool windowVisible() const;

    /*!
     * Set iconify status manually.
     */
    void setIconified(bool iconified);

    /*!
     * Returns how this window was iconified.
     */
    IconifyState iconifyState() const;

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
     * Returns true if this window needs a decoration
     */
    bool needDecoration() const;

    /*!
     * Sets whether this window is decorated or not
     */
    void setDecorated(bool decorated);

    /*!
     * Returns whether this item is the decorator window or not.
     */
    bool isDecorator() const;

    /*!
     * Sets whether or not this window is a decorator window.
     */
    void setIsDecorator(bool decorator);

    void setIsMapped(bool mapped) { window_mapped = mapped; }
    bool isMapped() const { return window_mapped; }
    
    void setNewlyMapped(bool newlyMapped) { newly_mapped = newlyMapped; }
    bool isNewlyMapped() const { return newly_mapped; }

    /*!
     * Restores window with animation. If deferAnimation is set to true
     * call startTransition() manually.
    */
    void restore(const QRectF &iconGeometry = QRectF(),
                 bool deferAnimation = false);

    /*!
     * Overrides QGraphicsItem::update() so we have complete control of item
     * updates.
     */
    void update();

    bool blurred();

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
     * Returns if window is hung or not.
     */
    ProcessStatus status() const;

    // For _NET_WM_PING abstraction
    void startPing();
    void stopPing();
    void receivedPing(ulong timeStamp);
    void setClientTimeStamp(ulong timeStamp);

    // Window geometry
    void setOriginalGeometry(const QRect geometry) {
        origGeometry = geometry;
    }

    /*!
     * Returns the WM_STATE of this window
     */
    int windowState() const { return window_state; }

    void setWindowState(int state) { window_state = state; }
    
    QRect originalGeometry() const { return origGeometry; }

    static MCompositeWindow *compositeWindow(Qt::HANDLE window);

    Qt::HANDLE window() const;

    virtual void windowRaised() = 0;

    /*!
     * Ensures that the corresponding texture reflects the contents of the
     * associated pixmap and schedules a redraw of this item.
     */
    virtual void updateWindowPixmap(XRectangle *rects = 0, int num = 0) = 0;

    /*!
     * Creates the pixmap id and saves the offscreen buffer that represents
     * this window
     *
     * \param renew Set to true if the window was just mapped or resized. This
     * will update the offscreen backing store.
     */
    virtual void saveBackingStore(bool renew = false) = 0;

    /*!
      Clears the texture that is associated with the offscreen pixmap
     */
    virtual void clearTexture() = 0;

    /*!
      Returns true if the window corresponding to the offscreen pixmap
      is rendering directly to the framebuffer, otherwise return false.
     */
    virtual bool isDirectRendered() const = 0;

    /*!
      Returns true if the window corresponding to the offscreen pixmap has an
      alpha channel, otherwise returns false.
     */
    virtual bool hasAlpha() const = 0;

    /*!
     * Sets the width and height if the item
     */
    virtual void resize(int w, int h) = 0;

    static bool isTransitioning();

    /*!
     * Tells if this window is transitioning.
     */
    bool thisIsTransitioning() const { return is_transitioning; }
    
    /*!
     * Returns whether this object represents a valid (i.e. viewable) window
     */
    bool isValid() const { return is_valid; }

    /*!
     * Returns whether override_redirect flag was in XWindowAttributes at
     * object creation time.
     */
    bool isOverrideRedirect() const { return attrs->override_redirect; }

    const XWMHints &getWMHints();

    const XWindowAttributes* windowAttributes() const { return attrs; };

    /*!
     * Returns value of _MEEGO_STACKING_LAYER. The value is between [0, 6].
     */
    unsigned int meegoStackingLayer();

    /*!
     * Called on PropertyNotify for this window.
     * Returns true if we should re-check stacking order.
     */
    bool propertyEvent(XPropertyEvent *e);

    /*! 
     * Returns whether this is an application window
     */
    bool isAppWindow(bool include_transients = false);

    void setClosing(bool closing) { is_closing = closing; }
    bool isClosing() const { return is_closing; }

public slots:

    void startTransition();
    void manipulationEnabled(bool isEnabled);
    void setUnBlurred();
    void setBlurred(bool);
    void fadeIn();
    void fadeOut();

private slots:

    /*! Called internally to update how this item looks when the transitions
      are done
     */
    void finalizeState();

    void pingTimeout();
    void pingWindow();
    void windowTransitioning();
    void q_delayShow();
    void q_itemRestored();

signals:
    /*!
     * Emitted if this window is hung
     */
    void windowHung(MCompositeWindow *window);

    /*!
     * Emitted for every ping signal sent to the window
     */
    void pingTriggered(MCompositeWindow *window);

    void acceptingInput();
    void visualized(bool);

    /*! Emitted when this window gets restored from an iconified state */
    void itemRestored(MCompositeWindow *window);
    /*! Emitted just after this window gets iconified  */
    void itemIconified(MCompositeWindow *window);
    /*! Emitted when desktop is raised */
    void desktopActivated(MCompositeWindow *window);

protected:

    virtual void hoverEnterEvent(QGraphicsSceneHoverEvent *);
    virtual void hoverLeaveEvent(QGraphicsSceneHoverEvent *);
    virtual void mouseReleaseEvent(QGraphicsSceneMouseEvent *);
    virtual QVariant itemChange(GraphicsItemChange change, const QVariant &value);
private:
    bool thumb_mode;
    MCompWindowAnimator *anim;
    qreal scalefrom;
    qreal scaleto;
    bool scaled;
    int zval;
    ulong ping_client_timestamp;
    ulong ping_server_timestamp;
    bool blur;
    bool iconified;
    bool iconified_final;
    IconifyState iconify_state;
    bool destroyed;
    ProcessStatus process_status;
    bool need_decor;
    bool is_decorator;
    bool window_visible;
    Atom window_type_atom;
    Window transient_for;
    QList<Atom> wm_protocols;
    bool wm_protocols_valid;
    QList<Atom> net_wm_state;
    bool window_obscured;
    bool window_mapped;
    bool is_valid;
    bool newly_mapped;
    bool is_closing;
    bool is_transitioning;
    QRect req_geom, real_geom;
    XWMHints *wmhints;
    XWindowAttributes *attrs;
    int meego_layer;

    static bool window_transitioning;

    // location of this window's icon
    QRectF iconGeometry;
    QPointF origPosition;
    // _NET_WM_STATE_FULLSCREEN
    QRect origGeometry;

    // Main ping timer
    QTimer *t_ping;
    Qt::HANDLE win_id;
    int  window_state;

    friend class MTexturePixmapPrivate;
};

#endif
