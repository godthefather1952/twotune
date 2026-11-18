#!/usr/bin/env python3
"""Build the optimized Zeus UI assets and the generated C++ rig geometry.

Input : Assets/Zeus/source/ZeusRigV2   (verbatim Zeus Rig V2 handoff, kept for provenance)
Output: Assets/Zeus/ui/*.png           (cleaned, registered, cropped, optimized layers)
        Assets/Zeus/ui/rig.json        (geometry, human readable)
        Source/Plugin/ZeusRigData.h    (same geometry for the plugin; generated, do not edit)
        Docs/screenshots/rig_preview_sheet.png (with --preview)

Requires Pillow + NumPy. Run from the repo root:
    python3 tools/build_zeus_assets.py [--preview]

What it fixes (see Docs/DESIGN_V1_IMPLEMENTATION.md):
  * Animation jumps: every pose is shifted horizontally so the body (base layer, no active arm)
    keeps the same centre of mass. The feet baseline is already identical (y=600) in the source.
  * Stray fragments: small detached islands (e.g. the gold chip above pose 8's head) are removed.
  * Lightning clipping: pose 5's bolt runs off the top of the source canvas (tip y=-20) and has a
    dark ledge artefact, so pose 5 uses the complete pose 4 bolt, re-aimed by the plugin.
  * Shoulder gaps: base+arm reproduce complete_no_bolt exactly at native registration, so the
    arm is never rotated; only the independent bolt moves.
"""
import json
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image

REPO = Path(__file__).resolve().parents[1]
SRC = REPO / "Assets/Zeus/source/ZeusRigV2"
OUT = REPO / "Assets/Zeus/ui"
HEADER = REPO / "Source/Plugin/ZeusRigData.h"

# Pose whose bolt image is replaced (pose index is 1-based like the manifest).
BOLT_SOURCE_OVERRIDE = {5: 4}
# Manual retouch (source-canvas rectangles x0, y0, x1, y1 cleared to transparent), for remnants
# that are attached to the figure and so not caught by island removal:
#   pose 5: the old bolt's gold grip bar and white matte drips baked into the base layer above
#           the raised fist, and a dark smudge on top of the arm layer's matte.
MANUAL_CLEAR = {5: {"base": [(455, 170, 526, 226)], "arm": [(455, 170, 526, 189)]}}
# Detached islands smaller than this fraction of a layer's opaque area are removed.
ISLAND_FRACTION = 0.015
# Dial: 270-degree meter from 6 o'clock (0%) to 3 o'clock (100%).
# centreY = pose 9's fist height, so at 100% the bolt runs horizontally straight forward to the
# right and its tip meets the end of the meter at 3 o'clock (pose 9's art bolt is horizontal too).
# centreX/radius: the smallest meter that clears every pose's body (so the 0% tip at 6 o'clock is
# never hidden behind Zeus's legs) while leaving a usable bolt length at 100%.
DIAL_CENTRE_X = 615.0
DIAL_RADIUS = 450.0
# The bolt reaches the arc by scaling: up to this much uniformly (thicker when long), the rest
# along its own axis only, so the bolt never balloons or turns into a stub.
UNIFORM_SCALE_MIN, UNIFORM_SCALE_MAX = 0.8, 1.25
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


GRIP_FADE_BEHIND, GRIP_FADE_LENGTH = 2.0, 14.0


