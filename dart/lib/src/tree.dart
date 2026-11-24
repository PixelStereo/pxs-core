// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-PixelStereo-Commercial
import 'dart:ffi' as ffi;
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

import 'bindings.g.dart';
import 'native.dart';

/// What a parameter is (the same set as the C interface).
enum ParameterType { float, int_, bool_, choice, text, trigger }

/// Where a parameter's value lives: read when it is asked for, written when OSC (or anything else) writes it, already held within the
/// parameter's limits. Values are null, bool, int, double, String, Uint8List or [Vec].
typedef ParameterGetter = Object? Function();
typedef ParameterSetter = void Function(Object? value);

/// An action: the values it got, one per argument; true when it did it.
typedef ActionHandler = bool Function(List<Object?> args);

/// What the tree says (the feedback of a parameter, a signal fired), as an OSC packet to send.
typedef OutputListener = void Function(Uint8List packet);

/// The answer to an OSCQuery request: an HTTP status and a JSON body (null when there is none).
class OscQueryAnswer {
  OscQueryAnswer(this.status, this.body);
  final int status;
  final String? body;
}

/// The address space of an application: parameters, actions and signals, OSC applied to it, what an OSCQuery client is told, presets.
/// Not thread safe: one isolate. Call [dispose] when done (it frees the native tree and the callbacks).
class PxsTree {
  PxsTree() : _tree = Pxs.bindings.pxs_tree_new() {
    if (_tree == ffi.nullptr) throw PxsException(-5, 'cannot make the tree');
  }

  final ffi.Pointer<pxs_tree> _tree;
  bool _disposed = false;
  ffi.NativeCallable<pxs_output_fnFunction>? _output;
  final List<ffi.NativeCallable<dynamic>> _callables = [];
  final List<ffi.Pointer<ffi.Uint8>> _held = [];
  // What each path made to call back into Dart, so that removing a path (a rebuilt namespace) frees it
  final Map<String, List<ffi.NativeCallable<dynamic>>> _byPath = {};
  final Map<String, List<ffi.Pointer<ffi.Uint8>>> _heldByPath = {};

  void _own(String path, Iterable<ffi.NativeCallable<dynamic>?> callables, [List<ffi.Pointer<ffi.Uint8>>? held]) {
    final list = _byPath.putIfAbsent(path, () => []);
    for (final c in callables) {
      if (c != null) list.add(c);
    }
    if (held != null) _heldByPath[path] = held;
  }

  PxsBindings get _b => Pxs.bindings;

  void _alive() {
    if (_disposed) throw StateError('the tree was disposed');
  }

  static void _check(int status, String what) {
    if (status != 0) throw PxsException(status, what);
  }

  /// A parameter at [path] ("/layer/1/opacity"). [min] and [max] are the range and the limits of a float or an int; [choices] the keys
  /// (lower case) of a choice. A parameter with no [get] is only written, with no [set] only read.
  void addParameter(
    String path,
    ParameterType type, {
    String? label,
    double min = 0,
    double max = 1,
    List<String> choices = const [],
    ParameterGetter? get,
    ParameterSetter? set,
  }) {
    _alive();
    ffi.NativeCallable<pxs_get_fnFunction>? getter;
    ffi.NativeCallable<pxs_set_fnFunction>? setter;
    ffi.Pointer<ffi.Uint8> lastText = ffi.nullptr; // a string given by `get` stays valid until the next call
    if (get != null) {
      getter = ffi.NativeCallable<pxs_get_fnFunction>.isolateLocal((ffi.Pointer<ffi.Void> user, ffi.Pointer<pxs_value> out) {
        try {
          final arena = Arena();
          try {
            writeValue(out, get(), arena);
            // the value points into the arena: move strings and blobs into memory held until the next read
            final r = out.ref;
            if (r.data != ffi.nullptr) {
              final copy = malloc<ffi.Uint8>(r.size + 1);
              copy.asTypedList(r.size).setAll(0, r.data.asTypedList(r.size));
              copy[r.size] = 0;
              if (lastText != ffi.nullptr) {
                malloc.free(lastText);
                _held.remove(lastText);
              }
              lastText = copy;
              _held.add(copy);
              r.data = copy;
            }
          } finally {
            arena.releaseAll();
          }
          return 0;
        } catch (_) {
          return 1;
        }
      }, exceptionalReturn: 1);
      _callables.add(getter);
    }
    if (set != null) {
      setter = ffi.NativeCallable<pxs_set_fnFunction>.isolateLocal((ffi.Pointer<ffi.Void> user, ffi.Pointer<pxs_value> value) {
        try {
          set(readValue(value));
          return 0;
        } catch (_) {
          return 1;
        }
      }, exceptionalReturn: 1);
      _callables.add(setter);
    }
    _own(path, [getter, setter]);
    using((arena) {
      final keys = arena<ffi.Pointer<ffi.Char>>(choices.isEmpty ? 1 : choices.length);
      for (var i = 0; i < choices.length; i++) {
        keys[i] = choices[i].toNativeUtf8(allocator: arena).cast();
      }
      final status = _b.pxs_tree_add_parameter(
        _tree,
        path.toNativeUtf8(allocator: arena).cast(),
        (label ?? path).toNativeUtf8(allocator: arena).cast(),
        type.index,
        min,
        max,
        keys,
        choices.length,
        getter?.nativeFunction ?? ffi.nullptr,
        setter?.nativeFunction ?? ffi.nullptr,
        ffi.nullptr,
      );
      _check(status, 'cannot add the parameter $path');
    });
  }

