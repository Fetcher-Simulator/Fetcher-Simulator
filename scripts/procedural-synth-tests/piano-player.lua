-- Disposable mouse/keyboard QA fixture; never saves the generated song.
local core=require('openmw.core');local self=require('openmw.self');local nearby=require('openmw.nearby')
local types=require('openmw.types');local I=require('openmw.interfaces');local input=require('openmw.input')
local start,ready,lastCount,role=nil,false,nil,nil
local function log(text) print('[SynthPianoTest] '..(role or '?')..' '..text) end
return {engineHandlers={onFrame=function()
    local now=core.getRealTime();start=start or now;role=role or types.NPC.record(self).name
    if role~='Synth A' then return end
    local api=I.ProceduralSynth;if not api then return end
    if not ready and now-start>6 then
        local w=api.workstation()
        if not w then
            for _,list in ipairs{nearby.activators,nearby.items} do for _,o in ipairs(list) do
                if o.recordId=='fetcher_procedural_synth_v1' or o.recordId=='fetcher_procedural_keyboard_v1' then o:activateBy(self);return end
            end end
        else
            api.studio();assert(w:newSong(120,4,4,8));assert(w:addNote(1,60,0));assert(w:addNote(1,64,4))
            w.selected=1;w.base=60;w.page='piano';w.revision=w.revision+1;ready=true;lastCount=2
            log('READY two notes; native mouse/keyboard inspection; no project save')
        end
    elseif ready then
        local w=api.workstation();if w then local notes=w:noteData(1);local count=notes and #notes or 0
            if count~=lastCount then log('notes='..count..' editing='..tostring(api.status()[1].editing));lastCount=count end
        end
    end
end,onKeyPress=function(e) if role=='Synth A' and e.code==input.KEY.Escape then log('native Escape received') end end}}
