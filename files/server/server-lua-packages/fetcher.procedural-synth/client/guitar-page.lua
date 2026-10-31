local ui=require('openmw.ui')
local util=require('openmw.util')
local Controls=require('scripts.multiplayer.fetcher.procedural-synth.client.controls')
local Dropdown=require('scripts.multiplayer.fetcher.procedural-synth.client.dropdown')
local Tools=require('scripts.multiplayer.fetcher.procedural-synth.client.guitar-tools')
local v=util.vector2
local function label(text,x,y,width,size)
    return {type=ui.TYPE.Text,props={text=text,position=v(x,y),size=v(width,20),textSize=size or 13,
        textColor=util.color.rgb(.85,.89,.94)}}
end
local names={'C','C#','D','D#','E','F','F#','G','G#','A','A#','B'}
local function pitch(n) return names[n%12+1]..(math.floor(n/12)-1) end
return function(owner,content)
    local w=owner.w;local t=w.project.tracks[w.selected]
    local o,profile=Tools.options(w.guitarOptions);if not o then o,profile=Tools.options() end;w.guitarOptions=o
    local p=w.guitarPreview
    if p and (p.generation~=w.generation or p.project~=w.project or p.index~=w.selected) then
        w.guitarPreview=nil;w.guitarStatus='Source changed. Preview again.';p=nil
    end
    content[#content+1]=label('GUITAR / BASS NOTE TOOLS',20,174,630,18)
    content[#content+1]=owner:button('Back to Piano [Esc]',710,169,185,function() w.page='piano';w.revision=w.revision+1 end)
    content[#content+1]=label(t and t.name..' / '..(w:clipSource() and 'Shared clip source' or 'Active part') or 'Choose a melodic part in Piano.',20,205,875)
    local status,rows;rows={}
    local function dirty()
        w.guitarPreview=nil;w.guitarStatus='Settings changed. Preview again before applying.'
        if status then status.layout.props.text=w.guitarStatus;owner.window:update(status) end
        for _,row in ipairs(rows) do row.layout.props.visible=false;owner.window:update(row) end
    end
    content[#content+1]=owner:button(profile.name..' v',20,238,360,function()
        local choices={};for _,item in ipairs(Tools.profiles) do choices[#choices+1]={id=item.id,label=item.name} end
        Dropdown.open(owner,choices,o.profile,function(id) o.profile=id;dirty();w.revision=w.revision+1 end,20,270,450)
    end)
    content[#content+1]=owner:button((o.scope=='all' and 'All notes' or 'Selected notes')..' v',395,238,220,function()
        Dropdown.open(owner,{{id='all',label='All notes in active source'},{id='selected',label='Selected notes only'}},o.scope,
            function(id) o.scope=id;dirty();w.revision=w.revision+1 end,395,270,380)
    end)
    local directions={'Down: low to high','Up: high to low','Alternate strokes'}
    content[#content+1]=owner:button(directions[o.direction+1]..' v',630,238,265,function()
        local choices={};for n,name in ipairs(directions) do choices[#choices+1]={id=n-1,label=name} end
        Dropdown.open(owner,choices,o.direction,function(id) o.direction=id;dirty();w.revision=w.revision+1 end,550,270,345)
    end)
    for _,entry in ipairs{{'spacing','Pick spacing (ms / string)',0,25,.5,20,288},
        {'span','Maximum fretted span',1,12,1,475,288},{'frets','Highest allowed fret',0,24,1,20,350}} do
        local key=entry[1]
        content[#content+1]=Controls.slider(owner,'guitar:'..key,entry[2],{min=entry[3],max=entry[4],step=entry[5],discrete=key~='spacing'},
            function() return o[key] end,function(value) o[key]=value;dirty() end,entry[6],entry[7],410)
    end
    local tuning={};for n,open in ipairs(profile.opens) do tuning[n]=pitch(open) end
    content[#content+1]=label('Low to high: '..table.concat(tuning,' / '),475,350,420,12)
    content[#content+1]=label('Source Tune offset: '..(t and t.params.tune1 or 0)..' semitones',475,376,420,12)
    content[#content+1]=owner:button('Preview plan',20,414,150,function() owner:releaseAll();w:previewGuitar() end)
    content[#content+1]=owner:button('Apply / one Undo',185,414,180,function() owner:releaseAll();w:applyGuitar() end,p~=nil)
    if t and t.kind=='midi' and not t.edited then content[#content+1]=owner:button('Edit MIDI first',380,414,180,function() w:convertPart(w.selected) end) end
    local layout=label(w.guitarStatus or 'Preview checks the full source. Only the chosen scope can change.',20,452,875,13)
    layout.props.multiline=true;layout.props.size=v(875,44);status=owner:element(layout);content[#content+1]=status
    content[#content+1]=label('Written pitch / step > string : fret / pick delay / stroke',20,506,610,12)
    local pages=p and math.max(1,math.ceil(#p.rows/5)) or 1
    local page=math.max(0,math.min(w.guitarPage or 0,pages-1));w.guitarPage=page
    content[#content+1]=owner:button('Report '..(page+1)..' / '..pages..' v',650,495,245,function()
        if not w.guitarPreview then return end
        local choices={};for n=0,pages-1 do
            local first=p.rows[n*5+1];choices[#choices+1]={id=n,label=string.format('Notes %d-%d / step %.4g',n*5+1,math.min(#p.rows,n*5+5),first.step)}
        end
        Dropdown.open(owner,choices,page,function(id) w.guitarPage=id;w.revision=w.revision+1 end,470,245,425)
    end)
    for n=1,5 do
        local row=p and p.rows[page*5+n];local text=row and string.format('%s / %.4g  >  %d : %d  /  %.2f ms  /  %s',pitch(row.n),row.step,row.string,row.fret,row.delay,row.up and 'Up' or 'Down') or ''
        rows[n]=owner:element(label(text,20,530+(n-1)*22,875,12));content[#content+1]=rows[n]
    end
    content[#content+1]=label('Pitches/velocities stay intact; later picks keep original ends. Reusing a string can shorten a selected gate.',20,650,890,11)
    content[#content+1]=label("Linked-source edits affect every placement. Their strum timing follows each placement's tempo.",20,673,890,11)
end
