// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-PixelStereo-Commercial
import 'dart:convert';
import 'dart:ffi' as ffi;

import 'package:ffi/ffi.dart';

import 'bindings.g.dart';
import 'native.dart';

/// One node of a device's tree, as a client reads it (pxs-core's `RemoteNode`).
class PxsRemoteNode {
  PxsRemoteNode._(this.json);

  final Map<String, dynamic> json;

  String get path => json['path'] as String;
  String get name => json['name'] as String;

  /// OSC type tags, one per argument; empty for a container.
  String get type => json['type'] as String;
  String get description => json['description'] as String;

  /// 0 none, 1 read, 2 write, 3 both.
  int get access => json['access'] as int;

  /// "action" (written, no value), "signal" (read, no value) or "" (a parameter, or a container).
  String get kind => json['kind'] as String;

  /// A type or an access: the node is a parameter, an action or a signal, whether it holds other nodes or not. pxs-core decides it.
  bool get hasValue => json['hasValue'] as bool;

  /// Neither a type nor an access: it only holds other nodes.
  bool get isContainer => json['container'] as bool;

  /// The names of the nodes it holds.
  List<String> get children => [for (final c in json['children'] as List) c as String];

  /// The last value known, one entry per argument (empty: none).
  List<dynamic> get value => (json['value'] as List?) ?? const [];

  /// DEFAULT_VALUE: what a reset writes, one entry per argument (empty: none announced).
  List<dynamic> get defaultValue => (json['default'] as List?) ?? const [];

  /// As one parameter (a node of one argument): null otherwise.
  PxsRemoteParam? get param => json['param'] == null ? null : PxsRemoteParam._(json['param'] as Map<String, dynamic>);
}

/// A node of one argument seen as a parameter (pxs-core's `ParamInfo`): what a control needs to be a view of it.
class PxsRemoteParam {
  PxsRemoteParam._(this._j);

  final Map<String, dynamic> _j;

  /// float, int, bool, choice, text or trigger.
  String get type => _j['type'] as String;
  String get label => _j['label'] as String;
  int get access => _j['access'] as int;
  bool get canWrite => access & 2 != 0;
  double get min => (_j['min'] as num).toDouble();
  double get max => (_j['max'] as num).toDouble();
  double get lo => (_j['lo'] as num).toDouble();
  double get hi => (_j['hi'] as num).toDouble();
  double get step => (_j['step'] as num).toDouble();

  /// No declared range: the value is not limited to what is shown.
  bool get unbounded => _j['unbounded'] as bool;
  bool get signal => _j['signal'] as bool;
  bool get action => _j['action'] as bool;
  String get unit => _j['unit'] as String;
  String get extendedType => _j['extendedType'] as String;
  List<String> get choices => [for (final c in _j['choices'] as List) c as String];

  /// DEFAULT_VALUE, when the device announces one; a reset is writing it. null: none.
  Object? get defaultValue => _j['default'];
}

/// What an OSCQuery device answered to `GET /`, kept as a tree to browse and read. The sockets are the caller's (HTTP, WebSocket LISTEN, UDP);
/// this says what the answer and the values mean, with the rule pxs-core has for every application. Not thread safe: one isolate.
/// Call [dispose] when done.
class PxsRemoteTree {
  PxsRemoteTree() : _remote = Pxs.bindings.pxs_remote_new() {
    if (_remote == ffi.nullptr) throw PxsException(-5, 'could not make a remote tree');
  }

  final ffi.Pointer<pxs_remote> _remote;
  bool _disposed = false;

  PxsBindings get _b => Pxs.bindings;

  void _alive() {
    if (_disposed) throw StateError('the remote tree was disposed');
  }

  /// Replaces everything with the answer to `GET /` (JSON text). false when it is not a node.
  bool load(String json) {
    _alive();
    return using((arena) => _b.pxs_remote_load(_remote, json.toNativeUtf8(allocator: arena).cast()) == 0);
  }

  /// A value the device told (LISTEN, `?VALUE`): [valueJson] is the JSON array, one entry per argument. false: no such node, or not an array.
  bool setValueJson(String path, String valueJson) {
    _alive();
    return using((arena) =>
        _b.pxs_remote_set_value(_remote, path.toNativeUtf8(allocator: arena).cast(), valueJson.toNativeUtf8(allocator: arena).cast()) == 0);
  }

  /// A value the device told, as Dart values.
  bool setValue(String path, List<Object?> value) => setValueJson(path, jsonEncode(value));

  /// The node at [path], or null.
  PxsRemoteNode? node(String path) {
    _alive();
    final text = _text((arena) => _b.pxs_remote_node(_remote, path.toNativeUtf8(allocator: arena).cast()));
    if (text == null) return null;
    return PxsRemoteNode._(jsonDecode(text) as Map<String, dynamic>);
  }

  /// What the tree is made of, not its values: two trees with the same signature show the same.
  String signature() {
    _alive();
    return _text((_) => _b.pxs_remote_signature(_remote)) ?? '';
  }

  /// The addresses of every node that has a value, in order.
  List<String> leaves() {
    _alive();
    final text = _text((_) => _b.pxs_remote_leaves(_remote));
    return text == null ? const [] : [for (final p in jsonDecode(text) as List) p as String];
  }

  String? _text(ffi.Pointer<ffi.Char> Function(Arena arena) call) => using((arena) {
        final p = call(arena);
        if (p == ffi.nullptr) return null;
        final s = p.cast<Utf8>().toDartString();
        _b.pxs_free(p.cast());
        return s;
      });

  /// Frees the native tree.
  void dispose() {
    if (_disposed) return;
    _disposed = true;
    _b.pxs_remote_free(_remote);
  }
}
