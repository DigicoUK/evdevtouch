/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the plugins module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qevdevtouch_p.h"
#include <QStringList>
#include <QHash>
#include <QSocketNotifier>
#include <QGuiApplication>
#include <QDebug>
#include <QtCore/private/qcore_unix_p.h>
#include <QtPlatformSupport/private/qdevicediscovery_p.h>
#include <linux/input.h>

#ifdef USE_MTDEV
extern "C" {
#include <mtdev.h>
}
#endif

QT_BEGIN_NAMESPACE

//#define EVDEBUG 1 // debug the touch events

// class QEvdevTouchScreenEventDispatcher -----------------------------------------------------------------------------------------------------

QEvdevTouchScreenEventDispatcher::QEvdevTouchScreenEventDispatcher()
    : m_maxScreenId(0)
{
}

void QEvdevTouchScreenEventDispatcher::processInputEvent(int screenId, QTouchDevice *device, QList<QWindowSystemInterface::TouchPoint> &touchPoints)
{
//    qDebug("screenId: %d/%d", screenId, m_maxScreenId);
//    qDebug() << touchPoints;
    m_touchScreenList[screenId].m_touchPoints = touchPoints;
    m_touchScreenList[screenId].isDataArrived = true;
    if(screenId > m_maxScreenId)
        m_maxScreenId = screenId;

    Qt::TouchPointStates combinedStates = 0;
    for(int i = 0; i < touchPoints.size(); i++)
        combinedStates |= touchPoints[i].state;
    m_touchScreenList[screenId].m_combinedStates = combinedStates;

    if(combinedStates == Qt::TouchPointPressed)
        m_touchScreenList[screenId].hasTouchingFinger = true;

//    qDebug("m_touchScreenList.size(): %d, m_maxScreenId: %d", m_touchScreenList.size(), m_maxScreenId);
//    for(int i = 0; i <= m_maxScreenId; i++)
//    {
//        if(m_touchScreenList.contains(i))
//            QWindowSystemInterface::handleTouchEvent(0, device, m_touchScreenList[i].m_touchPoints);
//    }

//    if(isAllDataArrived())
    {
//        qDebug("isAllDataArrived() true");
//        char fingers1[] = "----------";
//        char fingers2[] = "----------";
        QList<QWindowSystemInterface::TouchPoint> allTouchPoints;
        for(int i = 0; i <= m_maxScreenId; i++)
        {
            if(m_touchScreenList.contains(i) && m_touchScreenList[i].hasTouchingFinger)
            {
//                QWindowSystemInterface::handleTouchEvent(0, device, m_touchScreenList[i].m_touchPoints);
                allTouchPoints += m_touchScreenList[i].m_touchPoints;
                m_touchScreenList[i].isDataArrived = false;
                if(m_touchScreenList[i].m_combinedStates == Qt::TouchPointReleased)
                    m_touchScreenList[i].hasTouchingFinger = false;
            }
        }
//        for(int i = 0; i < allTouchPoints.size(); i++)
//        {
//            if(allTouchPoints[i].id >= 10)
//                fingers2[allTouchPoints[i].id - 10] = '*';
//            else
//                fingers1[allTouchPoints[i].id] = '*';
//        }
//        qDebug("isAllDataArrived() true %d ... %d + %d = %d %s %s", m_touchScreenList.size(),
//                m_touchScreenList[0].m_touchPoints.size(),
//                m_touchScreenList[1].m_touchPoints.size(),
//                allTouchPoints.size(),
//                fingers1,
//                fingers2
//            );

        combinedStates = 0;
        for(int i = 0; i <= m_maxScreenId; i++)
        {
            if(m_touchScreenList.contains(i))
                combinedStates |= m_touchScreenList[i].m_combinedStates;
        }

        if(combinedStates != Qt::TouchPointStationary)
        {
//            qDebug() << allTouchPoints;
            QWindowSystemInterface::handleTouchEvent(0, device, allTouchPoints);
        }
    }
//    else qDebug("isAllDataArrived() false");

}

bool QEvdevTouchScreenEventDispatcher::isAllDataArrived()
{
    for(int i = 0; i <= m_maxScreenId; i++)
        if(m_touchScreenList.contains(i) && m_touchScreenList[i].hasTouchingFinger && !m_touchScreenList[i].isDataArrived)
            return false;
    return true;
}

// class QEvdevTouchScreenData -----------------------------------------------------------------------------------------------------

class QEvdevTouchScreenData
{
public:
    QEvdevTouchScreenData(QEvdevTouchScreenDevice *q_ptr, QEvdevTouchScreenEventDispatcher *eventDispatcher);

    void processInputEvent(input_event *data);
    void assignIds();

    QEvdevTouchScreenDevice *m_dev;
    int m_lastEventType;
    QList<QWindowSystemInterface::TouchPoint> m_touchPoints;

    struct Contact {
        int trackingId;
        int x;
        int y;
        int maj;
        int pressure;
        Qt::TouchPointState state;
        QTouchEvent::TouchPoint::InfoFlags flags;
        Contact() : trackingId(-1),
            x(0), y(0), maj(-1), pressure(0),
            state(Qt::TouchPointPressed), flags(0) { }
    };
    QHash<int, Contact> m_contacts; // The key is a tracking id for type A, slot number for type B.
    QHash<int, Contact> m_lastContacts;
    Contact m_currentData;
    int m_currentSlot;

    int findClosestContact(const QHash<int, Contact> &contacts, int x, int y, int *dist);
    void reportPoints();
    void registerDevice();
    const char *getEventCodeString(int eventType, int eventCode);

    int hw_range_x_min;
    int hw_range_x_max;
    int hw_range_y_min;
    int hw_range_y_max;
    int hw_pressure_min;
    int hw_pressure_max;
    QString hw_name;
    QTouchDevice *m_device;
    bool m_typeB;
    QEvdevTouchScreenEventDispatcher *m_eventDispatcher;