  /// A node declared by hand, of any arguments: a point (`types: 'ff'`), a colour (`'fff'`), a text with its keys. [min] and [max] are the range of each argument
  /// (one per argument), [vals] the keys of a text node, [defaults] what a reset writes (DEFAULT_VALUE), [unit] what it measures. [get] gives one value per
  /// argument; [set] takes them and says whether it did. A node with a value may hold other nodes: add them at their own paths.
  void addNode(
    String path,
    String types, {
    int access = 3,
    String description = '',
    String unit = '',
    List<double>? min,
    List<double>? max,
    List<String> vals = const [],
    List<Object?> defaults = const [],
    List<Object?> Function()? get,
    bool Function(List<Object?> values)? set,
  }) {
    _alive();
    final held = <ffi.Pointer<ffi.Uint8>>[]; // the strings of the last read stay valid until the next
    ffi.NativeCallable<pxs_values_get_fnFunction>? getter;
    ffi.NativeCallable<pxs_values_set_fnFunction>? setter;
    if (get != null) {
      getter = ffi.NativeCallable<pxs_values_get_fnFunction>.isolateLocal(
        (ffi.Pointer<ffi.Void> user, ffi.Pointer<pxs_value> out, int room, ffi.Pointer<ffi.Size> count) {
          try {
            for (final p in held) {
              malloc.free(p);
              _held.remove(p);
            }
            held.clear();
            final values = get();
            final n = values.length < room ? values.length : room;
            final arena = Arena();
            try {
              for (var i = 0; i < n; i++) {
                writeValue(out + i, values[i], arena);
                final r = (out + i).ref;
                if (r.data != ffi.nullptr) {
                  final copy = malloc<ffi.Uint8>(r.size + 1);
                  copy.asTypedList(r.size).setAll(0, r.data.asTypedList(r.size));
                  copy[r.size] = 0;
                  held.add(copy);
                  _held.add(copy);
                  r.data = copy;
                }
              }
            } finally {
              arena.releaseAll();
            }
            count.value = n;
            return 0;
          } catch (_) {
            return 1;
          }
        },
        exceptionalReturn: 1,
      );
      _callables.add(getter);
    }
    if (set != null) {
      setter = ffi.NativeCallable<pxs_values_set_fnFunction>.isolateLocal(
        (ffi.Pointer<ffi.Void> user, ffi.Pointer<pxs_value> values, int count) {
          try {
            return set([for (var i = 0; i < count; i++) readValue(values + i)]) ? 0 : 1;
          } catch (_) {
            return 1;
          }
        },
        exceptionalReturn: 1,
      );
      _callables.add(setter);
    }
    _own(path, [getter, setter], held);
    using((arena) {
      final spec = arena<pxs_node>();
      final r = spec.ref;
      r.path = path.toNativeUtf8(allocator: arena).cast();
      r.types = types.toNativeUtf8(allocator: arena).cast();
      r.access = access;
      r.description = description.isEmpty ? ffi.nullptr : description.toNativeUtf8(allocator: arena).cast();
      r.unit = unit.isEmpty ? ffi.nullptr : unit.toNativeUtf8(allocator: arena).cast();
      if (min != null && max != null && min.length == max.length && min.isNotEmpty) {
        final lo = arena<ffi.Double>(min.length), hi = arena<ffi.Double>(min.length);
        for (var i = 0; i < min.length; i++) {
          lo[i] = min[i];
          hi[i] = max[i];
        }
        r.min = lo;
        r.max = hi;
        r.range_count = min.length;
      } else {
        r.min = ffi.nullptr;
        r.max = ffi.nullptr;
        r.range_count = 0;
      }
      if (vals.isNotEmpty) {
        final keys = arena<ffi.Pointer<ffi.Char>>(vals.length);
        for (var i = 0; i < vals.length; i++) {
          keys[i] = vals[i].toNativeUtf8(allocator: arena).cast();
        }
        r.vals = keys;
        r.vals_count = vals.length;
      } else {
        r.vals = ffi.nullptr;
        r.vals_count = 0;
      }
      if (defaults.isNotEmpty) {
        final d = arena<pxs_value>(defaults.length);
        for (var i = 0; i < defaults.length; i++) {
          writeValue(d + i, defaults[i], arena);
        }
        r.defaults = d;
        r.defaults_count = defaults.length;
      } else {
        r.defaults = ffi.nullptr;
        r.defaults_count = 0;
      }
      r.get = getter?.nativeFunction ?? ffi.nullptr;
      r.set = setter?.nativeFunction ?? ffi.nullptr;
      r.user = ffi.nullptr;
      _check(_b.pxs_tree_add_node(_tree, spec), 'cannot add the node $path');
    });
  }

