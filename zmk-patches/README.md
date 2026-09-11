# ZMK core patches

`config/west.yml` points at a fork of `splitkb/zmk` rather than splitkb's own branch,
because the `hlc_display_epaper_status` shield needs a feature upstream does not have:
a way for the central to send the active layer, the peripheral battery levels, the
selected endpoint, and WPM *down* to a peripheral. Upstream's only central-to-peripheral
payload is the HID indicator state.

The patches here are the source of truth for that fork. `.zmk/` is gitignored and
west-managed, so anything applied there directly is lost on the next `west update`.

## Recreating the fork branch

```sh
git clone https://github.com/splitkb/zmk
cd zmk
git checkout -b halcyon-split-status origin/main+halcyon-fixes
git am /path/to/zmk-patches/*.patch
git push <your-fork> halcyon-split-status
```

Then make sure `remotes:` and `revision:` in `config/west.yml` match where you pushed.

## Rebasing onto a newer splitkb branch

```sh
git fetch origin
git rebase origin/main+halcyon-fixes
git push --force-with-lease <your-fork> halcyon-split-status
```

Afterwards regenerate the patches so this directory keeps matching the branch:

```sh
git format-patch origin/main+halcyon-fixes -o /path/to/zmk-patches/
```

Every change is behind `CONFIG_ZMK_SPLIT_PERIPHERAL_STATUS_REPORTING`, which keeps the
conflict surface small.
