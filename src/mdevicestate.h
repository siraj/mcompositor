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

#ifdef GLES2_VERSION
#include <QtDBus>

struct ChannelDetails
{
    QDBusObjectPath channel;
    QVariantMap properties;
};

bool operator==(const ChannelDetails& v1, const ChannelDetails& v2);
inline bool operator!=(const ChannelDetails& v1, const ChannelDetails& v2)
{
    return !operator==(v1, v2);
}
QDBusArgument& operator<<(QDBusArgument& arg, const ChannelDetails& val);
const QDBusArgument& operator>>(const QDBusArgument& arg, ChannelDetails& val);

typedef QList<ChannelDetails> ChannelDetailsList;

Q_DECLARE_METATYPE(ChannelDetails);
Q_DECLARE_METATYPE(ChannelDetailsList);
#endif
#include <QObject>

/*!
 * This is a class listening to device state that is of interest to
 * MCompositeManager.
 */
class MDeviceState: public QObject
#ifdef GLES2_VERSION
                    , protected QDBusContext
#endif
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
    void channelsReply(QDBusPendingCallWatcher *watcher);
    void newChannelsSignal(const ChannelDetailsList &l);
    void channelClosed();
#endif

private:

#ifdef GLES2_VERSION
    QDBusConnection *systembus_conn;
    QDBusConnection *sessionbus_conn;
    QSet<QString> ongoing_calls;
#endif
    bool display_off;
    bool ongoing_call;
};

#endif
