"""Modern LCD geometry with hand-drawn, nearest-filtered pixel textures.

Run from any directory. Coordinates: +Y up, +Z front; original model units.
"""
from pathlib import Path
import json
import math
import numpy as np
from PIL import Image, ImageDraw
import trimesh
from model_geometry import contour, rounded, ring, dial, orient, perforated, subtract

OUT = Path(__file__).resolve().parent
TEX = OUT / "textures"
C = dict(ink="#263536", shell="#d4ded5", light="#edf3df", mid="#a5b9ae",
         shade="#7c958b", deep="#526e66", glass="#142c2c", scan="#183231",
         mint="#8cf5ba", white="#dbffe0", green="#52b78e", red="#ef807e",
         metal="#9fb6b3", yellow="#efcd77")
FONT = {
    "0": "111 101 101 101 111", "1": "010 110 010 010 111",
    "5": "111 100 110 001 110", "A": "010 101 111 101 101",
    "C": "011 100 100 100 011", "D": "110 101 101 101 110",
    "H": "101 101 111 101 101", "I": "111 010 010 010 111",
    "L": "100 100 100 100 111", "O": "010 101 101 101 010",
    "P": "110 101 110 100 100", "R": "110 101 110 101 101",
    "T": "111 010 010 010 010", "V": "101 101 101 101 010",
    "-": "000 000 111 000 000", " ": "000 000 000 000 000",
}


def label(draw, xy, text, color):
    x, y = xy
    for char in text:
        for j, row in enumerate(FONT[char].split()):
            for i, bit in enumerate(row):
                if bit == "1":
                    draw.point((x+i, y+j), fill=color)
        x += 4


def glass_finish(face_image, expression):
    """Upscale the LCD face without CRT raster or glow."""
    enlarged = face_image.resize((face_image.width*2, face_image.height*2), Image.Resampling.NEAREST)
    pixels = np.array(enlarged).astype(float)
    if expression == "off":
        pixels *= .64
    return Image.fromarray(np.clip(pixels, 0, 255).astype(np.uint8))


def paint(mesh, name, color=None, texture=None, uv=None):
    if texture is not None:
        mat = trimesh.visual.material.PBRMaterial(name=name, baseColorTexture=texture,
            baseColorFactor=[255, 255, 255, 255], metallicFactor=0, roughnessFactor=1, doubleSided=False)
        mesh.visual = trimesh.visual.TextureVisuals(uv=uv, material=mat)
    else:
        # Split color boundaries so glTF cannot interpolate triangular streaks on panels.
        base = np.array(trimesh.visual.color.hex_to_rgba(color), dtype=float)
        colors = []
        for n in mesh.face_normals:
            strength = 1.08 if n[1] > .45 else (.73 if n[1] < -.45 else (.86 if abs(n[0]) > .5 else 1))
            rgba = base.copy()
            srgb = np.clip(rgba[:3]*strength/255, 0, 1)
            rgba[:3] = np.round(np.where(srgb <= .04045, srgb/12.92, ((srgb+.055)/1.055)**2.4)*255)
            colors.append(rgba)
        mesh.unmerge_vertices()
        mesh.visual.vertex_colors = np.repeat(np.array(colors, dtype=np.uint8), 3, axis=0)
        # Merge coplanar, same-color vertices; retain hard normals at material boundaries.
        _ = mesh.vertex_normals
        mesh.merge_vertices(merge_norm=False, digits_norm=7)
    return mesh


def face(w, h, r, z, texture, name, cx=0, cy=0, back=False):
    ring = contour(w, h, r, z, cx, cy)
    vertices = np.vstack((ring, [cx, cy, z]))
    faces = [(len(ring), i, (i+1)%len(ring)) for i in range(len(ring))]
    if back:
        faces = [f[::-1] for f in faces]
    uv = np.column_stack(((vertices[:, 0]-cx)/w+.5, (vertices[:, 1]-cy)/h+.5))
    if back:
        uv[:, 0] = 1-uv[:, 0]
    return paint(trimesh.Trimesh(vertices=vertices, faces=faces, process=False), name, texture=texture, uv=uv)


