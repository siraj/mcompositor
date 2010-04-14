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
#include <MApplication>
#include <MHomeButtonPanel>
#include <MEscapeButtonPanel>
#include <MNavigationBar>
#include <MScene>
#include <MSceneManager>

#include "mdecoratorwindow.h"

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
    MApplication app(argc, argv);
    MScene scene;
    MDecoratorWindow window;
    window.setScene(&scene);
    MSceneManager sceneManager(&scene);
    MHomeButtonPanel homeButtonPanel;
    MEscapeButtonPanel escapeButtonPanel;

    QObject::connect(&homeButtonPanel, SIGNAL(buttonClicked()), &window,
                     SIGNAL(homeClicked()));
    QObject::connect(&escapeButtonPanel, SIGNAL(buttonClicked()), &window,
                     SIGNAL(escapeClicked()));

    MNavigationBar navigationBar;
    QObject::connect(&window, SIGNAL(windowTitleChanged(const QString&)), &navigationBar,
                     SLOT(setViewMenuDescription(const QString&)));

    window.init(sceneManager);

    sceneManager.appearSceneWindowNow(&navigationBar);
    sceneManager.appearSceneWindowNow(&homeButtonPanel);
    sceneManager.appearSceneWindowNow(&escapeButtonPanel);

    QRegion region;
    region += windowRectFromGraphicsItem(window, navigationBar);
    region += windowRectFromGraphicsItem(window, homeButtonPanel);
    region += windowRectFromGraphicsItem(window, escapeButtonPanel);
    window.setInputRegion(region);

    window.show();

    return app.exec();
}
