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

#include "qevdevtouchmanager_p.h"
#include "qevdevtouchhandler_p.h"

#include <QStringList>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QtPlatformSupport/private/qdevicediscovery_p.h>
#include <private/qguiapplication_p.h>
#include <private/qinputdevicemanager_p_p.h>

QT_BEGIN_NAMESPACE

Q_DECLARE_LOGGING_CATEGORY(qLcEvdevTouch)

QEvdevTouchManager::QEvdevTouchManager(const QString &key, const QString &specification, QObject *parent)
    : QObject(parent)
{
    Q_UNUSED(key);

    if (qEnvironmentVariableIsSet("QT_QPA_EVDEV_DEBUG"))
        const_cast<QLoggingCategory &>(qLcEvdevTouch()).setEnabled(QtDebugMsg, true);

    QString spec = QString::fromLocal8Bit(qgetenv("QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS"));

    if (spec.isEmpty())
        spec = specification;

    QStringList args = spec.split(QLatin1Char(':'));
    QStringList devices;

    foreach (const QString &arg, args) {
        if (arg.startsWith(QLatin1String("/dev/"))) {
            devices.append(arg);
            args.removeAll(arg);
        }
    }

    // build new specification without /dev/ elements
    m_spec = args.join(QLatin1Char(':'));

//    foreach (const QString &device, devices)
//        addDevice(device);
    foreach (const QString &device, devices)
        addLinkedDevice(device);

    m_handlerThread = new QEvDevLinkedTouchHandlerThread(&m_activeLinkedDevices);
    m_handlerThread->start();

    // when no devices specified, use device discovery to scan and monitor
    if (devices.isEmpty()) {
        qCDebug(qLcEvdevTouch) << "evdevtouch: Using device discovery";
        m_deviceDiscovery = QDeviceDiscovery::create(QDeviceDiscovery::Device_Touchpad | QDeviceDiscovery::Device_Touchscreen, this);
        if (m_deviceDiscovery) {
            QStringList devices = m_deviceDiscovery->scanConnectedDevices();
            foreach (const QString &device, devices)
                addDevice(device);
            connect(m_deviceDiscovery, SIGNAL(deviceDetected(QString)), this, SLOT(addDevice(QString)));
            connect(m_deviceDiscovery, SIGNAL(deviceRemoved(QString)), this, SLOT(removeDevice(QString)));
        }
    }
}

QEvdevTouchManager::~QEvdevTouchManager()
{
    m_handlerThread->quit();
    m_handlerThread->wait();
    qDeleteAll(m_activeDevices);
    qDeleteAll(m_activeLinkedDevices);
}

QEvDevLinkedTouchHandler::QEvDevLinkedTouchHandler(const QString &deviceNode, int tsID)
{
    m_deviceNode = deviceNode;
    m_tsID = tsID;
    qDebug() << "Adding device at" << m_deviceNode << m_tsID;

    m_fd = QT_OPEN(m_deviceNode.toLocal8Bit().constData(), O_RDONLY | O_NDELAY, 0);

    if (m_fd < 0) {
        qErrnoWarning(errno, "evdevtouch: Cannot open input device %s", qPrintable(m_deviceNode));
        return;
    }
}

QEvDevLinkedTouchHandler::~QEvDevLinkedTouchHandler()
{
    qDebug() << "Removing device" << m_deviceNode;
    if (m_fd >= 0)
        QT_CLOSE(m_fd);
}

int QEvDevLinkedTouchHandler::readData(::input_event *buffer, int sizeof_buffer)
{
//    ::input_event buffer[32];
    int events = 0;

    int n = 0;
    for (; ;) {
        events = QT_READ(m_fd, reinterpret_cast<char*>(buffer) + n, sizeof_buffer - n);
        if (events <= 0)
            goto err;
        n += events;
        if (n % sizeof(::input_event) == 0)
            break;
    }

//    n /= sizeof(::input_event);
//    for (int i = 0; i < n; ++i)
//        d->processInputEvent(&buffer[i]);

    return events;

err:
    if (!events) {
        qWarning("evdevtouch: Got EOF from input device");
        return -1;
    } else if (events < 0) {
        if (errno != EINTR && errno != EAGAIN) {
            qErrnoWarning(errno, "evdevtouch: Could not read from input device");
            if (errno == ENODEV) { // device got disconnected -> stop reading
//                delete m_notify;
//                m_notify = 0;
                QT_CLOSE(m_fd);
                m_fd = -1;
            }
            return -1;
        }
    }
    return 0;
}

