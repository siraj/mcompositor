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

#include <QApplication>
#include <QDebug>
#include "qtdecotator.h"
#include "windowframe.h"

class MDecoratorApp : public QApplication
{
public:
    MDecoratorApp(int argc, char **argv) : QApplication(argc, argv)
    {
        qDebug() << Q_FUNC_INFO;
        window.show();
    }

    virtual bool x11EventFilter(XEvent *e) { return window.x11Event(e);}

private:
    QtWindowFrame window;
};

int main(int argc, char **argv)
{
    MDecoratorApp app(argc, argv);
    return app.exec();
}
