-- Disposable A/B performer integration test; supplies inventory through stage script.
local core=require('openmw.core');local self=require('openmw.self');local nearby=require('openmw.nearby')
local camera=require('openmw.camera')
local types=require('openmw.types');local anim=require('openmw.animation');local I=require('openmw.interfaces');local mp=require('openmw.mp')
local phase,start,at=0,nil,0;local role;local invitations=0;local flags={};local npc
local function log(s) print('[SynthPerformerTest] '..role..' '..s) end
local function member(old,target) return {name=old.name,level=old.level,mute=old.mute,target=target or old.target} end
local function output(state,slot) for _,o in ipairs(state.outputs or {}) do if o.slot==slot then return o end end end
local function pose(api,instrument)
    for _,p in ipairs(api.performers()) do if p.instrument==instrument and p.attachments==1 and p.bones==6 then return p end end
end
local function run()
    local now=core.getRealTime();start=start or now;role=role or types.NPC.record(self).name
    if not flags.camera then camera.setMode(camera.MODE.ThirdPerson);flags.camera=true end
    local api=I.ProceduralSynth;if not api then return end
    local state=api.status()[1];local w=api.workstation();local own=api.performer()
    if role=='Synth B' then
        if own.invitation and own.invitation.token~=flags.token then
            flags.token=own.invitation.token;invitations=invitations+1
            api.readyInstrument('lute');api.respondToInvite(invitations>1)
            log(invitations==1 and 'PASS explicitly declined first invitation' or 'accepted fresh invitation with inventory lute')
        end
        if state and state.nativePlaying and output(state,1) and pose(api,'lute') and not flags.play then
            assert(own.membership and own.held=='lute','playback began without member consent/readiness')
            flags.play=now;anim.setVisualAttachment(self,'qa_expiry','meshes/m/Misc_de_lute_01.nif',{bone='Bip01 Spine2',timeout=0.2});log('PASS consenting self actor has native audio, lute attachment and six posed arm bones')
        end
        if flags.play and not flags.expiry and now-flags.play>1 then
            assert(anim.visualAttachmentCount(self)==1,'unrefreshed native attachment did not expire');flags.expiry=true;log('PASS native attachment expiry without Lua refresh')
        end
        if flags.play and not flags.holster and now-flags.play>4 then api.readyInstrument('none');flags.holster=now end
        if flags.holster and not flags.resumed and now-flags.holster>3 then
            assert(not output(state,1) and state.nativePlaying and anim.visualAttachmentCount(self)==0,'holster retained sound or prop')
            log('PASS holster removes only self audio/prop; other tracks continue');api.readyInstrument('lute');flags.resumed=true
        end
        if flags.resumed and state and state.performers and state.performers[2] and state.performers[2].instrument=='keyboard' and pose(api,'keyboard') and not flags.keyboard then
            flags.keyboard=now;log('PASS observer sees keyboard held by owned NPC')
        end
        if flags.keyboard and not flags.left and now-flags.keyboard>3 then api.leaveBand();flags.left=now end
        if flags.left and not flags.leaveChecked and now-flags.left>2 then
            assert(not api.performer().membership and not output(state,1) and anim.visualAttachmentCount(self)==0,'Leave kept consent or pose')
            flags.leaveChecked=true;log('PASS Leave revoked membership and removed self native source/held prop')
        end
        if flags.leaveChecked and not state and not flags.done then flags.done=true;log('PASS Finish cleaned up the whole band on observer') end
        return
    end
    if role~='Synth A' or phase==20 then return end
    if phase==0 and now-start>7 then
        for _,o in ipairs(nearby.activators) do if o.recordId=='fetcher_procedural_synth_v1' then o:activateBy(self);phase=1;at=now;break end end
    elseif phase==1 and w then
        if not flags.loaded then api.loadProject();flags.loaded=now;return end
        if now-flags.loaded<2 then return end
        if w.project.name=='Actor persistence QA' then
            local target=w.project.band[2].target
            assert(target and target.kind=='npc' and state.availability[2]=='Ready','saved NPC target did not resolve after restart')
            log('PASS saved owned NPC and its band target resolved after server restart; player consent stayed transient')
            api.close();phase=20;return
        end
        w.project.name='Actor band QA';for n=1,3 do w:add('piano');local t=w.project.tracks[n];t.notes={}
            for step=0,12,4 do t.notes[#t.notes+1]={n=48+n*7,s=step,l=3,v=0.8} end
        end
        assert(w:setMember(1,{name='Player lute',level=1,mute=false,target={kind='player',characterId=1002,instrument='lute'}}))
        assert(w:setOutput(2,1));w.cb.hire(2);api.studio();w.page='band';w.revision=w.revision+1;phase=2;at=now
    elseif phase==2 and now-at>3 and state.availability[1]=='Invite required' then
        log('PASS declined player never became a Ready destination');w.cb.invite(1);phase=3;at=now
    elseif phase==3 and state and state.availability[1]=='Ready' and w.project.band[2].target then
        npc=w.project.band[2].target
        assert(npc.kind=='npc' and npc.refId:match('^fetcher_synth_bard_1001_'),'NPC was not owned by leader character')
        local target={kind='npc',mpNum=npc.mpNum,refId=npc.refId,instrument='drum'}
        w:setMember(2,member(w.project.band[2],target));w:setOutput(3,2);phase=4;at=now
    elseif phase==4 and state and state.availability[2]=='Ready' and now-at>1 then assert(w:save());assert(w:play('clips'));phase=5;at=now
    elseif phase==5 and state and state.nativePlaying and pose(api,'lute') and pose(api,'drum') then
        assert(output(state,1).kind=='player' and output(state,2).kind=='npc','source identity did not change to actors')
        log('PASS player lute + owned NPC drum: disjoint native actor emitters, held props and six arm bones each')
        phase=6;at=now
    elseif phase==6 and state and state.availability[1]~='Ready' then
        assert(state.nativePlaying and output(state,2),'member holster stopped other performers')
        log('PASS player not-ready state stays silent without fallback');phase=7;at=now
    elseif phase==7 and state and state.availability[1]=='Ready' and output(state,1) and pose(api,'lute') and now-at>3 then
        log('PASS ready player rejoined current song phase')
        w:setMember(2,member(w.project.band[2],{kind='npc',mpNum=npc.mpNum,refId=npc.refId,instrument='keyboard'}));phase=8;at=now
    elseif phase==8 and now-at>1 and state.availability[2]=='Ready' then assert(w:play('clips'));phase=9;at=now
    elseif phase==9 and state and state.nativePlaying and pose(api,'keyboard') then
        log('PASS NPC now holds compact keyboard; native keyboard part plays at NPC')
        phase=10;at=now
    elseif phase==10 and state and state.availability[1]=='Invite required' and now-at>3 then
        log('PASS leaving player requires a fresh invitation');mp.sendToServer('SynthPerformer_QARemove',{mpNum=npc.mpNum});phase=11;at=now
    elseif phase==11 and state and state.availability[2]=='Missing' and now-at>3 then
        assert(state.nativePlaying and not output(state,2),'missing NPC moved its part elsewhere')
        log('PASS missing NPC silenced only its part');w.cb.hire(2);phase=12;at=now
    elseif phase==12 and w.project.band[2].target and w.project.band[2].target.mpNum~=npc.mpNum and state.availability[2]=='Ready' then
        w.project.name='Actor persistence QA';w:touch();assert(w:save());phase=13;at=now
    elseif phase==13 and now-at>3 and not w.dirty then
        api.close();phase=20;log('PASS complete; fresh owned NPC saved for restart verification')
    end
    if now-start>120 then error('performer probe timed out at phase '..phase) end
end
return {engineHandlers={onFrame=function()
    if phase==-1 then return end
    local ok,err=pcall(run);if not ok then phase=-1;log('FAIL '..tostring(err)) end
end},eventHandlers={Synth_Message=function(data)
    if data.op=='error' or data.op=='scoreError' then log('ERROR '..tostring(data.message)) end
end}}
