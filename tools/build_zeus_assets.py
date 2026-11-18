#!/usr/bin/env python3
"""Build the optimized Zeus UI assets and the generated C++ rig geometry.

Input : Assets/Zeus/source/ZeusRigV2   (verbatim Zeus Rig V2 handoff, kept for provenance)
Output: Assets/Zeus/ui/*.png           (cleaned, registered, cropped, optimized body/arm layers)
        Assets/Zeus/ui/rig.json        (geometry, human readable)
        Source/Plugin/ZeusRigData.h    (same geometry for the plugin; generated, do not edit)

Requires Pillow + NumPy. Run from the repo root:
    python3 tools/build_zeus_assets.py

The lightning bolt is NOT a bitmap: the plugin draws it as a vector path from the pose's fist to
the meter (Source/Plugin/ZeusRig.h), so it never stretches, clips or pixelates. The source rig's
independent lightning PNGs are kept in Assets/Zeus/source for provenance only.

What it fixes (see Docs/DESIGN_V1_IMPLEMENTATION.md):
  * Animation jumps: every pose is shifted horizontally so the body (base layer, no active arm)
    keeps the same centre of mass. The feet baseline is already identical (y=600) in the source.
  * Stray fragments: small detached islands (e.g. the gold chip above pose 8's head) are removed.
  * Old-bolt remnants baked into pose 5's body/arm layers above the fist are cleared.
  * The white matte halo around each active arm/fist (visible against the lightning) is removed.
  * Shoulder gaps: base+arm reproduce complete_no_bolt exactly at native registration, so the
    arm is never rotated.
"""
import json
from pathlib import Path

import numpy as np
from PIL import Image

REPO = Path(__file__).resolve().parents[1]
SRC = REPO / "Assets/Zeus/source/ZeusRigV2"
OUT = REPO / "Assets/Zeus/ui"
HEADER = REPO / "Source/Plugin/ZeusRigData.h"

# Manual retouch (source-canvas rectangles x0, y0, x1, y1 cleared to transparent), for remnants
# that are attached to the figure and so not caught by island removal:
#   pose 5: the old bolt's gold grip bar and white matte drips baked into the base layer above
#           the raised fist, and a dark smudge on top of the arm layer's matte.
MANUAL_CLEAR = {5: {"base": [(455, 170, 526, 226)], "arm": [(455, 170, 526, 189)]}}
# Detached islands smaller than this fraction of a layer's opaque area are removed.
ISLAND_FRACTION = 0.015
# Dial: 270-degree meter from 6 o'clock (0%) to 3 o'clock (100%).
# centreY = pose 9's fist height, so at 100% the bolt runs horizontally straight forward to the
# right and its tip meets the end of the meter at 3 o'clock.
# centreX/radius: the smallest meter that clears every pose's body (so the 0% tip at 6 o'clock is
# never hidden behind Zeus's legs) while leaving a usable bolt length at 100%.
DIAL_CENTRE_X = 615.0
DIAL_RADIUS = 450.0
PAD = 2


def load(rel):
    return np.asarray(Image.open(SRC / rel).convert("RGBA")).copy()


