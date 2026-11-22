// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-PixelStereo-Commercial
import 'dart:ffi' as ffi;
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

import 'bindings.g.dart';
import 'native.dart';

/// An OSC message: its address, the type tags (without the leading ','), and one value per tag.
class OscMessage {
  OscMessage(this.address, this.types, this.args);
  final String address;
  final String types;
  final List<Object?> args;

  @override
  String toString() => 'OscMessage($address, $types, $args)';
}

/// The OSC 1.0 codec of pxs-core (messages, bundles flattened), bounded against hostile packets.
class Osc {
  Osc._();

  /// The messages of a packet (a message, or a bundle with its nested bundles flattened). Throws [PxsException] for a packet that is
  /// malformed, empty or beyond a limit.
  static List<OscMessage> decode(Uint8List packet) {
    final b = Pxs.bindings;
    return using((arena) {
      final data = arena<ffi.Uint8>(packet.isEmpty ? 1 : packet.length);
      data.asTypedList(packet.length).setAll(0, packet);
      final status = arena<ffi.Int>();
      final handle = b.pxs_osc_decode(data, packet.length, status);
      if (handle == ffi.nullptr) throw PxsException(status.value, 'cannot decode the OSC packet');
      try {
        final out = <OscMessage>[];
        final count = b.pxs_osc_packet_count(handle);
        final value = arena<pxs_value>();
        for (var i = 0; i < count; i++) {
          final args = <Object?>[];
          final argc = b.pxs_osc_message_argc(handle, i);
          for (var k = 0; k < argc; k++) {
            b.pxs_osc_message_arg(handle, i, k, value);
            args.add(readValue(value));
          }
          out.add(OscMessage(
            b.pxs_osc_message_address(handle, i).cast<Utf8>().toDartString(),
            b.pxs_osc_message_types(handle, i).cast<Utf8>().toDartString(),
            args,
          ));
        }
        return out;
      } finally {
        b.pxs_osc_packet_free(handle);
      }
    });
  }

  /// The bytes of a message. Each value is written as its tag says (an 'i' takes a number and rounds it, an 'f' a float…); a missing one
  /// is the tag's zero.
  static Uint8List encode(String address, String types, List<Object?> args) {
    final b = Pxs.bindings;
    return using((arena) {
      final values = arena<pxs_value>(args.isEmpty ? 1 : args.length);
      for (var i = 0; i < args.length; i++) {
        writeValue(values + i, args[i], arena);
      }
      final out = arena<ffi.Pointer<ffi.Uint8>>();
      final size = arena<ffi.Size>();
      final status = b.pxs_osc_encode(
        address.toNativeUtf8(allocator: arena).cast(),
        types.toNativeUtf8(allocator: arena).cast(),
        values,
        args.length,
        out,
        size,
      );
      if (status != 0) throw PxsException(status, 'cannot encode the OSC message');
      return takeBytes(out.value, size.value);
    });
  }
}
