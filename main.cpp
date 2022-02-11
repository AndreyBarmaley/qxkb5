#include "mainsettings.h"

#include <QDir>
#include <QDebug>
#include <QLockFile>
#include <QApplication>
#include <QStandardPaths>
#include <exception>

int main(int argc, char *argv[])
{
  QCoreApplication::setApplicationName("QXkb5");
  QCoreApplication::setApplicationVersion("20220210");

  auto localData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  QDir().mkpath(localData);
  auto lockPath = QDir(localData).absoluteFilePath("lock");
  QLockFile lockFile(lockPath);

  if(! lockFile.tryLock(100))
  {
    qWarning() << "also running, see lock" << lockPath;
    return 1;
  }

  QApplication a(argc, argv);
  try
  {
    MainSettings w;
    w.hide();
    return a.exec();
  }
  catch(const std::exception & err)
  {
    qWarning() << err.what();
  }

  return -1;
}
