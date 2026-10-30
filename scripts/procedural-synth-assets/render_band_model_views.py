"""Inspect the converted NIFs from three sides without opening a game client."""
import bpy,json,sys
from pathlib import Path
from mathutils import Vector
assets=Path(sys.argv[sys.argv.index('--')+1]).resolve();output=Path(sys.argv[sys.argv.index('--')+2]).resolve();output.mkdir(parents=True,exist_ok=True)
bpy.ops.preferences.addon_enable(module='io_scene_mw')
bpy.context.preferences.addons['io_scene_mw'].preferences.scale_correction=1
bpy.context.preferences.addons['io_scene_mw'].preferences.texture_paths.add().name=str(assets/'textures')
views={}
for name in ['guitar','bass','drumkit']:
 bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
 bpy.ops.import_scene.mw(filepath=str(assets/f'meshes/fetcher/orchestra/{name}.nif'));bpy.context.view_layer.update()
 points=[o.matrix_world@v.co for o in bpy.context.scene.objects if o.type=='MESH' for v in o.data.vertices]
 low=Vector([min(p[i] for p in points) for i in range(3)]);high=Vector([max(p[i] for p in points) for i in range(3)]);center=(low+high)/2;span=max(high-low)
 scene=bpy.context.scene;scene.render.engine='BLENDER_EEVEE_NEXT';scene.render.resolution_x=512;scene.render.resolution_y=512;scene.render.resolution_percentage=100;scene.render.film_transparent=True;scene.render.image_settings.file_format='PNG';scene.view_settings.view_transform='Standard';scene.view_settings.exposure=-1
 camera=bpy.data.objects.new('Inspection camera',bpy.data.cameras.new('Inspection camera'));scene.collection.objects.link(camera);scene.camera=camera;camera.data.type='ORTHO';camera.data.ortho_scale=span*1.2;camera.data.clip_end=span*100
 for position in [(1,-1,2),(-1,1,2),(0,-2,0)]:
  lamp=bpy.data.objects.new('Inspection light',bpy.data.lights.new('Inspection light','AREA'));scene.collection.objects.link(lamp);lamp.location=center+Vector(position)*span;lamp.rotation_euler=(center-lamp.location).to_track_quat('-Z','Y').to_euler();lamp.data.energy=200*span*span;lamp.data.size=span*3
 directions={'front':(0,0,2),'side':(2,0,.2),'back':(0,0,-2)} if name!='drumkit' else {'front':(0,2,.8),'side':(2,0,.8),'back':(0,-2,.8)}
 views[name]=[]
 for side,direction in directions.items():
  camera.location=center+Vector(direction)*span;camera.rotation_euler=(center-camera.location).to_track_quat('-Z','Y').to_euler();scene.render.filepath=str(output/f'{name}-{side}.png');bpy.ops.render.render(write_still=True);views[name].append(scene.render.filepath)
(output/'views.json').write_text(json.dumps(views,indent=2));print('NIF_VIEWS_RENDERED',json.dumps(views))
