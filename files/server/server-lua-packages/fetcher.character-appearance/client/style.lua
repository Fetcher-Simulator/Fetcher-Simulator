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
local head={}
for _,name in ipairs({'middle','top','bottom','left','right','top_left_corner','top_right_corner','bottom_left_corner','bottom_right_corner'}) do
    head[name]=ui.texture{path='textures/menu_head_block_'..name..'.dds'}
end
-- The inventory/chat caption's filled head blocks, with fixed edges and tiled
-- centres. Relative pieces also follow scrollbar handles as their height changes.
function S.filled(x,y,w,h)
    local pieces={{'middle',2,2,-4,-4,0,0,1,1,true,true},
        {'top',2,0,-4,2,0,0,1,0,true,false},{'bottom',2,-2,-4,2,0,1,1,0,true,false},
        {'left',0,2,2,-4,0,0,0,1,false,true},{'right',-2,2,2,-4,1,0,0,1,false,true},
        {'top_left_corner',0,0,2,2,0,0,0,0},{'top_right_corner',-2,0,2,2,1,0,0,0},
        {'bottom_left_corner',0,-2,2,2,0,1,0,0},{'bottom_right_corner',-2,-2,2,2,1,1,0,0}}
    local content={}
    for _,p in ipairs(pieces) do content[#content+1]={type=ui.TYPE.Image,props={
        position=v(p[2],p[3]),size=v(p[4],p[5]),relativePosition=v(p[6],p[7]),relativeSize=v(p[8],p[9]),
        resource=head[p[1]],tileH=p[10] or false,tileV=p[11] or false}} end
    return {type=ui.TYPE.Widget,props={position=v(x,y),size=v(w,h)},content=ui.content(content)}
end
function S.title(caption,x,y,w,h,textSize)
    local left,right=S.filled(0,0,4,h),S.filled(0,0,4,h)
    left.external={grow=1};right.external={grow=1}
    local gap={type=ui.TYPE.Widget,props={size=v(8,h)}}
    local text={template=S.templates.textHeader,props={text=caption,textSize=textSize or 18,autoSize=true}}
    return {type=ui.TYPE.Flex,props={position=v(x,y),size=v(w,h),horizontal=true,autoSize=false,arrange=ui.ALIGNMENT.Center},
        content=ui.content{left,gap,text,{type=ui.TYPE.Widget,props={size=v(8,h)}},right}}
end
local arrows={['<']=ui.texture{path='textures/omw_menu_scroll_left.dds'},
    ['>']=ui.texture{path='textures/omw_menu_scroll_right.dds'}}
function S.button(caption,x,y,w,h)
    local text=S.label(caption,6,math.floor((h-20)/2),w-12,16)
    text.props.textAlignH=ui.ALIGNMENT.Center
    if arrows[caption] then text={type=ui.TYPE.Image,props={position=v(math.floor((w-12)/2),math.floor((h-12)/2)),
        size=v(12,12),resource=arrows[caption]}} end
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
