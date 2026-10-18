-- Numeric values stay in native units; text may use milliseconds, kHz or percent.
local ui=require('openmw.ui')
local util=require('openmw.util')
local async=require('openmw.async')
local input=require('openmw.input')
local C={}
local v=util.vector2
local white=ui.texture{path='white'}
local colors={track=util.color.rgb(0.14,0.17,0.21),text=util.color.rgb(0.85,0.89,0.94),accent=util.color.rgb(0.2,0.85,0.72),bad=util.color.rgb(1,0.4,0.35)}
local function cb(fn) return async:callback(function(...) return fn(...) or false end) end
local function rect(x,y,w,h,color) return {type=ui.TYPE.Image,props={position=v(x,y),size=v(w,h),resource=white,color=color}} end
function C.parse(text,spec)
    local number,unit=text:match('^%s*([%+%-%.%deE]+)%s*([%a%%]*)%s*$')
    local value=tonumber(number);unit=unit and unit:lower()
    if not value or value~=value then return end
    if (unit=='ms' or unit=='') and spec.unit=='s' then value=value/1000
    elseif unit=='khz' and spec.unit=='Hz' then value=value*1000
    elseif (unit=='%' or unit=='') and spec.unit=='%' then value=value/100
    elseif unit~='' and unit~=(spec.unit or ''):lower() then return end
    if value<spec.min or value>spec.max or spec.discrete and value%1~=0 then return end
    return value
end
function C.format(value,spec)
    if spec.unit=='s' then return string.format('%.6g ms',value*1000) end
    if spec.unit=='Hz' then return string.format('%.6g',value) end
    if spec.unit=='%' then return string.format('%.4g%%',value*100) end
    return string.format('%.5g',value)
end
function C.normalized(value,spec)
    if spec.log then return math.log(value/spec.min)/math.log(spec.max/spec.min) end
    return (value-spec.min)/(spec.max-spec.min)
end
function C.value(t,spec)
    local value=spec.log and spec.min*(spec.max/spec.min)^t or spec.min+(spec.max-spec.min)*t
    return spec.discrete and math.floor(value+0.5) or value
end
function C.slider(owner,name,title,spec,get,set,x,y,width)
    local element=owner:element{type=ui.TYPE.Widget,props={position=v(x,y),size=v(width,43)},content=ui.content{}}
    local editing,draft=false,nil
    local epoch=owner.controlEpoch
    local startX,startValue
    local field,refresh
    local function apply(value)
        if owner.uiClosed or owner.controlEpoch~=epoch then return end
        if value==get() then return end
        set(value);refresh()
        if owner.plot then owner:plots() end
    end
    local function commit()
        if not editing or owner.uiClosed or owner.controlEpoch~=epoch then return end
        editing=false;owner.typing=false
        local value=C.parse(draft or '',spec)
        if value~=nil then apply(value) elseif ui.showMessage then ui.showMessage('Enter a value between '..C.format(spec.min,spec)..' and '..C.format(spec.max,spec)..'.') end
        draft=nil;refresh()
    end
    field=owner:element{type=ui.TYPE.TextEdit,template={content=ui.content{{type=ui.TYPE.Image,props={relativeSize=v(1,1),resource=white,color=colors.track}}}},props={position=v(width-70,0),size=v(70,21),text=C.format(get(),spec),textSize=12,textColor=colors.accent},events={
        focusGain=cb(function() editing=true;owner.typing=true;draft=field.layout.props.text end),
        textChanged=cb(function(text)
            editing=true;owner.typing=true;draft=text
            field.layout.props.text=text
            field.layout.props.textColor=C.parse(text,spec)~=nil and colors.accent or colors.bad
        end),
        focusLoss=cb(commit),
        keyPress=cb(function(e)
            if e.code==input.KEY.Enter or e.code==input.KEY.NP_Enter then commit()
            elseif e.code==input.KEY.Escape then editing=false;owner.typing=false;draft=nil;refresh() end
        end)}}
    local trackWidth=width-36
    local function button(text,bx,fn)
        local b=rect(bx,24,15,16,colors.track)
        b.content=ui.content{{type=ui.TYPE.Text,props={position=v(3,0),size=v(14,18),text=text,textSize=12,textColor=colors.text}}}
        b.events={mouseClick=cb(fn)};return b
    end
    local function step(delta)
        local amount=spec.step or (spec.discrete and 1 or 0.01)
        if not spec.discrete and input.isShiftPressed and input.isShiftPressed() then amount=amount*0.1 end
        apply(math.max(spec.min,math.min(spec.max,get()+delta*amount)))
    end
    refresh=function()
        local track=rect(18,26,trackWidth,10,colors.track)
        track.content=ui.content{rect(0,0,math.max(1,trackWidth*C.normalized(get(),spec)),10,colors.accent)}
        local function move(e,press)
            local t=e.offset.x/(trackWidth*owner.window.scale)
            if press then startX=t;startValue=C.normalized(get(),spec) end
            if input.isShiftPressed and input.isShiftPressed() then t=(startValue or t)+(t-(startX or t))*0.1 end
            apply(C.value(math.max(0,math.min(1,t)),spec))
        end
        track.events={mousePress=cb(function(e) move(e,true) end),mouseMove=cb(function(e) if e.button==1 then move(e,false) end end)}
        element.layout.content=ui.content{{type=ui.TYPE.Text,props={position=v(0,0),size=v(math.max(30,width-72),22),text=type(title)=='function' and title() or title,textSize=12,textColor=colors.text}},track,field,
            button('-',0,function() step(-1) end),button('+',width-15,function() step(1) end)}
        if not editing then field.layout.props.text=C.format(get(),spec);field.layout.props.textColor=colors.accent;owner.window:update(field) end
        owner.window:update(element)
    end
    refresh();return element,refresh
end
return C
