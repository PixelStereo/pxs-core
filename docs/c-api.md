# The C interface

`include/pxs/c/pxs.h` gives a program that is not C++ — a Flutter application through `dart:ffi`, a script, another language's runtime —
what pxs-core does: the OSC codec, the address space (parameters, actions, signals, OSC applied to it, what an OSCQuery client is told),
presets, DMX frames, MIDI messages and the WebSocket handshake. It is one header and 30 functions, the same on every platform.

```sh
cmake -S . -B build -DPXS_CORE_BUILD_SHARED_C=ON && cmake --build build   # libpxs_c.so / .dylib / pxs_c.dll
```

Only the `pxs_*` functions are exported (checked by CI on Linux, macOS and Windows). On iOS, where a shared library cannot be loaded,
link the static `pxs-core::c` into the application and look the symbols up in the process.

## Rules

- **Nothing throws, nothing aborts.** A failure is a status (`PXS_ERR_*`, negative) or `NULL`.
- **Nothing is thread safe.** One object, one thread at a time (an isolate's, the UI's). Callbacks run on the thread that called.
- **Ownership is written down.** What a function returns that you own (a buffer, a string) is freed with `pxs_free`, an object with its
  `pxs_<name>_free`. What you pass in is copied before the call returns. A pointer inside a result lives as long as the object that holds it.
- **Text is UTF-8**, NUL-terminated; sizes are in bytes.
- **The interface grows by adding**: `PXS_C_API_VERSION` changes only with a new major version of it. Check `pxs_api_version()` at load time.

## Values

`pxs_value` is a plain struct (a `kind`, and the member that kind uses), so `ffigen` maps it without unions. A value you give is read during
the call; a value you get points into what holds it.

## A device in a dozen calls

```c
pxs_tree *tree = pxs_tree_new();
pxs_tree_set_output(tree, send_to_the_network, socket);                 // the feedback and the signals, as OSC packets
pxs_tree_add_parameter(tree, "/layer/1/opacity", "Opacity", PXS_PARAM_FLOAT, 0, 1, NULL, 0, get_opacity, set_opacity, &state);
pxs_tree_add_action(tree, "/show/go", "i", go_to_cue, &state);
pxs_tree_add_signal(tree, "/detect/peak", "f");

pxs_tree_apply_packet(tree, datagram, size);                            // what arrived on the UDP socket
int status; char *json = pxs_oscquery_answer(tree, "MyApp", osc_port, "GET", "/layer/1?VALUE", &status);  // what an HTTP request gets
pxs_free(json);
```

The sockets are yours (that is the point: `dart:io` on a phone, with the platform's permissions and multicast rules); pxs-core decides what
the bytes mean and what the answer is.

## Dart

Generate the bindings with [`ffigen`](https://pub.dev/packages/ffigen) from the header (`ffigen.yaml` is in this directory); load the
library, check `pxs_api_version()`, and wrap the objects in classes that free them with a `Finalizer` (`NativeFinalizer` on `pxs_tree_free`).
Callbacks (`pxs_get_fn`, `pxs_set_fn`, `pxs_action_fn`, `pxs_output_fn`) are `NativeCallable.isolateLocal`: they are called
synchronously on the isolate's own thread, by the calls above, which is what the interface promises.

*The Dart side is not in this repository yet and has not been run: this interface is tested from C and C++ (`test/c_api_test.cpp`,
`test/c_api_header_test.c`, `test/c_api_shared_test.c`).*
