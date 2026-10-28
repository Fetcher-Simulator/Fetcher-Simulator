"""Original low-poly Studio props; run with Blender 4.5 and io_scene_mw.

blender --background --factory-startup --disable-autoexec --python THIS --
  --output files/server/optional-assets/fetcher.procedural-synth --working build/orchestra-assets
No external models or game assets are used by this generator.
"""
import argparse, hashlib, json, math, shutil, subprocess, sys
from pathlib import Path
import bpy
from mathutils import Vector

parser=argparse.ArgumentParser()
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--working',type=Path,required=True)
parser.add_argument('--only',choices=['violin','cello','bow','trumpet','flute','clarinet','timpani','mallet'])
args=parser.parse_args(sys.argv[sys.argv.index('--')+1:])
if (args.output/'sketchfab-models.json').exists():
 raise RuntimeError('Legacy generator cannot overwrite the licensed Sketchfab defaults. Use a separate historical output folder.')
args.output=args.output.resolve();args.working=args.working.resolve()
models=args.output/'meshes/fetcher/orchestra';icons=args.output/'icons/fetcher/orchestra'
textures=args.output/'textures/fetcher/orchestra'
models.mkdir(parents=True,exist_ok=True);icons.mkdir(parents=True,exist_ok=True);textures.mkdir(parents=True,exist_ok=True);args.working.mkdir(parents=True,exist_ok=True)
magick=shutil.which('magick');assert magick,'ImageMagick is needed for the inventory icons'
bpy.ops.preferences.addon_enable(module='io_scene_mw')
bpy.context.preferences.addons['io_scene_mw'].preferences.scale_correction=1
from io_scene_mw import nif_shader
from es3 import nif

def material(name,color,shine=24,spec=.25):
 m=nif_shader.create_material(name).mw
 # TES3's material diffuse is not a substitute for an albedo texture in every
 # rendering path. Original tiny textures keep world/attachment colors equal.
 filename=name.lower().replace(' ','-')+'.dds';png=args.working/(filename+'.png')
 image=bpy.data.images.new(name,64,64,alpha=False)
 pixels=[]
 for y in range(64):
  for x in range(64):
   grain=1+.045*math.sin(x*.8+math.sin(y*.2)*2) if 'maple' in name or 'rosewood' in name else 1
   pixels.extend([min(1,c**(1/2.2)*grain) for c in color]+[1])
 image.pixels=pixels;image.filepath_raw=str(png);image.file_format='PNG';image.save()
 subprocess.run([magick,str(png),'-define','dds:compression=dxt1','-define','dds:mipmaps=6',str(textures/filename)],check=True)
 m.diffuse_color[:]=(1,1,1,1);m.ambient_color[:]=(1,1,1,1)
 m.base_texture.image=bpy.data.images.load(str(textures/filename),check_existing=True);m.base_texture.layer='UVMap'
 m.specular_color[:]=(spec,spec,spec,1);m.shine=shine
 return m.material
wood=material('Fetcher amber maple',(.34,.105,.028),36,.35)
edge=material('Fetcher dark rosewood',(.08,.022,.012))
black=material('Fetcher ebony',(.018,.014,.012),32,.3)
silver=material('Fetcher silver keys',(.65,.69,.72),80,.8)
brass=material('Fetcher golden brass',(.62,.39,.08),80,.8)
copper=material('Fetcher timpani copper',(.48,.19,.065),64,.65)
ivory=material('Fetcher vellum and bow hair',(.81,.76,.61),12,.08)
string=material('Fetcher steel strings',(.46,.47,.42),50,.55)
report=json.loads((args.output/'orchestra-report.json').read_text()) if args.only else {}

def mesh(name,vertices,faces,mat):
 data=bpy.data.meshes.new(name);data.from_pydata(vertices,[],faces);data.update()
 o=bpy.data.objects.new(name,data);bpy.context.collection.objects.link(o);o.data.materials.append(mat);return o
def sphere(name,p,scale,mat):
 bpy.ops.mesh.primitive_uv_sphere_add(segments=12,ring_count=6,location=p)
 o=bpy.context.object;o.name=name;o.scale=scale;o.data.materials.append(mat);return o
