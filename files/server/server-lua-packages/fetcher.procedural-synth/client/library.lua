-- Named saves and bounded server recovery are independent of audio playback.
local format=require('scripts.multiplayer.fetcher.procedural-synth.shared.project')(require('scripts.multiplayer.fetcher.procedural-synth.shared.schema'))
local L={};L.__index=L
function L.new(w,cb)
    local self=setmetatable({w=w,cb=cb,entries={},recoveries={},active=0,revision=0,serial=0,document=0,lastAuto=cb.now(),status='Loading project library...'},L)
    w.library=self;self:detach();return self
end
function L:changed(text) if text then self.status=text end;self.w.revision=self.w.revision+1 end
function L:detach()
    self.active=0;self.revision=0;self.document=self.document+1
    self.token=tostring(self.cb.now())..'-'..self.document..'-'..tostring(self.cb.session or 0)
    self.autosaved=-1;self.lastAuto=self.cb.now()
end
function L:request(op,data,done)
    if self.pending then self:changed('Please wait for the current project request.');return false end
    self.serial=self.serial+1;data=data or {};data.requestId=self.serial
    self.pending={id=self.serial,op=op,generation=self.w.generation,document=self.document,at=self.cb.now(),done=done}
    self:changed('Working...');self.cb.send(op,data);return true
end
function L:list() return self:request('libList') end
function L:guard(action,caption)
    self.w:endAutomationWrite()
    if self.pending then self:changed('Please wait for the current project request.');return false end
    if self.w.recording or self.w.failedTake then self:changed('Keep or cancel the take first.');self.cb.message(self.status);return false end
    if not self.w.dirty then action();return true end
    self.action=action;self.actionCaption=caption;self.returnPage=self.w.page;self.w.page='projects';self:changed('Unsaved changes: '..caption);self.cb.show();return false
end
function L:cancel() self.action=nil;self.w.page=self.returnPage or 'projects';self:changed('Cancelled; current project kept.') end
function L:continue(saved)
    if saved and self.action then local action=self.action;self.action=nil;self.w.page=self.returnPage or 'song';action() end
end
function L:save(asNew,name)
    self.w:endAutomationWrite()
    if self.w.recording or self.w.failedTake then self:changed('Keep or cancel the take before saving.');return false end
    local project=format.project(self.w.project);if not project then self:changed('Invalid project; save cancelled.');return false end
    if name then project.name=name end
    return self:request('libSave',{project=project,projectId=asNew and 0 or self.active,projectRevision=self.revision,saveRevision=self.w.generation},function(data,p)
        self.active=data.projectId;self.revision=data.projectRevision
        local same=self.w.generation==p.generation
        if same then
            if name then self.w.project.name=name;self.w.checkpoint.name=name end
            self.w.dirty=false
        end
        self:changed(same and 'Project saved.' or 'Snapshot saved; newer edits remain unsaved.')
        self:continue(same)
    end)
end
function L:snapshot(continue)
    if self.w.recording or self.w.failedTake or self.w.automationWriting then return false end
    local project=format.project(self.w.project);if not project then self:changed('Invalid project; recovery not written.');return false end
    return self:request('libSnapshot',{project=project,projectId=self.active,token=self.token,saveRevision=self.w.generation},function(data,p)
        self.autosaved=p.generation;self:changed('Recovery draft stored; named save unchanged.')
        if continue then self:continue(self.w.generation==p.generation) end
    end)
end
function L:open(id,recovery,approved)
    local function open()
        self:request(recovery and 'libRecover' or 'libOpen',recovery and {recoveryId=id} or {projectId=id},function(data,p)
            if self.w.generation~=p.generation then self:changed('Newer edits kept; retry opening the project.');return end
            if not self.w:load(data.project) then self:changed('Unsupported project; current edits kept.');return end
            self.active=data.projectId;self.revision=data.projectRevision or 0
            if recovery then self.w.dirty=true end
            self.w.page='song';self:changed(recovery and 'Recovered as an unsaved copy. Use Save As to keep it.' or 'Project opened.')
        end)
    end
    if approved then open();return true end
    return self:guard(open,recovery and 'restore recovery draft' or 'open another project')
end
function L:rename(entry,name)
    return self:request('libRename',{projectId=entry.id,projectRevision=entry.revision,name=name},function(data)
        if self.active==data.projectId then
            self.revision=data.projectRevision;local clean=not self.w.dirty
            self.w.project.name=data.name;self.w:touch('name');if clean then self.w.dirty=false end
        end
        self:changed('Project renamed.')
    end)
end
function L:delete(entry)
    return self:request('libDelete',{projectId=entry.id,projectRevision=entry.revision},function(data)
        if self.active==data.projectId then self:detach();self.w.dirty=true end
        self:changed('Named project deleted; any open copy stays in memory.')
    end)
end
function L:receive(data)
    local pending=self.pending
    if not pending or pending.id~=data.requestId then return end
    self.pending=nil
    if data.message then self:changed(data.message);self.cb.message(data.message);return end
    if data.entries then self.entries=data.entries;self.recoveries=data.recoveries or {};self.ready=true end
    if pending.document~=self.document then self:changed('Library refreshed; current edits kept.');return end
    if pending.done then pending.done(data,pending) else self:changed('Library ready. Autosave keeps the three most recent drafts.') end
end
function L:frame(now)
    if self.pending and now-self.pending.at>10 then self.pending=nil;self:changed('Project request timed out; edits kept. Refresh before retrying a save.');self.lastAuto=now end
    if not self.pending and now-self.lastAuto>=30 then
        self.lastAuto=now
        if self.w.dirty and self.autosaved~=self.w.generation then self:snapshot(false) end
    end
end
return L
