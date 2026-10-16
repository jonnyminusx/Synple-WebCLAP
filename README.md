# Synple WebCLAP

A browser port of [Synple](https://github.com/jonnyminusx/Synple), a subtractive synthesizer, built as a
[WebCLAP](https://github.com/WebCLAP) plugin (CLAP compiled to WebAssembly).

The interactive demo host now lives in `Jape-Host`.

## How it works

- `source/` is a small JUCE-free CLAP wrapper around the [Jape](https://github.com/jonnyminusx/Jape) engine,
  compiled to `synple.wclap.wasm` with [wasi-sdk](https://github.com/WebAssembly/wasi-sdk).
- `ui/` is the Synple React UI. It is built into a single HTML file, embedded in the WASM module and served
  through the CLAP `webview` extension. `ui/src/lib/bridge` replaces the JUCE JS bridge with message passing.
- GitHub tags (`v*`) trigger a release workflow that publishes `synple.wclap.wasm` as a release asset for hosts to consume.

## Build

```bash
export WASI_SDK_PATH=/path/to/wasi-sdk
(cd ui && npm ci)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=$WASI_SDK_PATH/share/cmake/wasi-sdk-p1.cmake
cmake --build build
```

Set `-DJAPE_SOURCE_DIR=...` to build against a local Jape checkout instead of the pinned release.

## License

See [LICENSE.md](LICENSE.md).
