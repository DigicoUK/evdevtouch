# evdevtouch
Additions to Qt's qevdevtouch to aggregate multiple adjacent touch panels

To use:
* Clone and compile
* Replace your existing evdevtouch plugin or place in dir with your app's binary
* Run your app with this new EvdevTouch plugin, providing a list of touch inputs separated by ':'
* e.g ```-plugin EvdevTouch:/dev/input/event0:/dev/input/event1```
