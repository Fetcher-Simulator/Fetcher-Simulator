-- Opt-in private-fixture driver. Never include in the shipped manifest.
local core=require('openmw.core');local self=require('openmw.self');local nearby=require('openmw.nearby')
local I=require('openmw.interfaces');local synth=require('openmw.synth')
local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local Format=require(prefix..'shared.project')(require(prefix..'shared.schema'))
local Studio=require(prefix..'client.studio');local Dropdown=require(prefix..'client.dropdown')
local view
local phase,started,at,root,meter,picker,revision=0,nil,0,nil,nil,nil,nil
local function log(s) print('[StudioUIRefreshQA] '..s) end
local function song()
    local s={tracks=1,division=96,names={},programs={['1:0']=30},tempos={},duration=40,beats=80,events={}}
    for n=0,79 do for _,kind in ipairs{'on','off'} do
        local beat=n+(kind=='off' and .7 or 0)
        s.events[#s.events+1]={kind=kind,part='1:0',track=1,channel=0,note=48+n%3*4,velocity=.7,tick=beat*96,time=beat*.5}
    end end
    return s
end
local function run()
    local now=core.getRealTime();started=started or now;assert(now-started<60,'UI refresh check timed out')
    synth.setMonitorVolume(0)
    local api=I.ProceduralSynth;if not api then return end
    if view and not view.uiClosed then view:frame(now) end
    local w=api.workstation()
    if phase==0 and now-started>4 then
        if not w then for _,o in ipairs(nearby.activators) do if o.recordId=='fetcher_procedural_synth_v1' then o:activateBy(self);break end end;return end
        local library=api.projectLibrary();if not library or not library.ready or library.pending then return end
        assert(w:load(Format.new()));assert(w:import(song(),'midi/private-ui-refresh.mid'));assert(w:convertPart(1))
        w.project.autoBand=false;w.page='mixer';api.hide()
        -- Script sandboxes have independent module tables. Use the real Studio
        -- class with the live workstation rather than monkey-patching a module
        -- in this driver and expecting the player script to share that table.
        view=Studio.new(w,{midiLabel=function() return 'UI refresh QA' end,
            hide=function() error('unexpected UI dismissal') end})
        phase=1;at=now
    elseif phase==1 and view and now-at>1 then
        assert(w:play('song'));phase=2;at=now
    elseif phase==2 and now-at>2 then
        assert(api.status()[1].nativePlaying,'Native music not playing')
        root=view.root;meter=view.meterLabels[1];revision=w.revision
        local entries={};for n=1,40 do entries[n]={id=n,label='Guitar '..n} end
        picker=Dropdown.open(view,entries,nil,function() end,30,140,600)
        picker.query='Guitar';picker:scrollTo(10)
        assert(api.projectLibrary():snapshot(false),'Recovery snapshot did not start')
        phase=3;at=now
    elseif phase==3 then
        assert(view.dropdown==picker and picker.active and view.root==root,'Background refresh dismissed dropdown')
        assert(picker.query=='Guitar' and picker.offset==10,'Dropdown lost search or scroll')
        assert(w:setMaster(.4+(math.floor((now-at)*10)%4)*.1))
        assert(view.meterLabels[1]==meter,'Master gain rebuilt meter text')
        if now-at>3 and not api.projectLibrary().pending then
            assert(w.revision>revision,'No asynchronous background revision was exercised')
            assert(api.status()[1].nativePlaying and api.status()[1].nativeFault==0,'Native playback failed during gain/menu updates')
            Dropdown.close(view);phase=4;at=now;log('PASS popup/filter/scroll and mixer rows survive native playback, master changes and server recovery reply')
        end
    elseif phase==4 and now-at>.5 then
        assert(view.root~=root and view.revision==w.revision,'Deferred rebuild not applied after popup dismissal')
        w:stop(true);view:destroy();api.close();phase=99;log('PASS complete: deferred refresh and sounding native transport');core.quit()
    end
end
return {engineHandlers={onFrame=function()
    if phase==99 then return end
    local ok,err=pcall(run);if not ok then log('FAIL '..tostring(err));phase=99;core.quit() end
end}}