    QHash<int, const char *> eTypes; // event type table, only for debug
    QHash<int, const char *> absCodes; // absolute event codes table, only for debug
    QHash<int, const char *> synCodes; // syn event codes table, only for debug
    QHash<int, const char *> keyCodes; // key event codes table, only for debug
};

QEvdevTouchScreenData::QEvdevTouchScreenData(QEvdevTouchScreenDevice *q_ptr, QEvdevTouchScreenEventDispatcher *eventDispatcher)
    : m_dev(q_ptr),
      m_lastEventType(-1),
      m_currentSlot(0),
      hw_range_x_min(0), hw_range_x_max(0),
      hw_range_y_min(0), hw_range_y_max(0),
      hw_pressure_min(0), hw_pressure_max(0),
      m_device(0), m_typeB(false),
      m_eventDispatcher(eventDispatcher)
{
    qDebug("QEvdevTouchScreenData() id: %d", m_dev->m_screenId);

#ifdef EVDEBUG
#define INSERT(list, value) list[value] = #value;
    // event types
    INSERT(eTypes, EV_SYN);
    INSERT(eTypes, EV_KEY);
    INSERT(eTypes, EV_REL);
    INSERT(eTypes, EV_ABS);
    INSERT(eTypes, EV_MSC);
    INSERT(eTypes, EV_SW);
    INSERT(eTypes, EV_LED);
    INSERT(eTypes, EV_SND);
    INSERT(eTypes, EV_REP);
    INSERT(eTypes, EV_FF);
    INSERT(eTypes, EV_PWR);
    INSERT(eTypes, EV_FF_STATUS);

    INSERT(absCodes, ABS_X		);
    INSERT(absCodes, ABS_Y		);
    INSERT(absCodes, ABS_Z		);
    INSERT(absCodes, ABS_RX		);
    INSERT(absCodes, ABS_RY		);
    INSERT(absCodes, ABS_RZ		);
    INSERT(absCodes, ABS_THROTTLE);
    INSERT(absCodes, ABS_RUDDER	);
    INSERT(absCodes, ABS_WHEEL	);
    INSERT(absCodes, ABS_GAS	);
    INSERT(absCodes, ABS_BRAKE	);
    INSERT(absCodes, ABS_HAT0X	);
    INSERT(absCodes, ABS_HAT0Y	);
    INSERT(absCodes, ABS_HAT1X	);
    INSERT(absCodes, ABS_HAT1Y	);
    INSERT(absCodes, ABS_HAT2X	);
    INSERT(absCodes, ABS_HAT2Y	);
    INSERT(absCodes, ABS_HAT3X	);
    INSERT(absCodes, ABS_HAT3Y	);
    INSERT(absCodes, ABS_PRESSURE);
    INSERT(absCodes, ABS_DISTANCE);
    INSERT(absCodes, ABS_TILT_X	);
    INSERT(absCodes, ABS_TILT_Y	);
    INSERT(absCodes, ABS_TOOL_WIDTH);
    INSERT(absCodes, ABS_VOLUME		);
    INSERT(absCodes, ABS_MISC		);
    INSERT(absCodes, ABS_MT_SLOT	);
    INSERT(absCodes, ABS_MT_TOUCH_MAJOR);
    INSERT(absCodes, ABS_MT_TOUCH_MINOR);
    INSERT(absCodes, ABS_MT_WIDTH_MAJOR);
    INSERT(absCodes, ABS_MT_WIDTH_MINOR);
    INSERT(absCodes, ABS_MT_ORIENTATION);
    INSERT(absCodes, ABS_MT_POSITION_X);
    INSERT(absCodes, ABS_MT_POSITION_Y);
    INSERT(absCodes, ABS_MT_TOOL_TYPE);
    INSERT(absCodes, ABS_MT_BLOB_ID	);
    INSERT(absCodes, ABS_MT_TRACKING_ID);
    INSERT(absCodes, ABS_MT_PRESSURE);
    INSERT(absCodes, ABS_MT_DISTANCE);
    INSERT(absCodes, ABS_MT_TOOL_X	);
    INSERT(absCodes, ABS_MT_TOOL_Y	);

    INSERT(synCodes, SYN_REPORT		);
    INSERT(synCodes, SYN_CONFIG		);
    INSERT(synCodes, SYN_MT_REPORT	);
    INSERT(synCodes, SYN_DROPPED	);

    INSERT(keyCodes, BTN_TOOL_PEN		);
    INSERT(keyCodes, BTN_TOOL_RUBBER	);
    INSERT(keyCodes, BTN_TOOL_BRUSH		);
    INSERT(keyCodes, BTN_TOOL_PENCIL	);
    INSERT(keyCodes, BTN_TOOL_AIRBRUSH	);
    INSERT(keyCodes, BTN_TOOL_FINGER	);
    INSERT(keyCodes, BTN_TOOL_MOUSE		);
    INSERT(keyCodes, BTN_TOOL_LENS		);
    INSERT(keyCodes, BTN_TOOL_QUINTTAP	);
    INSERT(keyCodes, BTN_TOUCH			);
    INSERT(keyCodes, BTN_STYLUS			);
    INSERT(keyCodes, BTN_STYLUS2		);
    INSERT(keyCodes, BTN_TOOL_DOUBLETAP	);
    INSERT(keyCodes, BTN_TOOL_TRIPLETAP	);
    INSERT(keyCodes, BTN_TOOL_QUADTAP	);
#endif
}

void QEvdevTouchScreenData::registerDevice()
{
    m_device = new QTouchDevice;
    m_device->setName(hw_name);
    m_device->setType(QTouchDevice::TouchScreen);
    m_device->setCapabilities(QTouchDevice::Position | QTouchDevice::Area);
    if (hw_pressure_max > hw_pressure_min)
        m_device->setCapabilities(m_device->capabilities() | QTouchDevice::Pressure);

    QWindowSystemInterface::registerTouchDevice(m_device);
}

