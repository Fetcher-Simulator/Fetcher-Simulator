# Server-hosted Studio MIDI

Studio supports both installed personal MIDI files and a server-hosted library.
The server advertises its catalogue when a player acquires a keyboard. The existing
MIDI/song dropdown marks server songs with `[Server]`. Selecting Play or Import
downloads only the selected file through the game connection; listeners still
receive the compiled performance, not a copy of the entire MIDI library.

Place `.mid`/`.midi` files (subdirectories supported) under `studio-midi/` in the
server working directory. This is deliberately separate from Bardcraft's legacy
executable-relative directory: multiple servers sharing one executable keep their
libraries separate. A server-owned symlink for the library root is supported;
file symlinks escaping that root are rejected. Neither a client request nor Lua
can supply an arbitrary filesystem root to the native MIDI bindings.

The native catalogue includes names, byte sizes and SHA-256 content revisions.
Reads must match the advertised revision. Files may be at most 512 KiB, paths at
most 187 UTF-8 bytes before the `midi/` prefix, and a catalogue at most 1024 songs.
Directory traversal and non-MIDI extensions are rejected. Names containing
consecutive dots are omitted to match Studio's saved-path rules. RIFF/RMID
containers are unwrapped during reads without modifying original files.
The SMF parser still enforces its event, track and timing limits.

The server sends one bounded packet per update, round-robin across receivers.
Catalogues use pages of 32 entries; file chunks are at most 16 KiB. Only the
current nearby keyboard owner can request transfers. Closing, disconnecting or
changing cells cancels queued transfers. Catalogues refresh at most once per
minute; reopening a keyboard requests the current catalogue. File requests and
catalogue requests have separate rate limits. Both server and client file caches
are bounded at 8 MiB; client data stays in memory for the current connection.
There is no player upload or automatic filesystem installation in this feature.

Personal files and exports retain their existing readers. If a server entry has
the same normalized VFS path as an installed personal file, the single dropdown
entry is marked `[Server]` and uses the server revision. Other personal paths
continue to work normally. Leaving a server clears its catalogue and file cache.
Saved projects can wait for the catalogue before restoring an unedited MIDI
source; cancelled/replaced imports reject late responses using keyboard epoch
and a separate request ID.

Delivery requires the server-side `mp.listStudioMidiFiles` and
`mp.readStudioMidiFile` bindings, `procedural_synth_midi.lua`, and the version-30
Studio Lua package. Existing compatible clients receive the Lua update on
reconnect; no client executable or launcher/content archive update is required.

Validation:

```
lua scripts/test_procedural_synth_hosted_midi.lua
lua scripts/test_procedural_synth_client.lua
lua scripts/test_procedural_synth.lua
g++ -std=c++20 -I. scripts/test_studio_midi_library.cpp -o test-studio-midi
./test-studio-midi /absolute/path/to/new-empty-test-directory
```

The native test's optional second path scans/re-reads the 600-song operator test
library. Native tests cover containment, missing/oversized/invalid files, stable
hashes and ordering, and RIFF normalization. Transport tests cover two-client
fairness, exact bytes, scope/revision checks, cancellation, timeout, missing files
and repeated downloads beyond the cache budget. The real player harness covers
server-only source restoration, personal-file coexistence, import and disconnect.
