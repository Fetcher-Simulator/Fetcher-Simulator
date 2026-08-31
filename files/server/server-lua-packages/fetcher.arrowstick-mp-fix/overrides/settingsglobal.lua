local I = require('openmw.interfaces')

I.Settings.registerGroup {
    key = 'SettingsArrowStick',
    page = 'ArrowStick',
    l10n = 'ArrowStick',
    name = 'settings_groupName',
    permanentStorage = true,
    order = 1,
    settings = {
        {
            key = 'despawnArrows',
            name = 'despawnArrows_name',
            description = "despawnArrows_desc",
            renderer = 'checkbox',
            default = false,
        },
        {
            key = 'stickChance',
            name = 'stickChance_name',
            description = 'stickChance_desc',
            renderer = "SuperSlider6",
            default = 100,
            argument = {
                default = 100,
                min = -1,
                showDefaultMark = true,
                showResetButton = true,
                bottomRow = true,
                unit = "%",
            },
        },
        {
            key = 'stickEnchanted',
            name = 'stickEnchanted_name',
            description = "stickEnchanted_desc",
            renderer = 'checkbox',
            default = false,
        },
        {
            key = 'stickUnderwater',
            name = 'stickUnderwater_name',
            description = "stickUnderwater_desc",
            renderer = 'checkbox',
            default = false,
        },
    }
}

I.Settings.registerGroup {
    key = 'SettingsArrowStick_impactEffects',
    page = 'ArrowStick',
    l10n = 'ArrowStick',
    name = 'impactEffects_groupName',
    description = "impactEffects_groupDesc",
    permanentStorage = true,
    order = 100,
    settings = {
        {
            key = 'impactEffects',
            name = 'impactEffects_name',
            description = "impactEffects_desc",
            renderer = 'checkbox',
            default = true,
        },
        {
            key = 'checkMaterial',
            name = 'checkMaterial_name',
            description = "checkMaterial_desc",
            renderer = 'checkbox',
            default = false,
        },
    }
}