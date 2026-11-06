local format=require('scripts.multiplayer.fetcher.procedural-synth.shared.sample-library')(require('scripts.multiplayer.fetcher.procedural-synth.shared.schema'))
local L={};L.__index=L
function L.new(w,cb) local self=setmetatable({w=w,cb=cb,entries={},serial=0,name='',status='Refresh to load your samples and slice kits.'},L);w.sampleLibrary=self;return self end
function L:changed(text) self.status=text or self.status;self.w.revision=self.w.revision+1 end
function L:request(op,data,done)
    if self.pending then self:changed('Wait for the current sample request.');return false end
    if self.uncertain and op~='sampleList' and op~='sampleOpen' then self:changed('Refresh the library after the timeout before saving again.');return false end
    self.deleteArmed=nil;self.serial=self.serial+1;data=data or {};data.requestId=self.serial
    self.pending={id=self.serial,at=self.cb.now(),done=done};self:changed('Working...');self.cb.send(op,data);return true
end
function L:list() return self:request('sampleList') end
function L:open(id,browser)
    local key=browser:draftKey()
    return self:request('sampleOpen',{entryId=id},function(data)
        local entry=data.entry
        if not entry or not format.recipe(entry.recipe) then self:changed('Unsupported saved sample; current draft kept.');return end
        if self.browser~=browser or browser:draftKey()~=key then self:changed('Newer sample edits kept. Select the saved entry again to load it.');return end
        if not browser:loadRecipe(entry.recipe) then self:changed('Saved sample cannot be opened; draft kept.');return end
        self.selected=entry;self.name=entry.name;self:changed('Loaded '..(entry.recipe.kind=='kit' and 'slice kit' or 'sample')..'. Wait for Ready, then preview or apply it.')
    end)
end
function L:save(browser,kind,update)
    if not format.name(self.name) then self:changed('Enter a name of 1-64 bytes.');return false end
    local selected=update and self.selected or nil
    if update and not selected then self:changed('Select and open a saved entry before updating it.');return false end
    if selected then kind=selected.recipe.kind end
    local recipe,err=browser:recipe(kind);if not recipe then self:changed(err);return false end
    return self:request('sampleSave',{entryId=selected and selected.id or 0,entryRevision=selected and selected.revision or 0,name=self.name,recipe=recipe},function(data)
        self.selected=data.entry;self:changed('Saved to this character. The current sample draft was kept.')
    end)
end
function L:rename()
    local selected=self.selected
    if not selected or not format.name(self.name) then self:changed('Open an entry and enter its new name first.');return false end
    return self:request('sampleRename',{entryId=selected.id,entryRevision=selected.revision,name=self.name},function(data) self.selected=data.entry;self:changed('Saved entry renamed.') end)
end
function L:delete()
    local selected=self.selected;if not selected then self:changed('Open the saved entry first.');return false end
    local token=selected.id..':'..selected.revision
    if self.deleteArmed~=token then self.deleteArmed=token;self:changed('Click Confirm delete to remove the saved entry. Your current draft and projects stay intact.');return false end
    return self:request('sampleDelete',{entryId=selected.id,entryRevision=selected.revision},function() self.selected=nil;self:changed('Saved entry deleted; the sample draft and existing projects were kept.') end)
end
function L:receive(data)
    if not self.pending or self.pending.id~=data.requestId then return end
    local pending=self.pending;self.pending=nil
    if data.message then self:changed(data.message);return end
    if data.entries then self.entries=data.entries;self.ready=true end
    self.uncertain=false
    if pending.done then pending.done(data) else self:changed('Sample library ready. Select an entry to preview or reuse it.') end
end
function L:frame(now)
    if self.pending and now-self.pending.at>10 then self.pending=nil;self.uncertain=true;self:changed('Sample request timed out. Refresh before saving again; your draft was kept.') end
end
return L
