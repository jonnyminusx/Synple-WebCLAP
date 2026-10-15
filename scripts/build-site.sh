#!/usr/bin/env bash
# Assembles the static demo site: WebCLAP browser-test-host (fetched, not vendored) + synple.wclap.wasm.
set -euo pipefail

HOST_REPO="https://github.com/WebCLAP/browser-test-host.git"
HOST_SHA="b42ade615bef2c989f96400a7b0b2ef15cabd396"

root="$(cd "$(dirname "$0")/.." && pwd)"
wasm="${1:-$root/build/synple.wclap.wasm}"
out="${2:-$root/site}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

git clone --quiet "$HOST_REPO" "$work/host"
git -C "$work/host" checkout --quiet "$HOST_SHA"
git -C "$work/host" submodule update --init --recursive --depth 1 --quiet

rm -rf "$out"
mkdir -p "$out/examples/signalsmith-clap-cpp" "$out/examples/synple"
host="$work/host"
cp -R "$host/clap-audionode" "$host/page-proxy-service-worker.js" "$host/background.jpg" "$out/"
rm -rf "$out/clap-audionode/host-dev"
mkdir -p "$out/audio" && cp "$host/audio/loop.mp3" "$out/audio/"
cp "$host/examples/signalsmith-clap-cpp/example-plugins.wclap.tar.gz" \
   "$host/examples/signalsmith-clap-cpp/LICENSE.txt" "$out/examples/signalsmith-clap-cpp/"
cp "$wasm" "$out/examples/synple/synple.wclap.wasm"

sed -e "s#examples/signalsmith-basics/basics.wclap.tar.gz#examples/synple/synple.wclap.wasm#" \
    -e "s#get('module')).buffer#get('module') || 'synple').buffer#" \
    -e "s#<title>[^<]*</title>#<title>Synple (WebCLAP)</title>#" \
    "$host/index.html" > "$out/index.html"
touch "$out/.nojekyll"
echo "Site assembled in $out"
