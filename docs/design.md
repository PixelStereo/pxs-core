# Design notes

## What belongs in pxs-core

A module belongs here when **its public interface mentions no Qt type and no platform call**, and when what it does is a
pure function of its input: encoding, decoding, validating, naming. Sockets, timers, threads, the UI and anything that
keeps state between calls belong to the layer above (pxs-qt for Qt applications, the application otherwise).

Not (yet) here, and why:

- **Transport** (UDP/TCP/WebSocket servers, mDNS), **the parameter tree and OSCQuery**, **ChangeFeed**: they expose `QString`,
  `QVariant` or `QObject` in their interface. Moving them is a redesign of the public API around portable types, not an
  extraction; it is decided module by module.
- **MIDI messages**: `parse()` fills an `InteractionEvent`, a Qt-facing type. The bytes of the messages are portable; the
  event type has to be split first.

## Rules for every module

1. **Bounded by construction.** Every length read from input is compared against the room left (never `pos + n`); loops and
   allocations are bounded by the input size and a named limit; no recursion without a depth limit.
2. **All or nothing.** A decoder that fails leaves its output as it found it.
3. **No Qt-isms.** Text is UTF-8 in `std::string`; a character is a Unicode code point (never a UTF-16 unit); a byte that is
   not valid UTF-8 is kept, and stays distinct from every other.
4. **One extra copy is a bug on a hot path.** Decoders take `(const uint8_t *, size_t)`; the `Bytes` overloads are
   conveniences over them. Encoders can append to a caller's buffer.
5. **The wire format is the contract.** Golden vectors are generated from a reference implementation and not edited to
   make a test pass.

## Why Qt adapters, not a second copy

pxs-qt keeps its public API (`osc::Message`, `pxs::dmx::Frame`, `pxs::websocket::decode`, …) and implements it on top of
pxs-core by converting at the boundary. The applications did not change, and there is one implementation of each format:
a bug fixed here is fixed everywhere. pxs-qt's own tests still run through the adapters, so they guard the conversion.
