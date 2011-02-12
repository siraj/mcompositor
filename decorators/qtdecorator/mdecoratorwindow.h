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
#ifndef MDECORATORWINDOW_H
#define MDECORATORWINDOW_H

#include <MWindow>
#include <MHomeButtonPanel>
#include <MEscapeButtonPanel>
#include <MNavigationBar>
#include <MMessageBox>
#include <mstatusbar.h>
#include <mlocale.h>

#include <X11/Xlib.h>
#ifdef HAVE_SHAPECONST
#include <X11/extensions/shapeconst.h>
#else
#include <X11/extensions/shape.h>
#endif

#include <QObject>

class MSceneManager;
class MDecorator;

class MDecoratorWindow : public MWindow
{
    Q_OBJECT

public:
    explicit MDecoratorWindow(QWidget *parent = 0);
    virtual ~MDecoratorWindow();
    
    const QRect availableClientRect() const;
    bool x11Event(XEvent *e);
    void setWindowTitle(const QString& title);
    void setOnlyStatusbar(bool mode, bool temporary = false);
    /*!
     * \brief Sets the region of the window that can receive input events.
     *
     * Input events landing on the area outside this region will fall directly
     * to the windows below.
     */
    void setInputRegion();
    void managedWindowChanged(Qt::HANDLE);
    void showQueryDialog(bool visible);

protected:
    virtual void closeEvent(QCloseEvent * event );

private slots:

    void screenRotated(const M::Orientation &orientation);
    void yesButtonClicked();
    void noButtonClicked();

signals:

    void homeClicked();
    void escapeClicked();

private:
    void setSceneSize();
    void setMDecoratorWindowProperty();

    MHomeButtonPanel *homeButtonPanel;
    MEscapeButtonPanel *escapeButtonPanel;
    MNavigationBar *navigationBar;
    MStatusBar *statusBar;
    MMessageBox *messageBox;
    Window managed_window;
    QRect availableRect; // available area for the managed window
    bool only_statusbar, requested_only_statusbar;
    Atom onlyStatusbarAtom, managedWindowAtom;
    MDecorator *d;
    MLocale locale;

    Q_DISABLE_COPY(MDecoratorWindow);
};

#endif
