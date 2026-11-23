#!/usr/bin/env bash
# The version of pxs-core is one line, VERSION. Checks that the Dart package says the same, and, when a tag is given
# (v0.10.0), that the tag is that version: the release is the tag, so a tag that does not match VERSION is refused.
set -euo pipefail
cd "$(dirname "$0")/.."
version=$(tr -d '[:space:]' < VERSION)
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "VERSION must be x.y.z, got '$version'" >&2; exit 1; }
dart_version=$(sed -n 's/^version: *//p' dart/pubspec.yaml | tr -d '[:space:]')
[[ "$dart_version" == "$version" ]] || { echo "dart/pubspec.yaml says $dart_version, VERSION says $version" >&2; exit 1; }
if [[ $# -ge 1 ]]; then
  [[ "$1" == "v$version" ]] || { echo "tag $1 does not match VERSION ($version)" >&2; exit 1; }
fi
echo "pxs-core $version"