  /// An action at [path] ([types]: the OSC type tags of its arguments, "N" for none).
  void addAction(String path, String types, ActionHandler handler) {
    _alive();
    final callable = ffi.NativeCallable<pxs_action_fnFunction>.isolateLocal(
      (ffi.Pointer<ffi.Void> user, ffi.Pointer<pxs_value> args, int count) {
        try {
          final values = <Object?>[for (var i = 0; i < count; i++) readValue(args + i)];
          return handler(values) ? 0 : 1;
        } catch (_) {
          return 1;
        }
      },
      exceptionalReturn: 1,
    );
    _callables.add(callable);
    _own(path, [callable]);
    using((arena) {
      _check(_b.pxs_tree_add_action(_tree, path.toNativeUtf8(allocator: arena).cast(), types.toNativeUtf8(allocator: arena).cast(), callable.nativeFunction, ffi.nullptr),
          'cannot add the action $path');
    });
  }

  /// A signal the application emits (no state: it is fired, nobody writes it).
  void addSignal(String path, [String types = 'N']) {
    _alive();
    using((arena) {
      _check(_b.pxs_tree_add_signal(_tree, path.toNativeUtf8(allocator: arena).cast(), types.toNativeUtf8(allocator: arena).cast()), 'cannot add the signal $path');
    });
  }

  /// True when there was a node at [path].
  bool remove(String path) {
    _alive();
    final removed = using((arena) => _b.pxs_tree_remove(_tree, path.toNativeUtf8(allocator: arena).cast()) == 0);
    if (removed) {
      // what the removed nodes called back into is freed with them (a namespace that is rebuilt does not grow)
      final prefix = path.endsWith('/') ? path : '$path/';
      for (final key in _byPath.keys.where((k) => k == path || k.startsWith(prefix)).toList()) {
        for (final c in _byPath.remove(key)!) {
          c.close();
          _callables.remove(c);
        }
        for (final p in _heldByPath.remove(key) ?? const <ffi.Pointer<ffi.Uint8>>[]) {
          malloc.free(p);
          _held.remove(p);
        }
      }
    }
    return removed;
  }