#ifdef EVDEBUG
const char *QEvdevTouchScreenData::getEventCodeString(int eventType, int eventCode)
{
    switch(eventType)
    {
        case EV_ABS: return absCodes.value(eventCode);
        case EV_KEY: return keyCodes.value(eventCode);
        case EV_SYN: return synCodes.value(eventCode);
        default: return "-";
    }
}
#endif

void QEvdevTouchScreenData::processInputEvent(input_event *data)
{
#ifdef EVDEBUG
    qDebug("screen%d %02x %s %04x %-20s %08x"
           , m_dev->m_screenId
           , data->type
           , eTypes.value(data->type)
           , data->code
           , getEventCodeString(data->type, data->code)
           , data->value
        );
#endif

    if (data->type == EV_ABS)
    {
        if (data->code == ABS_MT_POSITION_X)
        {
//            qDebug("    ABS_MT_POSITION_X: %d", (qint16) data->value + m_dev->m_screenId * 1280);
            m_currentData.x = (qint16) data->value;
            m_currentData.x = qBound(hw_range_x_min, m_currentData.x, hw_range_x_max) + m_dev->m_screenId * 1280;
            if (m_typeB)
                m_contacts[m_currentSlot].x = m_currentData.x;
        }
        else if (data->code == ABS_MT_POSITION_Y)
        {
//            qDebug("    ABS_MT_POSITION_Y: %d", (qint16) data->value);
            m_currentData.y = (qint16) data->value;
            m_currentData.y = qBound(hw_range_y_min, m_currentData.y, hw_range_y_max);
            if (m_typeB)
                m_contacts[m_currentSlot].y = m_currentData.y;
        }
        else if (data->code == ABS_MT_TRACKING_ID)
        {
//            qDebug("    ABS_MT_TRACKING_ID: %d, %d, %d", data->value, data->value + m_dev->m_screenId * 10, m_currentSlot);
            if(data->value >= 0)
                m_currentData.trackingId = data->value + m_dev->m_screenId * 10; // FIXME: hack for dual (multiple) touch screens. Can we do it nicer?
            else
                m_currentData.trackingId = data->value;

            if (m_typeB)
            {
                if (m_currentData.trackingId == -1)
                    m_contacts[m_currentSlot].state = Qt::TouchPointReleased;
                else
                    m_contacts[m_currentSlot].trackingId = m_currentData.trackingId;
            }
        }
        else if (data->code == ABS_MT_TOUCH_MAJOR)
        {
//            qDebug("    ABS_MT_TOUCH_MAJOR: %d", data->value);
            m_currentData.maj = data->value;
            if (data->value == 0)
                m_currentData.state = Qt::TouchPointReleased;
            if (m_typeB)
                m_contacts[m_currentSlot].maj = m_currentData.maj;
        }
        else if (data->code == ABS_PRESSURE)
        {
//            qDebug("    ABS_PRESSURE: %d", data->value);
            m_currentData.pressure = qBound(hw_pressure_min, data->value, hw_pressure_max);
            if (m_typeB)
                m_contacts[m_currentSlot].pressure = m_currentData.pressure;
        }
        else if (data->code == ABS_MT_SLOT)
        {
//            qDebug("    ABS_MT_SLOT: %d", data->value);
            m_currentSlot = data->value;
        }
//        else qDebug("    type %d unhandled event code: 0x%x %d", data->type, data->code, data->value);
            // #define ABS_MT_WIDTH_MAJOR 0x32 /* Major axis of approaching ellipse */ // (touch size?)
    }
    else if (data->type == EV_KEY && !m_typeB)
    {
//        qDebug("EV_KEY && !m_typeB: %d", data->value);
        if (data->code == BTN_TOUCH && data->value == 0)
            m_contacts[m_currentSlot].state = Qt::TouchPointReleased;
    }
    else if (data->type == EV_SYN && data->code == SYN_MT_REPORT && m_lastEventType != EV_SYN)
    {
//        qDebug("data->type == EV_SYN && data->code == SYN_MT_REPORT && m_lastEventType != EV_SYN");
        // If there is no tracking id, one will be generated later.
        // Until that use a temporary key.
        int key = m_currentData.trackingId;
        if (key == -1)
            key = m_contacts.count();

        m_contacts.insert(key, m_currentData);
        m_currentData = Contact();

    }
    else if (data->type == EV_SYN && data->code == SYN_REPORT)
    {
//        qDebug("data->type == EV_SYN && data->code == SYN_REPORT %d %d", m_dev->m_screenId, m_contacts.size());
        m_touchPoints.clear();
//        Qt::TouchPointStates combinedStates;
        QMutableHashIterator<int, Contact> contactsIterator(m_contacts);
        while (contactsIterator.hasNext())
        {
            contactsIterator.next();
//            struct TouchPoint {
//                TouchPoint() : id(0), pressure(0), state(Qt::TouchPointStationary), flags(0) { }
//                int id;                 // for application use
//                QPointF normalPosition; // touch device coordinates, (0 to 1, 0 to 1)
//                QRectF area;            // the touched area, centered at position in screen coordinates
//                qreal pressure;         // 0 to 1
//                Qt::TouchPointState state; //Qt::TouchPoint{Pressed|Moved|Stationary|Released}
//                QVector2D velocity;     // in screen coordinate system, pixels / seconds
//                QTouchEvent::TouchPoint::InfoFlags flags;
//                QVector<QPointF> rawPositions; // in screen coordinates
//            };
            QWindowSystemInterface::TouchPoint touchPoint;
            Contact &contact(contactsIterator.value());
            touchPoint.id = contact.trackingId;
//            tp.id = m_typeB ? it.key() : contact.trackingId;
            touchPoint.flags = contact.flags;

//            qDebug("contactsIterator.key(): %d, contact.trackingId: %d, m_currentSlot: %d", contactsIterator.key(), contact.trackingId, m_currentSlot);
            int key = m_typeB ? contactsIterator.key() : contact.trackingId;
//            tp.id = key;
            if (m_lastContacts.contains(key))
            {
                const Contact &prev(m_lastContacts.value(key));
                if (contact.state == Qt::TouchPointReleased)
                {
                    // Copy over the previous values for released points, just in case.
                    contact.x = prev.x;
                    contact.y = prev.y;
                    contact.maj = prev.maj;
                }
                else
                {
                    contact.state = (prev.x == contact.x && prev.y == contact.y)
                            ? Qt::TouchPointStationary : Qt::TouchPointMoved;
                }
            }

            // Avoid reporting a contact in released state more than once.
            if (contact.state == Qt::TouchPointReleased && !m_lastContacts.contains(key))
            {
                contactsIterator.remove();
                continue;
            }

            touchPoint.state = contact.state;
//            combinedStates |= tp.state;

            // Store the HW coordinates for now, will be updated later.
            touchPoint.area = QRectF(0, 0, contact.maj, contact.maj);
            touchPoint.area.moveCenter(QPoint(contact.x, contact.y));
            touchPoint.pressure = contact.pressure;

            // Get a normalized position in range 0..1.
            touchPoint.normalPosition = QPointF(contact.x, contact.y);

            m_touchPoints.append(touchPoint);

            if (contact.state == Qt::TouchPointReleased)
                contactsIterator.remove();
        }

//        qDebug("m_screenid: %d, m_contacts.size(): %d, m_touchPoints.size(): %d", m_dev->m_screenId, m_contacts.size(), m_touchPoints.size());

        m_lastContacts = m_contacts;
        if (!m_typeB)
            m_contacts.clear();

//        if (!m_touchPoints.isEmpty() && combinedStates != Qt::TouchPointStationary)
//            reportPoints();
        if (!m_touchPoints.isEmpty())
            reportPoints();
    }
//    else qDebug("unhandled case data->type: 0x%x, data->code: 0x%x, data->value: 0x%x", data->type, data->code, data->value);

    m_lastEventType = data->type;
}

