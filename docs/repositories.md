# Which repository do I use?

There are three, one per kind of application. They share one core, so a rule (what a leaf is, what a parameter must say, how a reset works) is written once.

| Repository | For | What it has | Language |
|---|---|---|---|
| **pxs-core** | the rules, for everyone | the address space (parameters, actions, signals, the tree), the OSC codec, the OSCQuery JSON (server **and client side**: `RemoteTree`), presets, MIDI and DMX frames, the change feed. No sockets, no UI, no Qt. A C interface (`pxs.h`) for other languages, and the Dart bindings of it (`dart/`, generated from the C header). | C++17, C, Dart bindings |
| **pxs-qt** | an application written with Qt (NyxMap, Kadran desktop, MpOsc…) | the Qt face of the core (`Parameter`, `ChangeFeed`, `pxs::convert`: QString, QVariant, QJson at the edge), the sockets (OSC over UDP, OSCQuery over HTTP and WebSocket, mDNS), the widgets | C++ / Qt |
| **pxs-flutter** | a Flutter application, on iOS and Android (OSCQontrol, OMTCam) | the native library built into the app, the Dart API over the C interface, and the sockets in Dart (`dart:io`: UDP, HTTP, WebSocket) and Bonjour | Dart / Flutter |

There is no separate "pxs-dart" or "pxs-mobile": the Dart bindings are the `dart/` folder of pxs-core (pure Dart, no Flutter, usable from a command line tool),
and what is specific to phones (packaging the native library, Bonjour, the sockets) is pxs-flutter. An application uses **one** of pxs-qt or pxs-flutter, never both.

The rule for what goes where: if it decides what the bytes or the nodes *mean*, it is in pxs-core, once. If it only moves them (a socket) or shows them (a widget),
it is in the layer of the application's toolkit.
