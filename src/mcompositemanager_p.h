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

#ifndef DUICOMPOSITEMANAGER_P_H
#define DUICOMPOSITEMANAGER_P_H

#include <QObject>
#include <QHash>
#include <QPixmap>
#include <QTimer>

#include <X11/Xutil.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xdamage.h>
#include <X11/Xlib-xcb.h>

class QGraphicsScene;
class QGLWidget;

class MCompositeScene;
class MSimpleWindowFrame;
class MCompAtoms;
class MCompositeWindow;
class MDeviceState;
class MWindowPropertyCache;
class MCompositeManagerExtension;

enum {
    INPUT_LAYER = 0,
    DOCK_LAYER,
    SYSTEM_LAYER,
    APPLICATION_LAYER,
    DESKTOP_LAYER,
    TOTAL_LAYERS
};

/*!
 * Internal implementation of MCompositeManager
 */

class MCompositeManagerPrivate: public QObject
{
    Q_OBJECT
public:
    enum ForcingLevel {
        NO_FORCED = 0,
        FORCED
    };

    MCompositeManagerPrivate(QObject *p);
    ~MCompositeManagerPrivate();

    static Window parentWindow(Window child);
    MCompositeWindow *bindWindow(Window w);
    QGraphicsScene *scene();

    void prepare();
    void loadPlugins();
    void activateWindow(Window w, Time timestamp,
		        bool disableCompositing = true);
    void updateWinList();
    void setWindowState(Window , int);
    void setWindowDebugProperties(Window w);
    void iconifyApps();
    void positionWindow(Window w, bool on_top);
    void addItem(MCompositeWindow *item);
    void damageEvent(XDamageNotifyEvent *);
    void destroyEvent(XDestroyWindowEvent *);
    void propertyEvent(XPropertyEvent *);
    void unmapEvent(XUnmapEvent *);
    void configureEvent(XConfigureEvent *);
    void configureRequestEvent(XConfigureRequestEvent *);
    void mapEvent(XMapEvent *);
    void mapRequestEvent(XMapRequestEvent *);
    void rootMessageEvent(XClientMessageEvent *);
    void clientMessageEvent(XClientMessageEvent *);
    void keyEvent(XKeyEvent*);
    void buttonEvent(XButtonEvent*);
    void installX11EventFilter(long xevent, MCompositeManagerExtension* extension);
    
    void redirectWindows();
    void showOverlayWindow(bool show);
    void enableRedirection(bool emit_signal);
    void setExposeDesktop(bool exposed);
    void checkStacking(bool force_visibility_check,
                       Time timestamp = CurrentTime);
    void checkInputFocus(Time timestamp = CurrentTime);
    void configureWindow(MCompositeWindow *cw, XConfigureRequestEvent *e);

    Window getTopmostApp(int *index_in_stacking_list = 0,
                         Window ignore_window = 0,
                         bool skip_always_mapped = false);
    Window getLastVisibleParent(MWindowPropertyCache *pc);

    bool possiblyUnredirectTopmostWindow();
    bool haveMappedWindow() const;
    bool isRedirected(Window window);
    bool x11EventFilter(XEvent *event);
    bool processX11EventFilters(XEvent *event, bool after);
    void removeWindow(Window w);
    bool needDecoration(Window w, MWindowPropertyCache *pc = 0);
    MCompositeWindow *getHighestDecorated(int *index = 0);
    
    void roughSort();
    void setCurrentApp(Window w, bool stacking_order_changed);
    bool raiseWithTransients(MWindowPropertyCache *pc,
                           int parent_idx, QList<int> *anewpos = NULL);
    MCompositeScene *watch;
    Window localwin, localwin_parent;
    Window xoverlay;
    Window prev_focus;
    Window close_button_win, home_button_win, buttoned_win;
    Window current_app;
    QRect home_button_geom, close_button_geom;

    static Window stack[TOTAL_LAYERS];

    MCompAtoms *atom;
    QGLWidget *glwidget;

    QList<Window> stacking_list;
    QList<Window> windows_as_mapped;

    QHash<Window, MCompositeWindow *> windows;
    struct FrameData {
        FrameData(): frame(0), parentWindow(0), mapped(false) {}
        MSimpleWindowFrame *frame;
        Window                parentWindow;
        bool mapped;
    };
    QHash<Window, FrameData> framed_windows;
    QHash<Window, QList<XConfigureRequestEvent*> > configure_reqs;
    QHash<Window, MWindowPropertyCache*> prop_caches;
    QMultiHash<int, MCompositeManagerExtension* > m_extensions;

    int damage_event;
    int damage_error;

    bool compositing;
    bool overlay_mapped;
    bool changed_properties;
    MDeviceState *device_state;

    // Indicates whether MCompositeManager::prepare() has finished.
    // Used by the destructor.
    bool prepared;

    xcb_connection_t *xcb_conn;

    // mechanism for lazy stacking
    QTimer stacking_timer;
    bool stacking_timeout_check_visibility;
    Time stacking_timeout_timestamp;
    void dirtyStacking(bool force_visibility_check, Time t = CurrentTime);
    void pingTopmost();

signals:
    void compositingEnabled();
    void currentAppChanged(Window w);

public slots:

    void gotHungWindow(MCompositeWindow *window, bool is_hung);
    void enableCompositing(bool forced = false);
    void disableCompositing(ForcingLevel forced = NO_FORCED);
    void showLaunchIndicator(int timeout);
    void hideLaunchIndicator();

    void lowerHandler(MCompositeWindow *window);
    void restoreHandler(MCompositeWindow *window);
    void closeHandler(MCompositeWindow *window);
    
    void onDesktopActivated(MCompositeWindow*);
    void exposeSwitcher();
    
    void displayOff(bool display_off);
    void callOngoing(bool call_ongoing);
    void stackingTimeout();
    void setupButtonWindows(Window topmost);
};

#endif
