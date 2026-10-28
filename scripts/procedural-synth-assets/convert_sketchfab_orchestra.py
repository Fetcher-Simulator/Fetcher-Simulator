"""Convert verified CC BY Sketchfab GLBs to static Morrowind performance props.

Run in Blender 4.5 with --background --factory-startup --disable-autoexec.
Original downloads/metadata stay outside the repository. No source scripts run.
"""
import argparse, hashlib, json, math, shutil, subprocess, sys
from pathlib import Path
import bpy
from mathutils import Matrix, Vector

p=argparse.ArgumentParser()
p.add_argument('--source',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--working',type=Path,required=True)
p.add_argument('--only')
args=p.parse_args(sys.argv[sys.argv.index('--')+1:])
args.output=args.output.resolve();args.working.mkdir(parents=True,exist_ok=True)
magick=shutil.which('magick');assert magick
bpy.ops.preferences.addon_enable(module='io_scene_mw')
bpy.context.preferences.addons['io_scene_mw'].preferences.scale_correction=1
bpy.context.preferences.addons['io_scene_mw'].preferences.texture_paths.add().name=str(args.output/'textures')
from io_scene_mw import nif_shader
from es3 import nif

plan=json.loads((Path(__file__).with_name('sketchfab-sources.json')).read_text())
report={};credits=[]
models=args.output/'meshes/fetcher/orchestra';textures=args.output/'textures/fetcher/orchestra';icons=args.output/'icons/fetcher/orchestra'
for directory in (models,textures,icons):directory.mkdir(parents=True,exist_ok=True)
def bounds(objects):
    points=[o.matrix_world@v.co for o in objects for v in o.data.vertices]
    return Vector([min(q[i] for q in points) for i in range(3)]),Vector([max(q[i] for q in points) for i in range(3)])
def clear():
    bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
def image_dds(image,filename,normal=False):
    png=args.working/(filename+'.png');image.filepath_raw=str(png);image.file_format='PNG';image.save()
    output=textures/(filename+'.dds')
    subprocess.run([magick,str(png),'-resize','1024x1024>','-alpha','off','-strip','-define','dds:compression='+('dxt5' if normal else 'dxt1'),'-define','dds:mipmaps=10',str(output)],check=True,capture_output=True)
    return output
def material(source,name,index):
    result=nif_shader.create_material('CC_'+name+'_'+str(index)).mw
    result.ambient_color[:]=(1,1,1,1);result.diffuse_color[:]=(1,1,1,1);result.specular_color[:]=(.3,.3,.3,1);result.shine=22
    shader=next((n for n in source.node_tree.nodes if n.type=='BSDF_PRINCIPLED'),None) if source and source.use_nodes else None
    color=shader.inputs['Base Color'].default_value[:] if shader else source.diffuse_color[:] if source else (.6,.6,.6,1)
    image=None
    if shader:
        links=shader.inputs['Base Color'].links
        if links and links[0].from_node.type=='TEX_IMAGE':image=links[0].from_node.image
    if not image:
        image=bpy.data.images.new('CC flat material',4,4,alpha=False)
        image.pixels=list(color)*16
    filename='cc_'+name+'_'+str(index);diffuse=image_dds(image,filename)
    result.base_texture.image=bpy.data.images.load(str(diffuse),check_existing=True);result.base_texture.layer='UVMap'
    if shader:
        metallic=shader.inputs['Metallic'].default_value
        result.specular_color[:]=((.55 if metallic>.5 else .18),)*3+(1,)
        result.shine=8+24*(1-shader.inputs['Roughness'].default_value)
        links=shader.inputs['Normal'].links
        if links and links[0].from_node.type=='NORMAL_MAP':
            normal_links=links[0].from_node.inputs['Color'].links
            if normal_links and normal_links[0].from_node.type=='TEX_IMAGE':
                normal=normal_links[0].from_node.image
                if normal:image_dds(normal,filename+'_n',True)
    return result.material
def export(name,objects,transform,sourceName):
    root=bpy.data.objects.new('CC_'+name,None);bpy.context.collection.objects.link(root);visuals=[];materials={}
    for original in objects:
        mesh=original.data.copy();mesh.transform(transform@original.matrix_world)
        mesh.update();obj=bpy.data.objects.new('CC_'+name+'_'+original.name,mesh);bpy.context.collection.objects.link(obj);obj.parent=root;obj.mw.object_flags=2
        if mesh.uv_layers:mesh.uv_layers.active.name='UVMap'
        else:
            uv=mesh.uv_layers.new(name='UVMap')
            for polygon in mesh.polygons:
                for loop in polygon.loop_indices:uv.data[loop].uv=(.5,.5)
        if not mesh.materials:mesh.materials.append(bpy.data.materials.new('Source neutral'))
        for index,old in enumerate(list(mesh.materials)):
            if old not in materials:materials[old]=material(old,sourceName,len(materials))
            mesh.materials[index]=materials[old]
        for polygon in mesh.polygons:polygon.use_smooth=True
        visuals.append(obj)
    bpy.ops.object.select_all(action='DESELECT');root.select_set(True)
    for o in visuals:o.select_set(True)
    bpy.context.view_layer.objects.active=root
    path=models/(name+'.nif')
    bpy.ops.export_scene.mw(filepath=str(path),use_selection=True,export_animations=False,extract_keyframe_data=False,preserve_root_tranforms=False,preserve_material_names=True,vertex_precision=.0001)
    stream=nif.NiStream();stream.load(path)
    triangles=sum(len(o.triangles) for o in stream.objects_of_type(nif.NiTriShapeData));assert 50<=triangles<=60000,(name,triangles)
    for texture in stream.objects_of_type(nif.NiSourceTexture):
        texture.filename='textures\\fetcher\\orchestra\\'+Path(texture.filename.replace('\\','/')).name
        assert (args.output/texture.filename.replace('\\','/')).is_file()
    stream.save(path)
    low,high=bounds(visuals);assert max(high-low)<200 and min(high-low)>.01,(name,low,high)
    report[name]={'source':sourceName,'model':path.relative_to(args.output).as_posix(),'triangles':triangles,'bounds':[list(low),list(high)],'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
    for o in bpy.context.scene.objects:
        if o.type=='MESH' and o not in visuals:o.hide_render=True
    scene=bpy.context.scene;scene.render.engine='BLENDER_EEVEE_NEXT';scene.render.resolution_x=512;scene.render.resolution_y=512;scene.render.resolution_percentage=100;scene.render.film_transparent=True
    scene.render.image_settings.file_format='PNG';scene.render.image_settings.color_mode='RGBA';center=(low+high)/2;span=max(high-low)
    camera=bpy.data.objects.new('Camera',bpy.data.cameras.new('Camera'));scene.collection.objects.link(camera)
    camera.location=center+Vector((1,-1.5,1))*span;camera.rotation_euler=(center-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.type='ORTHO';camera.data.ortho_scale=span*1.35;camera.data.clip_end=span*100;scene.camera=camera
    for pos,power in [((1,-2,3),700),((-2,1,1),400)]:
        lamp=bpy.data.objects.new('Lamp',bpy.data.lights.new('Lamp','AREA'));scene.collection.objects.link(lamp);lamp.location=center+Vector(pos)*span;lamp.rotation_euler=(center-lamp.location).to_track_quat('-Z','Y').to_euler();lamp.data.energy=power*span*span;lamp.data.size=span*3
    scene.world.color=(.3,.3,.3);scene.view_settings.view_transform='Standard';scene.render.filepath=str(args.working/(name+'.png'));bpy.ops.render.render(write_still=True)
    subprocess.run([magick,scene.render.filepath,'-resize','128x128','-define','dds:compression=dxt5','-define','dds:mipmaps=7',str(icons/(name+'.dds'))],check=True,capture_output=True)
def matrix(rotation,scale,center,offset=(0,0,0)):
    return Matrix.Translation(Vector(offset))@Matrix.Scale(scale,4)@rotation@Matrix.Translation(-Vector(center))
def crop_violin(mesh,bow):
    import bmesh
    bm=bmesh.new();bm.from_mesh(mesh);seen=set();remove=set()
    for vertex in bm.verts:
        if vertex in seen:continue
        stack=[vertex];component=[];seen.add(vertex)
        while stack:
            v=stack.pop();component.append(v)
            for edge in v.link_edges:
                other=edge.other_vert(v)
                if other not in seen:seen.add(other);stack.append(other)
        is_bow=sum(v.co.y-.23*v.co.x for v in component)/len(component)>.14
        if is_bow!=bow:
            for v in component:remove.update(v.link_faces)
    bmesh.ops.delete(bm,geom=list(remove),context='FACES')
    bmesh.ops.delete(bm,geom=[v for v in bm.verts if not v.link_faces],context='VERTS')
    bm.to_mesh(mesh);bm.free();mesh.update()

for name,item in plan.items():
    if args.only and args.only!=name:continue
    folder=args.source/name;meta=json.loads((folder/'sketchfab-metadata.json').read_text());download=json.loads((folder/'download.json').read_text())
    assert meta['uid']==item['uid'] and meta['license']['url'].replace('http://','https://').rstrip('/')=='https://creativecommons.org/licenses/by/4.0'
    source=folder/item['file'];assert hashlib.sha256(source.read_bytes()).hexdigest()==download['sha256']
    clear();bpy.ops.import_scene.gltf(filepath=str(source));bpy.context.view_layer.update()
    objects=[o for o in bpy.context.scene.objects if o.type=='MESH'];low,high=bounds(objects);span=high-low;center=(low+high)/2
    if name=='violin':
        # The creator supplied a violin and separate bow in one textured mesh.
        # The detached bow follows Y=.23*X+.23, above the violin's silhouette.
        # Connected UV islands split at the empty diagonal gap; no triangle cuts.
        original=objects[0];assert len(objects)==1
        original.data.transform(original.matrix_world);original.matrix_world=Matrix.Identity(4)
        groups=[]
        for bow in (False,True):
            mesh=original.data.copy();crop_violin(mesh,bow)
            obj=bpy.data.objects.new('Source bow' if bow else 'Source violin',mesh);bpy.context.collection.objects.link(obj);groups.append(obj)
        bpy.data.objects.remove(original,do_unlink=True)
        low,high=bounds([groups[0]]);scale=44.2/(high.x-low.x)
        export('violin',[groups[0]],matrix(Matrix.Rotation(-math.pi/2,4,'Z'),scale,(high.x-14/scale,-.118,-.01)),'violin')
        clear();bpy.ops.import_scene.gltf(filepath=str(source));original=next(o for o in bpy.context.scene.objects if o.type=='MESH');original.data.transform(original.matrix_world);original.matrix_world=Matrix.Identity(4)
        crop_violin(original.data,True)
        low,high=bounds([original]);export('bow',[original],matrix(Matrix.Rotation(-math.atan(.23),4,'Z'),48/(high.x-low.x),(low+high)/2),'violin')
    elif name=='cello':export(name,objects,matrix(Matrix.Rotation(-math.pi/2,4,'X'),131.34/span.z,(center.x,center.y,low.z),(0,-80,0)),name)
    elif name=='trumpet':export(name,objects,matrix(Matrix.Rotation(math.pi/2,4,'Z'),57/span.x,(low.x,.021,.061)),name)
    elif name=='flute':export(name,objects,matrix(Matrix.Rotation(math.pi/2,4,'Z'),61/span.y,(center.x,high.y,0),(-7,0,0)),name)
    elif name=='clarinet':export(name,objects,matrix(Matrix.Rotation(math.pi/2+.2,4,'X'),50/span.y,(center.x,high.y,center.z)),name)
    elif name=='timpani':export(name,objects,matrix(Matrix.Identity(4),80/span.z,(center.x,center.y,low.z)),name)
    elif name=='music-stand':export(name,objects,matrix(Matrix.Identity(4),82/span.z,(center.x,center.y,low.z)),name)
    elif name=='chair':export(name,objects,matrix(Matrix.Rotation(math.pi,4,'Z'),90/span.z,(center.x,center.y,low.z)),name)
    elif name=='mallet':
        # Select one complete stick, preserving the creator's geometry/materials.
        assert len(objects)==2 and all(len(o.data.polygons)>500 for o in objects)
        selected=[max(objects,key=lambda o:bounds([o])[0].z)]
        low,high=bounds(selected);center=(low+high)/2
        export(name,selected,matrix(Matrix.Rotation(-.25,4,'X')@Matrix.Rotation(math.pi/2,4,'Z'),24/(high.x-low.x),center,(0,8,-2)),name)
    credits.append({'id':name,'title':meta['name'],'author':meta['user']['displayName'],'authorUrl':meta['user']['profileUrl'],'source':meta['viewerUrl'],'license':'CC BY 4.0','licenseUrl':'https://creativecommons.org/licenses/by/4.0/','downloadSha256':download['sha256'],
      'changes':'Static glTF to NIF 4.0.0.2; rescaled/reoriented for in-game grips; original UVs retained; PBR adapted to legacy materials; textures resized to mipmapped DDS; inventory icons rendered from converted meshes.'})
old=args.output/'sketchfab-orchestra-report.json'
previous=json.loads(old.read_text()) if args.only and old.exists() else {};previous.update(report)
old.write_bytes((json.dumps(previous,indent=2)+'\n').encode())
registry=args.output/'sketchfab-models.json';previous=json.loads(registry.read_text()) if args.only and registry.exists() else []
previous=[x for x in previous if x['id'] not in {c['id'] for c in credits}]+credits
registry.write_bytes((json.dumps(previous,indent=2,ensure_ascii=False)+'\n').encode('utf-8'))
print('CC_ORCHESTRA_CONVERTED',json.dumps(report))
