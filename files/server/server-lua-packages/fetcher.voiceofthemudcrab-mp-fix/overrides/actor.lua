local core = require('openmw.core')
local nearby = require('openmw.nearby')
local types = require('openmw.types')
local async = require('openmw.async')
local storage = require('openmw.storage')

local aux_util = require('openmw_aux.util')

local voiceData = require('scripts.VoiceoftheMudcrab.voice_data')

local lastFile = ''
local inCombat = false

local self = require('openmw.self')

-- Stop only this mod's voice, leaving animation death sounds intact.
local function stopVoiceOnDeath()
    if not types.Actor.isDead(self) then return false end
    if lastFile ~= '' then
        core.sound.stopSoundFile3d(lastFile, self)
        lastFile = ''
    end
    return true
end

local function speak(category)
    if stopVoiceOnDeath() then return end
    if self.type == types.NPC or core.sound.isSoundFilePlaying(lastFile, self) then return end
    if self.object.recordId == 'mudcrab' or self.object.recordId == 'mudcrab_unique' then
        local player = nearby.players[1]
        local voiceDataTarget = aux_util.shallowCopy(voiceData.voices['mudcrab'][category])

        local index = voiceDataTarget[math.random(#voiceDataTarget)]
        core.sound.playSoundFile3d(index.file, self, {pitch=2.0, volume=2.0})
        lastFile = index.file
        return true
    end
end

local function activateIdle()
    if not inCombat then 
        speak('idle')
    else 
        speak('attack')
    end
    async:newUnsavableSimulationTimer(math.random(2,5), activateIdle)
end

local function votm_Combat(eventData)
    if eventData.status then
        inCombat = true
    else
        inCombat = false
    end
end

local function setupAutomaticLines()
    activateIdle()
end

return {
    engineHandlers = {
        onActive = setupAutomaticLines,
        onUpdate = stopVoiceOnDeath,
    },
    eventHandlers = {
        votm_Combat = votm_Combat,
    }
}