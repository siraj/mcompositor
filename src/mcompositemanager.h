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

#ifndef DUICOMPOSITEMANAGER_H
#define DUICOMPOSITEMANAGER_H

#include <QApplication>
#include <QGLWidget>

class QGraphicsScene;
class MCompositeManagerPrivate;
class MCompAtoms;

/*!
 * MCompositeManager is responsible for managing window events.
 *
 * It catches and redirects appropriate windows to offscreen pixmaps and
 * creates a MTexturePixmapItem object from these windows and adds them
 * to a QGraphicsScene. The manager also ensures the items are updated
 * when their contents change and removes them from its control when they are
 * destroyed.
 *
 */
class MCompositeManager: public QApplication
{
    Q_OBJECT
public:

    /*!
     * Initializes the compositing manager
     *
     * \param argc number of arguments passed from the command line
     * \param argv argument of strings passed from the command line
     */
    MCompositeManager(int &argc, char **argv);

    /*!
     * Cleans up resources
     */
    ~MCompositeManager();

    /*! Prepare and start composite management. This function should get called
     * after the window of this compositor is created and mapped to the screen
     */
    void prepareEvents();

    /*! Specify the QGLWidget used by the QGraphicsView to draw the items on
     * the screen.
     *
     * \param glw The QGLWidget widget used in used by the scene's
     * QGraphicsView viewport
     */
    void setGLWidget(QGLWidget *glw);

    /*!
     * Reimplemented from QApplication::x11EventFilter() to catch X11 events
     */
    virtual bool x11EventFilter(XEvent *event);

    /*!
     * Returns the scene where the items are rendered
     */
    QGraphicsScene *scene();

    /*!
     * Specifies the toplevel window where the items are rendered. This window
     * will reparented to the composite overlay window to ensure the compositor
     * stays on top of all windows.
     *
     * \param window Window id of the toplevel window where the items are
     * rendered. Typically, this will be the window id of a toplevel
     * QGraphicsView widget where the items are drawn
     */
    void setSurfaceWindow(Qt::HANDLE window);

    /*!
     * Redirects and manages existing windows as composited items
     */
    void redirectWindows();

    /*!
     * Returns whether a Window is redirected or not
     *
     * \param w Window id of a window
     */
    bool isRedirected(Qt::HANDLE window);
    
    void topmostWindowsRaise();

public slots:
    void enableCompositing();
    void disableCompositing();

    /*! Invoked remotely by MRmiClient to show a launch indicator
     *
     * \param timeout seconds elapsed to hide the launch indicator in case
     * window does not yet appear.
     */
    void showLaunchIndicator(int timeout);
    void hideLaunchIndicator();
     
signals:
    void decoratorRectChanged(const QRect& rect);

private:
    MCompositeManagerPrivate *d;

    friend class MCompositeWindow;
    friend class MCompWindowAnimator;
};

#endif