void QEvdevTouchScreenData::reportPoints()
{
//    QRect winRect;
//    winRect = QGuiApplication::primaryScreen()->geometry();

    // Map the coordinates based on the normalized position. QPA expects 'area'
    // to be in screen coordinates.
//    const int pointCount = m_touchPoints.count();
//    for (int i = 0; i < pointCount; ++i)
//    {
//        QWindowSystemInterface::TouchPoint &tp(m_touchPoints[i]);

//        // Generate a screen position that is always inside the active window
//        // or the primary screen.
//        tp.area = QRectF(0, 0, 8, 8);
//        tp.area.moveCenter(QPointF(tp.normalPosition.x(), tp.normalPosition.y()));

//        // Calculate normalized pressure.
//        if (!hw_pressure_min && !hw_pressure_max)
//            tp.pressure = tp.state == Qt::TouchPointReleased ? 0 : 1;
//        else
//            tp.pressure = (tp.pressure - hw_pressure_min) / qreal(hw_pressure_max - hw_pressure_min);
//    }

//    qDebug("m_screenid: %d %d %d", q->m_screenid, m_contacts.size(), m_touchPoints.size());
//    qDebug() << m_touchPoints;

//    Qt::TouchPointStates combinedStates = 0;
//    const char *state = "";
//    for(int i = 0; i < m_touchPoints.size(); i++)
//    {
//        combinedStates |= m_touchPoints[i].state;
//    }
//    switch(combinedStates)
//    {
//        case Qt::TouchPointPressed:  state = "pressed";  break;
//        case Qt::TouchPointReleased: state = "released"; break;
//    }
//    if(state[0])
//        qDebug("combinedStates: %d, 0x%x %s", m_dev->m_screenid, combinedStates, state);

//    m_dev->m_deviceList->at(0)->m_d->

//    QWindowSystemInterface::handleTouchEvent(0, m_device, m_touchPoints);
    m_eventDispatcher->processInputEvent(m_dev->m_screenId, m_device, m_touchPoints);
}

// class QEvdevTouchScreenDevice ---------------------------------------------------------------------------------------------------

#define LONG_BITS (sizeof(long) << 3)
#define NUM_LONGS(bits) (((bits) + LONG_BITS - 1) / LONG_BITS)

static inline bool testBit(long bit, const long *array)
{
    return (array[bit / LONG_BITS] >> bit % LONG_BITS) & 1;
}

QEvdevTouchScreenDevice::QEvdevTouchScreenDevice(const QString &params, QEvdevTouchScreenEventDispatcher *eventDispatcher, int id)
    : m_screenId(id), m_eventDispatcher(eventDispatcher), m_d(0), m_notify(0), m_fd(-1), m_xOffset(-1280 * id)
#ifdef USE_MTDEV
      , m_mtdev(0)
