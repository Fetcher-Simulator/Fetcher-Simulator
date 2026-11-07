-- Server-selected standing formation. World yaw is clockwise from +Y.
local M={stageRange=2000,spacing=140,rowSpacing=180,front=240,
    floorSlope=math.tan(math.rad(35)),floorNormalZ=math.cos(math.rad(35)),floorRoughness=24,floorMaxRise=512}
local function finite(n) return type(n)=='number' and n==n and math.abs(n)<1e8 end
local function exterior(cell) return type(cell)=='string' and cell:match('^EXT:%-?%d+,%-?%d+$')~=nil end
function M.sameWorld(a,b) return type(a)=='string' and type(b)=='string' and (a==b or exterior(a) and exterior(b)) end
function M.cellAt(cell,p)
    if exterior(cell) then return string.format('EXT:%d,%d',math.floor(p.x/8192),math.floor(p.y/8192)) end
    return cell
end
function M.inRange(home,p)
    return (type(home)=='table' or type(home)=='userdata') and finite(home.x) and finite(home.y) and finite(home.z) and (type(p)=='table' or type(p)=='userdata')
        and finite(p.x) and finite(p.y) and finite(p.z) and (p.x-home.x)^2+(p.y-home.y)^2+(p.z-home.z)^2<=M.stageRange^2
end
function M.floorLimit(anchor,p)
    return math.min(M.floorMaxRise,64+math.sqrt((p.x-anchor.x)^2+(p.y-anchor.y)^2)*M.floorSlope)
end
function M.position(anchor,index,count,wide)
    if type(anchor)~='table' or not finite(anchor.x) or not finite(anchor.y) or not finite(anchor.z)
        or not finite(anchor.rz or 0) or count<1 or count>32 or index<1 or index>count then return end
    -- A broad orchestra leaves fewer rows between the conductor and back row.
    local columns=math.min(8,math.ceil(math.sqrt(count*2)))
    local row=math.floor((index-1)/columns);local column=(index-1)%columns
    local width=math.min(columns,count-row*columns)
    local side=(column-(width-1)/2)*(wide and 280 or M.spacing);local forward=(wide and 320 or M.front)+row*(wide and 300 or M.rowSpacing)
    local yaw=anchor.rz or 0;local s,c=math.sin(yaw),math.cos(yaw)
    local x,y=anchor.x+side*c+forward*s,anchor.y-side*s+forward*c
    local atan2=math.atan2 or function(a,b) return math.atan(a,b) end
    return {x=x,y=y,z=anchor.z,rx=0,ry=0,rz=atan2(anchor.x-x,anchor.y-y)}
