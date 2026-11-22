# pxs_core (Dart)

Dart bindings to [pxs-core](../README.md) through its C interface (`libpxs_c`): the OSC codec, the address space (parameters, actions,
signals, OSC applied to it, what an OSCQuery client is told), presets, DMX frames, MIDI messages, the WebSocket handshake. For the Flutter
applications on iOS and Android, and for any Dart program.

The sockets are Dart's (`dart:io`): this package decides what the bytes mean and what the answer is, not how they travel.

```dart
Pxs.open(path: 'libpxs_c.dylib');                       // iOS: Pxs.open(): the static library is in the process

final tree = PxsTree();
tree.addParameter('/layer/1/opacity', ParameterType.float,
    get: () => opacity, set: (v) => opacity = (v as num).toDouble());
tree.addAction('/show/go', 'i', (args) { goTo(args.single as int); return true; });
tree.addSignal('/detect/peak', 'f');

tree.onOutput = (packet) => udp.send(packet, target, port);   // the feedback and the signals, as OSC
udp.listen((datagram) => tree.applyPacket(datagram));         // OSC in
final answer = tree.oscQueryAnswer('GET', '/layer/1/opacity?VALUE');   // answer.status, answer.body (JSON)

tree.dispose();                                              // frees the native tree and the callbacks
```

Values are `null`, `bool`, `int`, `double`, `String`, `Uint8List` (a blob) and `Vec` (2 to 4 floats). The type tag decides what goes on the wire.

## Building and testing

```sh
cmake -S .. -B ../build-shared -DPXS_CORE_BUILD_SHARED_C=ON && cmake --build ../build-shared --target pxs_c
dart pub get && dart analyze && dart test          # PXS_C_LIBRARY=/path/to/libpxs_c.* to test another build
```

`lib/src/bindings.g.dart` is generated from `../include/pxs/c/pxs.h` (`dart run ffigen --config ffigen.yaml`, needs LLVM's libclang) and
committed; CI regenerates it and fails if it differs, so it cannot fall behind the header.

## Rules (the C interface's)

Not thread safe: one isolate. Callbacks (a parameter's getter and setter, an action, the output) run synchronously, on the isolate's
thread, inside the call that causes them. A [PxsException] carries the C status. The C interface grows by adding; this package checks
its version when the library is opened.