#endif
{
    setObjectName(QLatin1String("Evdev Touch Handler Device subclass"));
    qDebug("evdevtouch (instance): Using device %s", qPrintable(params));

    if(params.isEmpty())
        return;

    m_fd = QT_OPEN(params.toLocal8Bit().constData(), O_RDONLY | O_NDELAY, 0);

    if (m_fd >= 0) {
        m_notify = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
        connect(m_notify, SIGNAL(activated(int)), this, SLOT(readData()), Qt::DirectConnection);
    } else {
        qErrnoWarning(errno, "Cannot open input device %s", qPrintable(params));
        return;
    }

#ifdef USE_MTDEV
    m_mtdev = static_cast<mtdev *>(calloc(1, sizeof(mtdev)));
    int mtdeverr = mtdev_open(m_mtdev, m_fd);
    if (mtdeverr) {
        qWarning("mtdev_open failed: %d", mtdeverr);
        QT_CLOSE(m_fd);
        return;
    }
#endif

    m_d = new QEvdevTouchScreenData(this, m_eventDispatcher);

    input_absinfo absInfo;
    memset(&absInfo, 0, sizeof(input_absinfo));
    if (ioctl(m_fd, EVIOCGABS(ABS_MT_POSITION_X), &absInfo) >= 0) {
        qDebug("min X: %d max X: %d", absInfo.minimum, absInfo.maximum);
        m_d->hw_range_x_min = absInfo.minimum;
        m_d->hw_range_x_max = absInfo.maximum;
    }
    if (ioctl(m_fd, EVIOCGABS(ABS_MT_POSITION_Y), &absInfo) >= 0) {
        qDebug("min Y: %d max Y: %d", absInfo.minimum, absInfo.maximum);
        m_d->hw_range_y_min = absInfo.minimum;
        m_d->hw_range_y_max = absInfo.maximum;
    }
    if (ioctl(m_fd, EVIOCGABS(ABS_PRESSURE), &absInfo) >= 0) {
        qDebug("min pressure: %d max pressure: %d", absInfo.minimum, absInfo.maximum);
        if (absInfo.maximum > absInfo.minimum) {
            m_d->hw_pressure_min = absInfo.minimum;
            m_d->hw_pressure_max = absInfo.maximum;
        }
    }
    char name[1024];
    if (ioctl(m_fd, EVIOCGNAME(sizeof(name) - 1), name) >= 0) {
        m_d->hw_name = QString::fromLocal8Bit(name);
        qDebug("device name: %s", name);
    }

    bool grabSuccess = !ioctl(m_fd, EVIOCGRAB, (void *) 1);
    if (grabSuccess)
        ioctl(m_fd, EVIOCGRAB, (void *) 0);
    else
        qWarning("ERROR: The device is grabbed by another process. No events will be read.");

#ifdef USE_MTDEV
    const char *mtdevStr = "(mtdev)";
    m_d->m_typeB = true;
#else
    const char *mtdevStr = "";
    m_d->m_typeB = false;
    long absbits[NUM_LONGS(ABS_CNT)];
    if (ioctl(m_fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits) >= 0)
        m_d->m_typeB = testBit(ABS_MT_SLOT, absbits);
#endif
    qDebug("Protocol type %c %s", m_d->m_typeB ? 'B' : 'A', mtdevStr);

    m_d->registerDevice();
}

QEvdevTouchScreenDevice::~QEvdevTouchScreenDevice()
{
#ifdef USE_MTDEV
    if (m_mtdev) {
        mtdev_close(m_mtdev);
        free(m_mtdev);
    }
#endif

    if (m_fd >= 0)
        QT_CLOSE(m_fd);

    delete m_d;
}

void QEvdevTouchScreenDevice::readData()
{
    ::input_event buffer[32];
    int n = 0;
    for(;;)
    {
#ifdef USE_MTDEV
        int result = mtdev_get(m_mtdev, m_fd, buffer, sizeof(buffer) / sizeof(::input_event));
        if (result > 0)
            result *= sizeof(::input_event);
#else
        int result = QT_READ(m_fd, reinterpret_cast<char*>(buffer) + n, sizeof(buffer) - n);
#endif
        if (!result)
        {
            qWarning("Got EOF from input device");
            return;
        }
        else if (result < 0)
        {
            if (errno != EINTR && errno != EAGAIN)
            {
                qWarning("Could not read from input device: %s", strerror(errno));
                if (errno == ENODEV) // device got disconnected -> stop reading
                {
                    delete m_notify;
                    m_notify = 0;
                    QT_CLOSE(m_fd);
                    m_fd = -1;
                }
                return;
            }
        }
        else
        {
            n += result;
            if (n % sizeof(::input_event) == 0)
                break;
        }
    }

    n /= sizeof(::input_event);

    for (int i = 0; i < n; ++i)
        m_d->processInputEvent(&buffer[i]);
}

// class QEvdevTouchScreenHandler --------------------------------------------------------------------------------------------------

QEvdevTouchScreenHandler::QEvdevTouchScreenHandler(const QString &specification, QObject *parent)
    : QObject(parent)
{
    setObjectName(QLatin1String("Evdev Touch Handler"));

    // only the first device argument is used for now
    QString spec = QString::fromLocal8Bit(qgetenv("QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS"));

    if (spec.isEmpty())
        spec = specification;

    QStringList args = spec.split(QLatin1Char(':'));
    for(int i = 0; i < args.count(); i++)
    {
        if(args.at(i).startsWith(QLatin1String("/dev/")))
        {
            m_deviceList.append(new QEvdevTouchScreenDevice(args.at(i), &m_eventDispatcher, m_deviceList.size()));
        }
    }
}

QEvdevTouchScreenHandler::~QEvdevTouchScreenHandler()
{
    for(int i = 0; i < m_deviceList.size(); i++)
    {
        delete m_deviceList[i];
    }
}

// class QEvdevTouchScreenHandlerThread --------------------------------------------------------------------------------------------

QEvdevTouchScreenHandlerThread::QEvdevTouchScreenHandlerThread(const QString &spec, QObject *parent)
    : QThread(parent), m_spec(spec), m_handler(0)
{
    start();
}

QEvdevTouchScreenHandlerThread::~QEvdevTouchScreenHandlerThread()
{
    quit();
    wait();
}

void QEvdevTouchScreenHandlerThread::run()
{
    m_handler = new QEvdevTouchScreenHandler(m_spec);
    exec();
    delete m_handler;
    m_handler = 0;
}

// ---------------------------------------------------------------------------------------------------------------------------------





#if 0 // old codes for reference
/* android (and perhaps some other linux-derived stuff) don't define everything
 * in linux/input.h, so we'll need to do that ourselves.
 */
