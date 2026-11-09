-- Locally rendered held props/arms; audio remains native and server-authorized.
local anim=require('openmw.animation');local util=require('openmw.util');local types=require('openmw.types')
local Performance=require('scripts.multiplayer.fetcher.procedural-synth.client.performance')
local Conductor=require('scripts.multiplayer.fetcher.procedural-synth.client.conductor')
local vfs=require('openmw.vfs')
local camera=require('openmw.camera')
local profiles=require('scripts.multiplayer.fetcher.procedural-synth.client.instrument-profiles')
local A={};A.__index=A;local v,T=util.vector3,util.transform
function A.new(mp,nearby,self,calibration) return setmetatable({mp=mp,nearby=nearby,self=self,calibration=calibration,objects={},poses={},conductors={},targetActors={}},A) end
function A:profile(name) return self.calibration and self.calibration:profile(name) or profiles[name] end
function A:targets(instrument)
    local out={};for key,p in pairs(self.targetActors) do if p.instrument==instrument and p.actor:isValid() then
        out[#out+1]={key=key,actor=p.actor,pose=p.pose or self.poses[key] and self.poses[key].pose}
    end end
    table.sort(out,function(a,b) return a.key<b.key end);return out
end
function A:key(r) return r.kind=='player' and ('p:'..r.guid) or ('n:'..r.mpNum) end
function A:source(r) return r and self.objects[self:key(r)] end
function A:hasModel(instrument)
    local profile=profiles[instrument]
    if not profile or vfs.fileExists and not vfs.fileExists(profile.model) then return false end
    for _,extra in ipairs(profile.extras or {}) do if vfs.fileExists and not vfs.fileExists(extra.model) then return false end end
    return true
end
function A:clear(key)
    local p=self.poses[key];if not p then return end
    if p.pose then pcall(p.pose.clear,p.pose) end
    if p.actor:isValid() and anim.clearVisualAttachment then pcall(anim.clearVisualAttachment,p.actor,'synth_instrument') end
    if p.actor:isValid() and anim.clearVisualAttachment then
        for _,key in ipairs{'synth_bow','synth_mallet_l','synth_mallet_r','synth_chair','synth_music_stand'} do pcall(anim.clearVisualAttachment,p.actor,key) end
        if p.kitProfile then
            for _,part in ipairs(profiles.kitParts) do pcall(anim.clearVisualAttachment,p.actor,part.key) end
        end
    end
    self.poses[key]=nil
end
function A:clearInstance(i)
    for _,r in pairs(i.performers or {}) do self:clear(self:key(r)) end
    if self.conductors[i.id] then self.conductors[i.id]:clear();self.conductors[i.id]=nil end
end
function A:clearAll()
    for key in pairs(self.poses) do self:clear(key) end
    for _,c in pairs(self.conductors) do c:clear() end
    self.conductors={};self.objects={};self.targetActors={}
end
function A:frame(instances,dt,now)
    self.objects={};self.targetActors={};local npcs={};local active={}
    for _,actor in ipairs(self.nearby.actors or {}) do local id=self.mp.getObjectMpNum(actor);if id then npcs[id]=actor end end
    for id,i in pairs(instances) do
        local ownerHeld=false
        for slot,r in pairs(i.performers or {}) do if r.kind=='player' and r.guid==i.owner and i.availability[slot]=='Ready' then ownerHeld=true end end
        local owner=i.leaderPlays~=false and not ownerHeld and i.owner and self.mp.getPlayerObject and self.mp.getPlayerObject(i.owner)
        if owner and owner:isValid() and owner.cell==self.self.cell then self.targetActors['keyboard:'..id]={actor=owner,instrument='keyboard',pose=i.pose} end
        for slot,r in pairs(i.performers or {}) do
            if i.availability[slot]=='Ready' then
                local actor=r.kind=='player' and self.mp.getPlayerObject(r.guid) or npcs[r.mpNum]
                local valid=actor and actor:isValid();local cell=valid and actor.cell;local observer=self.self.cell
                local sameWorld=cell==observer or cell and observer and cell.isExterior and observer.isExterior
                if valid and sameWorld and (not types.Actor.isDead or not types.Actor.isDead(actor)) then self.objects[self:key(r)]=actor end
                if self.objects[self:key(r)] then self.targetActors[self:key(r)]={actor=actor,instrument=r.instrument} end
                local group=i.outputs and i.outputs[slot]
                if group and (not actor or self:source(r)~=group.source) then group.audio:close();i.outputs[slot]=nil;group=nil end
                local visible=actor~=self.self or camera.getMode()~=camera.MODE.FirstPerson
                if visible and actor and self:source(r) and anim.visualAttachmentsVersion==1 then
                    local key=self:key(r);local p=self.poses[key]
                    local requested=r.kind=='npc' and r.posture=='seated' and (r.instrument=='keyboard' or r.instrument=='cello') and r.instrument..'_seated' or r.instrument
                    local profile=self:profile(requested) or profiles.lute
                    local missing=not self:hasModel(r.instrument)
                    if vfs.fileExists and not vfs.fileExists(profile.model) then profile=profiles.lute end
                    if p and (p.actor~=actor or p.instrument~=r.instrument or p.requestedProfile~=requested) then self:clear(key);p=nil end
                    if not p then p={actor=actor,instrument=r.instrument,requestedProfile=requested,pose=Performance.new(actor),refresh=-10};self.poses[key]=p end
                    if profile.kind=='drumkit' then
                        if p.kitSource~=profile then
                            p.kitSource=profile
                            local complete=(anim.maxVisualAttachments or 8)>=12
                                and (not vfs.fileExists or vfs.fileExists(profiles.kitStageModel))
                            for _,part in ipairs(profiles.kitParts) do
                                if vfs.fileExists and not vfs.fileExists(part.model) then complete=false end
                            end
                            if complete then
                                p.kitProfile={}
                                for field,value in pairs(profile) do p.kitProfile[field]=value end
                                p.kitProfile.animatedKit=true
                                p.kitProfile.model=profiles.kitStageModel
                            else p.kitProfile=nil end
                        end
                        profile=p.kitProfile or profile
                    end
                    p.profileKey=requested
                    if profile.seated and p.pose and not Performance.canSeat(p.pose.bones) then
                        profile=self:profile(r.instrument);p.profileKey=r.instrument;p.seatedUnsupported=true
                    end
                    self.targetActors[key].instrument=p.profileKey
                    p.missingModel=missing
                    active[key]=true
                    local playing=group and (group.audio.playing or group.audio.voices>0)
                    local cadence=playing and 1/30 or .15
                    if now-(p.frameAt or -10)>=cadence then
                        if not p.pose then p.pose=Performance.new(actor) end
                        local torso=anim.getBoneTransform(actor,'Bip01 Spine2',true,true)
                        local root=anim.getBoneTransform(actor,'Bip01',true,true)
                        if torso and root then
                            local strings=profile.kind=='guitar' or profile.kind=='bass' or profile.kind=='lute'
                            local _,yaw=actor.rotation:getAnglesXZ();local facing=strings and T.rotateZ(yaw) or actor.rotation
                            local position=profile.anchor=='feet' and actor.position or torso*v(0,0,0)
                            if profile.anchor~='feet' then
                                -- Do not feed the animated chest back into the
                                -- attachment each frame: idle/pose updates can
                                -- otherwise make a strapped instrument twitch.
                                p.chestAnchor=p.chestAnchor or facing:inverse()*(position-actor.position)
                                position=actor.position+facing*p.chestAnchor
                            end
                            local source={position=position,rotation=facing}
                            if p.pose and not p.pose.missing then
                                p.pose:frame(math.min(.2,now-(p.frameAt or now-dt)),source,playing and group.audio:notes() or {},now,group and group.audio.strikes or 0,profile,group and group.audio.performance and group.audio:performance() or nil,i.beatClock,self.previewInstrument==requested)
                            end
                            local transform=root:inverse()*(p.pose and p.pose.propTransform or T.identity)*T.move(position)*facing*T.move(profile.position)*profile.rotation
                            p.propPosition=(T.move(position)*facing*T.move(profile.position))*v(0,0,0)
                            anim.setVisualAttachment(actor,'synth_instrument',profile.model,{bone='Bip01',transform=transform,timeout=.6})
                            if profile.animatedKit then
                                for _,part in ipairs(profiles.kitParts) do
                                    local motion=profiles.kitPartTransform(part,p.pose and p.pose.drumMotion)
                                    anim.setVisualAttachment(actor,part.key,part.model,{bone='Bip01',transform=transform*motion,timeout=.6})
                                end
                            end
                            for _,extra in ipairs(profile.extras or {}) do
                                local attachment
                                if extra.hand and p.pose and p.pose.targets[extra.hand] then
                                    attachment=root:inverse()*T.move(p.pose.targets[extra.hand])*actor.rotation*T.move(extra.position or v(0,0,0))*(extra.rotation or T.identity)
                                    if profile.kind=='drumkit' and p.pose.stickRotations and p.pose.stickRotations[extra.hand] then
                                        attachment=root:inverse()*T.move(p.pose.targets[extra.hand])*actor.rotation*p.pose.stickRotations[extra.hand]*T.move(extra.position or v(0,0,0))*(extra.rotation or T.identity)
                                    end
                                    if extra.grip then attachment=attachment*T.move(v(0,0,0)-extra.grip) end
                                elseif extra.position then
                                    local travel=extra.bow and p.pose and p.pose.bowTravel or 0
                                    local offset=extra.position+extra.rotation*v(travel,0,0)
                                    attachment=transform*T.move(offset)*extra.rotation
                                end
                                if attachment and (not vfs.fileExists or vfs.fileExists(extra.model)) then
                                    anim.setVisualAttachment(actor,extra.key,extra.model,{bone='Bip01',transform=attachment,timeout=.6})
                                end
                            end
                            if r.kind=='npc' or r.instrument=='drumkit' then
                                local furniture={}
                                if profile.seated and p.pose and not p.seatedUnsupported then furniture[#furniture+1]={key='synth_chair',file='stool',position=profile.bench.position,rotation=profile.bench.rotation,scale=profile.bench.scale} end
                                -- Keyboards already include their own music rest and pages.
                                if r.instrument~='keyboard' then
                                    local stand=profile.musicStand or v(38,60,0)
                                    furniture[#furniture+1]={key='synth_music_stand',file='music-stand',position=stand,rotation=T.rotateZ(math.atan2(-stand.x,-stand.y))}
                                end
                                for _,prop in ipairs(furniture) do
                                    local model='meshes/fetcher/orchestra/'..prop.file..'.nif'
                                    if not vfs.fileExists or vfs.fileExists(model) then
                                        local scale=prop.scale or 1
                                        anim.setVisualAttachment(actor,prop.key,model,{bone='Bip01',transform=root:inverse()*T.move(actor.position)*actor.rotation*T.move(prop.position)*prop.rotation*T.scale(scale,scale,scale),timeout=.6})
                                    end
                                end
                            end
                        end
                        p.frameAt=now
                    end
                end
            end
        end
    end
    for key in pairs(self.poses) do if not active[key] then self:clear(key) end end
    local conducting={};local busy={}
    for _,target in pairs(self.targetActors) do busy[target.actor]=true end
    for id,i in pairs(instances) do
        local actor=i.conductor and i.leaderPlays==false and i.owner and self.mp.getPlayerObject and self.mp.getPlayerObject(i.owner)
        local cell=actor and actor:isValid() and actor.cell;local observer=self.self.cell
        local visible=actor~=self.self or camera.getMode()~=camera.MODE.FirstPerson
        if cell and visible and not busy[actor] and (cell==observer or cell.isExterior and observer and observer.isExterior)
            and (not types.Actor.isDead or not types.Actor.isDead(actor)) then
            local c=self.conductors[id]
            if c and c.actor~=actor then c:clear();self.conductors[id]=nil;c=nil end
            if not c then c=Conductor.new(actor);self.conductors[id]=c end
            if c then conducting[id]=true;c.active=c:frame(dt,i.beatClock,now) end
        end
    end
    for id,c in pairs(self.conductors) do if not conducting[id] then c:clear();self.conductors[id]=nil end end
end
function A:status()
    local result={}
    for key,p in pairs(self.poses) do local axes=0;for _,axis in pairs(p.pose and p.pose.fingerAxes or {}) do if axis then axes=axes+1 end end
        result[#result+1]={key=key,instrument=p.instrument,profile=p.profileKey,seated=p.pose and p.pose.seated==true,seatedUnsupported=p.seatedUnsupported,bones=p.pose and p.pose.applied or 0,fingerAxes=axes,
        fret=p.pose and p.pose.fretting and p.pose.fretting.position,stickLeft=p.pose and p.pose.stickPulse and p.pose.stickPulse.L,stickRight=p.pose and p.pose.stickPulse and p.pose.stickPulse.R,footLeft=p.pose and p.pose.pedalMotion and p.pose.pedalMotion.L,footRight=p.pose and p.pose.pedalMotion and p.pose.pedalMotion.R,
        fingers=p.pose and p.pose.fingers or 0,error=p.pose and p.pose.error or 0,reachError=p.pose and p.pose.reachError or 0,legReachError=p.pose and p.pose.legReachError or 0,missingModel=p.missingModel,
        body=p.pose and p.pose.bodyMotion,propPosition=p.propPosition,windRest=p.pose and p.pose.windRest,
        bowTravel=p.pose and p.pose.bowTravel,drumMotion=p.pose and p.pose.drumMotion,
        attachments=anim.visualAttachmentCount and anim.visualAttachmentCount(p.actor) or 0} end
    for id,c in pairs(self.conductors) do if c.active then result[#result+1]={key='conductor:'..id,instrument='conductor',bones=c.pose.applied,body=c.pose.bodyMotion,reachError=c.pose.reachError} end end
    return result
end
return A
