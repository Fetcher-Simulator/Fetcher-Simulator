-- Server-selected standing formation. World yaw is clockwise from +Y.
local M={spacing=140,rowSpacing=180,front=240}
local function finite(n) return type(n)=='number' and n==n and math.abs(n)<1e8 end
function M.position(anchor,index,count)
    if type(anchor)~='table' or not finite(anchor.x) or not finite(anchor.y) or not finite(anchor.z)
        or not finite(anchor.rz or 0) or count<1 or count>32 or index<1 or index>count then return end
    -- A broad orchestra leaves fewer rows between the conductor and back row.
    local columns=math.min(8,math.ceil(math.sqrt(count*2)))
    local row=math.floor((index-1)/columns);local column=(index-1)%columns
    local width=math.min(columns,count-row*columns)
    local side=(column-(width-1)/2)*M.spacing;local forward=M.front+row*M.rowSpacing
    local yaw=anchor.rz or 0;local s,c=math.sin(yaw),math.cos(yaw)
    local x,y=anchor.x+side*c+forward*s,anchor.y-side*s+forward*c
    local atan2=math.atan2 or function(a,b) return math.atan(a,b) end
    return {x=x,y=y,z=anchor.z,rx=0,ry=0,rz=atan2(anchor.x-x,anchor.y-y)}
end
function M.instrument(track)
    if track.instrument then return track.instrument end
    local role=track.sourceRole
    if track.kind=='drum' or ({kick=true,snare=true,hat=true,openhat=true,tom=true,pedalhat=true,rim=true,clap=true,crash=true,ride=true,ridebell=true,cowbell=true,tambourine=true,shaker=true,percussion=true})[role]
        or (track.params and (track.params.drumType or 0)>0) then return 'drum' end
    if role=='guitar' or role=='bass' or role=='pluck' then return 'lute' end
    return ({strings='violin',brass='trumpet',reed='clarinet',pipe='flute'})[role] or 'keyboard'
end
function M.plan(project)
    local function equal(a,b)
        if type(a)~=type(b) then return false end
        if type(a)~='table' then return a==b end
        for k,v in pairs(a) do if not equal(v,b[k]) then return false end end
        for k in pairs(b) do if a[k]==nil then return false end end;return true
    end
    local groups={}
    for index,track in ipairs(project.tracks) do
        local instrument=M.instrument(track);local group
        for _,g in ipairs(groups) do
            local previous=g.source
            if g.instrument==instrument and (instrument=='drum' or
                previous.sourceRole==track.sourceRole and previous.followPrograms==track.followPrograms
                and previous.presetId==track.presetId and equal(previous.params,track.params)) then group=g;break end
        end
        if not group then group={name=track.name,instrument=instrument,source=track,tracks={}};groups[#groups+1]=group end
        group.tracks[#group.tracks+1]=index
    end
    local leader
    if project.leaderPlays~=false then
        for _,g in ipairs(groups) do if g.instrument=='keyboard' then leader=g;break end end
        if not leader then for _,g in ipairs(groups) do if g.instrument~='drum' and g.instrument~='timpani' then leader=g;break end end end
        leader=leader or groups[1]
    end
    local out={routes={},leader=leader and leader.name}
    for _,g in ipairs(groups) do
        local slot=0
        if g~=leader then out[#out+1]={name=g.name,instrument=g.instrument};slot=#out end
        for _,index in ipairs(g.tracks) do out.routes[index]=slot end
    end
    return out
end
return M
