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

#include <QDir>
#include <QMenu>
#include <QImage>
#include <QColor>
#include <QRegExp> 
#include <QPainter>
#include <QProcess>
#include <QByteArray>
#include <QJsonValue>
#include <QJsonArray>
#include <QFontDialog>
#include <QFileDialog>
#include <QDataStream>
#include <QTreeWidget>
#include <QJsonObject>
#include <QApplication>
#include <QColorDialog>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTreeWidgetItem>

#include <QDebug>
#include <chrono>
#include <exception>

#include "mainsettings.h"
#include "ui_mainsettings.h"

QString GenericError::toString(const char* func) const
{
    auto err = get();
    
    if(err)
    {
        auto str = QString("error code: %1, major: 0x%2, minor: 0x%3, sequence: %4").
            arg((int) err->error_code).
            arg(err->major_code, 2, 16, QChar('0')).
            arg(err->minor_code, 4, 16, QChar('0')).
            arg((uint) err->sequence);
    
        if(func)
            return QString(func).append(" ").append(str);

        return str;
    }

    return nullptr;
}

/* MainSettings */
MainSettings::MainSettings(const QString & globalConfigPath, QWidget *parent) : QWidget(parent), ui(new Ui::MainSettings)
{
    skipClasses << "qxkb5";
    actionSettings = new QAction("Settings", this);
    actionExit = new QAction("Exit", this);

    auto version = QString("%1 version: %2").arg(QCoreApplication::applicationName()).arg(QCoreApplication::applicationVersion());
    auto github = QString("https://github.com/AndreyBarmaley/qxkb5");

    ui->setupUi(this);
    ui->tabWidget->setCurrentIndex(0);
    ui->aboutInfo->setText(QString("<center><b>%1</b></center><br><br>"
                                   "<p>Source code: <a href='%2'>%2</a></p>"
                                   "<p>Copyright © 2022 by Andrey Afletdinov <public.irkutsk@gmail.com></p>").arg(version).arg(github));

    configLoadGlobal(globalConfigPath);
    configLoadLocal();
    startupProcess();

    xcb = new XcbEventsPool(this);

    cacheLoadItems();

    QMenu* menu = new QMenu(this);
    menu->addAction(actionSettings);
    menu->addSeparator();
    menu->addAction(actionExit);

    initXkbLayoutIcons();
    int index = xcb->getXkbLayout();

    trayIcon = new QSystemTrayIcon(this);
    trayIcon->setIcon(layoutIcons.at(index));
    trayIcon->setToolTip(version);
    trayIcon->setContextMenu(menu);
    trayIcon->show();

/*
    // session screensaver
    const char* service = "org.mate.ScreenSaver";
    const char* path = "/org/mate/ScreenSaver";
    const char* interface = "org.mate.ScreenSaver";

    dbusInterfacePtr.reset(new QDBusInterface(service, path, interface, QDBusConnection::sessionBus()));
    if(dbusInterfacePtr->isValid())
    {
        connect(dbusInterfacePtr.get(), SIGNAL(ActiveChanged(bool)), this, SLOT(screenSaverActiveChanged(bool)));
    }
    else
    {
        qWarning() << "dbus interface not found: " << service;
        dbusInterfacePtr.reset();
    }
*/

    connect(actionSettings, SIGNAL(triggered()), this, SLOT(show()));
    connect(actionExit, SIGNAL(triggered()), this, SLOT(exitProgram()));
    connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this, SLOT(iconActivated(QSystemTrayIcon::ActivationReason)));
    connect(xcb, SIGNAL(activeWindowNotify(int)), this, SLOT(activeWindowChanged(int)));
    connect(xcb, SIGNAL(windowTitleNotify(int)), this, SLOT(windowTitleChanged(int)));
    connect(xcb, SIGNAL(xkbStateNotify(int)), this, SLOT(xkbStateChanged(int)));
    connect(xcb, SIGNAL(xkbNewKeyboardNotify(int)), this, SLOT(xkbNewKeyboardChanged(int)));
    connect(xcb, SIGNAL(shutdownNotify()), this, SLOT(exitProgram()));
    connect(xcb, SIGNAL(xkbNamesChanged()), this, SLOT(iconAttributeChanged()));
    connect(this, SIGNAL(iconAttributeNotify()), this, SLOT(iconAttributeChanged()));

    if(ui->checkBoxPeriodicCheck->isChecked())
	periodicCheckXkbRules = startTimer(std::chrono::seconds(2));

    // start events pool thread mode
    xcb->start();
}

MainSettings::~MainSettings()
{
    windowRestoreTitle(prevWindow);
    delete ui;
}

void MainSettings::startupProcess(void)
{
    if(ui->checkBoxStartup->isChecked() && !ui->lineEditStartup->text().isEmpty())
    {
        QStringList args = ui->lineEditStartup->text().split(QRegExp("\\s+"));
        auto cmd = args.front();
        args.pop_front();

        if(0)
            qWarning() << "cmd: " << cmd << args;

        QProcess process(this);
        process.setProgram(cmd);
        process.setArguments(args);

        process.start(QIODevice::NotOpen);

        if(process.waitForFinished())
            startupCmd = ui->lineEditStartup->text();

        forceReload = false;
    }
}

void MainSettings::screenSaverActiveChanged(bool state)
{
    if(! state)
	startupProcess();
}

void MainSettings::keyPressEvent(QKeyEvent* ev)
{
    if(ui->tabWidget->currentWidget() == ui->tabCache)
    {
        if(ev->key() == Qt::Key_Delete)
        {
            if(auto item = ui->treeWidgetCache->currentItem())
            {
                int index = ui->treeWidgetCache->indexOfTopLevelItem(item);
                ui->treeWidgetCache->takeTopLevelItem(index);
            }
        }
    }
}

void MainSettings::timerEvent(QTimerEvent* ev)
{
    if(ev->timerId() == periodicCheckXkbRules)
    {
	QRegExp rx("-layout\\s+\"([\\w,]+)");
	if(rx.indexIn(ui->lineEditStartup->text(), 0) != -1)
	{
	    auto names1 = rx.cap(1).split(",");
	    auto names2 = xcb->getXkbNames();

	    if(0)
                qWarning() << "names1: " << names1 << "names2: " << names2;

	    if(forceReload || names1.size() != names2.size())
		startupProcess();
	}
    }
}

