// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-PixelStereo-Commercial
import 'dart:convert';
import 'dart:ffi' as ffi;
import 'dart:io' show Platform;
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

import 'bindings.g.dart';

/// The C interface's version this package is written for (`PXS_C_API_VERSION`).
const int supportedApiVersion = 1;

/// What went wrong in a call to pxs-core: a status of the C interface and what was being done.
class PxsException implements Exception {
  PxsException(this.status, this.message);

  /// `PXS_ERR_*` of the header (negative), or an HTTP-like code where a call says so.
  final int status;
  final String message;

  @override
  String toString() => 'PxsException($status): $message';
}

/// The loaded library. [Pxs.open] once, at start-up; every other class uses it.
class Pxs {
  Pxs._();

  static PxsBindings? _bindings;
  static ffi.DynamicLibrary? _library;

  /// Loads libpxs_c. [path] is where it is; by default the platform's own name (`libpxs_c.so`, `libpxs_c.dylib`, `pxs_c.dll`), and on iOS
  /// the process itself (the static library is linked into the application). Throws when it is not there, or is another version of the
  /// C interface than this package is written for.
  static void open({String? path}) {
    final library = path != null
        ? ffi.DynamicLibrary.open(path)
        : Platform.isIOS
            ? ffi.DynamicLibrary.process()
            : ffi.DynamicLibrary.open(Platform.isWindows
                ? 'pxs_c.dll'
                : Platform.isMacOS
                    ? 'libpxs_c.dylib'
                    : 'libpxs_c.so');
    final bindings = PxsBindings(library);
    final version = bindings.pxs_api_version();
    if (version != supportedApiVersion) {
      throw PxsException(-1, 'the library speaks version $version of the C interface, this package version $supportedApiVersion');
    }
    _library = library;
    _bindings = bindings;
  }

  static bool get isOpen => _bindings != null;

  /// The generated bindings (the raw C functions), for what this package does not wrap.
  static PxsBindings get bindings {
    final b = _bindings;
    if (b == null) throw StateError('Pxs.open() has not been called');
    return b;
  }

  static ffi.DynamicLibrary get library => _library ?? (throw StateError('Pxs.open() has not been called'));

  /// pxs-core's version, "0.9.0".
  static String get version => bindings.pxs_version().cast<Utf8>().toDartString();
}

// ---- values, as they cross the boundary -------------------------------------------------------------------------------------------

const int _nil = 0, _bool = 1, _int32 = 2, _int64 = 3, _float = 4, _double = 5, _string = 6, _vec2 = 7, _vec3 = 8, _vec4 = 9, _blob = 10;

/// A float vector (a point, a position, a colour): [x, y], [x, y, z] or [r, g, b, a].
class Vec {
  Vec(this.components) : assert(components.length >= 2 && components.length <= 4);
  final List<double> components;

  @override
  bool operator ==(Object other) => other is Vec && _same(components, other.components);

  @override
  int get hashCode => Object.hashAll(components);

  @override
  String toString() => 'Vec($components)';

  static bool _same(List<double> a, List<double> b) {
    if (a.length != b.length) return false;
    for (var i = 0; i < a.length; i++) {
      if (a[i] != b[i]) return false;
    }
    return true;
  }
}

/// Writes a Dart value into a `pxs_value` (strings and blobs are allocated in [arena]: valid for the call).
/// null is Nil; bool; int (an int64); double; String; Uint8List (a blob); [Vec].
void writeValue(ffi.Pointer<pxs_value> out, Object? v, Arena arena) {
  final r = out.ref;
  r.kind = _nil;
  r.boolean = 0;
  r.integer = 0;
  r.number = 0;
  r.data = ffi.nullptr;
  r.size = 0;
  for (var i = 0; i < 4; i++) {
    r.vec[i] = 0;
  }
  if (v == null) return;
  if (v is bool) {
    r.kind = _bool;
    r.boolean = v ? 1 : 0;
  } else if (v is int) {
    r.kind = _int64;
    r.integer = v;
  } else if (v is double) {
    r.kind = _double;
    r.number = v;
  } else if (v is String) {
    final bytes = const Utf8Codec().encode(v);
    final p = arena<ffi.Uint8>(bytes.length + 1);
    p.asTypedList(bytes.length + 1)
      ..setAll(0, bytes)
      ..[bytes.length] = 0;
    r.kind = _string;
    r.data = p;
    r.size = bytes.length;
  } else if (v is Uint8List) {
    final p = arena<ffi.Uint8>(v.isEmpty ? 1 : v.length);
    p.asTypedList(v.length).setAll(0, v);
    r.kind = _blob;
    r.data = p;
    r.size = v.length;
  } else if (v is Vec) {
    r.kind = v.components.length == 2 ? _vec2 : v.components.length == 3 ? _vec3 : _vec4;
    for (var i = 0; i < v.components.length; i++) {
      r.vec[i] = v.components[i];
    }
  } else {
    throw ArgumentError.value(v, 'value', 'not a pxs value (null, bool, int, double, String, Uint8List or Vec)');
  }
}

/// Reads a `pxs_value` into a Dart value (strings and blobs are copied).
Object? readValue(ffi.Pointer<pxs_value> p) {
  final r = p.ref;
  switch (r.kind) {
    case _bool:
      return r.boolean != 0;
    case _int32:
    case _int64:
      return r.integer;
    case _float:
    case _double:
      return r.number;
    case _string:
      return r.data == ffi.nullptr ? '' : const Utf8Codec(allowMalformed: true).decode(r.data.asTypedList(r.size));
    case _blob:
      return r.data == ffi.nullptr ? Uint8List(0) : Uint8List.fromList(r.data.asTypedList(r.size));
    case _vec2:
      return Vec([r.vec[0], r.vec[1]]);
    case _vec3:
      return Vec([r.vec[0], r.vec[1], r.vec[2]]);
    case _vec4:
      return Vec([r.vec[0], r.vec[1], r.vec[2], r.vec[3]]);
    default:
      return null;
  }
}

/// Copies a buffer the library returned (`uint8_t *` + size) into Dart and frees it.
Uint8List takeBytes(ffi.Pointer<ffi.Uint8> p, int size) {
  final out = Uint8List.fromList(p.asTypedList(size));
  Pxs.bindings.pxs_free(p.cast());
  return out;
}
