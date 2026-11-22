# Releasing pxs-core

pxs-core is consumed as a Git submodule (pxs-qt, then the applications through pxs-qt) and as an installed CMake
package. A consumer pins a **tag**, never a branch and never a commit that is not on `main`.

## Cut a release

1. Everything for the release is merged on `main` and CI is green. Squash-merge is fine: the tag is made on `main`.
2. Update `CHANGELOG.md` (move the entries under the new version) and `project(pxs-core VERSION x.y.z)` in `CMakeLists.txt`.
3. Merge that, then tag the merge commit on `main`:
   ```sh
   git tag -a v0.7.0 -m "pxs-core 0.7.0" && git push origin v0.7.0
   ```
4. In pxs-qt: `git -C third_party/pxs-core fetch --tags && git -C third_party/pxs-core checkout v0.7.0`, commit the new
   pointer. pxs-qt's CI refuses a pointer that is not a tag of pxs-core (a tag is made on `main`, so it can never become
   unreachable after a squash-merge or a force-push).

## What a version number promises

- **patch**: bug fixes, tests, docs. Nothing a conforming peer can observe changes.
- **minor**: new modules or functions; a decoder may become stricter only for packets no conforming peer sends.
- **major**: anything else (what goes on the wire, a limit lowered, a function removed or changed). The golden vectors are
  the arbiter: if `test/golden/vectors.tsv` has to change, the release is major.

`find_package(pxs-core 0.7)` accepts any 0.7.x (`SameMinorVersion`), as 0.x may change between minors.