void MainSettings::periodicChecked(bool f)
{
    if(f)
    {
	killTimer(periodicCheckXkbRules);
	periodicCheckXkbRules = startTimer(std::chrono::seconds(2));
    }
    else
    if(0 < periodicCheckXkbRules)
    {
	killTimer(periodicCheckXkbRules);
    }
}

void MainSettings::exitProgram(void)
{
    hide();
    close();
}

void MainSettings::showEvent(QShowEvent* event)
{
    actionSettings->setDisabled(true);
}

void MainSettings::hideEvent(QHideEvent* event)
{
    actionSettings->setEnabled(true);
}

void MainSettings::closeEvent(QCloseEvent* event)
{
    if(isVisible())
    {
        if(ui->lineEditStartup->text() != startupCmd)
            startupProcess();

        event->ignore();
        hide();
    }

    cacheSaveItems();
    configSave();
}

void MainSettings::setBackgroundTransparent(bool f)
{
    ui->lineEditBackgroundColor->setDisabled(f);
    ui->pushButtonSelColor1->setDisabled(f);

    if(f)
    {
        ui->lineEditBackgroundColor->setText("transparent");
    }
    else
    {
        ui->lineEditBackgroundColor->setText("#191970");
        ui->lineEditTextColor->setText("#FFFFFF");
    }

    emit iconAttributeNotify();
}

void MainSettings::selectBackgroundColor(void)
{
    QColorDialog dialog(this);
    dialog.setCurrentColor(QColor(ui->lineEditBackgroundColor->text()));
    if(dialog.exec())
    {
        ui->lineEditBackgroundColor->setText(dialog.selectedColor().name());
        emit iconAttributeNotify();
    }
}

void MainSettings::allowPictureMode(bool f)
{
    ui->fromIconsPath->setEnabled(f);
    emit iconAttributeNotify();
}

void MainSettings::allowIconsPath(bool)
{
    emit iconAttributeNotify();
}

void MainSettings::iconAttributeChanged(void)
{
    initXkbLayoutIcons();
    int index = xcb->getXkbLayout();
    trayIcon->setIcon(layoutIcons.at(index));
}

void MainSettings::selectTextColor(void)
{
    QColorDialog dialog(this);
    dialog.setCurrentColor(QColor(ui->lineEditTextColor->text()));
    if(dialog.exec())
    {
        ui->lineEditTextColor->setText(dialog.selectedColor().name());
        emit iconAttributeNotify();
    }
}

void MainSettings::selectFont(void)
{
    auto fontArgs = ui->lineEditFont->text().split(", ");
    QFont font(fontArgs.front());
    if(1 < fontArgs.size())
        font.setPointSize(fontArgs.at(1).toInt());
    if(2 < fontArgs.size())
        font.setWeight(fontArgs.at(2).toInt());

    QFontDialog dialog(this);
    dialog.setCurrentFont(font);
    if(dialog.exec())
    {
        auto font = dialog.selectedFont();
        ui->lineEditFont->setText(QString("%1, %2, %3").arg(font.family()).arg(font.pointSize()).arg(font.weight()));
        emit iconAttributeNotify();
    }
}

void MainSettings::selectIconsPath(void)
{
    QFileDialog dialog(this);
    dialog.setDirectory(QDir(ui->lineEditIconsPath->text()));
    dialog.setOption(QFileDialog::ShowDirsOnly, true);

    if(dialog.exec())
    {
        ui->lineEditIconsPath->setText(dialog.directory().absolutePath());
        emit iconAttributeNotify();
    }
}

void MainSettings::iconActivated(QSystemTrayIcon::ActivationReason reason)
{
    if(reason == QSystemTrayIcon::Trigger)
        xcb->switchXkbLayout();
}

void MainSettings::configSave(void)
{
    auto localData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(localData);
    auto configPath = QDir(localData).absoluteFilePath("config");

    QFile file(configPath);
    if(! file.open(QIODevice::WriteOnly))
        return;

    QDataStream ds(&file);

    ds << int(VERSION) <<
          ui->checkBoxStartup->isChecked() <<
          ui->backgroundTransparent->isChecked() <<
          ui->lineEditBackgroundColor->text() <<
          ui->lineEditTextColor->text() <<
          ui->lineEditFont->text() <<
          ui->groupBoxPictureMode->isChecked() <<
          ui->fromIconsPath->isChecked() <<
          ui->lineEditIconsPath->text() <<
          ui->lineEditStartup->text() <<
          ui->checkBoxSound->isChecked() <<
          ui->checkBoxChangeTitle->isChecked() <<
          ui->lineEditTitleFormat->text() <<
          ui->checkBoxPeriodicCheck->isChecked();
}

bool MainSettings::configLoadLocal(void)
{
    auto localData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(localData);
    auto configPath = QDir(localData).absoluteFilePath("config");

    QFile file(configPath);
    if(! file.open(QIODevice::ReadOnly))
        return false;

    QDataStream ds(&file);
    int version;
    ds >> version;

    bool startup;
    ds >> startup;
    ui->checkBoxStartup->setChecked(startup);

    bool transparent;
    ds >> transparent;
    ui->backgroundTransparent->setChecked(transparent);

    QString backgroundColor, textColor, font;
    ds >> backgroundColor >> textColor >> font;

    ui->lineEditBackgroundColor->setText(backgroundColor);
    ui->lineEditTextColor->setText(textColor);
    ui->lineEditFont->setText(font);

    bool picmode;
    ds >> picmode;
    ui->groupBoxPictureMode->setChecked(picmode);

    bool frompath;
    ds >> frompath;
    ui->fromIconsPath->setChecked(frompath);

    QString iconpath;
    ds >> iconpath;
    ui->lineEditIconsPath->setText(iconpath);

    QString startcmd;
    ds >> startcmd;
    ui->lineEditStartup->setText(startcmd);

    bool sound;
    ds >> sound;
    ui->checkBoxSound->setChecked(sound);

    if(20220510 < version)
    {
        bool changeTitle;
        ds >> changeTitle;
        ui->checkBoxChangeTitle->setChecked(changeTitle);

        QString titleFormat;
        ds >> titleFormat;
        ui->lineEditTitleFormat->setText(titleFormat);
    }

    if(20220609 < version)
    {
	bool periodic;
        ds >> periodic;
	ui->checkBoxPeriodicCheck->setChecked(periodic);
    }

    return true;
}

