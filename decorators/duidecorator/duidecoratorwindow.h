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
#ifndef DUIDECORATORWINDOW_H
#define DUIDECORATORWINDOW_H

#include <DuiWindow>

#include <X11/Xlib.h>
#ifdef HAVE_SHAPECONST
#include <X11/extensions/shapeconst.h>
#else
#include <X11/extensions/shape.h>
#endif

#include <QObject>

class DuiSceneManager;

class DuiDecoratorWindow : public DuiWindow
{
    Q_OBJECT

public:
    explicit DuiDecoratorWindow(QWidget *parent = 0);
    virtual ~DuiDecoratorWindow();

    void init(DuiSceneManager &sceneManager);

    /*!
     * \brief Sets the region of the window that can receive input events.
     *
     * Input events landing on the area outside this region will fall directly
     * to the windows below.
     */
    void setInputRegion(const QRegion &region);

signals:

    void homeClicked();
    void escapeClicked();

private:
    void fillXRectangle(XRectangle *xRect, const QRect &rect) const;
    void setSceneSize(DuiSceneManager &sceneManager);
    void setDuiDecoratorWindowProperty();

    Q_DISABLE_COPY(DuiDecoratorWindow);
};

#endif
