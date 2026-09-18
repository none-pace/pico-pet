"""Export the actual GLB display surface for the native terminal projector."""
from pathlib import Path
import trimesh

root = Path(__file__).resolve().parents[2]
scene = trimesh.load(root / 'tv3d/tv_bot.glb', force='scene', process=False)
transform, name = scene.graph.get('screen-display')
mesh = scene.geometry[name]
vertices = trimesh.transform_points(mesh.vertices, transform)
lines = ['#pragma once', 'struct ScreenVertex { double x,y,z,u,v; };',
         'inline constexpr ScreenVertex kScreenVertices[] = {']
for point, uv in zip(vertices, mesh.visual.uv):
    lines.append('    {' + ','.join(f'{float(v):.9f}' for v in (*point, *uv)) + '},')
lines += ['};', 'inline constexpr int kScreenTriangles[][3] = {']
lines += ['    {' + ','.join(str(int(v)) for v in face) + '},' for face in mesh.faces]
lines += ['};']
(root / 'native/assets/screen-mesh.h').write_text('\n'.join(lines)+'\n', encoding='ascii')
print(f'Exported {len(vertices)} screen vertices, {len(mesh.faces)} triangles')
