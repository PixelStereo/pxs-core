// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-PixelStereo-Commercial
import 'dart:convert';
import 'dart:ffi' as ffi;
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

import 'bindings.g.dart';
import 'native.dart';

/// A DMX frame of Art-Net or sACN (E1.31).
class DmxFrame {
  DmxFrame({
    this.sacn = false,
    this.universe = 0,
    this.priority = 100,
    this.sequence = 0,
    this.length = 512,
    this.terminated = false,
    Uint8List? cid,
    this.name = '',
    Uint8List? values,
  })  : cid = cid ?? Uint8List(16),
        values = values ?? Uint8List(512);

  /// true: sACN, false: Art-Net.
  bool sacn;

  /// Art-Net: 0..32767; sACN: 1..63999.
  int universe;

  /// sACN: 0..200.
  int priority;
  int sequence;

  /// Channels in the packet, 1..512.
  int length;
  bool terminated;

  /// sACN: the sender's 16-byte id.
  Uint8List cid;

  /// sACN: the source's name.
  String name;

  /// The 512 levels.
  Uint8List values;
}

class Dmx {
  Dmx._();

  /// The packet of a frame. Throws [PxsException] when the universe or the priority is out of range.
  static Uint8List encode(DmxFrame frame) {
    final b = Pxs.bindings;
    return using((arena) {
      final f = arena<pxs_dmx_frame>();
      final r = f.ref;
      r.sacn = frame.sacn ? 1 : 0;
      r.universe = frame.universe;
      r.priority = frame.priority;
      r.sequence = frame.sequence;
      r.length = frame.length;
      r.terminated = frame.terminated ? 1 : 0;
      for (var i = 0; i < 16; i++) {
        r.cid[i] = i < frame.cid.length ? frame.cid[i] : 0;
      }
      final name = utf8.encode(frame.name);
      for (var i = 0; i < 64; i++) {
        r.name[i] = i < name.length && i < 63 ? name[i] : 0;
      }
      for (var i = 0; i < 512; i++) {
        r.values[i] = i < frame.values.length ? frame.values[i] : 0;
      }
      final out = arena<ffi.Pointer<ffi.Uint8>>();
      final size = arena<ffi.Size>();
      final status = b.pxs_dmx_encode(f, out, size);
      if (status != 0) throw PxsException(status, 'cannot encode the DMX frame');
      return takeBytes(out.value, size.value);
    });
  }

  /// What a packet carries; [sacn] says which protocol to expect. Throws [PxsException] for anything that is not DMX of that protocol.
  static DmxFrame decode(Uint8List packet, {required bool sacn}) {
    final b = Pxs.bindings;
    return using((arena) {
      final data = arena<ffi.Uint8>(packet.isEmpty ? 1 : packet.length);
      data.asTypedList(packet.length).setAll(0, packet);
      final f = arena<pxs_dmx_frame>();
      final status = b.pxs_dmx_decode(data, packet.length, sacn ? 1 : 0, f);
      if (status != 0) throw PxsException(status, 'not a DMX packet');
      final r = f.ref;
      final nameBytes = <int>[];
      for (var i = 0; i < 64 && r.name[i] != 0; i++) {
        nameBytes.add(r.name[i] & 0xff);
      }
      return DmxFrame(
        sacn: r.sacn != 0,
        universe: r.universe,
        priority: r.priority,
        sequence: r.sequence,
        length: r.length,
        terminated: r.terminated != 0,
        cid: Uint8List.fromList([for (var i = 0; i < 16; i++) r.cid[i]]),
        name: utf8.decode(nameBytes, allowMalformed: true),
        values: Uint8List.fromList([for (var i = 0; i < 512; i++) r.values[i]]),
      );
    });
  }
}

enum MidiKind { controlChange, note, pitchBend, programChange }

/// A MIDI channel message as a control sees it: channel 1..16, the number (controller, note, program), the value 0..1.
class MidiMessage {
  MidiMessage(this.kind, this.channel, this.number, this.value);
  final MidiKind kind;
  final int channel;
  final int number;
  final double value;

  @override
  String toString() => 'MidiMessage($kind, ch $channel, #$number, $value)';
}

class Midi {
  Midi._();

  /// The message of some bytes, or null for anything that is not a channel message pxs-core reads (clock, SysEx, aftertouch…).
  static MidiMessage? parse(Uint8List bytes) {
    final b = Pxs.bindings;
    return using((arena) {
      final data = arena<ffi.Uint8>(bytes.isEmpty ? 1 : bytes.length);
      data.asTypedList(bytes.length).setAll(0, bytes);
      final m = arena<pxs_midi_message>();
      if (b.pxs_midi_parse(data, bytes.length, m) != 0) return null;
      return MidiMessage(MidiKind.values[m.ref.kind], m.ref.channel, m.ref.number, m.ref.value);
    });
  }

  static Uint8List _build(int kind, int channel, int number, double value) {
    final b = Pxs.bindings;
    return using((arena) {
      final out = arena<ffi.Pointer<ffi.Uint8>>();
      final size = arena<ffi.Size>();
      final status = b.pxs_midi_build(kind, channel, number, value, out, size);
      if (status != 0) throw PxsException(status, 'cannot build the MIDI message');
      return takeBytes(out.value, size.value);
    });
  }

  static Uint8List controlChange(int channel, int controller, double value) => _build(0, channel, controller, value);
  static Uint8List noteOn(int channel, int note, double velocity) => _build(1, channel, note, velocity);
  static Uint8List noteOff(int channel, int note) => _build(2, channel, note, 0);
  static Uint8List pitchBend(int channel, double value) => _build(3, channel, 0, value);
  static Uint8List programChange(int channel, int program) => _build(4, channel, program, 0);
}

class WebSocket {
  WebSocket._();

  /// The Sec-WebSocket-Accept of a handshake, from the client's Sec-WebSocket-Key.
  static String acceptKey(String key) {
    final b = Pxs.bindings;
    return using((arena) {
      final p = b.pxs_websocket_accept_key(key.toNativeUtf8(allocator: arena).cast());
      if (p == ffi.nullptr) throw PxsException(-5, 'cannot compute the accept key');
      final text = p.cast<Utf8>().toDartString();
      b.pxs_free(p.cast());
      return text;
    });
  }
}
