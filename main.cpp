/***************************************************************************
 *   Copyright © 2022 by Andrey Afletdinov <public.irkutsk@gmail.com>      *
 *                                                                         *
 *   Part of the QXKB5                                                     *
 *   https://github.com/AndreyBarmaley/qxkb5                               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include "mainsettings.h"

#include <QDir>
#include <QFile>
#include <QDebug>
#include <QLockFile>
#include <QApplication>
#include <QStandardPaths>
#include <QCommandLineParser>
#include <exception>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("QXkb5");
    QCoreApplication::setApplicationVersion(QString::number(VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription("Xkb switcher based xcb and Qt5");
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption configOption(QStringList() << "c" << "config", "Global file config (json format).", "config");
    parser.addOption(configOption);

    parser.process(app);
    QString configFile = parser.value(configOption);

    auto localData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(localData);
    auto lockPath = QDir(localData).absoluteFilePath("lock");
    QLockFile lockFile(lockPath);

    if(! lockFile.tryLock(100))
    {
        qWarning() << "also running, see lock" << lockPath;
        return 1;
    }

    try
    {
        MainSettings widget(configFile);
        widget.hide();
        return app.exec();
    }
    catch(const std::exception & err)
    {
        qWarning() << err.what();
    }

    return -1;
}
