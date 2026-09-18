"""Pack GLB geometry and textures for the Windows D3D11 renderer."""
from pathlib import Path
import io
import json
import struct
import numpy as np
from PIL import Image
import trimesh

root = Path(__file__).resolve().parents[2]
data = (root/'tv3d/tv_bot.glb').read_bytes()
length = struct.unpack_from('<I', data, 12)[0]
tree = json.loads(data[20:20+length])
binary = data[28+length:]
scene = trimesh.load(root/'tv3d/tv_bot.glb', force='scene', process=False)

def accessor(index):
    a = tree['accessors'][index]
    b = tree['bufferViews'][a['bufferView']]
    dtype = {5121:'u1',5123:'<u2',5125:'<u4',5126:'<f4'}[a['componentType']]
    width = {'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4}[a['type']]
    out = np.frombuffer(binary, dtype=dtype, count=a['count']*width,
                        offset=b.get('byteOffset',0)+a.get('byteOffset',0)).reshape(-1,width)
    return out.astype(float)/255 if a.get('normalized') and dtype=='u1' else out

atlas = Image.new('RGBA',(1024,1024),'white')
rects = []
for i, image in enumerate(tree['images']):
    b = tree['bufferViews'][image['bufferView']]
    im = Image.open(io.BytesIO(binary[b.get('byteOffset',0):b.get('byteOffset',0)+b['byteLength']])).convert('RGBA')
    x,y=(i%4)*256,(i//4)*256
    atlas.paste(im,(x,y));rects.append((x,y,im.width,im.height))
states=['idle','happy','love','surprise','sleep','blink','off','computer','computer-0','computer-1','computer-2','computer-3']
face_size=Image.open(root/f'tv3d/textures/screen-{states[0]}.png').size
faces=Image.new('RGBA',(face_size[0]*12,face_size[1]))
for i,state in enumerate(states):faces.paste(Image.open(root/f'tv3d/textures/screen-{state}.png'),(face_size[0]*i,0))
vertices=[]
for node in tree['nodes']:
    if 'mesh' not in node:continue
    matrix,_=scene.graph.get(node['name'])
    for primitive in tree['meshes'][node['mesh']]['primitives']:
        attrs=primitive['attributes'];positions=trimesh.transform_points(accessor(attrs['POSITION']),matrix)
        colors=accessor(attrs['COLOR_0']) if 'COLOR_0' in attrs else np.ones((len(positions),4))
        colors=np.where(colors[:,:3]<=.0031308,colors[:,:3]*12.92,1.055*colors[:,:3]**(1/2.4)-.055)
        uv=accessor(attrs['TEXCOORD_0']) if 'TEXCOORD_0' in attrs else np.zeros((len(positions),2))
        mat=tree['materials'][primitive['material']].get('pbrMetallicRoughness',{})
        texture=mat.get('baseColorTexture')
        kind=1 if node['name']=='screen-display' else 2 if node['name']=='power-rocker' else 0
        if node['name'] in ('front-housing','rear-cover','control-rail','back-service-cover'):
            kind=3
        elif any(part in node['name'] for part in ('fastener','-grip','-collar','-rod-','port-rim','screen-outer-lip','machined-trim','-face-ring')) and not node['name'].endswith('-slots'):
            kind=4
        if texture and kind!=1:
            x,y,w,h=rects[tree['textures'][texture['index']]['source']]
            uv=np.column_stack(((x+.5+uv[:,0]*(w-1))/1024,(y+.5+uv[:,1]*(h-1))/1024))
        elif kind!=1:uv=np.full_like(uv,.999)
        if node['name'].startswith('antenna-') and any(part in node['name'] for part in ('-rod-', '-collar', '-tip')):
            kind += 10 if node['name'].startswith('antenna--1-') else 20
        for idx in accessor(primitive['indices']).ravel():
            vertices.append((*positions[idx],*uv[idx],*colors[idx],float(kind)))
packed=np.asarray(vertices,dtype='<f4')
(root/'native/assets/realtime-model.bin').write_bytes(struct.pack('<I',len(packed))+packed.tobytes())
atlas.save(root/'native/assets/realtime-atlas.png');faces.save(root/'native/assets/realtime-faces.png')
print(f'Packed {len(packed)//3} triangles, {len(packed)*36:,} vertex bytes')