bool MainSettings::configLoadGlobal(const QString & jsonPath)
{
    if(jsonPath.isEmpty())
        return false;

    QFile file(jsonPath);
    if(! file.open(QIODevice::ReadOnly))
    {
        qWarning() << "error open file" << jsonPath;
        return false;
    }

    auto data = file.readAll();
    if(data.isEmpty())
    {
        qWarning() << "file empty" << jsonPath;
        return false;
    }

    auto jsonDoc = QJsonDocument::fromJson(data);
    if(jsonDoc.isEmpty())
    {
        qWarning() << "not json format" << jsonPath;
        return false;
    }

    if(! jsonDoc.isObject())
    {
        qWarning() << "not json object" << jsonPath;
        return false;
    }

    auto jsonObject = jsonDoc.object();
    if(jsonObject.isEmpty())
    {
        qWarning() << "json empty" << jsonPath;
        return false;
    }

    bool transparent = jsonObject.value("background:transparent").toBool();
    ui->backgroundTransparent->setChecked(transparent);

    QString startupCmd = jsonObject.value("startup:cmd").toString();
    if(! startupCmd.isEmpty())
    {
        ui->checkBoxStartup->setChecked(true);
        ui->lineEditStartup->setText(startupCmd);
    }

    QString backgroundColor = jsonObject.value("background:color").toString();
    if(! backgroundColor.isEmpty())
        ui->lineEditBackgroundColor->setText(backgroundColor);

    QString textColor = jsonObject.value("text:color").toString();
    if(! textColor.isEmpty())
        ui->lineEditTextColor->setText(textColor);

    QString labelFont = jsonObject.value("label:font").toString();
    if(! labelFont.isEmpty())
        ui->lineEditFont->setText(labelFont);

    bool picmode = jsonObject.value("picture:mode").toBool();
    ui->groupBoxPictureMode->setChecked(picmode);

    bool sound = jsonObject.value("sound").toBool();
    ui->checkBoxSound->setChecked(sound);

    bool changeTitle = jsonObject.value("title:change").toBool();
    ui->checkBoxChangeTitle->setChecked(changeTitle);

    QString titleFormat = jsonObject.value("title:format").toString();
    ui->lineEditTitleFormat->setText(titleFormat);

    for(auto val : jsonObject.value("windows:skip").toArray())
        skipClasses << val.toString();

    bool periodicCheck = jsonObject.value("periodic:check").toBool();
    ui->checkBoxPeriodicCheck->setChecked(periodicCheck);

    return true;
}

void MainSettings::cacheSaveItems(void)
{
    auto localData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(localData);
    auto cachePath = QDir(localData).absoluteFilePath("cache");

    QFile file(cachePath);
    if(! file.open(QIODevice::WriteOnly))
        return;

    QDataStream ds(&file);
    int counts = ui->treeWidgetCache->topLevelItemCount();
    ds << int(VERSION) << counts;

    for(int cur = 0; cur < counts; ++cur)
    {
        auto item = ui->treeWidgetCache->topLevelItem(cur);
        ds << item->text(0) << item->text(1);
        ds << item->data(2, Qt::UserRole).toInt();
        ds << item->data(3, Qt::UserRole).toInt();
    }
}

QString layoutStateName(int v)
{
    if(v == LayoutState::StateFirst)
        return "first";
    if(v == LayoutState::StateFixed)
        return "fixed";
    if(v == LayoutState::StateNormal)
        return "normal";
    return "unknown";
}

void setHighlightStatusItem(QTreeWidgetItem* item, int state2)
{
    for(int col = 0; col < item->columnCount(); ++col)
    {
        item->setToolTip(col, col == 2 ? "change layout" : "change state: normal, first, fixed");

        auto font = item->font(col);
        font.setBold(state2 == LayoutState::StateFixed || state2 == LayoutState::StateFirst);
        item->setFont(col, font);
    }
}

void MainSettings::cacheLoadItems(void)
{
    auto localData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(localData);
    auto cachePath = QDir(localData).absoluteFilePath("cache");

    QFile file(cachePath);
    if(! file.open(QIODevice::ReadOnly))
        return;

    QDataStream ds(&file);
    ui->treeWidgetCache->clear();

    int version, counts;
    ds >> version >> counts;

    for(int cur = 0; cur < counts; ++cur)
    {
        QString class1, class2;
        int layout2, state2;

        ds >> class1 >> class2 >> layout2 >> state2;

        auto names = xcb->getXkbNames();
        if(names.size())
        {
            QString layout1 = 0 <= layout2 && names.size() > layout2 ? names.at(layout2) : names.front();
            QString state1 = layoutStateName(state2);

            auto item = new QTreeWidgetItem(QStringList() << class1 << class2 << layout1 << state1);
            item->setData(2, Qt::UserRole, layout2);
            item->setData(3, Qt::UserRole, state2);

            setHighlightStatusItem(item, state2);
            ui->treeWidgetCache->addTopLevelItem(item);
        }
    }
}

QTreeWidgetItem* MainSettings::cacheFindItem(const QString & class1, const QString & class2)
{
    auto items1 = ui->treeWidgetCache->findItems(class1, Qt::MatchFixedString, 0);
    auto items2 = ui->treeWidgetCache->findItems(class2, Qt::MatchFixedString, 1);

    for(auto & item : items1)
        if(items2.contains(item)) return item;

    return nullptr;
}