def fade_grip(rgba, frame):
    """The source bolts end in a blunt cut (with a dark ledge) at the grip. Fade the bolt out from
    just in front of the fist backwards, so the fist layer hides the end at any rotation."""
    ax, ay = frame["armAnchor"]["x"], frame["armAnchor"]["y"]
    tx, ty = frame["lightningTip"]["x"], frame["lightningTip"]["y"]
    length = math.hypot(tx - ax, ty - ay)
    ux, uy = (tx - ax) / length, (ty - ay) / length
    ys, xs = np.mgrid[0:rgba.shape[0], 0:rgba.shape[1]]
    along = (xs - ax) * ux + (ys - ay) * uy
    ramp = np.clip((along + GRIP_FADE_BEHIND) / GRIP_FADE_LENGTH, 0.0, 1.0)
    out = rgba.copy()
    out[:, :, 3] = (rgba[:, :, 3] * ramp).astype(np.uint8)
    return out


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
        report = {}
        base, arm = load(f["assetPaths"]["base"]), load(f["assetPaths"]["arm"])
        for layer_name, layer in (("base", base), ("arm", arm)):
            for x0, y0, x1, y1 in MANUAL_CLEAR.get(n, {}).get(layer_name, []):
                layer[y0:y1, x0:x1, 3] = 0
        figure = np.maximum(base[:, :, 3], arm[:, :, 3])
        base, rb = remove_islands(base, connectivity_alpha=figure)
        arm, ra = remove_islands(arm, connectivity_alpha=figure)
        src = frames[BOLT_SOURCE_OVERRIDE.get(n, n) - 1]
        bolt, rl = remove_islands(load(src["assetPaths"]["lightning"]), fraction=0.03)
        bolt = fade_grip(bolt, src)
        report.update(base_removed=rb, arm_removed=ra, bolt_removed=rl)

        base, arm = shift_x(base, dx), shift_x(arm, dx)
        b = save_crop(base, f"pose_{n:02d}_base.png")
        a = save_crop(arm, f"pose_{n:02d}_arm.png")
        l = save_crop(bolt, f"bolt_{n:02d}.png")  # bolt keeps its own source coordinates

        anchor = (f["armAnchor"]["x"] + dx, f["armAnchor"]["y"])
        boltAnchor = (src["armAnchor"]["x"] - l["x"], src["armAnchor"]["y"] - l["y"])
        boltTip = (src["lightningTip"]["x"] - l["x"], src["lightningTip"]["y"] - l["y"])
        poses.append(dict(frame=n, name=f["name"], intensity=f["intensity"], shiftX=dx,
                          fist=anchor, base=b, arm=a, bolt=l,
                          boltAnchor=boltAnchor, boltTip=boltTip,
                          boltSource=src["assetPaths"]["lightning"], cleanup=report))

    # Dial geometry (see DIAL_* above): centre height locked to pose 9's fist.
    cx, cy = DIAL_CENTRE_X, poses[8]["fist"][1]
    radius = DIAL_RADIUS
    dial = {"centreX": cx, "centreY": cy, "radius": radius,
            "uniformScaleMin": UNIFORM_SCALE_MIN, "uniformScaleMax": UNIFORM_SCALE_MAX}

    rig = {"generatedBy": "tools/build_zeus_assets.py", "source": "Assets/Zeus/source/ZeusRigV2",
           "canvas": manifest["frames"][0]["canvas"], "dial": dial, "poses": poses}
    (OUT / "rig.json").write_text(json.dumps(rig, indent=2) + "\n")
    write_header(rig)

    for p in poses:
        cleaned = {k: v for k, v in p["cleanup"].items() if v}
        print(f"pose {p['frame']}: shift {p['shiftX']:+d}px  fist {p['fist']}  cleanup {cleaned or '-'}")
    print(f"dial centre ({cx:.2f}, {cy:.2f}) radius {radius:.2f}")
    total = sum(f.stat().st_size for f in OUT.glob("*.png"))
    print(f"ui assets: {len(list(OUT.glob('*.png')))} files, {total / 1024:.0f} KiB")

    if "--preview" in sys.argv:
        preview(rig)


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
        "    float intensity;     // nominal intensity this pose represents",
        "    Point fist;          // where the bolt is gripped (canvas)",
        "    Rect base, arm, bolt;  // placement of each cropped layer (canvas); bolt is in source coords",
        "    Point boltAnchor;    // grip end inside the cropped bolt image",
        "    Point boltTip;       // tip inside the cropped bolt image",
        "};",
        "",
        f"constexpr float dialCentreX = {d['centreX']:.3f}f;",
        f"constexpr float dialCentreY = {d['centreY']:.3f}f;",
        f"constexpr float dialRadius = {d['radius']:.3f}f;",
        f"constexpr float boltUniformScaleMin = {d['uniformScaleMin']:.3f}f;",
        f"constexpr float boltUniformScaleMax = {d['uniformScaleMax']:.3f}f;",
        "",
        "constexpr std::array<Pose, 9> poses {{",
    ]
    for p in rig["poses"]:
        r = lambda k: "{%d, %d, %d, %d}" % (p[k]["x"], p[k]["y"], p[k]["w"], p[k]["h"])
        lines.append("    { %.1ff, {%.2ff, %.2ff}, %s, %s, %s, {%.2ff, %.2ff}, {%.2ff, %.2ff} }, // %d %s"
                     % (p["intensity"], p["fist"][0], p["fist"][1], r("base"), r("arm"), r("bolt"),
                        p["boltAnchor"][0], p["boltAnchor"][1], p["boltTip"][0], p["boltTip"][1],
                        p["frame"], p["name"]))
    lines += ["}};", "", "} // namespace evolutionvox::ui::zeus", ""]
    HEADER.write_text("\n".join(lines))


