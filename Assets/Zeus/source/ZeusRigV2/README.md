# EvolutionVox Zeus Layer Pack V2 — reconstructed layers

Nine frames, each 1280×800 transparent PNGs, no percent markings.

**Layer order:** ivory interface → character base without active arm → independent lightning → active arm.

* `characters/complete_no_bolt` — regenerated Zeus with both hands but no lightning.
* `characters/base_without_active_arm` — active arm split off.
* `characters/active_arm` — arm layer matching the pose.
* `lightning/independent` — separate full-length gold lightning bolts, including reconstructed final tip.
* `composites` — frame-by-frame preview of composited sprites.
* `manifest.json` — anchor, tip and canvas coordinates.

**Quality note:** This resolves lightning independence and exposes separate arm layers across the nine poses. It does **not** create a continuous 3D arm rig: the arm is a frame-based cutout, and under-arm anatomy is not reconstructed for arbitrary continuous rotation. Pose transitions work best by switching sprites and aligning each bolt to its pose. The generated no-bolt body poses differ slightly from the previously approved illustration and should receive visual approval. For precise lightning-to-dial arc matching, the dial geometry must use the manifest tip points or be re-registered per pose.