void MainSettings::cacheItemClicked(QTreeWidgetItem* item, int column)
{
    // change layout priority
    if(column == 2)
    {
        auto names = xcb->getXkbNames();
        if(names.size())
        {
            int layout2 = (item->data(2, Qt::UserRole).toInt() + 1) % names.size();

            item->setText(2, names.at(layout2));
            item->setData(2, Qt::UserRole, layout2);
        }
    }
    // change state
    else
    {
        auto state2 = item->data(3, Qt::UserRole).toInt();
        if(state2 >= LayoutState::StateFixed)
            state2 = LayoutState::StateNormal;
        else
            state2 += 1;

        item->setText(3, layoutStateName(state2));
        item->setData(3, Qt::UserRole, state2);

        setHighlightStatusItem(item, state2);
    }
}

void MainSettings::windowRestoreTitle(xcb_window_t win)
{
    if(XCB_WINDOW_NONE != win)
    {
        auto list = xcb->getPropertyStringList(win, XCB_ATOM_WM_CLASS);
        if(! list.empty() && ! skipClasses.contains(list.front(), Qt::CaseInsensitive))
        {
            if(auto item = cacheFindItem(list.front(), list.back()))
            {
                auto title = item->data(0, Qt::UserRole).toString();
                xcb->setWindowName(win, title.toStdString());
            }
        }
    }
}

void MainSettings::windowUpdateTitle(xcb_window_t win, const QString & title, const QString & label)
{
    auto format = ui->lineEditTitleFormat->text();
    auto text = format.replace(QString("%{title}"), title).replace(QString("%{label}"), label);

    xcb->setWindowEvents(win, XCB_EVENT_MASK_NO_EVENT);
    xcb->setWindowName(win, text.toStdString());
    xcb->setWindowEvents(win, XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_KEY_PRESS);
}

void MainSettings::windowTitleChanged(int win)
{
    if(ui->checkBoxChangeTitle->isChecked())
    {
        auto list = xcb->getPropertyStringList(win, XCB_ATOM_WM_CLASS);
        if(! list.empty())
        {
            QString title = xcb->getWindowName(win);
            auto layout = xcb->getXkbLayout();
            auto names = xcb->getXkbNames();

            // update backup title
            if(auto item = cacheFindItem(list.front(), list.back()))
                item->setData(0, Qt::UserRole, title);

            if(static_cast<int>(prevWindow) == win &&
                0 <= layout && layout < names.size())
                windowUpdateTitle(prevWindow, title, names.at(layout));
        }
    }
}

void MainSettings::activeWindowChanged(int win)
{
    // disable events
    xcb->setWindowEvents(prevWindow, XCB_EVENT_MASK_NO_EVENT);

    if(ui->checkBoxChangeTitle->isChecked())
        windowRestoreTitle(prevWindow);

    prevWindow = win;

    // enable events
    xcb->setWindowEvents(win, XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_KEY_PRESS);

    // update cache
    auto list = xcb->getPropertyStringList(win, XCB_ATOM_WM_CLASS);
    if(list.empty() || skipClasses.contains(list.front(), Qt::CaseInsensitive)) return;

    auto layout1 = xcb->getXkbLayout();
    auto names = xcb->getXkbNames();

    if(auto item = cacheFindItem(list.front(), list.back()))
    {
        // backup title
        if(item->data(0, Qt::UserRole).isNull())
        {
            QString title = xcb->getWindowName(win);
            item->setData(0, Qt::UserRole, title);
        }

        auto layout2 = item->data(2, Qt::UserRole).toInt();

        if(layout2 != layout1)
            xcb->switchXkbLayout(layout2);
    }
    else
    // item not found
    if(0 <= layout1 && layout1 < names.size())
    {
        QString title = xcb->getWindowName(win);
        auto item = new QTreeWidgetItem(QStringList() << list.front() << list.back() << names.at(layout1) << "normal");

        // title
        item->setData(0, Qt::UserRole, title);
        // layout
        item->setData(2, Qt::UserRole, layout1);
        // state
        item->setData(3, Qt::UserRole, int(LayoutState::StateNormal));
        ui->treeWidgetCache->addTopLevelItem(item);
    }

    windowTitleChanged(win);
}

void MainSettings::xkbNewKeyboardChanged(int changed)
{
    // XCB_XKB_NKN_DETAIL_KEYCODES = 1, XCB_XKB_NKN_DETAIL_GEOMETRY = 2, XCB_XKB_NKN_DETAIL_DEVICE_ID = 4

    if(changed & XCB_XKB_NKN_DETAIL_KEYCODES)
        return;

    if(changed & XCB_XKB_NKN_DETAIL_GEOMETRY)
        forceReload = true;
}

void MainSettings::xkbStateChanged(int layout1)
{
    if(0 == prevWindow)
        return;

    auto list = xcb->getPropertyStringList(prevWindow, XCB_ATOM_WM_CLASS);
    if(list.empty() || skipClasses.contains(list.front(), Qt::CaseInsensitive)) return;

    auto names = xcb->getXkbNames();
    auto item = cacheFindItem(list.front(), list.back());

    if(item)
    {
        auto state2 = item->data(3, Qt::UserRole).toInt();
        auto layout2 = item->data(2, Qt::UserRole).toInt();
        bool play = false;

        if(layout2 != layout1)
        {
            if(state2 == LayoutState::StateFixed)
            {
                // revert layout
                xcb->switchXkbLayout(layout2);
            }
            else
            if(state2 == LayoutState::StateNormal &&
                0 <= layout1 && layout1 < names.size())
            {
                item->setText(2, names.at(layout1));
                item->setData(2, Qt::UserRole, layout1);
                play = true;
            }
        }

        if(state2 == LayoutState::StateFirst)
            play = true;

        if(play && ui->checkBoxSound->isChecked())
        {
            if(soundClick.isFinished())
                soundClick.play();
        }

        if(ui->checkBoxChangeTitle->isChecked() &&
            0 <= layout1 && layout1 < names.size())
        {
            auto title = item->data(0, Qt::UserRole).toString();
            windowUpdateTitle(prevWindow, title, names.at(layout1));
        }
    }
    else
    if(0 <= layout1 && layout1 < names.size())
    {
        auto item = new QTreeWidgetItem(QStringList() << list.front() << list.back() << names.at(layout1) << "normal");
        item->setData(2, Qt::UserRole, layout1);
        item->setData(3, Qt::UserRole, int(LayoutState::StateNormal));
        ui->treeWidgetCache->addTopLevelItem(item);
    }

    if(layout1 < layoutIcons.size())
        trayIcon->setIcon(layoutIcons.at(layout1));
}

