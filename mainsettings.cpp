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
#include <QPainter>
#include <QProcess>
#include <QByteArray>
#include <QFontDialog>
#include <QFileDialog>
#include <QDataStream>
#include <QTreeWidget>
#include <QApplication>
#include <QColorDialog>
#include <QStandardPaths>
#include <QTreeWidgetItem>

#include <QDebug>
#include <exception>

#include "mainsettings.h"
#include "ui_mainsettings.h"

/* MainSettings */
MainSettings::MainSettings(QWidget *parent) :
    QWidget(parent), ui(new Ui::MainSettings), xcb(nullptr), soundClick(":/sounds/small2")
{
    actionSettings = new QAction("Settings", this);
    actionExit = new QAction("Exit", this);

    auto version = QString("%1 version: %2").arg(QCoreApplication::applicationName()).arg(QCoreApplication::applicationVersion());
    auto github = QString("https://github.com/AndreyBarmaley/qxkb5");

    ui->setupUi(this);
    ui->tabWidget->setCurrentIndex(0);
    ui->aboutInfo->setText(QString("<center><b>%1</b></center><br><br>"
                                   "<p>Source code: <a href='%2'>%2</a></p>"
                                   "<p>Copyright © 2022 by Andrey Afletdinov <public.irkutsk@gmail.com></p>").arg(version).arg(github));

    configLoad();
    startupProcess();

    xcb = new XcbEventsPool(this);
    xcb->initXkbLayouts();

    cacheLoadItems();

    QMenu* menu = new QMenu(this);
    menu->addAction(actionSettings);
    menu->addSeparator();
    menu->addAction(actionExit);

    initXkbLayoutIcons(true);
    int index = xcb->getXkbLayout();

    trayIcon = new QSystemTrayIcon(this);
    trayIcon->setIcon(layoutIcons.at(index));
    trayIcon->setToolTip(version);
    trayIcon->setContextMenu(menu);
    trayIcon->show();

    connect(actionSettings, SIGNAL(triggered()), this, SLOT(show()));
    connect(actionExit, SIGNAL(triggered()), this, SLOT(exitProgram()));
    connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this, SLOT(iconActivated(QSystemTrayIcon::ActivationReason)));
    connect(xcb, SIGNAL(activeWindowNotify(int)), this, SLOT(activeWindowChanged(int)));
    connect(xcb, SIGNAL(xkbStateNotify(int)), this, SLOT(xkbStateChanged(int)));
    connect(xcb, SIGNAL(shutdownNotify()), this, SLOT(exitProgram()));
    connect(xcb, SIGNAL(xkbNamesChanged()), this, SLOT(iconAttributeChanged()));
    connect(this, SIGNAL(iconAttributeNotify()), this, SLOT(iconAttributeChanged()));

    // start events pool thread mode
    xcb->start();
}

MainSettings::~MainSettings()
{
    delete ui;
}

