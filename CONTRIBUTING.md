# Contributing

Thank you for helping. Before opening a pull request, read [LICENSING.md](LICENSING.md): pxs-core is available under the
AGPL **and** under a commercial licence, so the copyright holder must be free to relicense every line of it.

## Contributor licence

By submitting a contribution (a pull request, a patch, an issue with code), you confirm that:

1. you wrote it, or have the right to submit it under these terms (no code copied from a source whose licence does not
   allow it, nothing your employer owns without their written consent);
2. you license it to the project's copyright holder (Pixel Stereo) under the **AGPL-3.0-only**, **and** grant the holder a
   perpetual, worldwide, non-exclusive, royalty-free, irrevocable licence to use, modify, sublicense and distribute it
   under any other terms, including commercial licences and licences that are not open source;
3. you keep your own copyright in it.

Say so in the pull request description (`I agree to the contributor licence in CONTRIBUTING.md`), and sign off your commits
(`git commit -s`). A contribution without that statement is not merged.

## Rules of the code

See [docs/design.md](docs/design.md): no Qt, no platform API, bounded against hostile input, all or nothing on failure, the
wire format frozen by the golden vectors. New code comes with tests; a decoder comes with a fuzz target.
`cmake -S . -B build -DPXS_CORE_SANITIZE=ON -DPXS_CORE_WARNINGS_AS_ERRORS=ON && ctest --test-dir build` must pass, and so
must `reuse lint`.

*This contributor licence is a plain-language text, not legal advice; have it reviewed, or use a CLA service, if the project
grows.*
