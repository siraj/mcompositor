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

#include <QtGui>
#include <QGLWidget>
#ifdef GLES2_VERSION
#include <duiglwidget.h>
#endif
#include "duicompositescene.h"
#include "duicompositemanager.h"

int main(int argc, char *argv[])
{
    DuiCompositeManager app(argc, argv);

    QGraphicsScene *scene = app.scene();
    QGraphicsView view(scene);

#ifndef DESKTOP_VERSION
    view.setUpdatesEnabled(false);
    view.setAutoFillBackground(false);
    view.setBackgroundBrush(Qt::NoBrush);
    view.setForegroundBrush(Qt::NoBrush);
#else
    view.setAttribute(Qt::WA_TranslucentBackground);
#endif
    view.setFrameShadow(QFrame::Plain);

    view.setWindowFlags(Qt::X11BypassWindowManagerHint);
    view.setAttribute(Qt::WA_NoSystemBackground);
#if QT_VERSION >= 0x040600
    view.move(-2, -2);
    view.setViewportUpdateMode(QGraphicsView::NoViewportUpdate);
    view.setOptimizationFlags(QGraphicsView::IndirectPainting);
#endif
    app.setSurfaceWindow(view.winId());

    view.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view.setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view.setSizePolicy(QSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed));
    view.setMinimumSize(QApplication::desktop()->width() + 2,
                        QApplication::desktop()->height() + 2);
    view.setMaximumSize(QApplication::desktop()->width() + 2,
                        QApplication::desktop()->height() + 2);

    QGLFormat fmt;
    fmt.setSamples(0);
    fmt.setSampleBuffers(false);

    // Temporary setback. In the future, we will bring back GLES rendering
    // directly instead of using this class
#ifdef GLES2_VERSION
    DuiGLWidget *imp = new DuiGLWidget(fmt);
    imp->setAutoFillBackground(false);
#else
    QGLWidget *imp = new QGLWidget(fmt);
#endif
    imp->setMinimumSize(QApplication::desktop()->width(),
                        QApplication::desktop()->height());
    imp->setMaximumSize(QApplication::desktop()->width(),
                        QApplication::desktop()->height());
    app.setGLWidget(imp);
    view.setViewport(imp);
    imp->makeCurrent();
    view.show();

    app.prepareEvents();
    app.redirectWindows();

    return app.exec();
}
