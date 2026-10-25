local camera=require('openmw.camera')
local types=require('openmw.types')
local player=require('openmw.self')
local util=require('openmw.util')
local P={};P.__index=P
function P.new()
    local p=setmetatable({mode=camera.getMode(),yaw=camera.getYaw(),pitch=camera.getPitch(),
        position=camera.getPosition(),orbit=0,zoom=1,switches={}},P)
    for _,name in ipairs({'Looking','ViewMode','VanityMode'}) do
        local key=types.Player.CONTROL_SWITCH[name]
        p.switches[key]=types.Player.getControlSwitch(player,key)
        types.Player.setControlSwitch(player,key,false)
    end
    return p
end
function P:apply(selection) types.Player.previewAppearance(player,selection) end
function P:frame()
    local head=camera.getTrackedPosition()
    local height=math.max(24,head.z-player.position.z)
    local distance=math.max(36,math.min(600,height*1.35))*self.zoom
    local yaw=player.rotation:getYaw()+self.orbit
    local forward=util.vector3(-math.sin(yaw),math.cos(yaw),0)
    local right=util.vector3(math.cos(yaw),math.sin(yaw),0)
    camera.setMode(camera.MODE.Static,true)
    camera.setStaticPosition(head+forward*distance-right*(distance*0.23))
    camera.setYaw(yaw+math.pi);camera.setPitch(0)
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
end
return P