#ifndef ABS_MT_TOUCH_MAJOR
#define ABS_MT_TOUCH_MAJOR      0x30    /* Major axis of touching ellipse */
#endif
#ifndef ABS_MT_POSITION_X
#define ABS_MT_POSITION_X 0x35    /* Center X ellipse position */
#endif
#ifndef ABS_MT_POSITION_Y
#define ABS_MT_POSITION_Y       0x36    /* Center Y ellipse position */
#endif
#ifndef ABS_MT_SLOT
#define ABS_MT_SLOT 0x2f
#endif
#ifndef ABS_CNT
#define ABS_CNT                 (ABS_MAX+1)
#endif
#ifndef ABS_MT_TRACKING_ID
#define ABS_MT_TRACKING_ID      0x39    /* Unique ID of initiated contact */
#endif
#ifndef SYN_MT_REPORT
#define SYN_MT_REPORT           2
#endif

class QEvdevTouchScreenData
{
public:
    QEvdevTouchScreenData(QEvdevTouchScreenDevice *q_ptr);

    void processInputEvent(input_event *data);
    void assignIds();

    QEvdevTouchScreenDevice *q;
    int m_lastEventType;
    QList<QWindowSystemInterface::TouchPoint> m_touchPoints;

    struct Contact {
        int trackingId;
        int x;
        int y;
        int maj;
        int pressure;
        Qt::TouchPointState state;
        QTouchEvent::TouchPoint::InfoFlags flags;
        Contact() : trackingId(-1),
            x(0), y(0), maj(-1), pressure(0),
            state(Qt::TouchPointPressed), flags(0) { }
    };
    QHash<int, Contact> m_contacts; // The key is a tracking id for type A, slot number for type B.
    QHash<int, Contact> m_lastContacts;
    Contact m_currentData;
    int m_currentSlot;

    int findClosestContact(const QHash<int, Contact> &contacts, int x, int y, int *dist);
    void reportPoints();
    void registerDevice();

    int hw_range_x_min;
    int hw_range_x_max;
    int hw_range_y_min;
    int hw_range_y_max;
    int hw_pressure_min;
    int hw_pressure_max;
    QString hw_name;
    bool m_forceToActiveWindow;
    QTouchDevice *m_device;
    bool m_typeB;
};

QEvdevTouchScreenData::QEvdevTouchScreenData(QEvdevTouchScreenDevice *q_ptr)
    : q(q_ptr),
      m_lastEventType(-1),
      m_currentSlot(0),
      hw_range_x_min(0), hw_range_x_max(0),
      hw_range_y_min(0), hw_range_y_max(0),
      hw_pressure_min(0), hw_pressure_max(0),
      m_device(0), m_typeB(false)
{
//    m_forceToActiveWindow = args.contains(QLatin1String("force_window"));
    m_forceToActiveWindow = false;
    qDebug("QEvdevTouchScreenData() id: %d", q->m_id);
}

void QEvdevTouchScreenData::registerDevice()
{
    m_device = new QTouchDevice;
    m_device->setName(hw_name);
    m_device->setType(QTouchDevice::TouchScreen);
    m_device->setCapabilities(QTouchDevice::Position | QTouchDevice::Area);
    if (hw_pressure_max > hw_pressure_min)
        m_device->setCapabilities(m_device->capabilities() | QTouchDevice::Pressure);

    QWindowSystemInterface::registerTouchDevice(m_device);
}

#define LONG_BITS (sizeof(long) << 3)
#define NUM_LONGS(bits) (((bits) + LONG_BITS - 1) / LONG_BITS)

static inline bool testBit(long bit, const long *array)
{
    return (array[bit / LONG_BITS] >> bit % LONG_BITS) & 1;
}

QEvdevTouchScreenDevice::QEvdevTouchScreenDevice(const QString &dev, int id)
    : m_notify(0), m_fd(-1), m_d(0), m_id(id), m_xOffset(-1280 * id)
#ifdef USE_MTDEV
      , m_mtdev(0)
#endif
{
    setObjectName(QLatin1String("Evdev Touch Handler Device subclass"));
    qDebug("evdevtouch (instance): Using device %s", qPrintable(dev));

    if(dev.isEmpty())
        return;

    m_fd = QT_OPEN(dev.toLocal8Bit().constData(), O_RDONLY | O_NDELAY, 0);

    if (m_fd >= 0) {
        m_notify = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
        connect(m_notify, SIGNAL(activated(int)), this, SLOT(readData()));
    } else {
        qErrnoWarning(errno, "Cannot open input device %s", qPrintable(dev));
        return;
    }

#ifdef USE_MTDEV
    m_mtdev = static_cast<mtdev *>(calloc(1, sizeof(mtdev)));
    int mtdeverr = mtdev_open(m_mtdev, m_fd);
    if (mtdeverr) {
        qWarning("mtdev_open failed: %d", mtdeverr);
        QT_CLOSE(m_fd);
        return;
    }
#endif

    m_d = new QEvdevTouchScreenData(this);

    input_absinfo absInfo;
    memset(&absInfo, 0, sizeof(input_absinfo));
    if (ioctl(m_fd, EVIOCGABS(ABS_MT_POSITION_X), &absInfo) >= 0) {
        qDebug("min X: %d max X: %d", absInfo.minimum, absInfo.maximum);
        m_d->hw_range_x_min = 0 + m_xOffset;
        m_d->hw_range_x_max = 2560 + m_xOffset;
    }
    if (ioctl(m_fd, EVIOCGABS(ABS_MT_POSITION_Y), &absInfo) >= 0) {
        qDebug("min Y: %d max Y: %d", absInfo.minimum, absInfo.maximum);
        m_d->hw_range_y_min = absInfo.minimum;
        m_d->hw_range_y_max = absInfo.maximum;
    }
    if (ioctl(m_fd, EVIOCGABS(ABS_PRESSURE), &absInfo) >= 0) {
        qDebug("min pressure: %d max pressure: %d", absInfo.minimum, absInfo.maximum);
        if (absInfo.maximum > absInfo.minimum) {
            m_d->hw_pressure_min = absInfo.minimum;
            m_d->hw_pressure_max = absInfo.maximum;
        }
    }
    char name[1024];
    if (ioctl(m_fd, EVIOCGNAME(sizeof(name) - 1), name) >= 0) {
        m_d->hw_name = QString::fromLocal8Bit(name);
        qDebug("device name: %s", name);
    }

    bool grabSuccess = !ioctl(m_fd, EVIOCGRAB, (void *) 1);
    if (grabSuccess)
        ioctl(m_fd, EVIOCGRAB, (void *) 0);
    else
        qWarning("ERROR: The device is grabbed by another process. No events will be read.");

#ifdef USE_MTDEV
    const char *mtdevStr = "(mtdev)";
    m_d->m_typeB = true;
#else
    const char *mtdevStr = "";
    m_d->m_typeB = false;
    long absbits[NUM_LONGS(ABS_CNT)];
    if (ioctl(m_fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits) >= 0)
        m_d->m_typeB = testBit(ABS_MT_SLOT, absbits);
#endif
    qDebug("Protocol type %c %s", m_d->m_typeB ? 'B' : 'A', mtdevStr);

    m_d->registerDevice();
}

