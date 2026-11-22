# Making the parameter tree portable — design proposal (stage 2 and after)

Status: **proposal, for review**. Nothing here is implemented. Stage 1 (MIDI messages, the change feed) is in pxs-core 0.3.0.

## Goal

A tool that has no Qt — a command-line gateway, the Flutter mobile apps through a C ABI, a server — must be able to declare
an address space (parameters, actions, signals), be driven by OSC, be described to OSCQuery clients, save and recall
presets, and tell its listeners what changed: with the **same behaviour** as the Qt applications, from **one**
implementation. The Qt applications keep working, and Qt stays the layer that adds sockets, signals and widgets.

## What is in the way (measured on pxs-qt 18f71e0)

| Module | Lines | Qt types in its interface |
|---|---|---|
| `params/Parameter` | 371 | `QString`, `QVariant` (value, default, bindings), `QJsonValue`, `QStringList`, `QList<int>` |
| `tree/Tree`, `Node` | 632 | `QString`, `QVariantList`, `QJsonArray/Object`, `osc::Message`; **public data members** (`type`, `access`, `range`, `get`, `set`, …) that applications assign |
| `tree/Preset` | 170 | `QJsonObject` |
| `oscquery/*` | 800 + 323 (mDNS) | QtNetwork sockets, `QJsonObject`, `QObject` |
| `ChangeFeed` | 56 | `QString` (a portable one exists since 0.3.0) |

Applications are the constraint: NyxMap alone has ~490 places that read or write a value, and nodes are filled in by hand.

## Design

### 1. A portable value
`pxs::core::Value`, a closed variant (the lesson of libossia: a fixed set beats a dynamic one):

`Impulse | bool | int32 | float | double | string | Vec2 | Vec3 | Vec4 | Blob | List`

`Vec2/3/4` are `std::array<float, N>` (a colour is a `Vec4` with an extended type `color.rgba`, as OSCQuery says it). A node
holds `std::vector<Value>`, one per OSC argument, exactly what `QVariantList` is today. The OSC mapping is fixed and
tested: `Vec3` is `fff`, `TYPE "fff"`, `RANGE` per component. Conversion to and from `osc::Value` (pxs-core 0.2) and to
JSON is in pxs-core; conversion to and from `QVariant` is in pxs-qt, with a round-trip test over every case the Qt tree
handles today.

### 2. A portable Parameter and Tree
`pxs::core::Parameter` and `pxs::core::tree::{Node, Tree}` with the semantics of today's classes, in `std::string` and
`Value`, **no Qt, no global state**, thread rules written in the header. `ParamInfo` keeps its fields (type, ramp, access,
range, limits, step, choices, unit, extended type, …). JSON (OSCQuery answers, presets, project files) uses
**nlohmann/json**, vendored as one pinned header with its licence in `LICENSES/` (REUSE-checked).

### 3. The Qt layer is an adapter, and the behaviour is the contract
pxs-qt keeps `pxs::Tree`, `Node`, `Parameter` and their signatures. The regression gate is what exists today: `tree_test`,
`params_test`, `preset_test`, `oscquery_spec_test`, `server_test`, plus a **differential test** of the old Qt tree against the
adapter over randomly generated trees and OSC traffic (what `apply` does, what `json()` returns, what a preset saves),
exactly as was done for the codecs. The Qt `ChangeFeed` moves onto `pxs::core::ChangeFeed` here, where the conversion
cost disappears (the tree notifies in `std::string`).

### 4. OSCQuery is sans-I/O
The core builds the answers (`FULL_PATH`, `CONTENTS`, attributes, `HOST_INFO`) and parses the requests (`?VALUE`, `LISTEN`,
`PATH_ADDED`…) as functions of the tree and bytes. An HTTP/1.1 request parser and WebSocket upgrade are state machines
over bytes (the framing and the handshake key exist since 0.2); mDNS is a packet builder and parser. **Sockets are not in
the core**: Qt uses QtNetwork, Flutter uses `dart:io`, a tool may use an optional Asio module. The same golden vectors and
fuzzers apply to every new parser (an HTTP parser is new attack surface).

### 5. A C ABI for Flutter
`pxs-c`: opaque handles, flat structs, no exception crossing the boundary, an event queue to poll (no callback from a
foreign thread into Dart), ownership written per function. Generated Dart bindings with `ffigen`. Versioned separately:
it is the hardest surface to change once published. Only codecs, values, tree and OSCQuery pure parts are exposed.

## The one hard problem: `Node`'s public data members
Applications assign `node.type`, `node.get`, `node.set`, `node.range`… With a portable core those become accessors. Three
ways, **decision needed**:

- **A — Source-compatible shim** (recommended first step): keep Qt `Node` with its public members as today, and make the
  Qt `Tree` *own* a core tree that it keeps in sync through the same entry points (`add`, `addAction`, `Parameter`). Apps do
  not change; a thin mirror costs memory and a conversion on writes. Then migrate the apps module by module and remove the
  shim: each step is a small PR the CI can judge.
- **B — Break once, in lockstep**: Qt `Node` becomes a façade with accessors; the seven apps change in the same release
  window. Cleanest end state, a large coordinated change (NyxMap dominates).
- **C — Two trees**: leave the Qt tree and write a new portable one. No migration, but two implementations that must be kept
  identical: this is exactly what the pxs-core split was meant to avoid. Not recommended.

## Order of work
1. Value + conversions + golden vectors (pxs-core 0.4).
2. Parameter and Tree in core, with the differential harness against the Qt tree (0.5); Qt adapter, shim (A).
3. Presets and JSON (nlohmann) (0.6).
4. OSCQuery pure parts, HTTP parser, mDNS packets (0.7), each with a fuzzer.
5. Migrate the apps off the shim, one PR per app; then `pxs-c` and the Flutter binding.

Each step is a pxs-core tag and a pxs-qt PR; none leaves a repository in a state its CI cannot judge.

## Decisions needed
1. **A, B or C** for the public `Node` members (recommended: A, then migrate).
2. **Value set**: is `Vec2/Vec3/Vec4/Blob/List` right, or is anything missing (a `Color` of its own? integer vectors?).
3. **Mobile networking**: confirm sockets stay out of the core (Qt: QtNetwork, Flutter: `dart:io`).
