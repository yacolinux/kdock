#!/bin/sh
set -eu

root_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root_dir"

ref=${1:-HEAD}
version=${2:-$(sed -n 's/^project(kdock VERSION \([^ ]*\).*/\1/p' CMakeLists.txt)}
output_dir=${3:-dist}

if [ -z "$version" ]; then
    echo "could not determine project version" >&2
    exit 1
fi

mkdir -p "$output_dir"
prefix="kdock-${version}/"
tarball="$output_dir/kdock-${version}.tar.gz"
zipfile="$output_dir/kdock-${version}.zip"

git archive --format=tar.gz --prefix="$prefix" "$ref" > "$tarball"
git archive --format=zip --prefix="$prefix" "$ref" > "$zipfile"

echo "$tarball"
echo "$zipfile"