QPixmap MainSettings::getLayoutIcon(const QString & layoutName)
{
    if(ui->groupBoxPictureMode->isChecked())
    {
        QPixmap px;

        if(ui->fromIconsPath->isChecked())
        {
            auto format = QString("%1.png").arg(layoutName.left(2)).toLower();
            auto iconFile = QDir(ui->lineEditIconsPath->text()).absoluteFilePath(format);
            if(px.load(iconFile))
                return px;
        }

        if(px.load(QString(":/icons/").append(layoutName.left(2).toLower())))
            return px;
    }

    QImage image(32, 32, QImage::Format_RGBA8888);
    auto backcol = ui->lineEditBackgroundColor->text();
    image.fill(backcol == "transparent" ? Qt::transparent : QColor(backcol));

    QPainter painter(&image);
    painter.setPen(QColor(ui->lineEditTextColor->text()));

    // fontName, fontSize, fontWeight
    auto fontArgs = ui->lineEditFont->text().split(", ");
    QFont font(fontArgs.front());
    if(1 < fontArgs.size())
        font.setPointSize(fontArgs.at(1).toInt());
    if(2 < fontArgs.size())
        font.setWeight(fontArgs.at(2).toInt());

    painter.setFont(font);
    painter.drawText(image.rect(), Qt::AlignCenter, layoutName.left(2));

    return QPixmap::fromImage(image);
}

void MainSettings::initXkbLayoutIcons(void)
{
    ui->systemInfo->setText(QString("xkb info: %1").arg(xcb->getSymbolsLabel()));
    layoutIcons.clear();

    for(auto & name : xcb->getXkbNames())
        layoutIcons << getLayoutIcon(name);
}

/* XcbConnection */
XcbConnection::XcbConnection() :
    conn{ xcb_connect(nullptr, nullptr), xcb_disconnect },
    xkbctx{ nullptr, xkb_context_unref }, xkbmap{ nullptr, xkb_keymap_unref }, xkbstate{ nullptr, xkb_state_unref },
    xkbext(nullptr), root(XCB_WINDOW_NONE), xkbdevid(-1), atomActiveWindow(XCB_ATOM_NONE), atomNetWmName(XCB_ATOM_NONE), atomUtf8String(XCB_ATOM_NONE)
{
    if(xcb_connection_has_error(conn.get()))
        throw std::runtime_error("xcb_connect");

    auto setup = xcb_get_setup(conn.get());
    if(! setup)
        throw std::runtime_error("xcb_get_setup");

    auto screen = xcb_setup_roots_iterator(setup).data;
    if(! screen)
        throw std::runtime_error("xcb_setup_roots");

    root = screen->root;
    atomActiveWindow = getAtom("_NET_ACTIVE_WINDOW");
    atomNetWmName = getAtom("_NET_WM_NAME");
    atomUtf8String = getAtom("UTF8_STRING");

    xkbext = xcb_get_extension_data(conn.get(), &xcb_xkb_id);
    if(! xkbext)
        throw std::runtime_error("xkb_get_extension_data");

    auto xcbReply = getReplyFunc2(xcb_xkb_use_extension, conn.get(), XCB_XKB_MAJOR_VERSION, XCB_XKB_MINOR_VERSION);

    if(xcbReply.error())
        throw std::runtime_error("xcb_xkb_use_extension");

    xkbdevid = xkb_x11_get_core_keyboard_device_id(conn.get());
    if(xkbdevid < 0)
        throw std::runtime_error("xkb_x11_get_core_keyboard_device_id");

    xkbctx.reset(xkb_context_new(XKB_CONTEXT_NO_FLAGS));
    if(! xkbctx)
        throw std::runtime_error("xkb_context_new");

    xkbmap.reset(xkb_x11_keymap_new_from_device(xkbctx.get(), conn.get(), xkbdevid, XKB_KEYMAP_COMPILE_NO_FLAGS));
    if(!xkbmap)
        throw std::runtime_error("xkb_x11_keymap_new_from_device");

    xkbstate.reset(xkb_x11_state_new_from_device(xkbmap.get(), conn.get(), xkbdevid));
    if(!xkbstate)
        throw std::runtime_error("xkb_x11_state_new_from_device");

    // XCB_XKB_MAP_PART_KEY_TYPES, XCB_XKB_MAP_PART_KEY_SYMS, XCB_XKB_MAP_PART_MODIFIER_MAP, XCB_XKB_MAP_PART_EXPLICIT_COMPONENTS
    // XCB_XKB_MAP_PART_KEY_ACTIONS, XCB_XKB_MAP_PART_VIRTUAL_MODS, XCB_XKB_MAP_PART_VIRTUAL_MOD_MAP
    uint16_t required_map_parts = 0;
    uint16_t required_events = XCB_XKB_EVENT_TYPE_NEW_KEYBOARD_NOTIFY | XCB_XKB_EVENT_TYPE_MAP_NOTIFY | XCB_XKB_EVENT_TYPE_STATE_NOTIFY;

    auto cookie = xcb_xkb_select_events_checked(conn.get(), xkbdevid, required_events, 0, required_events, required_map_parts, required_map_parts, nullptr);
    if(GenericError(xcb_request_check(conn.get(), cookie)))
        throw std::runtime_error("xcb_xkb_select_events");

    const uint32_t values[] = { XCB_EVENT_MASK_PROPERTY_CHANGE };
    xcb_change_window_attributes(conn.get(), root, XCB_CW_EVENT_MASK, values);

    xcb_flush(conn.get());
}

