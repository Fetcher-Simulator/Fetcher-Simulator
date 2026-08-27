local ui = require('openmw.ui')
local util = require('openmw.util')
local v2 = util.vector2
local auxUi = require('openmw_aux.ui')
local I = require('openmw.interfaces')
local async = require('openmw.async')
local core = require('openmw.core')

local baseTemplates = require('scripts.InventoryExtender.ui.templates.base')
local specialTemplates = require('scripts.InventoryExtender.ui.templates.magic')
local constants = require('scripts.InventoryExtender.util.constants')
local iconPack = require('scripts.InventoryExtender.util.iconPack')
local configPlayer = require('scripts.InventoryExtender.config.player')

local l10n = core.l10n('InventoryExtender')

local CategoryFilter = {}

function CategoryFilter.create(props, onCategoryChange, onSearchChange, ctx)
    local maxHeight = props.maxHeight or 48
    local compact = props.compact == true
    local vanillaStyle = props.vanillaStyle == true

    local topBarHeight = specialTemplates.LINE_HEIGHT * 1.5
    local categoryBarHeight = compact and (topBarHeight - 4) or (maxHeight - topBarHeight)
    local elementHeight = compact and (topBarHeight + 8) or maxHeight
    local compactIconPadding = 8
    local vanillaOpacityStates = {
        default = 0.45,
        hover = 0.7,
        pressed = 0.85,
        active = 1,
        activeHover = 1,
        activePressed = 0.9,
        disabled = 0.2,
        disabledHover = 0.2,
        disabledPressed = 0.2,
    }

    local element, createSearchBar
    local placeholder = l10n('UI_SearchBarPlaceholder')

    local function replaceSearchBar(text)
        local content = element.layout.content.topBar.content
        local indexOfBar = content:indexOf('searchBar')
        local indexOfProxy = content:indexOf('searchBarProxy')

        if indexOfBar and indexOfProxy then
            local lowIndex = math.min(indexOfBar, indexOfProxy)
            local highIndex = math.max(indexOfBar, indexOfProxy)

            -- Remove from highest to lowest so the second saved index stays valid.
            local highName = indexOfBar > indexOfProxy and 'searchBar' or 'searchBarProxy'
            local lowName = indexOfBar > indexOfProxy and 'searchBarProxy' or 'searchBar'
            content[highName] = nil
            content[lowName] = nil

            local lowValue
            local highValue
            if indexOfBar < indexOfProxy then
                lowValue = { name = 'searchBarProxy' }
                highValue = createSearchBar(text)
            else
                lowValue = createSearchBar(text)
                highValue = { name = 'searchBarProxy' }
            end

            content:insert(lowIndex, lowValue)
            content:insert(highIndex, highValue)
        else
            -- Recover from a partially failed swap instead of leaving the whole
            -- InventoryExtender UI poisoned until the player relogs.
            local anchorIndex = indexOfBar or indexOfProxy or (#content + 1)
            if indexOfBar then
                content.searchBar = nil
            elseif indexOfProxy then
                content.searchBarProxy = nil
            end
            content:insert(anchorIndex, createSearchBar(text))
            content:insert(anchorIndex + 1, { name = 'searchBarProxy' })
        end

        ctx.updateQueue[element] = true
    end

    createSearchBar = function(text)
        return {
            name = 'searchBar',
            template = I.MWUI.templates.box,
            content = ui.content {
                {
                    name = 'padding',
                    template = I.MWUI.templates.padding,
                    content = ui.content {
                        {
                            name = 'textEdit',
                            template = baseTemplates.textEditLine,
                            props = {
                                size = v2(200, topBarHeight - 12),
                                text = text,
                                textColor = text == placeholder and constants.Colors.DISABLED or constants.Colors.DEFAULT,
                            },
                            events = {
                                textChanged = async:callback(function(text, layout)
                                    layout.props.text = text
                                    if layout.props.textColor == constants.Colors.DISABLED then
                                        layout.props.textColor = constants.Colors.DEFAULT
                                        element:update()
                                    end
                                    if onSearchChange then
                                        onSearchChange(text)
                                    end
                                end),
                                focusGain = async:callback(function(_, layout)
                                    if layout.props.text == placeholder then
                                        layout.props.text = ''
                                        element:update()
                                    end
                                end),
                                focusLoss = async:callback(function(_, layout)
                                    if layout.props.text == '' then
                                        layout.props.text = placeholder
                                    end
                                    replaceSearchBar(layout.props.text)
                                end),
                            }
                        }
                    }
                }
            },
        }
    end

    local categoryBarLayout = {
        name = 'categoryBar',
        template = configPlayer.tweaks.b_CategoryBarBorders and I.MWUI.templates.borders or nil,
        content = ui.content {
            {
                name = 'categoryBarButtons',
                type = ui.TYPE.Widget,
                props = {
                    relativeSize = v2(1, 1),
                },
                content = ui.content {},
            }
        },
        props = {
            position = compact and v2(0, 0) or v2(0, topBarHeight + 8),
            size = v2(0, categoryBarHeight),
            anchor = compact and nil or v2(0.5, 0),
            relativePosition = compact and nil or v2(0.5, 0),
        },
        external = compact and { grow = 1, stretch = 1, } or nil,
    }

    local topBarContent = ui.content {
        baseTemplates.intervalH(8),
    }
    if compact then
        topBarContent:add(categoryBarLayout)
        topBarContent:add(baseTemplates.intervalH(8))
    else
        topBarContent:add({
            name = 'selectedCategoryName',
            template = baseTemplates.textHeader,
            props = {
                text = '',
                textSize = topBarHeight,
            },
        })
        topBarContent:add({
            external = { grow = 1, stretch = 1, }
        })
    end
    topBarContent:add(createSearchBar(placeholder))
    topBarContent:add({
        name = 'searchBarProxy', -- when destroying the search bar, flip the indices of these to force focus loss and free the keyboard
    })
    if props.onOpenSettings then
        topBarContent:add(baseTemplates.intervalH(4))
        topBarContent:add(specialTemplates.interactive({
            onClick = function()
                props.onOpenSettings()
            end,
            name = 'columnSettings',
        }, baseTemplates.imageButton(iconPack.getPath('edit_mode.dds'), v2(topBarHeight - 12, topBarHeight - 12), nil, 'columnSettingsButton'), ctx))
    end
    topBarContent:add(baseTemplates.intervalH(8))

    local topBarLayout = {
        name = 'topBar',
        type = ui.TYPE.Flex,
        props = {
            horizontal = true,
            autoSize = false,
            size = v2(0, topBarHeight),
            relativeSize = v2(1, 0),
            position = v2(0, 4),
            align = ui.ALIGNMENT.Start,
            arrange = ui.ALIGNMENT.Center,
        },
        content = topBarContent,
    }

    local state = {
        selectedCategory = nil,
    }

    local function getCategories()
        if props.getCategories then
            return props.getCategories() or {}
        end
        return I.InventoryExtender.getCategories()
    end

    element = ui.create({
        props = {
            size = v2(0, elementHeight),
            relativeSize = v2(1, 0),
        },
        content = compact and ui.content { topBarLayout } or ui.content {
            topBarLayout,
            categoryBarLayout,
        },
        userData = {},
    })

    local updateCategories

    local function isCategoryDisabled(category, categoryDisabledLookup)
        if categoryDisabledLookup then
            return categoryDisabledLookup[category.key] == true
        elseif props.isCategoryDisabled then
            return props.isCategoryDisabled(category)
        end
        return false
    end

    local function getDefaultCategoryKey(categories, categoryDisabledLookup)
        for _, category in ipairs(categories) do
            if category.defaultSelected ~= false and not isCategoryDisabled(category, categoryDisabledLookup) then
                return category.key
            end
        end

        return categories[1] and categories[1].key or nil
    end

    local function resolveCategoryKey(categoryKey, categories, categoryDisabledLookup)
        categories = categories or getCategories()
        for _, category in ipairs(categories) do
            if category.key == categoryKey then
                return categoryKey
            end
        end

        return getDefaultCategoryKey(categories, categoryDisabledLookup)
    end

    local function getCategoryBarContent()
        if compact then
            return element.layout.content.topBar.content.categoryBar.content.categoryBarButtons.content
        end
        return element.layout.content.categoryBar.content.categoryBarButtons.content
    end

    updateCategories = function()
        local categoryBarContent = getCategoryBarContent()
        for i = #categoryBarContent, 1, -1 do
            auxUi.deepDestroy(categoryBarContent[i])
        end

        if compact then
            element.layout.content.topBar.content.categoryBar.content.categoryBarButtons.content = ui.content {}
            categoryBarContent = element.layout.content.topBar.content.categoryBar.content.categoryBarButtons.content
        else
            element.layout.content.categoryBar.content.categoryBarButtons.content = ui.content {}
            categoryBarContent = element.layout.content.categoryBar.content.categoryBarButtons.content
        end

        local categories = getCategories()
        local categoryDisabledLookup = props.getCategoryDisabledLookup and props.getCategoryDisabledLookup() or nil
        state.selectedCategory = resolveCategoryKey(state.selectedCategory, categories, categoryDisabledLookup)

        for i, category in ipairs(categories) do
            local isSelected = state.selectedCategory == category.key
            local isDisabled = isCategoryDisabled(category, categoryDisabledLookup)
            local iconSize = v2(categoryBarHeight - compactIconPadding, categoryBarHeight - compactIconPadding)
            local iconAlpha = isSelected and vanillaOpacityStates.active or ((isDisabled and not isSelected) and vanillaOpacityStates.disabled or vanillaOpacityStates.default)
            local buttonContent

            if vanillaStyle then
                buttonContent = ui.content {
                    {
                        name = 'categoryBackground',
                        type = ui.TYPE.Image,
                        props = {
                            resource = baseTemplates.createTexture(iconPack.getPath('category_bgr.dds')),
                            relativeSize = v2(1, 0),
                            size = v2(0, iconSize.y + (compact and 6 or -2)),
                            relativePosition = v2(0.5, 0.5),
                            anchor = v2(0.5, 0.5),
                            alpha = iconAlpha,
                        },
                        userData = {
                            opacityStates = vanillaOpacityStates,
                        },
                    },
                    {
                        name = 'categoryIcon',
                        type = ui.TYPE.Image,
                        props = {
                            resource = ui.texture { path = category.icon },
                            size = iconSize + (compact and v2(4, 4) or v2(-4, -4)),
                            relativePosition = v2(0.5, 0.5),
                            anchor = v2(0.5, 0.5),
                            color = util.color.rgb(0.01, 0.01, 0.01),
                            alpha = 1,
                        },
                    }
                }
            else
                buttonContent = ui.content {
                    {
                        name = 'categoryIcon',
                        type = ui.TYPE.Image,
                        props = {
                            resource = ui.texture { path = category.icon },
                            size = iconSize,
                            relativePosition = v2(0.5, 0.5),
                            anchor = v2(0.5, 0.5),
                            color = isSelected and constants.Colors.ACTIVE or (isDisabled and constants.Colors.DISABLED or constants.Colors.DEFAULT),
                            alpha = isDisabled and 0.5 or 1,
                        },
                        userData = {
                            colorable = true,
                        }
                    }
                }
            end

            categoryBarContent:add(specialTemplates.interactive({
                canClick = function()
                    return not isDisabled
                end,
                onClick = function()
                    if state.selectedCategory ~= category.key then
                        state.selectedCategory = category.key
                        if onCategoryChange then
                            onCategoryChange(state.selectedCategory)
                        else
                            updateCategories()
                        end
                    end
                end,
                name = category.key,
            }, {
                external = { grow = 1, stretch = 1, },
                content = buttonContent,
                userData = {
                    active = isSelected,
                    disabled = isDisabled and not isSelected,
                    tooltipFn = function()
                        return specialTemplates.tooltip(4, ui.content {
                            {
                                template = baseTemplates.textNormal,
                                props = {
                                    text = category.name,
                                }
                            }
                        }, category.key)
                    end,
                },
                props = {
                    anchor = v2(0.5, 0),
                    relativeSize = v2(1 / #categories, 1),
                    relativePosition = v2((i - 0.5) / #categories, 0),
                }
            }, ctx))
        end

        if not compact then
            element.layout.content.topBar.content.selectedCategoryName.props.text = state.selectedCategory and (I.InventoryExtender.getCategory(state.selectedCategory).name or '') or ''
        end

        element:update()
    end

    updateCategories()

    element.layout.userData.updateCategories = updateCategories

    local function resizeImages(totalWidth)
        local categories = getCategories()
        if #categories == 0 then return end

        local availableWidth = totalWidth
        local barWidth = nil
        if compact then
            availableWidth = math.max(0, totalWidth - 240)
        else
            local widthPerCategory = math.max(1, math.floor(totalWidth / #categories))
            barWidth = widthPerCategory * #categories - 8
            availableWidth = barWidth
        end

        local maxSizePerCategory = availableWidth / #categories
        local imageSize = math.min(categoryBarHeight - compactIconPadding, maxSizePerCategory)
        local imageSizeVec = v2(imageSize, imageSize)

        local anyChanged = false
        if not compact and element.layout.content.categoryBar.props.size.x ~= barWidth then
            anyChanged = true
            element.layout.content.categoryBar.props.size = v2(barWidth, categoryBarHeight - 4)
        end
        for _, child in ipairs(getCategoryBarContent()) do
            if child.layout and child.layout.content.categoryIcon then
                if child.layout.content.categoryIcon.props.size ~= imageSizeVec then
                    anyChanged = true
                    child.layout.content.categoryIcon.props.size = imageSizeVec + (compact and v2(4, 4) or v2(-4, -4))
                    if child.layout.content.categoryBackground then
                        child.layout.content.categoryBackground.props.size = v2(0, imageSizeVec.y + (compact and 6 or -2))
                    end
                    child:update()
                end
            end
        end

        if anyChanged and not compact then
            element.layout.props.size = v2(0, topBarHeight + imageSize + 16)
            element:update()
        elseif anyChanged then
            element:update()
        end
    end

    element.layout.userData.resizeImages = resizeImages

    local function clearSearch()
        local content = element.layout.content.topBar.content
        local indexOfBar = content:indexOf('searchBar')
        local indexOfProxy = content:indexOf('searchBarProxy')

        if indexOfBar and indexOfProxy then
            content.searchBar = createSearchBar(placeholder)
        else
            replaceSearchBar(placeholder)
        end

        element:update()
        if onSearchChange then
            onSearchChange('')
        end
    end
    element.layout.userData.clearSearch = clearSearch

    local function cycleCategory(i)
        local categories = getCategories()
        if #categories == 0 then return end
        local buttons = getCategoryBarContent()

        local currentIndex = 1
        for index, category in ipairs(categories) do
            if category.key == state.selectedCategory then
                currentIndex = index
                break
            end
        end

        local newIndex = currentIndex
        local foundEnabled = false
        for _ = 1, #categories do
            newIndex = newIndex + i
            if newIndex < 1 then
                newIndex = #categories
            elseif newIndex > #categories then
                newIndex = 1
            end

            local button = buttons[newIndex]
            local isDisabled = button and button.layout and button.layout.userData and button.layout.userData.disabled or false
            if not isDisabled then
                foundEnabled = true
                break
            end
        end

        if not foundEnabled then
            return
        end

        state.selectedCategory = categories[newIndex].key
        if onCategoryChange then
            onCategoryChange(state.selectedCategory)
        else
            updateCategories()
        end

        local searchBar = element.layout.content.topBar.content.searchBar
        local searchText = searchBar
            and searchBar.content
            and searchBar.content.padding
            and searchBar.content.padding.content
            and searchBar.content.padding.content.textEdit
            and searchBar.content.padding.content.textEdit.props
            and searchBar.content.padding.content.textEdit.props.text
            or placeholder
        replaceSearchBar(searchText)
    end
    element.layout.userData.cycleCategory = cycleCategory

    element.layout.userData.setCategory = function(categoryKey)
        local categories = getCategories()
        local categoryDisabledLookup = props.getCategoryDisabledLookup and props.getCategoryDisabledLookup() or nil
        local resolvedCategoryKey = resolveCategoryKey(categoryKey, categories, categoryDisabledLookup)
        if state.selectedCategory ~= resolvedCategoryKey then
            state.selectedCategory = resolvedCategoryKey
            if onCategoryChange then
                onCategoryChange(state.selectedCategory)
            else
                updateCategories()
            end
        end
    end

    element.layout.userData.getCategory = function()
        return state.selectedCategory
    end

    return element
end

return CategoryFilter
