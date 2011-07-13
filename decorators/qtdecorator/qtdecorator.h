#ifndef QT_WINDOW_DECO_H
#define QT_WINDOW_DECO_H

#include <mabstractdecorator.h>

class QtWindowFrame;

class QtDeco : public MAbstractDecorator
{
    Q_OBJECT
public:
   QtDeco(QtWindowFrame *parent = 0);
   virtual ~QtDeco();

   void activateEvent();

   void manageEvent(Qt::HANDLE window);

   void setAutoRotation(bool mode){}

   void setOnlyStatusbar(bool mode){}

   void showQueryDialog(bool visible){}

private:
   QtWindowFrame *window;

};

#endif //QT_WINDOW_DECO_H
