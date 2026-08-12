# Multiplayer quest journal persistence

The multiplayer server persists quest journal entries and quest indices against
the character that originally produced each change. `Config.JOURNAL_SHARING`
controls which persisted character records are combined and sent to a player:

- `"player"` (default) loads only the selected character's journal.
- `"group"` combines journals for members of the selected character's named
  `Config.JOURNAL_GROUPS` group. A character not assigned to a group remains
  player-specific.
- `"server"` combines journals produced by every character in the database.

Because records retain their originating character, changing the sharing mode
does not delete, duplicate, or reassign stored journal data.

## Configuring groups

Groups are keyed by an operator-chosen name. Members use the authenticated
account name and can optionally restrict membership to one character slot:

```lua
Config.JOURNAL_SHARING = "group"
Config.JOURNAL_GROUPS = {
    fellowship = {
        { account = "alice", character = "Nerevar" },
        { account = "bob" }, -- every character on bob's account
    },
}
```

A member should appear in no more than one group. Group names are sorted before
membership is resolved, making accidental duplicate assignments deterministic,
but the configuration should still be corrected.

## MWScript behavior

Only the journal result is synchronized. The receiving client directly applies
the resolved entry text, timestamp, and quest index; it does not execute the
originating `Journal` or `SetJournalIndex` MWScript instruction. Arbitrary
MWScript global and local variables remain local unless another multiplayer
system explicitly synchronizes them.

Consequently, `"player"` mode never advances another player's quest journal.
`"group"` and `"server"` modes intentionally make `GetJournalIndex` observe the
shared quest state on recipients after a journal update arrives.

## Restore ordering

The server begins the pre-world bootstrap by sending runtime record definitions
and the authoritative journal while the client is still outside the world.
`CharacterData` can travel on a different transport lane and may arrive early,
so its arrival is not itself permission to enter the world. The client retains
it until `RuntimeContentBootstrapComplete` has been observed, all required
runtime definitions have installed successfully, and the required
server-supplied OpenMW Lua package set has staged and activated. The final
semantic-state gate also requires authoritative crime, faction, and known-topic
snapshots. Faction state is mirrored into normal `NpcStats` before returning-
player Lua `onLoad`, and known topics are mirrored into the normal
`DialogueManager`; neither operation replays the script or dialogue result that
originally caused it. Large journal snapshots use `Set` followed by `Append`
chunks and carry an explicit final-chunk marker, so no partial snapshot is
exposed.

Before clearing or mutating the local journal, the client verifies that every
referenced quest Dialogue and INFO exists in the effective ESM store. A
snapshot or live `Add` remains queued if its typed Dialogue definition has not
arrived yet and is retried after dynamic-record insertion. Snapshot replacement
is therefore atomic and definition-first; reconnect cannot silently discard a
dynamically supplied journal entry. Snapshot entries suppress notifications,
while live shared additions display the normal notification once.

Applying either form directly constructs the semantic journal entry/index. It
does not run the INFO result script and does not replay any MWScript instruction
that originally produced the state.

The ordering relevant to dynamically supplied quests is therefore:

```text
typed Dialogue/INFO definitions installed in the effective ESM store
    -> server Lua executable policy activated
    -> authoritative crime/faction/topic snapshots received
    -> CharacterData released
    -> faction tuple applied to normal NpcStats
    -> known topics applied to normal DialogueManager
    -> journal entries and indices applied against those definitions
```

On reconnect or server restart, a dynamically supplied quest giver sees the
restored faction tuple and known topic set through ordinary dialogue filters,
while journal state is reconstructed directly. No client re-executes another
player's INFO result script merely because a semantic result was persisted or
shared.

The current packet and database implementation covers quest entries and quest
indices. Dialogue topic-response history and read-book tracking use separate
multiplayer message IDs and are not part of this journal persistence policy.
