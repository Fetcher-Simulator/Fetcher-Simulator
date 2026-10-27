-- Owned Studio performers stand in their server-selected formation, even at rest.
local self=require('openmw.self')
if not self.recordId:match('^fetcher_synth_bard_%d+_%d+$') then return {} end
local function stand()
    self:enableAI(false)
    local c=self.controls
    c.movement=0;c.sideMovement=0;c.yawChange=0;c.pitchChange=0
    c.run=false;c.sneak=false;c.jump=false;c.use=0
end
return {engineHandlers={onUpdate=stand}}