void MainSettings::startupProcess(void)
{
    if(ui->checkBoxStartup->isChecked() && !ui->lineEditStartup->text().isEmpty())
    {
        QProcess process(this);
        process.start(ui->lineEditStartup->text());
        if(process.waitForFinished())
            startupCmd = ui->lineEditStartup->text();
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

void MainSettings::allowIconsPath(bool f)
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
          ui->checkBoxSound->isChecked();
}

void MainSettings::configLoad(void)
{
    auto localData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(localData);
    auto configPath = QDir(localData).absoluteFilePath("config");

    QFile file(configPath);
    if(! file.open(QIODevice::ReadOnly))
        return;

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

        auto & names = xcb->getListNames();

        QString layout1 = names.size() > layout2 ? names.at(layout2) : names.front();
        QString state1 = layoutStateName(state2);

        auto item = new QTreeWidgetItem(QStringList() << class1 << class2 << layout1 << state1);
        item->setData(2, Qt::UserRole, layout2);
        item->setData(3, Qt::UserRole, state2);

        setHighlightStatusItem(item, state2);
        ui->treeWidgetCache->addTopLevelItem(item);
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
        auto & names = xcb->getListNames();
        int layout2 = (item->data(2, Qt::UserRole).toInt() + 1) % names.size();

        item->setText(2, names.at(layout2));
        item->setData(2, Qt::UserRole, layout2);
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

void MainSettings::activeWindowChanged(int win)
{
    auto list = xcb->getPropertyStringList(win, XCB_ATOM_WM_CLASS);
    if(list.empty()) return;

    auto layout1 = xcb->getXkbLayout();
    auto & names = xcb->getListNames();

    auto item = cacheFindItem(list.front(), list.back());
    if(item)
    {
        auto layout2 = item->data(2, Qt::UserRole).toInt();

        if(layout2 != layout1)
            xcb->switchXkbLayout(layout2);
    }
    else
    {
        auto item = new QTreeWidgetItem(QStringList() << list.front() << list.back() << names.at(layout1) << "normal");
        item->setData(2, Qt::UserRole, layout1);
        item->setData(3, Qt::UserRole, int(LayoutState::StateNormal));
        ui->treeWidgetCache->addTopLevelItem(item);
    }
}

void MainSettings::xkbStateChanged(int layout1)
{
    auto win = xcb->getActiveWindow();
    if(win == XCB_WINDOW_NONE)
        return;

    auto list = xcb->getPropertyStringList(win, XCB_ATOM_WM_CLASS);
    if(list.empty()) return;

    auto & names = xcb->getListNames();

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
            if(state2 == LayoutState::StateNormal)
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
    }
    else
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

void MainSettings::initXkbLayoutIcons(bool initXkbLayer)
{
    if(initXkbLayer)
        xcb->initXkbLayouts();

    ui->systemInfo->setText(QString("xkb info: %1").arg(xcb->getSymbolsLabel()));
    layoutIcons.clear();

    for(auto & name : xcb->getListNames())
        layoutIcons << getLayoutIcon(name);
}

/* XcbConnection */
XcbConnection::XcbConnection() :
    conn{ xcb_connect(":0", nullptr), xcb_disconnect },
    xkbctx{ nullptr, xkb_context_unref }, xkbmap{ nullptr, xkb_keymap_unref }, xkbstate{ nullptr, xkb_state_unref },
    xkbext(nullptr), root(XCB_WINDOW_NONE), symbolsNameAtom(XCB_ATOM_NONE), activeWindowAtom(XCB_ATOM_NONE), xkbdevid(-1)
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
    activeWindowAtom = getAtom("_NET_ACTIVE_WINDOW");

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

    if(auto reply = xcbReply.reply())
    {
        const char* name = xcb_get_atom_name_name(reply.get());
        size_t len = xcb_get_atom_name_name_length(reply.get());
        return QString(QByteArray(name, len));
    }

    return QString("NONE");
}

QString XcbConnection::getSymbolsLabel(void) const
{
    return getAtomName(symbolsNameAtom);
}

const QStringList & XcbConnection::getListNames(void) const
{
    return listNames;
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
    return getPropertyWindow(root, activeWindowAtom);
}

void XcbConnection::initXkbLayouts(void)
{
    auto xcbReply = getReplyFunc2(xcb_xkb_get_names, conn.get(), XCB_XKB_ID_USE_CORE_KBD, XCB_XKB_NAME_DETAIL_GROUP_NAMES | XCB_XKB_NAME_DETAIL_SYMBOLS);

    if(xcbReply.error())
        throw std::runtime_error("xcb_xkb_get_names");

    if(auto reply = xcbReply.reply())
    {
        const void *buffer = xcb_xkb_get_names_value_list(reply.get());
        xcb_xkb_get_names_value_list_t list;

        xcb_xkb_get_names_value_list_unpack(buffer, reply->nTypes, reply->indicators, reply->virtualMods,
                                            reply->groupNames, reply->nKeys, reply->nKeyAliases, reply->nRadioGroups, reply->which, & list);
        int groups = xcb_xkb_get_names_value_list_groups_length(reply.get(), & list);

        symbolsNameAtom = list.symbolsName;
        listNames.clear();

        for(int ii = 0; ii < groups; ++ii)
            listNames << getAtomName(list.groups[ii]);
    }
}

bool XcbConnection::switchXkbLayout(int layout)
{
    if(listNames.size())
    {
        // next
        if(layout < 0)
            layout = (getXkbLayout() + 1) % listNames.size();

        auto cookie = xcb_xkb_latch_lock_state_checked(conn.get(), XCB_XKB_ID_USE_CORE_KBD, 0, 0, 1, layout, 0, 0, 0);
        if(! GenericError(xcb_request_check(conn.get(), cookie)))
            return true;
    }

    return false;
}

int XcbConnection::getXkbLayout(void) const
{
    auto xcbReply = getReplyFunc2(xcb_xkb_get_state, conn.get(), XCB_XKB_ID_USE_CORE_KBD);

    if(xcbReply.error())
        throw std::runtime_error("xcb_xkb_get_state");

    if(auto reply = xcbReply.reply())
        return reply->group;

    return 0;
}

xcb_window_t XcbConnection::getPropertyWindow(xcb_window_t win, xcb_atom_t prop, uint32_t offset) const
{
    auto xcbReply = getReplyFunc2(xcb_get_property, conn.get(), false, win, prop, XCB_ATOM_WINDOW, offset, 1);

    if(xcbReply.error())
        return XCB_WINDOW_NONE;

    if(auto reply = xcbReply.reply())
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

    if(auto reply = xcbReply.reply())
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
    connect(this, & XcbEventsPool::xkbStateResetNotify, [this](){ initXkbLayouts(); emit xkbNamesChanged(); });
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

            if(XCB_PROPERTY_NOTIFY == type)
            {
                auto pn = reinterpret_cast<xcb_property_notify_event_t*>(ev.get());
                if(pn && pn->atom == activeWindowAtom)
                {
                    activeWindow = getActiveWindow();
                    if(activeWindow != XCB_WINDOW_NONE)
                        emit activeWindowNotify(activeWindow);
                }
            }
            else
            if(xkbext->first_event == type)
            {
                auto xkbev = ev->pad0;
                if(XCB_XKB_MAP_NOTIFY == xkbev)
                {
                    //auto mn = reinterpret_cast<xcb_xkb_map_notify_event_t*>(ev.get());
                    resetMapState = true;
                }
                else
                if(XCB_XKB_NEW_KEYBOARD_NOTIFY == xkbev)
                {
                    auto kn = reinterpret_cast< xcb_xkb_new_keyboard_notify_event_t*>(ev.get());
                    if(kn->deviceID == xkbdevid && (kn->changed & XCB_XKB_NKN_DETAIL_KEYCODES))
                        resetMapState = true;
                }
                else
                if(xkbev == XCB_XKB_STATE_NOTIFY)
                {
                    auto sn = reinterpret_cast<xcb_xkb_state_notify_event_t*>(ev.get());
                    xkb_state_update_mask(xkbstate.get(), sn->baseMods, sn->latchedMods, sn->lockedMods,
                                                      sn->baseGroup, sn->latchedGroup, sn->lockedGroup);
                    if(sn->changed & XCB_XKB_STATE_PART_GROUP_STATE)
                        emit xkbStateNotify(sn->group);
                }

                if(resetMapState)
                {
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