def rod(name,a,b,r,mat,vertices=10,r2=None):
 a,b=Vector(a),Vector(b);d=b-a
 bpy.ops.mesh.primitive_cone_add(vertices=vertices,radius1=r,radius2=r if r2 is None else r2,depth=d.length,location=(a+b)/2)
 o=bpy.context.object;o.name=name;o.rotation_euler=d.to_track_quat('Z','Y').to_euler();o.data.materials.append(mat);return o
def line(name,points,r,mat):
 curve=bpy.data.curves.new(name,'CURVE');curve.dimensions='3D';curve.resolution_u=1;curve.bevel_depth=r;curve.bevel_resolution=1
 spline=curve.splines.new('POLY');spline.points.add(len(points)-1)
 for p,point in zip(spline.points,points):p.co=(*point,1)
 o=bpy.data.objects.new(name,curve);bpy.context.collection.objects.link(o);o.data.materials.append(mat);return o
def box(name,p,size,mat):
 bpy.ops.mesh.primitive_cube_add(size=1,location=p);o=bpy.context.object;o.name=name;o.scale=size;o.data.materials.append(mat);return o
def rings(name,profile,mat,axis='Z',segments=24):
 vertices=[]
 for radius,height in profile:
  for n in range(segments):
   a=2*math.pi*n/segments;x,y=radius*math.cos(a),radius*math.sin(a)
   vertices.append((x,y,height) if axis=='Z' else (x,height,y))
 faces=[]
 for row in range(len(profile)-1):
  for n in range(segments):
   a=row*segments+n;b=row*segments+(n+1)%segments
   faces.append((a,a+segments,b+segments,b) if axis=='Y' else (a,b,b+segments,a+segments))
 return mesh(name,vertices,faces,mat)

def section_solid(name,sections,mat,scale=1):
 # Closed solid extruded along Y. Section points wind counter-clockwise in X/Z.
 count=len(sections[0][1]);vertices=[];faces=[]
 for y,points in sections:
  assert len(points)==count
  vertices.extend((x*scale,y*scale,z*scale) for x,z in points)
 for row in range(len(sections)-1):
  for i in range(count):faces.append((row*count+i,(row+1)*count+i,(row+1)*count+(i+1)%count,row*count+(i+1)%count))
 faces.append(tuple(range(count)));faces.append(tuple(reversed(range((len(sections)-1)*count,len(vertices)))))
 return mesh(name,vertices,faces,mat)

def bowed(scale=1):
 # One symmetric waist outline, layered into rib, arched top and back.
 half=[(0,-14),(4,-13),(7,-10),(7.5,-6),(5.2,-2),(3.8,0),(4.6,3),(6.5,6),(6,9),(3,12),(0,13)]
 outline=half+ [(-x,y) for x,y in reversed(half[1:-1])]
 vertices=[]
 for z,factor in [(-1.3,.95),(0,1),(1.6,1),(2.8,.65)]:
  vertices.extend([(x*factor*scale,y*scale,z*scale) for x,y in outline])
 n=len(outline);faces=[]
 for row in range(3):
  for i in range(n):faces.append((row*n+i,row*n+(i+1)%n,(row+1)*n+(i+1)%n,(row+1)*n+i))
 faces.append(tuple(reversed(range(n))));faces.append(tuple(3*n+i for i in range(n)))
 mesh('Arched maple body',vertices,faces,wood)
 def neck_section(width,bottom):return [(-width,bottom+.4),(-width*.6,bottom),(width*.6,bottom),(width,bottom+.4),(width,3),(-width,3)]
 section_solid('Maple neck and heel',[(11.5,neck_section(1.25,-1)),(16,neck_section(1.15,.4)),(26,neck_section(.95,1.1))],wood,scale)
 def board_section(width):return [(-width,3),(width,3),(width,3.55),(width*.5,3.85),(-width*.5,3.85),(-width,3.55)]
 section_solid('Ebony fingerboard',[(6,board_section(1.3)),(25,board_section(1))],black,scale)
 box('Nut',(0,24.8*scale,3.9*scale),(2.1*scale,.45*scale,.4*scale),black)
 box('Maple pegbox',(0,26.5*scale,2.3*scale),(2.1*scale,3.5*scale,2*scale),wood)
 box('Pegbox recess',(0,26.8*scale,3.32*scale),(1.15*scale,2.4*scale,.12*scale),black)
 sphere('Carved scroll',(0,28.3*scale,2.3*scale),(1.6*scale,1.9*scale,1.6*scale),wood)
 for y in (26,28):rod('Tuning peg',(-2.7*scale,y*scale,2.3*scale),(2.7*scale,y*scale,2.3*scale),.35*scale,black)
 box('Tailpiece',(0,-9*scale,3*scale),(2.3*scale,7*scale,.6*scale),black)
 box('Bridge',(0,0,3.55*scale),(4.3*scale,.65*scale,2*scale),ivory)
 for x in (-.75,-.25,.25,.75):line('String',[(x*scale,-11*scale,3.5*scale),(x*scale,0,4.6*scale),(x*.8*scale,24.8*scale,4.1*scale)],.045*scale,string)
 for sign in (-1,1):
  line('F hole',[(sign*x*scale,y*scale,2.9*scale) for x,y in [(3.9,-5),(3.3,-4),(3.3,-1),(4.3,2),(4,4)]],.2*scale,black)
 sphere('Chin rest',(-3*scale,-11*scale,3*scale),(2.5*scale,2.7*scale,.6*scale),black)

