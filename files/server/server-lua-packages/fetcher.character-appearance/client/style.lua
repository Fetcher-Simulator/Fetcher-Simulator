-- Reuse Morrowind's MWUI text colours, borders and original button textures.
local ui=require('openmw.ui')
local util=require('openmw.util')
local I=require('openmw.interfaces')
local v=util.vector2
local S={templates=I.MWUI.templates}
S.normal=S.templates.textNormal.props.textColor
S.header=S.templates.textHeader.props.textColor
S.disabled=util.color.rgb(179/255,168/255,135/255)
S.bad=util.color.rgb(.8,.24,.12)
local white=ui.texture{path='white'}
function S.image(x,y,w,h,color)
    return {type=ui.TYPE.Image,props={position=v(x,y),size=v(w,h),resource=white,color=color}}
end
function S.label(text,x,y,w,size,header)
    return {template=header and S.templates.textHeader or S.templates.textNormal,
        props={position=v(x,y),size=v(w,26),text=text,textSize=size or 16}}
end
function S.frame(x,y,w,h,thick)
    return {type=ui.TYPE.Widget,props={position=v(x,y),size=v(w,h)},content=ui.content{
        S.image(0,0,w,h,util.color.rgb(0,0,0)),
        {template=thick and S.templates.bordersThick or S.templates.borders,props={size=v(w,h)}},
    }}
end
local textures={}
for _,name in ipairs({'top','bottom','left','right','top_left_corner','top_right_corner','bottom_left_corner','bottom_right_corner'}) do
    textures[name]=ui.texture{path='textures/menu_button_frame_'..name..'.dds'}
end
function S.button(caption,x,y,w,h)
    local text=S.label(caption,6,math.floor((h-20)/2),w-12,16)
    text.props.textAlignH=ui.ALIGNMENT.Center
    local content={text}
    local pieces={{'top',4,0,w-8,4,true,false},{'bottom',4,h-4,w-8,4,true,false},
        {'left',0,4,4,h-8,false,true},{'right',w-4,4,4,h-8,false,true},
        {'top_left_corner',0,0,4,4},{'top_right_corner',w-4,0,4,4},
        {'bottom_left_corner',0,h-4,4,4},{'bottom_right_corner',w-4,h-4,4,4}}
    for _,p in ipairs(pieces) do
        content[#content+1]={type=ui.TYPE.Image,props={position=v(p[2],p[3]),size=v(p[4],p[5]),
            resource=textures[p[1]],tileH=p[6] or false,tileV=p[7] or false}}
    end
    return {type=ui.TYPE.Widget,props={position=v(x,y),size=v(w,h)},content=ui.content(content)}
end
return S
