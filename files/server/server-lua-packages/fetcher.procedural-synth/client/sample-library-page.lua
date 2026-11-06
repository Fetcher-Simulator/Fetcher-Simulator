local ui=require('openmw.ui');local util=require('openmw.util');local async=require('openmw.async')
local Dropdown=require('scripts.multiplayer.fetcher.procedural-synth.client.dropdown')
local v=util.vector2;local white=ui.texture{path='white'}
local function escaped(s) return tostring(s or ''):gsub('#','##') end
local function label(text,x,y,width,accent) return {type=ui.TYPE.Text,props={position=v(x,y),size=v(width,25),text=escaped(text),textSize=13,textColor=accent and util.color.rgb(.2,.85,.72) or util.color.rgb(.85,.89,.94)}} end
return function(owner,content)
    local w=owner.w;local library=w.sampleLibrary;local browser=owner:sampleBrowser()
    if not library then content[#content+1]=label('Sample library unavailable',20,180,870);return end
    local function button(text,x,y,width,fn,on) content[#content+1]=owner:button(text,x,y,width,fn,on) end
    content[#content+1]=label('PERSONAL SAMPLES + SLICE KITS',20,171,650,true)
    button('Back to editor',725,166,165,function() w.page='samples';owner:setSampleFocus(owner.libraryReturnFocus==true);w.revision=w.revision+1 end)
    local selected=library.selected
    button(selected and (selected.recipe.kind=='kit' and 'Kit: ' or 'Sample: ')..escaped(selected.name)..' v' or 'Choose saved sample / kit v',20,218,680,function()
        local entries={};for _,entry in ipairs(library.entries) do entries[#entries+1]={id=entry.id,label=(entry.kind=='kit' and 'Kit / ' or 'Sample / ')..escaped(entry.name)..' / '..entry.slices..' slices',search=entry.name..' '..entry.kind} end
        Dropdown.open(owner,entries,selected and selected.id,function(id) library:open(id,browser) end,20,252,870)
    end)
    button('Refresh',720,218,170,function() library:list() end)
    content[#content+1]=label('Name for saving / renaming',20,263,700)
    content[#content+1]=owner:element{type=ui.TYPE.TextEdit,template={content=ui.content{{type=ui.TYPE.Image,props={relativeSize=v(1,1),resource=white,color=util.color.rgb(.14,.17,.21)}}}},
        props={position=v(20,290),size=v(870,28),text=library.name,textSize=14,textColor=util.color.rgb(.85,.89,.94)},events={
        textChanged=async:callback(function(text) library.name=text:sub(1,64);library.deleteArmed=nil;return false end),
        focusGain=async:callback(function() owner.typing=true;return false end),focusLoss=async:callback(function() owner.typing=false;return false end)}}
    button('Save new sample',20,335,175,function() library:save(browser,'sample',false) end)
    button('Save new kit',205,335,150,function() library:save(browser,'kit',false) end)
    button('Update saved',365,335,155,function() library:save(browser,nil,true) end)
    button('Rename saved',530,335,160,function() library:rename() end)
    button(library.deleteArmed and 'Confirm delete' or 'Delete saved',700,335,190,function() library:delete() end)
    content[#content+1]=label(library.status,20,382,870,true)
    local info=browser:info()
    content[#content+1]=label((browser.path or 'Choose a source on Samples or load a saved entry.'):sub(1,125),20,420,870)
    owner.librarySampleStatus=owner:element(label('Draft: '..info.status,20,449,870));content[#content+1]=owner.librarySampleStatus
    button('Preview',20,490,130,function() if not browser:audition() then library:changed('Wait for a matching Ready sample before previewing.') end end)
    button('Release',165,490,130,function() browser:release() end)
    button('Stop preview',310,490,145,function() browser:stop() end)
    button('Use sample on part',470,490,200,function()
        local ref=browser:reference();if ref then
            if not w.project.tracks[w.selected] then library:changed('Create or select a part in the Mixer before assigning a sample.');return end
            browser:stop();w:setSample(w.selected,ref)
        else library:changed('Wait for Ready before assigning the sample.') end
    end)
    local function create(arrange)
        local view=browser.editView;local refs,err=browser:slices(view and view.points or {})
        if not refs then library:changed(err or 'Load a Ready kit, or add markers in the waveform editor first.');return end
        browser:stop();w:addSampleSlices(refs,library.name~='' and library.name or 'Sample kit',arrange)
    end
    button('Current kit to drum lanes',20,545,265,function() create(false) end)
    button('Current kit to Arrange phrase',300,545,300,function() create(true) end)
    button('Open waveform editor',615,545,275,function() w.page='samples';owner:setSampleFocus(true) end)
    content[#content+1]=label('Entries belong to this character on this server. Sound files must be installed and match their saved hash.',20,598,880)
    content[#content+1]=label('Save stores the applied trim/loop and kit markers. Reusing an entry changes the draft; apply it to edit a part.',20,629,880)
    content[#content+1]=label('New sample/kit always saves a copy. Update saved replaces only the opened entry, with revision checks.',20,660,880)
end
