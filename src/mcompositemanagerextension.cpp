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

#include "mcompositemanager.h"
#include "mcompositemanager_p.h"
#include "mcompositemanagerextension.h"

MCompositeManagerExtension::MCompositeManagerExtension(QObject *parent)
    :QObject(parent)
{
    MCompositeManager *p = (MCompositeManager *) qApp;
    connect(p->d, SIGNAL(currentAppChanged(Window)), SLOT(q_currentAppChanged(Window)) );
}

MCompositeManagerExtension::~MCompositeManagerExtension()
{
}

void MCompositeManagerExtension::q_currentAppChanged(Window window)
{
    emit currentAppChanged(window);
}

void MCompositeManagerExtension::listenXEventType(long XEventType)
{
    MCompositeManager *p = (MCompositeManager *) qApp;
    p->d->installX11EventFilter(XEventType, this);
}

Qt::HANDLE MCompositeManagerExtension::desktopWindow()
{    
    MCompositeManager *p = (MCompositeManager *) qApp;
    return p->d->stack[DESKTOP_LAYER];
}

Qt::HANDLE MCompositeManagerExtension::currentAppWindow()
{
    MCompositeManager *p = (MCompositeManager *) qApp;
    return p->d->current_app;
}

bool MCompositeManagerExtension::windowIconified(MCompositeWindow* window, 
                                                 bool deferred)
{
    Q_UNUSED(window)
    Q_UNUSED(deferred)
        
    return false;
}

bool MCompositeManagerExtension::windowRestored(MCompositeWindow* window, 
                                                bool deferred)
{
    Q_UNUSED(window)
    Q_UNUSED(deferred)

    return false;
}

bool MCompositeManagerExtension::windowShown(MCompositeWindow* window)
{
    Q_UNUSED(window)

    return false;
}

bool MCompositeManagerExtension::windowClosed(MCompositeWindow* window)
{
    Q_UNUSED(window)

    return false;
}
