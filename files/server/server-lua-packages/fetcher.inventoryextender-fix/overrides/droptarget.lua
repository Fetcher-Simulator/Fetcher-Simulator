-- Mouse capture sends release to the pressed table, even over another window.
-- Resolve only cross-window Container drops; clicks and other modes keep their target.
local function contains(window, position)
    local props = window and window.element and window.element.layout.props
    if not props or props.visible == false or not props.position or not props.size then return false end
    return position.x >= props.position.x and position.y >= props.position.y
        and position.x < props.position.x + props.size.x
        and position.y < props.position.y + props.size.y
end

return function(mode, pickpocket, windows, receivingType, target, position)
    if mode ~= 'Container' or pickpocket or not position then return target end
    if receivingType ~= 'Inventory' and receivingType ~= 'Container' then return target end
    -- Keep the receiving window authoritative in overlapping regions.
    if contains(windows[receivingType], position) then return target end
    local other = windows[receivingType == 'Inventory' and 'Container' or 'Inventory']
    if contains(other, position) then return other.target end
    -- A captured release outside either inventory is not a drop into its source.
    return nil
end
