/****************************************************************************
**
** Copyright (C) 2015 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the plugins module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL21$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see http://www.qt.io/terms-conditions. For further
** information use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 or version 3 as published by the Free
** Software Foundation and appearing in the file LICENSE.LGPLv21 and
** LICENSE.LGPLv3 included in the packaging of this file. Please review the
** following information to ensure the GNU Lesser General Public License
** requirements will be met: https://www.gnu.org/licenses/lgpl.html and
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** As a special exception, The Qt Company gives you certain additional
** rights. These rights are described in The Qt Company LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef QEVDEVTOUCHHANDLER_P_H
#define QEVDEVTOUCHHANDLER_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

#include <QObject>
#include <QString>
#include <QList>
#include <QThread>
#include <qpa/qwindowsysteminterface.h>
#include <linux/input.h>

#if !defined(QT_NO_MTDEV)
struct mtdev;
#endif

#define EVDEBUG 1

QT_BEGIN_NAMESPACE

class QSocketNotifier;
class QEvdevTouchScreenData;

class QEvdevTouchScreenHandler : public QObject
{
    Q_OBJECT

public:
    explicit QEvdevTouchScreenHandler(const QString &device, const QString &spec = QString(), QObject *parent = 0);
    ~QEvdevTouchScreenHandler();

private slots:
    void readData();

private:
    QSocketNotifier *m_notify;
    int m_fd;
    QEvdevTouchScreenData *d;
#if !defined(QT_NO_MTDEV)
    mtdev *m_mtdev;
#endif
};

class QEvdevTouchScreenHandlerThread : public QThread
{
public:
    explicit QEvdevTouchScreenHandlerThread(const QString &device, const QString &spec, QObject *parent = 0);
    ~QEvdevTouchScreenHandlerThread();
    void run() Q_DECL_OVERRIDE;
    QEvdevTouchScreenHandler *handler() { return m_handler; }

private:
    QString m_device;
    QString m_spec;
    QEvdevTouchScreenHandler *m_handler;
};

class QEvDevLinkedTouchHandler
{
public:
    QEvDevLinkedTouchHandler(const QString &, int);
    ~QEvDevLinkedTouchHandler();
    int readData(::input_event *buffer, int sizeof_buffer);

    int m_tsID;
    int m_fd;
    QString m_deviceNode;

private:
};

class QEvDevLinkedTouchHandlerThread : public QThread
{
public:
    explicit QEvDevLinkedTouchHandlerThread(QHash<QString, QEvDevLinkedTouchHandler *> *);
//    ~QEvDevLinkedTouchHandlerThread();
    void run() Q_DECL_OVERRIDE;
    input_event *prepareEvent(input_event *e, int tsID);
//    QEvdevTouchScreenHandler *handler() { return m_handler; }

private:
//    QString m_device;
//    QString m_spec;
//    QEvdevTouchScreenHandler *m_handler;
    QHash<QString, QEvDevLinkedTouchHandler *> *m_activeLinkedDevices;
    QEvdevTouchScreenData *d;

#ifdef EVDEBUG
    const char *getEventCodeString(int eventType, int eventCode);
    QHash<int, const char *> eTypes; // event type table, only for debug
    QHash<int, const char *> absCodes; // absolute event codes table, only for debug
    QHash<int, const char *> synCodes; // syn event codes table, only for debug
    QHash<int, const char *> keyCodes; // key event codes table, only for debug
#endif
};

QT_END_NAMESPACE

#endif // QEVDEVTOUCH_P_H
