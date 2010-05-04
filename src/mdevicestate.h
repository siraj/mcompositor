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

#ifndef MDEVICESTATE_H
#define MDEVICESTATE_H

#include <contextproperty.h>
#ifdef GLES2_VERSION
#include <QtDBus>
#endif
#include <QObject>

/*!
 * This is a class listening to device state that is of interest to
 * MCompositeManager.
 */
class MDeviceState: public QObject
{
    Q_OBJECT

public:

    MDeviceState(QObject* parent = 0);
    ~MDeviceState();

    bool displayOff() const { return display_off; }
    bool ongoingCall() const { return ongoing_call; }

signals:

    void callStateChange(bool call_ongoing);
    void displayStateChange(bool display_off);

private slots:

#ifdef GLES2_VERSION
    void mceDisplayStatusIndSignal(QString mode);
#endif
    void callPropChanged();

private:

#ifdef GLES2_VERSION
    QDBusConnection *systembus_conn;
#endif
    ContextProperty *call_prop;
    bool display_off;
    bool ongoing_call;
};

#endif
