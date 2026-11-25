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

/// How a control's numbers move (pxs/core/physics/physics.h): [none] where the hand is, [interpolate] glides to it, [massSpring] is
/// attracted to the finger and, let go, goes on with the push it got (thrown, it bounces on the ends; with a tension, it is pulled back).
class PxsPhysics {
  const PxsPhysics({
    this.mode = none,
    this.friction = 5,
    this.attraction = 120,
    this.tension = 0,
    this.wall = 0.6,
    this.rate = 12,
  });

  static const none = 0, interpolate = 1, massSpring = 2;
  static const frictionMax = 30.0, attractionMax = 400.0, tensionMax = 400.0, wallMax = 1.0, rateMin = 0.5, rateMax = 60.0;

  final int mode;
  final double friction, attraction, tension, wall, rate;

  /// Each setting within its range, the mode one of the three.
  PxsPhysics clamped() => using((arena) {
        final p = arena<pxs_physics>();
        _write(p.ref);
        Pxs.bindings.pxs_physics_clamp(p);
        return _read(p.ref);
      });

  void _write(pxs_physics o) {
    o.mode = mode;
    o.friction = friction;
    o.attraction = attraction;
    o.tension = tension;
    o.wall = wall;
    o.rate = rate;
  }

  static PxsPhysics _read(pxs_physics p) => PxsPhysics(
      mode: p.mode, friction: p.friction, attraction: p.attraction, tension: p.tension, wall: p.wall, rate: p.rate);

  /// One step of [dt] seconds: [pos] and [vel] (one per number, 0..1 of its range) move towards [target], in place. [held]: the hand is
  /// on the control; [hand] (null: all) which numbers it holds. Number i moves by [each][i], by the last one when there are fewer. True
  /// while something still moves.
  static bool step(List<double> pos, List<double> vel, List<double> target, List<PxsPhysics> each, double dt,
      {bool held = false, List<bool>? hand}) {
    final n = pos.length;
    if (vel.length != n || target.length != n || each.isEmpty) throw ArgumentError('one position, speed and target per number');
    return using((arena) {
      final p = arena<ffi.Double>(n == 0 ? 1 : n), v = arena<ffi.Double>(n == 0 ? 1 : n), t = arena<ffi.Double>(n == 0 ? 1 : n);
      final h = hand == null ? ffi.nullptr : arena<ffi.Uint8>(n == 0 ? 1 : n);
      for (var i = 0; i < n; i++) {
        p[i] = pos[i];
        v[i] = vel[i];
        t[i] = target[i];
        if (hand != null) h[i] = i < hand.length && hand[i] ? 1 : 0;
      }
      final s = arena<pxs_physics>(each.length);
      for (var i = 0; i < each.length; i++) {
        each[i]._write((s + i).ref);
      }
      final r = Pxs.bindings.pxs_physics_step(p, v, t, n, held ? 1 : 0, h.cast(), s, each.length, dt);
      if (r < 0) throw PxsException(r, 'physics');
      for (var i = 0; i < n; i++) {
        pos[i] = p[i];
        vel[i] = v[i];
      }
      return r == 1;
    });
  }
}
