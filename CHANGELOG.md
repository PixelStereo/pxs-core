# Changelog

Versions follow [semantic versioning](https://semver.org). A change of what goes on the wire, of a limit, or of what a
decoder accepts is breaking unless it only makes a decoder stricter for packets no conforming peer sends.

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
