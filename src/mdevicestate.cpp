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

MDeviceState::MDeviceState(QObject* parent)
    : QObject(parent),
      ongoing_call(false)
{
    // TODO: find out initial state for ongoing_call
    display_off = false;
#ifdef GLES2_VERSION
    systembus_conn = new QDBusConnection(QDBusConnection::systemBus());
    systembus_conn->connect(MCE_SERVICE, MCE_SIGNAL_PATH, MCE_SIGNAL_IF,
                            MCE_DISPLAY_SIG, this,
                            SLOT(mceDisplayStatusIndSignal(QString)));
    systembus_conn->connect("", "/com/nokia/csd/csnet",
                            "com.nokia.csd.CSNet", "ActivityChanged", this,
                            SLOT(csdActivityChangedSignal(QString)));
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
}

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

void MDeviceState::csdActivityChangedSignal(QString mode)
{
    if (mode == "Call") {
        ongoing_call = true;
        emit callStateChange(true);
    } else if (ongoing_call) {
        ongoing_call = false;
        emit callStateChange(false);
    }
}
#endif
