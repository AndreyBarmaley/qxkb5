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
  QWidget(parent), ui(new Ui::MainSettings), xcb(nullptr)
{
  actionSettings = new QAction("Settings", this);
  actionExit = new QAction("Exit", this);
  auto version = QString("%1 version: %2\n").arg(QCoreApplication::applicationName()).arg(QCoreApplication::applicationVersion());
  ui->setupUi(this);
  ui->tabWidget->setCurrentIndex(0);
  ui->aboutInfo->appendPlainText(version);
  ui->aboutInfo->appendPlainText("Source code: https://github.com/AndreyBarmaley/qxkb5\n");
  ui->aboutInfo->appendPlainText("Copyright © 2021 by Andrey Afletdinov <public.irkutsk@gmail.com>\n");

  mapLoadItems();
  loadConfig();

  if(ui->checkBoxStartup)
  {
    QProcess process;
    process.start(ui->lineEditStartup->text());
    process.waitForFinished();
  }

  QMenu* menu = new QMenu(this);
  menu->addAction(actionSettings);
  menu->addSeparator();
  menu->addAction(actionExit);

  xcb = new XcbEventsPool(this);
  initXkbLayoutIcons();
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

  // start events pool thread mode
  xcb->start();
}

MainSettings::~MainSettings()
{
  delete ui;
}

void MainSettings::exitProgram(void)
{
  mapSaveItems();
  saveConfig();
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
    event->ignore();
    hide();
  }
}

void MainSettings::selectBackgroundColor(void)
{
  QColorDialog dialog(this);
  dialog.setCurrentColor(QColor(ui->lineEditBackgroundColor->text()));
  if(dialog.exec())
  {
    ui->lineEditBackgroundColor->setText(dialog.selectedColor().name());
    initXkbLayoutIcons();

    int index = xcb->getXkbLayout();
    trayIcon->setIcon(layoutIcons.at(index));
  }
}

void MainSettings::selectTextColor(void)
{
  QColorDialog dialog(this);
  dialog.setCurrentColor(QColor(ui->lineEditTextColor->text()));
  if(dialog.exec())
  {
    ui->lineEditTextColor->setText(dialog.selectedColor().name());
    initXkbLayoutIcons();

    int index = xcb->getXkbLayout();
    trayIcon->setIcon(layoutIcons.at(index));
  }
}

void MainSettings::selectFont(void)
{
  auto fontArgs = ui->lineEditFont->text().split(", ");
  QFontDialog dialog(this);
  dialog.setCurrentFont(QFont(fontArgs.front(), fontArgs.back().toInt()));
  if(dialog.exec())
  {
    auto font = dialog.selectedFont();
    ui->lineEditFont->setText(QString("%1, %2").arg(font.family()).arg(font.pointSize()));
    initXkbLayoutIcons();

    int index = xcb->getXkbLayout();
    trayIcon->setIcon(layoutIcons.at(index));
  }
}

void MainSettings::iconActivated(QSystemTrayIcon::ActivationReason reason)
{
  if(reason == QSystemTrayIcon::Trigger)
    xcb->switchXkbLayout();
}

void MainSettings::saveConfig(void)
{
  auto localData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  QDir().mkpath(localData);
  auto configPath = QDir(localData).absoluteFilePath("config");

  QFile file(configPath);
  if(! file.open(QIODevice::WriteOnly))
    return;

  QDataStream ds(&file);

  ds << ui->checkBoxStartup->isChecked() <<
        ui->lineEditBackgroundColor->text() <<
        ui->lineEditTextColor->text() <<
        ui->lineEditFont->text();
}

void MainSettings::loadConfig(void)
{
  auto localData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  QDir().mkpath(localData);
  auto configPath = QDir(localData).absoluteFilePath("config");

  QFile file(configPath);
  if(! file.open(QIODevice::ReadOnly))
    return;

  QDataStream ds(&file);
  bool startup;
  ds >> startup;
  ui->checkBoxStartup->setChecked(startup);

  QString backgroundColor, textColor, font;
  ds >> backgroundColor >> textColor >> font;

  ui->lineEditBackgroundColor->setText(backgroundColor);
  ui->lineEditTextColor->setText(textColor);
  ui->lineEditFont->setText(font);
}

