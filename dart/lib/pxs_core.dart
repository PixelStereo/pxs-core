// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-PixelStereo-Commercial
/// pxs-core for Dart: OSC, the address space and OSCQuery, DMX, MIDI, through the C interface (`libpxs_c`).
///
/// ```dart
/// Pxs.open(path: 'libpxs_c.dylib');
/// final tree = PxsTree();
/// tree.addParameter('/layer/1/opacity', ParameterType.float, get: () => opacity, set: (v) => opacity = v as double);
/// tree.onOutput = (packet) => socket.send(packet, target, port);   // the feedback, as OSC
/// tree.applyPacket(datagram);                                       // what arrived on the UDP socket
/// final answer = tree.oscQueryAnswer('GET', '/layer/1/opacity?VALUE');
/// ```
library;

export 'src/dmx_midi_websocket.dart';
export 'src/native.dart' show Pxs, PxsException, Vec;
export 'src/osc.dart';
export 'src/remote.dart';
export 'src/tree.dart';
