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
bool operator==(const ChannelDetails& v1, const ChannelDetails& v2)
{
    return ((v1.channel == v2.channel)
            && (v1.properties == v2.properties)
            );
}

QDBusArgument& operator<<(QDBusArgument& arg, const ChannelDetails& val)
{
    arg.beginStructure();
    arg << val.channel << val.properties;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, ChannelDetails& val)
{
    arg.beginStructure();
    arg >> val.channel >> val.properties;
    arg.endStructure();
    return arg;
}

void MDeviceState::channelsReply(QDBusPendingCallWatcher *watcher)
{
    QDBusPendingReply<QVariant> reply = *watcher;

    if (!reply.isError()) {
        ChannelDetailsList l = qdbus_cast<ChannelDetailsList>(reply.value());
        if (l.size() > 0)
            // FIXME: maybe check against l[0].channel.path() would be good
            csdActivityChangedSignal(QString("Call"));
    } else
        qWarning("Failed to get a reply from Telepathy");

    watcher->deleteLater();
}
#endif

MDeviceState::MDeviceState()
{
    display_off = false;
    ongoing_call = false;
#ifdef GLES2_VERSION
    qDBusRegisterMetaType<ChannelDetails>();
    qDBusRegisterMetaType<ChannelDetailsList>();

    // FIXME: Use ContextKit for these when they provide the infos
    sessionbus_conn = new QDBusConnection(QDBusConnection::sessionBus());
    if (!sessionbus_conn->isConnected())
        qWarning("Failed to connect to the D-Bus session bus");
    else {
        QDBusMessage m = QDBusMessage::createMethodCall(
                          "org.freedesktop.Telepathy.Connection.ring.tel.ring",
                          "/org/freedesktop/Telepathy/Connection/ring/tel/ring",
                          "org.freedesktop.DBus.Properties", "Get");
        QList<QVariant> a;
        a.append(QString(
                 "org.freedesktop.Telepathy.Connection.Interface.Requests"));
        a.append(QString("Channels"));
        m.setArguments(a);

        QDBusPendingCall r = sessionbus_conn->asyncCall(m);
        QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(r);
        connect(watcher,
                SIGNAL(finished(QDBusPendingCallWatcher*)),
                SLOT(channelsReply(QDBusPendingCallWatcher*)));
    }

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
