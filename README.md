### Modified QT evedevtouch

This is a modification of the QT evdevtouch plugin for touch hanling in linux. The original version of the plugin doesn't support scenario where on large screen spreads over multiple screens without windowing manager but instead uses eglfs only for it.

We have added an option for this QPA plugin to get the left x coordinate in the window from where it starts to offset the plugin to a correct position thus allowing the plugin to report the touches correctly inside one large window.

The plugin can be used by using the following input argument and also the default arguments are supported
```
-plugin EvdevTouch:/dev/foo/bar1(x_offset=0):/dev/foo/bar2(x_offset=1280)
```

As multiple devices are given the x offset needs to be tied to the device by using () and putting the offset there as shown above.

This plugin also ensures that all of the created plugins use unique id range so that each plugin has range of 10000 touch id's witch they rotate when the last id is consumed. This is required because of bug in qt that causes touches to be incorrectly removed.
To fix this bug you need to have this plugin as well as apply this patch to qt to take advantage of the unique touch id's of this plugin as well as fix the bug caused by Qt trying to generate unique touch id's but failing when multiple touch screens are involved.

#### Patch:
```
diff --git a/src/gui/kernel/qwindowsysteminterface.cpp b/src/gui/kernel/qwindowsysteminterface.cpp
index 13f45d236e..18e34138cd 100644
--- a/src/gui/kernel/qwindowsysteminterface.cpp
+++ b/src/gui/kernel/qwindowsysteminterface.cpp
@@ -573,7 +573,7 @@ QList<QTouchEvent::TouchPoint>
     QList<QWindowSystemInterface::TouchPoint>::const_iterator point = points.constBegin();
     QList<QWindowSystemInterface::TouchPoint>::const_iterator end = points.constEnd();
     while (point != end) {
-        p.setId(acquireCombinedPointId(deviceId, point->id));
+        p.setId(point->id);
         if (point->uniqueId >= 0)
             p.setUniqueId(point->uniqueId);
         p.setPressure(point->pressure);
```
