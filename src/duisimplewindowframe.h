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

#include <QWidget>

class SystemButton;

/*!
 * DuiSimpleWindow is responsible for drawing window frames for windows
 * which do not manage itself. It draws a simple toolbar which contains
 * a close button and a minimize button.
 *
 */
class DuiSimpleWindowFrame: public QWidget
{
    Q_OBJECT
public:

    /*!
     * Initializes the DuiSimpleWindowFrame
     *
     * \param managedWindow the window that will be drawn with a frame
     */
    DuiSimpleWindowFrame(Qt::HANDLE managedWindow);

    /*!
     * Frees the resources
     */
    virtual ~DuiSimpleWindowFrame();

    /*!
     * Returns the suggested size of the contents of the window that
     * is supposed to be managed by this frame.
     */
    const QSize &suggestedWindowSize() const;

    /*!
     * Returns the window id of the area where the managed window is
     * going to be re-parented
     */
    Qt::HANDLE windowArea();

    /*!
     * Returns the window id of the managed window contained by this frame
     */
    Qt::HANDLE managedWindow();

    /*!
    * Sets parent window of this frame if any
    *
    * \param frame the parent window this this frame
    */
    void setParentFrame(Qt::HANDLE frame);

    /*!
    * Sets the decoration of the frame to dialog mode by hiding the minimize button
    *
    * \param dialogDecor true if dialog mode false otherwise.
    */
    void setDialogDecoration(bool dialogDecor);

public slots:

    /*!
     * Slot to minimize the managed window
     */
    void minimizeWindow();

    /*!
     * Slot to send a close event to the managed window
     */
    void closeWindow();

private:
    QSize window_size;
    QWidget *window_area;
    SystemButton *homesb;

    QPixmap *homepixmap;
    QPixmap *closepixmap;

    Qt::HANDLE parent_frame;
    // the actual window that is being managed
    Qt::HANDLE managed_window;

    static QPixmap *homepix;
    static QPixmap *closepix;

#ifdef UNIT_TEST
    friend class Ut_DuiSimpleWindowFrame;
#endif
};