QEvdevTouchScreenDevice::~QEvdevTouchScreenDevice()
{
#ifdef USE_MTDEV
    if (m_mtdev) {
        mtdev_close(m_mtdev);
        free(m_mtdev);
    }
#endif

    if (m_fd >= 0)
        QT_CLOSE(m_fd);

    delete m_d;
}

QEvdevTouchScreenHandler::QEvdevTouchScreenHandler(const QString &spec, QObject *parent)
    : QObject(parent)
{
    setObjectName(QLatin1String("Evdev Touch Handler"));

    // only the first device argument is used for now
    QStringList args = spec.split(QLatin1Char(':'));
    for(int i = 0; i < args.count(); i++)
    {
        if(args.at(i).startsWith(QLatin1String("/dev/")))
        {
            m_deviceList.append(new QEvdevTouchScreenDevice(args.at(i), i));
        }
    }
}

QEvdevTouchScreenHandler::~QEvdevTouchScreenHandler()
{
    for(int i = 0; i < m_deviceList.size(); i++)
    {
        delete m_deviceList[i];
    }
}

void QEvdevTouchScreenDevice::readData()
{
    ::input_event buffer[32];
    int n = 0;
    for (; ;) {
#ifdef USE_MTDEV
        int result = mtdev_get(m_mtdev, m_fd, buffer, sizeof(buffer) / sizeof(::input_event));
        if (result > 0)
            result *= sizeof(::input_event);
#else
        int result = QT_READ(m_fd, reinterpret_cast<char*>(buffer) + n, sizeof(buffer) - n);
#endif
        if (!result) {
            qWarning("Got EOF from input device");
            return;
        } else if (result < 0) {
            if (errno != EINTR && errno != EAGAIN) {
                qWarning("Could not read from input device: %s", strerror(errno));
                if (errno == ENODEV) { // device got disconnected -> stop reading
                    delete m_notify;
                    m_notify = 0;
                    QT_CLOSE(m_fd);
                    m_fd = -1;
                }
                return;
            }
        } else {
            n += result;
            if (n % sizeof(::input_event) == 0)
                break;
        }
    }

    n /= sizeof(::input_event);

    for (int i = 0; i < n; ++i)
        m_d->processInputEvent(&buffer[i]);
}

void QEvdevTouchScreenData::processInputEvent(input_event *data)
{
    if (data->type == EV_ABS) {

        if (data->code == ABS_MT_POSITION_X) {
            m_currentData.x = qBound(hw_range_x_min, data->value, hw_range_x_max);
            if (m_typeB)
                m_contacts[m_currentSlot].x = m_currentData.x;
        } else if (data->code == ABS_MT_POSITION_Y) {
            m_currentData.y = qBound(hw_range_y_min, data->value, hw_range_y_max);
            if (m_typeB)
                m_contacts[m_currentSlot].y = m_currentData.y;
        } else if (data->code == ABS_MT_TRACKING_ID) {
            m_currentData.trackingId = data->value + q->m_id * 5;
            if (m_typeB) {
                if (m_currentData.trackingId == -1)
                    m_contacts[m_currentSlot].state = Qt::TouchPointReleased;
                else
                    m_contacts[m_currentSlot].trackingId = m_currentData.trackingId;
            }
        } else if (data->code == ABS_MT_TOUCH_MAJOR) {
            m_currentData.maj = data->value;
            if (data->value == 0)
                m_currentData.state = Qt::TouchPointReleased;
            if (m_typeB)
                m_contacts[m_currentSlot].maj = m_currentData.maj;
        } else if (data->code == ABS_PRESSURE) {
            m_currentData.pressure = qBound(hw_pressure_min, data->value, hw_pressure_max);
            if (m_typeB)
                m_contacts[m_currentSlot].pressure = m_currentData.pressure;
        } else if (data->code == ABS_MT_SLOT) {
            m_currentSlot = data->value;
        }

    } else if (data->type == EV_KEY && !m_typeB) {
        if (data->code == BTN_TOUCH && data->value == 0)
          {
            m_contacts[m_currentSlot].state = Qt::TouchPointReleased;
          }
    } else if (data->type == EV_SYN && data->code == SYN_MT_REPORT && m_lastEventType != EV_SYN) {

        // If there is no tracking id, one will be generated later.
        // Until that use a temporary key.
        int key = m_currentData.trackingId;
        if (key == -1)
            key = m_contacts.count();

        m_contacts.insert(key, m_currentData);
        m_currentData = Contact();

    } else if (data->type == EV_SYN && data->code == SYN_REPORT) {

        // Ensure valid IDs even when the driver does not report ABS_MT_TRACKING_ID.
        if (!m_contacts.isEmpty() && m_contacts.constBegin().value().trackingId == -1)
            assignIds();

        m_touchPoints.clear();
        Qt::TouchPointStates combinedStates;
        QMutableHashIterator<int, Contact> it(m_contacts);
        while (it.hasNext()) {
            it.next();
            QWindowSystemInterface::TouchPoint tp;
            Contact &contact(it.value());
            tp.id = contact.trackingId;
            tp.flags = contact.flags;

            int key = m_typeB ? it.key() : contact.trackingId;
            if (m_lastContacts.contains(key)) {
                const Contact &prev(m_lastContacts.value(key));
                if (contact.state == Qt::TouchPointReleased) {
                    // Copy over the previous values for released points, just in case.
                    contact.x = prev.x;
                    contact.y = prev.y;
                    contact.maj = prev.maj;
                } else {
                    contact.state = (prev.x == contact.x && prev.y == contact.y)
                            ? Qt::TouchPointStationary : Qt::TouchPointMoved;
                }
            }

            // Avoid reporting a contact in released state more than once.
            if (contact.state == Qt::TouchPointReleased
                    && !m_lastContacts.contains(key)) {
                it.remove();
                continue;
            }

            tp.state = contact.state;
            combinedStates |= tp.state;

            // Store the HW coordinates for now, will be updated later.
            tp.area = QRectF(0, 0, contact.maj, contact.maj);
            tp.area.moveCenter(QPoint(contact.x, contact.y));
            tp.pressure = contact.pressure;

            // Get a normalized position in range 0..1.
            tp.normalPosition = QPointF((contact.x - hw_range_x_min) / qreal(hw_range_x_max - hw_range_x_min),
                                        (contact.y - hw_range_y_min) / qreal(hw_range_y_max - hw_range_y_min));

            m_touchPoints.append(tp);

            if (contact.state == Qt::TouchPointReleased)
                it.remove();
        }

        m_lastContacts = m_contacts;
        if (!m_typeB)
            m_contacts.clear();

        if (!m_touchPoints.isEmpty() && combinedStates != Qt::TouchPointStationary)
            reportPoints();
    }

    m_lastEventType = data->type;
}

