#ifndef QT_FRAME_WINDOW_H
#define QT_FRAME_WINDOW_H

#include <QWidget>
#include "qtdecotator.h"


class QtWindowFrame : public QWidget
{
    Q_OBJECT
public:
        QtWindowFrame(QWidget *parent = 0) ;
        virtual ~QtWindowFrame();

        bool x11Event(XEvent *e);

private:
        QtDeco *mDeco;

        Q_DISABLE_COPY(QtWindowFrame)
};
#endif // QT_FRAME_WINDOW_H
