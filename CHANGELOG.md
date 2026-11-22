# Changelog

Versions follow [semantic versioning](https://semver.org). A change of what goes on the wire, of a limit, or of what a
decoder accepts is breaking unless it only makes a decoder stricter for packets no conforming peer sends.

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