void MainSettings::mapSaveItems(void)
{
  auto localData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  QDir().mkpath(localData);
  auto cachePath = QDir(localData).absoluteFilePath("cache");

  QFile file(cachePath);
  if(! file.open(QIODevice::WriteOnly))
    return;

  QDataStream ds(&file);
  int count = ui->treeWidgetCache->topLevelItemCount();
  ds << count;

  for(int cur = 0; cur < count; ++cur)
  {
    auto item = ui->treeWidgetCache->topLevelItem(cur);
    ds << item->text(0) << item->text(1) << item->text(2) << item->data(2,Qt::UserRole).toInt();
  }
}

void MainSettings::mapLoadItems(void)
{
  auto localData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  QDir().mkpath(localData);
  auto cachePath = QDir(localData).absoluteFilePath("cache");

  QFile file(cachePath);
  if(! file.open(QIODevice::ReadOnly))
    return;

  QDataStream ds(&file);
  ui->treeWidgetCache->clear();

  int count; ds >> count;
  for(int cur = 0; cur < count; ++cur)
  {
    QString class1, class2, label;
    int layout;

    ds >> class1 >> class2 >> label >> layout;

    auto item = new QTreeWidgetItem(QStringList() << class1 << class2 << label);
    item->setData(2, Qt::UserRole, layout);
    ui->treeWidgetCache->addTopLevelItem(item);
  }
}

QTreeWidgetItem* MainSettings::mapFindItem(const QString & class1, const QString & class2)
{
  auto items1 = ui->treeWidgetCache->findItems(class1, Qt::MatchFixedString, 0);
  auto items2 = ui->treeWidgetCache->findItems(class2, Qt::MatchFixedString, 1);

  for(auto & item : items1)
    if(items2.contains(item)) return item;
  return nullptr;
}

void MainSettings::activeWindowChanged(int win)
{
  auto list = xcb->getPropertyStringList(win, XCB_ATOM_WM_CLASS);
  auto current = xcb->getXkbLayout();
  auto names = xcb->getListNames();

  auto item = mapFindItem(list.front(), list.back());
  if(item)
  {
    auto val = item->data(2,Qt::UserRole).toInt();
    if(val != current)
      xcb->switchXkbLayout(val);
  }
  else
  {
    auto item = new QTreeWidgetItem(QStringList() << list.front() << list.back() << names.at(current));
    item->setData(2, Qt::UserRole, current);
    ui->treeWidgetCache->addTopLevelItem(item);
  }
}

void MainSettings::xkbStateChanged(int layout)
{
  auto win = xcb->getActiveWindow();
  if(win == XCB_WINDOW_NONE)
    return;

  auto names = xcb->getListNames();
  auto list = xcb->getPropertyStringList(win, XCB_ATOM_WM_CLASS);

  auto item = mapFindItem(list.front(), list.back());
  if(item)
  {
    auto val = item->data(2,Qt::UserRole).toInt();
    if(val != layout)
      item->setData(2, Qt::UserRole, layout);
  }
  else
  {
    auto item = new QTreeWidgetItem(QStringList() << list.front() << list.back() << names.at(layout));
    item->setData(2, Qt::UserRole, layout);
    ui->treeWidgetCache->addTopLevelItem(item);
  }

  if(layout < layoutIcons.size())
    trayIcon->setIcon(layoutIcons.at(layout));
}

QPixmap MainSettings::getLayoutIcon(const QString & layoutName)
{
  QImage image(32, 32, QImage::Format_RGBA8888);
  QPainter painter(&image);
  painter.fillRect(image.rect(), QColor(ui->lineEditBackgroundColor->text()));
  painter.setPen(QColor(ui->lineEditTextColor->text()));
  auto fontArgs = ui->lineEditFont->text().split(", ");
  painter.setFont(QFont(fontArgs.front(), fontArgs.back().toInt()));
  painter.drawText(image.rect(), Qt::AlignCenter, layoutName.left(2));
  return QPixmap::fromImage(image);
}