QString XcbConnection::getAtomName(xcb_atom_t atom) const
{
    auto xcbReply = getReplyFunc2(xcb_get_atom_name, conn.get(), atom);

    if(auto & reply = xcbReply.reply())
    {
        const char* name = xcb_get_atom_name_name(reply.get());
        size_t len = xcb_get_atom_name_name_length(reply.get());
        return QString(QByteArray(name, len));
    }

    return QString("NONE");
}

xcb_atom_t XcbConnection::getAtom(const QString & name, bool create) const
{
    auto xcbReply = getReplyFunc2(xcb_intern_atom, conn.get(), create ? 0 : 1, name.length(), name.toStdString().c_str());

    if(xcbReply.error())
        return XCB_ATOM_NONE;

    return xcbReply.reply() ? xcbReply.reply()->atom : XCB_ATOM_NONE;
}

xcb_window_t XcbConnection::getActiveWindow(void) const
{
    return getPropertyWindow(root, atomActiveWindow);
}

QString XcbConnection::getSymbolsLabel(void) const
{
    auto xcbReply = getReplyFunc2(xcb_xkb_get_names, conn.get(), XCB_XKB_ID_USE_CORE_KBD, XCB_XKB_NAME_DETAIL_GROUP_NAMES | XCB_XKB_NAME_DETAIL_SYMBOLS);

    if(xcbReply.error())
        throw std::runtime_error("xcb_xkb_get_names");

    if(auto & reply = xcbReply.reply())
    {
        const void *buffer = xcb_xkb_get_names_value_list(reply.get());
        xcb_xkb_get_names_value_list_t list;

        xcb_xkb_get_names_value_list_unpack(buffer, reply->nTypes, reply->indicators, reply->virtualMods,
                                            reply->groupNames, reply->nKeys, reply->nKeyAliases, reply->nRadioGroups, reply->which, & list);
        return getAtomName(list.symbolsName);
    }

    return nullptr;
}

QStringList XcbConnection::getXkbNames(void) const
{
    auto xcbReply = getReplyFunc2(xcb_xkb_get_names, conn.get(), XCB_XKB_ID_USE_CORE_KBD, XCB_XKB_NAME_DETAIL_GROUP_NAMES | XCB_XKB_NAME_DETAIL_SYMBOLS);

    if(xcbReply.error())
        throw std::runtime_error("xcb_xkb_get_names");

    QStringList res;
    if(auto & reply = xcbReply.reply())
    {
        const void *buffer = xcb_xkb_get_names_value_list(reply.get());
        xcb_xkb_get_names_value_list_t list;

        xcb_xkb_get_names_value_list_unpack(buffer, reply->nTypes, reply->indicators, reply->virtualMods,
                                            reply->groupNames, reply->nKeys, reply->nKeyAliases, reply->nRadioGroups, reply->which, & list);
        int groups = xcb_xkb_get_names_value_list_groups_length(reply.get(), & list);

        for(int ii = 0; ii < groups; ++ii)
            res << getAtomName(list.groups[ii]);
    }

    return res;
}

bool XcbConnection::switchXkbLayout(int layout)
{
    // next
    if(layout < 0)
    {
        auto names = getXkbNames();
        layout = (getXkbLayout() + 1) % names.size();
    }

    auto cookie = xcb_xkb_latch_lock_state_checked(conn.get(), XCB_XKB_ID_USE_CORE_KBD, 0, 0, 1, layout, 0, 0, 0);
    if(! GenericError(xcb_request_check(conn.get(), cookie)))
        return true;

    return false;
}

int XcbConnection::getDeviceId(void) const
{
    return xkbdevid;
}

int XcbConnection::getXkbLayout(void) const
{
    auto xcbReply = getReplyFunc2(xcb_xkb_get_state, conn.get(), XCB_XKB_ID_USE_CORE_KBD);

    if(xcbReply.error())
        throw std::runtime_error("xcb_xkb_get_state");

    if(auto & reply = xcbReply.reply())
        return reply->group;

    return 0;
}

XcbPropertyReply XcbConnection::getPropertyAnyType(xcb_window_t win, xcb_atom_t prop, uint32_t offset, uint32_t length) const
{
    auto xcbReply = getReplyFunc2(xcb_get_property, conn.get(), false, win, prop, XCB_GET_PROPERTY_TYPE_ANY, offset, length);
                
    if(auto & err = xcbReply.error())
        qWarning() << err.toString("xcb_get_property");
        
    return XcbPropertyReply(std::move(xcbReply.first));
}

xcb_atom_t XcbConnection::getPropertyType(xcb_window_t win, xcb_atom_t prop) const
{
    auto reply = getPropertyAnyType(win, prop, 0, 0);
    return reply ? reply->type : (xcb_atom_t) XCB_ATOM_NONE;
}

QString XcbConnection::getWindowName(xcb_window_t win) const
{
    QString res = getPropertyString(win, atomNetWmName);

    if(res.isEmpty())
    {
        if(atomUtf8String == getPropertyType(win, atomNetWmName))
        {
            if(auto reply = getPropertyAnyType(win, atomNetWmName, 0, 8192))
            {
                auto ptr = reinterpret_cast<const char*>(reply.value());
                if(ptr) res.append(ptr);
            }
        }
    }

    return res;
}

void XcbConnection::setWindowEvents(xcb_window_t win, uint32_t mask)
{
    const uint32_t values[] = { mask };
    auto cookie = xcb_change_window_attributes_checked(conn.get(), win, XCB_CW_EVENT_MASK, values);

    if(auto err = checkRequest(cookie))
        qWarning() << err.toString("xcb_change_window_attributes");
}

GenericError XcbConnection::checkRequest(const xcb_void_cookie_t & cookie) const
{
    return GenericError(xcb_request_check(conn.get(), cookie));
}

bool XcbConnection::setWindowName(xcb_window_t win, const std::string & title)
{
    // set wm name
    auto cookie = xcb_change_property(conn.get(), XCB_PROP_MODE_REPLACE, win, atomNetWmName, atomUtf8String, 8, title.size(), title.data());

    if(auto err = checkRequest(cookie))
    {
        qWarning() << err.toString("xcb_change_property");
        return false;
    }

    return true;
}

