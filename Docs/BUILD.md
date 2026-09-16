# Building NÁCAR

## Requirements

| | |
|---|---|
| CMake | 3.22 or newer |
| Compiler | anything with complete C++20 — GCC 13, Clang 15, MSVC 19.34 |
| JUCE | 8.0.4 |

Targets produced:

| Platform | Formats |
|---|---|
| macOS (arm64 + x86_64) | VST3, AU, Standalone |
| Windows x64 | VST3, Standalone |
| Linux x64 | VST3, Standalone |

AAX and CLAP are not enabled for V1. Nothing in the codebase assumes a format,
so adding them is one line in `CMakeLists.txt` plus the relevant SDK.

## Getting JUCE

The build looks for JUCE in three places, in order:

1. `-DNACAR_JUCE_PATH=/path/to/JUCE`
2. a `JUCE/` directory inside the repository
3. failing both, it fetches 8.0.4 with `FetchContent`

The first is fastest and is what CI should use.

```bash
git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git ~/JUCE
```

## Linux prerequisites

JUCE needs the X11, ALSA and FreeType development packages:

```bash
sudo apt-get install -y \
    libasound2-dev libfreetype6-dev libfontconfig1-dev \
    libx11-dev libxcomposite-dev libxcursor-dev libxext-dev \
    libxinerama-dev libxrandr-dev libxrender-dev \
    libglu1-mesa-dev mesa-common-dev
```

## Configure and build

```bash
cmake -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DNACAR_JUCE_PATH=$HOME/JUCE

cmake --build build --parallel
```

Artefacts land under `build/NACAR_artefacts/<config>/`.

### Debug

```bash
cmake -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DNACAR_JUCE_PATH=$HOME/JUCE
cmake --build build-debug --parallel
```

Debug builds keep JUCE's assertions live. Several of them exist specifically to
catch realtime-contract violations, so a Debug run is worth doing before any
release.

## Tests

The headless runner builds by default:

```bash
cmake --build build --target NacarTests
./build/Tests/NacarTests_artefacts/Release/NacarTests
```

It needs no audio device and no plugin host. It exits non-zero on failure, so it
drops straight into CI.

Turn it off with `-DNACAR_BUILD_TESTS=OFF`.

## Notes

`-ffast-math` is deliberately **not** enabled. Denormal and NaN behaviour
matters in the filters and the reverb tails, and the synth's own stability
guards depend on `std::isfinite` meaning what it says.

The source list is globbed with `CONFIGURE_DEPENDS`, so adding a file under
`Source/` needs no build-script change — but it does need a fresh `cmake -B`
with some generators. If a new file appears not to build, reconfigure.