QEvDevLinkedTouchHandlerThread::QEvDevLinkedTouchHandlerThread(QHash<QString, QEvDevLinkedTouchHandler *> *activeLinkedDevices)
{
    qDebug() << "thread construct";
    m_activeLinkedDevices = activeLinkedDevices;
}

void QEvDevLinkedTouchHandlerThread::run()
{
    ::input_event buffer[32];
//    unsigned long *intBuffer = (unsigned long *) buffer;
    unsigned int events = 0;
    int n = 0;

    while(true)
    {
//        qDebug() << "thread run";
        foreach(QEvDevLinkedTouchHandler *linkedTouchHandler, *m_activeLinkedDevices)
        {
            while((events = linkedTouchHandler->readData(buffer, sizeof(buffer))))
            {
//                qDebug() << "device" << linkedTouchHandler->m_deviceNode << events / sizeof(::input_event);
//                for(unsigned int i = 0; i < events / sizeof(::input_event); i++)
//                    qDebug("%08lx %08lx %08lx %08lx", intBuffer[i * 4 + 0], intBuffer[i * 4 + 1], intBuffer[i * 4 + 2], intBuffer[i * 4 + 3]);
                n = events / sizeof(::input_event);
//                for(int i = 0; i < n; ++i)
//                    d->processInputEvent(&buffer[i]);
            }
//            qDebug() << "device" << linkedTouchHandler->m_deviceNode << events;
        }
        msleep(10);
    }
}

void QEvdevTouchManager::addDevice(const QString &deviceNode)
{
    qCDebug(qLcEvdevTouch) << "Adding device at" << deviceNode;
//    qDebug() << "Adding device at" << deviceNode << m_spec;
    QEvdevTouchScreenHandlerThread *handler;
    handler = new QEvdevTouchScreenHandlerThread(deviceNode, m_spec);
    if (handler) {
        m_activeDevices.insert(deviceNode, handler);
        QInputDeviceManagerPrivate::get(QGuiApplicationPrivate::inputDeviceManager())->setDeviceCount(
            QInputDeviceManager::DeviceTypeTouch, m_activeDevices.count());
    } else {
        qWarning("evdevtouch: Failed to open touch device %s", qPrintable(deviceNode));
    }
}

void QEvdevTouchManager::addLinkedDevice(const QString &deviceNode)
{
    qCDebug(qLcEvdevTouch) << "Adding device at" << deviceNode;
//    qDebug() << "Adding device at" << deviceNode;
    QEvDevLinkedTouchHandler *handler;
    handler = new QEvDevLinkedTouchHandler(deviceNode, m_activeLinkedDevices.count());
    if (handler) {
        m_activeLinkedDevices.insert(deviceNode, handler);
//        QInputDeviceManagerPrivate::get(QGuiApplicationPrivate::inputDeviceManager())->setDeviceCount(QInputDeviceManager::DeviceTypeTouch, m_activeDevices.count());
    } else {
        qWarning("evdevtouch: Failed to open touch device %s", qPrintable(deviceNode));
    }

//    QEvdevTouchScreenHandlerThread *handler;
//    handler = new QEvdevTouchScreenHandlerThread(deviceNode, m_spec);
//    if (handler) {
//        m_activeDevices.insert(deviceNode, handler);
//        QInputDeviceManagerPrivate::get(QGuiApplicationPrivate::inputDeviceManager())->setDeviceCount(
//            QInputDeviceManager::DeviceTypeTouch, m_activeDevices.count());
//    } else {
//        qWarning("evdevtouch: Failed to open touch device %s", qPrintable(deviceNode));
//    }
}

void QEvdevTouchManager::removeDevice(const QString &deviceNode)
{
    if (m_activeDevices.contains(deviceNode)) {
        qCDebug(qLcEvdevTouch) << "Removing device at" << deviceNode;
        QEvdevTouchScreenHandlerThread *handler = m_activeDevices.value(deviceNode);
        m_activeDevices.remove(deviceNode);
        QInputDeviceManagerPrivate::get(QGuiApplicationPrivate::inputDeviceManager())->setDeviceCount(
            QInputDeviceManager::DeviceTypeTouch, m_activeDevices.count());
        delete handler;
    }
}

QT_END_NAMESPACE