QString XcbConnection::getPropertyString(xcb_window_t win, xcb_atom_t prop) const
{
    if(XCB_ATOM_STRING == getPropertyType(win, prop))
    {
        if(auto reply = getPropertyAnyType(win, prop, 0, 8192))
        {
            auto ptr = reinterpret_cast<const char*>(reply.value());
            if(ptr) return QString(ptr);
        }
    }

    return nullptr;
}

xcb_window_t XcbConnection::getPropertyWindow(xcb_window_t win, xcb_atom_t prop, uint32_t offset) const
{
    auto xcbReply = getReplyFunc2(xcb_get_property, conn.get(), false, win, prop, XCB_ATOM_WINDOW, offset, 1);

    if(xcbReply.error())
        return XCB_WINDOW_NONE;

    if(auto & reply = xcbReply.reply())
    {
        if(auto res = static_cast<xcb_window_t*>(xcb_get_property_value(reply.get())))
            return *res;
    }

    return XCB_WINDOW_NONE;
}

QStringList XcbConnection::getPropertyStringList(xcb_window_t win, xcb_atom_t prop) const
{
    auto xcbReply = getReplyFunc2(xcb_get_property, conn.get(), false, win, prop, XCB_ATOM_STRING, 0, ~0);
    QStringList res;

    if(xcbReply.error())
        return res;

    if(auto & reply = xcbReply.reply())
    {
        int len = xcb_get_property_value_length(reply.get());
        auto ptr = static_cast<const char*>(xcb_get_property_value(reply.get()));

        for(auto & ba : QByteArray(ptr, len - (ptr[len - 1] ? 0 : 1 /* remove last nul */)).split(0))
            res << QString(ba);
    }

    return res;
}

/* XcbEventsPool */
XcbEventsPool::XcbEventsPool(QObject* obj) : QThread(obj), shutdown(false)
{
    connect(this, & XcbEventsPool::xkbStateResetNotify, [this](){ emit xkbNamesChanged(); });
}

XcbEventsPool::~XcbEventsPool()
{
    shutdown = true;
    if(! wait(1000))
    {
        terminate();
        wait();
    }
}