def violin():bowed()
def cello():
 bowed(1.7)
 rod('Adjustable standing endpin',(0,-80,0),(0,-22,0),.35,silver)
def bow():
 line('Pernambuco bow',[(-23,0,0),(-12,0,1.1),(0,0,1.5),(13,0,1.1),(24,0,0)],.25,wood)
 box('Horsehair ribbon',(.5,0,-.25),(47,.35,.18),ivory)
 box('Ebony frog',(19,0,-.25),(4.3,1.1,1.3),black)
 rod('Silver adjuster',(22,0,-.2),(25,0,-.2),.4,silver)
def trumpet():
 # Bell/leadpipe point +Y. Folded tubing lies in the upright Y/Z plane,
 # with the valve row along Y and each piston pressing vertically along Z.
 line('Folded brass tubing',[(0,0,0),(0,13,0),(0,16,4),(0,33,4),(0,33,-4),(0,12,-4),(0,12,-8),(0,37,-8),(0,39,0),(0,44,0)],.62,brass)
 rings('Flared trumpet bell',[(.65,39),(.9,44),(1.8,48),(3.5,51),(6.2,53),(6.5,53.2),(6.2,53.5),(3.25,51.2),(1.5,48.3),(.7,44.5)],brass,'Y')
 rod('Mouthpiece shank',(0,-2.5,0),(0,1,0),.55,silver)
 rod('Mouthpiece cup',(0,-3.5,0),(0,-2.5,0),1.15,silver,r2=.55)
 for y in (14.5,17,19.5):
  rod('Valve casing',(0,y,-4),(0,y,4),.8,brass)
  rod('Valve stem',(0,y,3.8),(0,y,5.4),.22,silver)
  rod('Finger button',(0,y,5.4),(0,y,5.8),.85,ivory)
def flute():
 rod('Silver flute',(-7,0,0),(43,0,0),.7,silver,16)
 sphere('Lip plate',(0,0,.3),(2,1.15,.3),silver)
 sphere('Embouchure hole',(0,0,.6),(.7,.45,.12),black)
 for x in (8,12,16,22,26,30,35,39):
  rod('Tone hole',(x,0,.55),(x,0,.75),.65,black,10)
  sphere('Key cup',(x,.5,.9),(.8,.75,.15),silver)
 rod('Key rod',(6,1,0),(40,1,0),.15,silver,6)
def clarinet():
 rod('Ebony clarinet',(0,0,-3),(0,9,-42),1.05,black,16,r2=1.35)
 rod('Mouthpiece',(0,-.5,1),(0,1,-6),.55,black,r2=1)
 for n in range(7):
  z=-9-n*4;y=-z*9/42
  sphere('Silver ring and key',(0,y+1,z),(1.25,.3,.7),silver)
 rod('Key mechanism',(-1,1,-7),(-1,9,-40),.16,silver,6)
 bell=rings('Clarinet bell',[(1.3,-42),(1.7,-46),(2.8,-49),(3,-49.3),(2.75,-49.6),(1.45,-46.5)],black)
 bell.location.y=9
