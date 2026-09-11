# Agent notes: building firmware locally with Docker

This repository can build its own firmware locally, without pushing to GitHub Actions.
The Docker setup mirrors the CI workflow
(`zmkfirmware/zmk/.github/workflows/build-user-config.yml@main`, referenced from
`.github/workflows/build.yml`) closely enough that a local artifact should match the one
CI produces for the same commit.

## Quick start

```bash
direnv allow           # once per clone; exports UID/GID from .envrc
docker compose build   # ~3 minutes, sets up the west workspace inside the image
docker compose run --rm build
```

Artifacts land in `firmware/` on the host, owned by the invoking user. The directory is
gitignored.

Build a single target by passing a substring filter, matched against
`"<shield> <board>"`:

```bash
docker compose run --rm build kyria_right
```

Repeat builds are incremental — a full rebuild of one target takes a few seconds.

## How it is put together

| File | Role |
| :--- | :--- |
| `Dockerfile` | `FROM zmkfirmware/zmk-build-arm:stable`, the same image CI uses. Runs `west init -l` / `west update` / `west zephyr-export` at *image build* time into `/west`. |
| `docker-compose.yml` | Service `build`. Mounts the repo at `/workspace`, `./firmware` at `/workspace/firmware`, and the named volume `zmk-build` at `/west/build`. |
| `docker/build-firmware.sh` | Entrypoint. Re-syncs config, walks `build.yaml`, compiles each row, copies artifacts out. |
| `.envrc` | Exports `UID`/`GID` so the container can chown artifacts back to the host user. |
| `.dockerignore` | Build context is only `config/west.yml` and the entrypoint script. |

Design points worth knowing before changing any of this:

- **The west workspace lives in the image, not in a volume.** Everything repeatable
  (fetching Zephyr, ZMK, modules; `west zephyr-export`) happens in `docker compose build`,
  so `docker compose run build` needs no network and starts compiling immediately.
- **`zephyr/module.yml` exists at the repo root**, so this repository is itself a Zephyr
  module. CI handles that by building from a workspace *outside* the checkout and passing
  the checkout as `-DZMK_EXTRA_MODULES`. The local build does the same: workspace at
  `/west`, repo at `/workspace`.
- **`config/` is re-copied into `/west/config` on every run.** Without this, keymap and
  `.conf` edits on the host would silently not reach the build. `west.yml` is preserved
  because the west manifest path points at it.
- **Per-target build directories** live at `/west/build/<hash>`, where the hash covers
  board, shield, snippet and cmake-args. That keeps rebuilds incremental while making a
  stale CMake cache impossible.
- **The container runs as root.** Do not add `user:` to the compose service — the Zephyr
  SDK paths and the `~/.cmake` package registry written by `west zephyr-export` assume
  root. The entrypoint chowns `firmware/` at the end instead.
- **No fail-fast**, matching CI: a failing target does not stop the others, and the run
  exits nonzero with a summary listing what failed.
- **Artifact names match CI exactly**, including spaces from multi-shield entries, e.g.
  `halcyon_kyria_left mod_battery_lipo hlc_display_epaper_status-halcyon_wireless__zmk-zmk.uf2`.
  (`${board//\//_}` replaces both slashes of `halcyon_wireless//zmk`, hence the double
  underscore.)

## Refreshing the toolchain and modules

`config/west.yml` pins `zmk` to the branch `halcyon-split-status`, which moves. A plain
`docker compose build` reuses the cached west layer when `west.yml` itself has not
changed, so force a genuine refresh with the `REFRESH` build arg:

```bash
docker compose build --build-arg REFRESH=$(date +%s)
```

Changing `config/west.yml` (adding a module, repointing a revision) invalidates that layer
on its own, so a plain `docker compose build` is enough in that case.

## Troubleshooting

- *"no west workspace at /west"* — the image is missing or something was mounted over
  `/west`. Run `docker compose build`.
- Suspected stale CMake cache: `docker volume rm zmk-halcyon-config_zmk-build`, then
  rebuild.
- `firmware/` files owned by root: `.envrc` was not loaded. Run `direnv allow`, or pass
  `UID`/`GID` explicitly in the environment.
- Do **not** repurpose the `.zmk/` directory for builds. It is a west workspace used for
  code indexing and its `.west/config` filters out Zephyr, so it cannot compile anything.

## Related files

- `build.yaml` — the build matrix; both CI and the local build read it.
- `README.md` — user-facing documentation, including the upstream local-build instructions
  that do not use Docker.
