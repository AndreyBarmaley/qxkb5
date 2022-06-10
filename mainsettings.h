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

#ifndef MAINSETTINGS_H
#define MAINSETTINGS_H

#define VERSION 20220609

#include <QIcon>
#include <QList>
#include <QSound>
#include <QObject>
#include <QThread>
#include <QWidget>
#include <QAction>
#include <QString>
#include <QPixmap>
#include <QStringList>
#include <QCloseEvent>
#include <QSystemTrayIcon>
#include <QTreeWidgetItem>

#include <atomic>
#include <memory>
#include <functional>

#include "xcb/xcb.h"
#define explicit dont_use_cxx_explicit
#include "xcb/xkb.h"
#undef explicit
#include "xkbcommon/xkbcommon-x11.h"

namespace Ui {
    class MainSettings;
}

template<typename ReplyType>
struct GenericReply : std::shared_ptr<ReplyType>
{
    GenericReply(ReplyType* ptr) : std::shared_ptr<ReplyType>(ptr, std::free)
    {
    }
};

struct GenericError : std::shared_ptr<xcb_generic_error_t>
{
    GenericError(xcb_generic_error_t* err) : std::shared_ptr<xcb_generic_error_t>(err, std::free) {}
    QString toString(const char* func = nullptr) const;
};

struct GenericEvent : std::shared_ptr<xcb_generic_event_t>
{
    GenericEvent(xcb_generic_event_t* ev) : std::shared_ptr<xcb_generic_event_t>(ev, std::free) {}
    const xcb_generic_error_t*  toerror(void) const { return reinterpret_cast<const xcb_generic_error_t*>(get()); }
};

template<typename ReplyType>
struct ReplyError : std::pair<GenericReply<ReplyType>, GenericError>
{
    ReplyError(ReplyType* ptr, xcb_generic_error_t* err) : std::pair<GenericReply<ReplyType>, GenericError>(ptr, err)
    {
    }

    const GenericReply<ReplyType> & reply(void) const { return std::pair<GenericReply<ReplyType>, GenericError>::first; }
    const GenericError & error(void) const { return std::pair<GenericReply<ReplyType>, GenericError>::second; }
};

template<typename Reply, typename Cookie>
ReplyError<Reply> getReply1(std::function<Reply*(xcb_connection_t*, Cookie, xcb_generic_error_t**)> func, xcb_connection_t* conn, Cookie cookie)
{
    xcb_generic_error_t* error = nullptr;
    Reply* reply = func(conn, cookie, & error);
    return ReplyError<Reply>(reply, error);
}

struct XcbPropertyReply : GenericReply<xcb_get_property_reply_t>
{   
    uint32_t length(void) { return xcb_get_property_value_length(get()); }
    void* value(void) { return xcb_get_property_value(get()); }

    XcbPropertyReply(xcb_get_property_reply_t* ptr) : GenericReply<xcb_get_property_reply_t>(ptr) {}
    XcbPropertyReply(const GenericReply<xcb_get_property_reply_t> & ptr) : GenericReply<xcb_get_property_reply_t>(ptr) {}
};

struct XcbConnection
{
protected:
    std::unique_ptr<xcb_connection_t, decltype(xcb_disconnect)*> conn;
    std::unique_ptr<xkb_context, decltype(xkb_context_unref)*> xkbctx;
    std::unique_ptr<xkb_keymap, decltype(xkb_keymap_unref)*> xkbmap;
    std::unique_ptr<xkb_state, decltype(xkb_state_unref)*> xkbstate;
    const xcb_query_extension_reply_t* xkbext;
    xcb_window_t root;
    int32_t xkbdevid;
    xcb_atom_t atomActiveWindow;
    xcb_atom_t atomNetWmName;
    xcb_atom_t atomUtf8String;

public:
    XcbConnection();
    virtual ~XcbConnection(){}

    GenericError checkRequest(const xcb_void_cookie_t &) const;

