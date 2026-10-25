local camera=require('openmw.camera')
local types=require('openmw.types')
local player=require('openmw.self')
local util=require('openmw.util')
local P={};P.__index=P
function P.new()
    local p=setmetatable({mode=camera.getMode(),yaw=camera.getYaw(),pitch=camera.getPitch(),
        roll=camera.getRoll(),extraYaw=camera.getExtraYaw(),extraPitch=camera.getExtraPitch(),extraRoll=camera.getExtraRoll(),
        position=camera.getPosition(),orbit=0,elevation=0,zoom=1,switches={}},P)
    for _,name in ipairs({'Looking','ViewMode','VanityMode'}) do
        local key=types.Player.CONTROL_SWITCH[name]
        p.switches[key]=types.Player.getControlSwitch(player,key)
        types.Player.setControlSwitch(player,key,false)
    end
    return p
end
function P:apply(selection) types.Player.previewAppearance(player,selection) end
function P:rotate(dx,dy)
    self.orbit=(self.orbit+dx*.008+math.pi)%(2*math.pi)-math.pi
    self.elevation=math.max(-.65,math.min(1,self.elevation+dy*.006))
end
function P:zoomBy(delta)
    self.zoom=math.max(.35,math.min(2.5,self.zoom*math.exp(-delta*.12)))
end
function P:frame()
    local head=camera.getTrackedPosition()
    local height=math.max(24,head.z-player.position.z)
    local distance=math.max(36,math.min(600,height*1.35))*self.zoom
    local yaw=player.rotation:getYaw()+self.orbit
    -- Recompute the camera from the model's pivot every frame. Dragging changes
    -- spherical coordinates around that pivot, never the camera's own position.
    local c=math.cos(self.elevation)
    local forward=util.vector3(-math.sin(yaw)*c,math.cos(yaw)*c,math.sin(self.elevation))
    local position=head+forward*distance
    local aim=head-position
    -- Lua camera yaw is clockwise from +Y (the native camera negates it).
    -- Derive both angles from the actual look-at vector, not the orbit angle.
    local aimYaw=math.atan2(aim.x,aim.y)
    local aimPitch=math.atan2(-aim.z,math.sqrt(aim.x*aim.x+aim.y*aim.y))
    camera.setMode(camera.MODE.Static,true)
    camera.setStaticPosition(position)
    camera.setExtraYaw(0);camera.setExtraPitch(0);camera.setExtraRoll(0)
    camera.setYaw(aimYaw);camera.setPitch(aimPitch);camera.setRoll(0)
end
function P:close()
    if self.closed then return end
    self.closed=true
    -- nil restores the latest server-confirmed appearance, including a save
    -- confirmed while the preview was open. It never sends a chargen packet.
    pcall(types.Player.previewAppearance,player,nil)
    for key,value in pairs(self.switches) do pcall(types.Player.setControlSwitch,player,key,value) end
    pcall(camera.setMode,self.mode,true)
    if self.mode==camera.MODE.Static then pcall(camera.setStaticPosition,self.position) end
    pcall(camera.setYaw,self.yaw);pcall(camera.setPitch,self.pitch)
    pcall(camera.setRoll,self.roll)
    pcall(camera.setExtraYaw,self.extraYaw);pcall(camera.setExtraPitch,self.extraPitch);pcall(camera.setExtraRoll,self.extraRoll)
end
return P