int QEvdevTouchScreenData::findClosestContact(const QHash<int, Contact> &contacts, int x, int y, int *dist)
{
    int minDist = -1, id = -1;
    for (QHash<int, Contact>::const_iterator it = contacts.constBegin(), ite = contacts.constEnd();
         it != ite; ++it) {
        const Contact &contact(it.value());
        int dx = x - contact.x;
        int dy = y - contact.y;
        int dist = dx * dx + dy * dy;
        if (minDist == -1 || dist < minDist) {
            minDist = dist;
            id = contact.trackingId;
        }
    }
    if (dist)
        *dist = minDist;
    return id;
}

void QEvdevTouchScreenData::assignIds()
{
    QHash<int, Contact> candidates = m_lastContacts, pending = m_contacts, newContacts;
    int maxId = -1;
    QHash<int, Contact>::iterator it, ite, bestMatch;
    while (!pending.isEmpty() && !candidates.isEmpty()) {
        int bestDist = -1, bestId = 0;
        for (it = pending.begin(), ite = pending.end(); it != ite; ++it) {
            int dist;
            int id = findClosestContact(candidates, it->x, it->y, &dist);
            if (id >= 0 && (bestDist == -1 || dist < bestDist)) {
                bestDist = dist;
                bestId = id;
                bestMatch = it;
            }
        }
        if (bestDist >= 0) {
            bestMatch->trackingId = bestId;
            newContacts.insert(bestId, *bestMatch);
            candidates.remove(bestId);
            pending.erase(bestMatch);
            if (bestId > maxId)
                maxId = bestId;
        }
    }
    if (candidates.isEmpty()) {
        for (it = pending.begin(), ite = pending.end(); it != ite; ++it) {
            it->trackingId = ++maxId;
            newContacts.insert(it->trackingId, *it);
        }
    }
    m_contacts = newContacts;
}

void QEvdevTouchScreenData::reportPoints()
{
    QRect winRect;
    if (m_forceToActiveWindow) {
        QWindow *win = QGuiApplication::focusWindow();
        if (!win)
            return;
        winRect = win->geometry();
    } else {
        winRect = QGuiApplication::primaryScreen()->geometry();
    }

    const int hw_w = hw_range_x_max - hw_range_x_min;
    const int hw_h = hw_range_y_max - hw_range_y_min;

    // Map the coordinates based on the normalized position. QPA expects 'area'
    // to be in screen coordinates.
    const int pointCount = m_touchPoints.count();
    for (int i = 0; i < pointCount; ++i) {
        QWindowSystemInterface::TouchPoint &tp(m_touchPoints[i]);

        // Generate a screen position that is always inside the active window
        // or the primary screen.
        const qreal wx = winRect.left() + tp.normalPosition.x() * winRect.width();
        const qreal wy = winRect.top() + tp.normalPosition.y() * winRect.height();
        const qreal sizeRatio = (winRect.width() + winRect.height()) / qreal(hw_w + hw_h);
        if (tp.area.width() == -1) // touch major was not provided
            tp.area = QRectF(0, 0, 8, 8);
        else
            tp.area = QRectF(0, 0, tp.area.width() * sizeRatio, tp.area.height() * sizeRatio);
        tp.area.moveCenter(QPointF(wx, wy));

        // Calculate normalized pressure.
        if (!hw_pressure_min && !hw_pressure_max)
            tp.pressure = tp.state == Qt::TouchPointReleased ? 0 : 1;
        else
            tp.pressure = (tp.pressure - hw_pressure_min) / qreal(hw_pressure_max - hw_pressure_min);
    }

    QWindowSystemInterface::handleTouchEvent(0, m_device, m_touchPoints);
}


QEvdevTouchScreenHandlerThread::QEvdevTouchScreenHandlerThread(const QString &spec, QObject *parent)
    : QThread(parent), m_spec(spec), m_handler(0)
{
    start();
}

QEvdevTouchScreenHandlerThread::~QEvdevTouchScreenHandlerThread()
{
    quit();
    wait();
}

void QEvdevTouchScreenHandlerThread::run()
{
    m_handler = new QEvdevTouchScreenHandler(m_spec);
    exec();
    delete m_handler;
    m_handler = 0;
}
#endif

QT_END_NAMESPACE