    int getXkbLayout(void) const;
    bool switchXkbLayout(int layout = -1);
    QStringList getXkbNames(void) const;

    xcb_atom_t getAtom(const QString & name, bool create = true) const;

    XcbPropertyReply getPropertyAnyType(xcb_window_t win, xcb_atom_t prop, uint32_t offset, uint32_t length) const;
    xcb_atom_t getPropertyType(xcb_window_t, xcb_atom_t) const;

    xcb_window_t getActiveWindow(void) const;
    xcb_window_t getPropertyWindow(xcb_window_t win, xcb_atom_t prop, uint32_t offset = 0) const;
    QString getPropertyString(xcb_window_t, xcb_atom_t) const;

    QString getAtomName(xcb_atom_t) const;

    QString getWindowName(xcb_window_t) const;
    bool setWindowName(xcb_window_t, const std::string &);

    void setWindowEvents(xcb_window_t, uint32_t mask);

    QString getSymbolsLabel(void) const;
    QStringList getPropertyStringList(xcb_window_t win, xcb_atom_t prop) const;

    template<typename Reply, typename Cookie>
    ReplyError<Reply> getReply2(std::function<Reply*(xcb_connection_t*, Cookie, xcb_generic_error_t**)> func, Cookie cookie) const
    {
        return getReply1<Reply, Cookie>(func, conn.get(), cookie);
    }

#define getReplyFunc2(NAME,conn,...) getReply2<NAME##_reply_t,NAME##_cookie_t>(NAME##_reply,NAME(conn,##__VA_ARGS__))
};

class XcbEventsPool : public QThread, public XcbConnection
{
    Q_OBJECT

    std::atomic<bool> shutdown;

public:
    XcbEventsPool(QObject*);
    ~XcbEventsPool();

protected:
    void run() override;

signals:
    void keycodePressNotify(int, int);
    void windowTitleNotify(int);
    void activeWindowNotify(int);
    void shutdownNotify(void);
    void xkbStateNotify(int);
    void xkbStateResetNotify(void);
    void xkbNamesChanged(void);
};

enum LayoutState { StateNormal, StateFirst, StateFixed };

class MainSettings : public QWidget
{
    Q_OBJECT

    Ui::MainSettings* ui;
    XcbEventsPool* xcb;
    QSystemTrayIcon* trayIcon;
    QAction* actionSettings;
    QAction* actionExit;
    QList<QIcon> layoutIcons;
    QSound soundClick;
    QString startupCmd;
    QStringList skipClasses;
    xcb_window_t prevWindow;

public:
    explicit MainSettings(const QString & config, QWidget *parent = 0);
    ~MainSettings();

protected:
    void closeEvent(QCloseEvent*) override;
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;
    QPixmap getLayoutIcon(const QString &);
    QTreeWidgetItem* cacheFindItem(const QString & class1, const QString & class2);
    void cacheSaveItems(void);
    void cacheLoadItems(void);
    void configSave(void);
    bool configLoadLocal(void);
    bool configLoadGlobal(const QString &);
    void initXkbLayoutIcons(void);
    void startupProcess(void);
    void windowRestoreTitle(xcb_window_t);
    void windowUpdateTitle(xcb_window_t, const QString &, const QString &);

private slots:
    void iconActivated(QSystemTrayIcon::ActivationReason reason);
    void exitProgram(void);
    void activeWindowChanged(int);
    void xkbStateChanged(int);
    void windowTitleChanged(int);
    void selectBackgroundColor(void);
    void selectTextColor(void);
    void selectFont(void);
    void setBackgroundTransparent(bool);
    void selectIconsPath(void);
    void iconAttributeChanged(void);
    void cacheItemClicked(QTreeWidgetItem*, int);
    void allowIconsPath(bool);
    void allowPictureMode(bool);

signals:
    void iconAttributeNotify(void);
};

#endif // MAINSETTINGS_H
