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

#ifndef DUICOMPOSITEWINDOW_H
#define DUICOMPOSITEWINDOW_H

#include <QGraphicsItem>
#include <QtOpenGL>
#include <X11/Xutil.h>

class DuiCompWindowAnimator;

/*!
 * This is the base class for composited window items. It provided general
 * functions to animate and save the state of individual items on the
 * composited desktop. Also provides functionality for thumbnailed view of
 * the contents.
 */
class DuiCompositeWindow: public QObject, public QGraphicsItem
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
    enum WindowType {
        Unknown = 0,
        Normal,
        Transient
    };

    /*! Construct a DuiCompositeWindow
     *
     * \param window id to the window represented by this item
     * \param item QGraphicsItem parent, defaults to none
     */
    DuiCompositeWindow(Qt::HANDLE window, QGraphicsItem *item = 0);

    /*! Destroys this DuiCompositeWindow and frees resources
     *
     */
    virtual ~DuiCompositeWindow();

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

    WindowType windowType() const { return window_type; }
    
    void setWindowType(WindowType type) {
        window_type = type;
    }

    Atom windowTypeAtom() const { return window_type_atom; }

    void setWindowTypeAtom(Atom atom) {
        window_type_atom = atom;
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
    
    QRect originalGeometry() const { return origGeometry; }

    static DuiCompositeWindow *compositeWindow(Qt::HANDLE window);

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
     Returns true if the window corresponding to the offscreen pixmap has an
     is an override-redirect window, otherwise returns false.
    */
    virtual bool isOverrideRedirect() const = 0;

    /*!
     * Sets the width and height if the item
     */
    virtual void resize(int w, int h) = 0;

    static bool isTransitioning();

public slots:

    void startTransition();
    void manipulationEnabled(bool isEnabled);
    void setUnBlurred();
    void setBlurred(bool);

    void delayShow(int delay = 300);

private slots:

    /*! Called internally to update how this item looks when the transitions
      are done
     */
    void finalizeState();

    void pingTimeout();
    void pingWindow();
    void windowTransitioning();
    void windowSettled();
    void q_delayShow();
    void q_itemRestored();

signals:
    /*!
     * Emitted if this window is hung
     */
    void windowHung(DuiCompositeWindow *window);

    /*!
     * Emitted for every ping signal sent to the window
     */
    void pingTriggered(DuiCompositeWindow *window);

    void acceptingInput();
    void visualized(bool);

    /*! Emmitted when this window gets restored from an iconified state */
    void itemRestored(DuiCompositeWindow *window);
    /*! Emmitted just after this window gets iconified  */
    void itemIconified(DuiCompositeWindow *window);

protected:

    virtual void hoverEnterEvent(QGraphicsSceneHoverEvent *);
    virtual void hoverLeaveEvent(QGraphicsSceneHoverEvent *);
    virtual void mouseReleaseEvent(QGraphicsSceneMouseEvent *);
    virtual QVariant itemChange(GraphicsItemChange change, const QVariant &value);

private:
    bool thumb_mode;
    DuiCompWindowAnimator *anim;
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
    bool requestzval;
    ProcessStatus process_status;
    bool need_decor;
    bool is_decorator;
    bool window_visible;
    WindowType window_type;
    Atom window_type_atom;
    Window transient_for;
    bool wants_focus;
    QList<Atom> wm_protocols;
    bool window_obscured;
    bool window_mapped;

    static bool window_transitioning;

    // location of this window's icon
    QRectF iconGeometry;
    QPointF origPosition;
    // _NET_WM_STATE_FULLSCREEN
    QRect origGeometry;

    // Main ping timer
    QTimer *t_ping;
    Qt::HANDLE win_id;
};

#endif
