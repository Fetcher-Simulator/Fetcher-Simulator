-- Small, deterministic presentation gestures. Beat is the audible score clock,
-- never a client's wall clock. Root position, furniture and gameplay stay put.
local M={}
local function clamp(n,a,b) return math.max(a,math.min(b,n)) end
local function smooth(old,wanted,dt,rate) return old+(wanted-old)*(1-math.exp(-math.min(.2,math.max(0,dt))*rate)) end
function M.frame(state,dt,clock,kind,seated,moving,reach)
    state=state or {level=0,yaw=0}
    local active=clock and clock.playing and not moving
    state.level=smooth(state.level,active and 1 or 0,dt,7)
    local beat=clock and clock.beat or 0
    -- Follow half-time at fast tempos, with smaller gestures as tempo rises.
    local rate=clock and clock.rate or 2
    local accent=beat/(rate>2.6 and 2 or 1)
    local phase=(accent%1)*math.pi*2;local level=state.level
    local emphasis=math.min(1,(2/math.max(rate,2))^2)
    local tap=(1-math.cos(phase))*.5
    -- Generic transform.rotate is right-handed (unlike rotateZ's game yaw):
    -- facing +Y, a right-side +X reach needs a negative world-Z turn.
    local yaw=kind=='drumkit' and -clamp(reach or 0,-1,1)*math.rad(23) or 0
    state.yaw=smooth(state.yaw,yaw*level,dt,16)
    local strings=kind=='guitar' or kind=='bass' or kind=='lute'
    local roll=not seated and kind~='conductor' and math.sin(beat*math.pi/2)*math.rad(strings and .6 or 1.4)*level or 0
    state.roll=smooth(state.roll or 0,roll,dt,4)
    return {level=level,yaw=state.yaw,
        roll=state.roll,
        nod=(tap-.5)*math.rad(2.4)*level*emphasis,
        foot=not seated and kind~='conductor' and tap*math.rad(5)*level*emphasis or 0},state
end
function M.drumReach(hands)
    if not hands then return 0 end
    -- Right-side ride/floor tom, left-side hats/crash. Snare stays centered.
    local direction={[42]=-1,[46]=-1,[49]=-.65,[55]=-.65,[57]=-.65,
        [51]=1,[53]=1,[59]=1,[41]=.7,[43]=.7,[45]=.25,[47]=.25,[48]=-.2,[50]=-.2}
    local right,left=hands.R,hands.L
    local r=right and (direction[right.note] or 0)*(right.reach or right.pulse) or 0
    local l=left and (direction[left.note] or 0)*(left.reach or left.pulse) or 0
    return math.abs(r)>=math.abs(l) and r or l
end
function M.moving(state,position,dt)
    local last=state.position;state.position=position
    local moving=last and (position-last):length()>math.max(.35,dt*4)
    state.stillFor=moving and 0 or (state.stillFor or 0)+dt
    return state.stillFor<.25
end
function M.conduct(beat,elapsed,ending)
    local phase=(beat%1)*math.pi*2
    local cue=clamp(elapsed/.35,0,1)*(1-clamp(ending or 0,0,1))
    -- A downward right-hand ictus and a slower left-hand phrase cue.
    return {L={-22-3*math.sin(beat*math.pi/2)*cue,27,85+(23+4*math.cos(beat*math.pi/2))*cue},
        R={23+5*math.sin(phase)*cue,30,84+(25+9*math.cos(phase))*cue}}
end
return M
