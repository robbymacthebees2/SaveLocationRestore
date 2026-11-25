# Save Location Restore

A native [Dusklight](https://github.com/TwilitRealm/dusklight) mod that restores Link to the exact
stage, room, position, and facing recorded when a save was written. It stores this location in
Dusklight's per-slot mod save data. Saves without this metadata continue to use the vanilla
checkpoint spawn.

Requires Dusklight v2.0.3 or later.

## Quick start

1. Edit `mod.json.in` to set the mod ID (reverse-DNS style) and author.
2. Build locally:
   ```sh
   cmake -B build
   cmake --build build
   ```

The result is `build/mods/save_location_restore.dusk`. Copy it into the game's mods folder to try it:

- Windows: `%APPDATA%\TwilitRealm\Dusklight\mods`
- Linux: `~/.local/share/TwilitRealm/Dusklight/mods`
- macOS: `~/Library/Application Support/TwilitRealm/Dusklight/mods`

During development, rebuild, copy and click **Reload** in the in-game mod manager to pick up changes.
After making a save with the mod enabled, loading that slot restores the captured position and
facing. Older saves and saves made without the mod's metadata retain vanilla checkpoint behavior.

> [!IMPORTANT]
> A mod built locally will only be valid for your own platform, and shouldn't be distributed.
> The repository will build a [cross-platform bundle](#github-actions) for distribution. See below.

## Updating to a new Dusklight version

The `DUSKLIGHT_VERSION` line in `CMakeLists.txt` pins this mod to v2.0.3. To update it, change that
release tag (or use a commit hash) and reconfigure. The pinned version is fetched into `dusklight/`
automatically. Use the checkout to browse game code, headers, and mod services.

> [!IMPORTANT]
> The Dusklight checkout is for **reference only**. Mods use
> [services](https://github.com/TwilitRealm/dusklight/blob/main/docs/modding.md#built-in-services) and
> [hooks](https://github.com/TwilitRealm/dusklight/blob/main/docs/modding.md#hooking-game-functions) to interact with
> game code.

## GitHub Actions

The included GitHub Actions workflow builds the mod for the following platforms:
- Windows (AMD64 & ARM64)
- macOS (Apple Silicon & Intel)
- iOS (Apple Silicon)
- Linux (x86_64 & aarch64)
- Android (aarch64)

It then merges the per-platform builds into a single `.dusk` supporting all platforms. (Artifact `mod-combined`) 

Pushing a tag to the repository creates a GitHub release with the combined bundle.

## For Dusklight developers

Point the build at an existing checkout instead of fetching one:

```sh
cmake -B build -DDUSKLIGHT_DIR=~/path/to/dusklight
```
