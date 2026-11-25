// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-PixelStereo-Commercial
import 'dart:ffi' as ffi;

import 'package:ffi/ffi.dart';

import 'bindings.g.dart';
import 'native.dart';

/// A setting of a filter: its name, its suffix (" s"…), its range and default.
class PxsFilterSetting {
  const PxsFilterSetting(this.name, this.suffix, this.min, this.max, this.def);
  final String name, suffix;
  final double min, max, def;
}

/// The filters pxs-core runs (pxs/core/filter/filter.h): what they are called, how they are saved, their settings. A kind is a number
/// from 0 to [count] - 1, in pxs-core's order; it is saved by its [key].
class PxsFilters {
  PxsFilters._();

  static PxsBindings get _b => Pxs.bindings;
  static String _s(ffi.Pointer<ffi.Char> p) => p == ffi.nullptr ? '' : p.cast<Utf8>().toDartString();

  static int get count => _b.pxs_filter_count();
  static String key(int kind) => _s(_b.pxs_filter_key(kind));
  static String name(int kind) => _s(_b.pxs_filter_name(kind));

  /// "Smoothing", "Shape", "Timing" or "Trigger".
  static String group(int kind) => _s(_b.pxs_filter_group(kind));
  static String about(int kind) => _s(_b.pxs_filter_about(kind));

  /// The kind saved as [key], null for none.
  static int? fromKey(String key) => using((arena) {
        final k = _b.pxs_filter_from_key(key.toNativeUtf8(allocator: arena).cast());
        return k < 0 ? null : k;
      });

  /// Setting [which] (0..3: a, b, c, d) of a kind; null when the kind does not use it.
  static PxsFilterSetting? setting(int kind, int which) => using((arena) {
        final out = arena<pxs_filter_setting>();
        if (_b.pxs_filter_setting_of(kind, which, out) != 0) return null;
        final s = out.ref;
        return PxsFilterSetting(_s(s.name), _s(s.suffix), s.min, s.max, s.def);
      });

  static int get mathOpCount => _b.pxs_math_op_count();
  static String mathOpKey(int op) => _s(_b.pxs_math_op_key(op));

  /// "x + a"…
  static String mathOpName(int op) => _s(_b.pxs_math_op_name(op));
  static int? mathOpFromKey(String key) => using((arena) {
        final op = _b.pxs_math_op_from_key(key.toNativeUtf8(allocator: arena).cast());
        return op < 0 ? null : op;
      });
  static bool mathOpUsesValue(int op) => _b.pxs_math_op_uses_value(op) != 0;
  static double mathOpApply(int op, double x, double a) => _b.pxs_math_op_apply(op, x, a);
}

/// A key of a Curve filter: (t, v) in 0..1, eased towards the next one (curve 0 to 5, 6 a Bézier by its handles, 100 a hold).
class PxsCurveKey {
  const PxsCurveKey(this.t, this.v, {this.curve = 0, this.inDt = 0, this.inDv = 0, this.outDt = 0, this.outDv = 0});
  final double t, v;
  final int curve;
  final double inDt, inDv, outDt, outDv;
}

/// One stage of a chain, as the application keeps it.
class PxsFilterStage {
  PxsFilterStage(this.kind,
      {this.enabled = true, this.a = 0, this.b = 0, this.c = 0, this.d = 0, this.op = 2, this.curve = const []});
  final int kind;
  final bool enabled;
  final double a, b, c, d;

  /// Math: its operation (2: multiply).
  final int op;

  /// Curve: its keys (none: the value goes through).
  final List<PxsCurveKey> curve;
}

/// A value through its stages, frame after frame (pxs-core's `filter::Chain`): [input] what comes in, [step] each frame. Not thread
/// safe: one isolate. [dispose] when done (a chain that is forgotten is freed with it).
class PxsFilterChain implements ffi.Finalizable {
  PxsFilterChain() : _chain = Pxs.bindings.pxs_filter_chain_new() {
    if (_chain == ffi.nullptr) throw PxsException(-5, 'no filter chain');
    _finalizer.attach(this, _chain.cast(), detach: this);
  }

  static final _finalizer = ffi.NativeFinalizer(Pxs.bindings.addresses.pxs_filter_chain_free.cast());

  final ffi.Pointer<pxs_filter_chain> _chain;
  bool _disposed = false;

  PxsBindings get _b => Pxs.bindings;

  ffi.Pointer<pxs_filter_chain> get _live {
    if (_disposed) throw StateError('the filter chain was disposed');
    return _chain;
  }

  /// A value from the source (not a number: ignored).
  void input(double value) => _b.pxs_filter_chain_input(_live, value);

  /// One frame of [dt] seconds through [stages]: the result, null when nothing came in yet.
  double? step(List<PxsFilterStage> stages, double dt) {
    final chain = _live;
    return using((arena) {
      final n = stages.length;
      final native = arena<pxs_filter_stage>(n == 0 ? 1 : n);
      for (var i = 0; i < n; ++i) {
        final s = stages[i], o = (native + i).ref;
        o.kind = s.kind;
        o.enabled = s.enabled ? 1 : 0;
        o.a = s.a;
        o.b = s.b;
        o.c = s.c;
        o.d = s.d;
        o.op = s.op;
        o.curve_count = s.curve.length;
        if (s.curve.isEmpty) {
          o.curve = ffi.nullptr;
        } else {
          final keys = arena<pxs_curve_key>(s.curve.length);
          for (var k = 0; k < s.curve.length; ++k) {
            final c = s.curve[k], kk = (keys + k).ref;
            kk.t = c.t;
            kk.v = c.v;
            kk.curve = c.curve;
            kk.in_dt = c.inDt;
            kk.in_dv = c.inDv;
            kk.out_dt = c.outDt;
            kk.out_dv = c.outDv;
          }
          o.curve = keys;
        }
      }
      final out = arena<ffi.Double>();
      final r = _b.pxs_filter_chain_step(chain, native, n, dt, out);
      if (r < 0) throw PxsException(r, 'a filter stage that cannot be');
      return r == 1 ? out.value : null;
    });
  }

  /// The value after each stage of the last step.
  List<double> get stageOutputs => using((arena) {
        final count = arena<ffi.Size>();
        final p = _b.pxs_filter_chain_stage_outputs(_live, count);
        return p == ffi.nullptr ? const [] : List<double>.of(p.asTypedList(count.value));
      });

  /// The source's last value; null before the first.
  double? get level => _optional(_b.pxs_filter_chain_level);

  /// What entered the first Curve in the last step; null when none did.
  double? get curveInput => _optional(_b.pxs_filter_chain_curve_input);

  double? _optional(int Function(ffi.Pointer<pxs_filter_chain>, ffi.Pointer<ffi.Double>) read) => using((arena) {
        final out = arena<ffi.Double>();
        return read(_live, out) == 1 ? out.value : null;
      });

  /// From scratch: nothing came in, no state.
  void reset() => _b.pxs_filter_chain_reset(_live);

  void dispose() {
    if (_disposed) return;
    _disposed = true;
    _finalizer.detach(this);
    _b.pxs_filter_chain_free(_chain);
  }
}