def timpani():
 rings('Copper kettle',[(3,23),(12,26),(19,36),(22,49),(23,61)],copper)
 rod('Vellum head',(0,0,63.5),(0,0,64),22,ivory,32)
 rings('Tension hoop',[(22,61),(23.5,62),(23.5,65),(22.6,65)],silver)
 for n in range(8):
  a=n*math.pi/4;x,y=23*math.cos(a),23*math.sin(a)
  rod('Tension screw',(x,y,57),(x,y,65),.4,black,8)
 for n in range(3):
  a=n*2*math.pi/3;x,y=18*math.cos(a),18*math.sin(a)
  rod('Tripod leg',(x,y,0),(x*.7,y*.7,36),1,black)
 box('Tuning pedal',(0,-20,2),(7,11,2),black)
def mallet():
 rod('Mallet shaft',(0,0,0),(0,8,-10),.38,wood)
 sphere('Felt mallet head',(0,8,-10),(1.8,1.8,1.8),ivory)

def save(name,make):
 bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False);make()
 root=bpy.data.objects.new('Fetcher_'+name,None);bpy.context.collection.objects.link(root)
 objects=list(bpy.context.scene.objects)
 for o in objects:
  if o==root:continue
  bpy.context.view_layer.objects.active=o;o.select_set(True)
  if o.type=='CURVE':bpy.ops.object.convert(target='MESH')
  o.parent=root
  bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
  o.select_set(False)
 meshes=[o for o in bpy.context.scene.objects if o.type=='MESH']
 for o in meshes:
  if not o.data.uv_layers:
   layer=o.data.uv_layers.new(name='UVMap')
   for polygon in o.data.polygons:
    for index in polygon.loop_indices:
     point=o.data.vertices[o.data.loops[index].vertex_index].co;layer.data[index].uv=(point.x/40+.5,point.y/70+.5)
  o.data.calc_loop_triangles()
 triangles=sum(len(o.data.loop_triangles) for o in meshes)
 points=[o.matrix_world@v.co for o in meshes for v in o.data.vertices]
 bounds=[[min(v[i] for v in points) for i in range(3)],[max(v[i] for v in points) for i in range(3)]]
 bpy.ops.object.select_all(action='SELECT');bpy.context.view_layer.objects.active=root
 path=models/(name+'.nif')
 bpy.ops.export_scene.mw(filepath=str(path),use_selection=True,export_animations=False,extract_keyframe_data=False,
     preserve_root_tranforms=False,preserve_material_names=True,vertex_precision=.0001)
 stream=nif.NiStream();stream.load(path)
 actual=sum(len(o.triangles) for o in stream.objects_of_type(nif.NiTriShapeData))
 assert 0<=triangles-actual<=8,(name,triangles,actual)
 for texture in stream.objects_of_type(nif.NiSourceTexture):
  texture.filename='textures\\fetcher\\orchestra\\'+Path(texture.filename.replace('\\','/')).name
  assert (args.output/texture.filename.replace('\\','/')).is_file(),'Unresolved original texture'
 stream.save(path)
 report[name]={'model':path.relative_to(args.output).as_posix(),'triangles':actual,'bounds':bounds,'bytes':path.stat().st_size}
 if name in ('violin','cello'):
  shapes=list(stream.objects_of_type(nif.NiTriShape))
  board=next(o for o in shapes if o.name=='Ebony fingerboard')
  neck=next(o for o in shapes if o.name=='Maple neck and heel')
  gap=float(min(p[2] for p in board.data.vertices)-max(p[2] for p in neck.data.vertices))
  assert abs(gap)<.0001,(name,'Serialized fingerboard floats above neck',gap)
  report[name]['fingerboardToNeckGap']=gap
 if name=='trumpet':
  shapes=list(stream.objects_of_type(nif.NiTriShape))
  tubing=next(o for o in shapes if o.name=='Folded brass tubing').data.vertices
  spans=[float(max(p[i] for p in tubing)-min(p[i] for p in tubing)) for i in range(3)]
  assert spans[2]>8*spans[0],('Trumpet tubing must stand in Y/Z',spans)
  casings=[[o.rotation @ p * o.scale + o.translation for p in o.data.vertices] for o in shapes if o.name.startswith('Valve casing')]
  centers=[tuple(float(sum(p[i] for p in points)/len(points)) for i in range(3)) for points in casings]
  assert len(centers)==3 and max(p[0] for p in centers)-min(p[0] for p in centers)<.001
  assert 4.9<max(p[1] for p in centers)-min(p[1] for p in centers)<5.1
  for points in casings:
   span=[float(max(p[i] for p in points)-min(p[i] for p in points)) for i in range(3)]
   assert span[2]>4*max(span[:2]),('Pistons must remain upright',span)
  report[name]['tubingPlane']='YZ';report[name]['valveRowAxis']='Y';report[name]['pistonAxis']='Z'
 bpy.ops.wm.save_as_mainfile(filepath=str(args.working/(name+'.blend')))
 # Small transparent in-game inventory icon, rendered from the exported geometry.
 scene=bpy.context.scene;scene.render.engine='BLENDER_EEVEE_NEXT';scene.render.resolution_x=128;scene.render.resolution_y=128;scene.render.resolution_percentage=100
 scene.render.film_transparent=True;scene.render.image_settings.file_format='PNG';scene.render.image_settings.color_mode='RGBA'
 center=Vector([(bounds[0][i]+bounds[1][i])/2 for i in range(3)]);span=max(bounds[1][i]-bounds[0][i] for i in range(3))
 cam=bpy.data.cameras.new('Icon camera');camera=bpy.data.objects.new('Icon camera',cam);scene.collection.objects.link(camera)
 camera.location=center+Vector((1,-1.5,1))*span;camera.rotation_euler=(center-camera.location).to_track_quat('-Z','Y').to_euler();cam.type='ORTHO';cam.ortho_scale=span*1.35;scene.camera=camera
 for offset,power in [((1,-1,2),1500),((-1,1,1),900)]:
  light=bpy.data.lights.new('Icon area','AREA');light.energy=power*span*span/25;light.shape='DISK';light.size=span*2
  obj=bpy.data.objects.new('Icon area',light);scene.collection.objects.link(obj);obj.location=center+Vector(offset)*span;obj.rotation_euler=(center-obj.location).to_track_quat('-Z','Y').to_euler()
 scene.world.color=(.4,.4,.4);scene.view_settings.view_transform='Standard';scene.view_settings.exposure=.3
 png=args.working/(name+'.png');scene.render.filepath=str(png);bpy.ops.render.render(write_still=True)
 subprocess.run([magick,str(png),'-define','dds:compression=dxt5','-define','dds:mipmaps=7',str(icons/(name+'.dds'))],check=True)