def modern_screen_texture(expression):
    im = Image.new("RGB", (84, 48), C["glass"])
    d = ImageDraw.Draw(im)
    if expression.startswith("computer"):
        selected = int(expression[-1]) if expression[-1].isdigit() else -1
        label(d, (7, 5), "PICO", C["mint"])
        d.line((7, 13, 76, 13), fill=C["green"])
        for index, x in enumerate((8, 26, 44, 62)):
            d.rectangle((x, 19, x+13, 33), fill="#244b42", outline=C["white"] if index==selected else C["green"])
            if index==selected:
                d.line((x+3, 37, x+10, 37), fill=C["mint"], width=2)
            if index==0:
                for a, h in ((3, 4), (6, 7), (9, 10)):
                    d.rectangle((x+a, 30-h, x+a+1, 30), fill=C["mint"])
            elif index==1:
                d.rectangle((x+3, 22, x+10, 30), outline=C["mint"])
                for y in (25, 28): d.line((x+4, y, x+9, y), fill=C["green"])
            elif index==2:
                d.line((x+3, 24, x+10, 24, x+7, 29, x+3, 24), fill=C["green"])
                for a, b in ((2, 23), (9, 23), (6, 28)):
                    d.rectangle((x+a, b, x+a+2, b+2), fill=C["mint"])
            else:
                for y in (23, 26, 29): d.line((x+3, y, x+10, y), fill=C["mint"])
    elif expression != "off":
        label(d, (7, 5), "PICO", C["green"])
        for x in (27, 51):
            if expression in ("sleep", "blink"):
                d.line((x, 25, x+5, 25), fill=C["mint"], width=2)
            elif expression=="happy":
                d.line((x, 25, x, 22, x+2, 20, x+4, 22, x+5, 25), fill=C["mint"], width=2)
            else:
                d.rounded_rectangle((x, 18, x+5, 26), 1, fill=C["red"] if expression=="love" else C["mint"])
                d.line((x+1, 19, x+1, 21), fill=C["white"])
        if expression=="surprise": d.rectangle((39, 30, 44, 35), outline=C["mint"])
        elif expression=="sleep": d.line((39, 33, 44, 33), fill=C["green"])
        else: d.line((37, 30, 37, 32, 39, 34, 44, 34, 46, 32, 46, 30), fill=C["mint"])
        for x in (23, 58): d.line((x, 29, x+3, 29), fill=C["red"])
    return glass_finish(im, expression)


