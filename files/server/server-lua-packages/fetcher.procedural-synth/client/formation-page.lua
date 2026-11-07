-- A diagram only: server positions and the bounded collision preflight use the same Layout.
local ui=require('openmw.ui');local util=require('openmw.util');local async=require('openmw.async')
local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local Layout=require(prefix..'shared.band-layout');local Project=require(prefix..'shared.project')(require(prefix..'shared.schema'))
local Dropdown=require(prefix..'client.dropdown');local Controls=require(prefix..'client.controls')
local v=util.vector2;local white=ui.texture{path='white'}
local colors={strings=util.color.rgb(.35,.75,1),winds=util.color.rgb(.4,.9,.65),brass=util.color.rgb(1,.75,.3),
 keys=util.color.rgb(.8,.65,1),rhythm=util.color.rgb(1,.55,.4),percussion=util.color.rgb(.9,.5,.8)}
local function rect(x,y,w,h,color) return {type=ui.TYPE.Image,props={position=v(x,y),size=v(w,h),resource=white,color=color}} end
local function text(s,x,y,w,color,size) return {type=ui.TYPE.Text,props={position=v(x,y),size=v(w,23),text=s,textSize=size or 12,textColor=color or util.color.rgb(.85,.89,.94)}} end
local function escaped(s) return tostring(s):gsub('#','##') end
return function(owner,content)
 local w=owner.w;local function button(s,x,y,width,fn,on) content[#content+1]=owner:button(s,x,y,width,fn,on) end
 content[#content+1]=text('FORMATION / STAGE PREVIEW',20,170,660,util.color.rgb(.2,.85,.72),16)
 button('Back to Band',720,166,170,function() w.page='band';w.revision=w.revision+1 end)
 local get=function() return Project.formation(w.project.formation) end
 local function change(key,value,quiet) local candidate=get();candidate[key]=value;return w:setFormation(candidate,key,quiet) end
 button('Preset: '..get().preset..' v',20,218,260,function()
  local choices={};for _,preset in ipairs(Project.formationPresets) do choices[#choices+1]={id=preset.id,label=preset.name} end
  Dropdown.open(owner,choices,get().preset,function(id) change('preset',id) end,20,250,420)
 end)
 button('Spacing presets v',20,259,260,function()
  Dropdown.open(owner,{{id=1,label='Standard / 100%'},{id=1.25,label='Roomy / 125%'},{id=1.6,label='Wide / 160%'}},get().spacing,function(value)
   local settings=get();settings.spacing=value;settings.depth=value;settings.distance=1;w:setFormation(settings,'spacingPreset')
  end,20,292,420)
 end)
 for n,entry in ipairs{{'spacing','Across spacing'},{'depth','Row depth'},{'distance','Leader distance'}} do
  content[#content+1]=Controls.slider(owner,'formation:'..entry[1],entry[2],{min=1,max=2,step=.01,unit='%'},function() return get()[entry[1]] end,
   function(value) change(entry[1],value,true) end,20,308+(n-1)*58,260)
 end
 button(w.formationMode=='bound' and 'Preview: bound NPCs v' or 'Preview: automatic band v',20,491,260,function()
  Dropdown.open(owner,{{id='auto',label='Automatic band (grouped song parts)'},{id='bound',label='Currently bound NPCs (restage)'}},w.formationMode or 'auto',function(mode) w.formationMode=mode;w.revision=w.revision+1 end,20,300,440)
 end)
 button(w.bandSetup and 'Setting up NPCs...' or 'Create / update auto band',20,539,260,function() owner:releaseAll();w:setupNpcBand(false) end)
 button('Restage bound NPCs',20,580,260,function() owner:releaseAll();w:setupNpcBand(true) end)
 content[#content+1]=text('Settings save with the project.',20,628,270)
 content[#content+1]=text('Apply placement stops playback.',20,652,270)
 local preview=owner:element(rect(300,218,590,355,util.color.rgb(.075,.09,.12)));content[#content+1]=preview
 local status=owner:element(text('',300,590,590,util.color.rgb(.2,.85,.72)));content[#content+1]=status
 for n,section in ipairs(Layout.sections) do
  local x=300+(n-1)%3*195;local y=620+math.floor((n-1)/3)*25
  content[#content+1]=rect(x,y+5,10,10,colors[section.id]);content[#content+1]=text(section.name,x+17,y,175,colors[section.id])
 end
 content[#content+1]=text('Top-down diagram, leader at bottom. Apply checks ground/walls and the 2000-unit keyboard range; preview does not move actors.',20,678,880,util.color.rgb(.6,.65,.72),11)
 local view={preview=preview,status=status};owner.formationView=view
 function view:frame()
  local plan=w.formationMode=='bound' and Layout.bound(w.project.band) or Layout.plan(w.project)
  local positions=Layout.positions({x=0,y=0,z=0,rz=0},plan,get()) or {};local signature={w.formationMode or 'auto'}
  for n,p in ipairs(positions) do signature[#signature+1]=table.concat({p.x,p.y,p.section,plan[n].instrument or '',plan[n].slot or n},',') end
  local settings=get();signature[#signature+1]=table.concat({settings.preset,settings.spacing,settings.depth,settings.distance},',');signature=table.concat(signature,'|')
  if self.signature==signature then return end;self.signature=signature
  local maxX,maxY=100,300;for _,p in ipairs(positions) do maxX=math.max(maxX,math.abs(p.x));maxY=math.max(maxY,p.y) end
  local scale=math.min(535/(maxX*2+160),265/(maxY+100));local data={text('FRONT / BAND',205,8,200,nil,11)}
  local bx,by=295,320;data[#data+1]=rect(bx-5,by-5,10,10,util.color.rgb(.9,.95,1));data[#data+1]=text('Leader',bx-25,by+8,80,nil,11)
  for n,p in ipairs(positions) do
   local color=colors[p.section];local x,y=bx+p.x*scale,by-p.y*scale;local size=plan.wideSpacing and 17 or 13
   data[#data+1]=rect(x-size/2,y-size/2,size,size,color)
   data[#data+1]=text(tostring(plan[n].slot or n),x-6,y-8,28,util.color.rgb(.03,.04,.06),10)
  end
  if #positions==0 then data[#data+1]=text(w.formationMode=='bound' and 'No NPCs bound. Create the automatic band first.' or 'Create/import song parts to preview performers.',75,140,470) end
  self.preview.layout.content=ui.content(data);owner.window:update(self.preview)
  local width=#positions>0 and maxX*2 or 0
  self.status.layout.props.text=string.format('%d NPCs | span about %.0f x %.0f units | %s',#positions,width,#positions>0 and maxY or 0,plan.wideSpacing and 'drum-kit clearance' or 'standard clearance')
  owner.window:update(self.status)
 end
 view:frame()
end
