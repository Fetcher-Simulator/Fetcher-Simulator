-- Opt-in private-fixture acceptance driver. Never ship in the package manifest.
local core=require('openmw.core');local self=require('openmw.self');local nearby=require('openmw.nearby')
local types=require('openmw.types');local I=require('openmw.interfaces');local synth=require('openmw.synth')
local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local schema=require(prefix..'shared.schema');local Format=require(prefix..'shared.project')(schema)
local Sets=require(prefix..'client.soundsets')
local phase,started,at,role,index,entry=0,nil,0,nil,1,nil
local function log(s) print('[StudioBandSoundQA] '..tostring(role)..' '..s) end
local function song()
    local s={tracks=14,division=96,names={},programs={},tempos={},duration=80,beats=160,events={}}
    local parts={{0,52,24},{1,52,25},{2,52,30},{3,40,34}}
    for _,lane in ipairs(schema.kits[6].lanes) do parts[#parts+1]={9,lane.note} end
    for n,p in ipairs(parts) do
        local part=n..':'..p[1]..(p[1]==9 and ':'..p[2] or '');s.programs[part]=p[3]
        for hit=0,159 do for _,kind in ipairs{'on','off'} do
            local beat=hit+(kind=='off' and .6 or 0)
            s.events[#s.events+1]={kind=kind,part=part,track=n,channel=p[1],note=p[2],velocity=.6+(hit%3)*.1,tick=beat*96,time=beat*.5}
        end end
    end
    table.sort(s.events,function(a,b) return a.time==b.time and a.track<b.track or a.time<b.time end)
    return s
end
local function run()
    local now=core.getRealTime();started=started or now;role=role or types.NPC.record(self).name
    assert(now-started<150,'Band sound acceptance timed out')
    synth.setMonitorVolume(0);local api=I.ProceduralSynth;if not api then return end
    local w=api.workstation();local state=api.status()[1]
    if role=='Synth B' then
        if phase==0 and state and #state.channels>=14 and #api.performers()==5 then
            local strings=0;for _,c in ipairs(state.channels) do if c.id<=4 and c.instrumentType==1 then strings=strings+1 end end
            if strings==4 and state.voices>0 and state.nativeFault==0 then phase=99;log('PASS late observer: four string families, full kit, five performers and sounding native outputs') end
        end;return
    end
    if phase==0 and now-started>6 then
        if not w then for _,o in ipairs(nearby.activators) do if o.recordId=='fetcher_procedural_synth_v1' then o:activateBy(self);break end end;return end
        local library=api.projectLibrary();if not library or not library.ready or library.pending then return end
        assert(w:load(Format.new()));assert(w:import(song(),'midi/private-band-sounds.mid'))
        assert(w.project.tracks[1].presetId=='b:guitar-nylon' and w.project.tracks[2].presetId=='b:guitar-steel' and w.project.tracks[3].presetId=='b:guitar-metal' and w.project.tracks[4].presetId=='b:bass-picked','Initial sound identities differ')
        for n=1,14 do assert(w:convertPart(n)) end;w.song=nil
        w.project.leaderPlays=false;assert(w:setupNpcBand(false));phase=1;at=now
    elseif phase==1 and not w.bandSetup and #api.performers()==5 then
        assert(w:play('song'));api.hide();phase=2;at=now;log('PASS source-free 14-part band starts with one shared drummer')
    elseif phase==2 and now-at>2 then
        if not state or not state.nativePlaying then return end
        assert(state.nativeFault==0 and #api.performers()==5 and w.project.tracks[4].params.tune1==0,'Sound cycling broke native playback or MIDI bass octave')
        assert(w.project.tracks[1].presetId=='b:guitar-nylon' and w.project.tracks[2].presetId=='b:guitar-steel','Sound set replaced acoustic guitar identity')
        index=index+1
        if index<=#Sets.entries then assert(w:applySoundSet(Sets.entries[index].id));at=now;log('PASS live set '..Sets.entries[index].name)
        else assert(api.projectLibrary():save(true,'Band sounds QA'));phase=3;at=now end
    elseif phase==3 and now-at>2 and not api.projectLibrary().pending and not w.dirty then
        entry=assert(api.projectLibrary().active);assert(api.projectLibrary():open(entry,false,true));phase=4;at=now
    elseif phase==4 and now-at>3 and not api.projectLibrary().pending then
        assert(w.project.tracks[1].sourceProgram==24 and w.project.tracks[2].sourceProgram==25 and w.project.tracks[4].sourceProgram==34,'Server library dropped source program metadata')
        assert(not w:needsMidi() and w:applySoundSet('thrash') and w:play('song'),'Source-free saved band could not replay')
        phase=5;at=now;log('PASS server library reload and native replay retain guitar/bass families')
    elseif phase==5 and now-at>5 then
        assert(w.playing and api.status()[1].nativeFault==0);w:stop(true);assert(w:setupNpcBand(false,false,true));phase=6;at=now
    elseif phase==6 and now-at>3 and not w.bandSetup then
        assert(#api.performers()==0,'Band cleanup left NPCs');api.close();phase=99;log('PASS complete: live sound sets, source-free save/load, late observer and cleanup');core.quit()
    end
end
return {engineHandlers={onFrame=function()
    if phase==99 then return end
    local ok,err=pcall(run);if not ok then log('FAIL '..tostring(err));phase=99;core.quit() end
end}}
