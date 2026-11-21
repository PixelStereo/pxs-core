# pxs-core

Portable C++ building blocks shared by Pixel Stereo applications. The OSC
module provides packet encoding, decoding, and address-pattern matching
without Qt or platform APIs.

```cmake
add_subdirectory(pxs-core)
target_link_libraries(MyApp PRIVATE pxs-core::osc)
```

The Qt compatibility layer and UI modules live in
[pxs-qt](https://github.com/PixelStereo/pxs-qt). `pxs-core` is licensed under
the GNU Affero General Public License, version 3; see `LICENSE`.
