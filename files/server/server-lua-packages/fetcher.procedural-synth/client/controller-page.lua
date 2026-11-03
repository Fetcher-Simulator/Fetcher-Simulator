local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local Map=require(prefix..'client.controller-map');local Controls=require(prefix..'client.controls')
local Dropdown=require(prefix..'client.dropdown');local ui=require('openmw.ui');local util=require('openmw.util')
local function text(s,x,y,width)
    return {type=ui.TYPE.Text,props={text=s,position=util.vector2(x,y),size=util.vector2(width,24),textSize=12}}
end
return function(s,content)
    local w=s.w;local r=w.recorder;local map=r.controllers
    local function button(caption,x,y,width,fn,on) content[#content+1]=s:button(caption,x,y,width,fn,on) end
    button('< MIDI mappings',20,163,200,function() w.page='midimapping';w.revision=w.revision+1 end)
    content[#content+1]=text('KNOBS / FADERS — local absolute CC assignments',240,171,650)
    button(r.mapping.enabled and 'Mappings: On' or 'Mappings: Off',20,211,200,function() r:setMapping('enabled',not r.mapping.enabled) end,r.mapping.enabled)
    button('+ Add binding',230,211,180,function() if #map.bindings<Map.maxBindings then map:edit(#map.bindings+1,{}) end end)
    button(w.automationWriting and 'End Auto write' or 'Write synth CCs to Auto',420,211,270,function() r:writeMappings() end,w.automationWriting)
    local pages=math.max(1,math.ceil(#map.bindings/4));local page=math.min(s.controllerPage or 0,pages-1);s.controllerPage=page
    button('Bindings '..(page+1)..'/'..pages..' v',700,211,190,function()
        local entries={};for n=0,pages-1 do entries[#entries+1]={id=n,label='Bindings '..(n*4+1)..'–'..math.min(n*4+4,#map.bindings)} end
        Dropdown.open(s,entries,page,function(n) s.controllerPage=n;w.revision=w.revision+1 end,490,245,400)
    end)
    for slot=page*4+1,math.min(#map.bindings,page*4+4) do
        local b=map.bindings[slot];local y=257+(slot-page*4-1)*87
        button(slot..'  '..Map.targets[b.target].label..' v',20,y,410,function()
            Dropdown.open(s,Map.order,b.target,function(id) map:edit(slot,{target=id}) end,20,math.min(290,y+31),600)
        end)
        local global=b.target=='master' or b.target:find('^effects:')
        button(global and 'All tracks / return' or (b.track==0 and 'Current track / Synth' or 'Track '..b.track)..' v',440,y,230,function()
            if global then return end
            local entries={{id=0,label='Current track / manual Synth'}}
            for n=1,32 do local t=w.project.tracks[n];entries[#entries+1]={id=n,label='Track '..n..(t and (' / '..t.name) or ' / not present')} end
            Dropdown.open(s,entries,b.track,function(n) map:edit(slot,{track=n}) end,440,math.min(290,y+31),450)
        end)
        button(b.pickup and 'Pickup: On' or 'Pickup: Off',680,y,140,function() map:edit(slot,{pickup=not b.pickup}) end,b.pickup)
        button('X',830,y,60,function() map:edit(slot,nil,true) end)
        content[#content+1]=Controls.slider(s,'controller:'..slot,'CC (-1 = off)',{min=-1,max=119,discrete=true,step=1},
            function() return map.bindings[slot].cc end,function(cc) map:edit(slot,{cc=cc}) end,20,y+31,430)
        button(r.learn==slot and 'Listening / cancel' or 'Learn CC',470,y+37,200,function() r:learnMapping(slot) end,r.learn==slot)
        content[#content+1]=text(Map.targets[b.target].automatable and 'Auto capture available' or 'Live / saved value; no Auto lane yet',680,y+41,220)
    end
    s.controllerStatus=s:element(text(map.status or 'Select a target, click Learn CC and move a knob or fader.',20,621,870));content[#content+1]=s.controllerStatus
    content[#content+1]=text('Pickup waits until the physical control reaches the current value. Bindings also work on Synth, Piano and Drums.',20,653,880)
    content[#content+1]=text('Absolute 7-bit CC only; sustain and channel-mode CCs are reserved. Mixer/return edits save with the project.',20,684,880)
end