for name,make in [('violin',violin),('cello',cello),('bow',bow),('trumpet',trumpet),('flute',flute),('clarinet',clarinet),('timpani',timpani),('mallet',mallet)]:
 if not args.only or args.only==name:save(name,make)
(args.output/'orchestra-report.json').write_bytes((json.dumps(report,indent=2)+'\n').encode('utf-8'))
notice='''Original Fetcher Studio orchestral props

Violin, cello, bow, trumpet, flute, clarinet, timpani and mallet meshes and icons
were generated from scripts/procedural-synth-assets/generate_orchestra.py for
Fetcher Simulator. No downloaded or game-owned mesh/texture was used.
Attribution: Fetcher Simulator contributors / Fetcher Studio orchestral props.
License: Creative Commons Attribution 4.0 International (CC BY 4.0).
https://creativecommons.org/licenses/by/4.0/
These are original stylized standing-performance props, not exact replicas or
endorsements of a manufacturer's instruments. The cello stance is provisional
until seated layouts ship. Existing keyboard/stand attribution remains separate.
'''
(args.output/'ORCHESTRA-ASSETS.txt').write_bytes(notice.encode('utf-8'))
p=args.output/'ATTRIBUTION.txt';s=p.read_text(encoding='utf-8')
if 'Original Fetcher Studio orchestral props' not in s:p.write_bytes((s+'\n'+notice).encode('utf-8'))
manifest=json.loads((args.output/'asset-manifest.json').read_text(encoding='utf-8'))
manifest['files']=[{'path':p.relative_to(args.output).as_posix(),'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
 for p in sorted(args.output.rglob('*')) if p.is_file() and p.name!='asset-manifest.json']
(args.output/'asset-manifest.json').write_bytes((json.dumps(manifest,indent=2)+'\n').encode('utf-8'))
print('ORCHESTRA_ASSETS_PASS',json.dumps(report))
