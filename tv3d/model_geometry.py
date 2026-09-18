"""Manifold mechanical parts; dimensions are model units, not pixel voxels."""
import math
import numpy as np
import trimesh


def contour(w, h, r, z, cx=0, cy=0, steps=8):
    r = min(r, w/2, h/2)
    points = []
    for x, y, angle in ((w/2-r, h/2-r, 0), (-w/2+r, h/2-r, 90),
                        (-w/2+r, -h/2+r, 180), (w/2-r, -h/2+r, 270)):
        for a in np.linspace(angle, angle+90, steps+1):
            a = math.radians(a)
            point = [cx+x+r*math.cos(a), cy+y+r*math.sin(a), z]
            if not points or np.linalg.norm(np.array(point)-points[-1]) > 1e-8:
                points.append(point)
    if np.linalg.norm(np.array(points[0])-points[-1]) < 1e-8:
        points.pop()
    return np.array(points)


def clean(mesh):
    mesh.merge_vertices()
    mesh.update_faces(mesh.nondegenerate_faces())
    mesh.remove_unreferenced_vertices()
    mesh.fix_normals()
    return mesh


def loft(rings, cap=True):
    n = len(rings[0])
    if any(len(r) != n for r in rings):
        raise ValueError("Loft contours must have matching vertex counts")
    vertices = np.vstack(rings)
    faces = []
    for k in range(len(rings)-1):
        for i in range(n):
            a, b = k*n+i, k*n+(i+1)%n
            faces.extend(((a, b, b+n), (a, b+n, a+n)))
    if cap:
        vertices = np.vstack((vertices, np.mean(rings[0], axis=0), np.mean(rings[-1], axis=0)))
        for i in range(n):
            faces.extend(((len(vertices)-2, (i+1)%n, i),
                          (len(vertices)-1, (len(rings)-1)*n+i, (len(rings)-1)*n+(i+1)%n)))
    return clean(trimesh.Trimesh(vertices=vertices, faces=faces, process=False))


def rounded(w, h, depth, radius, bevel=.2, center=(0, 0, 0)):
    bevel = min(bevel, depth*.45, radius*.6)
    steps = 12 if max(w, h) > 30 else 6
    rings = [contour(w-2*bevel, h-2*bevel, radius-bevel, -depth/2, steps=steps),
             contour(w, h, radius, -depth/2+bevel, steps=steps),
             contour(w, h, radius, depth/2-bevel, steps=steps),
             contour(w-2*bevel, h-2*bevel, radius-bevel, depth/2, steps=steps)]
    m = loft(rings)
    m.apply_translation(center)
    return m


def subtract(mesh, cutters):
    result = trimesh.boolean.difference([mesh, *cutters], engine="manifold", check_volume=True)
    if result.is_empty or not result.is_volume:
        raise ValueError("Boolean produced an invalid solid")
    return clean(result)


def ring(w, h, radius, border, depth, center=(0, 0, 0), bevel=.18):
    return subtract(rounded(w, h, depth, radius, bevel, center),
                    [rounded(w-2*border, h-2*border, depth+2, max(.15, radius-border),
                             0, center)])


def dial(profile, center=(0, 0, 0), sections=64, ribs=0, amplitude=0):
    """Lathe with optional shallow flutes, all joined into one closed surface."""
    angles = np.arange(sections)*2*math.pi/sections
    rings = []
    for radius, z in profile:
        r = radius + amplitude*(.5+.5*np.cos(angles*ribs)) if ribs else radius+angles*0
        rings.append(np.column_stack((r*np.cos(angles), r*np.sin(angles), angles*0+z)))
    mesh = loft(rings)
    mesh.apply_translation(center)
    return mesh


def orient(mesh, center, normal):
    m = mesh.copy()
    # Fix roll explicitly: align_vectors alone may swap width and height for Y normals.
    z = np.array(normal, dtype=float)
    z /= np.linalg.norm(z)
    x = np.array([1., 0, 0]) if abs(z[1]) > .99 else np.cross([0, 1, 0], z)
    x /= np.linalg.norm(x)
    y = np.cross(z, x)
    transform = np.eye(4)
    transform[:3, :3] = np.column_stack((x, y, z))
    m.apply_transform(transform)
    m.apply_translation(center)
    return m


def perforated(w, h, depth, radius, slots, bevel=.2):
    blank = rounded(w, h, depth, radius, bevel)
    cutters = [rounded(sw, sh, depth+2, sr, 0, (x, y, 0)) for x, y, sw, sh, sr in slots]
    return subtract(blank, cutters)