void MainSettings::initXkbLayoutIcons(void)
{
  if(xcb)
  {
    xcb->initXkbLayouts();

    ui->systemInfo->setText(xcb->getSymbolsLabel());
    layoutIcons.clear();

    for(auto & name : xcb->getListNames())
      layoutIcons << getLayoutIcon(name);
  }
}

/* XcbConnection */
XcbConnection::XcbConnection() : conn(nullptr), root(XCB_WINDOW_NONE), symbolsNameAtom(XCB_ATOM_NONE), activeWindowAtom(XCB_ATOM_NONE),
  xkbext(nullptr), xkbctx{ nullptr, xkb_context_unref }, xkbmap{ nullptr, xkb_keymap_unref }, xkbstate{ nullptr, xkb_state_unref }, xkbdevid(-1)
{
  conn = xcb_connect(":0", nullptr);
  if(xcb_connection_has_error(conn))
    throw std::runtime_error("xcb_connect");

  auto setup = xcb_get_setup(conn);
  if(! setup)
    throw std::runtime_error("xcb_get_setup");

  auto screen = xcb_setup_roots_iterator(setup).data;
  if(! screen)
    throw std::runtime_error("xcb_setup_roots");

  root = screen->root;
  activeWindowAtom = getAtom("_NET_ACTIVE_WINDOW");

  xkbext = xcb_get_extension_data(conn, &xcb_xkb_id);
  if(! xkbext)
    throw std::runtime_error("xkb_get_extension_data");

  auto xcbReply = getReplyFunc2(xcb_xkb_use_extension, conn, XCB_XKB_MAJOR_VERSION, XCB_XKB_MINOR_VERSION);

  if(xcbReply.error())
    throw std::runtime_error("xcb_xkb_use_extension");

  xkbdevid = xkb_x11_get_core_keyboard_device_id(conn);
  if(xkbdevid < 0)
    throw std::runtime_error("xkb_x11_get_core_keyboard_device_id");

  xkbctx.reset(xkb_context_new(XKB_CONTEXT_NO_FLAGS));
  if(! xkbctx)
    throw std::runtime_error("xkb_context_new");

  xkbmap.reset(xkb_x11_keymap_new_from_device(xkbctx.get(), conn, xkbdevid, XKB_KEYMAP_COMPILE_NO_FLAGS));
  if(!xkbmap)
    throw std::runtime_error("xkb_x11_keymap_new_from_device");

  xkbstate.reset(xkb_x11_state_new_from_device(xkbmap.get(), conn, xkbdevid));
  if(!xkbstate)
    throw std::runtime_error("xkb_x11_state_new_from_device");

  uint16_t required_map_parts = (XCB_XKB_MAP_PART_KEY_TYPES | XCB_XKB_MAP_PART_KEY_SYMS | XCB_XKB_MAP_PART_MODIFIER_MAP |
                                 XCB_XKB_MAP_PART_EXPLICIT_COMPONENTS | XCB_XKB_MAP_PART_KEY_ACTIONS | XCB_XKB_MAP_PART_VIRTUAL_MODS | XCB_XKB_MAP_PART_VIRTUAL_MOD_MAP);
  uint16_t required_events = ( XCB_XKB_EVENT_TYPE_NEW_KEYBOARD_NOTIFY | XCB_XKB_EVENT_TYPE_MAP_NOTIFY | XCB_XKB_EVENT_TYPE_STATE_NOTIFY);

  auto cookie = xcb_xkb_select_events_checked(conn, xkbdevid, required_events, 0, required_events, required_map_parts, required_map_parts, nullptr);
  if(GenericError(xcb_request_check(conn, cookie)))
    throw std::runtime_error("xcb_xkb_select_events");

  const uint32_t values[] = { XCB_EVENT_MASK_PROPERTY_CHANGE };
  xcb_change_window_attributes(conn, root, XCB_CW_EVENT_MASK, values);
  xcb_flush(conn);
}

