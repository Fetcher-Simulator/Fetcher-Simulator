local root=arg[1] or '.'
local checks=0
local function check(ok,message) checks=checks+1;assert(ok,message) end
local cells,players,messages,writes={},{},{},0
local function copy(t) local r={} for k,v in pairs(t or {}) do r[k]=v end return r end
local mp={}
function mp.setGlobalPhysics(v) mp.defaults=copy(v) end
function mp.setCellPhysics(id,v) cells[id]=copy(v);writes=writes+1 end
function mp.getCellPhysics(id) return cells[id] or mp.defaults end
function mp.getPlayerPhysics(id) return players[id] or mp.defaults end
function mp.setPlayerPhysics(id,v) players[id]=copy(v);writes=writes+1 end
function mp.clearPlayerPhysics(id) players[id]=nil;writes=writes+1 end
function mp.clearCellPhysics(id) cells[id]=nil;writes=writes+1 end
package.preload.mp=function() return mp end
local commands=assert(loadfile(root..'/apps/openmw-server/scripts/surf_commands.lua'))()
local marks=assert(loadfile(root..'/apps/openmw-server/scripts/mark_recall.lua'))()
marks.onServerInit()
for _,id in ipairs{'surf_mesa_mw','surf_utopia_mw','surf_kitsune_mw'} do
 check(cells[id].enabled and cells[id].normalizeMovement,id..' must opt in')
 check(cells[id].referenceSpeed==100 and cells[id].referenceAthletics==100 and cells[id].referenceAcrobatics==50,id..' reference baseline')
end
check(cells.surf_kitsune_mw.gravity==1.6 and cells.surf_kitsune_mw.airAccel==600,'Existing map tuning retained')
check(not cells.Balmora,'Ordinary cells remain unchanged')
local player={guid=1,name='Tester',cell='surf_mesa_mw'}
function player:sendMessage(v) messages[#messages+1]=v end
local env={normalizeCellId=function(s) return s end,isAdmin=function() return true end,
 requireAdmin=function() return true end,findPlayerByName=function() return player end}
local function chat(text) return commands.handleChat(player,{message=text},env) end
for _,text in ipairs{'/surf off','/surf gravity 0.1','/surf clear','/surf player Tester on','/surf self referenceSpeed 1'} do
 local before=writes;check(chat(text)==false,'Recognized command '..text)
 check(writes==before,'Map profile must reject personal mutation '..text)
 check(messages[#messages]:find('shared movement settings',1,true),'Explain shared map policy')
end
local before=writes;chat('/surf');check(writes==before,'Reading physics must not mutate')
player.cell='Balmora';chat('/surf gravity 1.1');check(players[1].gravity==1.1,'Personal settings work outside normalized maps')
chat('/surf cell surf_mesa_mw referenceAcrobatics 75');check(cells.surf_mesa_mw.referenceAcrobatics==75,'Admin can tune reference acrobatics')
before=writes
for _,value in ipairs{'-1','101','1e999','nope'} do
 chat('/surf cell surf_mesa_mw referenceSpeed '..value)
 check(writes==before,'Reject invalid reference '..value)
end
chat('/surf cell surf_mesa_mw normalize off');check(cells.surf_mesa_mw.normalizeMovement==false,'Explicit administrative opt out')
commands.onPlayerDisconnect{guid=1};check(players[1]==nil,'Personal state cleared on disconnect')
print('Surf movement: '..checks..' checks passed')