def remove_islands(rgba, fraction=ISLAND_FRACTION, connectivity_alpha=None):
    """Keep the connected opaque region(s) that matter; drop small detached islands.
    connectivity_alpha: optional alpha of the whole figure (base+arm). Islands are found on that
    union, so a piece of the base that touches the arm layer is never mistaken for a stray."""
    own = rgba[:, :, 3] > 24
    mask = own if connectivity_alpha is None else (connectivity_alpha > 24) | own
    total = int(mask.sum())
    if total == 0:
        return rgba, []
    remaining = mask.copy()
    removed = []
    keep = np.zeros_like(mask)
    while remaining.any():
        ys, xs = np.nonzero(remaining)
        seed = np.zeros_like(mask)
        seed[ys[0], xs[0]] = True
        region = seed
        while True:  # 8-connected flood fill by repeated dilation inside the mask
            grown = region.copy()
            grown[1:] |= region[:-1]; grown[:-1] |= region[1:]
            grown[:, 1:] |= grown[:, :-1]; grown[:, :-1] |= grown[:, 1:]
            grown &= remaining
            if (grown == region).all():
                break
            region = grown
        remaining &= ~region
        area = int(region.sum())
        if area >= fraction * total:
            keep |= region
        elif (region & own).any():
            ry, rx = np.nonzero(region & own)
            removed.append((int((region & own).sum()), int(rx.min()), int(ry.min()),
                            int(rx.max()), int(ry.max())))
    # Remove the islands together with their faint anti-aliased fringe.
    drop = mask & ~keep
    if drop.any():
        fringe = drop.copy()
        for _ in range(2):
            f = fringe.copy()
            f[1:] |= fringe[:-1]; f[:-1] |= fringe[1:]
            f[:, 1:] |= fringe[:, :-1]; f[:, :-1] |= fringe[:, 1:]
            fringe = f
        fringe &= ~keep
        rgba[fringe, 3] = 0
    return rgba, removed


def defringe_arm(arm, figure_alpha, band=4):
    """Remove the source art's white matte halo from the active-arm layer. It is most visible where
    the gold lightning passes behind the fist. Only near-white pixels within `band` px of the
    OUTSIDE of the whole figure (base + arm) are cleared, never along the arm/body seam, so no
    shoulder gap can appear. The base layer is left alone (Zeus's hair and beard are white)."""
    outside = figure_alpha <= 24
    near = outside.copy()
    for _ in range(band):
        g = near.copy()
        g[1:] |= near[:-1]; g[:-1] |= near[1:]; g[:, 1:] |= near[:, :-1]; g[:, :-1] |= near[:, 1:]
        near = g
    rgb = arm[:, :, :3].astype(np.int32)
    whitish = (rgb.min(axis=2) > 205) & ((rgb.max(axis=2) - rgb.min(axis=2)) < 38)
    kill = near & ~outside & whitish & (arm[:, :, 3] > 0)
    arm[kill, 3] = 0
    return arm, int(kill.sum())


def shift_x(rgba, dx):
    out = np.zeros_like(rgba)
    w = rgba.shape[1]
    if dx >= 0:
        out[:, dx:] = rgba[:, : w - dx]
    else:
        out[:, : w + dx] = rgba[:, -dx:]
    return out


def bbox(rgba):
    ys, xs = np.nonzero(rgba[:, :, 3] > 0)
    x0, y0 = max(int(xs.min()) - PAD, 0), max(int(ys.min()) - PAD, 0)
    x1, y1 = min(int(xs.max()) + PAD + 1, rgba.shape[1]), min(int(ys.max()) + PAD + 1, rgba.shape[0])
    return x0, y0, x1, y1


def save_crop(rgba, name):
    x0, y0, x1, y1 = bbox(rgba)
    crop = rgba[y0:y1, x0:x1].copy()
    crop[crop[:, :, 3] == 0, :3] = 0  # zero hidden colour so PNG compresses well
    Image.fromarray(crop, "RGBA").save(OUT / name, optimize=True)
    return {"file": name, "x": x0, "y": y0, "w": x1 - x0, "h": y1 - y0}


