-- Locally rendered held props/arms; audio remains native and server-authorized.
local anim=require('openmw.animation');local util=require('openmw.util');local types=require('openmw.types')
local Performance=require('scripts.multiplayer.fetcher.procedural-synth.client.performance')
local vfs=require('openmw.vfs')
local camera=require('openmw.camera')
local A={};A.__index=A;local v,T=util.vector3,util.transform
local profiles={
    keyboard={model='meshes/fetcher/synth/keyboard.nif',position=v(0,22,85),rotation=T.identity,
        targets={L=v(-12,23,90),R=v(12,23,90)},strike=1.4},
    lute={model='meshes/m/Misc_de_lute_01.nif',position=v(0,18,82),rotation=T.rotateX(math.pi/2)*T.rotateZ(-0.5),
        targets={L=v(-14,20,104),R=v(8,23,84)},strike=3},
    drum={model='meshes/m/Misc_de_drum_01.nif',position=v(0,24,76),rotation=T.identity,
        targets={L=v(-8,23,88),R=v(8,23,88)},strike=6}}
function A.new(mp,nearby,self) return setmetatable({mp=mp,nearby=nearby,self=self,objects={},poses={}},A) end
function A:key(r) return r.kind=='player' and ('p:'..r.guid) or ('n:'..r.mpNum) end
function A:source(r) return r and self.objects[self:key(r)] end
function A:clear(key)
    local p=self.poses[key];if not p then return end
    if p.pose then pcall(p.pose.clear,p.pose) end
    if p.actor:isValid() and anim.clearVisualAttachment then pcall(anim.clearVisualAttachment,p.actor,'synth_instrument') end
    self.poses[key]=nil
end
function A:clearInstance(i) for _,r in pairs(i.performers or {}) do self:clear(self:key(r)) end end
function A:clearAll() for key in pairs(self.poses) do self:clear(key) end;self.objects={} end
function A:frame(instances,dt,now)
    self.objects={};local npcs={};local active={}
    for _,actor in ipairs(self.nearby.actors or {}) do local id=self.mp.getObjectMpNum(actor);if id then npcs[id]=actor end end
    for _,i in pairs(instances) do
        for slot,r in pairs(i.performers or {}) do
            if i.availability[slot]=='Ready' then
                local actor=r.kind=='player' and self.mp.getPlayerObject(r.guid) or npcs[r.mpNum]
                if actor and actor:isValid() and actor.cell==self.self.cell and (not types.Actor.isDead or not types.Actor.isDead(actor)) then self.objects[self:key(r)]=actor end
                local group=i.outputs and i.outputs[slot]
                if group and (not actor or self:source(r)~=group.source) then group.audio:close();i.outputs[slot]=nil;group=nil end
                local visible=actor~=self.self or camera.getMode()~=camera.MODE.FirstPerson
                if visible and actor and self:source(r) and group and (group.audio.playing or group.audio.voices>0) and anim.visualAttachmentsVersion==1 then
                    local key=self:key(r);local p=self.poses[key];local profile=profiles[r.instrument]
                    if r.instrument=='keyboard' and vfs.fileExists and not vfs.fileExists(profile.model) then profile=profiles.lute end
                    if p and (p.actor~=actor or p.instrument~=r.instrument) then self:clear(key);p=nil end
                    if not p then p={actor=actor,instrument=r.instrument,pose=Performance.new(actor),refresh=-10};self.poses[key]=p end
                    active[key]=true
                    if p.pose and not p.pose.missing then
                        local source={position=actor.position,rotation=actor.rotation}
                        p.pose:frame(dt,source,group.audio:notes(),now,group.audio.strikes,profile)
                    end
                    if profile and now-p.refresh>0.2 then
                        local bone='Bip01 Spine2'
                        if not p.transform then
                            local transform=anim.getBoneTransform(actor,bone,true,true)
                            if transform then p.transform=transform:inverse()*T.move(actor.position)*actor.rotation*T.move(profile.position)*profile.rotation end
                        end
                        if p.transform then anim.setVisualAttachment(actor,'synth_instrument',profile.model,{bone=bone,transform=p.transform,timeout=0.6}) end
                        p.refresh=now
                    end
                end
            end
        end
    end
    for key in pairs(self.poses) do if not active[key] then self:clear(key) end end
end
function A:status()
    local result={}
    for key,p in pairs(self.poses) do result[#result+1]={key=key,instrument=p.instrument,bones=p.pose and p.pose.applied or 0,
        attachments=anim.visualAttachmentCount and anim.visualAttachmentCount(p.actor) or 0} end
    return result
end
return A