  /// Where everything the tree says goes (the feedback of a parameter, a signal fired): OSC packets to send. null: nothing is told.
  set onOutput(OutputListener? listener) {
    _alive();
    _output?.close();
    _output = null;
    if (listener == null) {
      _b.pxs_tree_set_output(_tree, ffi.nullptr, ffi.nullptr);
      return;
    }
    final callable = ffi.NativeCallable<pxs_output_fnFunction>.isolateLocal((ffi.Pointer<ffi.Void> user, ffi.Pointer<ffi.Uint8> packet, int size) {
      listener(Uint8List.fromList(packet.asTypedList(size)));
    });
    _output = callable;
    _b.pxs_tree_set_output(_tree, callable.nativeFunction, ffi.nullptr);
  }

  /// Applies the messages of an OSC packet from the network; how many nodes took one. Throws for a packet that is malformed.
  int applyPacket(Uint8List packet) {
    _alive();
    return using((arena) {
      final data = arena<ffi.Uint8>(packet.isEmpty ? 1 : packet.length);
      data.asTypedList(packet.length).setAll(0, packet);
      final accepted = _b.pxs_tree_apply_packet(_tree, data, packet.length);
      if (accepted < 0) throw PxsException(accepted, 'cannot apply the OSC packet');
      return accepted;
    });
  }

  /// Says that a parameter now holds another value (its message goes to the output). False: no such parameter.
  bool changed(String path) {
    _alive();
    return using((arena) => _b.pxs_tree_changed(_tree, path.toNativeUtf8(allocator: arena).cast()) == 0);
  }

  /// Fires a signal with these values (to the output). False: not a signal.
  bool fire(String path, [List<Object?> args = const []]) {
    _alive();
    return using((arena) {
      final values = arena<pxs_value>(args.isEmpty ? 1 : args.length);
      for (var i = 0; i < args.length; i++) {
        writeValue(values + i, args[i], arena);
      }
      return _b.pxs_tree_fire(_tree, path.toNativeUtf8(allocator: arena).cast(), values, args.length) == 0;
    });
  }

  /// A locked node refuses every write from outside. False: no such node.
  bool lock(String path, bool locked) {
    _alive();
    return using((arena) => _b.pxs_tree_lock(_tree, path.toNativeUtf8(allocator: arena).cast(), locked ? 1 : 0) == 0);
  }

  /// What an OSCQuery server answers to a request ([method] "GET", [target] "/layer/1?VALUE"): the HTTP status and the JSON body.
  OscQueryAnswer oscQueryAnswer(String method, String target, {String name = 'pxs', int oscPort = 0}) {
    _alive();
    return using((arena) {
      final status = arena<ffi.Int>();
      final body = _b.pxs_oscquery_answer(
        _tree,
        name.toNativeUtf8(allocator: arena).cast(),
        oscPort,
        method.toNativeUtf8(allocator: arena).cast(),
        target.toNativeUtf8(allocator: arena).cast(),
        status,
      );
      String? text;
      if (body != ffi.nullptr) {
        text = body.cast<Utf8>().toDartString();
        _b.pxs_free(body.cast());
      }
      return OscQueryAnswer(status.value, text);
    });
  }

  /// The values of every parameter that is saved, as JSON text.
  String presetCapture([String name = '']) {
    _alive();
    return using((arena) {
      final json = _b.pxs_preset_capture(_tree, name.toNativeUtf8(allocator: arena).cast());
      if (json == ffi.nullptr) throw PxsException(-5, 'cannot capture a preset');
      final text = json.cast<Utf8>().toDartString();
      _b.pxs_free(json.cast());
      return text;
    });
  }

  /// Puts back a preset (JSON text); how many values were written.
  int presetRecall(String json) {
    _alive();
    return using((arena) {
      final applied = _b.pxs_preset_recall(_tree, json.toNativeUtf8(allocator: arena).cast());
      if (applied < 0) throw PxsException(applied, 'cannot recall the preset');
      return applied;
    });
  }

  /// Frees the native tree and the callbacks. The tree cannot be used afterwards.
  void dispose() {
    if (_disposed) return;
    _disposed = true;
    _b.pxs_tree_set_output(_tree, ffi.nullptr, ffi.nullptr);
    _output?.close();
    _b.pxs_tree_free(_tree);
    for (final c in _callables) {
      c.close();
    }
    _callables.clear();
    for (final p in _held) {
      malloc.free(p);
    }
    _held.clear();
  }
}
