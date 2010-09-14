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

#ifndef MCOMPMGREXTENSIONFACTORY_H
#define MCOMPMGREXTENSIONFACTORY_H

#include <QtPlugin>

class MCompmgrExtensionFactory
{
 public:
    virtual ~MCompmgrExtensionFactory() {}

    virtual MCompositeManagerExtension* create() = 0;
    virtual QString extensionName() = 0;
};

Q_DECLARE_INTERFACE(MCompmgrExtensionFactory,
                    "com.nokia.mCompositor.MCompmgrExtensionFactory/1.0");

#endif //MCOMPMGREXTENSIONFACTORY_H