end
function M.instrument(track)
    if track.instrument then return track.instrument end
    local role=track.sourceRole
    if track.kind=='drum' or ({kick=true,snare=true,hat=true,openhat=true,tom=true,pedalhat=true,rim=true,clap=true,crash=true,ride=true,ridebell=true,cowbell=true,tambourine=true,shaker=true,percussion=true})[role]
        or (track.params and (track.params.drumType or 0)>0) then return 'drumkit' end
    if role=='guitar' then return track.sourceProgram==24 and 'classical' or track.sourceProgram==25 and 'acoustic' or 'guitar' end
    if role=='bass' then return 'bass' end
    if role=='pluck' then return 'lute' end
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
            if g.instrument==instrument and (instrument=='drum' or instrument=='drumkit' or
                previous.sourceRole==track.sourceRole and previous.followPrograms==track.followPrograms
                and previous.presetId==track.presetId and equal(previous.params,track.params)) then group=g;break end
        end
        if not group then group={name=track.name,instrument=instrument,source=track,tracks={}};groups[#groups+1]=group end
        group.tracks[#group.tracks+1]=index
    end
    local leader
    if project.leaderPlays~=false then
        for _,g in ipairs(groups) do if g.instrument=='keyboard' then leader=g;break end end
        if not leader then for _,g in ipairs(groups) do if g.instrument~='drum' and g.instrument~='drumkit' and g.instrument~='timpani' then leader=g;break end end end
        leader=leader or groups[1]
    end
    local out={routes={},leader=leader and leader.name}
    for _,g in ipairs(groups) do
        if g.instrument=='drumkit' then out.wideSpacing=true end
        local slot=0
        if g~=leader then out[#out+1]={name=g.name,instrument=g.instrument};slot=#out end
        for _,index in ipairs(g.tracks) do out.routes[index]=slot end
    end
    return out
end
M.sections={{id='strings',name='Strings'},{id='winds',name='Woodwinds'},{id='brass',name='Brass'},
    {id='keys',name='Keys'},{id='rhythm',name='Guitars / bass'},{id='percussion',name='Percussion'}}
function M.section(instrument)
    return ({violin='strings',cello='strings',flute='winds',clarinet='winds',trumpet='brass',keyboard='keys',
        drum='percussion',drumkit='percussion',timpani='percussion'})[instrument] or 'rhythm'
end
function M.bound(band,accept)
    local out={};local seen={}
    for slot,member in ipairs(band or {}) do local t=member.target
        if t and t.kind=='npc' and not seen[t.mpNum] and (not accept or accept(t)) then
            out[#out+1]={target=t,instrument=t.instrument,slot=slot,name=member.name};seen[t.mpNum]=true
            if t.instrument=='drumkit' then out.wideSpacing=true end
        end
    end;return out
end
-- Configurations are bounded scalars; never accept client-provided X/Y positions.
-- Missing settings reproduce the existing grid byte-for-byte.
function M.positions(anchor,plan,config)
    if type(plan)~='table' or #plan>32 or not M.position(anchor,1,math.max(1,#plan)) then return nil end
    if config==nil then config={preset='grid',spacing=1,depth=1,distance=1} end
    if type(config)~='table' or not ({grid=true,sections=true,arc=true})[config.preset] then return nil end
    for key in pairs(config) do if key~='preset' and key~='spacing' and key~='depth' and key~='distance' then return nil end end
    for _,key in ipairs{'spacing','depth','distance'} do if not finite(config[key]) or config[key]<1 or config[key]>2 then return nil end end
    local wide=plan.wideSpacing;local spacing=(wide and 280 or M.spacing)*config.spacing
    local depth=(wide and 300 or M.rowSpacing)*config.depth;local front=(wide and 320 or M.front)*config.distance
    local out={};local columns=math.min(8,math.ceil(math.sqrt(math.max(1,#plan)*2)))
    local function place(index,side,forward)
        local yaw=anchor.rz or 0;local sn,cs=math.sin(yaw),math.cos(yaw)
        local x,y=anchor.x+side*cs+forward*sn,anchor.y-side*sn+forward*cs
        local atan2=math.atan2 or function(a,b) return math.atan(a,b) end
        out[index]={x=x,y=y,z=anchor.z,rx=0,ry=0,rz=atan2(anchor.x-x,anchor.y-y),section=M.section(plan[index].instrument)}
    end
    if config.preset=='sections' then
        -- Keep small families together while packing shared rows; avoid wasting
        -- a complete deep row for each one-note section in a mixed MIDI band.
        local rows,current={},{}
        local function flush() if #current>0 then rows[#rows+1]=current;current={} end end
        for _,section in ipairs(M.sections) do
            local indices={};for index,part in ipairs(plan) do if M.section(part.instrument)==section.id then indices[#indices+1]=index end end
            if #indices<=columns and #current+#indices>columns then flush() end
            for _,index in ipairs(indices) do current[#current+1]=index;if #current==columns then flush() end end
        end
        flush()
        for row,indices in ipairs(rows) do for column,index in ipairs(indices) do
            place(index,(column-(#indices+1)/2)*spacing,front+(row-1)*depth)
        end end
    elseif config.preset=='arc' then
        local angle=columns>1 and math.rad(120)/(columns-1) or 0
        local radius=columns>1 and math.max(front/math.cos(math.rad(60)),spacing/(2*math.sin(angle/2))) or front
        for index=1,#plan do
            local row=math.floor((index-1)/columns);local width=math.min(columns,#plan-row*columns)
            local theta=((index-1)%columns-(width-1)/2)*angle;local r=radius+row*depth
            place(index,math.sin(theta)*r,math.cos(theta)*r)
        end
    else
        for index=1,#plan do
            local row=math.floor((index-1)/columns);local width=math.min(columns,#plan-row*columns)
            place(index,((index-1)%columns-(width-1)/2)*spacing,front+row*depth)
        end
    end
    return out
end
return M