void XcbEventsPool::run(void)
{
    // check current active window
    auto activeWindow = getActiveWindow();
    if(activeWindow != XCB_WINDOW_NONE)
        emit activeWindowNotify(activeWindow);

    // events
    while(true)
    {
        if(shutdown)
            break;

        if(int err = xcb_connection_has_error(conn.get()))
        {
            qWarning() << "xcb error code:" << err;
            emit shutdownNotify();
            break;
        }

        while(auto ev = GenericEvent(xcb_poll_for_event(conn.get())))
        {
            auto type = ev ? ev->response_type & ~0x80 : 0;
            if(type == 0)
                continue;

            bool resetMapState = false;

            if(XCB_KEY_PRESS == type)
            {
                if(auto kp = reinterpret_cast<xcb_key_press_event_t*>(ev.get()))
                {
                    emit keycodePressNotify(kp->detail, kp->state);
                }
            }
            else
            if(XCB_PROPERTY_NOTIFY == type)
            {
                if(auto pn = reinterpret_cast<xcb_property_notify_event_t*>(ev.get()))
                {
                    // root window
                    if(pn->window == root)
                    {
                        // changed property: active window
                        if(pn->atom == atomActiveWindow)
                        {
                            activeWindow = getActiveWindow();
                            if(activeWindow != XCB_WINDOW_NONE)
                                emit activeWindowNotify(activeWindow);
                        }
                    }
                    // other window
                    else
                    {
                        // changed property: wm name
                        if(pn->atom == atomNetWmName)
                        {
                            emit windowTitleNotify(pn->window);
                        }
                    }
                }
            }
            else
            if(xkbext->first_event == type)
            {
                auto xkbev = ev->pad0;
                if(XCB_XKB_MAP_NOTIFY == xkbev)
                {
                    if(auto mn = reinterpret_cast<xcb_xkb_map_notify_event_t*>(ev.get()))
                    {
/*
typedef struct xcb_xkb_map_notify_event_t {
    uint8_t         response_type;
    uint8_t         xkbType;
    uint16_t        sequence;
    xcb_timestamp_t time;
    uint8_t         deviceID;
    uint8_t         ptrBtnActions;
    uint16_t        changed;
    xcb_keycode_t   minKeyCode;
    xcb_keycode_t   maxKeyCode;
    uint8_t         firstType;
    uint8_t         nTypes;
    xcb_keycode_t   firstKeySym;
    uint8_t         nKeySyms;
    xcb_keycode_t   firstKeyAct;
    uint8_t         nKeyActs;
    xcb_keycode_t   firstKeyBehavior;
    uint8_t         nKeyBehavior;
    xcb_keycode_t   firstKeyExplicit;
    uint8_t         nKeyExplicit;
    xcb_keycode_t   firstModMapKey;
    uint8_t         nModMapKeys;
    xcb_keycode_t   firstVModMapKey;
    uint8_t         nVModMapKeys;
    uint16_t        virtualMods;
    uint8_t         pad0[2];
} xcb_xkb_map_notify_event_t;
*/

                        resetMapState = true;

			if(0)
        		qWarning() << QString("new map notify - xkbType: %1, deviceID: %2, ptrBtnActions: 0x%3, keyCode: (%4, %5), chaged: 0x%6, time: %7").
			    arg((int) mn->xkbType).
			    arg((int) mn->deviceID).
			    arg((int) mn->ptrBtnActions, 2, 16, QChar('0')).
			    arg((int) mn->minKeyCode).
			    arg((int) mn->maxKeyCode).
			    arg((int) mn->changed, 4, 16, QChar('0')).
			    arg((int) mn->time);
                    }
                }
                else
                if(XCB_XKB_NEW_KEYBOARD_NOTIFY == xkbev)
                {
                    if(auto kn = reinterpret_cast<xcb_xkb_new_keyboard_notify_event_t*>(ev.get()))
                    {
/*
typedef struct xcb_xkb_new_keyboard_notify_event_t {
    uint8_t         response_type;
    uint8_t         xkbType;
    uint16_t        sequence;
    xcb_timestamp_t time;
    uint8_t         deviceID;
    uint8_t         oldDeviceID;
    xcb_keycode_t   minKeyCode;
    xcb_keycode_t   maxKeyCode;
    xcb_keycode_t   oldMinKeyCode;
    xcb_keycode_t   oldMaxKeyCode;
    uint8_t         requestMajor;
    uint8_t         requestMinor;
    uint16_t        changed;
    uint8_t         pad0[14];
} xcb_xkb_new_keyboard_notify_event_t;
*/
                        //if(kn->deviceID == xkbdevid && (kn->changed & XCB_XKB_NKN_DETAIL_KEYCODES))
                        //    resetMapState = true;

			// changed: XCB_XKB_NKN_DETAIL_KEYCODES = 1, XCB_XKB_NKN_DETAIL_GEOMETRY = 2, XCB_XKB_NKN_DETAIL_DEVICE_ID  = 4

			if(0)
                        qWarning() << QString("new keyboard notify - xkbType: %1, deviceID: (%2,%3,%4), keyCode: (%5,%6), oldKeyCode: (%7,%8), chaged: 0x%9, time: %10").
			    arg((int) kn->xkbType).
			    arg((int) xkbdevid).
			    arg((int) kn->deviceID).
			    arg((int) kn->oldDeviceID).
			    arg((int) kn->minKeyCode).
			    arg((int) kn->maxKeyCode).
			    arg((int) kn->oldMinKeyCode).
			    arg((int) kn->oldMaxKeyCode).
			    arg((int) kn->changed, 4, 16, QChar('0')).
			    arg((int) kn->time);
/*
    // wifi mouse
    "new keyboard notify - xkbType: 0, deviceID: (3,3,3), keyCode: (8,255), oldKeyCode: (8,255), chaged: 0x0002, time: 1557398869"
    "new keyboard notify - xkbType: 0, deviceID: (3,5,5), keyCode: (8,255), oldKeyCode: (8,255), chaged: 0x0002, time: 1557398869"
    "new keyboard notify - xkbType: 0, deviceID: (3,6,6), keyCode: (8,255), oldKeyCode: (8,255), chaged: 0x0002, time: 1557398869"
*/
                        if(xkbdevid == kn->deviceID)
                            emit xkbNewKeyboardNotify(kn->changed);
                    }
                }
                else
                if(xkbev == XCB_XKB_STATE_NOTIFY)
                {
                    if(auto sn = reinterpret_cast<xcb_xkb_state_notify_event_t*>(ev.get()))
                    {
/*
typedef struct xcb_xkb_state_notify_event_t {
    uint8_t         response_type;
    uint8_t         xkbType;
    uint16_t        sequence;
    xcb_timestamp_t time;
    uint8_t         deviceID;
    uint8_t         mods;
    uint8_t         baseMods;
    uint8_t         latchedMods;
    uint8_t         lockedMods;
    uint8_t         group;
    int16_t         baseGroup;
    int16_t         latchedGroup;
    uint8_t         lockedGroup;
    uint8_t         compatState;
    uint8_t         grabMods;
    uint8_t         compatGrabMods;
    uint8_t         lookupMods;
    uint8_t         compatLoockupMods;
    uint16_t        ptrBtnState;
    uint16_t        changed;
    xcb_keycode_t   keycode;
    uint8_t         eventType;
    uint8_t         requestMajor;
    uint8_t         requestMinor;
} xcb_xkb_state_notify_event_t;
*/
                        if(0)
		        qWarning() << QString("new state notify - xkbType: %1, deviceID: %2, mods1(0x%3,0x%4,0x%5,0x%6), group(0x%7,0x%8,0x%9,0x%10), compatState: 0x%11, mods2(0x%12,0x%13,0x%14,0x%15), ptrBtnState: 0x%16, changed: 0x%17, keycode: %18, time: %19").
			    arg((int) sn->xkbType).
			    arg((int) sn->deviceID).
			    arg((int) sn->mods, 2, 16, QChar('0')).
			    arg((int) sn->baseMods, 2, 16, QChar('0')).
			    arg((int) sn->latchedMods, 2, 16, QChar('0')).
			    arg((int) sn->lockedMods, 2, 16, QChar('0')).
			    arg((int) sn->group, 2, 16, QChar('0')).
			    arg((int) sn->baseGroup, 4, 16, QChar('0')).
			    arg((int) sn->latchedGroup, 4, 16, QChar('0')).
			    arg((int) sn->lockedGroup, 2, 16, QChar('0')).
			    arg((int) sn->compatState, 2, 16, QChar('0')).
			    arg((int) sn->grabMods, 2, 16, QChar('0')).
			    arg((int) sn->compatGrabMods, 2, 16, QChar('0')).
			    arg((int) sn->lookupMods, 2, 18, QChar('0')).
			    arg((int) sn->compatLoockupMods, 2, 18, QChar('0')).
			    arg((int) sn->ptrBtnState, 4, 16, QChar('0')).
			    arg((int) sn->changed, 4, 16, QChar('0')).
			    arg((int) sn->keycode).
			    arg((int) sn->time);


                        xkb_state_update_mask(xkbstate.get(), sn->baseMods, sn->latchedMods, sn->lockedMods,
                                                      sn->baseGroup, sn->latchedGroup, sn->lockedGroup);

                        if(sn->changed & XCB_XKB_STATE_PART_GROUP_STATE)
                            emit xkbStateNotify(sn->group);
                    }

                }

                if(resetMapState)
                {
		    qWarning() << "reset map state!";

                    // free state first
                    xkbstate.reset();
                    xkbmap.reset();

                    // set new
                    xkbmap.reset(xkb_x11_keymap_new_from_device(xkbctx.get(), conn.get(), xkbdevid, XKB_KEYMAP_COMPILE_NO_FLAGS));
                    xkbstate.reset(xkb_x11_state_new_from_device(xkbmap.get(), conn.get(), xkbdevid));

                    emit xkbStateResetNotify();
                }
            }
        }

        msleep(25);
    }
}
