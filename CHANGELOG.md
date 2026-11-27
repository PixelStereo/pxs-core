# Changelog

Versions follow [semantic versioning](https://semver.org). A change of what goes on the wire, of a limit, or of what a
decoder accepts is breaking unless it only makes a decoder stricter for packets no conforming peer sends.

## 0.16.0

### Added
- Scoped presets with saved timeline definitions and a shared query for editable preset parameters.
- Native MIDI and DMX control ranges and encoding for device output.

## 0.15.0

### Added
- Optional source-tree target `pxs-core::rtmidi` for native MIDI ports. Applications can enable the pinned RtMidi backend with
  `PXS_CORE_BUILD_RTMIDI`; the portable MIDI codec remains available without a native backend.
- Preset timing inheritance, per-parameter overrides and playback with timeline easing.
- OSC device namespace segments derived from device names.

## 0.14.0

### Added
- **`Tree::fireIfChanged`** (`pxs/core/tree/tree.h`): a valued signal fired only when its arguments differ from what it fired last, for
  signals a tick says again and again (a progress, a level) — the OSC output and the observers are not told the same value twice. An
  impulse (type `N`) fires every time, as it should. What was fired last is forgotten when the node is removed, or the tree cleared.
- **Devices** (`pxs/core/device/device.h`, the module `pxs-core::device`): what an application drives, the same in every application, from
  OSCQlock's destinations. A device is an OSCQuery device, a plain OSC receiver, a DMX fixture or a MIDI instrument, with the addresses it
  takes (a path and a range). What a value is on its wire: an OSC message of the address's tag (f, d, i, h, T); DMX on 8 bits, or 16 on two
  channels, coarse then fine, from the fixture's start address in its universe; MIDI control change, note, pitch bend or program change on
  the device's channel. A plain OSC device may declare no address and take any path (MpOsc's destinations). `Registry` keeps an id for
  each device by what it is (kind and name; an OSCQuery device added by hand by its host and port), so that a device found again or read
  from a show file gets the id it had, says whether a value differs from the last one sent there, and saves the devices made by hand with
  every id as JSON. The sockets, the DMX outputs, the MIDI ports and the discovery stay the platform's (pxs-qt). Not yet in the C interface.
- **Physics filter** (`filter::Kind::Physics`, key `physics`, in Smoothing): `pxs-core::physics`'s Mass-Spring as a stage of a chain, in
  0..1, with Friction, Attraction, Tension and Bounce. The value holds it (the spring pulls it with Attraction) while values come
  (`kPhysicsQuiet`, a quarter of a second); when the source goes quiet it is let go: thrown on by its speed, pulled back to where the
  value was by Tension, bouncing on 0 and 1. Interpolate is Weighted Average. `kKindCount` is 19; `pxs-core::filter` links
  `pxs-core::physics`.

## 0.13.0

### Added
- **Filters** (`pxs/core/filter/filter.h`, the module `pxs-core::filter`): a value through a chain of stages, frame by frame, each on or off.
  The 18 filters of NyxMap's interactions (Average, Weighted Average, Attack Release, Damper; Curve, Math, Clamp, Steps, Remap, Threshold;
  Delay, Hold, Debounce, Auto Return, Speed; Toggle, Pulse Detection, Envelope), their settings (names, ranges, defaults), the keys they
  are saved under, their groups and descriptions, the Math operations, and the state each keeps between frames (`Chain`). Moved from
  pxs-qt's `InteractionProcessor`, whose tests and NyxMap's pass unchanged on it; `StageView` lets an application hand its own stages
  over each frame without copying a curve. A Curve filter's keys are `pxs-core::timeline`'s.
