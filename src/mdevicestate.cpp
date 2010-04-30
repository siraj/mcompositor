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

#define TP_RING_SERVICE "org.freedesktop.Telepathy.Connection.ring.tel.ring"
#define TP_RING_PATH "/org/freedesktop/Telepathy/Connection/ring/tel/ring"
#define TP_REQ_IFACE "org.freedesktop.Telepathy.Connection.Interface.Requests"
#define TP_CHANNEL_IFACE "org.freedesktop.Telepathy.Channel"

#define INCOMING_CALL TP_RING_PATH "/incoming"
#define OUTGOING_CALL TP_RING_PATH "/outgoing"
static const QString incoming = QString(INCOMING_CALL);
static const QString outgoing = QString(OUTGOING_CALL);

void MDeviceState::channelsReply(QDBusPendingCallWatcher *watcher)
{
    QDBusPendingReply<QVariant> reply = *watcher;

    if (!reply.isError()) {
        ChannelDetailsList l = qdbus_cast<ChannelDetailsList>(reply.value());
        for (int i = 0; i < l.size(); ++i) {
             QString p = l[i].channel.path();
             if ((p.startsWith(incoming) || p.startsWith(outgoing))
                 && !ongoing_calls.contains(p)) {
                 ongoing_calls.insert(p);
                 sessionbus_conn->connect("", p, TP_CHANNEL_IFACE, "Closed",
                                          this, SLOT(channelClosed()));
             }
        }
    } else
        qWarning("Failed to get a reply from Telepathy");

    if (!ongoing_call && !ongoing_calls.isEmpty()) {
        ongoing_call = true;
        emit callStateChange(true);
    }

    watcher->deleteLater();
}

void MDeviceState::newChannelsSignal(const ChannelDetailsList &l)
{
    for (int i = 0; i < l.size(); ++i) {
         QString p = l[i].channel.path();
         if ((p.startsWith(incoming) || p.startsWith(outgoing))
             && !ongoing_calls.contains(p)) {
             ongoing_calls.insert(p);
             sessionbus_conn->connect("", p, TP_CHANNEL_IFACE, "Closed",
                                      this, SLOT(channelClosed()));
         }
    }
    if (!ongoing_call && !ongoing_calls.isEmpty()) {
        ongoing_call = true;
        emit callStateChange(true);
    }
}

void MDeviceState::channelClosed()
{
    QString p = message().path();
    if (ongoing_calls.contains(p)) {
        ongoing_calls.remove(p);
        sessionbus_conn->disconnect("", p, TP_CHANNEL_IFACE, "Closed",
                                    this, SLOT(channelClosed()));
    }
    if (ongoing_call && ongoing_calls.isEmpty()) {
        ongoing_call = false;
        emit callStateChange(false);
    }
}

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

MDeviceState::MDeviceState(QObject* parent)
    : QObject(parent),
      ongoing_call(false)
{
    display_off = false;
#ifdef GLES2_VERSION
    qDBusRegisterMetaType<ChannelDetails>();
    qDBusRegisterMetaType<ChannelDetailsList>();

    // FIXME: Use ContextKit for these when they provide the infos
    sessionbus_conn = new QDBusConnection(QDBusConnection::sessionBus());
    if (!sessionbus_conn->isConnected())
        qWarning("Failed to connect to the D-Bus session bus");
    else {
        QDBusMessage m = QDBusMessage::createMethodCall(TP_RING_SERVICE,
                                TP_RING_PATH,
                                "org.freedesktop.DBus.Properties", "Get");
        QList<QVariant> a;
        a.append(QString(TP_REQ_IFACE));
        a.append(QString("Channels"));
        m.setArguments(a);

        QDBusPendingCall r = sessionbus_conn->asyncCall(m);
        QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(r);
        connect(watcher,
                SIGNAL(finished(QDBusPendingCallWatcher*)),
                SLOT(channelsReply(QDBusPendingCallWatcher*)));

        sessionbus_conn->connect("", TP_RING_PATH, TP_REQ_IFACE,
                                 "NewChannels", this,
                                 SLOT(newChannelsSignal(ChannelDetailsList)));
    }

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
}