# ---------------------------------------------------------------------------------------------
# Preview: the same maths the plugin uses (Source/Plugin/ZeusRig.h), for quick visual QA.
def pose_index(intensity, poses):
    best = 0
    for i, p in enumerate(poses):
        if abs(p["intensity"] - intensity) < abs(poses[best]["intensity"] - intensity):
            best = i
    return best


def bolt_matrix(p, tip):
    ax, ay = p["boltAnchor"]; tx, ty = p["boltTip"]
    fx, fy = p["fist"]
    src_len = math.hypot(tx - ax, ty - ay)
    dst_len = math.hypot(tip[0] - fx, tip[1] - fy)
    s = dst_len / src_len
    u = min(max(s, UNIFORM_SCALE_MIN), UNIFORM_SCALE_MAX)  # uniform part
    axial = s / u
    a0 = math.atan2(ty - ay, tx - ax)
    a1 = math.atan2(tip[1] - fy, tip[0] - fx)
    c0, s0, c1, s1 = math.cos(a0), math.sin(a0), math.cos(a1), math.sin(a1)
    # M = R(a1) * diag(u * axial, u) * R(-a0)
    sa, sb = u * axial, u
    m00 = c1 * sa * c0 + (-s1) * sb * (-s0)
    m01 = c1 * sa * s0 + (-s1) * sb * c0
    m10 = s1 * sa * c0 + c1 * sb * (-s0)
    m11 = s1 * sa * s0 + c1 * sb * c0
    return (m00, m01, m10, m11), (ax, ay), (fx, fy), s


def preview(rig):
    from PIL import ImageDraw
    shots = REPO / "Docs/screenshots"
    shots.mkdir(parents=True, exist_ok=True)
    d = rig["dial"]
    poses = rig["poses"]
    imgs = {}
    for p in poses:
        for k in ("base", "arm", "bolt"):
            imgs[(p["frame"], k)] = Image.open(OUT / p[k]["file"]).convert("RGBA")
    W, H = 1280, 900
    oy = 60
    cells = []
    for t in [0, 12, 25, 37, 50, 62, 75, 87, 100]:
        img = Image.new("RGBA", (W, H), (250, 244, 233, 255))
        dr = ImageDraw.Draw(img)
        th = math.radians(90 + 270 * t / 100)
        tip = (d["centreX"] + d["radius"] * math.cos(th), d["centreY"] + d["radius"] * math.sin(th))
        box = [d["centreX"] - d["radius"], d["centreY"] - d["radius"] + oy,
               d["centreX"] + d["radius"], d["centreY"] + d["radius"] + oy]
        dr.arc(box, 90, 360, fill=(220, 200, 160, 255), width=10)
        if t > 0:
            dr.arc(box, 90, 90 + 270 * t / 100, fill=(232, 178, 40, 255), width=10)
        p = poses[pose_index(t, poses)]
        def paste(k):
            r = p[k]; img.alpha_composite(imgs[(p["frame"], k)], (r["x"], r["y"] + oy))
        paste("base")
        (m00, m01, m10, m11), (ax, ay), (fx, fy), s = bolt_matrix(p, tip)
        det = m00 * m11 - m01 * m10
        i00, i01, i10, i11 = m11 / det, -m01 / det, -m10 / det, m00 / det
        # output (X,Y) -> source: src = a + Minv * ((X,Y) - fist)
        fyo = fy + oy
        coeffs = (i00, i01, ax - i00 * fx - i01 * fyo, i10, i11, ay - i10 * fx - i11 * fyo)
        boltImg = imgs[(p["frame"], "bolt")].transform((W, H), Image.AFFINE, coeffs, Image.BICUBIC)
        img.alpha_composite(boltImg)
        paste("arm")
        dr = ImageDraw.Draw(img)
        dr.ellipse([tip[0] - 5, tip[1] + oy - 5, tip[0] + 5, tip[1] + oy + 5], outline=(200, 0, 0, 255), width=2)
        dr.text((20, 20), f"intensity {t}%  pose {p['frame']}  stretch x{s:.2f}", fill=(60, 40, 20, 255))
        cells.append(img.convert("RGB").resize((W // 3, H // 3), Image.LANCZOS))
    sheet = Image.new("RGB", (W, H), (255, 255, 255))
    for k, c in enumerate(cells):
        sheet.paste(c, ((k % 3) * (W // 3), (k // 3) * (H // 3)))
    sheet.save(shots / "rig_preview_sheet.png", optimize=True)
    print("preview:", shots / "rig_preview_sheet.png")


if __name__ == "__main__":
    main()
