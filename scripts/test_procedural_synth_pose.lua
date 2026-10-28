-- Geometric IK edge cases with a minimal engine-vector replacement.
local V={};V.__index=V
local function v(x,y,z) return setmetatable({x=x,y=y,z=z},V) end
function V.__add(a,b) return v(a.x+b.x,a.y+b.y,a.z+b.z) end
function V.__sub(a,b) return v(a.x-b.x,a.y-b.y,a.z-b.z) end
function V.__mul(a,b) return v(a.x*b,a.y*b,a.z*b) end
function V.__div(a,b) return a*(1/b) end
function V:dot(b) return self.x*b.x+self.y*b.y+self.z*b.z end
function V:cross(b) return v(self.y*b.z-self.z*b.y,self.z*b.x-self.x*b.z,self.x*b.y-self.y*b.x) end
function V:length() return math.sqrt(self:dot(self)) end
package.loaded['openmw.animation']={}
package.loaded['openmw.types']={}
package.loaded['openmw.util']={vector3=v}
package.loaded['scripts.multiplayer.fetcher.procedural-synth.client.instrument-profiles']={}
package.loaded['scripts.multiplayer.fetcher.procedural-synth.client.seated-pose']=dofile('files/server/server-lua-packages/fetcher.procedural-synth/client/seated-pose.lua')
local pose=dofile('files/server/server-lua-packages/fetcher.procedural-synth/client/performance.lua')
local checks=0
local function check(value,message) checks=checks+1;assert(value,message) end
for _,target in ipairs{v(0,30,-10),v(0,1000,0),v(0,0,0.001),v(30,0,0),v(0,-30,0)} do
    local s=v(0,0,0)
    local e,w=pose.elbow(s,target,24,22,v(1,0,-1))
    check(math.abs((e-s):length()-24)<1e-6,'upper-arm length changed')
    check(math.abs((w-e):length()-22)<1e-6,'forearm length changed')
    check((w-s):length()<=46,'unreachable target stretched skeleton')
end
check(pose.elbow(v(0,0,0),v(0,0,0),20,20,v(0,0,1))==nil,'degenerate target accepted')
local flex=pose.bendAxis(v(1,0,0),v(0,0,-1))
check(flex.y==1 and math.abs(flex:dot(v(1,0,0)))<1e-8,'finger flexion axis bends toward the palm rather than assuming bone-local Z')
check(pose.bendAxis(v(1,0,0),v(1,0,0))==nil,'degenerate finger plane falls back safely')
for _,pair in ipairs{{v(1,0,0),v(0,0,-1)},{v(-1,0,0),v(0,-1,0)},{v(0,1,0),v(0,0,-1)}} do
    local axis=pose.bendAxis(pair[1],pair[2])
    check(axis:cross(pair[1]):dot(pair[2])>0,'positive flexion turns either hand toward its palm')
end
print('PASS arm IK reach, lengths and degenerate targets; '..checks..' assertions')
