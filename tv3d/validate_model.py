"""Validate the desktop-pet assembly and export contract after rebuilding."""
import json
from pathlib import Path

import numpy as np
from PIL import Image
import trimesh

ROOT = Path(__file__).resolve().parent
manifest = json.loads((ROOT / "model.json").read_text())
scene = trimesh.load(ROOT / manifest["model"], force="scene", process=False)
surfaces = {"screen-display", "channel-cap", "volume-cap", "service-label"}
problems = []


def check(condition, message):
    if not condition:
        problems.append(message)


for name, mesh in scene.geometry.items():
    check(np.isfinite(mesh.vertices).all(), f"{name}: non-finite positions")
    check((mesh.area_faces > 1e-9).all(), f"{name}: degenerate triangles")
    welded = mesh.copy()
    welded.merge_vertices(merge_norm=True, merge_tex=True)
    if name not in surfaces:
        check(welded.is_volume, f"{name}: solid is open or has inconsistent normals")
    if mesh.visual.kind == "texture" and mesh.visual.uv is not None:
        check(np.isfinite(mesh.visual.uv).all(), f"{name}: invalid UVs")

check(abs(scene.bounds[0, 1]) < 1e-5, "Foot soles must lie on Y=0")
check(np.allclose(scene.bounds, manifest["bounds"], atol=.001), "Manifest bounds are stale")
check(len(scene.geometry) == manifest["meshCount"], "Mesh count mismatch")
check(sum(len(m.faces) for m in scene.geometry.values()) == manifest["triangleCount"], "Triangle count mismatch")
check(scene.extents.max() < 110, "Stray or parked geometry is present")
for name, pivot in manifest["pivots"].items():
    transform, _ = scene.graph.get(name, pivot["parent"])
    check(np.allclose(transform[:3, 3], pivot["position"]), f"{name}: incorrect pivot")
for name, path in manifest["expressions"].items():
    check(Image.open(ROOT / path).size == tuple(manifest["screenSize"]), f"{name}: incorrect texture dimensions")
for name in ("speaker-perforated-grille", "side-vent-louvres-1", "back-service-cover"):
    mesh = scene.geometry[name].copy()
    mesh.merge_vertices(merge_norm=True)
    check(mesh.euler_number < 0, f"{name}: expected through-holes are missing")

if manifest["version"] >= 5:
    glass = scene.geometry["screen-display"].bounds
    check(glass[1, 0]-glass[0, 0] > 83, "Modern screen must span the front housing")
    check(abs(glass[:, 0].mean()) < .001, "Display must be horizontally centered")
    check(abs(glass[1, 2]-glass[0, 2]) < .001, "Modern glass must be planar")
    controls = ("speaker-perforated-grille", "status-lamp-lens", "channel-grip", "volume-grip", "power-rocker")
    for name in controls:
        bounds = scene.geometry[name].bounds
        check(bounds[1, 1] < glass[0, 1] - 1, f"{name}: component overlaps the display")
    for left, right in zip(controls, controls[1:]):
        check(scene.geometry[left].bounds[1, 0]+1 < scene.geometry[right].bounds[0, 0],
              f"{left}/{right}: control rail spacing is too narrow")

if problems:
    raise SystemExit("\n".join(problems))
print(f"PASS: {len(scene.geometry)} meshes, {manifest['triangleCount']:,} triangles; solids, holes, UVs, ground origin, pivots and textures")
