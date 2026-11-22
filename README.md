# pxs-core

Portable C++17 building blocks shared by Pixel Stereo applications: the **wire formats** they speak, with no Qt, no
sockets and no platform API. A command-line tool, a microcontroller gateway or a server can link `pxs-core` without
pulling a GUI toolkit; the Qt applications use the same code through the adapters of
[pxs-qt](https://github.com/PixelStereo/pxs-qt).

| Target | Header | What it does |
|---|---|---|
| `pxs-core::osc` | `pxs/core/osc/codec.h` | OSC 1.0 packets (messages, bundles) and address-pattern matching |
| | `pxs/core/osc/names.h` | OSC-safe address segments (`safeName`, `SegmentNamer`, `uniqueSegments`) |
| `pxs-core::dmx` | `pxs/core/dmx/frame.h` | Art-Net (OpDmx) and sACN (E1.31) frames |
| `pxs-core::websocket` | `pxs/core/websocket/frame.h` | RFC 6455 frames and the handshake accept key |
| `pxs-core::midi` | `pxs/core/midi/message.h` | MIDI channel messages (control change, notes, pitch bend, program change): parse and build |
| `pxs-core::value` | `pxs/core/value.h` | `Value`: the closed set of types of the address space (bool, ints, floats, string, vec2/3/4, blob) and its conversions |
| `pxs-core::tree` | `pxs/core/tree/tree.h`, `parameter.h` | the address space: `Tree`, `Node`, `Parameter` (parameters, actions, signals), OSC applied to it, locks, observers; the OSCQuery JSON of a tree (`oscquery.h`) and presets (`preset.h`) |
| `pxs-core::feed` | `pxs/core/feed/change_feed.h` | header-only observer of changes (the OSC feedback), lock-free, safe to destroy after `ignore` |

```cmake
add_subdirectory(third_party/pxs-core)          # or find_package(pxs-core 0.7 REQUIRED) after `cmake --install`
target_link_libraries(MyTool PRIVATE pxs-core::osc)
```

```cpp
#include <pxs/core/osc/codec.h>
using namespace pxs::core::osc;

std::vector<Message> in;
if (decode(datagram.data(), datagram.size(), in))     // false: malformed, empty or beyond a limit; `in` untouched
    for (const Message &m : in) { /* m.address, m.types, m.args (std::variant) */ }

Bytes packet = encode(Message{"/mix/level", "f", {0.5f}});
bool hit = match("/mix/*", "/mix/level");
```

## What you can rely on

- **Hostile input is the normal case.** Every decoder checks each length against the room left, bounds its work and
  memory (the limits are named constants in the headers), refuses what is malformed and never reads outside its
  buffer. Failure is all-or-nothing: a refused packet leaves the output as it was.
- **Nothing throws and nothing is global.** The functions are pure; they are safe to call from any thread on distinct
  objects.
- **The wire format is frozen.** `test/golden/vectors.tsv` holds vectors generated from the pxs implementation that
  predates this library. They are never regenerated to make a test pass: a change there is a change of the protocol the
  applications speak, and it is a breaking release (see [docs/design.md](docs/design.md)).
- **Verified three ways:** golden and unit tests, a deterministic hostile-input corpus run under ASan/UBSan, and
  libFuzzer targets (`fuzz/`) seeded from the golden vectors. All run in CI on Linux (GCC, Clang), macOS and Windows
  (MSVC) on runners that have no Qt installed.

## Building and testing

```sh
cmake -S . -B build -DPXS_CORE_SANITIZE=ON -DPXS_CORE_WARNINGS_AS_ERRORS=ON
cmake --build build && ctest --test-dir build --output-on-failure
```

Options: `PXS_CORE_BUILD_TESTS` (default on when top-level), `PXS_CORE_BUILD_FUZZERS` (clang), `PXS_CORE_INSTALL`,
`PXS_CORE_WARNINGS_AS_ERRORS`, `PXS_CORE_SANITIZE`. Warnings and sanitizers apply to pxs-core's own targets only, never to
a consumer.

## Versions

[Semantic versioning](https://semver.org). Applications and pxs-qt pin pxs-core by **tag**; see
[RELEASING.md](RELEASING.md) and [CHANGELOG.md](CHANGELOG.md).

## License

**Dual-licensed**: the GNU Affero General Public License, version 3 (`LICENSE`), **or** a commercial licence from the
copyright holder under a separate agreement, for products and stores the AGPL does not fit (a closed-source app, the Apple
App Store…). See [LICENSING.md](LICENSING.md); contributions follow [CONTRIBUTING.md](CONTRIBUTING.md).
