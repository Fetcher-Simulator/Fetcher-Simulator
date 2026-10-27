# Shared surf movement profile

Surf cells can opt into a server-selected movement profile without replacing a
player's attributes, skills, inventory, race or appearance. The initial references
are **Speed 100, Athletics 100, Acrobatics 50**, zero encumbrance and full fatigue.
These values are inputs to the movement formulas, not character-stat edits.

The built-in Kitsune, Mesa and Utopia cell settings enable normalization at server
startup. Existing ramp, gravity, acceleration and friction tuning is retained.
Server Lua can configure other maps using the existing physics API:

```lua
mp.setCellPhysics("my_surf_cell", {
    enabled = true,
    normalizeMovement = true,
    referenceSpeed = 100,
    referenceAthletics = 100,
    referenceAcrobatics = 50,
})
```

Reference values must be finite numbers between 0 and 100. `/surf` displays the
effective profile. Administrators can tune a cell with, for example,
`/surf cell surf_kitsune_mw referenceAcrobatics 75`. Personal `/surf` mutations are
rejected in normalized cells. The native resolver also ignores earlier personal
physics overrides there, so entering with an already configured override cannot
change the shared map settings. Outside normalized cells, existing override
precedence is preserved.

## Native behavior

- Walking/running, jump impulse and airborne input use the reference values.
  Actual Speed/Athletics/Acrobatics, attribute and skill modifiers, carried weight
  and fatigue cannot change these calculations.
- Physical movement ignores race weight and reference scale multipliers, regardless
  of the client's `normalise race speed` setting. Visual model scale, identity race,
  body race, skin parts, rig, tail and equipment are not changed by normalization.
- Jump, Levitate, Slow Fall, Water Walking and storm movement do not alter the
  normalized physics. Effects remain on the character and resume their ordinary
  influence after leaving the cell. Walk/run/sneak remain player controls with
  consistent reference-based behavior.
- Surf movement does not award Athletics/Acrobatics progression, consume movement
  fatigue, or inflict landing damage. Existing death/incapacitation checks remain.
- Each physics frame snapshots its selected cell settings. Client-side checks
  require the current player and matching cell, so stale packets cannot leave a
  movement profile active in the next cell. Disconnect resets settings as before.

No stat snapshot/restore, database migration, race swap or appearance refresh is
needed. The existing collision solver and visual model selection remain in use;
this change does not standardize arbitrary modded collision meshes or implement
leaderboard eligibility/anti-cheat validation.

The new settings require a matching client and server (introduced in protocol 26; the current Studio
source uses **protocol 27**). Protocol-25
clients are rejected by the normal handshake rather than silently running different
movement rules. The server-delivered package API remains Lua 144 / multiplayer 12.

## Validation — 2026-10-26

- Native client, server and component-test targets build with MSVC 2022.
- Five `SurfMovement` component tests cover default opt-in, personal-override
  precedence, map exit, settings serialization/truncation and invalid references.
  The relevant appearance and handshake checks pass with them: 11 tests total.
- `lua scripts/test_surf_movement.lua .`: 32 checks cover built-in maps, unchanged
  map tuning, shared-profile commands, range checks and ordinary-cell behavior.
- An isolated local server using the installed Vanilla profile and real Kitsune
  spawn passes native playback with low versus high stats, overencumbrance, an
  Imperial/Orc body change, movement magic and cell exit. Walk/run formula results
  match exactly. Short physical run samples, including movement magic, remain
  within 5% in the final run; these frame-sampled acceleration measurements include
  frame scheduling variance. Jump heights with low skills, high skills and all three
  Jump/Slow Fall/Levitate buffs differ by less than 0.01 game units (about 91.4 units
  at the tested gravity). The low-skill jump does not train Acrobatics.
- Appearance remains unchanged by profile activation/exit, real stat values remain
  as set by the private test, and ordinary movement resumes outside the map.

Private logs and helpers are retained under `build/surf-normalization-qa`. The test
uses its own database, profile, key copy and UDP port 25571; it does not modify the
normal install, existing port-25567 fixture or VPS servers.
