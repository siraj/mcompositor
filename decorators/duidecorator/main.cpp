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
#include <DuiApplication>
#include <DuiHomeButtonPanel>
#include <DuiEscapeButtonPanel>
#include <DuiNavigationBar>
#include <DuiScene>
#include <DuiSceneManager>

#include "duidecoratorwindow.h"

static QRect windowRectFromGraphicsItem(QGraphicsView &view, QGraphicsItem &item)
{
    return view.mapFromScene(
               item.mapToScene(
                   item.boundingRect()
               )
           ).boundingRect();
}

int main(int argc, char **argv)
{
    DuiApplication app(argc, argv);
    DuiScene scene;
    DuiDecoratorWindow window;
    window.setScene(&scene);
    DuiSceneManager sceneManager(&scene);
    DuiHomeButtonPanel homeButtonPanel;
    DuiEscapeButtonPanel escapeButtonPanel;

    QObject::connect(&homeButtonPanel, SIGNAL(buttonClicked()), &window,
                     SIGNAL(homeClicked()));
    QObject::connect(&escapeButtonPanel, SIGNAL(buttonClicked()), &window,
                     SIGNAL(escapeClicked()));

    DuiNavigationBar navigationBar;

    window.init(sceneManager);

    sceneManager.showWindowNow(&navigationBar);
    sceneManager.showWindowNow(&homeButtonPanel);
    sceneManager.showWindowNow(&escapeButtonPanel);

    QRegion region;
    region += windowRectFromGraphicsItem(window, navigationBar);
    region += windowRectFromGraphicsItem(window, homeButtonPanel);
    region += windowRectFromGraphicsItem(window, escapeButtonPanel);
    window.setInputRegion(region);

    window.show();

    return app.exec();
}