def build():
    """Modern display assembly: shared dimensions, shallow glass and a separate control rail."""
    TEX.mkdir(exist_ok=True)
    states = ("idle", "happy", "sleep", "surprise", "love", "blink", "off",
              "computer", "computer-0", "computer-1", "computer-2", "computer-3")
    for state in states:
        modern_screen_texture(state).save(TEX / f"screen-{state}.png")
    scene = trimesh.Scene(base_frame="pet-root")
    scene.graph.update(frame_from="pet-root", frame_to="body",
                       matrix=trimesh.transformations.translation_matrix([0, 39.5, 0]))

    def add(name, mesh, color):
        scene.add_geometry(paint(mesh, name, C[color]), node_name=name, geom_name=name,
                           parent_node_name="body")

    def fasteners(name, positions, normal=(0, 0, 1), radius=.6):
        heads, slots = [], []
        for position in positions:
            head = dial([(radius, -.12), (radius, .10), (radius*.8, .23)], sections=20)
            head = subtract(head, [rounded(radius*1.1, radius*.22, .3, .04, 0, (0, 0, .23))])
            heads.append(orient(head, position, normal))
            slots.append(orient(rounded(radius*1.1, radius*.20, .025, .03, 0, (0, 0, .09)), position, normal))
        add(name, trimesh.util.concatenate(heads), "metal")
        add(name+"-slots", trimesh.util.concatenate(slots), "ink")

    # The housing has a real through-opening, with no solid front cap behind the display.
    aperture = rounded(85, 49, 50, 3.5, 0, (0, 3, 4))
    add("front-housing", subtract(rounded(94, 64, 13, 6, 1.2, (0, 0, 13)), [aperture]), "shell")
    add("cabinet-joint-gasket", ring(92.8, 62.8, 5.6, .65, .8, (0, 0, 6.4)), "deep")
    rear = rounded(92, 62, 22, 5.5, 1.6, (0, 0, -5.5))
    cutouts = [orient(rounded(12, 32, 3, 1.2, .1), (side*46, 0, -4), (side, 0, 0)) for side in (-1, 1)]
    cutouts.append(rounded(70, 41, 3, 2, .1, (0, 0, -16.5)))
    add("rear-cover", subtract(rear, cutouts), "mid")
    add("screen-outer-lip", ring(87.4, 51.4, 4.4, .65, .75, (0, 3, 19.65)), "metal")
    add("screen-machined-trim", ring(88.5, 52.5, 4.9, .22, .28, (0, 3, 19.57), .06), "light")
    add("screen-recess-bezel", ring(86.1, 50.1, 3.8, 1.05, 1.1, (0, 3, 19.65)), "ink")
    add("screen-rubber-seal", ring(84.1, 48.1, 2.8, .28, .3, (0, 3, 20.12), .04), "deep")
    glass = face(83.6, 47.6, 2.55, 20.15, modern_screen_texture("idle"), "screen-pixels", 0, 3)
    scene.add_geometry(glass, node_name="screen-display", geom_name="screen-display", parent_node_name="body")

    add("control-rail", rounded(85, 6, .7, 1.4, .15, (0, -26, 19.7)), "deep")
    slots = [(x, 0, 1, 2.5, .4) for x in range(-13, 14, 3)]
    grille = perforated(31, 4.4, .6, .8, slots, .1)
    grille.apply_translation([-22, -26, 20.2])
    add("speaker-acoustic-cavity", rounded(31, 4.4, .12, .8, .02, (-22, -26, 20.12)), "ink")
    add("speaker-perforated-grille", grille, "shade")
    add("status-lamp-surround", rounded(6, 2, .4, .7, .08, (0, -26, 20.2)), "ink")
    add("status-lamp-lens", rounded(4.8, .9, .35, .4, .07, (0, -26, 20.5)), "mint")
    for tag, x in (("channel", 14), ("volume", 23)):
        add(tag+"-mount", dial([(2.65, 19.9), (2.65, 20.3), (2.4, 20.5)], (x, -26, 0), sections=48), "ink")
        add(tag+"-grip", dial([(2.15, 20.35), (2.15, 21.2), (1.95, 21.45)], (x, -26, 0), sections=48, ribs=16, amplitude=.07), "metal")
        add(tag+"-indicator", rounded(.35, 1, .12, .1, .02, (x, -25.15, 21.5)), "light")
        ticks = [rounded(.22, .5, .08, .05, .01, (x+math.cos(a)*2.85, -26+math.sin(a)*2.85, 20.12))
                 for a in np.linspace(.45, math.pi-.45, 7)]
        add(tag+"-scale", trimesh.util.concatenate(ticks), "light")
        add(tag+"-face", dial([(1.6, 21.43), (1.6, 21.49)], (x, -26, 0), sections=48), "shade")
        add(tag+"-face-ring", ring(3.75, 3.75, 1.875, .15, .12, (x, -26, 21.46), .025), "light")
    add("power-switch-recess", rounded(7, 3.8, .5, 1, .1, (35, -26, 20.15)), "ink")
    add("power-rocker", rounded(5.3, 2.4, .85, .65, .15, (35, -26, 20.65)), "red")
    fasteners("fascia-fasteners", [(x, y, 19.56) for x in (-44.5, 44.5) for y in (-28.5, 28.5)], radius=.45)

    for side in (-1, 1):
        vent = perforated(14, 34, .6, 1.3, [(0, y, 10, 1.3, .5) for y in range(-12, 13, 4)], .1)
        add(f"side-vent-louvres-{side}", orient(vent, (side*46.15, 0, -4), (side, 0, 0)), "deep")
        add(f"side-vent-cavity-{side}", orient(rounded(12, 32, .2, 1, .04), (side*44.7, 0, -4), (side, 0, 0)), "ink")
        fasteners(f"side-vent-fasteners-{side}", [(side*46.5, y, -4) for y in (-15.5, 15.5)], (side, 0, 0), .45)
        for z in (-9, 12):
            add(f"foot-{side}-{z}", rounded(13, 7.5, 10, 2, .6, (side*30, -35.75, z)), "deep")
            add(f"foot-{side}-{z}-mount", rounded(12, 1.4, 9, 1.6, .25, (side*30, -32.1, z)), "shade")
        start, end = np.array([side*26, 35, 5]), np.array([side*32, 47, 5])
        add(f"antenna-{side}-base", rounded(8, 3, 6, 1, .3, (side*26, 32.7, 5)), "deep")
        add(f"antenna-{side}-hinge", orient(dial([(2.5, -1.3), (2.5, 1.3)], sections=32), start, (0, 0, 1)), "metal")
        fasteners(f"antenna-{side}-fastener", [(side*26, 35, 6.35)], radius=1)
        for i, (a, b, radius) in enumerate(((0, .55, 1.15), (.55, 1, .75))):
            add(f"antenna-{side}-rod-{i}", trimesh.creation.cylinder(radius=radius, segment=[start+(end-start)*a, start+(end-start)*b], sections=24), "metal" if i==0 else "light")
        add(f"antenna-{side}-collar", orient(dial([(1.3, -.4), (1.3, .3), (1, .5)], sections=24), start+(end-start)*.55, end-start), "deep")
        tip = trimesh.creation.icosphere(subdivisions=2, radius=2.1)
        tip.apply_translation(end)
        add(f"antenna-{side}-tip", tip, "red")
    add("carry-handle", ring(29, 8, 2.5, 1.5, 3, (0, 35, -5), .25), "deep")
    for x in (-13.5, 13.5):
        add(f"handle-mount-{x}", rounded(4, 2, 5, .5, .15, (x, 31.7, -5)), "shade")
    back = perforated(72, 43, .6, 3, [(x, y, 16, 1.4, .5) for x in (-22, 0, 22) for y in (4, 8, 12)], .1)
    add("back-service-cover", orient(back, (0, 0, -16.8), (0, 0, -1)), "deep")
    add("back-service-cavity", rounded(70, 41, .2, 2, .04, (0, 0, -15.1)), "ink")
    fasteners("back-fasteners", [(x, y, -17.15) for x in (-33, 33) for y in (-18, 18)], (0, 0, -1), .7)
    for name, x, width in (("power", 22, 8), ("usb", 9, 6)):
        add(name+"-port-well", orient(rounded(width, 3.4, .3, .6, .05), (x, -12, -17.2), (0, 0, -1)), "ink")
        add(name+"-port-rim", orient(ring(width+.8, 4.2, .9, .45, .6), (x, -12, -17.35), (0, 0, -1)), "metal")
        add(name+"-port-contact", orient(rounded(width-2, .5, .15, .15, .03), (x, -12, -17.4), (0, 0, -1)), "yellow")
    sticker = Image.new("RGB", (40, 16), C["shell"])
    label(ImageDraw.Draw(sticker), (3, 3), "PICO-01", C["deep"])
    label(ImageDraw.Draw(sticker), (3, 10), "DC 5V", C["deep"])
    scene.add_geometry(face(24, 9.6, .4, -17.2, sticker, "service-pixels", -16, -11, back=True),
                       node_name="service-label", geom_name="service-label", parent_node_name="body")

    def export_materials(tree):
        tree.setdefault("extensionsUsed", []).append("KHR_materials_unlit")
        fallback = len(tree.setdefault("materials", []))
        tree["materials"].append({"name": "shell-palette", "pbrMetallicRoughness": {"metallicFactor": 0, "roughnessFactor": 1}})
        for mesh in tree["meshes"]:
            for primitive in mesh["primitives"]:
                primitive.setdefault("material", fallback)
        for material in tree.get("materials", []):
            material.setdefault("extensions", {})["KHR_materials_unlit"] = {}
        tree["samplers"] = [{"magFilter": 9728, "minFilter": 9728, "wrapS": 33071, "wrapT": 33071}]
        for texture in tree.get("textures", []):
            texture["sampler"] = 0

    data = trimesh.exchange.gltf.export_glb(scene, include_normals=True, tree_postprocessor=export_materials)
    (OUT / "tv_bot.glb").write_bytes(data)
    manifest = {"model": "tv_bot.glb", "screenNode": "screen-display", "screenSize": [168, 96],
                "expressions": {e: f"textures/screen-{e}.png" for e in states},
                "bounds": scene.bounds.round(3).tolist(), "meshCount": len(scene.geometry),
                "triangleCount": sum(len(m.faces) for m in scene.geometry.values()),
                "version": 5, "origin": "ground-center", "upAxis": "+Y", "forwardAxis": "+Z",
                "previewOffsetY": -39.5, "pivots": {}}
    (OUT / "model.json").write_text(json.dumps(manifest, indent=2)+"\n", encoding="utf-8")
    print(f"Exported modern display: {manifest['meshCount']} meshes, {manifest['triangleCount']} triangles")


if __name__ == "__main__":
    build()
