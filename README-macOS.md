<p align="center">
  <img src="docs/preview/app-icon.png" width="128" alt="SpaghettiKart app icon">
</p>

# SpaghettiKart on macOS: quarrel07's working fork

A working fork of [HarbourMasters/SpaghettiKart](https://github.com/HarbourMasters/SpaghettiKart), the Mario Kart 64 PC port built on [libultraship](https://github.com/Kenix3/libultraship). Fixes are developed here and sent upstream as pull requests; the fork exists so they can be built and tested together before they land.

## Branches

* **`main`** mirrors upstream `main`.
* **`fork-release`** is upstream `main` plus every open pull request from this fork merged in, plus the macOS-only pieces listed below. Builds and tags come from this branch.
* Everything else is a pull request branch.

The open pull requests are listed at [upstream, filtered by author](https://github.com/HarbourMasters/SpaghettiKart/pulls?q=is%3Apr+is%3Aopen+author%3Aquarrel07), and the same for [libultraship](https://github.com/Kenix3/libultraship/pulls?q=is%3Apr+is%3Aopen+author%3Aquarrel07).

## Getting the game

No builds are published from this fork. The official game is at the [upstream releases](https://github.com/HarbourMasters/SpaghettiKart/releases). To run the fork's branch, build it as below.

On first run the game asks for your **US Mario Kart 64 ROM** (`.z64`, SHA-1 `579C48E211AE952530FFC8738709F078D5DD215E`) and extracts `mk64.o2r` into `~/Library/Application Support/com.spaghettikart/`. No copyrighted assets are bundled; you supply your own legally dumped ROM.

## Building

```bash
brew install cmake ninja sdl2 sdl3 sdl2_net libpng glew libzip nlohmann-json tinyxml2 spdlog libogg libvorbis vorbis-tools boost
git clone --recurse-submodules https://github.com/quarrel07/SpaghettiKart.git
cd SpaghettiKart
git checkout fork-release && git submodule update --init --recursive
cmake -H. -Bbuild-cmake -GNinja -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake       # produces build-cmake/SpaghettiKart.app
cmake/macos/make-dmg.sh build-cmake   # optional: wraps the app in a .dmg
```

The app is ad-hoc signed and self-contained, using upstream's own macOS packaging. `sdl3` is needed at build time because Homebrew's `sdl2` is the sdl2-compat shim, which loads SDL3 at runtime and gets bundled alongside. Pass `-DSPAGHETTI_BUNDLE_DEPS=OFF` for a local build that uses your Homebrew libraries directly.

## What is fork-only

| Where | What |
|-------|------|
| `libultraship` submodule | Pinned to [`quarrel07/libultraship@sk-libus-pm-2026-08-15`](https://github.com/quarrel07/libultraship/tree/sk-libus-pm-2026-08-15): the same lus commit upstream pins, plus this fork's open libultraship pull requests and a macOS SDL2 lookup fix. |
| `src/port/Engine.cpp` | Metal shader prewarm at boot (upstream PR #710) and sharp ImGui text on Retina displays. |
| `Info.plist` | Fork version number, writable data folder at `~/Library/Application Support/com.spaghettikart` via `SHIP_HOME`, HiDPI flag. |
| `cmake/macos/make-dmg.sh` | Packages the built app as a `.dmg`. |
| `.github/workflows/fork-release.yml` | CI for the `fork-release` branch. |

Torch is upstream's own pin, unchanged.

## Credits

All credit for SpaghettiKart goes to the **[HarbourMasters](https://github.com/HarbourMasters)** team and contributors, and to **[Kenix3](https://github.com/Kenix3)** and the libultraship project. This fork only carries fixes on their way upstream and the macOS packaging conveniences above.
