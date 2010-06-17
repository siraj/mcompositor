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

#ifdef GLES2_VERSION
#include <QtDBus>
#include <mce/dbus-names.h>
#include <mce/mode-names.h>
#endif

#include "mdevicestate.h"

#ifdef GLES2_VERSION
void MDeviceState::mceDisplayStatusIndSignal(QString mode)
{
    if (mode == MCE_DISPLAY_OFF_STRING) {
        display_off = true;
        emit displayStateChange(true);
    } else if (mode == MCE_DISPLAY_ON_STRING) {
        display_off = false;
        emit displayStateChange(false);
    }
}
#endif

void MDeviceState::callPropChanged()
{
    QString val = call_prop->value().toString();
    if (val == "active") {
        ongoing_call = true;
        emit callStateChange(true);
    } else {
        ongoing_call = false;
        emit callStateChange(false);
    }
}

MDeviceState::MDeviceState(QObject* parent)
    : QObject(parent),
      ongoing_call(false)
{
    display_off = false;

    call_prop = new ContextProperty("Phone.Call");
    connect(call_prop, SIGNAL(valueChanged()), this, SLOT(callPropChanged()));

#ifdef GLES2_VERSION
    systembus_conn = new QDBusConnection(QDBusConnection::systemBus());
    systembus_conn->connect(MCE_SERVICE, MCE_SIGNAL_PATH, MCE_SIGNAL_IF,
                            MCE_DISPLAY_SIG, this,
                            SLOT(mceDisplayStatusIndSignal(QString)));

    if (!systembus_conn->isConnected())
        qWarning("Failed to connect to the D-Bus system bus");

    /* FIXME: Temporary workaround, current MCE does not seem to provide
     * get_display_status interface */
    QFile file("/sys/class/backlight/himalaya/brightness");
    if (file.open(QIODevice::ReadOnly)) {
        char buf[50];
        qint64 len = file.readLine(buf, sizeof(buf));
        buf[49] = '\0';
        if (len != -1) {
            int i = atoi(buf);
            if (i == 0) display_off = true;
        }
        file.close();
    }
#endif
}

MDeviceState::~MDeviceState()
{
    delete call_prop;
    call_prop = 0;
}
