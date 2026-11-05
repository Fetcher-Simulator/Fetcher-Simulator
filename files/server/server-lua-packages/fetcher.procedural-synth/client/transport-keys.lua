-- One press latch also covers the native Window key event and engine event.
local input=require('openmw.input')
local core=require('openmw.core')
local M={}
function M.handle(owner,code,on,action)
    if code~=input.KEY.Space and code~=input.KEY.Enter and code~=input.KEY.NP_Enter then return false end
    owner.transportHeld=owner.transportHeld or {}
    if not on then
        if not input.isKeyPressed or not input.isKeyPressed(code) then owner.transportHeld[code]=nil end
        return false
    end
    if owner.typing then owner.transportHeld[code]=true;return false end
    if owner.dropdown or not action then owner.transportHeld[code]=true;return false end
    if owner.transportHeld[code] then return true end
    owner.transportHeld[code]=true
    -- Window and engine delivery can straddle a frame (including key-up).
    -- Collapse delayed duplicates and native repeat into one transport action.
    local now=core.getRealTime()
    if now-(owner.transportActionAt or -1e9)<.15 then return true end
    owner.transportActionAt=now
    if code~=input.KEY.Space then action('cursor')
    elseif input.isCtrlPressed and input.isCtrlPressed() then action('loop')
    elseif input.isShiftPressed and input.isShiftPressed() then action('beginning')
    else action('pause') end
    return true
end
function M.frame(owner,action)
    -- Native menus may consume printable presses before onKeyPress. Poll only
    -- the transport keys while this editor owns Interface; reuse the same latch.
    if not input.isKeyPressed then return end
    for _,code in ipairs{input.KEY.Space,input.KEY.Enter,input.KEY.NP_Enter} do
        if code then M.handle(owner,code,input.isKeyPressed(code),action) end
    end
end
return M