XcbConnection::~XcbConnection()
{
  xcb_disconnect(conn);
}

QString XcbConnection::getAtomName(xcb_atom_t atom)
{
  auto xcbReply = getReplyFunc2(xcb_get_atom_name, conn, atom);

  if(auto reply = xcbReply.reply())
  {
    const char* name = xcb_get_atom_name_name(reply.get());
    size_t len = xcb_get_atom_name_name_length(reply.get());
    return QString(QByteArray(name, len));
  }

  return QString("NONE");
}

QString XcbConnection::getSymbolsLabel(void)
{
  return getAtomName(symbolsNameAtom);
}

const QStringList & XcbConnection::getListNames(void)
{
  return listNames;
}

xcb_atom_t XcbConnection::getAtom(const QString & name, bool create) const
{
  auto xcbReply = getReplyFunc2(xcb_intern_atom, conn, create ? 0 : 1, name.length(), name.toStdString().c_str());

  if(xcbReply.error())
    return XCB_ATOM_NONE;

  return xcbReply.reply() ? xcbReply.reply()->atom : XCB_ATOM_NONE;
}

xcb_window_t XcbConnection::getActiveWindow(void)
{
  return getPropertyWindow(root, activeWindowAtom);
}

void XcbConnection::initXkbLayouts(void)
{
  auto xcbReply = getReplyFunc2(xcb_xkb_get_names, conn, XCB_XKB_ID_USE_CORE_KBD, XCB_XKB_NAME_DETAIL_GROUP_NAMES | XCB_XKB_NAME_DETAIL_SYMBOLS);

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

    auto cookie = xcb_xkb_latch_lock_state_checked(conn, XCB_XKB_ID_USE_CORE_KBD, 0, 0, 1, layout, 0, 0, 0);
    if(! GenericError(xcb_request_check(conn, cookie)))
      return true;
  }

  return false;
}

int XcbConnection::getXkbLayout(void)
{
  auto xcbReply = getReplyFunc2(xcb_xkb_get_state, conn, XCB_XKB_ID_USE_CORE_KBD);

  if(xcbReply.error())
    throw std::runtime_error("xcb_xkb_get_state");

  if(auto reply = xcbReply.reply())
    return reply->group;

  return 0;
}

xcb_window_t XcbConnection::getPropertyWindow(xcb_window_t win, xcb_atom_t prop, uint32_t offset)
{
  auto xcbReply = getReplyFunc2(xcb_get_property, conn, false, win, prop, XCB_ATOM_WINDOW, offset, 1);

  if(xcbReply.error())
    return XCB_WINDOW_NONE;

  if(auto reply = xcbReply.reply())
  {
    if(auto res = static_cast<xcb_window_t*>(xcb_get_property_value(reply.get())))
      return *res;
  }

  return XCB_WINDOW_NONE;
}

QStringList XcbConnection::getPropertyStringList(xcb_window_t win, xcb_atom_t prop)
{
  auto xcbReply = getReplyFunc2(xcb_get_property, conn, false, win, prop, XCB_ATOM_STRING, 0, ~0);
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
}

XcbEventsPool::~XcbEventsPool()
{
  shutdown = true;
  for(int ii = 1; ii < 10; ++ii)
  {
    msleep(100);
    if(isFinished()) break;
  }
  if(isRunning()) exit();
}

void XcbEventsPool::run(void)
{
  // check current active window
  auto activeWindow = getActiveWindow();
  if(activeWindow != XCB_WINDOW_NONE)
    emit activeWindowNotify(activeWindow);

  // events
  while(0 == xcb_connection_has_error(conn))
  {
    if(shutdown)
      break;

    while(auto ev = GenericEvent(xcb_poll_for_event(conn)))
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
            xkbmap.reset(xkb_x11_keymap_new_from_device(xkbctx.get(), conn, xkbdevid, XKB_KEYMAP_COMPILE_NO_FLAGS));
            xkbstate.reset(xkb_x11_state_new_from_device(xkbmap.get(), conn, xkbdevid));
          }
        }
    }

    msleep(1);
  }
}