- **Physics** (`pxs/core/physics/physics.h`, the module `pxs-core::physics`): a control's numbers moved by the hand, Interpolate and
  Mass-Spring (attraction, tension, friction, the bounce on the ends of the range), with the ranges of the settings. Moved from pxs-qt's
  controls; it takes plain values per number (which control and which range stay the application's).
- **C interface and Dart**: `pxs_filter_*`, `pxs_filter_chain_*`, `pxs_math_op_*`, `pxs_curve_at` and `pxs_physics_*`; `PxsFilters`,
  `PxsFilterChain` (freed by a `NativeFinalizer` when forgotten) and `PxsPhysics` in the Dart package. OSCQontrol's own Dart filters and
  physics gave the same values as these (within 1e-9, every filter and mode, Bézier curves included) before it moved to them. The C
  interface grows by adding (`PXS_C_API_VERSION` stays 1); `bindings.g.dart` is regenerated.

## 0.12.0

### Added
- **Timelines** (`pxs/core/timeline/timeline.h`, the module `pxs-core::timeline`): numbers drawn over time, the logic only. The keys of a
  curve and the way each goes to the next (six easings, a hold, a Bézier with its handles), colour curves, a first key that starts from the
  value the number has, the oscillators (sine, triangle, saw, square, random, smooth random, drawn from a seed), and the transport (one
  pass of a duration, once, in a loop or back and forth, a number of passes, a speed, a loop mode changed while it plays, and what play, pause, stop, rewind, seek do to it). From NyxMap's
  timelines, checked against them value for value (3 million values, bit for bit) before it moved. What a timeline drives, how it is saved
  and how it is edited stay with the application (pxs-qt has the Qt face and the editor).

## 0.11.0

### Changed (breaking)
- **A value can hold nodes.** `Node::isContainer()` is now "has no value of its own" (no type and no access), and `kind()` follows
  it. A node with a type that also holds listed children (`/layer/1/color/tint` and `/layer/1/color/tint/enable`) is a
  parameter, not a container: it keeps its VALUE, is a preset member, and tells its listeners; its OSCQuery answer has both
  TYPE and CONTENTS. New: `Node::hasValue()`. Before, any node with listed children answered as a container.

### Added
- **The client side of the tree** (`pxs/core/tree/remote.h`: `RemoteTree`, `RemoteNode`, `remoteNodeJson`): what an OSCQuery device answers to `GET /`, read as a tree with the
  same rule as the server's (a node with a type or an access has a value, whether it holds nodes or not), as `ParamInfo` for a node of one
  argument, and `DEFAULT_VALUE` as the reset (nothing is guessed from `VALUE`). Written once for the Qt applications and the Flutter ones; no sockets in it.
- **C interface and Dart**: `pxs_remote_*` (`new`, `free`, `load`, `set_value`, `node`, `signature`, `leaves`) and `PxsRemoteNode` / `PxsRemoteParam` / `PxsRemoteTree` in the Dart package: the client tree
  for the Flutter applications, with the same rule. The C interface grows by adding (`PXS_C_API_VERSION` stays 1); `bindings.g.dart` is regenerated.
- **`pxs_tree_add_node`** and `PxsTree.addNode`: a node declared by hand, of any arguments (a point `ff`, a colour `fff`, a text with its keys), with a range per argument, a default,
  a unit, and the way to read and write its values; it may hold other nodes. The C interface grows by adding.
- `Tree::check()`: what every application's tree must have, in one place. A parameter has its TYPE and ACCESS, a readable one its
  VALUE, a numeric one its RANGE, a writable numeric one its DEFAULT_VALUE. There is no reset address: a client resets a
  parameter by writing its DEFAULT_VALUE (`Tree::reset` does the same in the application). For an application's tests.
- `oscQueryAttributes()`: every attribute of OSCQuery and whether pxs serves it. `CRITICAL`, `HTML` and `OVERLOADS` are declared
  on `Node` (`critical`, `html`, `overloads`) and marked not served: implement each when an application needs it.

## 0.10.0

### Changed (breaking)
- **Attached methods** (`Node::attached`): a method of a node (a parameter's reset, for one) is reached by its address like any
  node, but is not listed in its parent's CONTENTS and does not make a parameter a container. The parameter keeps its VALUE,
  is still a preset member, and still tells its listeners when it changes. Before, a parameter with a `reset` child answered
  as a folder. What OSCQuery answers for such a node changes: its CONTENTS no longer lists the reset.

## 0.9.1

### Added
- **The Dart package** (`dart/`, `pxs_core`): bindings generated by ffigen from the C interface (committed; CI regenerates them and fails if they differ
  from the header) and wrappers that free what they hold: `Pxs.open`, `Osc`, `PxsTree` (parameters, actions and signals with Dart callbacks
  through `NativeCallable.isolateLocal`, OSC in and out, OSCQuery answers, presets, locks), `Dmx`, `Midi`, `WebSocket`. 15 tests run against the
  real `libpxs_c` (CI on Linux and macOS); `dart analyze --fatal-infos` is clean.

## 0.9.0

### Added
- **The C interface** (`pxs/c/pxs.h`, `pxs-core::c`, and `libpxs_c` with `-DPXS_CORE_BUILD_SHARED_C=ON`): 30 functions for a program that is not C++ (a Flutter
  application through `dart:ffi`): the OSC codec, the address space (parameters, actions, signals, OSC applied to it, the feedback as OSC
  packets, what an OSCQuery client is told, locks), presets, DMX frames, MIDI messages, the WebSocket accept key. Plain structs, status codes,
  no exception across the boundary, ownership written down (`docs/c-api.md`, `docs/ffigen.yaml`). Tested from C++ and from a C99 program with the
  header's warnings on; the shared library exports only the `pxs_*` functions (checked in CI on Linux, macOS and Windows) and is loaded by a C
  program as `dart:ffi` loads it.

## 0.8.0

### Added
- `pxs/core/tree/oscquery_server.h`: what an OSCQuery server answers to an HTTP request, without the HTTP: `answerRequest(tree, info,
  method, target)` -> status and JSON body (200 / 204 / 400 / 404 / 405, as the Qt server did), `splitTarget`, `originAllowed` (which page
  may open a WebSocket: none, localhost, loopback, or an address of this machine), `parseCommand` (the LISTEN / IGNORE of a WebSocket text
  message). The server (QTcpServer, `dart:io`'s HttpServer, a socket of its own) reads the request and writes the answer.
- Compared with `QUrl` (the parser the Qt server used) on 400k random targets: the same path and query for every origin-form target (starting
  with one `/`) except these, which differ on purpose: a non-ASCII query is not upper-cased (attribute names are ASCII), a `%2F` or `%23` in a
  path is decoded (QUrl keeps them encoded), a malformed `%` leaves the other escapes decoded (QUrl leaves the whole component encoded), a
  target that starts with `//` or a scheme is read as a path (QUrl reads an authority, or calls it invalid: 400).
- Fuzz target: arbitrary request targets.

## 0.7.0

### Added
- `ArgRange::items`: the range of an array in the type tags (`"i[ff]i"`) is a nested array of ranges, as OSCQuery writes it (the Qt tree
  allowed it through a free-form JSON range; the portable `ArgRange` could not say it).

## 0.6.0

### Added
- `pxs/core/tree/oscquery.h`: what an OSCQuery client is told about a tree, as JSON text: `describeJson` (FULL_PATH, CONTENTS, ACCESS,
  TYPE, VALUE, RANGE, CLIPMODE, DESCRIPTION, EXTENDED_TYPE, UNIT, DEFAULT_VALUE, TAGS, PXS_LOCKED and the node's own attributes),
  `attributeJson` (?VALUE, ?TYPE…), `valueJson`/`valuesJson` (the table of the OSCQuery proposal by type tag: a colour as #RRGGBBAA,
  arrays as arrays), `hostInfoJson`, `defaultExtensionsJson`. `Node::attributes` (the application's own attributes, each a JSON value
  as text). Keys are in alphabetical order.
- `pxs/core/tree/preset.h`: `Preset`, `capture`, `recall` (a preset by address: values, then actions) and its JSON, read
  tolerantly. `Tree::apply(address, values)` (arguments already as values).
- nlohmann/json 3.11.3 vendored in `third_party/nlohmann` (MIT, checksum recorded in `REUSE.toml` and its README), used inside the library
  only: no public header includes it, a consumer needs nothing of it.
- Checked against the Qt tree like the tree itself: the differential harness now also compares the description of every node, the
  attributes, `HOST_INFO`, the captured preset, the recall counts and the preset read back from its text, on trees that include
  hand-filled nodes of every type tag (r, h, c, m, b, T, [ff]i…) with tags, units, defaults, ranges and attributes: 10.6 million
  comparisons, no difference.
- Fuzz target: arbitrary text as a preset, an attribute and a HOST_INFO member. It found on its first run that the JSON library throws on a
  string that is not valid UTF-8 (an OSC string from the network can be anything): the JSON is now written with U+FFFD in place of invalid
  bytes, and nothing in the library throws. The crashing input is in the seed corpus and a test.

## 0.5.0

### Added
- `pxs-core::tree`: `Parameter`, `ParamInfo`, `Tree`, `Node` and the OSC bridge (`toOsc`, `fromOsc`, `toOscArguments`), moved from
  pxs-qt's `pxs::Tree` / `Parameter` in `std::string` and `Value`: no Qt, no global state (the change feed is an instance given to
  the parameter). A node's range is structured (`ArgRange`: MIN, MAX or VALS) and the locks are lists of addresses; the JSON of
  OSCQuery and of the presets comes with the JSON stage (0.6).
- Checked against the Qt tree it comes from with a differential harness: 3000 random trees (every parameter type, access, limits,
  values, choices, defaults, units, locks) with 40 random operations each (OSC messages with exact and pattern addresses, resets,
  locks, signals, structure changes): every value, write count, flag, range, feed, observer, structure and output notification
  equal over 2.2 million comparisons. The harness will run in pxs-qt's CI until its own tree is replaced.
- Fuzz target: OSC packets applied to a tree with a parameter of every type.

### Changed, against the Qt tree (deliberate; the harness lists them as its "loose" mode)
- An OSC argument that is no number — text that does not read as one, a blob, a Nil — is **refused** by a Float or Int parameter
  (the Qt tree wrote 0 for any text: `QVariant::canConvert<double>` is true for every string).
- A message with **no argument** (or a Nil) no longer selects the **first key** of a Choice parameter; it is refused.
- ±infinity is refused by an Int parameter (the Qt tree wrote an unspecified integer; a Float parameter already refused it).
- A Text parameter given an OSC float writes the shortest text of the float ("-108.8916"), not of its widening to a double
  ("-108.8916015625").
- A Bool written directly with a double follows `!= 0` (0.3 is on; QVariant rounded it to off). Choice keys are matched in lower
  case ASCII (QString lowercased all of Unicode).
- Written values are `Value`s of a given kind: a Float writes a double, an Int an int32, a Bool a bool, a Choice or Text a string.

## 0.4.0

### Added
- `pxs-core::value`: `Value`, the closed set of types of the address space (Nil, bool, int32, int64, float, double, string,
  Vec2, Vec3, Vec4, blob) with explicit, locale-independent conversions: `number()` (text read as a decimal number, "inf" and
  "nan", nothing on overflow or underflow), `truthy()`, `text()` (shortest text that reads back), and `ValueList`. Golden
  vectors for the reading of numbers in text come from Qt 6.12; QVariant's other conversions (an out-of-range double wrapping
  to an int, "0.4" being true) are deliberately not kept. Fuzz target for number reading and printing.

## 0.3.0

### Added
- `pxs-core::midi`: parse and build the channel messages of MIDI (moved from pxs-qt; the same bytes and values for every input
  the old code handled). A value that is not a number is now 0 (it was unspecified).
- `pxs-core::feed`: `ChangeFeed`, the observer of changes (moved from pxs-qt as an instance instead of a global; `ignore`
  still returns only when the callbacks in flight are over). CI runs it under ThreadSanitizer.

### Changed
- **Dual licence**: AGPL-3.0-only **or** a commercial licence from the copyright holder (`LICENSING.md`, `LICENSES/`,
  `REUSE.toml`), checked by `reuse lint` in CI. Contributions come with a licence grant that keeps this possible
  (`CONTRIBUTING.md`). The AGPL option is unchanged.

## 0.2.0

### Added
- `pxs-core::dmx`: Art-Net and sACN frames (moved from pxs-qt, byte for byte).
- `pxs-core::websocket`: RFC 6455 frames and the handshake accept key (moved from pxs-qt; SHA-1 and base64 included, no
  library needed).
- `pxs/core/osc/names.h`: `safeName`, `SegmentNamer`, `uniqueSegments` (moved from pxs-qt; the same output for every name).
- `osc::decode(const uint8_t*, size_t, …)` and `osc::encode(const Message&, Bytes &out)`: no copy of a received packet, and
  a message can be appended to a buffer.
- Named limits (`kMaxPacketBytes`, `kMaxArguments`, `kMaxBundleDepth`, `kMaxMessagesPerPacket`, `kMaxPatternChars`,
  `kMaxSegmentChars`).
- Tests (golden vectors, unit, hostile-input corpus), libFuzzer targets, CI on Linux/macOS/Windows with sanitizers,
  an installable CMake package (`find_package(pxs-core)`), `pxs/core/version.h`.

### Changed
- **OSC address patterns work on Unicode code points**, not UTF-16 units: `?` matches an emoji whole (it matched half of
  one in pxs-qt before). A byte that is not valid UTF-8 is a character of its own, equal to no other byte and to no valid
  character (before, all of them collapsed into U+FFFD and compared equal).
- `osc::decode` is all-or-nothing: a refused packet leaves `out` as it was (it used to keep the messages decoded before
  the error), and the limit on messages counts the ones of the packet, not those already in `out`.
- `osc::Message::types` is bytes, one tag per byte, and `types.size() == args.size()` always holds; a byte that is no
  known tag is an unknown tag (nil). (A non-ASCII tag used to be decoded as UTF-8.)
- WebSocket `decode` is stricter, as RFC 6455 requires: it refuses reserved bits, undefined opcodes and control frames of more
  than 125 bytes, and refuses from the header alone what can never become a valid frame (an unmasked frame no longer
  waits for its payload). Lengths are compared without overflow.
- The CMake targets take their include paths and C++ standard as `PUBLIC` properties and nothing else; warnings and
  sanitizers are `PRIVATE`. A second `add_subdirectory` of pxs-core in the same build is a no-op.

## 0.1.0
- OSC packet codec and address-pattern matching, extracted from pxs.
