#ifndef WINDOWSTACK_H
#define WINDOWSTACK_H

#include <QObject>
#include <tasfixtureplugininterface.h>
#include <QtGui>
#include <MSceneManager>
#include <MApplication>

class FixtureWindowstack : public QObject, public TasFixturePluginInterface
{
  Q_OBJECT
  Q_INTERFACES(TasFixturePluginInterface)
 
public:

     FixtureWindowstack(QObject* parent=0);
     ~FixtureWindowstack();
     bool execute(void* objectInstance, QString actionName,
		  QHash<QString, QString> parameters, QString & stdOut);
};
 
#endif