def main():
    manifest = json.loads((SRC / "manifest.json").read_text())
    frames = manifest["frames"]
    OUT.mkdir(parents=True, exist_ok=True)
    for stale in OUT.glob("bolt_*.png"):   # bitmap bolts are no longer used
        stale.unlink()

    # Registration shifts (integer px, so the art is never resampled).
    centres = []
    for f in frames:
        a = load(f["assetPaths"]["base"])[:, :, 3].astype(np.float64)
        centres.append(float((a.sum(0) * np.arange(a.shape[1])).sum() / a.sum()))
    target = float(np.median(centres))
    shifts = [int(round(target - c)) for c in centres]

    poses = []
    for i, f in enumerate(frames):
        n = f["frame"]
        dx = shifts[i]
        base, arm = load(f["assetPaths"]["base"]), load(f["assetPaths"]["arm"])
        for layer_name, layer in (("base", base), ("arm", arm)):
            for x0, y0, x1, y1 in MANUAL_CLEAR.get(n, {}).get(layer_name, []):
                layer[y0:y1, x0:x1, 3] = 0
        figure = np.maximum(base[:, :, 3], arm[:, :, 3])
        base, rb = remove_islands(base, connectivity_alpha=figure)
        arm, ra = remove_islands(arm, connectivity_alpha=figure)
        arm, fringe = defringe_arm(arm, np.maximum(base[:, :, 3], arm[:, :, 3]))

        base, arm = shift_x(base, dx), shift_x(arm, dx)
        b = save_crop(base, f"pose_{n:02d}_base.png")
        a = save_crop(arm, f"pose_{n:02d}_arm.png")
        poses.append(dict(frame=n, name=f["name"], intensity=f["intensity"], shiftX=dx,
                          fist=(f["armAnchor"]["x"] + dx, f["armAnchor"]["y"]), base=b, arm=a,
                          cleanup={"base_removed": rb, "arm_removed": ra, "arm_matte_px": fringe}))

    # Dial geometry (see DIAL_* above): centre height locked to pose 9's fist.
    cx, cy = DIAL_CENTRE_X, poses[8]["fist"][1]
    dial = {"centreX": cx, "centreY": cy, "radius": DIAL_RADIUS}

    rig = {"generatedBy": "tools/build_zeus_assets.py", "source": "Assets/Zeus/source/ZeusRigV2",
           "canvas": manifest["frames"][0]["canvas"], "dial": dial, "poses": poses}
    (OUT / "rig.json").write_text(json.dumps(rig, indent=2) + "\n")
    write_header(rig)

    for p in poses:
        cleaned = {k: v for k, v in p["cleanup"].items() if v}
        print(f"pose {p['frame']}: shift {p['shiftX']:+d}px  fist {p['fist']}  cleanup {cleaned or '-'}")
    print(f"dial centre ({cx:.2f}, {cy:.2f}) radius {DIAL_RADIUS:.2f}")
    total = sum(f.stat().st_size for f in OUT.glob("*.png"))
    print(f"ui assets: {len(list(OUT.glob('*.png')))} files, {total / 1024:.0f} KiB")


def write_header(rig):
    d = rig["dial"]
    lines = [
        "// GENERATED by tools/build_zeus_assets.py from Assets/Zeus/source/ZeusRigV2. Do not edit.",
        "// Coordinates are in Zeus Rig V2 canvas pixels (1280x800 source canvas).",
        "#pragma once",
        "#include <array>",
        "",
        "namespace evolutionvox::ui::zeus {",
        "",
        "struct Rect { int x, y, w, h; };",
        "struct Point { float x, y; };",
        "struct Pose {",
        "    float intensity;  // nominal intensity this pose represents",
        "    Point fist;       // where the lightning is gripped (canvas)",
        "    Rect base, arm;   // placement of the cropped body / active-arm layers (canvas)",
        "};",
        "",
        f"constexpr float dialCentreX = {d['centreX']:.3f}f;",
        f"constexpr float dialCentreY = {d['centreY']:.3f}f;",
        f"constexpr float dialRadius = {d['radius']:.3f}f;",
        "",
        "constexpr std::array<Pose, 9> poses {{",
    ]
    for p in rig["poses"]:
        r = lambda k: "{%d, %d, %d, %d}" % (p[k]["x"], p[k]["y"], p[k]["w"], p[k]["h"])
        lines.append("    { %.1ff, {%.2ff, %.2ff}, %s, %s }, // %d %s"
                     % (p["intensity"], p["fist"][0], p["fist"][1], r("base"), r("arm"), p["frame"], p["name"]))
    lines += ["}};", "", "} // namespace evolutionvox::ui::zeus", ""]
    HEADER.write_text("\n".join(lines))


if __name__ == "__main__":
    main()
