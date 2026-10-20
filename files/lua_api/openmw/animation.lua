---
-- Defines functions that allow control of character animations.
-- Note that for some methods, such as @{openmw.animation#playBlended} you should use the associated methods on the 
-- [AnimationController](interface_animation.html) interface rather than invoking this API directly.
-- @context local
-- @module animation
-- @usage local anim = require('openmw.animation')

--- Possible @{#Priority} values
-- @field [parent=#animation] #Priority PRIORITY

--- `animation.PRIORITY`
-- @type Priority
-- @field #number Default "0"
-- @field #number WeaponLowerBody "1"
-- @field #number SneakIdleLowerBody "2"
-- @field #number SwimIdle "3"
-- @field #number Jump "4"
-- @field #number Movement "5"
-- @field #number Hit "6"
-- @field #number Weapon "7"
-- @field #number Block "8"
-- @field #number Knockdown "9"
-- @field #number Torch "10"
-- @field #number Storm "11"
-- @field #number Death "12" 
-- @field #number Scripted "13" Special priority used by scripted animations. When any animation with this priority is present, all animations without this priority are paused.

--- Possible @{#BlendMask} values
-- @field [parent=#animation] #BlendMask BLEND_MASK

--- `animation.BLEND_MASK`
-- @type BlendMask
-- @field #number LowerBody "1" All bones from 'Bip01 pelvis' and below
-- @field #number Torso "2" All bones from 'Bip01 Spine1' and up, excluding arms
-- @field #number LeftArm "4" All bones from 'Bip01 L Clavicle' and out
-- @field #number RightArm "8" All bones from 'Bip01 R Clavicle' and out
-- @field #number UpperBody "14" All bones from 'Bip01 Spine1' and up, including arms
-- @field #number All "15" All bones

--- Possible @{#BoneGroup} values
-- @field [parent=#animation] #BoneGroup BONE_GROUP

--- `animation.BONE_GROUP`
-- @type BoneGroup
-- @field #number LowerBody "1" All bones from 'Bip01 pelvis' and below
-- @field #number Torso "2" All bones from 'Bip01 Spine1' and up, excluding arms
-- @field #number LeftArm "3" All bones from 'Bip01 L Clavicle' and out
-- @field #number RightArm "4" All bones from 'Bip01 R Clavicle' and out


---
-- Check if the object has an animation object or not
-- @function [parent=#animation] hasAnimation
-- @param openmw.core#GameObject actor
-- @return #boolean

---
-- Skips animations for one frame, equivalent to mwscript's SkipAnim.
-- Can only be used on self.
-- @function [parent=#animation] skipAnimationThisFrame
-- @param openmw.core#GameObject actor

---
-- Get the absolute position within the animation track of the given text key
-- @function [parent=#animation] getTextKeyTime
-- @param openmw.core#GameObject actor
-- @param #string text key
-- @return #number

---
-- Check if the given animation group is currently playing
-- @function [parent=#animation] isPlaying
-- @param openmw.core#GameObject actor
-- @param #string groupName
-- @return #boolean

---
-- Get the current absolute time of the given animation group if it is playing, or -1 if it is not playing.
-- @function [parent=#animation] getCurrentTime
-- @param openmw.core#GameObject actor
-- @param #string groupName
-- @return #number

---
-- Check whether the animation is a looping animation or not. This is determined by a combination 
-- of groupName, some of which are hardcoded to be looping, and the presence of loop start/stop keys.
-- The groupNames that are hardcoded as looping are the following, as well as per-weapon-type suffixed variants of each.
-- "walkforward", "walkback", "walkleft", "walkright", "swimwalkforward", "swimwalkback", "swimwalkleft", "swimwalkright", 
-- "runforward", "runback", "runleft", "runright", "swimrunforward", "swimrunback", "swimrunleft", "swimrunright", 
-- "sneakforward", "sneakback", "sneakleft", "sneakright", "turnleft", "turnright", "swimturnleft", "swimturnright",
-- "spellturnleft", "spellturnright", "torch", "idle", "idle2", "idle3", "idle4", "idle5", "idle6", "idle7", "idle8", 
-- "idle9", "idlesneak", "idlestorm", "idleswim", "jump", "inventoryhandtohand", "inventoryweapononehand", 
-- "inventoryweapontwohand", "inventoryweapontwowide"
-- @function [parent=#animation] isLoopingAnimation
-- @param openmw.core#GameObject actor
-- @param #string groupName
-- @return #boolean


---
-- Cancels and removes the animation group from the list of active animations.
-- Can only be used on self.
-- @function [parent=#animation] cancel
-- @param openmw.core#GameObject actor
-- @param #string groupName

---
-- Enables or disables looping for the given animation group. Looping is enabled by default.
-- Can only be used on self.
-- @function [parent=#animation] setLoopingEnabled
-- @param openmw.core#GameObject actor
-- @param #string groupName
-- @param #boolean enabled

---
-- Returns the completion of the animation, or nil if the animation group is not active.
-- @function [parent=#animation] getCompletion
-- @param openmw.core#GameObject actor
-- @param #string groupName
-- @return #number, #nil

---
-- Returns the remaining number of loops, not counting the current loop, or nil if the animation group is not active.
-- @function [parent=#animation] getLoopCount
-- @param openmw.core#GameObject actor
-- @param #string groupName
-- @return #number, #nil

---
-- Get the current playback speed of an animation group, or nil if the animation group is not active.
-- @function [parent=#animation] getSpeed
-- @param openmw.core#GameObject actor
-- @param #string groupName
-- @return #number, #nil

---
-- Modifies the playback speed of an animation group.
-- Note that this is not sticky and only affects the speed until the currently playing sequence ends.
-- Can only be used on self.
-- @function [parent=#animation] setSpeed
-- @param openmw.core#GameObject actor
-- @param #string groupName
-- @param #number speed The new animation speed, where speed=1 is normal speed.

---
-- Clears all animations currently in the animation queue. This affects animations played by mwscript, @{openmw.animation#playQueued}, and ai packages, but does not affect animations played using @{openmw.animation#playBlended}.
-- Can only be used on self.
-- @function [parent=#animation] clearAnimationQueue
-- @param openmw.core#GameObject actor
-- @param #boolean clearScripted whether to keep animation with priority Scripted or not.

---
-- Acts as a slightly extended version of MWScript's LoopGroup. Plays this animation exclusively
-- until it ends, or the queue is cleared using #clearAnimationQueue. Use #clearAnimationQueue and the `startkey` option
-- to imitate the behavior of LoopGroup's play modes.
-- Can only be used on self.
-- @function [parent=#animation] playQueued
-- @param openmw.core#GameObject actor
-- @param #string groupName
-- @param #table options A table of play options.  Can contain:
--
--   * `loops` - a number >= 0, the number of times the animation should loop after the first play (default: infinite).
--   * `speed` - a floating point number >= 0, the speed at which the animation should play (default: 1);
--   * `startKey` - the animation key at which the animation should start (default: "start")
--   * `stopKey` - the animation key at which the animation should end (default: "stop")
--   * `forceLoop` - a boolean, to set if the animation should loop even if it's not a looping animation (default: false)
-- 
-- @usage -- Play death1 without waiting. Equivalent to playgroup, death1, 1
-- anim.clearAnimationQueue(self, false)
-- anim.playQueued(self, 'death1')
-- 
-- @usage -- Play an animation group with custom start/stop keys
-- anim.clearAnimationQueue(self, false)
-- anim.playQueued(self, 'spellcast', { startKey = 'self start', stopKey = 'self stop' })
--

---
-- Play an animation directly. You probably want to use the [AnimationController](interface_animation.html) interface, which will trigger relevant handlers, 
-- instead of calling this directly. Note that the still hardcoded character controller may at any time and for any reason alter
-- or cancel currently playing animations, so making your own calls to this function either directly or through the [AnimationController](interface_animation.html)
-- interface may be of limited utility. For now, use openmw.animation#playQueued to script your own animations.
-- Can only be used on self.
-- @function [parent=#animation] playBlended
-- @param openmw.core#GameObject actor
-- @param #string groupName
-- @param #table options A table of play options. Can contain:
--
--   * `loops` - a number >= 0, the number of times the animation should loop after the first play (default: 0).
--   * `priority` - Either a single #Priority value that will be assigned to all bone groups. Or a table mapping bone groups to its priority (default: PRIORITY.Default).
--   * `blendMask` - A mask of which bone groups to include in the animation (Default: BLEND_MASK.All).
--   * `autoDisable` - If true, the animation will be immediately  removed upon finishing, which means information will not be possible to query once completed. (Default: true)
--   * `speed` - a floating point number >= 0, the speed at which the animation should play (default: 1)
--   * `startKey` - the animation key at which the animation should start (default: "start")
--   * `stopKey` - the animation key at which the animation should end (default: "stop")
--   * `startPoint` - a floating point number 0 <= value <= 1, starting completion of the animation (default: 0)
--   * `forceLoop` - a boolean, to set if the animation should loop even if it's not a looping animation (default: false)

---
-- Check if the actor's animation has the given animation group or not.
-- @function [parent=#animation] hasGroup
-- @param openmw.core#GameObject actor
-- @param #string groupName
-- @return #boolean 

---
-- Check if the actor's skeleton has the given bone or not
-- @function [parent=#animation] hasBone
-- @param openmw.core#GameObject actor
-- @param #string boneName
-- @return #boolean 

---
-- Get the current active animation for a bone group
-- @function [parent=#animation] getActiveGroup
-- @param openmw.core#GameObject actor
-- @param #number boneGroup Bone group enum, see @{openmw.animation#BONE_GROUP}
-- @return #string 

---
-- Plays a VFX on the actor.
-- Can be used on self, or on a global object from a global script. Local scripts can also invoke it on another actor by sending an AddVfx event to that actor.
-- @function [parent=#animation] addVfx
-- @param openmw.core#GameObject actor
-- @param #string model path (normally taken from a record such as @{openmw.types#StaticRecord.model} or similar)
-- @param #table options optional table of parameters. Can contain:
--
--   * `loop` - boolean, if true the effect will loop until removed (default: false).
--   * `boneName` - name of the bone to attach the vfx to. (default: "")
--   * `particleTextureOverride` - name of the particle texture to use. (default: "")
--   * `vfxId` - a string ID that can be used to remove the effect later, using #removeVfx, and to avoid duplicate effects. The default value of "" can have duplicates. To avoid interaction with the engine, use unique identifiers unrelated to magic effect IDs. The engine uses this identifier to add and remove magic effects based on what effects are active on the actor. If this is set equal to the @{openmw.core#MagicEffectId} identifier of the magic effect being added, for example core.magic.EFFECT_TYPE.FireDamage, then the engine will remove it once the fire damage effect on the actor reaches 0. (Default: "").
--   * `useAmbientLight` - boolean, vfx get a white ambient light attached in Morrowind. If false don't attach this. (default: true)
--   * `autoTransform` - boolean, if true, the engine will auto-calculate the transform. (default: true)
--   * `transform` - relative transform (openmw.util#Transform) applied to the vfx. If autoTransform is true this will be applied on top of it.
--   * `worldTransform` - world-space matrix transform (openmw.util#Transform) including position. Used with `attachToNearestBone` to preserve an exact world-space attachment point and orientation.
--   * `attachToNearestBone` - boolean. If true, attach to the animation bone nearest `worldTransform` and convert that transform into the bone local space. Requires `worldTransform`. (default: false)
--   * `nearestBoneOffsetScale` - number from 0 to 1. When attaching to the nearest bone, scales the world-impact offset toward the selected bone before converting to bone-local space. `1` preserves the exact world position; smaller values pull coarse collision hits toward the rendered skeleton. (default: 1)
--
-- @usage local mgef = core.magic.effects.records[myEffectName]
-- anim.addVfx(self, 'VFX_Hands', {boneName = 'Bip01 L Hand', particleTextureOverride = mgef.particle, loop = mgef.continuousVfx, vfxId = mgef.id..'_myuniquenamehere'})
-- -- later:
-- anim.removeVfx(self, mgef.id..'_myuniquenamehere')
-- 
-- @usage -- Add vfx to another actor using an event
-- local mgef = core.magic.effects.records[myEffectName]
-- target:sendEvent('AddVfx', {
--   model = types.Static.record(mgef.hitStatic).model,
--   options = {
--     vfxId = mgef.id,
--     particleTextureOverride = mgef.particle,
--     loop = false,
--     transform = util.transform.move(100, 0, 0)
--   }
-- })
--


---
-- Removes a specific VFX.
-- Can only be used on self.
-- @function [parent=#animation] removeVfx
-- @param openmw.core#GameObject actor
-- @param #string vfxId a string ID that uniquely identifies the VFX to remove

---
-- Removes all vfx from the actor.
-- Can only be used on self.
-- @function [parent=#animation] removeAllVfx
-- @param openmw.core#GameObject actor




--- Version of the optional visual bone-transform extension (currently 1).
-- @field [parent=#animation] #number boneTransformsVersion

--- Lists named transform nodes/bones in the object's current rendered model.
-- Local/player scripts only. Call during `onFrame` or an input callback.
-- Names are matched case-insensitively by the other bone functions.
-- @function [parent=#animation] getBoneNames
-- @param openmw.core#GameObject object
-- @return #list<#string>

--- Reads the last rendered bone transform, or nil for a missing bone.
-- Call during `onFrame` or an input callback; asynchronous reads are rejected.
-- The result includes scale and can be applied to a vector to obtain a position.
-- `unposed` excludes this API's offsets on the bone and its ancestors, allowing
-- IK to use the underlying animated pose without feeding its own output back.
-- @function [parent=#animation] getBoneTransform
-- @param openmw.core#GameObject object
-- @param #string bone
-- @param #boolean worldSpace Optional, default false (parent-local space).
-- @param #boolean unposed Optional, default false.
-- @return openmw.util#Transform
-- @return openmw.util#Transform Rotation only, with scale/translation removed.

--- Queues a temporary visual offset after normal animation and look controllers.
-- Available to local/player scripts for loaded objects, including remote actors.
-- Position is an offset in the bone parent's coordinates (game units). Rotation
-- is a util.transform rotation in the bone's own coordinates. It preserves scale.
-- Defaults: position zero, rotation identity, weight 1, timeout 0.5 real seconds.
-- Weight must be 0..1, timeout 0.05..2, each position component within ±1000;
-- all values must be finite and rotation nonzero. At most 64 bones per object.
-- Refresh during `onFrame`; expiry restores normal animation even if Lua stops.
-- Missing/unloaded/disabled objects at action execution are skipped. Model rebuilds
-- discard offsets. These offsets do not change physics, root motion, saved games,
-- or network state; mods must synchronize performance intent separately.
-- @function [parent=#animation] setBoneTransform
-- @param openmw.core#GameObject object
-- @param #string bone
-- @param #table options Fields: position, rotation, weight, timeout.
-- @return #boolean True when queued; use getBoneNames to check node availability.
-- @usage
-- -- In a player script's onFrame handler:
-- anim.setBoneTransform(actor, 'Bip01 R Hand', {
--     rotation = util.transform.rotateX(0.1), weight = 1, timeout = 0.5,
-- })

--- Removes one scripted offset and restores the underlying animated transform.
-- Queued at the same synchronized boundary as setBoneTransform.
-- @function [parent=#animation] clearBoneTransform
-- @param openmw.core#GameObject object
-- @param #string bone

--- Version of the optional expiring local visual attachment extension (1).
-- @field [parent=#animation] #number visualAttachmentsVersion

--- Adds or refreshes a presentation-only model on a loaded actor, including remote actors.
-- Queued at the synchronized action boundary. Does not create an inventory item,
-- change equipment, apply physics or replicate intent. Up to eight keys per actor.
-- Call from a local/player script. Refresh while needed; expiry uses real time.
-- @function [parent=#animation] setVisualAttachment
-- @param openmw.core#GameObject actor
-- @param #string key 1..64 ASCII letters/digits/underscore/hyphen; scoped separately from VFX IDs.
-- @param #string model NIF path, at most 256 bytes.
-- @param #table options `bone` (default Bip01 Spine2), `transform` (bone-local util.transform),
--   `timeout` (0.05..2 seconds, default 0.5). Finite transform components must be within +/-1000.
-- @return #boolean True if queued; missing models/bones are checked when the action runs.

--- Removes only this API's attachment with the given key.
-- @function [parent=#animation] clearVisualAttachment
-- @param openmw.core#GameObject actor
-- @param #string key

--- Number of active attachments from this API; read from onFrame or an input callback.
-- @function [parent=#animation] visualAttachmentCount
-- @param openmw.core#GameObject actor
-- @return #number

return nil

