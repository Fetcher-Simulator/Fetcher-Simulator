#include "PlayerDatabase.hpp"

#include <algorithm>
#include <chrono>
#include <components/debug/debuglog.hpp>
#include <components/openmw-mp/SpellbookSync.hpp>
#include <sqlite3.h>

#include <ctime>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace mwmp
{

    // ============================================================================
    //  Schema
    // ============================================================================

    static const char* kSchema = R"SQL(
PRAGMA journal_mode = WAL;
PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS accounts (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    username      TEXT    UNIQUE NOT NULL,
    created_at    INTEGER NOT NULL DEFAULT 0,
    password_hash TEXT    NOT NULL DEFAULT ''
);

CREATE TABLE IF NOT EXISTS characters (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    account_id  INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    name        TEXT    NOT NULL,
    cell        TEXT    NOT NULL DEFAULT '',
    pos_x       REAL    NOT NULL DEFAULT 0,
    pos_y       REAL    NOT NULL DEFAULT 0,
    pos_z       REAL    NOT NULL DEFAULT 0,
    rot_x       REAL    NOT NULL DEFAULT 0,
    rot_y       REAL    NOT NULL DEFAULT 0,
    rot_z       REAL    NOT NULL DEFAULT 0,
    is_new      INTEGER NOT NULL DEFAULT 1,
    last_seen   INTEGER NOT NULL DEFAULT 0,
    race        TEXT    NOT NULL DEFAULT '',
    head_mesh   TEXT    NOT NULL DEFAULT '',
    hair_mesh   TEXT    NOT NULL DEFAULT '',
    is_male     INTEGER NOT NULL DEFAULT 1,
    class_id    TEXT    NOT NULL DEFAULT '',
    class_name  TEXT    NOT NULL DEFAULT '',
    birth_sign  TEXT    NOT NULL DEFAULT '',
    class_data  TEXT    NOT NULL DEFAULT '',
    nickname    TEXT    NOT NULL DEFAULT '',
    inventory_saved INTEGER NOT NULL DEFAULT 0,
    equipment_saved INTEGER NOT NULL DEFAULT 0,
    stats_saved INTEGER NOT NULL DEFAULT 0,
    level INTEGER NOT NULL DEFAULT 1,
    level_progress REAL NOT NULL DEFAULT 0,
    inventory_revision INTEGER NOT NULL DEFAULT 0,
    spellbook_saved INTEGER NOT NULL DEFAULT 0,
    spellbook_revision INTEGER NOT NULL DEFAULT 0
);

CREATE INDEX IF NOT EXISTS idx_chars_account ON characters(account_id);

-- Ed25519 keypairs — one account may have many registered public keys.
-- Authentication via keypair is an alternative to password auth.
CREATE TABLE IF NOT EXISTS account_keypairs (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    account_id  INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    public_key  TEXT    NOT NULL UNIQUE,   -- base64-encoded Ed25519 public key (32 bytes)
    label       TEXT    NOT NULL DEFAULT '', -- e.g. "home PC", "laptop"
    created_at  INTEGER NOT NULL DEFAULT 0
);

CREATE INDEX IF NOT EXISTS idx_keypairs_account ON account_keypairs(account_id);

CREATE TABLE IF NOT EXISTS world_objects (
    mp_num      INTEGER PRIMARY KEY,
    cell_id     TEXT    NOT NULL,
    ref_id      TEXT    NOT NULL,
    item_count  INTEGER NOT NULL DEFAULT 1,
    pos_x       REAL    NOT NULL DEFAULT 0,
    pos_y       REAL    NOT NULL DEFAULT 0,
    pos_z       REAL    NOT NULL DEFAULT 0,
    rot_x       REAL    NOT NULL DEFAULT 0,
    rot_y       REAL    NOT NULL DEFAULT 0,
    rot_z       REAL    NOT NULL DEFAULT 0
);

CREATE INDEX IF NOT EXISTS idx_world_objects_cell ON world_objects(cell_id);

CREATE TABLE IF NOT EXISTS world_spawned_actors (
    mp_num            INTEGER PRIMARY KEY,
    cell_id           TEXT    NOT NULL,
    ref_id            TEXT    NOT NULL,
    ref_num           INTEGER NOT NULL DEFAULT 0,
    persistent        INTEGER NOT NULL DEFAULT 1,
    pos_x             REAL    NOT NULL DEFAULT 0,
    pos_y             REAL    NOT NULL DEFAULT 0,
    pos_z             REAL    NOT NULL DEFAULT 0,
    rot_x             REAL    NOT NULL DEFAULT 0,
    rot_y             REAL    NOT NULL DEFAULT 0,
    rot_z             REAL    NOT NULL DEFAULT 0,
    health_base       REAL    NOT NULL DEFAULT 0,
    health_current    REAL    NOT NULL DEFAULT 0,
    health_mod        REAL    NOT NULL DEFAULT 0,
    magicka_base      REAL    NOT NULL DEFAULT 0,
    magicka_current   REAL    NOT NULL DEFAULT 0,
    magicka_mod       REAL    NOT NULL DEFAULT 0,
    fatigue_base      REAL    NOT NULL DEFAULT 0,
    fatigue_current   REAL    NOT NULL DEFAULT 0,
    fatigue_mod       REAL    NOT NULL DEFAULT 0,
    is_dead           INTEGER NOT NULL DEFAULT 0,
    death_state       INTEGER NOT NULL DEFAULT 0,
    death_anim_group  TEXT    NOT NULL DEFAULT '',
    created_at        INTEGER NOT NULL DEFAULT 0,
    updated_at        INTEGER NOT NULL DEFAULT 0
);

CREATE INDEX IF NOT EXISTS idx_world_spawned_actors_cell
    ON world_spawned_actors(cell_id);

CREATE TABLE IF NOT EXISTS world_dead_vanilla_actors (
    ref_id            TEXT    NOT NULL,
    ref_num           INTEGER NOT NULL DEFAULT 0,
    cell_id           TEXT    NOT NULL,
    pos_x             REAL    NOT NULL DEFAULT 0,
    pos_y             REAL    NOT NULL DEFAULT 0,
    pos_z             REAL    NOT NULL DEFAULT 0,
    rot_x             REAL    NOT NULL DEFAULT 0,
    rot_y             REAL    NOT NULL DEFAULT 0,
    rot_z             REAL    NOT NULL DEFAULT 0,
    health_base       REAL    NOT NULL DEFAULT 0,
    health_current    REAL    NOT NULL DEFAULT 0,
    health_mod        REAL    NOT NULL DEFAULT 0,
    magicka_base      REAL    NOT NULL DEFAULT 0,
    magicka_current   REAL    NOT NULL DEFAULT 0,
    magicka_mod       REAL    NOT NULL DEFAULT 0,
    fatigue_base      REAL    NOT NULL DEFAULT 0,
    fatigue_current   REAL    NOT NULL DEFAULT 0,
    fatigue_mod       REAL    NOT NULL DEFAULT 0,
    is_dead           INTEGER NOT NULL DEFAULT 1,
    death_state       INTEGER NOT NULL DEFAULT 0,
    is_instant_death  INTEGER NOT NULL DEFAULT 1,
    death_anim_group  TEXT    NOT NULL DEFAULT '',
    created_at        INTEGER NOT NULL DEFAULT 0,
    updated_at        INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY(ref_id, ref_num)
);

CREATE INDEX IF NOT EXISTS idx_world_dead_vanilla_actors_cell
    ON world_dead_vanilla_actors(cell_id);

CREATE TABLE IF NOT EXISTS world_containers (
    cell_id        TEXT    NOT NULL,
    ref_id         TEXT    NOT NULL,
    ref_num        INTEGER NOT NULL DEFAULT 0,
    mp_num         INTEGER NOT NULL DEFAULT 0,
    has_authority  INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY(cell_id, ref_id, ref_num)
);

CREATE TABLE IF NOT EXISTS world_container_items (
    cell_id      TEXT    NOT NULL,
    ref_id       TEXT    NOT NULL,
    ref_num      INTEGER NOT NULL DEFAULT 0,
    item_index   INTEGER NOT NULL,
    item_ref_id  TEXT    NOT NULL,
    item_count   INTEGER NOT NULL DEFAULT 0,
    charge       INTEGER NOT NULL DEFAULT -1,
    PRIMARY KEY(cell_id, ref_id, ref_num, item_index),
    FOREIGN KEY(cell_id, ref_id, ref_num)
        REFERENCES world_containers(cell_id, ref_id, ref_num)
        ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_world_container_items_parent
    ON world_container_items(cell_id, ref_id, ref_num);

CREATE TABLE IF NOT EXISTS world_doors (
    cell_id      TEXT    NOT NULL,
    ref_id       TEXT    NOT NULL,
    ref_num      INTEGER NOT NULL DEFAULT 0,
    mp_num       INTEGER NOT NULL DEFAULT 0,
    is_open      INTEGER NOT NULL DEFAULT 0,
    is_locked    INTEGER NOT NULL DEFAULT 0,
    lock_level   INTEGER NOT NULL DEFAULT 0,
    revision     INTEGER NOT NULL DEFAULT 1,
    PRIMARY KEY(cell_id, ref_id, ref_num)
);

CREATE INDEX IF NOT EXISTS idx_world_doors_cell ON world_doors(cell_id);

CREATE TABLE IF NOT EXISTS world_metadata (
    key    TEXT    PRIMARY KEY,
    value  INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS world_dynamic_records (
    record_type   TEXT    NOT NULL,
    record_id     TEXT    NOT NULL,
    record_scope  TEXT    NOT NULL DEFAULT 'permanent',
    record_data   BLOB    NOT NULL,
    schema_version INTEGER NOT NULL DEFAULT 0,
    created_at    INTEGER NOT NULL DEFAULT 0,
    updated_at    INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY(record_type, record_id)
);

CREATE INDEX IF NOT EXISTS idx_world_dynamic_records_scope
    ON world_dynamic_records(record_scope);

CREATE TABLE IF NOT EXISTS world_dynamic_record_catalog (
    record_type    TEXT    NOT NULL,
    record_id      TEXT    NOT NULL,
    record_scope   TEXT    NOT NULL DEFAULT 'permanent',
    is_persistent  INTEGER NOT NULL DEFAULT 1,
    definition_fingerprint TEXT NOT NULL DEFAULT '',
    creator_account_id INTEGER NOT NULL DEFAULT 0,
    creator_character_id INTEGER NOT NULL DEFAULT 0,
    creation_source TEXT NOT NULL DEFAULT '',
    schema_version INTEGER NOT NULL DEFAULT 0,
    validation_version INTEGER NOT NULL DEFAULT 0,
    created_at     INTEGER NOT NULL DEFAULT 0,
    updated_at     INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY(record_type, record_id)
);

CREATE INDEX IF NOT EXISTS idx_world_dynamic_record_catalog_persistent
    ON world_dynamic_record_catalog(is_persistent);

CREATE TABLE IF NOT EXISTS world_dynamic_record_links (
    record_id    TEXT    NOT NULL,
    link_kind    TEXT    NOT NULL,
    owner_a      TEXT    NOT NULL DEFAULT '',
    owner_b      TEXT    NOT NULL DEFAULT '',
    owner_c      TEXT    NOT NULL DEFAULT '',
    owner_index  INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY(record_id, link_kind, owner_a, owner_b, owner_c, owner_index)
);

CREATE INDEX IF NOT EXISTS idx_world_dynamic_record_links_record
    ON world_dynamic_record_links(record_id);

CREATE TABLE IF NOT EXISTS craft_requests (
    account_id      INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    character_id    INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    request_id      TEXT    NOT NULL,
    request_hash    TEXT    NOT NULL,
    status          TEXT    NOT NULL DEFAULT 'pending',
    result_payload  BLOB    NOT NULL DEFAULT '',
    created_at      INTEGER NOT NULL DEFAULT 0,
    updated_at      INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY(account_id, character_id, request_id)
);

CREATE INDEX IF NOT EXISTS idx_craft_requests_updated
    ON craft_requests(updated_at);

CREATE TABLE IF NOT EXISTS server_record_requests (
    source          TEXT    NOT NULL,
    request_id      TEXT    NOT NULL,
    request_hash    TEXT    NOT NULL,
    status          TEXT    NOT NULL DEFAULT 'pending',
    result_payload  BLOB    NOT NULL DEFAULT '',
    created_at      INTEGER NOT NULL DEFAULT 0,
    updated_at      INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY(source, request_id)
);

CREATE INDEX IF NOT EXISTS idx_server_record_requests_updated
    ON server_record_requests(updated_at);

CREATE TABLE IF NOT EXISTS world_dynamic_record_legacy_backup (
    record_type    TEXT    NOT NULL,
    record_id      TEXT    NOT NULL,
    record_scope   TEXT    NOT NULL,
    record_data    BLOB    NOT NULL,
    schema_version INTEGER NOT NULL DEFAULT 0,
    backed_up_at   INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY(record_type, record_id)
);

CREATE TABLE IF NOT EXISTS world_dynamic_record_migration_failures (
    record_type    TEXT    NOT NULL,
    record_id      TEXT    NOT NULL,
    reason         TEXT    NOT NULL,
    attempts       INTEGER NOT NULL DEFAULT 1,
    last_attempt_at INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY(record_type, record_id)
);

CREATE TABLE IF NOT EXISTS character_inventory (
    character_id          INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    item_index            INTEGER NOT NULL,
    ref_id                TEXT    NOT NULL,
    item_count            INTEGER NOT NULL DEFAULT 0,
    charge                INTEGER NOT NULL DEFAULT -1,
    enchantment_charge    REAL    NOT NULL DEFAULT -1,
    soul                  TEXT    NOT NULL DEFAULT '',
    instance_id           INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY(character_id, item_index)
);

CREATE TABLE IF NOT EXISTS character_equipment (
    character_id          INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    slot                  INTEGER NOT NULL,
    ref_id                TEXT    NOT NULL,
    item_count            INTEGER NOT NULL DEFAULT 0,
    charge                INTEGER NOT NULL DEFAULT -1,
    enchantment_charge    REAL    NOT NULL DEFAULT -1,
    soul                  TEXT    NOT NULL DEFAULT '',
    instance_id           INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY(character_id, slot)
);

-- Learned spellbook: canonical set of learned spell IDs (ESM::Spell records
-- with type ST_Spell). Baseline content spells (race/birthsign powers,
-- abilities, diseases) are never stored here.
CREATE TABLE IF NOT EXISTS character_spellbook (
    character_id          INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    spell_id              TEXT    NOT NULL,
    PRIMARY KEY(character_id, spell_id)
);

CREATE INDEX IF NOT EXISTS idx_character_spellbook_character
    ON character_spellbook(character_id);

CREATE TABLE IF NOT EXISTS character_dynamic_stats (
    character_id          INTEGER PRIMARY KEY REFERENCES characters(id) ON DELETE CASCADE,
    health_base           REAL    NOT NULL DEFAULT 0,
    health_current        REAL    NOT NULL DEFAULT 0,
    health_mod            REAL    NOT NULL DEFAULT 0,
    magicka_base          REAL    NOT NULL DEFAULT 0,
    magicka_current       REAL    NOT NULL DEFAULT 0,
    magicka_mod           REAL    NOT NULL DEFAULT 0,
    fatigue_base          REAL    NOT NULL DEFAULT 0,
    fatigue_current       REAL    NOT NULL DEFAULT 0,
    fatigue_mod           REAL    NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS character_attributes (
    character_id          INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    attribute_index       INTEGER NOT NULL,
    base                  INTEGER NOT NULL DEFAULT 0,
    mod                   REAL    NOT NULL DEFAULT 0,
    damage                REAL    NOT NULL DEFAULT 0,
    PRIMARY KEY(character_id, attribute_index)
);

CREATE TABLE IF NOT EXISTS character_skills (
    character_id          INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    skill_index           INTEGER NOT NULL,
    base                  REAL    NOT NULL DEFAULT 0,
    mod                   REAL    NOT NULL DEFAULT 0,
    damage                REAL    NOT NULL DEFAULT 0,
    progress              REAL    NOT NULL DEFAULT 0,
    increases             INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY(character_id, skill_index)
);

CREATE TABLE IF NOT EXISTS character_journal_entries (
    id                    INTEGER PRIMARY KEY AUTOINCREMENT,
    character_id          INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    quest_id              TEXT    NOT NULL,
    info_id               TEXT    NOT NULL,
    journal_index         INTEGER NOT NULL DEFAULT 0,
    entry_text            TEXT    NOT NULL DEFAULT '',
    actor_name            TEXT    NOT NULL DEFAULT '',
    has_timestamp         INTEGER NOT NULL DEFAULT 0,
    days_passed           INTEGER NOT NULL DEFAULT 0,
    month                 INTEGER NOT NULL DEFAULT 0,
    day_of_month          INTEGER NOT NULL DEFAULT 0,
    changed_at            INTEGER NOT NULL DEFAULT 0,
    UNIQUE(character_id, quest_id, info_id)
);

CREATE INDEX IF NOT EXISTS idx_character_journal_entries_character
    ON character_journal_entries(character_id, id);

CREATE TABLE IF NOT EXISTS character_journal_quests (
    character_id          INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    quest_id              TEXT    NOT NULL,
    journal_index         INTEGER NOT NULL DEFAULT 0,
    changed_at            INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY(character_id, quest_id)
);

CREATE TABLE IF NOT EXISTS character_marks (
    character_id          INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    mark_name             TEXT    NOT NULL,
    cell                  TEXT    NOT NULL DEFAULT '',
    pos_x                 REAL    NOT NULL DEFAULT 0,
    pos_y                 REAL    NOT NULL DEFAULT 0,
    pos_z                 REAL    NOT NULL DEFAULT 0,
    rot_x                 REAL    NOT NULL DEFAULT 0,
    rot_y                 REAL    NOT NULL DEFAULT 0,
    rot_z                 REAL    NOT NULL DEFAULT 0,
    PRIMARY KEY(character_id, mark_name)
);

CREATE INDEX IF NOT EXISTS idx_character_marks_character
    ON character_marks(character_id);

CREATE TABLE IF NOT EXISTS character_lua_storage (
    character_id        INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    storage_namespace  TEXT    NOT NULL,
    storage_key        TEXT    NOT NULL,
    value              BLOB    NOT NULL,
    updated_at         INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY(character_id, storage_namespace, storage_key)
);

CREATE INDEX IF NOT EXISTS idx_character_lua_storage_namespace
    ON character_lua_storage(storage_namespace, storage_key);
)SQL";

static const char* kWorldItemTakeSchema = R"SQL(
CREATE TABLE IF NOT EXISTS world_taken_references (
    object_kind INTEGER NOT NULL, cell_id TEXT NOT NULL, ref_id TEXT NOT NULL,
    ref_index INTEGER NOT NULL DEFAULT 0, ref_content_file INTEGER NOT NULL DEFAULT -1,
    mp_num INTEGER NOT NULL DEFAULT 0,
    taken_by_character INTEGER NOT NULL,
    take_request_id TEXT NOT NULL, taken_at INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY(object_kind, cell_id, ref_id, ref_index, ref_content_file, mp_num)
);
CREATE INDEX IF NOT EXISTS idx_world_taken_references_cell ON world_taken_references(cell_id);
CREATE TABLE IF NOT EXISTS world_item_take_requests (
    account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    character_id INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    request_id TEXT NOT NULL, request_hash TEXT NOT NULL, object_kind INTEGER NOT NULL,
    cell_id TEXT NOT NULL, ref_id TEXT NOT NULL, ref_index INTEGER NOT NULL DEFAULT 0,
    ref_content_file INTEGER NOT NULL DEFAULT -1, mp_num INTEGER NOT NULL DEFAULT 0,
    item_ref_id TEXT NOT NULL, item_count INTEGER NOT NULL, crime_value INTEGER NOT NULL DEFAULT 0,
    theft INTEGER NOT NULL DEFAULT 0, inventory_revision INTEGER NOT NULL,
    created_at INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY(account_id, character_id, request_id)
);
)SQL";

static const char* kInventoryTakeSchema = R"SQL(
CREATE TABLE IF NOT EXISTS inventory_take_requests (
    account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    character_id INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    request_id TEXT NOT NULL,
    request_hash TEXT NOT NULL,
    take_kind INTEGER NOT NULL,
    cell_id TEXT NOT NULL,
    source_ref_id TEXT NOT NULL,
    source_ref_num INTEGER NOT NULL,
    source_mp_num INTEGER NOT NULL,
    source_actor_id INTEGER NOT NULL,
    source_migration_generation INTEGER NOT NULL,
    item_ref_id TEXT NOT NULL,
    item_charge INTEGER NOT NULL,
    item_count INTEGER NOT NULL,
    inventory_revision INTEGER NOT NULL,
    detected INTEGER NOT NULL,
    detection_roll INTEGER NOT NULL,
    theft INTEGER NOT NULL,
    crime_value INTEGER NOT NULL,
    created_at INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY(account_id, character_id, request_id)
);
)SQL";

    static const char* kCombatEventSchema = R"SQL(
CREATE TABLE IF NOT EXISTS combat_events (
    event_id INTEGER PRIMARY KEY AUTOINCREMENT,
    account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    character_id INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    attacker_guid INTEGER NOT NULL,
    victim_actor_id INTEGER NOT NULL,
    victim_ref_id TEXT NOT NULL,
    cell_id TEXT NOT NULL,
    migration_generation INTEGER NOT NULL,
    authority_generation INTEGER NOT NULL,
    actor_authority_guid INTEGER NOT NULL,
    proposed_damage REAL NOT NULL,
    proposed_health_damage INTEGER NOT NULL,
    proposal_hash TEXT NOT NULL,
    created_at_ms INTEGER NOT NULL,
    status INTEGER NOT NULL DEFAULT 0,
    result_sequence INTEGER NOT NULL DEFAULT 0,
    result_flags INTEGER NOT NULL DEFAULT 0,
    applied_damage REAL NOT NULL DEFAULT 0,
    qualifying_crime INTEGER NOT NULL DEFAULT 0,
    assault_reported INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_combat_events_victim
    ON combat_events(character_id, victim_actor_id, migration_generation, status);
)SQL";

    static const char* kWerewolfStateSchema = R"SQL(
CREATE TABLE IF NOT EXISTS character_werewolf_state (
    character_id INTEGER PRIMARY KEY REFERENCES characters(id) ON DELETE CASCADE,
    is_werewolf INTEGER NOT NULL DEFAULT 0,
    transition_counter INTEGER NOT NULL DEFAULT 0,
    updated_at INTEGER NOT NULL DEFAULT 0
);
)SQL";

    // Migration: add chargen columns to databases created before they existed.
    static const char* kMigrations[] = {
        "ALTER TABLE characters ADD COLUMN race       TEXT NOT NULL DEFAULT ''",
        "ALTER TABLE characters ADD COLUMN head_mesh  TEXT NOT NULL DEFAULT ''",
        "ALTER TABLE characters ADD COLUMN hair_mesh  TEXT NOT NULL DEFAULT ''",
        "ALTER TABLE characters ADD COLUMN is_male    INTEGER NOT NULL DEFAULT 1",
        "ALTER TABLE characters ADD COLUMN class_id   TEXT NOT NULL DEFAULT ''",
        "ALTER TABLE characters ADD COLUMN class_name TEXT NOT NULL DEFAULT ''",
        "ALTER TABLE characters ADD COLUMN birth_sign TEXT NOT NULL DEFAULT ''",
        "ALTER TABLE characters ADD COLUMN class_data TEXT NOT NULL DEFAULT ''",
        // accounts table migrations
        "ALTER TABLE accounts ADD COLUMN password_hash TEXT NOT NULL DEFAULT ''",
        // unique character name per account (safe on existing single-char DBs)
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_chars_unique_name ON characters(account_id, name)",
        // Ed25519 keypairs table
        "CREATE TABLE IF NOT EXISTS account_keypairs ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,"
        "  public_key TEXT NOT NULL UNIQUE,"
        "  label TEXT NOT NULL DEFAULT '',"
        "  created_at INTEGER NOT NULL DEFAULT 0)",
        "CREATE INDEX IF NOT EXISTS idx_keypairs_account ON account_keypairs(account_id)",
        "ALTER TABLE characters ADD COLUMN nickname TEXT NOT NULL DEFAULT ''",
        "ALTER TABLE characters ADD COLUMN inventory_saved INTEGER NOT NULL DEFAULT 0",
        "ALTER TABLE characters ADD COLUMN equipment_saved INTEGER NOT NULL DEFAULT 0",
        "ALTER TABLE characters ADD COLUMN stats_saved INTEGER NOT NULL DEFAULT 0",
        "ALTER TABLE characters ADD COLUMN level INTEGER NOT NULL DEFAULT 1",
        "ALTER TABLE characters ADD COLUMN level_progress REAL NOT NULL DEFAULT 0",
        "ALTER TABLE character_inventory ADD COLUMN instance_id INTEGER NOT NULL DEFAULT 0",
        "ALTER TABLE character_equipment ADD COLUMN instance_id INTEGER NOT NULL DEFAULT 0",
        "CREATE TABLE IF NOT EXISTS character_inventory ("
        "  character_id INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,"
        "  item_index INTEGER NOT NULL,"
        "  ref_id TEXT NOT NULL,"
        "  item_count INTEGER NOT NULL DEFAULT 0,"
        "  charge INTEGER NOT NULL DEFAULT -1,"
        "  enchantment_charge REAL NOT NULL DEFAULT -1,"
        "  soul TEXT NOT NULL DEFAULT '',"
        "  PRIMARY KEY(character_id, item_index))",
        "CREATE TABLE IF NOT EXISTS character_equipment ("
        "  character_id INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,"
        "  slot INTEGER NOT NULL,"
        "  ref_id TEXT NOT NULL,"
        "  item_count INTEGER NOT NULL DEFAULT 0,"
        "  charge INTEGER NOT NULL DEFAULT -1,"
        "  enchantment_charge REAL NOT NULL DEFAULT -1,"
        "  soul TEXT NOT NULL DEFAULT '',"
        "  PRIMARY KEY(character_id, slot))",
        "CREATE TABLE IF NOT EXISTS character_dynamic_stats ("
        "  character_id INTEGER PRIMARY KEY REFERENCES characters(id) ON DELETE CASCADE,"
        "  health_base REAL NOT NULL DEFAULT 0,"
        "  health_current REAL NOT NULL DEFAULT 0,"
        "  health_mod REAL NOT NULL DEFAULT 0,"
        "  magicka_base REAL NOT NULL DEFAULT 0,"
        "  magicka_current REAL NOT NULL DEFAULT 0,"
        "  magicka_mod REAL NOT NULL DEFAULT 0,"
        "  fatigue_base REAL NOT NULL DEFAULT 0,"
        "  fatigue_current REAL NOT NULL DEFAULT 0,"
        "  fatigue_mod REAL NOT NULL DEFAULT 0)",
        "CREATE TABLE IF NOT EXISTS character_attributes ("
        "  character_id INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,"
        "  attribute_index INTEGER NOT NULL,"
        "  base INTEGER NOT NULL DEFAULT 0,"
        "  mod REAL NOT NULL DEFAULT 0,"
        "  damage REAL NOT NULL DEFAULT 0,"
        "  PRIMARY KEY(character_id, attribute_index))",
        "CREATE TABLE IF NOT EXISTS character_skills ("
        "  character_id INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,"
        "  skill_index INTEGER NOT NULL,"
        "  base REAL NOT NULL DEFAULT 0,"
        "  mod REAL NOT NULL DEFAULT 0,"
        "  damage REAL NOT NULL DEFAULT 0,"
        "  progress REAL NOT NULL DEFAULT 0,"
        "  increases INTEGER NOT NULL DEFAULT 0,"
        "  PRIMARY KEY(character_id, skill_index))",
        "CREATE TABLE IF NOT EXISTS character_marks ("
        "  character_id INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,"
        "  mark_name TEXT NOT NULL,"
        "  cell TEXT NOT NULL DEFAULT '',"
        "  pos_x REAL NOT NULL DEFAULT 0,"
        "  pos_y REAL NOT NULL DEFAULT 0,"
        "  pos_z REAL NOT NULL DEFAULT 0,"
        "  rot_x REAL NOT NULL DEFAULT 0,"
        "  rot_y REAL NOT NULL DEFAULT 0,"
        "  rot_z REAL NOT NULL DEFAULT 0,"
        "  PRIMARY KEY(character_id, mark_name))",
        "CREATE INDEX IF NOT EXISTS idx_character_marks_character ON character_marks(character_id)",
        "CREATE TABLE IF NOT EXISTS character_lua_storage ("
        "  character_id INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,"
        "  storage_namespace TEXT NOT NULL,"
        "  storage_key TEXT NOT NULL,"
        "  value BLOB NOT NULL,"
        "  updated_at INTEGER NOT NULL DEFAULT 0,"
        "  PRIMARY KEY(character_id, storage_namespace, storage_key))",
        "CREATE INDEX IF NOT EXISTS idx_character_lua_storage_namespace"
        " ON character_lua_storage(storage_namespace, storage_key)",
        "CREATE TABLE IF NOT EXISTS world_dynamic_records ("
        "  record_type TEXT NOT NULL,"
        "  record_id TEXT NOT NULL,"
        "  record_scope TEXT NOT NULL DEFAULT 'permanent',"
        "  record_data BLOB NOT NULL,"
        "  created_at INTEGER NOT NULL DEFAULT 0,"
        "  updated_at INTEGER NOT NULL DEFAULT 0,"
        "  PRIMARY KEY(record_type, record_id))",
        "CREATE INDEX IF NOT EXISTS idx_world_dynamic_records_scope ON world_dynamic_records(record_scope)",
        "CREATE TABLE IF NOT EXISTS world_dynamic_record_catalog ("
        "  record_type TEXT NOT NULL,"
        "  record_id TEXT NOT NULL,"
        "  record_scope TEXT NOT NULL DEFAULT 'permanent',"
        "  is_persistent INTEGER NOT NULL DEFAULT 1,"
        "  created_at INTEGER NOT NULL DEFAULT 0,"
        "  updated_at INTEGER NOT NULL DEFAULT 0,"
        "  PRIMARY KEY(record_type, record_id))",
        "CREATE INDEX IF NOT EXISTS idx_world_dynamic_record_catalog_persistent"
        " ON world_dynamic_record_catalog(is_persistent)",
        "CREATE TABLE IF NOT EXISTS world_dynamic_record_links ("
        "  record_id TEXT NOT NULL,"
        "  link_kind TEXT NOT NULL,"
        "  owner_a TEXT NOT NULL DEFAULT '',"
        "  owner_b TEXT NOT NULL DEFAULT '',"
        "  owner_c TEXT NOT NULL DEFAULT '',"
        "  owner_index INTEGER NOT NULL DEFAULT 0,"
        "  PRIMARY KEY(record_id, link_kind, owner_a, owner_b, owner_c, owner_index))",
        "CREATE INDEX IF NOT EXISTS idx_world_dynamic_record_links_record"
        " ON world_dynamic_record_links(record_id)",
        "ALTER TABLE characters ADD COLUMN inventory_revision INTEGER NOT NULL DEFAULT 0",
        "ALTER TABLE characters ADD COLUMN spellbook_saved INTEGER NOT NULL DEFAULT 0",
        "ALTER TABLE characters ADD COLUMN spellbook_revision INTEGER NOT NULL DEFAULT 0",
        "CREATE TABLE IF NOT EXISTS character_spellbook ("
        "  character_id INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,"
        "  spell_id TEXT NOT NULL,"
        "  PRIMARY KEY(character_id, spell_id))",
        "CREATE INDEX IF NOT EXISTS idx_character_spellbook_character ON character_spellbook(character_id)",
        "ALTER TABLE world_dynamic_records ADD COLUMN schema_version INTEGER NOT NULL DEFAULT 0",
        "ALTER TABLE world_dynamic_record_catalog ADD COLUMN definition_fingerprint TEXT NOT NULL DEFAULT ''",
        "ALTER TABLE world_dynamic_record_catalog ADD COLUMN creator_account_id INTEGER NOT NULL DEFAULT 0",
        "ALTER TABLE world_dynamic_record_catalog ADD COLUMN creator_character_id INTEGER NOT NULL DEFAULT 0",
        "ALTER TABLE world_dynamic_record_catalog ADD COLUMN creation_source TEXT NOT NULL DEFAULT ''",
        "ALTER TABLE world_dynamic_record_catalog ADD COLUMN schema_version INTEGER NOT NULL DEFAULT 0",
        "ALTER TABLE world_dynamic_record_catalog ADD COLUMN validation_version INTEGER NOT NULL DEFAULT 0",
        "ALTER TABLE world_doors ADD COLUMN revision INTEGER NOT NULL DEFAULT 1",
        "CREATE TABLE IF NOT EXISTS craft_requests ("
        "  account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,"
        "  character_id INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,"
        "  request_id TEXT NOT NULL,"
        "  request_hash TEXT NOT NULL,"
        "  status TEXT NOT NULL DEFAULT 'pending',"
        "  result_payload BLOB NOT NULL DEFAULT '',"
        "  created_at INTEGER NOT NULL DEFAULT 0,"
        "  updated_at INTEGER NOT NULL DEFAULT 0,"
        "  PRIMARY KEY(account_id, character_id, request_id))",
        "CREATE INDEX IF NOT EXISTS idx_craft_requests_updated ON craft_requests(updated_at)",
        "CREATE TABLE IF NOT EXISTS semantic_requests ("
        "  service TEXT NOT NULL,"
        "  account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,"
        "  character_id INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,"
        "  request_id TEXT NOT NULL, request_hash TEXT NOT NULL, status TEXT NOT NULL,"
        "  error_code INTEGER NOT NULL DEFAULT 0, result_payload BLOB NOT NULL,"
        "  source TEXT NOT NULL DEFAULT '', created_at INTEGER NOT NULL DEFAULT 0,"
        "  updated_at INTEGER NOT NULL DEFAULT 0,"
        "  PRIMARY KEY(service, account_id, character_id, request_id))",
        "CREATE INDEX IF NOT EXISTS idx_semantic_requests_updated"
        " ON semantic_requests(service, updated_at)",
        "CREATE TABLE IF NOT EXISTS character_crime_state ("
        "  character_id INTEGER PRIMARY KEY REFERENCES characters(id) ON DELETE CASCADE,"
        "  bounty INTEGER NOT NULL DEFAULT 0 CHECK(bounty >= 0 AND bounty <= 2147483647),"
        "  current_crime_id INTEGER NOT NULL DEFAULT -1"
        "    CHECK(current_crime_id >= -1 AND current_crime_id <= 2147483647),"
        "  paid_crime_id INTEGER NOT NULL DEFAULT -1"
        "    CHECK(paid_crime_id >= -1 AND paid_crime_id <= current_crime_id),"
        "  revision INTEGER NOT NULL DEFAULT 0 CHECK(revision >= 0),"
        "  updated_at INTEGER NOT NULL DEFAULT 0)",
        "CREATE TABLE IF NOT EXISTS character_topic_state ("
        "  character_id INTEGER PRIMARY KEY REFERENCES characters(id) ON DELETE CASCADE,"
        "  revision INTEGER NOT NULL DEFAULT 0 CHECK(revision >= 0),"
        "  updated_at INTEGER NOT NULL DEFAULT 0)",
        "CREATE TABLE IF NOT EXISTS character_known_topics ("
        "  character_id INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,"
        "  topic_id TEXT NOT NULL, PRIMARY KEY(character_id, topic_id))",
        "CREATE INDEX IF NOT EXISTS idx_character_known_topics_character"
        " ON character_known_topics(character_id)",
        "CREATE TABLE IF NOT EXISTS character_faction_state ("
        "  character_id INTEGER PRIMARY KEY REFERENCES characters(id) ON DELETE CASCADE,"
        "  revision INTEGER NOT NULL DEFAULT 0 CHECK(revision >= 0),"
        "  updated_at INTEGER NOT NULL DEFAULT 0)",
        "CREATE TABLE IF NOT EXISTS character_factions ("
        "  character_id INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,"
        "  faction_id TEXT NOT NULL, rank INTEGER NOT NULL DEFAULT -1,"
        "  reputation INTEGER NOT NULL DEFAULT 0, expelled INTEGER NOT NULL DEFAULT 0 CHECK(expelled IN (0,1)),"
        "  PRIMARY KEY(character_id, faction_id))",
        "CREATE INDEX IF NOT EXISTS idx_character_factions_character"
        " ON character_factions(character_id)",
        "CREATE TABLE IF NOT EXISTS server_record_requests ("
        "  source TEXT NOT NULL,"
        "  request_id TEXT NOT NULL,"
        "  request_hash TEXT NOT NULL,"
        "  status TEXT NOT NULL DEFAULT 'pending',"
        "  result_payload BLOB NOT NULL DEFAULT '',"
        "  created_at INTEGER NOT NULL DEFAULT 0,"
        "  updated_at INTEGER NOT NULL DEFAULT 0,"
        "  PRIMARY KEY(source, request_id))",
        "CREATE INDEX IF NOT EXISTS idx_server_record_requests_updated"
        " ON server_record_requests(updated_at)",
        "CREATE TABLE IF NOT EXISTS world_dynamic_record_legacy_backup ("
        "  record_type TEXT NOT NULL, record_id TEXT NOT NULL, record_scope TEXT NOT NULL,"
        "  record_data BLOB NOT NULL, schema_version INTEGER NOT NULL DEFAULT 0,"
        "  backed_up_at INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(record_type, record_id))",
        "CREATE TABLE IF NOT EXISTS world_dynamic_record_migration_failures ("
        "  record_type TEXT NOT NULL, record_id TEXT NOT NULL, reason TEXT NOT NULL,"
        "  attempts INTEGER NOT NULL DEFAULT 1, last_attempt_at INTEGER NOT NULL DEFAULT 0,"
        "  PRIMARY KEY(record_type, record_id))",
        "CREATE TABLE IF NOT EXISTS world_metadata ("
        "  key TEXT PRIMARY KEY,"
        "  value INTEGER NOT NULL DEFAULT 0)",
        "CREATE TABLE IF NOT EXISTS world_spawned_actors ("
        "  mp_num INTEGER PRIMARY KEY,"
        "  cell_id TEXT NOT NULL,"
        "  ref_id TEXT NOT NULL,"
        "  ref_num INTEGER NOT NULL DEFAULT 0,"
        "  persistent INTEGER NOT NULL DEFAULT 1,"
        "  pos_x REAL NOT NULL DEFAULT 0,"
        "  pos_y REAL NOT NULL DEFAULT 0,"
        "  pos_z REAL NOT NULL DEFAULT 0,"
        "  rot_x REAL NOT NULL DEFAULT 0,"
        "  rot_y REAL NOT NULL DEFAULT 0,"
        "  rot_z REAL NOT NULL DEFAULT 0,"
        "  health_base REAL NOT NULL DEFAULT 0,"
        "  health_current REAL NOT NULL DEFAULT 0,"
        "  health_mod REAL NOT NULL DEFAULT 0,"
        "  magicka_base REAL NOT NULL DEFAULT 0,"
        "  magicka_current REAL NOT NULL DEFAULT 0,"
        "  magicka_mod REAL NOT NULL DEFAULT 0,"
        "  fatigue_base REAL NOT NULL DEFAULT 0,"
        "  fatigue_current REAL NOT NULL DEFAULT 0,"
        "  fatigue_mod REAL NOT NULL DEFAULT 0,"
        "  is_dead INTEGER NOT NULL DEFAULT 0,"
        "  death_state INTEGER NOT NULL DEFAULT 0,"
        "  death_anim_group TEXT NOT NULL DEFAULT '',"
        "  created_at INTEGER NOT NULL DEFAULT 0,"
        "  updated_at INTEGER NOT NULL DEFAULT 0)",
        "CREATE INDEX IF NOT EXISTS idx_world_spawned_actors_cell ON world_spawned_actors(cell_id)",
        "CREATE TABLE IF NOT EXISTS world_dead_vanilla_actors ("
        "  ref_id TEXT NOT NULL,"
        "  ref_num INTEGER NOT NULL DEFAULT 0,"
        "  cell_id TEXT NOT NULL,"
        "  pos_x REAL NOT NULL DEFAULT 0,"
        "  pos_y REAL NOT NULL DEFAULT 0,"
        "  pos_z REAL NOT NULL DEFAULT 0,"
        "  rot_x REAL NOT NULL DEFAULT 0,"
        "  rot_y REAL NOT NULL DEFAULT 0,"
        "  rot_z REAL NOT NULL DEFAULT 0,"
        "  health_base REAL NOT NULL DEFAULT 0,"
        "  health_current REAL NOT NULL DEFAULT 0,"
        "  health_mod REAL NOT NULL DEFAULT 0,"
        "  magicka_base REAL NOT NULL DEFAULT 0,"
        "  magicka_current REAL NOT NULL DEFAULT 0,"
        "  magicka_mod REAL NOT NULL DEFAULT 0,"
        "  fatigue_base REAL NOT NULL DEFAULT 0,"
        "  fatigue_current REAL NOT NULL DEFAULT 0,"
        "  fatigue_mod REAL NOT NULL DEFAULT 0,"
        "  is_dead INTEGER NOT NULL DEFAULT 1,"
        "  death_state INTEGER NOT NULL DEFAULT 0,"
        "  is_instant_death INTEGER NOT NULL DEFAULT 1,"
        "  death_anim_group TEXT NOT NULL DEFAULT '',"
        "  created_at INTEGER NOT NULL DEFAULT 0,"
        "  updated_at INTEGER NOT NULL DEFAULT 0,"
        "  PRIMARY KEY(ref_id, ref_num))",
        "CREATE INDEX IF NOT EXISTS idx_world_dead_vanilla_actors_cell"
        " ON world_dead_vanilla_actors(cell_id)",
    };

    // ============================================================================
    //  Helpers
    // ============================================================================

    namespace
    {
        struct BrowsableTableDef
        {
            const char* name;
            const char* orderBy;
        };

        static const BrowsableTableDef kBrowsableTableDefs[] = {
            { "accounts", "id" },
            { "characters", "id" },
            { "account_keypairs", "id" },
            { "world_objects", "mp_num" },
            { "world_spawned_actors", "cell_id, mp_num" },
            { "world_dead_vanilla_actors", "cell_id, ref_id, ref_num" },
            { "world_containers", "cell_id, ref_id, ref_num" },
            { "world_container_items", "cell_id, ref_id, ref_num, item_index" },
            { "world_doors", "cell_id, ref_id, ref_num" },
            { "world_metadata", "key" },
            { "world_dynamic_records", "created_at, record_type, record_id" },
            { "world_dynamic_record_catalog", "created_at, record_type, record_id" },
            { "world_dynamic_record_links", "record_id, link_kind, owner_a, owner_b, owner_c, owner_index" },
            { "craft_requests", "updated_at, account_id, character_id, request_id" },
            { "semantic_requests", "service, updated_at, account_id, character_id, request_id" },
            { "server_record_requests", "updated_at, source, request_id" },
            { "world_dynamic_record_legacy_backup", "backed_up_at, record_type, record_id" },
            { "world_dynamic_record_migration_failures", "last_attempt_at, record_type, record_id" },
            { "character_inventory", "character_id, item_index" },
            { "character_equipment", "character_id, slot" },
            { "character_dynamic_stats", "character_id" },
            { "character_attributes", "character_id, attribute_index" },
            { "character_skills", "character_id, skill_index" },
            { "character_journal_entries", "character_id, id" },
            { "character_journal_quests", "character_id, quest_id" },
            { "character_marks", "character_id, mark_name" },
            { "character_lua_storage", "character_id, storage_namespace, storage_key" },
            { "character_crime_state", "character_id" },
            { "character_topic_state", "character_id" },
            { "character_known_topics", "character_id, topic_id" },
            { "character_faction_state", "character_id" },
            { "character_factions", "character_id, faction_id" },
        };

        void checkSqlite(int rc, sqlite3* db, const char* op)
        {
            if (rc != SQLITE_OK && rc != SQLITE_ROW && rc != SQLITE_DONE)
                throw std::runtime_error(std::string("[PlayerDB] ") + op + ": " + sqlite3_errmsg(db));
        }

        void clearDynamicRecordLinksForOwner(sqlite3* db, sqlite3_stmt* stmt, std::string_view linkKind,
            std::string_view ownerA, std::string_view ownerB, std::string_view ownerC)
        {
            sqlite3_bind_text(stmt, 1, linkKind.data(), static_cast<int>(linkKind.size()), SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, ownerA.data(), static_cast<int>(ownerA.size()), SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, ownerB.data(), static_cast<int>(ownerB.size()), SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 4, ownerC.data(), static_cast<int>(ownerC.size()), SQLITE_TRANSIENT);
            checkSqlite(sqlite3_step(stmt), db, "clearDynamicRecordLinksForOwner");
            sqlite3_reset(stmt);
            sqlite3_clear_bindings(stmt);
        }

        void insertDynamicRecordLink(sqlite3* db, sqlite3_stmt* stmt, std::string_view recordId,
            std::string_view linkKind, std::string_view ownerA, std::string_view ownerB, std::string_view ownerC,
            int64_t ownerIndex)
        {
            if (recordId.empty())
                return;

            sqlite3_bind_text(stmt, 1, recordId.data(), static_cast<int>(recordId.size()), SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, linkKind.data(), static_cast<int>(linkKind.size()), SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, ownerA.data(), static_cast<int>(ownerA.size()), SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 4, ownerB.data(), static_cast<int>(ownerB.size()), SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 5, ownerC.data(), static_cast<int>(ownerC.size()), SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 6, ownerIndex);
            checkSqlite(sqlite3_step(stmt), db, "insertDynamicRecordLink");
            sqlite3_reset(stmt);
            sqlite3_clear_bindings(stmt);
        }

        const BrowsableTableDef* findBrowsableTableDef(std::string_view tableName)
        {
            for (const BrowsableTableDef& entry : kBrowsableTableDefs)
            {
                if (tableName == entry.name)
                    return &entry;
            }
            return nullptr;
        }

        std::optional<std::string> sqliteColumnToString(sqlite3_stmt* stmt, int index)
        {
            if (sqlite3_column_type(stmt, index) == SQLITE_NULL)
                return std::nullopt;

            const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, index));
            if (!text)
                return std::string();

            return std::string(text, static_cast<std::size_t>(sqlite3_column_bytes(stmt, index)));
        }
    }

    // ============================================================================
    //  Constructor / Destructor
    // ============================================================================

    PlayerDatabase::PlayerDatabase(std::string_view path)
    {
        const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
        if (const int rc = sqlite3_open_v2(std::string(path).c_str(), &mDb, flags, nullptr); rc != SQLITE_OK)
        {
            const std::string msg = mDb ? sqlite3_errmsg(mDb) : "unknown error";
            sqlite3_close(mDb);
            mDb = nullptr;
            throw std::runtime_error("[PlayerDB] open '" + std::string(path) + "': " + msg);
        }

        exec(kSchema);
        exec(kWorldItemTakeSchema);
        exec(kInventoryTakeSchema);
        exec(kCombatEventSchema);
        exec(kWerewolfStateSchema);

        // Run migrations — ALTER TABLE errors on "duplicate column name" for columns
        // that already exist; we ignore those errors so this is idempotent.
        for (const char* sql : kMigrations)
        {
            char* errmsg = nullptr;
            sqlite3_exec(mDb, sql, nullptr, nullptr, &errmsg);
            if (errmsg)
                sqlite3_free(errmsg); // ignore — column may already exist
        }

        Log(Debug::Info) << "[PlayerDB] opened: " << path;
    }

    PlayerDatabase::~PlayerDatabase()
    {
        if (mDb)
        {
            sqlite3_close_v2(mDb);
            mDb = nullptr;
        }
    }

    // ============================================================================
    //  Private helpers
    // ============================================================================

    void PlayerDatabase::exec(const char* sql)
    {
        char* errmsg = nullptr;
        const int rc = sqlite3_exec(mDb, sql, nullptr, nullptr, &errmsg);
        if (rc != SQLITE_OK)
        {
            std::string msg = errmsg ? errmsg : "unknown";
            sqlite3_free(errmsg);
            throw std::runtime_error(std::string("[PlayerDB] exec: ") + msg);
        }
    }

    sqlite3_stmt* PlayerDatabase::prepare(const char* sql)
    {
        sqlite3_stmt* stmt = nullptr;
        checkSqlite(sqlite3_prepare_v2(mDb, sql, -1, &stmt, nullptr), mDb, "prepare");
        return stmt;
    }

    // ============================================================================
    //  Public API
    // ============================================================================

    int64_t PlayerDatabase::lookupAccount(std::string_view username)
    {
        sqlite3_stmt* s = prepare("SELECT id FROM accounts WHERE username = ?1");
        sqlite3_bind_text(s, 1, username.data(), static_cast<int>(username.size()), SQLITE_STATIC);
        const int rc = sqlite3_step(s);
        const int64_t id = (rc == SQLITE_ROW) ? sqlite3_column_int64(s, 0) : -1;
        sqlite3_finalize(s);
        return id;
    }

    int64_t PlayerDatabase::createAccount(std::string_view username)
    {
        sqlite3_stmt* s = prepare("INSERT INTO accounts(username, created_at) VALUES(?1, ?2)");
        sqlite3_bind_text(s, 1, username.data(), static_cast<int>(username.size()), SQLITE_STATIC);
        sqlite3_bind_int64(s, 2, static_cast<int64_t>(std::time(nullptr)));
        checkSqlite(sqlite3_step(s), mDb, "insert account");
        sqlite3_finalize(s);
        const int64_t id = sqlite3_last_insert_rowid(mDb);
        Log(Debug::Info) << "[PlayerDB] new account: '" << username << "' id=" << id;
        return id;
    }

    int64_t PlayerDatabase::lookupOrCreateAccount(std::string_view username)
    {
        const int64_t id = lookupAccount(username);
        return (id >= 0) ? id : createAccount(username);
    }

    std::string PlayerDatabase::getPasswordHash(int64_t accountId)
    {
        sqlite3_stmt* s = prepare("SELECT password_hash FROM accounts WHERE id = ?1");
        sqlite3_bind_int64(s, 1, accountId);
        std::string hash;
        if (sqlite3_step(s) == SQLITE_ROW)
        {
            const char* t = reinterpret_cast<const char*>(sqlite3_column_text(s, 0));
            if (t)
                hash = t;
        }
        sqlite3_finalize(s);
        return hash;
    }

    void PlayerDatabase::setPasswordHash(int64_t accountId, std::string_view hash)
    {
        sqlite3_stmt* s = prepare("UPDATE accounts SET password_hash = ?1 WHERE id = ?2");
        sqlite3_bind_text(s, 1, hash.data(), static_cast<int>(hash.size()), SQLITE_STATIC);
        sqlite3_bind_int64(s, 2, accountId);
        checkSqlite(sqlite3_step(s), mDb, "setPasswordHash");
        sqlite3_finalize(s);
    }

    std::optional<PlayerRecord> PlayerDatabase::lookupCharacter(int64_t accountId, std::string_view charName)
    {
        sqlite3_stmt* s = prepare(
            "SELECT id, name, cell, pos_x, pos_y, pos_z, rot_x, rot_y, rot_z, is_new,"
            " race, head_mesh, hair_mesh, is_male, class_id, class_name, birth_sign, class_data, nickname,"
            " inventory_saved, equipment_saved, stats_saved,"
            " spellbook_saved"
            " FROM characters WHERE account_id = ?1 AND name = ?2 LIMIT 1");
        sqlite3_bind_int64(s, 1, accountId);
        sqlite3_bind_text(s, 2, charName.data(), static_cast<int>(charName.size()), SQLITE_STATIC);
        const int rc = sqlite3_step(s);
        if (rc != SQLITE_ROW)
        {
            sqlite3_finalize(s);
            return std::nullopt;
        }

        PlayerRecord rec;
        rec.accountId = accountId;
        rec.characterId = sqlite3_column_int64(s, 0);
        auto col = [&](int i) -> std::string {
            const char* t = reinterpret_cast<const char*>(sqlite3_column_text(s, i));
            return t ? t : "";
        };
        rec.playerName = col(1);
        rec.cell = col(2);
        rec.posX = static_cast<float>(sqlite3_column_double(s, 3));
        rec.posY = static_cast<float>(sqlite3_column_double(s, 4));
        rec.posZ = static_cast<float>(sqlite3_column_double(s, 5));
        rec.rotX = static_cast<float>(sqlite3_column_double(s, 6));
        rec.rotY = static_cast<float>(sqlite3_column_double(s, 7));
        rec.rotZ = static_cast<float>(sqlite3_column_double(s, 8));
        rec.isNew = sqlite3_column_int(s, 9) != 0;
        rec.race = col(10);
        rec.headMesh = col(11);
        rec.hairMesh = col(12);
        rec.isMale = sqlite3_column_int(s, 13) != 0;
        rec.classId = col(14);
        rec.className = col(15);
        rec.birthSign = col(16);
        rec.classData = col(17);
        rec.nickname = col(18);
        rec.hasSavedInventory = sqlite3_column_int(s, 19) != 0;
        rec.hasSavedEquipment = sqlite3_column_int(s, 20) != 0;
        rec.hasSavedStats = sqlite3_column_int(s, 21) != 0;
        rec.hasSavedSpellbook = sqlite3_column_int(s, 22) != 0;
        sqlite3_finalize(s);
        return rec;
    }

    PlayerRecord PlayerDatabase::createCharacter(int64_t accountId, std::string_view charName)
    {
        sqlite3_stmt* s = prepare("INSERT INTO characters(account_id, name, is_new, last_seen) VALUES(?1, ?2, 1, ?3)");
        sqlite3_bind_int64(s, 1, accountId);
        sqlite3_bind_text(s, 2, charName.data(), static_cast<int>(charName.size()), SQLITE_STATIC);
        sqlite3_bind_int64(s, 3, static_cast<int64_t>(std::time(nullptr)));
        checkSqlite(sqlite3_step(s), mDb, "insert character");
        sqlite3_finalize(s);

        PlayerRecord rec;
        rec.accountId = accountId;
        rec.characterId = sqlite3_last_insert_rowid(mDb);
        rec.playerName = std::string(charName);
        rec.isNew = true;
        Log(Debug::Info) << "[PlayerDB] new character: '" << charName << "' id=" << rec.characterId
                         << " account=" << accountId;
        return rec;
    }

    bool PlayerDatabase::characterNameTaken(int64_t accountId, std::string_view charName)
    {
        sqlite3_stmt* s = prepare("SELECT 1 FROM characters WHERE account_id = ?1 AND name = ?2 LIMIT 1");
        sqlite3_bind_int64(s, 1, accountId);
        sqlite3_bind_text(s, 2, charName.data(), static_cast<int>(charName.size()), SQLITE_STATIC);
        const bool found = sqlite3_step(s) == SQLITE_ROW;
        sqlite3_finalize(s);
        return found;
    }

    PlayerRecord PlayerDatabase::lookupOrCreateCharacter(int64_t accountId, std::string_view charName)
    {
        auto existing = lookupCharacter(accountId, charName);
        return existing ? *existing : createCharacter(accountId, charName);
    }

    void PlayerDatabase::savePosition(
        int64_t characterId, std::string_view cell, float x, float y, float z, float rx, float ry, float rz)
    {
        sqlite3_stmt* s = prepare(
            "UPDATE characters SET cell=?1, pos_x=?2, pos_y=?3, pos_z=?4,"
            " rot_x=?5, rot_y=?6, rot_z=?7, last_seen=?8 WHERE id=?9");
        sqlite3_bind_text(s, 1, cell.data(), static_cast<int>(cell.size()), SQLITE_STATIC);
        sqlite3_bind_double(s, 2, x);
        sqlite3_bind_double(s, 3, y);
        sqlite3_bind_double(s, 4, z);
        sqlite3_bind_double(s, 5, rx);
        sqlite3_bind_double(s, 6, ry);
        sqlite3_bind_double(s, 7, rz);
        sqlite3_bind_int64(s, 8, static_cast<int64_t>(std::time(nullptr)));
        sqlite3_bind_int64(s, 9, characterId);
        checkSqlite(sqlite3_step(s), mDb, "savePosition");
        sqlite3_finalize(s);
    }

    void PlayerDatabase::saveChargenData(int64_t characterId, const std::string& race, const std::string& headMesh,
        const std::string& hairMesh, bool isMale, const std::string& classId, const std::string& className,
        const std::string& birthSign, const std::string& classData)
    {
        sqlite3_stmt* s = prepare(
            "UPDATE characters SET race=?1, head_mesh=?2, hair_mesh=?3, is_male=?4,"
            " class_id=?5, class_name=?6, birth_sign=?7, class_data=?8 WHERE id=?9");
        sqlite3_bind_text(s, 1, race.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(s, 2, headMesh.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(s, 3, hairMesh.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(s, 4, isMale ? 1 : 0);
        sqlite3_bind_text(s, 5, classId.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(s, 6, className.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(s, 7, birthSign.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(s, 8, classData.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(s, 9, characterId);
        checkSqlite(sqlite3_step(s), mDb, "saveChargenData");
        sqlite3_finalize(s);
        Log(Debug::Info) << "[PlayerDB] chargen data saved for char id=" << characterId << " race=" << race
                         << " class=" << classId << " birthSign=" << birthSign;
    }

    void PlayerDatabase::markChargenComplete(int64_t characterId)
    {
        sqlite3_stmt* s = prepare("UPDATE characters SET is_new=0, last_seen=?1 WHERE id=?2");
        sqlite3_bind_int64(s, 1, static_cast<int64_t>(std::time(nullptr)));
        sqlite3_bind_int64(s, 2, characterId);
        checkSqlite(sqlite3_step(s), mDb, "markChargenComplete");
        sqlite3_finalize(s);
        Log(Debug::Info) << "[PlayerDB] chargen complete for char id=" << characterId;
    }

    void PlayerDatabase::touch(int64_t characterId)
    {
        sqlite3_stmt* s = prepare("UPDATE characters SET last_seen=?1 WHERE id=?2");
        sqlite3_bind_int64(s, 1, static_cast<int64_t>(std::time(nullptr)));
        sqlite3_bind_int64(s, 2, characterId);
        checkSqlite(sqlite3_step(s), mDb, "touch");
        sqlite3_finalize(s);
    }

    bool PlayerDatabase::deleteCharacter(int64_t accountId, std::string_view charName)
    {
        sqlite3_stmt* s = prepare("DELETE FROM characters WHERE account_id=?1 AND name=?2");
        sqlite3_bind_int64(s, 1, accountId);
        sqlite3_bind_text(s, 2, charName.data(), static_cast<int>(charName.size()), SQLITE_TRANSIENT);
        checkSqlite(sqlite3_step(s), mDb, "deleteCharacter");
        int changes = sqlite3_changes(mDb);
        sqlite3_finalize(s);
        return changes > 0;
    }

    void PlayerDatabase::setNickname(int64_t characterId, std::string_view nickname)
    {
        sqlite3_stmt* s = prepare("UPDATE characters SET nickname=?1 WHERE id=?2");
        sqlite3_bind_text(s, 1, nickname.data(), static_cast<int>(nickname.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int64(s, 2, characterId);
        checkSqlite(sqlite3_step(s), mDb, "setNickname");
        sqlite3_finalize(s);
    }

    std::vector<PlayerMark> PlayerDatabase::loadCharacterMarks(int64_t characterId)
    {
        sqlite3_stmt* s = prepare(
            "SELECT mark_name, cell, pos_x, pos_y, pos_z, rot_x, rot_y, rot_z"
            " FROM character_marks WHERE character_id=?1 ORDER BY mark_name");
        sqlite3_bind_int64(s, 1, characterId);

        std::vector<PlayerMark> marks;
        while (sqlite3_step(s) == SQLITE_ROW)
        {
            PlayerMark mark;
            auto col = [&](int i) -> std::string {
                const char* t = reinterpret_cast<const char*>(sqlite3_column_text(s, i));
                return t ? t : "";
            };

            mark.name = col(0);
            mark.cell = col(1);
            mark.position.pos[0] = static_cast<float>(sqlite3_column_double(s, 2));
            mark.position.pos[1] = static_cast<float>(sqlite3_column_double(s, 3));
            mark.position.pos[2] = static_cast<float>(sqlite3_column_double(s, 4));
            mark.position.rot[0] = static_cast<float>(sqlite3_column_double(s, 5));
            mark.position.rot[1] = static_cast<float>(sqlite3_column_double(s, 6));
            mark.position.rot[2] = static_cast<float>(sqlite3_column_double(s, 7));
            marks.push_back(std::move(mark));
        }

        sqlite3_finalize(s);
        return marks;
    }

    void PlayerDatabase::upsertCharacterMark(int64_t characterId, const PlayerMark& mark)
    {
        sqlite3_stmt* s = prepare(
            "INSERT INTO character_marks(character_id, mark_name, cell, pos_x, pos_y, pos_z, rot_x, rot_y, rot_z)"
            " VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)"
            " ON CONFLICT(character_id, mark_name) DO UPDATE SET"
            " cell=excluded.cell,"
            " pos_x=excluded.pos_x,"
            " pos_y=excluded.pos_y,"
            " pos_z=excluded.pos_z,"
            " rot_x=excluded.rot_x,"
            " rot_y=excluded.rot_y,"
            " rot_z=excluded.rot_z");
        sqlite3_bind_int64(s, 1, characterId);
        sqlite3_bind_text(s, 2, mark.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 3, mark.cell.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(s, 4, mark.position.pos[0]);
        sqlite3_bind_double(s, 5, mark.position.pos[1]);
        sqlite3_bind_double(s, 6, mark.position.pos[2]);
        sqlite3_bind_double(s, 7, mark.position.rot[0]);
        sqlite3_bind_double(s, 8, mark.position.rot[1]);
        sqlite3_bind_double(s, 9, mark.position.rot[2]);
        checkSqlite(sqlite3_step(s), mDb, "upsertCharacterMark");
        sqlite3_finalize(s);
    }

    void PlayerDatabase::deleteCharacterMark(int64_t characterId, std::string_view name)
    {
        sqlite3_stmt* s = prepare("DELETE FROM character_marks WHERE character_id=?1 AND mark_name=?2");
        sqlite3_bind_int64(s, 1, characterId);
        sqlite3_bind_text(s, 2, name.data(), static_cast<int>(name.size()), SQLITE_TRANSIENT);
        checkSqlite(sqlite3_step(s), mDb, "deleteCharacterMark");
        sqlite3_finalize(s);
    }

    std::vector<Item> PlayerDatabase::loadCharacterInventory(int64_t characterId)
    {
        sqlite3_stmt* s = prepare(
            "SELECT ref_id, item_count, charge, enchantment_charge, soul, instance_id"
            " FROM character_inventory WHERE character_id=?1 ORDER BY item_index");
        sqlite3_bind_int64(s, 1, characterId);

        std::vector<Item> items;
        while (sqlite3_step(s) == SQLITE_ROW)
        {
            Item item;
            auto col = [&](int i) -> std::string {
                const char* t = reinterpret_cast<const char*>(sqlite3_column_text(s, i));
                return t ? t : "";
            };

            item.refId = col(0);
            item.count = sqlite3_column_int(s, 1);
            item.charge = sqlite3_column_int(s, 2);
            item.enchantmentCharge = static_cast<float>(sqlite3_column_double(s, 3));
            item.soul = col(4);
            item.instanceId = static_cast<uint32_t>(sqlite3_column_int64(s, 5));
            items.push_back(std::move(item));
        }

        sqlite3_finalize(s);
        return items;
    }

    void PlayerDatabase::saveCharacterInventory(int64_t characterId, const std::vector<Item>& items,
        bool touchLastSeen, std::optional<uint64_t> inventoryRevision)
    {
        exec("BEGIN");
        try
        {
            const std::string characterKey = std::to_string(characterId);
            sqlite3_stmt* clear = prepare("DELETE FROM character_inventory WHERE character_id=?1");
            sqlite3_bind_int64(clear, 1, characterId);
            checkSqlite(sqlite3_step(clear), mDb, "clearCharacterInventory");
            sqlite3_finalize(clear);

            sqlite3_stmt* insert = prepare(
                "INSERT INTO character_inventory(character_id, item_index, ref_id, item_count, charge, "
                "enchantment_charge, soul, instance_id)"
                " VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)");

            for (std::size_t i = 0; i < items.size(); ++i)
            {
                const Item& item = items[i];
                sqlite3_bind_int64(insert, 1, characterId);
                sqlite3_bind_int(insert, 2, static_cast<int>(i));
                sqlite3_bind_text(insert, 3, item.refId.c_str(), static_cast<int>(item.refId.size()), SQLITE_TRANSIENT);
                sqlite3_bind_int(insert, 4, item.count);
                sqlite3_bind_int(insert, 5, item.charge);
                sqlite3_bind_double(insert, 6, item.enchantmentCharge);
                sqlite3_bind_text(insert, 7, item.soul.c_str(), static_cast<int>(item.soul.size()), SQLITE_TRANSIENT);
                sqlite3_bind_int64(insert, 8, item.instanceId);
                checkSqlite(sqlite3_step(insert), mDb, "insertCharacterInventory");
                sqlite3_reset(insert);
                sqlite3_clear_bindings(insert);
            }
            sqlite3_finalize(insert);

            sqlite3_stmt* clearLinks = prepare(
                "DELETE FROM world_dynamic_record_links"
                " WHERE link_kind=?1 AND owner_a=?2 AND owner_b=?3 AND owner_c=?4");
            clearDynamicRecordLinksForOwner(mDb, clearLinks, "inventory_item", characterKey, "", "");
            sqlite3_finalize(clearLinks);

            sqlite3_stmt* insertLink = prepare(
                "INSERT OR REPLACE INTO world_dynamic_record_links(record_id, link_kind, owner_a, owner_b, owner_c, "
                "owner_index)"
                " VALUES(?1, ?2, ?3, ?4, ?5, ?6)");
            for (std::size_t i = 0; i < items.size(); ++i)
                insertDynamicRecordLink(
                    mDb, insertLink, items[i].refId, "inventory_item", characterKey, "", "",
                    items[i].instanceId != 0 ? items[i].instanceId : static_cast<int64_t>(i));
            sqlite3_finalize(insertLink);

            sqlite3_stmt* mark = prepare(
                touchLastSeen
                    ? (inventoryRevision
                        ? "UPDATE characters SET inventory_saved=1, inventory_revision=?1, last_seen=?2 WHERE id=?3"
                        : "UPDATE characters SET inventory_saved=1, last_seen=?1 WHERE id=?2")
                    : (inventoryRevision
                        ? "UPDATE characters SET inventory_saved=1, inventory_revision=?1 WHERE id=?2"
                        : "UPDATE characters SET inventory_saved=1 WHERE id=?1"));
            if (touchLastSeen)
            {
                if (inventoryRevision)
                {
                    sqlite3_bind_int64(mark, 1, static_cast<sqlite3_int64>(*inventoryRevision));
                    sqlite3_bind_int64(mark, 2, static_cast<int64_t>(std::time(nullptr)));
                    sqlite3_bind_int64(mark, 3, characterId);
                }
                else
                {
                    sqlite3_bind_int64(mark, 1, static_cast<int64_t>(std::time(nullptr)));
                    sqlite3_bind_int64(mark, 2, characterId);
                }
            }
            else
            {
                if (inventoryRevision)
                {
                    sqlite3_bind_int64(mark, 1, static_cast<sqlite3_int64>(*inventoryRevision));
                    sqlite3_bind_int64(mark, 2, characterId);
                }
                else
                    sqlite3_bind_int64(mark, 1, characterId);
            }
            checkSqlite(sqlite3_step(mark), mDb, "markCharacterInventorySaved");
            sqlite3_finalize(mark);

            exec("COMMIT");
        }
        catch (...)
        {
            try
            {
                exec("ROLLBACK");
            }
            catch (...)
            {
            }
            throw;
        }
    }

    WorldItemTakeCommitResult PlayerDatabase::commitWorldItemTake(const WorldItemTakeCommit& commit)
    {
        auto bindIdentity = [](sqlite3_stmt* statement, int first, const PlacedObjectIdentity& identity) {
            sqlite3_bind_int(statement, first, static_cast<int>(identity.kind));
            sqlite3_bind_text(statement, first + 1, identity.cellId.c_str(),
                static_cast<int>(identity.cellId.size()), SQLITE_TRANSIENT);
            sqlite3_bind_text(statement, first + 2, identity.refId.c_str(),
                static_cast<int>(identity.refId.size()), SQLITE_TRANSIENT);
            sqlite3_bind_int64(statement, first + 3, identity.refIndex);
            sqlite3_bind_int(statement, first + 4, identity.refContentFile);
            sqlite3_bind_int64(statement, first + 5, identity.mpNum);
        };
        auto readStoredResult = [](sqlite3_stmt* statement) {
            auto textColumn = [&](int column) {
                const char* value = reinterpret_cast<const char*>(sqlite3_column_text(statement, column));
                return std::string(value ? value : "");
            };
            WorldItemTakeResult result;
            result.accepted = true;
            result.object.kind = static_cast<PlacedObjectKind>(sqlite3_column_int(statement, 1));
            result.object.cellId = textColumn(2);
            result.object.refId = textColumn(3);
            result.object.refIndex = static_cast<std::uint32_t>(sqlite3_column_int64(statement, 4));
            result.object.refContentFile = sqlite3_column_int(statement, 5);
            result.object.mpNum = static_cast<std::uint32_t>(sqlite3_column_int64(statement, 6));
            result.itemRefId = textColumn(7);
            result.itemCount = sqlite3_column_int(statement, 8);
            result.crimeValue = sqlite3_column_int64(statement, 9);
            result.theft = sqlite3_column_int(statement, 10) != 0;
            result.inventoryRevision = static_cast<std::uint64_t>(sqlite3_column_int64(statement, 11));
            return result;
        };

        exec("BEGIN IMMEDIATE");
        try
        {
            sqlite3_stmt* existing = prepare(
                "SELECT request_hash, object_kind, cell_id, ref_id, ref_index, ref_content_file, mp_num,"
                " item_ref_id, item_count, crime_value, theft, inventory_revision"
                " FROM world_item_take_requests"
                " WHERE account_id=?1 AND character_id=?2 AND request_id=?3");
            sqlite3_bind_int64(existing, 1, commit.accountId);
            sqlite3_bind_int64(existing, 2, commit.characterId);
            sqlite3_bind_text(existing, 3, commit.requestId.c_str(),
                static_cast<int>(commit.requestId.size()), SQLITE_TRANSIENT);
            if (sqlite3_step(existing) == SQLITE_ROW)
            {
                const char* storedHashText
                    = reinterpret_cast<const char*>(sqlite3_column_text(existing, 0));
                const bool sameHash = storedHashText && commit.requestHash == storedHashText;
                WorldItemTakeCommitResult duplicate;
                duplicate.status = sameHash ? WorldItemTakeCommitStatus::DuplicateRequest
                                            : WorldItemTakeCommitStatus::DuplicateRequestConflict;
                duplicate.result = readStoredResult(existing);
                duplicate.result.requestId = commit.requestId;
                duplicate.result.replayed = sameHash;
                sqlite3_finalize(existing);
                exec("COMMIT");
                return duplicate;
            }
            sqlite3_finalize(existing);

            sqlite3_stmt* revision = prepare(
                "SELECT inventory_revision FROM characters WHERE id=?1 AND account_id=?2");
            sqlite3_bind_int64(revision, 1, commit.characterId);
            sqlite3_bind_int64(revision, 2, commit.accountId);
            const bool revisionMatches = sqlite3_step(revision) == SQLITE_ROW
                && static_cast<std::uint64_t>(sqlite3_column_int64(revision, 0))
                    == commit.expectedInventoryRevision;
            sqlite3_finalize(revision);
            if (!revisionMatches)
            {
                exec("COMMIT");
                return { WorldItemTakeCommitStatus::StaleInventoryRevision, {} };
            }

            sqlite3_stmt* taken = prepare(
                "SELECT 1 FROM world_taken_references WHERE object_kind=?1 AND cell_id=?2 AND ref_id=?3"
                " AND ref_index=?4 AND ref_content_file=?5 AND mp_num=?6");
            bindIdentity(taken, 1, commit.object);
            const bool alreadyTaken = sqlite3_step(taken) == SQLITE_ROW;
            sqlite3_finalize(taken);
            if (alreadyTaken)
            {
                exec("COMMIT");
                return { WorldItemTakeCommitStatus::ObjectAlreadyTaken, {} };
            }

            sqlite3_stmt* insertTaken = prepare(
                "INSERT INTO world_taken_references(object_kind, cell_id, ref_id, ref_index, ref_content_file,"
                " mp_num, taken_by_character, take_request_id, taken_at)"
                " VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)");
            bindIdentity(insertTaken, 1, commit.object);
            sqlite3_bind_int64(insertTaken, 7, commit.characterId);
            sqlite3_bind_text(insertTaken, 8, commit.requestId.c_str(),
                static_cast<int>(commit.requestId.size()), SQLITE_TRANSIENT);
            sqlite3_bind_int64(insertTaken, 9, static_cast<int64_t>(std::time(nullptr)));
            checkSqlite(sqlite3_step(insertTaken), mDb, "insertTakenWorldReference");
            sqlite3_finalize(insertTaken);

            if (commit.object.kind == PlacedObjectKind::ServerPlaced)
            {
                sqlite3_stmt* removeWorld = prepare("DELETE FROM world_objects WHERE mp_num=?1");
                sqlite3_bind_int64(removeWorld, 1, commit.object.mpNum);
                checkSqlite(sqlite3_step(removeWorld), mDb, "deleteTakenWorldObject");
                sqlite3_finalize(removeWorld);

                const std::string placedOwner = std::to_string(commit.object.mpNum);
                sqlite3_stmt* clearPlacedLink = prepare(
                    "DELETE FROM world_dynamic_record_links WHERE link_kind='placed_object' AND owner_a=?1");
                sqlite3_bind_text(clearPlacedLink, 1, placedOwner.c_str(), -1, SQLITE_TRANSIENT);
                checkSqlite(sqlite3_step(clearPlacedLink), mDb, "deleteTakenWorldObjectLink");
                sqlite3_finalize(clearPlacedLink);
            }

            sqlite3_stmt* clear = prepare("DELETE FROM character_inventory WHERE character_id=?1");
            sqlite3_bind_int64(clear, 1, commit.characterId);
            checkSqlite(sqlite3_step(clear), mDb, "clearTakeInventory");
            sqlite3_finalize(clear);
            sqlite3_stmt* insert = prepare(
                "INSERT INTO character_inventory(character_id, item_index, ref_id, item_count, charge,"
                " enchantment_charge, soul, instance_id) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)");
            for (std::size_t index = 0; index < commit.inventory.size(); ++index)
            {
                const Item& item = commit.inventory[index];
                sqlite3_bind_int64(insert, 1, commit.characterId);
                sqlite3_bind_int(insert, 2, static_cast<int>(index));
                sqlite3_bind_text(insert, 3, item.refId.c_str(), static_cast<int>(item.refId.size()), SQLITE_TRANSIENT);
                sqlite3_bind_int(insert, 4, item.count);
                sqlite3_bind_int(insert, 5, item.charge);
                sqlite3_bind_double(insert, 6, item.enchantmentCharge);
                sqlite3_bind_text(insert, 7, item.soul.c_str(), static_cast<int>(item.soul.size()), SQLITE_TRANSIENT);
                sqlite3_bind_int64(insert, 8, item.instanceId);
                checkSqlite(sqlite3_step(insert), mDb, "insertTakeInventory");
                sqlite3_reset(insert);
                sqlite3_clear_bindings(insert);
            }
            sqlite3_finalize(insert);

            const std::string characterKey = std::to_string(commit.characterId);
            sqlite3_stmt* clearInventoryLinks = prepare(
                "DELETE FROM world_dynamic_record_links"
                " WHERE link_kind='inventory_item' AND owner_a=?1 AND owner_b='' AND owner_c=''");
            sqlite3_bind_text(clearInventoryLinks, 1, characterKey.c_str(), -1, SQLITE_TRANSIENT);
            checkSqlite(sqlite3_step(clearInventoryLinks), mDb, "clearTakeInventoryLinks");
            sqlite3_finalize(clearInventoryLinks);
            sqlite3_stmt* insertInventoryLink = prepare(
                "INSERT OR REPLACE INTO world_dynamic_record_links(record_id, link_kind, owner_a, owner_b, owner_c,"
                " owner_index) VALUES(?1, 'inventory_item', ?2, '', '', ?3)");
            for (std::size_t index = 0; index < commit.inventory.size(); ++index)
            {
                const Item& item = commit.inventory[index];
                sqlite3_bind_text(insertInventoryLink, 1, item.refId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(insertInventoryLink, 2, characterKey.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(insertInventoryLink, 3,
                    item.instanceId != 0 ? item.instanceId : static_cast<sqlite3_int64>(index));
                checkSqlite(sqlite3_step(insertInventoryLink), mDb, "insertTakeInventoryLink");
                sqlite3_reset(insertInventoryLink);
                sqlite3_clear_bindings(insertInventoryLink);
            }
            sqlite3_finalize(insertInventoryLink);

            sqlite3_stmt* updateRevision = prepare(
                "UPDATE characters SET inventory_saved=1, inventory_revision=?1, last_seen=?2"
                " WHERE id=?3 AND account_id=?4 AND inventory_revision=?5");
            sqlite3_bind_int64(updateRevision, 1,
                static_cast<sqlite3_int64>(commit.resultingInventoryRevision));
            sqlite3_bind_int64(updateRevision, 2, static_cast<int64_t>(std::time(nullptr)));
            sqlite3_bind_int64(updateRevision, 3, commit.characterId);
            sqlite3_bind_int64(updateRevision, 4, commit.accountId);
            sqlite3_bind_int64(updateRevision, 5,
                static_cast<sqlite3_int64>(commit.expectedInventoryRevision));
            checkSqlite(sqlite3_step(updateRevision), mDb, "updateTakeInventoryRevision");
            if (sqlite3_changes(mDb) != 1)
                throw std::runtime_error("inventory revision changed during world item take");
            sqlite3_finalize(updateRevision);

            if (commit.crimeMutation)
            {
                const CrimeCommitResult crime = commitPlayerCrimeMutationInTransaction(*commit.crimeMutation);
                if (crime.status == CrimeCommitStatus::StaleRevision)
                {
                    exec("ROLLBACK");
                    return { WorldItemTakeCommitStatus::StaleCrimeRevision, {} };
                }
                if (crime.status == CrimeCommitStatus::DuplicateRequestConflict)
                {
                    exec("ROLLBACK");
                    return { WorldItemTakeCommitStatus::CrimeDuplicateConflict, {} };
                }
            }

            sqlite3_stmt* request = prepare(
                "INSERT INTO world_item_take_requests(account_id, character_id, request_id, request_hash,"
                " object_kind, cell_id, ref_id, ref_index, ref_content_file, mp_num, item_ref_id, item_count,"
                " crime_value, theft, inventory_revision, created_at)"
                " VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16)");
            sqlite3_bind_int64(request, 1, commit.accountId);
            sqlite3_bind_int64(request, 2, commit.characterId);
            sqlite3_bind_text(request, 3, commit.requestId.c_str(), static_cast<int>(commit.requestId.size()), SQLITE_TRANSIENT);
            sqlite3_bind_text(request, 4, commit.requestHash.c_str(), static_cast<int>(commit.requestHash.size()), SQLITE_TRANSIENT);
            bindIdentity(request, 5, commit.object);
            sqlite3_bind_text(request, 11, commit.result.itemRefId.c_str(), static_cast<int>(commit.result.itemRefId.size()), SQLITE_TRANSIENT);
            sqlite3_bind_int(request, 12, commit.result.itemCount);
            sqlite3_bind_int64(request, 13, commit.result.crimeValue);
            sqlite3_bind_int(request, 14, commit.result.theft ? 1 : 0);
            sqlite3_bind_int64(request, 15, static_cast<sqlite3_int64>(commit.resultingInventoryRevision));
            sqlite3_bind_int64(request, 16, static_cast<int64_t>(std::time(nullptr)));
            checkSqlite(sqlite3_step(request), mDb, "insertWorldItemTakeRequest");
            sqlite3_finalize(request);

            exec("COMMIT");
            return { WorldItemTakeCommitStatus::Committed, commit.result };
        }
        catch (...)
        {
            try { exec("ROLLBACK"); } catch (...) {}
            throw;
        }
    }

    std::optional<StoredInventoryTake> PlayerDatabase::loadInventoryTake(
        std::int64_t accountId, std::int64_t characterId, std::string_view requestId)
    {
        sqlite3_stmt* statement = prepare(
            "SELECT request_hash, take_kind, cell_id, source_ref_id, source_ref_num, source_mp_num,"
            " source_actor_id, source_migration_generation, item_ref_id, item_charge, item_count,"
            " inventory_revision, detected, detection_roll, theft, crime_value"
            " FROM inventory_take_requests WHERE account_id=?1 AND character_id=?2 AND request_id=?3");
        sqlite3_bind_int64(statement, 1, accountId);
        sqlite3_bind_int64(statement, 2, characterId);
        sqlite3_bind_text(statement, 3, requestId.data(), static_cast<int>(requestId.size()), SQLITE_TRANSIENT);
        if (sqlite3_step(statement) != SQLITE_ROW)
        {
            sqlite3_finalize(statement);
            return std::nullopt;
        }
        auto textColumn = [](sqlite3_stmt* row, int column) {
            const char* value = reinterpret_cast<const char*>(sqlite3_column_text(row, column));
            return std::string(value ? value : "");
        };
        StoredInventoryTake stored;
        stored.requestHash = textColumn(statement, 0);
        InventoryTakeResult& result = stored.result;
        result.requestId = std::string(requestId);
        result.accepted = true;
        result.replayed = true;
        result.kind = static_cast<InventoryTakeKind>(sqlite3_column_int(statement, 1));
        result.source.cellId = textColumn(statement, 2);
        result.source.refId = textColumn(statement, 3);
        result.source.refNum = static_cast<std::uint32_t>(sqlite3_column_int64(statement, 4));
        result.source.mpNum = static_cast<std::uint32_t>(sqlite3_column_int64(statement, 5));
        result.source.actorInstanceId = static_cast<std::uint64_t>(sqlite3_column_int64(statement, 6));
        result.source.migrationGeneration = static_cast<std::uint32_t>(sqlite3_column_int64(statement, 7));
        result.itemRefId = textColumn(statement, 8);
        result.itemCharge = sqlite3_column_int(statement, 9);
        result.itemCount = sqlite3_column_int(statement, 10);
        result.inventoryRevision = static_cast<std::uint64_t>(sqlite3_column_int64(statement, 11));
        result.detected = sqlite3_column_int(statement, 12) != 0;
        result.detectionRoll = sqlite3_column_int(statement, 13);
        result.theft = sqlite3_column_int(statement, 14) != 0;
        result.crimeValue = sqlite3_column_int64(statement, 15);
        sqlite3_finalize(statement);
        return stored;
    }

    InventoryTakeCommitResult PlayerDatabase::commitInventoryTake(const InventoryTakeCommit& commit)
    {
        auto textColumn = [](sqlite3_stmt* statement, int column) {
            const char* value = reinterpret_cast<const char*>(sqlite3_column_text(statement, column));
            return std::string(value ? value : "");
        };
        auto storedResult = [&](sqlite3_stmt* statement) {
            InventoryTakeResult result;
            result.requestId = commit.requestId;
            result.accepted = true;
            result.kind = static_cast<InventoryTakeKind>(sqlite3_column_int(statement, 1));
            result.source.cellId = textColumn(statement, 2);
            result.source.refId = textColumn(statement, 3);
            result.source.refNum = static_cast<std::uint32_t>(sqlite3_column_int64(statement, 4));
            result.source.mpNum = static_cast<std::uint32_t>(sqlite3_column_int64(statement, 5));
            result.source.actorInstanceId = static_cast<std::uint64_t>(sqlite3_column_int64(statement, 6));
            result.source.migrationGeneration = static_cast<std::uint32_t>(sqlite3_column_int64(statement, 7));
            result.itemRefId = textColumn(statement, 8);
            result.itemCharge = sqlite3_column_int(statement, 9);
            result.itemCount = sqlite3_column_int(statement, 10);
            result.inventoryRevision = static_cast<std::uint64_t>(sqlite3_column_int64(statement, 11));
            result.detected = sqlite3_column_int(statement, 12) != 0;
            result.detectionRoll = sqlite3_column_int(statement, 13);
            result.theft = sqlite3_column_int(statement, 14) != 0;
            result.crimeValue = sqlite3_column_int64(statement, 15);
            return result;
        };

        exec("BEGIN IMMEDIATE");
        try
        {
            sqlite3_stmt* existing = prepare(
                "SELECT request_hash, take_kind, cell_id, source_ref_id, source_ref_num, source_mp_num,"
                " source_actor_id, source_migration_generation, item_ref_id, item_charge, item_count,"
                " inventory_revision, detected, detection_roll, theft, crime_value"
                " FROM inventory_take_requests WHERE account_id=?1 AND character_id=?2 AND request_id=?3");
            sqlite3_bind_int64(existing, 1, commit.accountId);
            sqlite3_bind_int64(existing, 2, commit.characterId);
            sqlite3_bind_text(existing, 3, commit.requestId.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(existing) == SQLITE_ROW)
            {
                const std::string storedHash = textColumn(existing, 0);
                InventoryTakeCommitResult duplicate;
                duplicate.status = storedHash == commit.requestHash
                    ? InventoryTakeCommitStatus::DuplicateRequest
                    : InventoryTakeCommitStatus::DuplicateRequestConflict;
                duplicate.result = storedResult(existing);
                duplicate.result.replayed = duplicate.status == InventoryTakeCommitStatus::DuplicateRequest;
                sqlite3_finalize(existing);
                exec("COMMIT");
                return duplicate;
            }
            sqlite3_finalize(existing);

            sqlite3_stmt* revision = prepare(
                "SELECT inventory_revision FROM characters WHERE id=?1 AND account_id=?2");
            sqlite3_bind_int64(revision, 1, commit.characterId);
            sqlite3_bind_int64(revision, 2, commit.accountId);
            const bool revisionMatches = sqlite3_step(revision) == SQLITE_ROW
                && static_cast<std::uint64_t>(sqlite3_column_int64(revision, 0))
                    == commit.expectedInventoryRevision;
            sqlite3_finalize(revision);
            if (!revisionMatches)
            {
                exec("COMMIT");
                return { InventoryTakeCommitStatus::StaleInventoryRevision, {} };
            }

            if (commit.expectedSource)
            {
                const ContainerRecord& expected = *commit.expectedSource;
                sqlite3_stmt* items = prepare(
                    "SELECT item_ref_id, item_count, charge FROM world_container_items"
                    " WHERE cell_id=?1 AND ref_id=?2 AND ref_num=?3 ORDER BY item_index");
                sqlite3_bind_text(items, 1, expected.cellId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(items, 2, expected.refId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(items, 3, expected.refNum);
                std::vector<ContainerItem> stored;
                while (sqlite3_step(items) == SQLITE_ROW)
                    stored.push_back({ textColumn(items, 0), sqlite3_column_int(items, 1), sqlite3_column_int(items, 2) });
                sqlite3_finalize(items);
                if (stored != expected.items)
                {
                    exec("COMMIT");
                    return { InventoryTakeCommitStatus::StaleSource, {} };
                }
            }

            if (commit.resultingSource)
            {
                const ContainerRecord& source = *commit.resultingSource;
                sqlite3_stmt* parent = prepare(
                    "INSERT INTO world_containers(cell_id, ref_id, ref_num, mp_num, has_authority)"
                    " VALUES(?1, ?2, ?3, ?4, 1) ON CONFLICT(cell_id, ref_id, ref_num) DO UPDATE SET"
                    " mp_num=excluded.mp_num, has_authority=1");
                sqlite3_bind_text(parent, 1, source.cellId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(parent, 2, source.refId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(parent, 3, source.refNum);
                sqlite3_bind_int64(parent, 4, source.mpNum);
                checkSqlite(sqlite3_step(parent), mDb, "commitInventoryTake(sourceParent)");
                sqlite3_finalize(parent);

                sqlite3_stmt* clear = prepare(
                    "DELETE FROM world_container_items WHERE cell_id=?1 AND ref_id=?2 AND ref_num=?3");
                sqlite3_bind_text(clear, 1, source.cellId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(clear, 2, source.refId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(clear, 3, source.refNum);
                checkSqlite(sqlite3_step(clear), mDb, "commitInventoryTake(clearSource)");
                sqlite3_finalize(clear);
                sqlite3_stmt* insert = prepare(
                    "INSERT INTO world_container_items(cell_id, ref_id, ref_num, item_index, item_ref_id,"
                    " item_count, charge) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7)");
                for (std::size_t index = 0; index < source.items.size(); ++index)
                {
                    const ContainerItem& item = source.items[index];
                    sqlite3_bind_text(insert, 1, source.cellId.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(insert, 2, source.refId.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int64(insert, 3, source.refNum);
                    sqlite3_bind_int(insert, 4, static_cast<int>(index));
                    sqlite3_bind_text(insert, 5, item.refId.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int(insert, 6, item.count);
                    sqlite3_bind_int(insert, 7, item.charge);
                    checkSqlite(sqlite3_step(insert), mDb, "commitInventoryTake(insertSource)");
                    sqlite3_reset(insert);
                    sqlite3_clear_bindings(insert);
                }
                sqlite3_finalize(insert);

                const std::string ownerC = std::to_string(source.refNum);
                sqlite3_stmt* clearLinks = prepare(
                    "DELETE FROM world_dynamic_record_links"
                    " WHERE link_kind=?1 AND owner_a=?2 AND owner_b=?3 AND owner_c=?4");
                clearDynamicRecordLinksForOwner(
                    mDb, clearLinks, "container_parent", source.cellId, source.refId, ownerC);
                clearDynamicRecordLinksForOwner(
                    mDb, clearLinks, "container_item", source.cellId, source.refId, ownerC);
                sqlite3_finalize(clearLinks);
                sqlite3_stmt* insertLink = prepare(
                    "INSERT OR REPLACE INTO world_dynamic_record_links(record_id, link_kind, owner_a, owner_b,"
                    " owner_c, owner_index) VALUES(?1, ?2, ?3, ?4, ?5, ?6)");
                insertDynamicRecordLink(mDb, insertLink, source.refId, "container_parent",
                    source.cellId, source.refId, ownerC, 0);
                for (std::size_t index = 0; index < source.items.size(); ++index)
                    insertDynamicRecordLink(mDb, insertLink, source.items[index].refId, "container_item",
                        source.cellId, source.refId, ownerC, static_cast<std::int64_t>(index));
                sqlite3_finalize(insertLink);
            }

            if (commit.resultingInventoryRevision != commit.expectedInventoryRevision)
            {
                sqlite3_stmt* clear = prepare("DELETE FROM character_inventory WHERE character_id=?1");
                sqlite3_bind_int64(clear, 1, commit.characterId);
                checkSqlite(sqlite3_step(clear), mDb, "commitInventoryTake(clearInventory)");
                sqlite3_finalize(clear);
                sqlite3_stmt* insert = prepare(
                    "INSERT INTO character_inventory(character_id, item_index, ref_id, item_count, charge,"
                    " enchantment_charge, soul, instance_id) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)");
                for (std::size_t index = 0; index < commit.inventory.size(); ++index)
                {
                    const Item& item = commit.inventory[index];
                    sqlite3_bind_int64(insert, 1, commit.characterId);
                    sqlite3_bind_int(insert, 2, static_cast<int>(index));
                    sqlite3_bind_text(insert, 3, item.refId.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int(insert, 4, item.count);
                    sqlite3_bind_int(insert, 5, item.charge);
                    sqlite3_bind_double(insert, 6, item.enchantmentCharge);
                    sqlite3_bind_text(insert, 7, item.soul.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int64(insert, 8, item.instanceId);
                    checkSqlite(sqlite3_step(insert), mDb, "commitInventoryTake(insertInventory)");
                    sqlite3_reset(insert);
                    sqlite3_clear_bindings(insert);
                }
                sqlite3_finalize(insert);
                const std::string characterKey = std::to_string(commit.characterId);
                sqlite3_stmt* clearLinks = prepare(
                    "DELETE FROM world_dynamic_record_links"
                    " WHERE link_kind=?1 AND owner_a=?2 AND owner_b=?3 AND owner_c=?4");
                clearDynamicRecordLinksForOwner(mDb, clearLinks, "inventory_item", characterKey, "", "");
                sqlite3_finalize(clearLinks);
                sqlite3_stmt* insertLink = prepare(
                    "INSERT OR REPLACE INTO world_dynamic_record_links(record_id, link_kind, owner_a, owner_b,"
                    " owner_c, owner_index) VALUES(?1, ?2, ?3, ?4, ?5, ?6)");
                for (std::size_t index = 0; index < commit.inventory.size(); ++index)
                {
                    const Item& item = commit.inventory[index];
                    insertDynamicRecordLink(mDb, insertLink, item.refId, "inventory_item", characterKey, "", "",
                        item.instanceId != 0 ? item.instanceId : static_cast<std::int64_t>(index));
                }
                sqlite3_finalize(insertLink);
                sqlite3_stmt* update = prepare(
                    "UPDATE characters SET inventory_saved=1, inventory_revision=?1, last_seen=?2"
                    " WHERE id=?3 AND account_id=?4 AND inventory_revision=?5");
                sqlite3_bind_int64(update, 1, static_cast<sqlite3_int64>(commit.resultingInventoryRevision));
                sqlite3_bind_int64(update, 2, static_cast<sqlite3_int64>(std::time(nullptr)));
                sqlite3_bind_int64(update, 3, commit.characterId);
                sqlite3_bind_int64(update, 4, commit.accountId);
                sqlite3_bind_int64(update, 5, static_cast<sqlite3_int64>(commit.expectedInventoryRevision));
                checkSqlite(sqlite3_step(update), mDb, "commitInventoryTake(updateRevision)");
                if (sqlite3_changes(mDb) != 1)
                    throw std::runtime_error("inventory revision changed during inventory take");
                sqlite3_finalize(update);
            }

            if (commit.crimeMutation)
            {
                const CrimeCommitResult crime = commitPlayerCrimeMutationInTransaction(*commit.crimeMutation);
                if (crime.status == CrimeCommitStatus::StaleRevision)
                {
                    exec("ROLLBACK");
                    return { InventoryTakeCommitStatus::StaleCrimeRevision, {} };
                }
                if (crime.status == CrimeCommitStatus::DuplicateRequestConflict)
                {
                    exec("ROLLBACK");
                    return { InventoryTakeCommitStatus::CrimeDuplicateConflict, {} };
                }
            }

            const InventorySourceIdentity& source = commit.result.source;
            sqlite3_stmt* request = prepare(
                "INSERT INTO inventory_take_requests(account_id, character_id, request_id, request_hash, take_kind,"
                " cell_id, source_ref_id, source_ref_num, source_mp_num, source_actor_id,"
                " source_migration_generation, item_ref_id, item_charge, item_count, inventory_revision,"
                " detected, detection_roll, theft, crime_value, created_at)"
                " VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16,?17,?18,?19,?20)");
            sqlite3_bind_int64(request, 1, commit.accountId);
            sqlite3_bind_int64(request, 2, commit.characterId);
            sqlite3_bind_text(request, 3, commit.requestId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(request, 4, commit.requestHash.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(request, 5, static_cast<int>(commit.result.kind));
            sqlite3_bind_text(request, 6, source.cellId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(request, 7, source.refId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(request, 8, source.refNum);
            sqlite3_bind_int64(request, 9, source.mpNum);
            sqlite3_bind_int64(request, 10, static_cast<sqlite3_int64>(source.actorInstanceId));
            sqlite3_bind_int64(request, 11, source.migrationGeneration);
            sqlite3_bind_text(request, 12, commit.result.itemRefId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(request, 13, commit.result.itemCharge);
            sqlite3_bind_int(request, 14, commit.result.itemCount);
            sqlite3_bind_int64(request, 15, static_cast<sqlite3_int64>(commit.result.inventoryRevision));
            sqlite3_bind_int(request, 16, commit.result.detected ? 1 : 0);
            sqlite3_bind_int(request, 17, commit.result.detectionRoll);
            sqlite3_bind_int(request, 18, commit.result.theft ? 1 : 0);
            sqlite3_bind_int64(request, 19, commit.result.crimeValue);
            sqlite3_bind_int64(request, 20, static_cast<sqlite3_int64>(std::time(nullptr)));
            checkSqlite(sqlite3_step(request), mDb, "commitInventoryTake(insertRequest)");
            sqlite3_finalize(request);
            exec("COMMIT");
            return { InventoryTakeCommitStatus::Committed, commit.result };
        }
        catch (...)
        {
            try { exec("ROLLBACK"); } catch (...) {}
            throw;
        }
    }

    std::vector<PlacedObjectIdentity> PlayerDatabase::loadTakenWorldItemReferences()
    {
        sqlite3_stmt* statement = prepare(
            "SELECT object_kind, cell_id, ref_id, ref_index, ref_content_file, mp_num"
            " FROM world_taken_references ORDER BY cell_id, ref_id, ref_index, ref_content_file, mp_num");
        std::vector<PlacedObjectIdentity> result;
        while (sqlite3_step(statement) == SQLITE_ROW)
        {
            auto textColumn = [&](int column) {
                const char* value = reinterpret_cast<const char*>(sqlite3_column_text(statement, column));
                return std::string(value ? value : "");
            };
            PlacedObjectIdentity identity;
            identity.kind = static_cast<PlacedObjectKind>(sqlite3_column_int(statement, 0));
            identity.cellId = textColumn(1);
            identity.refId = textColumn(2);
            identity.refIndex = static_cast<std::uint32_t>(sqlite3_column_int64(statement, 3));
            identity.refContentFile = sqlite3_column_int(statement, 4);
            identity.mpNum = static_cast<std::uint32_t>(sqlite3_column_int64(statement, 5));
            if (isCanonicalPlacedObjectIdentity(identity))
                result.push_back(std::move(identity));
        }
        sqlite3_finalize(statement);
        return result;
    }

    std::uint64_t PlayerDatabase::createCombatEvent(const CombatEventRecord& event)
    {
        if (event.accountId <= 0 || event.characterId <= 0 || event.attackerGuid == 0
            || event.victimActorInstanceId == 0 || event.victimRefId.empty()
            || event.cellId.empty() || event.migrationGeneration == 0 || event.authorityGeneration == 0
            || event.actorAuthorityGuid == 0 || !std::isfinite(event.proposedDamage)
            || event.proposedDamage <= 0.f || event.proposalHash.size() != 64 || event.createdAtMs == 0)
            throw std::invalid_argument("invalid combat event proposal");

        sqlite3_stmt* statement = prepare(
            "INSERT INTO combat_events(account_id, character_id, attacker_guid, victim_actor_id, victim_ref_id,"
            " cell_id, migration_generation, authority_generation, actor_authority_guid, proposed_damage,"
            " proposed_health_damage, proposal_hash, created_at_ms)"
            " VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13)");
        sqlite3_bind_int64(statement, 1, event.accountId);
        sqlite3_bind_int64(statement, 2, event.characterId);
        sqlite3_bind_int64(statement, 3, event.attackerGuid);
        sqlite3_bind_int64(statement, 4, static_cast<sqlite3_int64>(event.victimActorInstanceId));
        sqlite3_bind_text(statement, 5, event.victimRefId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 6, event.cellId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(statement, 7, event.migrationGeneration);
        sqlite3_bind_int64(statement, 8, event.authorityGeneration);
        sqlite3_bind_int64(statement, 9, event.actorAuthorityGuid);
        sqlite3_bind_double(statement, 10, event.proposedDamage);
        sqlite3_bind_int(statement, 11, event.proposedHealthDamage ? 1 : 0);
        sqlite3_bind_text(statement, 12, event.proposalHash.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(statement, 13, static_cast<sqlite3_int64>(event.createdAtMs));
        checkSqlite(sqlite3_step(statement), mDb, "createCombatEvent");
        sqlite3_finalize(statement);
        return static_cast<std::uint64_t>(sqlite3_last_insert_rowid(mDb));
    }

    std::optional<CombatEventRecord> PlayerDatabase::loadCombatEvent(std::uint64_t eventId)
    {
        sqlite3_stmt* statement = prepare(
            "SELECT account_id, character_id, attacker_guid, victim_actor_id, victim_ref_id, cell_id,"
            " migration_generation, authority_generation, actor_authority_guid, proposed_damage,"
            " proposed_health_damage, proposal_hash, created_at_ms, status, result_sequence, result_flags,"
            " applied_damage, qualifying_crime, assault_reported FROM combat_events WHERE event_id=?1");
        sqlite3_bind_int64(statement, 1, static_cast<sqlite3_int64>(eventId));
        if (sqlite3_step(statement) != SQLITE_ROW)
        {
            sqlite3_finalize(statement);
            return std::nullopt;
        }
        auto text = [&](int column) {
            const char* value = reinterpret_cast<const char*>(sqlite3_column_text(statement, column));
            return std::string(value ? value : "");
        };
        CombatEventRecord event;
        event.eventId = eventId;
        event.accountId = sqlite3_column_int64(statement, 0);
        event.characterId = sqlite3_column_int64(statement, 1);
        event.attackerGuid = static_cast<std::uint32_t>(sqlite3_column_int64(statement, 2));
        event.victimActorInstanceId = static_cast<std::uint64_t>(sqlite3_column_int64(statement, 3));
        event.victimRefId = text(4);
        event.cellId = text(5);
        event.migrationGeneration = static_cast<std::uint32_t>(sqlite3_column_int64(statement, 6));
        event.authorityGeneration = static_cast<std::uint32_t>(sqlite3_column_int64(statement, 7));
        event.actorAuthorityGuid = static_cast<std::uint32_t>(sqlite3_column_int64(statement, 8));
        event.proposedDamage = static_cast<float>(sqlite3_column_double(statement, 9));
        event.proposedHealthDamage = sqlite3_column_int(statement, 10) != 0;
        event.proposalHash = text(11);
        event.createdAtMs = static_cast<std::uint64_t>(sqlite3_column_int64(statement, 12));
        event.accepted = sqlite3_column_int(statement, 13) == 1;
        event.resultSequence = static_cast<std::uint32_t>(sqlite3_column_int64(statement, 14));
        event.resultFlags = static_cast<std::uint8_t>(sqlite3_column_int(statement, 15));
        event.appliedDamage = static_cast<float>(sqlite3_column_double(statement, 16));
        event.qualifyingCrime = sqlite3_column_int(statement, 17) != 0;
        event.assaultReported = sqlite3_column_int(statement, 18) != 0;
        sqlite3_finalize(statement);
        return event;
    }

    CombatEventCommitStatus PlayerDatabase::acceptCombatEvent(std::uint64_t eventId,
        std::uint32_t resultSequence, std::uint8_t resultFlags, float appliedDamage,
        bool qualifyingCrime, const std::vector<CrimeMutationCommit>& crimeMutations,
        bool assaultReported)
    {
        exec("BEGIN IMMEDIATE");
        try
        {
            const std::optional<CombatEventRecord> current = loadCombatEvent(eventId);
            if (!current)
            {
                exec("COMMIT");
                return CombatEventCommitStatus::UnknownEvent;
            }
            if (current->accepted)
            {
                const bool identical = current->resultSequence == resultSequence
                    && current->resultFlags == resultFlags && current->appliedDamage == appliedDamage
                    && current->qualifyingCrime == qualifyingCrime;
                exec("COMMIT");
                return identical ? CombatEventCommitStatus::IdenticalReplay
                                 : CombatEventCommitStatus::ConflictingReplay;
            }

            sqlite3_stmt* statement = prepare(
                "UPDATE combat_events SET status=1, result_sequence=?1, result_flags=?2, applied_damage=?3,"
                " qualifying_crime=?4, assault_reported=?5 WHERE event_id=?6 AND status=0");
            sqlite3_bind_int64(statement, 1, resultSequence);
            sqlite3_bind_int(statement, 2, resultFlags);
            sqlite3_bind_double(statement, 3, appliedDamage);
            sqlite3_bind_int(statement, 4, qualifyingCrime ? 1 : 0);
            sqlite3_bind_int(statement, 5, assaultReported ? 1 : 0);
            sqlite3_bind_int64(statement, 6, static_cast<sqlite3_int64>(eventId));
            checkSqlite(sqlite3_step(statement), mDb, "acceptCombatEvent");
            const bool updated = sqlite3_changes(mDb) == 1;
            sqlite3_finalize(statement);
            if (!updated)
            {
                exec("ROLLBACK");
                return CombatEventCommitStatus::ConflictingReplay;
            }

            for (const CrimeMutationCommit& crimeMutation : crimeMutations)
            {
                const CrimeCommitResult crime = commitPlayerCrimeMutationInTransaction(crimeMutation);
                if (crime.status == CrimeCommitStatus::StaleRevision)
                {
                    exec("ROLLBACK");
                    return CombatEventCommitStatus::StaleCrimeRevision;
                }
                if (crime.status == CrimeCommitStatus::DuplicateRequestConflict)
                {
                    exec("ROLLBACK");
                    return CombatEventCommitStatus::CrimeDuplicateConflict;
                }
            }

            exec("COMMIT");
            return CombatEventCommitStatus::Committed;
        }
        catch (...)
        {
            try { exec("ROLLBACK"); } catch (...) {}
            throw;
        }
    }

    void PlayerDatabase::markCombatAssaultReported(std::uint64_t eventId, bool reported)
    {
        sqlite3_stmt* statement = prepare(
            "UPDATE combat_events SET assault_reported=?1 WHERE event_id=?2 AND status=1");
        sqlite3_bind_int(statement, 1, reported ? 1 : 0);
        sqlite3_bind_int64(statement, 2, static_cast<sqlite3_int64>(eventId));
        checkSqlite(sqlite3_step(statement), mDb, "markCombatAssaultReported");
        sqlite3_finalize(statement);
    }

    bool PlayerDatabase::hasReportedCriminalAssault(std::int64_t characterId,
        std::uint64_t victimActorInstanceId, std::uint32_t migrationGeneration)
    {
        sqlite3_stmt* statement = prepare(
            "SELECT 1 FROM combat_events WHERE character_id=?1 AND victim_actor_id=?2"
            " AND migration_generation=?3 AND status=1 AND qualifying_crime=1 AND assault_reported=1 LIMIT 1");
        sqlite3_bind_int64(statement, 1, characterId);
        sqlite3_bind_int64(statement, 2, static_cast<sqlite3_int64>(victimActorInstanceId));
        sqlite3_bind_int64(statement, 3, migrationGeneration);
        const bool found = sqlite3_step(statement) == SQLITE_ROW;
        sqlite3_finalize(statement);
        return found;
    }

    WerewolfStateTransition PlayerDatabase::loadWerewolfState(std::int64_t characterId)
    {
        if (characterId <= 0)
            throw std::invalid_argument("Werewolf state requires a character");
        WerewolfStateTransition result;
        sqlite3_stmt* select = prepare(
            "SELECT is_werewolf, transition_counter FROM character_werewolf_state WHERE character_id=?1");
        sqlite3_bind_int64(select, 1, characterId);
        if (sqlite3_step(select) == SQLITE_ROW)
        {
            result.isWerewolf = sqlite3_column_int(select, 0) != 0;
            const sqlite3_int64 counter = sqlite3_column_int64(select, 1);
            if (counter < 0)
            {
                sqlite3_finalize(select);
                throw std::runtime_error("Corrupt werewolf transition counter");
            }
            result.transition = static_cast<std::uint64_t>(counter);
        }
        sqlite3_finalize(select);
        return result;
    }

    WerewolfStateTransition PlayerDatabase::updateWerewolfState(
        std::int64_t characterId, bool isWerewolf, const std::optional<CrimeMutationCommit>& crimeMutation)
    {
        if (characterId <= 0)
            throw std::invalid_argument("Werewolf state requires a character");

        exec("BEGIN IMMEDIATE");
        try
        {
            WerewolfStateTransition result;
            result.isWerewolf = isWerewolf;
            bool exists = false;
            bool previous = false;
            sqlite3_stmt* select = prepare(
                "SELECT is_werewolf, transition_counter FROM character_werewolf_state WHERE character_id=?1");
            sqlite3_bind_int64(select, 1, characterId);
            if (sqlite3_step(select) == SQLITE_ROW)
            {
                exists = true;
                previous = sqlite3_column_int(select, 0) != 0;
                const sqlite3_int64 counter = sqlite3_column_int64(select, 1);
                if (counter < 0)
                {
                    sqlite3_finalize(select);
                    throw std::runtime_error("Corrupt werewolf transition counter");
                }
                result.transition = static_cast<std::uint64_t>(counter);
            }
            sqlite3_finalize(select);

            result.changed = !exists ? isWerewolf : previous != isWerewolf;
            result.transformed = result.changed && isWerewolf;
            if (result.changed)
            {
                if (result.transition >= MaximumPersistedRevision)
                    throw std::overflow_error("Werewolf transition counter overflow");
                ++result.transition;
            }

            sqlite3_stmt* upsert = prepare(
                "INSERT INTO character_werewolf_state(character_id, is_werewolf, transition_counter, updated_at)"
                " VALUES(?1, ?2, ?3, ?4)"
                " ON CONFLICT(character_id) DO UPDATE SET is_werewolf=excluded.is_werewolf,"
                " transition_counter=excluded.transition_counter, updated_at=excluded.updated_at");
            sqlite3_bind_int64(upsert, 1, characterId);
            sqlite3_bind_int(upsert, 2, isWerewolf ? 1 : 0);
            sqlite3_bind_int64(upsert, 3, static_cast<sqlite3_int64>(result.transition));
            sqlite3_bind_int64(upsert, 4, static_cast<sqlite3_int64>(std::time(nullptr)));
            checkSqlite(sqlite3_step(upsert), mDb, "updateWerewolfState");
            sqlite3_finalize(upsert);

            if (crimeMutation)
            {
                if (!result.transformed)
                    throw std::invalid_argument("Werewolf crime mutation requires a transformation edge");
                const CrimeCommitResult crime = commitPlayerCrimeMutationInTransaction(*crimeMutation);
                if (crime.status == CrimeCommitStatus::StaleRevision)
                    throw std::runtime_error("Werewolf crime revision changed during atomic transition");
                if (crime.status == CrimeCommitStatus::DuplicateRequestConflict)
                    throw std::runtime_error("Werewolf crime request conflicts with stored semantic result");
            }

            exec("COMMIT");
            return result;
        }
        catch (...)
        {
            try { exec("ROLLBACK"); } catch (...) {}
            throw;
        }
    }

    std::vector<EquipmentItem> PlayerDatabase::loadCharacterEquipment(int64_t characterId)
    {
        sqlite3_stmt* s = prepare(
            "SELECT slot, ref_id, item_count, charge, enchantment_charge, soul, instance_id"
            " FROM character_equipment WHERE character_id=?1 ORDER BY slot");
        sqlite3_bind_int64(s, 1, characterId);

        std::vector<EquipmentItem> equipment;
        while (sqlite3_step(s) == SQLITE_ROW)
        {
            EquipmentItem entry;
            auto col = [&](int i) -> std::string {
                const char* t = reinterpret_cast<const char*>(sqlite3_column_text(s, i));
                return t ? t : "";
            };

            entry.slot = sqlite3_column_int(s, 0);
            entry.item.refId = col(1);
            entry.item.count = sqlite3_column_int(s, 2);
            entry.item.charge = sqlite3_column_int(s, 3);
            entry.item.enchantmentCharge = static_cast<float>(sqlite3_column_double(s, 4));
            entry.item.soul = col(5);
            entry.item.instanceId = static_cast<uint32_t>(sqlite3_column_int64(s, 6));
            equipment.push_back(std::move(entry));
        }

        sqlite3_finalize(s);
        return equipment;
    }

    void PlayerDatabase::saveCharacterEquipment(
        int64_t characterId, const std::vector<EquipmentItem>& equipment, bool touchLastSeen)
    {
        exec("BEGIN");
        try
        {
            const std::string characterKey = std::to_string(characterId);
            sqlite3_stmt* clear = prepare("DELETE FROM character_equipment WHERE character_id=?1");
            sqlite3_bind_int64(clear, 1, characterId);
            checkSqlite(sqlite3_step(clear), mDb, "clearCharacterEquipment");
            sqlite3_finalize(clear);

            sqlite3_stmt* insert = prepare(
                "INSERT INTO character_equipment(character_id, slot, ref_id, item_count, charge, enchantment_charge, "
                "soul, instance_id)"
                " VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)");

            for (const EquipmentItem& entry : equipment)
            {
                if (entry.item.refId.empty())
                    continue;

                sqlite3_bind_int64(insert, 1, characterId);
                sqlite3_bind_int(insert, 2, entry.slot);
                sqlite3_bind_text(
                    insert, 3, entry.item.refId.c_str(), static_cast<int>(entry.item.refId.size()), SQLITE_TRANSIENT);
                sqlite3_bind_int(insert, 4, entry.item.count);
                sqlite3_bind_int(insert, 5, entry.item.charge);
                sqlite3_bind_double(insert, 6, entry.item.enchantmentCharge);
                sqlite3_bind_text(
                    insert, 7, entry.item.soul.c_str(), static_cast<int>(entry.item.soul.size()), SQLITE_TRANSIENT);
                sqlite3_bind_int64(insert, 8, entry.item.instanceId);
                checkSqlite(sqlite3_step(insert), mDb, "insertCharacterEquipment");
                sqlite3_reset(insert);
                sqlite3_clear_bindings(insert);
            }
            sqlite3_finalize(insert);

            sqlite3_stmt* clearLinks = prepare(
                "DELETE FROM world_dynamic_record_links"
                " WHERE link_kind=?1 AND owner_a=?2 AND owner_b=?3 AND owner_c=?4");
            clearDynamicRecordLinksForOwner(mDb, clearLinks, "equipment_item", characterKey, "", "");
            sqlite3_finalize(clearLinks);

            sqlite3_stmt* insertLink = prepare(
                "INSERT OR REPLACE INTO world_dynamic_record_links(record_id, link_kind, owner_a, owner_b, owner_c, "
                "owner_index)"
                " VALUES(?1, ?2, ?3, ?4, ?5, ?6)");
            for (const EquipmentItem& entry : equipment)
            {
                if (entry.item.refId.empty())
                    continue;
                insertDynamicRecordLink(
                    mDb, insertLink, entry.item.refId, "equipment_item", characterKey, "", "",
                    entry.item.instanceId != 0 ? entry.item.instanceId : static_cast<uint32_t>(entry.slot));
            }
            sqlite3_finalize(insertLink);

            sqlite3_stmt* mark
                = prepare(touchLastSeen ? "UPDATE characters SET equipment_saved=1, last_seen=?1 WHERE id=?2"
                                        : "UPDATE characters SET equipment_saved=1 WHERE id=?1");
            if (touchLastSeen)
            {
                sqlite3_bind_int64(mark, 1, static_cast<int64_t>(std::time(nullptr)));
                sqlite3_bind_int64(mark, 2, characterId);
            }
            else
            {
                sqlite3_bind_int64(mark, 1, characterId);
            }
            checkSqlite(sqlite3_step(mark), mDb, "markCharacterEquipmentSaved");
            sqlite3_finalize(mark);

            exec("COMMIT");
        }
        catch (...)
        {
            try
            {
                exec("ROLLBACK");
            }
            catch (...)
            {
            }
            throw;
        }
    }

    std::vector<std::string> PlayerDatabase::loadCharacterSpellbook(int64_t characterId)
    {
        sqlite3_stmt* s = prepare(
            "SELECT spell_id FROM character_spellbook WHERE character_id=?1 ORDER BY spell_id");
        sqlite3_bind_int64(s, 1, characterId);

        std::vector<std::string> spellIds;
        while (sqlite3_step(s) == SQLITE_ROW)
        {
            const char* text = reinterpret_cast<const char*>(sqlite3_column_text(s, 0));
            if (text)
                spellIds.emplace_back(text);
        }

        sqlite3_finalize(s);
        return spellIds;
    }

    uint64_t PlayerDatabase::loadSpellbookRevision(int64_t characterId)
    {
        sqlite3_stmt* s = prepare("SELECT spellbook_revision FROM characters WHERE id=?1");
        sqlite3_bind_int64(s, 1, characterId);
        const int rc = sqlite3_step(s);
        const uint64_t revision
            = rc == SQLITE_ROW ? static_cast<uint64_t>(sqlite3_column_int64(s, 0)) : 0;
        sqlite3_finalize(s);
        return revision;
    }

    void PlayerDatabase::saveCharacterSpellbook(int64_t characterId,
        const std::vector<std::string>& spellIds, bool touchLastSeen,
        std::optional<uint64_t> spellbookRevision)
    {
        const std::vector<std::string> canonical = mwmp::canonicalizeSpellIds(spellIds);

        exec("BEGIN");
        try
        {
            sqlite3_stmt* clear = prepare("DELETE FROM character_spellbook WHERE character_id=?1");
            sqlite3_bind_int64(clear, 1, characterId);
            checkSqlite(sqlite3_step(clear), mDb, "clearCharacterSpellbook");
            sqlite3_finalize(clear);

            sqlite3_stmt* insert = prepare(
                "INSERT OR IGNORE INTO character_spellbook(character_id, spell_id) VALUES(?1, ?2)");
            for (const std::string& spellId : canonical)
            {
                if (spellId.empty())
                    continue;
                sqlite3_bind_int64(insert, 1, characterId);
                sqlite3_bind_text(insert, 2, spellId.c_str(), static_cast<int>(spellId.size()), SQLITE_TRANSIENT);
                checkSqlite(sqlite3_step(insert), mDb, "insertCharacterSpellbook");
                sqlite3_reset(insert);
                sqlite3_clear_bindings(insert);
            }
            sqlite3_finalize(insert);

            // Rebuild dynamic-record GC links so a learned dynamic spell stays
            // alive while the character knows it. Links are keyed by character
            // (owner_a) and rebuilt wholesale on every persisted mutation.
            const std::string characterKey = std::to_string(characterId);
            sqlite3_stmt* clearLinks = prepare(
                "DELETE FROM world_dynamic_record_links"
                " WHERE link_kind=?1 AND owner_a=?2 AND owner_b=?3 AND owner_c=?4");
            clearDynamicRecordLinksForOwner(mDb, clearLinks, "spellbook_spell", characterKey, "", "");
            sqlite3_finalize(clearLinks);

            sqlite3_stmt* insertLink = prepare(
                "INSERT OR REPLACE INTO world_dynamic_record_links(record_id, link_kind, owner_a, owner_b, owner_c, "
                "owner_index)"
                " VALUES(?1, ?2, ?3, ?4, ?5, ?6)");
            for (std::size_t i = 0; i < canonical.size(); ++i)
                insertDynamicRecordLink(
                    mDb, insertLink, canonical[i], "spellbook_spell", characterKey, "", "",
                    static_cast<int64_t>(i));
            sqlite3_finalize(insertLink);

            sqlite3_stmt* mark = prepare(
                touchLastSeen
                    ? (spellbookRevision
                        ? "UPDATE characters SET spellbook_saved=1, spellbook_revision=?1, last_seen=?2 WHERE id=?3"
                        : "UPDATE characters SET spellbook_saved=1, last_seen=?1 WHERE id=?2")
                    : (spellbookRevision
                        ? "UPDATE characters SET spellbook_saved=1, spellbook_revision=?1 WHERE id=?2"
                        : "UPDATE characters SET spellbook_saved=1 WHERE id=?1"));
            if (touchLastSeen)
            {
                if (spellbookRevision)
                {
                    sqlite3_bind_int64(mark, 1, static_cast<sqlite3_int64>(*spellbookRevision));
                    sqlite3_bind_int64(mark, 2, static_cast<int64_t>(std::time(nullptr)));
                    sqlite3_bind_int64(mark, 3, characterId);
                }
                else
                {
                    sqlite3_bind_int64(mark, 1, static_cast<int64_t>(std::time(nullptr)));
                    sqlite3_bind_int64(mark, 2, characterId);
                }
            }
            else
            {
                if (spellbookRevision)
                {
                    sqlite3_bind_int64(mark, 1, static_cast<sqlite3_int64>(*spellbookRevision));
                    sqlite3_bind_int64(mark, 2, characterId);
                }
                else
                    sqlite3_bind_int64(mark, 1, characterId);
            }
            checkSqlite(sqlite3_step(mark), mDb, "markCharacterSpellbookSaved");
            sqlite3_finalize(mark);

            exec("COMMIT");
        }
        catch (...)
        {
            try
            {
                exec("ROLLBACK");
            }
            catch (...)
            {
            }
            throw;
        }
    }

    bool PlayerDatabase::loadCharacterStats(int64_t characterId, BasePlayer& player)
    {
        sqlite3_stmt* meta = prepare("SELECT stats_saved, level, level_progress FROM characters WHERE id=?1 LIMIT 1");
        sqlite3_bind_int64(meta, 1, characterId);
        const int metaRc = sqlite3_step(meta);
        if (metaRc != SQLITE_ROW || sqlite3_column_int(meta, 0) == 0)
        {
            sqlite3_finalize(meta);
            return false;
        }
        player.level = sqlite3_column_int(meta, 1);
        player.levelProgress = static_cast<float>(sqlite3_column_double(meta, 2));
        sqlite3_finalize(meta);

        sqlite3_stmt* dyn = prepare(
            "SELECT health_base, health_current, health_mod, magicka_base, magicka_current, magicka_mod,"
            " fatigue_base, fatigue_current, fatigue_mod"
            " FROM character_dynamic_stats WHERE character_id=?1 LIMIT 1");
        sqlite3_bind_int64(dyn, 1, characterId);
        if (sqlite3_step(dyn) == SQLITE_ROW)
        {
            player.dynamicStats.health.base = static_cast<float>(sqlite3_column_double(dyn, 0));
            player.dynamicStats.health.current = static_cast<float>(sqlite3_column_double(dyn, 1));
            player.dynamicStats.health.mod = static_cast<float>(sqlite3_column_double(dyn, 2));
            player.dynamicStats.magicka.base = static_cast<float>(sqlite3_column_double(dyn, 3));
            player.dynamicStats.magicka.current = static_cast<float>(sqlite3_column_double(dyn, 4));
            player.dynamicStats.magicka.mod = static_cast<float>(sqlite3_column_double(dyn, 5));
            player.dynamicStats.fatigue.base = static_cast<float>(sqlite3_column_double(dyn, 6));
            player.dynamicStats.fatigue.current = static_cast<float>(sqlite3_column_double(dyn, 7));
            player.dynamicStats.fatigue.mod = static_cast<float>(sqlite3_column_double(dyn, 8));
        }
        sqlite3_finalize(dyn);

        sqlite3_stmt* attrs = prepare(
            "SELECT attribute_index, base, mod, damage"
            " FROM character_attributes WHERE character_id=?1 ORDER BY attribute_index");
        sqlite3_bind_int64(attrs, 1, characterId);
        while (sqlite3_step(attrs) == SQLITE_ROW)
        {
            const int index = sqlite3_column_int(attrs, 0);
            if (index < 0 || index >= BasePlayer::NUM_ATTRIBUTES)
                continue;
            Attribute& attribute = player.attributes[static_cast<std::size_t>(index)];
            attribute.base = sqlite3_column_int(attrs, 1);
            attribute.mod = static_cast<float>(sqlite3_column_double(attrs, 2));
            attribute.damage = static_cast<float>(sqlite3_column_double(attrs, 3));
        }
        sqlite3_finalize(attrs);

        sqlite3_stmt* skills = prepare(
            "SELECT skill_index, base, mod, damage, progress, increases"
            " FROM character_skills WHERE character_id=?1 ORDER BY skill_index");
        sqlite3_bind_int64(skills, 1, characterId);
        while (sqlite3_step(skills) == SQLITE_ROW)
        {
            const int index = sqlite3_column_int(skills, 0);
            if (index < 0 || index >= BasePlayer::NUM_SKILLS)
                continue;
            Skill& skill = player.skills[static_cast<std::size_t>(index)];
            skill.base = static_cast<float>(sqlite3_column_double(skills, 1));
            skill.mod = static_cast<float>(sqlite3_column_double(skills, 2));
            skill.damage = static_cast<float>(sqlite3_column_double(skills, 3));
            skill.progress = static_cast<float>(sqlite3_column_double(skills, 4));
            skill.increases = sqlite3_column_int(skills, 5);
        }
        sqlite3_finalize(skills);

        player.hasSavedStats = true;
        return true;
    }

    void PlayerDatabase::writeCharacterStatsRows(int64_t characterId, const BasePlayer& player)
    {
        sqlite3_stmt* dyn = prepare(
            "INSERT INTO character_dynamic_stats(character_id, health_base, health_current, health_mod,"
            " magicka_base, magicka_current, magicka_mod, fatigue_base, fatigue_current, fatigue_mod)"
            " VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)"
            " ON CONFLICT(character_id) DO UPDATE SET"
            " health_base=excluded.health_base,"
            " health_current=excluded.health_current,"
            " health_mod=excluded.health_mod,"
            " magicka_base=excluded.magicka_base,"
            " magicka_current=excluded.magicka_current,"
            " magicka_mod=excluded.magicka_mod,"
            " fatigue_base=excluded.fatigue_base,"
            " fatigue_current=excluded.fatigue_current,"
            " fatigue_mod=excluded.fatigue_mod");
        sqlite3_bind_int64(dyn, 1, characterId);
        sqlite3_bind_double(dyn, 2, player.dynamicStats.health.base);
        sqlite3_bind_double(dyn, 3, player.dynamicStats.health.current);
        sqlite3_bind_double(dyn, 4, player.dynamicStats.health.mod);
        sqlite3_bind_double(dyn, 5, player.dynamicStats.magicka.base);
        sqlite3_bind_double(dyn, 6, player.dynamicStats.magicka.current);
        sqlite3_bind_double(dyn, 7, player.dynamicStats.magicka.mod);
        sqlite3_bind_double(dyn, 8, player.dynamicStats.fatigue.base);
        sqlite3_bind_double(dyn, 9, player.dynamicStats.fatigue.current);
        sqlite3_bind_double(dyn, 10, player.dynamicStats.fatigue.mod);
        checkSqlite(sqlite3_step(dyn), mDb, "upsertCharacterDynamicStats");
        sqlite3_finalize(dyn);

        sqlite3_stmt* clearAttrs = prepare("DELETE FROM character_attributes WHERE character_id=?1");
        sqlite3_bind_int64(clearAttrs, 1, characterId);
        checkSqlite(sqlite3_step(clearAttrs), mDb, "clearCharacterAttributes");
        sqlite3_finalize(clearAttrs);

        sqlite3_stmt* insertAttr = prepare(
            "INSERT INTO character_attributes(character_id, attribute_index, base, mod, damage)"
            " VALUES(?1, ?2, ?3, ?4, ?5)");
        for (std::size_t i = 0; i < player.attributes.size(); ++i)
        {
            const Attribute& attribute = player.attributes[i];
            sqlite3_bind_int64(insertAttr, 1, characterId);
            sqlite3_bind_int(insertAttr, 2, static_cast<int>(i));
            sqlite3_bind_int(insertAttr, 3, attribute.base);
            sqlite3_bind_double(insertAttr, 4, attribute.mod);
            sqlite3_bind_double(insertAttr, 5, attribute.damage);
            checkSqlite(sqlite3_step(insertAttr), mDb, "insertCharacterAttribute");
            sqlite3_reset(insertAttr);
            sqlite3_clear_bindings(insertAttr);
        }
        sqlite3_finalize(insertAttr);

        sqlite3_stmt* clearSkills = prepare("DELETE FROM character_skills WHERE character_id=?1");
        sqlite3_bind_int64(clearSkills, 1, characterId);
        checkSqlite(sqlite3_step(clearSkills), mDb, "clearCharacterSkills");
        sqlite3_finalize(clearSkills);

        sqlite3_stmt* insertSkill = prepare(
            "INSERT INTO character_skills(character_id, skill_index, base, mod, damage, progress, increases)"
            " VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7)");
        for (std::size_t i = 0; i < player.skills.size(); ++i)
        {
            const Skill& skill = player.skills[i];
            sqlite3_bind_int64(insertSkill, 1, characterId);
            sqlite3_bind_int(insertSkill, 2, static_cast<int>(i));
            sqlite3_bind_double(insertSkill, 3, skill.base);
            sqlite3_bind_double(insertSkill, 4, skill.mod);
            sqlite3_bind_double(insertSkill, 5, skill.damage);
            sqlite3_bind_double(insertSkill, 6, skill.progress);
            sqlite3_bind_int(insertSkill, 7, skill.increases);
            checkSqlite(sqlite3_step(insertSkill), mDb, "insertCharacterSkill");
            sqlite3_reset(insertSkill);
            sqlite3_clear_bindings(insertSkill);
        }
        sqlite3_finalize(insertSkill);

        sqlite3_stmt* mark = prepare(
            "UPDATE characters SET stats_saved=1, level=?1, level_progress=?2 WHERE id=?3");
        sqlite3_bind_int(mark, 1, player.level);
        sqlite3_bind_double(mark, 2, player.levelProgress);
        sqlite3_bind_int64(mark, 3, characterId);
        checkSqlite(sqlite3_step(mark), mDb, "markCharacterStatsSaved");
        sqlite3_finalize(mark);
    }

    void PlayerDatabase::saveCharacterStats(int64_t characterId, const BasePlayer& player, bool touchLastSeen)
    {
        exec("BEGIN");
        try
        {
            writeCharacterStatsRows(characterId, player);

            if (touchLastSeen)
            {
                sqlite3_stmt* mark = prepare("UPDATE characters SET last_seen=?1 WHERE id=?2");
                sqlite3_bind_int64(mark, 1, static_cast<int64_t>(std::time(nullptr)));
                sqlite3_bind_int64(mark, 2, characterId);
                checkSqlite(sqlite3_step(mark), mDb, "markCharacterStatsLastSeen");
                sqlite3_finalize(mark);
            }

            exec("COMMIT");
        }
        catch (...)
        {
            try
            {
                exec("ROLLBACK");
            }
            catch (...)
            {
            }
            throw;
        }
    }

    void PlayerDatabase::saveCharacterJournalChanges(
        int64_t characterId, const std::vector<BasePlayer::JournalItem>& changes)
    {
        if (characterId <= 0 || changes.empty())
            return;

        exec("BEGIN");
        try
        {
            sqlite3_stmt* insertEntry = prepare(
                "INSERT OR IGNORE INTO character_journal_entries(character_id, quest_id, info_id, journal_index,"
                " entry_text, actor_name, has_timestamp, days_passed, month, day_of_month, changed_at)"
                " VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11)");
            sqlite3_stmt* upsertQuest = prepare(
                "INSERT INTO character_journal_quests(character_id, quest_id, journal_index, changed_at)"
                " VALUES(?1, ?2, ?3, ?4)"
                " ON CONFLICT(character_id, quest_id) DO UPDATE SET"
                " journal_index=excluded.journal_index, changed_at=excluded.changed_at");

            int64_t changedAt = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            for (const BasePlayer::JournalItem& item : changes)
            {
                if (item.quest.empty())
                    continue;

                if (item.type == BasePlayer::JournalItem::Type::Entry && !item.infoId.empty())
                {
                    sqlite3_bind_int64(insertEntry, 1, characterId);
                    sqlite3_bind_text(insertEntry, 2, item.quest.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(insertEntry, 3, item.infoId.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int(insertEntry, 4, item.index);
                    sqlite3_bind_text(insertEntry, 5, item.text.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(insertEntry, 6, item.actorName.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int(insertEntry, 7, item.hasTimestamp ? 1 : 0);
                    sqlite3_bind_int(insertEntry, 8, item.daysPassed);
                    sqlite3_bind_int(insertEntry, 9, item.month);
                    sqlite3_bind_int(insertEntry, 10, item.dayOfMonth);
                    sqlite3_bind_int64(insertEntry, 11, changedAt++);
                    checkSqlite(sqlite3_step(insertEntry), mDb, "insertCharacterJournalEntry");
                    sqlite3_reset(insertEntry);
                    sqlite3_clear_bindings(insertEntry);
                }

                sqlite3_bind_int64(upsertQuest, 1, characterId);
                sqlite3_bind_text(upsertQuest, 2, item.quest.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(upsertQuest, 3, item.index);
                sqlite3_bind_int64(upsertQuest, 4, changedAt++);
                checkSqlite(sqlite3_step(upsertQuest), mDb, "upsertCharacterJournalQuest");
                sqlite3_reset(upsertQuest);
                sqlite3_clear_bindings(upsertQuest);
            }

            sqlite3_finalize(insertEntry);
            sqlite3_finalize(upsertQuest);
            exec("COMMIT");
        }
        catch (...)
        {
            try
            {
                exec("ROLLBACK");
            }
            catch (...)
            {
            }
            throw;
        }
    }

    std::vector<BasePlayer::JournalItem> PlayerDatabase::loadCharacterJournals(
        const std::vector<int64_t>& characterIds)
    {
        if (characterIds.empty())
            return {};

        std::string placeholders;
        for (std::size_t i = 0; i < characterIds.size(); ++i)
        {
            if (i != 0)
                placeholders += ',';
            placeholders += '?' + std::to_string(i + 1);
        }

        std::vector<BasePlayer::JournalItem> result;
        std::unordered_set<std::string> seenEntries;
        const std::string entrySql =
            "SELECT quest_id, info_id, journal_index, entry_text, actor_name, has_timestamp, days_passed, month,"
            " day_of_month FROM character_journal_entries WHERE character_id IN (" + placeholders
            + ") ORDER BY changed_at, id";
        sqlite3_stmt* entries = prepare(entrySql.c_str());
        for (std::size_t i = 0; i < characterIds.size(); ++i)
            sqlite3_bind_int64(entries, static_cast<int>(i + 1), characterIds[i]);
        while (sqlite3_step(entries) == SQLITE_ROW)
        {
            auto textCol = [&](int index) {
                const char* value = reinterpret_cast<const char*>(sqlite3_column_text(entries, index));
                return value ? std::string(value) : std::string();
            };

            BasePlayer::JournalItem item;
            item.type = BasePlayer::JournalItem::Type::Entry;
            item.quest = textCol(0);
            item.infoId = textCol(1);
            const std::string key = item.quest + '\x1f' + item.infoId;
            if (!seenEntries.insert(key).second)
                continue;
            item.index = sqlite3_column_int(entries, 2);
            item.text = textCol(3);
            item.actorName = textCol(4);
            item.hasTimestamp = sqlite3_column_int(entries, 5) != 0;
            item.daysPassed = sqlite3_column_int(entries, 6);
            item.month = sqlite3_column_int(entries, 7);
            item.dayOfMonth = sqlite3_column_int(entries, 8);
            result.push_back(std::move(item));
        }
        sqlite3_finalize(entries);

        struct LatestIndex
        {
            int index = 0;
            int64_t changedAt = std::numeric_limits<int64_t>::min();
        };
        std::map<std::string, LatestIndex> latestIndices;
        const std::string questSql =
            "SELECT quest_id, journal_index, changed_at FROM character_journal_quests WHERE character_id IN ("
            + placeholders + ") ORDER BY changed_at";
        sqlite3_stmt* quests = prepare(questSql.c_str());
        for (std::size_t i = 0; i < characterIds.size(); ++i)
            sqlite3_bind_int64(quests, static_cast<int>(i + 1), characterIds[i]);
        while (sqlite3_step(quests) == SQLITE_ROW)
        {
            const char* questText = reinterpret_cast<const char*>(sqlite3_column_text(quests, 0));
            if (!questText)
                continue;
            LatestIndex& latest = latestIndices[questText];
            const int64_t changedAt = sqlite3_column_int64(quests, 2);
            if (changedAt >= latest.changedAt)
            {
                latest.index = sqlite3_column_int(quests, 1);
                latest.changedAt = changedAt;
            }
        }
        sqlite3_finalize(quests);

        for (const auto& [quest, latest] : latestIndices)
        {
            BasePlayer::JournalItem item;
            item.type = BasePlayer::JournalItem::Type::Index;
            item.quest = quest;
            item.index = latest.index;
            result.push_back(std::move(item));
        }
        return result;
    }

    std::vector<std::string> PlayerDatabase::loadReferencedJournalInfoIds(std::string_view dialogueId)
    {
        sqlite3_stmt* statement = prepare(
            "SELECT DISTINCT info_id FROM character_journal_entries"
            " WHERE lower(quest_id)=lower(?1) ORDER BY lower(info_id), info_id");
        sqlite3_bind_text(
            statement, 1, dialogueId.data(), static_cast<int>(dialogueId.size()), SQLITE_TRANSIENT);
        std::vector<std::string> result;
        while (sqlite3_step(statement) == SQLITE_ROW)
        {
            const char* value = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
            if (value != nullptr)
                result.emplace_back(value);
        }
        sqlite3_finalize(statement);
        return result;
    }

    std::vector<JournalCharacterIdentity> PlayerDatabase::listJournalCharacterIdentities()
    {
        sqlite3_stmt* statement = prepare(
            "SELECT c.id, a.username, c.name FROM characters c"
            " JOIN accounts a ON a.id=c.account_id ORDER BY c.id");
        std::vector<JournalCharacterIdentity> result;
        while (sqlite3_step(statement) == SQLITE_ROW)
        {
            JournalCharacterIdentity identity;
            identity.characterId = sqlite3_column_int64(statement, 0);
            const char* account = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
            const char* character = reinterpret_cast<const char*>(sqlite3_column_text(statement, 2));
            identity.accountName = account ? account : "";
            identity.characterName = character ? character : "";
            result.push_back(std::move(identity));
        }
        sqlite3_finalize(statement);
        return result;
    }

    std::vector<PlacedObject> PlayerDatabase::loadWorldObjects()
    {
        sqlite3_stmt* s = prepare(
            "SELECT mp_num, cell_id, ref_id, item_count, pos_x, pos_y, pos_z, rot_x, rot_y, rot_z"
            " FROM world_objects ORDER BY cell_id, mp_num");

        std::vector<PlacedObject> objects;
        while (sqlite3_step(s) == SQLITE_ROW)
        {
            PlacedObject object;
            object.mpNum = static_cast<uint32_t>(sqlite3_column_int64(s, 0));

            auto col = [&](int i) -> std::string {
                const char* t = reinterpret_cast<const char*>(sqlite3_column_text(s, i));
                return t ? t : "";
            };

            object.cellId = col(1);
            object.refId = col(2);
            object.count = sqlite3_column_int(s, 3);
            object.position.pos[0] = static_cast<float>(sqlite3_column_double(s, 4));
            object.position.pos[1] = static_cast<float>(sqlite3_column_double(s, 5));
            object.position.pos[2] = static_cast<float>(sqlite3_column_double(s, 6));
            object.position.rot[0] = static_cast<float>(sqlite3_column_double(s, 7));
            object.position.rot[1] = static_cast<float>(sqlite3_column_double(s, 8));
            object.position.rot[2] = static_cast<float>(sqlite3_column_double(s, 9));
            objects.push_back(std::move(object));
        }

        sqlite3_finalize(s);
        return objects;
    }

    void PlayerDatabase::upsertWorldObject(const PlacedObject& object)
    {
        exec("BEGIN");
        try
        {
            const std::string ownerA = std::to_string(object.mpNum);

            sqlite3_stmt* s = prepare(
                "INSERT INTO world_objects(mp_num, cell_id, ref_id, item_count, pos_x, pos_y, pos_z, rot_x, rot_y, "
                "rot_z)"
                " VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)"
                " ON CONFLICT(mp_num) DO UPDATE SET"
                " cell_id=excluded.cell_id,"
                " ref_id=excluded.ref_id,"
                " item_count=excluded.item_count,"
                " pos_x=excluded.pos_x,"
                " pos_y=excluded.pos_y,"
                " pos_z=excluded.pos_z,"
                " rot_x=excluded.rot_x,"
                " rot_y=excluded.rot_y,"
                " rot_z=excluded.rot_z");

            sqlite3_bind_int64(s, 1, object.mpNum);
            sqlite3_bind_text(s, 2, object.cellId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(s, 3, object.refId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(s, 4, object.count);
            sqlite3_bind_double(s, 5, object.position.pos[0]);
            sqlite3_bind_double(s, 6, object.position.pos[1]);
            sqlite3_bind_double(s, 7, object.position.pos[2]);
            sqlite3_bind_double(s, 8, object.position.rot[0]);
            sqlite3_bind_double(s, 9, object.position.rot[1]);
            sqlite3_bind_double(s, 10, object.position.rot[2]);
            checkSqlite(sqlite3_step(s), mDb, "upsertWorldObject");
            sqlite3_finalize(s);

            sqlite3_stmt* clearLinks = prepare(
                "DELETE FROM world_dynamic_record_links"
                " WHERE link_kind=?1 AND owner_a=?2 AND owner_b=?3 AND owner_c=?4");
            clearDynamicRecordLinksForOwner(mDb, clearLinks, "placed_object", ownerA, object.cellId, "");
            sqlite3_finalize(clearLinks);

            sqlite3_stmt* insertLink = prepare(
                "INSERT OR REPLACE INTO world_dynamic_record_links(record_id, link_kind, owner_a, owner_b, owner_c, "
                "owner_index)"
                " VALUES(?1, ?2, ?3, ?4, ?5, ?6)");
            insertDynamicRecordLink(mDb, insertLink, object.refId, "placed_object", ownerA, object.cellId, "", 0);
            sqlite3_finalize(insertLink);

            exec("COMMIT");
        }
        catch (...)
        {
            try
            {
                exec("ROLLBACK");
            }
            catch (...)
            {
            }
            throw;
        }
    }

    void PlayerDatabase::deleteWorldObject(uint32_t mpNum)
    {
        exec("BEGIN");
        try
        {
            const std::string ownerA = std::to_string(mpNum);

            sqlite3_stmt* s = prepare("DELETE FROM world_objects WHERE mp_num=?1");
            sqlite3_bind_int64(s, 1, mpNum);
            checkSqlite(sqlite3_step(s), mDb, "deleteWorldObject");
            sqlite3_finalize(s);

            sqlite3_stmt* clearLinks
                = prepare("DELETE FROM world_dynamic_record_links WHERE link_kind=?1 AND owner_a=?2");
            sqlite3_bind_text(clearLinks, 1, "placed_object", -1, SQLITE_STATIC);
            sqlite3_bind_text(clearLinks, 2, ownerA.c_str(), -1, SQLITE_TRANSIENT);
            checkSqlite(sqlite3_step(clearLinks), mDb, "deleteWorldObject(link)");
            sqlite3_finalize(clearLinks);

            exec("COMMIT");
        }
        catch (...)
        {
            try
            {
                exec("ROLLBACK");
            }
            catch (...)
            {
            }
            throw;
        }
    }

    std::size_t PlayerDatabase::deleteWorldObjectsForCell(std::string_view cellId)
    {
        if (cellId.empty())
            return 0;

        exec("BEGIN");
        try
        {
            sqlite3_stmt* s = prepare("DELETE FROM world_objects WHERE cell_id=?1");
            sqlite3_bind_text(s, 1, cellId.data(), static_cast<int>(cellId.size()), SQLITE_TRANSIENT);
            checkSqlite(sqlite3_step(s), mDb, "deleteWorldObjectsForCell");
            const std::size_t removed = static_cast<std::size_t>(sqlite3_changes(mDb));
            sqlite3_finalize(s);

            sqlite3_stmt* clearLinks = prepare(
                "DELETE FROM world_dynamic_record_links WHERE link_kind=?1 AND owner_b=?2");
            sqlite3_bind_text(clearLinks, 1, "placed_object", -1, SQLITE_STATIC);
            sqlite3_bind_text(clearLinks, 2, cellId.data(), static_cast<int>(cellId.size()), SQLITE_TRANSIENT);
            checkSqlite(sqlite3_step(clearLinks), mDb, "deleteWorldObjectsForCell(link)");
            sqlite3_finalize(clearLinks);

            exec("COMMIT");
            return removed;
        }
        catch (...)
        {
            try
            {
                exec("ROLLBACK");
            }
            catch (...)
            {
            }
            throw;
        }
    }

    std::vector<PersistedSpawnedActor> PlayerDatabase::loadSpawnedActors()
    {
        sqlite3_stmt* s = prepare(
            "SELECT mp_num, cell_id, ref_id, ref_num, persistent,"
            " pos_x, pos_y, pos_z, rot_x, rot_y, rot_z,"
            " health_base, health_current, health_mod,"
            " magicka_base, magicka_current, magicka_mod,"
            " fatigue_base, fatigue_current, fatigue_mod,"
            " is_dead, death_state, death_anim_group, created_at, updated_at"
            " FROM world_spawned_actors WHERE persistent != 0 ORDER BY cell_id, mp_num");

        std::vector<PersistedSpawnedActor> records;
        while (sqlite3_step(s) == SQLITE_ROW)
        {
            PersistedSpawnedActor record;
            BaseActor& actor = record.actor;
            actor.mpNum = static_cast<uint32_t>(sqlite3_column_int64(s, 0));

            auto col = [&](int i) -> std::string {
                const char* t = reinterpret_cast<const char*>(sqlite3_column_text(s, i));
                return t ? t : "";
            };

            actor.cellId = col(1);
            actor.refId = col(2);
            actor.refNum = static_cast<uint32_t>(sqlite3_column_int64(s, 3));
            record.persistent = sqlite3_column_int(s, 4) != 0;
            actor.position.pos[0] = static_cast<float>(sqlite3_column_double(s, 5));
            actor.position.pos[1] = static_cast<float>(sqlite3_column_double(s, 6));
            actor.position.pos[2] = static_cast<float>(sqlite3_column_double(s, 7));
            actor.position.rot[0] = static_cast<float>(sqlite3_column_double(s, 8));
            actor.position.rot[1] = static_cast<float>(sqlite3_column_double(s, 9));
            actor.position.rot[2] = static_cast<float>(sqlite3_column_double(s, 10));
            actor.dynamicStats.health.base = static_cast<float>(sqlite3_column_double(s, 11));
            actor.dynamicStats.health.current = static_cast<float>(sqlite3_column_double(s, 12));
            actor.dynamicStats.health.mod = static_cast<float>(sqlite3_column_double(s, 13));
            actor.dynamicStats.magicka.base = static_cast<float>(sqlite3_column_double(s, 14));
            actor.dynamicStats.magicka.current = static_cast<float>(sqlite3_column_double(s, 15));
            actor.dynamicStats.magicka.mod = static_cast<float>(sqlite3_column_double(s, 16));
            actor.dynamicStats.fatigue.base = static_cast<float>(sqlite3_column_double(s, 17));
            actor.dynamicStats.fatigue.current = static_cast<float>(sqlite3_column_double(s, 18));
            actor.dynamicStats.fatigue.mod = static_cast<float>(sqlite3_column_double(s, 19));
            actor.isDead = sqlite3_column_int(s, 20) != 0;
            actor.deathState = static_cast<uint8_t>(sqlite3_column_int(s, 21));
            actor.deathAnimGroup = col(22);
            record.createdAt = sqlite3_column_int64(s, 23);
            record.updatedAt = sqlite3_column_int64(s, 24);
            actor.equipment.resize(BaseActor::NUM_EQUIPMENT_SLOTS);
            records.push_back(std::move(record));
        }

        sqlite3_finalize(s);
        return records;
    }

    void PlayerDatabase::upsertSpawnedActor(const PersistedSpawnedActor& record)
    {
        const BaseActor& actor = record.actor;
        if (actor.mpNum == 0 || actor.refId.empty() || actor.cellId.empty())
            return;

        const int64_t now = static_cast<int64_t>(std::time(nullptr));
        const int64_t createdAt = record.createdAt != 0 ? record.createdAt : now;

        sqlite3_stmt* s = prepare(
            "INSERT INTO world_spawned_actors("
            " mp_num, cell_id, ref_id, ref_num, persistent,"
            " pos_x, pos_y, pos_z, rot_x, rot_y, rot_z,"
            " health_base, health_current, health_mod,"
            " magicka_base, magicka_current, magicka_mod,"
            " fatigue_base, fatigue_current, fatigue_mod,"
            " is_dead, death_state, death_anim_group, created_at, updated_at)"
            " VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11,"
            " ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21, ?22, ?23, ?24, ?25)"
            " ON CONFLICT(mp_num) DO UPDATE SET"
            " cell_id=excluded.cell_id,"
            " ref_id=excluded.ref_id,"
            " ref_num=excluded.ref_num,"
            " persistent=excluded.persistent,"
            " pos_x=excluded.pos_x,"
            " pos_y=excluded.pos_y,"
            " pos_z=excluded.pos_z,"
            " rot_x=excluded.rot_x,"
            " rot_y=excluded.rot_y,"
            " rot_z=excluded.rot_z,"
            " health_base=excluded.health_base,"
            " health_current=excluded.health_current,"
            " health_mod=excluded.health_mod,"
            " magicka_base=excluded.magicka_base,"
            " magicka_current=excluded.magicka_current,"
            " magicka_mod=excluded.magicka_mod,"
            " fatigue_base=excluded.fatigue_base,"
            " fatigue_current=excluded.fatigue_current,"
            " fatigue_mod=excluded.fatigue_mod,"
            " is_dead=excluded.is_dead,"
            " death_state=excluded.death_state,"
            " death_anim_group=excluded.death_anim_group,"
            " updated_at=excluded.updated_at");

        sqlite3_bind_int64(s, 1, actor.mpNum);
        sqlite3_bind_text(s, 2, actor.cellId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 3, actor.refId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(s, 4, actor.refNum);
        sqlite3_bind_int(s, 5, record.persistent ? 1 : 0);
        sqlite3_bind_double(s, 6, actor.position.pos[0]);
        sqlite3_bind_double(s, 7, actor.position.pos[1]);
        sqlite3_bind_double(s, 8, actor.position.pos[2]);
        sqlite3_bind_double(s, 9, actor.position.rot[0]);
        sqlite3_bind_double(s, 10, actor.position.rot[1]);
        sqlite3_bind_double(s, 11, actor.position.rot[2]);
        sqlite3_bind_double(s, 12, actor.dynamicStats.health.base);
        sqlite3_bind_double(s, 13, actor.dynamicStats.health.current);
        sqlite3_bind_double(s, 14, actor.dynamicStats.health.mod);
        sqlite3_bind_double(s, 15, actor.dynamicStats.magicka.base);
        sqlite3_bind_double(s, 16, actor.dynamicStats.magicka.current);
        sqlite3_bind_double(s, 17, actor.dynamicStats.magicka.mod);
        sqlite3_bind_double(s, 18, actor.dynamicStats.fatigue.base);
        sqlite3_bind_double(s, 19, actor.dynamicStats.fatigue.current);
        sqlite3_bind_double(s, 20, actor.dynamicStats.fatigue.mod);
        sqlite3_bind_int(s, 21, actor.isDead ? 1 : 0);
        sqlite3_bind_int(s, 22, actor.deathState);
        sqlite3_bind_text(s, 23, actor.deathAnimGroup.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(s, 24, createdAt);
        sqlite3_bind_int64(s, 25, now);
        checkSqlite(sqlite3_step(s), mDb, "upsertSpawnedActor");
        sqlite3_finalize(s);
    }

    void PlayerDatabase::deleteSpawnedActor(uint32_t mpNum)
    {
        if (mpNum == 0)
            return;

        sqlite3_stmt* s = prepare("DELETE FROM world_spawned_actors WHERE mp_num=?1");
        sqlite3_bind_int64(s, 1, mpNum);
        checkSqlite(sqlite3_step(s), mDb, "deleteSpawnedActor");
        sqlite3_finalize(s);
    }

    std::size_t PlayerDatabase::deleteSpawnedActorsForCell(std::string_view cellId)
    {
        if (cellId.empty())
            return 0;

        sqlite3_stmt* s = prepare("DELETE FROM world_spawned_actors WHERE cell_id=?1");
        sqlite3_bind_text(s, 1, cellId.data(), static_cast<int>(cellId.size()), SQLITE_TRANSIENT);
        checkSqlite(sqlite3_step(s), mDb, "deleteSpawnedActorsForCell");
        const std::size_t removed = static_cast<std::size_t>(sqlite3_changes(mDb));
        sqlite3_finalize(s);
        return removed;
    }

    std::vector<BaseActor> PlayerDatabase::loadDeadVanillaActors()
    {
        sqlite3_stmt* s = prepare(
            "SELECT ref_id, ref_num, cell_id,"
            " pos_x, pos_y, pos_z, rot_x, rot_y, rot_z,"
            " health_base, health_current, health_mod,"
            " magicka_base, magicka_current, magicka_mod,"
            " fatigue_base, fatigue_current, fatigue_mod,"
            " is_dead, death_state, is_instant_death, death_anim_group, created_at, updated_at"
            " FROM world_dead_vanilla_actors WHERE is_dead != 0 ORDER BY cell_id, ref_id, ref_num");

        std::vector<BaseActor> actors;
        while (sqlite3_step(s) == SQLITE_ROW)
        {
            BaseActor actor;
            auto col = [&](int i) -> std::string {
                const char* t = reinterpret_cast<const char*>(sqlite3_column_text(s, i));
                return t ? t : "";
            };

            actor.refId = col(0);
            actor.refNum = static_cast<uint32_t>(sqlite3_column_int64(s, 1));
            actor.mpNum = 0;
            actor.cellId = col(2);
            actor.position.pos[0] = static_cast<float>(sqlite3_column_double(s, 3));
            actor.position.pos[1] = static_cast<float>(sqlite3_column_double(s, 4));
            actor.position.pos[2] = static_cast<float>(sqlite3_column_double(s, 5));
            actor.position.rot[0] = static_cast<float>(sqlite3_column_double(s, 6));
            actor.position.rot[1] = static_cast<float>(sqlite3_column_double(s, 7));
            actor.position.rot[2] = static_cast<float>(sqlite3_column_double(s, 8));
            actor.dynamicStats.health.base = static_cast<float>(sqlite3_column_double(s, 9));
            actor.dynamicStats.health.current = static_cast<float>(sqlite3_column_double(s, 10));
            actor.dynamicStats.health.mod = static_cast<float>(sqlite3_column_double(s, 11));
            actor.dynamicStats.magicka.base = static_cast<float>(sqlite3_column_double(s, 12));
            actor.dynamicStats.magicka.current = static_cast<float>(sqlite3_column_double(s, 13));
            actor.dynamicStats.magicka.mod = static_cast<float>(sqlite3_column_double(s, 14));
            actor.dynamicStats.fatigue.base = static_cast<float>(sqlite3_column_double(s, 15));
            actor.dynamicStats.fatigue.current = static_cast<float>(sqlite3_column_double(s, 16));
            actor.dynamicStats.fatigue.mod = static_cast<float>(sqlite3_column_double(s, 17));
            actor.isDead = sqlite3_column_int(s, 18) != 0;
            actor.deathState = static_cast<uint8_t>(sqlite3_column_int(s, 19));
            actor.isInstantDeath = sqlite3_column_int(s, 20) != 0;
            actor.deathAnimGroup = col(21);
            actor.equipment.resize(BaseActor::NUM_EQUIPMENT_SLOTS);
            actors.push_back(std::move(actor));
        }

        sqlite3_finalize(s);
        return actors;
    }

    std::vector<BaseActor> PlayerDatabase::loadDisposedVanillaActors()
    {
        sqlite3_stmt* s = prepare(
            "SELECT ref_id, ref_num, cell_id"
            " FROM world_dead_vanilla_actors WHERE is_dead = 0 ORDER BY cell_id, ref_id, ref_num");

        std::vector<BaseActor> actors;
        while (sqlite3_step(s) == SQLITE_ROW)
        {
            BaseActor actor;
            const char* refId = reinterpret_cast<const char*>(sqlite3_column_text(s, 0));
            const char* cellId = reinterpret_cast<const char*>(sqlite3_column_text(s, 2));
            actor.refId = refId ? refId : "";
            actor.refNum = static_cast<uint32_t>(sqlite3_column_int64(s, 1));
            actor.mpNum = 0;
            actor.cellId = cellId ? cellId : "";
            actor.isDead = false;
            actor.equipment.resize(BaseActor::NUM_EQUIPMENT_SLOTS);
            actors.push_back(std::move(actor));
        }

        sqlite3_finalize(s);
        return actors;
    }

    void PlayerDatabase::upsertDeadVanillaActor(const BaseActor& actor)
    {
        if (actor.mpNum != 0 || actor.refId.empty() || actor.cellId.empty() || !actor.isDead)
            return;

        const int64_t now = static_cast<int64_t>(std::time(nullptr));

        sqlite3_stmt* s = prepare(
            "INSERT INTO world_dead_vanilla_actors("
            " ref_id, ref_num, cell_id,"
            " pos_x, pos_y, pos_z, rot_x, rot_y, rot_z,"
            " health_base, health_current, health_mod,"
            " magicka_base, magicka_current, magicka_mod,"
            " fatigue_base, fatigue_current, fatigue_mod,"
            " is_dead, death_state, is_instant_death, death_anim_group, created_at, updated_at)"
            " VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9,"
            " ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18,"
            " ?19, ?20, ?21, ?22, ?23, ?24)"
            " ON CONFLICT(ref_id, ref_num) DO UPDATE SET"
            " cell_id=excluded.cell_id,"
            " pos_x=excluded.pos_x,"
            " pos_y=excluded.pos_y,"
            " pos_z=excluded.pos_z,"
            " rot_x=excluded.rot_x,"
            " rot_y=excluded.rot_y,"
            " rot_z=excluded.rot_z,"
            " health_base=excluded.health_base,"
            " health_current=excluded.health_current,"
            " health_mod=excluded.health_mod,"
            " magicka_base=excluded.magicka_base,"
            " magicka_current=excluded.magicka_current,"
            " magicka_mod=excluded.magicka_mod,"
            " fatigue_base=excluded.fatigue_base,"
            " fatigue_current=excluded.fatigue_current,"
            " fatigue_mod=excluded.fatigue_mod,"
            " is_dead=excluded.is_dead,"
            " death_state=excluded.death_state,"
            " is_instant_death=excluded.is_instant_death,"
            " death_anim_group=excluded.death_anim_group,"
            " updated_at=excluded.updated_at");

        sqlite3_bind_text(s, 1, actor.refId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(s, 2, actor.refNum);
        sqlite3_bind_text(s, 3, actor.cellId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(s, 4, actor.position.pos[0]);
        sqlite3_bind_double(s, 5, actor.position.pos[1]);
        sqlite3_bind_double(s, 6, actor.position.pos[2]);
        sqlite3_bind_double(s, 7, actor.position.rot[0]);
        sqlite3_bind_double(s, 8, actor.position.rot[1]);
        sqlite3_bind_double(s, 9, actor.position.rot[2]);
        sqlite3_bind_double(s, 10, actor.dynamicStats.health.base);
        sqlite3_bind_double(s, 11, actor.dynamicStats.health.current);
        sqlite3_bind_double(s, 12, actor.dynamicStats.health.mod);
        sqlite3_bind_double(s, 13, actor.dynamicStats.magicka.base);
        sqlite3_bind_double(s, 14, actor.dynamicStats.magicka.current);
        sqlite3_bind_double(s, 15, actor.dynamicStats.magicka.mod);
        sqlite3_bind_double(s, 16, actor.dynamicStats.fatigue.base);
        sqlite3_bind_double(s, 17, actor.dynamicStats.fatigue.current);
        sqlite3_bind_double(s, 18, actor.dynamicStats.fatigue.mod);
        sqlite3_bind_int(s, 19, actor.isDead ? 1 : 0);
        sqlite3_bind_int(s, 20, actor.deathState);
        sqlite3_bind_int(s, 21, actor.isInstantDeath ? 1 : 0);
        sqlite3_bind_text(s, 22, actor.deathAnimGroup.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(s, 23, now);
        sqlite3_bind_int64(s, 24, now);
        checkSqlite(sqlite3_step(s), mDb, "upsertDeadVanillaActor");
        sqlite3_finalize(s);
    }

    void PlayerDatabase::upsertDisposedVanillaActor(const BaseActor& actor)
    {
        if (actor.mpNum != 0 || actor.refId.empty() || actor.cellId.empty())
            return;

        const int64_t now = static_cast<int64_t>(std::time(nullptr));
        sqlite3_stmt* s = prepare(
            "INSERT INTO world_dead_vanilla_actors("
            " ref_id, ref_num, cell_id, is_dead, created_at, updated_at)"
            " VALUES(?1, ?2, ?3, 0, ?4, ?5)"
            " ON CONFLICT(ref_id, ref_num) DO UPDATE SET"
            " cell_id=excluded.cell_id,"
            " is_dead=0,"
            " updated_at=excluded.updated_at");
        sqlite3_bind_text(s, 1, actor.refId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(s, 2, actor.refNum);
        sqlite3_bind_text(s, 3, actor.cellId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(s, 4, now);
        sqlite3_bind_int64(s, 5, now);
        checkSqlite(sqlite3_step(s), mDb, "upsertDisposedVanillaActor");
        sqlite3_finalize(s);
    }

    void PlayerDatabase::deleteDeadVanillaActor(std::string_view refId, uint32_t refNum)
    {
        if (refId.empty())
            return;

        sqlite3_stmt* s = prepare("DELETE FROM world_dead_vanilla_actors WHERE ref_id=?1 AND ref_num=?2");
        sqlite3_bind_text(s, 1, refId.data(), static_cast<int>(refId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int64(s, 2, refNum);
        checkSqlite(sqlite3_step(s), mDb, "deleteDeadVanillaActor");
        sqlite3_finalize(s);
    }

    std::size_t PlayerDatabase::deleteDeadVanillaActorsForCell(std::string_view cellId)
    {
        if (cellId.empty())
            return 0;

        sqlite3_stmt* s = prepare("DELETE FROM world_dead_vanilla_actors WHERE cell_id=?1");
        sqlite3_bind_text(s, 1, cellId.data(), static_cast<int>(cellId.size()), SQLITE_TRANSIENT);
        checkSqlite(sqlite3_step(s), mDb, "deleteDeadVanillaActorsForCell");
        const std::size_t removed = static_cast<std::size_t>(sqlite3_changes(mDb));
        sqlite3_finalize(s);
        return removed;
    }

    std::vector<ContainerRecord> PlayerDatabase::loadContainerRecords()
    {
        sqlite3_stmt* s = prepare(
            "SELECT cell_id, ref_id, ref_num, mp_num, has_authority"
            " FROM world_containers ORDER BY cell_id, ref_id, ref_num");

        std::vector<ContainerRecord> records;
        while (sqlite3_step(s) == SQLITE_ROW)
        {
            ContainerRecord record;
            auto col = [&](int i) -> std::string {
                const char* t = reinterpret_cast<const char*>(sqlite3_column_text(s, i));
                return t ? t : "";
            };

            record.cellId = col(0);
            record.refId = col(1);
            record.refNum = static_cast<uint32_t>(sqlite3_column_int64(s, 2));
            record.mpNum = static_cast<uint32_t>(sqlite3_column_int64(s, 3));
            record.hasAuthority = sqlite3_column_int(s, 4) != 0;
            records.push_back(std::move(record));
        }
        sqlite3_finalize(s);

        sqlite3_stmt* items = prepare(
            "SELECT cell_id, ref_id, ref_num, item_ref_id, item_count, charge"
            " FROM world_container_items"
            " ORDER BY cell_id, ref_id, ref_num, item_index");

        while (sqlite3_step(items) == SQLITE_ROW)
        {
            auto col = [&](int i) -> std::string {
                const char* t = reinterpret_cast<const char*>(sqlite3_column_text(items, i));
                return t ? t : "";
            };

            const std::string cellId = col(0);
            const std::string refId = col(1);
            const uint32_t refNum = static_cast<uint32_t>(sqlite3_column_int64(items, 2));

            for (auto& record : records)
            {
                if (record.cellId == cellId && record.refId == refId && record.refNum == refNum)
                {
                    ContainerItem item;
                    item.refId = col(3);
                    item.count = sqlite3_column_int(items, 4);
                    item.charge = sqlite3_column_int(items, 5);
                    record.items.push_back(std::move(item));
                    break;
                }
            }
        }
        sqlite3_finalize(items);

        return records;
    }

    void PlayerDatabase::upsertContainerRecord(const ContainerRecord& record)
    {
        exec("BEGIN");
        try
        {
            const std::string ownerC = std::to_string(record.refNum);

            sqlite3_stmt* parent = prepare(
                "INSERT INTO world_containers(cell_id, ref_id, ref_num, mp_num, has_authority)"
                " VALUES(?1, ?2, ?3, ?4, ?5)"
                " ON CONFLICT(cell_id, ref_id, ref_num) DO UPDATE SET"
                " mp_num=excluded.mp_num,"
                " has_authority=excluded.has_authority");
            sqlite3_bind_text(parent, 1, record.cellId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(parent, 2, record.refId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(parent, 3, record.refNum);
            sqlite3_bind_int64(parent, 4, record.mpNum);
            sqlite3_bind_int(parent, 5, record.hasAuthority ? 1 : 0);
            checkSqlite(sqlite3_step(parent), mDb, "upsertContainerRecord(parent)");
            sqlite3_finalize(parent);

            sqlite3_stmt* clearItems
                = prepare("DELETE FROM world_container_items WHERE cell_id=?1 AND ref_id=?2 AND ref_num=?3");
            sqlite3_bind_text(clearItems, 1, record.cellId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(clearItems, 2, record.refId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(clearItems, 3, record.refNum);
            checkSqlite(sqlite3_step(clearItems), mDb, "upsertContainerRecord(clearItems)");
            sqlite3_finalize(clearItems);

            sqlite3_stmt* itemStmt = prepare(
                "INSERT INTO world_container_items(cell_id, ref_id, ref_num, item_index, item_ref_id, item_count, "
                "charge)"
                " VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7)");

            for (std::size_t i = 0; i < record.items.size(); ++i)
            {
                const auto& item = record.items[i];
                sqlite3_bind_text(itemStmt, 1, record.cellId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(itemStmt, 2, record.refId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(itemStmt, 3, record.refNum);
                sqlite3_bind_int(itemStmt, 4, static_cast<int>(i));
                sqlite3_bind_text(itemStmt, 5, item.refId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(itemStmt, 6, item.count);
                sqlite3_bind_int(itemStmt, 7, item.charge);
                checkSqlite(sqlite3_step(itemStmt), mDb, "upsertContainerRecord(item)");
                sqlite3_reset(itemStmt);
                sqlite3_clear_bindings(itemStmt);
            }
            sqlite3_finalize(itemStmt);

            sqlite3_stmt* clearLinks = prepare(
                "DELETE FROM world_dynamic_record_links"
                " WHERE link_kind=?1 AND owner_a=?2 AND owner_b=?3 AND owner_c=?4");
            clearDynamicRecordLinksForOwner(mDb, clearLinks, "container_parent", record.cellId, record.refId, ownerC);
            clearDynamicRecordLinksForOwner(mDb, clearLinks, "container_item", record.cellId, record.refId, ownerC);
            sqlite3_finalize(clearLinks);

            sqlite3_stmt* insertLink = prepare(
                "INSERT OR REPLACE INTO world_dynamic_record_links(record_id, link_kind, owner_a, owner_b, owner_c, "
                "owner_index)"
                " VALUES(?1, ?2, ?3, ?4, ?5, ?6)");
            insertDynamicRecordLink(
                mDb, insertLink, record.refId, "container_parent", record.cellId, record.refId, ownerC, 0);
            for (std::size_t i = 0; i < record.items.size(); ++i)
                insertDynamicRecordLink(mDb, insertLink, record.items[i].refId, "container_item", record.cellId,
                    record.refId, ownerC, static_cast<int64_t>(i));
            sqlite3_finalize(insertLink);

            exec("COMMIT");
        }
        catch (...)
        {
            try
            {
                exec("ROLLBACK");
            }
            catch (...)
            {
            }
            throw;
        }
    }

    void PlayerDatabase::deleteContainerRecord(std::string_view cellId, std::string_view refId, uint32_t refNum)
    {
        exec("BEGIN");
        try
        {
            const std::string ownerC = std::to_string(refNum);

            sqlite3_stmt* s = prepare("DELETE FROM world_containers WHERE cell_id=?1 AND ref_id=?2 AND ref_num=?3");
            sqlite3_bind_text(s, 1, cellId.data(), static_cast<int>(cellId.size()), SQLITE_TRANSIENT);
            sqlite3_bind_text(s, 2, refId.data(), static_cast<int>(refId.size()), SQLITE_TRANSIENT);
            sqlite3_bind_int64(s, 3, refNum);
            checkSqlite(sqlite3_step(s), mDb, "deleteContainerRecord");
            sqlite3_finalize(s);

            sqlite3_stmt* clearLinks = prepare(
                "DELETE FROM world_dynamic_record_links"
                " WHERE link_kind=?1 AND owner_a=?2 AND owner_b=?3 AND owner_c=?4");
            clearDynamicRecordLinksForOwner(mDb, clearLinks, "container_parent", cellId, refId, ownerC);
            clearDynamicRecordLinksForOwner(mDb, clearLinks, "container_item", cellId, refId, ownerC);
            sqlite3_finalize(clearLinks);

            exec("COMMIT");
        }
        catch (...)
        {
            try
            {
                exec("ROLLBACK");
            }
            catch (...)
            {
            }
            throw;
        }
    }

    std::size_t PlayerDatabase::deleteContainerRecordsForCell(std::string_view cellId)
    {
        if (cellId.empty())
            return 0;

        exec("BEGIN");
        try
        {
            sqlite3_stmt* s = prepare("DELETE FROM world_containers WHERE cell_id=?1");
            sqlite3_bind_text(s, 1, cellId.data(), static_cast<int>(cellId.size()), SQLITE_TRANSIENT);
            checkSqlite(sqlite3_step(s), mDb, "deleteContainerRecordsForCell");
            const std::size_t removed = static_cast<std::size_t>(sqlite3_changes(mDb));
            sqlite3_finalize(s);

            sqlite3_stmt* clearLinks = prepare(
                "DELETE FROM world_dynamic_record_links"
                " WHERE owner_a=?1 AND (link_kind=?2 OR link_kind=?3)");
            sqlite3_bind_text(clearLinks, 1, cellId.data(), static_cast<int>(cellId.size()), SQLITE_TRANSIENT);
            sqlite3_bind_text(clearLinks, 2, "container_parent", -1, SQLITE_STATIC);
            sqlite3_bind_text(clearLinks, 3, "container_item", -1, SQLITE_STATIC);
            checkSqlite(sqlite3_step(clearLinks), mDb, "deleteContainerRecordsForCell(link)");
            sqlite3_finalize(clearLinks);

            exec("COMMIT");
            return removed;
        }
        catch (...)
        {
            try
            {
                exec("ROLLBACK");
            }
            catch (...)
            {
            }
            throw;
        }
    }

    std::vector<DoorEntry> PlayerDatabase::loadDoorStates()
    {
        sqlite3_stmt* s = prepare(
            "SELECT cell_id, ref_id, ref_num, mp_num, is_open, is_locked, lock_level, revision"
            " FROM world_doors ORDER BY cell_id, ref_id, ref_num");

        std::vector<DoorEntry> entries;
        while (sqlite3_step(s) == SQLITE_ROW)
        {
            DoorEntry entry;
            auto col = [&](int i) -> std::string {
                const char* t = reinterpret_cast<const char*>(sqlite3_column_text(s, i));
                return t ? t : "";
            };

            entry.cellId = col(0);
            entry.refId = col(1);
            entry.refNum = static_cast<uint32_t>(sqlite3_column_int64(s, 2));
            entry.mpNum = static_cast<uint32_t>(sqlite3_column_int64(s, 3));
            entry.isOpen = sqlite3_column_int(s, 4) != 0;
            entry.isLocked = sqlite3_column_int(s, 5) != 0;
            entry.lockLevel = sqlite3_column_int(s, 6);
            entry.revision = static_cast<std::uint64_t>(sqlite3_column_int64(s, 7));
            entries.push_back(std::move(entry));
        }
        sqlite3_finalize(s);
        return entries;
    }

    void PlayerDatabase::upsertDoorState(const DoorEntry& entry)
    {
        exec("BEGIN");
        try
        {
            const std::string ownerC = std::to_string(entry.refNum);

            sqlite3_stmt* s = prepare(
                "INSERT INTO world_doors(cell_id, ref_id, ref_num, mp_num, is_open, is_locked, lock_level, revision)"
                " VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)"
                " ON CONFLICT(cell_id, ref_id, ref_num) DO UPDATE SET"
                " mp_num=excluded.mp_num,"
                " is_open=excluded.is_open,"
                " is_locked=excluded.is_locked,"
                " lock_level=excluded.lock_level,"
                " revision=excluded.revision");
            sqlite3_bind_text(s, 1, entry.cellId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(s, 2, entry.refId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(s, 3, entry.refNum);
            sqlite3_bind_int64(s, 4, entry.mpNum);
            sqlite3_bind_int(s, 5, entry.isOpen ? 1 : 0);
            sqlite3_bind_int(s, 6, entry.isLocked ? 1 : 0);
            sqlite3_bind_int(s, 7, entry.lockLevel);
            sqlite3_bind_int64(s, 8, static_cast<sqlite3_int64>(entry.revision));
            checkSqlite(sqlite3_step(s), mDb, "upsertDoorState");
            sqlite3_finalize(s);

            sqlite3_stmt* clearLinks = prepare(
                "DELETE FROM world_dynamic_record_links"
                " WHERE link_kind=?1 AND owner_a=?2 AND owner_b=?3 AND owner_c=?4");
            clearDynamicRecordLinksForOwner(mDb, clearLinks, "door_state", entry.cellId, entry.refId, ownerC);
            sqlite3_finalize(clearLinks);

            sqlite3_stmt* insertLink = prepare(
                "INSERT OR REPLACE INTO world_dynamic_record_links(record_id, link_kind, owner_a, owner_b, owner_c, "
                "owner_index)"
                " VALUES(?1, ?2, ?3, ?4, ?5, ?6)");
            insertDynamicRecordLink(mDb, insertLink, entry.refId, "door_state", entry.cellId, entry.refId, ownerC, 0);
            sqlite3_finalize(insertLink);

            exec("COMMIT");
        }
        catch (...)
        {
            try
            {
                exec("ROLLBACK");
            }
            catch (...)
            {
            }
            throw;
        }
    }

    void PlayerDatabase::deleteDoorState(std::string_view cellId, std::string_view refId, uint32_t refNum)
    {
        exec("BEGIN");
        try
        {
            const std::string ownerC = std::to_string(refNum);

            sqlite3_stmt* s = prepare("DELETE FROM world_doors WHERE cell_id=?1 AND ref_id=?2 AND ref_num=?3");
            sqlite3_bind_text(s, 1, cellId.data(), static_cast<int>(cellId.size()), SQLITE_TRANSIENT);
            sqlite3_bind_text(s, 2, refId.data(), static_cast<int>(refId.size()), SQLITE_TRANSIENT);
            sqlite3_bind_int64(s, 3, refNum);
            checkSqlite(sqlite3_step(s), mDb, "deleteDoorState");
            sqlite3_finalize(s);

            sqlite3_stmt* clearLinks = prepare(
                "DELETE FROM world_dynamic_record_links"
                " WHERE link_kind=?1 AND owner_a=?2 AND owner_b=?3 AND owner_c=?4");
            clearDynamicRecordLinksForOwner(mDb, clearLinks, "door_state", cellId, refId, ownerC);
            sqlite3_finalize(clearLinks);

            exec("COMMIT");
        }
        catch (...)
        {
            try
            {
                exec("ROLLBACK");
            }
            catch (...)
            {
            }
            throw;
        }
    }

    std::size_t PlayerDatabase::deleteDoorStatesForCell(std::string_view cellId)
    {
        if (cellId.empty())
            return 0;

        exec("BEGIN");
        try
        {
            sqlite3_stmt* s = prepare("DELETE FROM world_doors WHERE cell_id=?1");
            sqlite3_bind_text(s, 1, cellId.data(), static_cast<int>(cellId.size()), SQLITE_TRANSIENT);
            checkSqlite(sqlite3_step(s), mDb, "deleteDoorStatesForCell");
            const std::size_t removed = static_cast<std::size_t>(sqlite3_changes(mDb));
            sqlite3_finalize(s);

            sqlite3_stmt* clearLinks = prepare(
                "DELETE FROM world_dynamic_record_links WHERE link_kind=?1 AND owner_a=?2");
            sqlite3_bind_text(clearLinks, 1, "door_state", -1, SQLITE_STATIC);
            sqlite3_bind_text(clearLinks, 2, cellId.data(), static_cast<int>(cellId.size()), SQLITE_TRANSIENT);
            checkSqlite(sqlite3_step(clearLinks), mDb, "deleteDoorStatesForCell(link)");
            sqlite3_finalize(clearLinks);

            exec("COMMIT");
            return removed;
        }
        catch (...)
        {
            try
            {
                exec("ROLLBACK");
            }
            catch (...)
            {
            }
            throw;
        }
    }

    uint64_t PlayerDatabase::loadNextMpNum(uint64_t minimumNext)
    {
        minimumNext = std::max<uint64_t>(minimumNext, 1);

        sqlite3_stmt* s = prepare("SELECT value FROM world_metadata WHERE key='next_mp_num' LIMIT 1");
        uint64_t storedNext = 0;
        if (sqlite3_step(s) == SQLITE_ROW)
        {
            const sqlite3_int64 value = sqlite3_column_int64(s, 0);
            if (value > 0)
                storedNext = static_cast<uint64_t>(value);
        }
        sqlite3_finalize(s);

        const uint64_t nextMpNum = std::max(storedNext, minimumNext);
        if (nextMpNum != storedNext)
            saveNextMpNum(nextMpNum);

        return nextMpNum;
    }

    void PlayerDatabase::saveNextMpNum(uint64_t nextMpNum)
    {
        nextMpNum = std::max<uint64_t>(nextMpNum, 1);
        if (nextMpNum > static_cast<uint64_t>(std::numeric_limits<sqlite3_int64>::max()))
            throw std::runtime_error("[PlayerDB] next_mp_num exceeds SQLite INTEGER range");

        sqlite3_stmt* s = prepare(
            "INSERT INTO world_metadata(key, value) VALUES('next_mp_num', ?1)"
            " ON CONFLICT(key) DO UPDATE SET value=excluded.value");
        sqlite3_bind_int64(s, 1, static_cast<sqlite3_int64>(nextMpNum));
        checkSqlite(sqlite3_step(s), mDb, "saveNextMpNum");
        sqlite3_finalize(s);
    }

    std::vector<PersistedDynamicRecord> PlayerDatabase::loadDynamicRecords()
    {
        sqlite3_stmt* s = prepare(
            "SELECT record_type, record_id, record_scope, record_data, created_at, updated_at, schema_version"
            " FROM world_dynamic_records ORDER BY created_at, record_type, record_id");

        std::vector<PersistedDynamicRecord> records;
        while (sqlite3_step(s) == SQLITE_ROW)
        {
            PersistedDynamicRecord record;
            auto textCol = [&](int i) -> std::string {
                const char* t = reinterpret_cast<const char*>(sqlite3_column_text(s, i));
                return t ? t : "";
            };

            record.recordType = textCol(0);
            record.recordId = textCol(1);
            record.recordScope = textCol(2);

            const void* blob = sqlite3_column_blob(s, 3);
            const int blobSize = sqlite3_column_bytes(s, 3);
            if (blob && blobSize > 0)
                record.data.assign(static_cast<const char*>(blob), static_cast<std::size_t>(blobSize));

            record.createdAt = sqlite3_column_int64(s, 4);
            record.updatedAt = sqlite3_column_int64(s, 5);
            record.schemaVersion = static_cast<uint16_t>(sqlite3_column_int(s, 6));
            records.push_back(std::move(record));
        }

        sqlite3_finalize(s);
        return records;
    }

    void PlayerDatabase::upsertDynamicRecord(const PersistedDynamicRecord& record)
    {
        sqlite3_stmt* s = prepare(
            "INSERT INTO world_dynamic_records(record_type, record_id, record_scope, record_data, created_at, "
            "updated_at, schema_version)"
            " VALUES(?1, ?2, ?3, ?4, COALESCE(?5, strftime('%s', 'now')), ?6, ?7)"
            " ON CONFLICT(record_type, record_id) DO UPDATE SET"
            " record_scope=excluded.record_scope,"
            " record_data=excluded.record_data,"
            " updated_at=excluded.updated_at,"
            " schema_version=excluded.schema_version");

        const int64_t createdAt = record.createdAt != 0 ? record.createdAt : static_cast<int64_t>(std::time(nullptr));
        const int64_t updatedAt = record.updatedAt != 0 ? record.updatedAt : static_cast<int64_t>(std::time(nullptr));

        sqlite3_bind_text(s, 1, record.recordType.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 2, record.recordId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 3, record.recordScope.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_blob(s, 4, record.data.data(), static_cast<int>(record.data.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int64(s, 5, createdAt);
        sqlite3_bind_int64(s, 6, updatedAt);
        sqlite3_bind_int(s, 7, record.schemaVersion);
        checkSqlite(sqlite3_step(s), mDb, "upsertDynamicRecord");
        sqlite3_finalize(s);
    }

    void PlayerDatabase::deleteDynamicRecord(std::string_view recordType, std::string_view recordId)
    {
        sqlite3_stmt* s = prepare("DELETE FROM world_dynamic_records WHERE record_type=?1 AND record_id=?2");
        sqlite3_bind_text(s, 1, recordType.data(), static_cast<int>(recordType.size()), SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 2, recordId.data(), static_cast<int>(recordId.size()), SQLITE_TRANSIENT);
        checkSqlite(sqlite3_step(s), mDb, "deleteDynamicRecord");
        sqlite3_finalize(s);
    }

    void PlayerDatabase::backupLegacyDynamicRecord(const PersistedDynamicRecord& record)
    {
        sqlite3_stmt* s = prepare(
            "INSERT OR IGNORE INTO world_dynamic_record_legacy_backup(record_type, record_id, record_scope,"
            " record_data, schema_version, backed_up_at) VALUES(?1, ?2, ?3, ?4, ?5, ?6)");
        sqlite3_bind_text(s, 1, record.recordType.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 2, record.recordId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 3, record.recordScope.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_blob(s, 4, record.data.data(), static_cast<int>(record.data.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int(s, 5, record.schemaVersion);
        sqlite3_bind_int64(s, 6, static_cast<int64_t>(std::time(nullptr)));
        checkSqlite(sqlite3_step(s), mDb, "backupLegacyDynamicRecord");
        sqlite3_finalize(s);
    }

    void PlayerDatabase::recordLegacyDynamicRecordMigrationFailure(
        std::string_view recordType, std::string_view recordId, std::string_view reason)
    {
        sqlite3_stmt* s = prepare(
            "INSERT INTO world_dynamic_record_migration_failures(record_type, record_id, reason, attempts,"
            " last_attempt_at) VALUES(?1, ?2, ?3, 1, ?4)"
            " ON CONFLICT(record_type, record_id) DO UPDATE SET reason=excluded.reason,"
            " attempts=world_dynamic_record_migration_failures.attempts+1,"
            " last_attempt_at=excluded.last_attempt_at");
        sqlite3_bind_text(s, 1, recordType.data(), static_cast<int>(recordType.size()), SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 2, recordId.data(), static_cast<int>(recordId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 3, reason.data(), static_cast<int>(reason.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int64(s, 4, static_cast<int64_t>(std::time(nullptr)));
        checkSqlite(sqlite3_step(s), mDb, "recordLegacyDynamicRecordMigrationFailure");
        sqlite3_finalize(s);
    }

    void PlayerDatabase::clearLegacyDynamicRecordMigrationFailure(
        std::string_view recordType, std::string_view recordId)
    {
        sqlite3_stmt* s = prepare(
            "DELETE FROM world_dynamic_record_migration_failures WHERE record_type=?1 AND record_id=?2");
        sqlite3_bind_text(s, 1, recordType.data(), static_cast<int>(recordType.size()), SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 2, recordId.data(), static_cast<int>(recordId.size()), SQLITE_TRANSIENT);
        checkSqlite(sqlite3_step(s), mDb, "clearLegacyDynamicRecordMigrationFailure");
        sqlite3_finalize(s);
    }

    std::optional<CraftRequestRecord> PlayerDatabase::loadCraftRequest(
        int64_t accountId, int64_t characterId, std::string_view requestId)
    {
        sqlite3_stmt* s = prepare(
            "SELECT request_hash, status, result_payload, created_at, updated_at"
            " FROM craft_requests WHERE account_id=?1 AND character_id=?2 AND request_id=?3 LIMIT 1");
        sqlite3_bind_int64(s, 1, accountId);
        sqlite3_bind_int64(s, 2, characterId);
        sqlite3_bind_text(s, 3, requestId.data(), static_cast<int>(requestId.size()), SQLITE_TRANSIENT);

        std::optional<CraftRequestRecord> result;
        if (sqlite3_step(s) == SQLITE_ROW)
        {
            CraftRequestRecord record;
            record.accountId = accountId;
            record.characterId = characterId;
            record.requestId = std::string(requestId);
            auto textColumn = [&](int index) {
                const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(s, index));
                return text ? std::string(text, static_cast<std::size_t>(sqlite3_column_bytes(s, index)))
                            : std::string{};
            };
            record.requestHash = textColumn(0);
            record.status = textColumn(1);
            const void* payload = sqlite3_column_blob(s, 2);
            const int payloadSize = sqlite3_column_bytes(s, 2);
            if (payload != nullptr && payloadSize > 0)
                record.resultPayload.assign(static_cast<const char*>(payload), static_cast<std::size_t>(payloadSize));
            record.createdAt = sqlite3_column_int64(s, 3);
            record.updatedAt = sqlite3_column_int64(s, 4);
            result = std::move(record);
        }
        sqlite3_finalize(s);
        return result;
    }

    std::optional<CraftRequestRecord> PlayerDatabase::loadServerRecordRequest(
        std::string_view source, std::string_view requestId)
    {
        sqlite3_stmt* s = prepare(
            "SELECT request_hash, status, result_payload, created_at, updated_at"
            " FROM server_record_requests WHERE source=?1 AND request_id=?2 LIMIT 1");
        sqlite3_bind_text(s, 1, source.data(), static_cast<int>(source.size()), SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 2, requestId.data(), static_cast<int>(requestId.size()), SQLITE_TRANSIENT);

        std::optional<CraftRequestRecord> result;
        if (sqlite3_step(s) == SQLITE_ROW)
        {
            CraftRequestRecord record;
            record.requestId = std::string(requestId);
            auto textColumn = [&](int index) {
                const auto* value = reinterpret_cast<const char*>(sqlite3_column_text(s, index));
                return value ? std::string(value, static_cast<std::size_t>(sqlite3_column_bytes(s, index)))
                             : std::string{};
            };
            record.requestHash = textColumn(0);
            record.status = textColumn(1);
            const void* payload = sqlite3_column_blob(s, 2);
            const int payloadSize = sqlite3_column_bytes(s, 2);
            if (payload != nullptr && payloadSize > 0)
                record.resultPayload.assign(static_cast<const char*>(payload), static_cast<std::size_t>(payloadSize));
            record.createdAt = sqlite3_column_int64(s, 3);
            record.updatedAt = sqlite3_column_int64(s, 4);
            result = std::move(record);
        }
        sqlite3_finalize(s);
        return result;
    }

    bool PlayerDatabase::insertPendingCraftRequest(const CraftRequestRecord& request)
    {
        const int64_t now = static_cast<int64_t>(std::time(nullptr));
        sqlite3_stmt* s = prepare(
            "INSERT OR IGNORE INTO craft_requests(account_id, character_id, request_id, request_hash, status,"
            " result_payload, created_at, updated_at) VALUES(?1, ?2, ?3, ?4, 'pending', '', ?5, ?5)");
        sqlite3_bind_int64(s, 1, request.accountId);
        sqlite3_bind_int64(s, 2, request.characterId);
        sqlite3_bind_text(s, 3, request.requestId.c_str(), static_cast<int>(request.requestId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_text(
            s, 4, request.requestHash.c_str(), static_cast<int>(request.requestHash.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int64(s, 5, request.createdAt != 0 ? request.createdAt : now);
        checkSqlite(sqlite3_step(s), mDb, "insertPendingCraftRequest");
        const bool inserted = sqlite3_changes(mDb) != 0;
        sqlite3_finalize(s);
        return inserted;
    }

    bool PlayerDatabase::insertRejectedCraftRequest(
        const CraftRequestRecord& request, std::string_view resultPayload)
    {
        const int64_t now = static_cast<int64_t>(std::time(nullptr));
        sqlite3_stmt* s = prepare(
            "INSERT OR IGNORE INTO craft_requests(account_id, character_id, request_id, request_hash, status,"
            " result_payload, created_at, updated_at) VALUES(?1, ?2, ?3, ?4, 'rejected', ?5, ?6, ?6)");
        sqlite3_bind_int64(s, 1, request.accountId);
        sqlite3_bind_int64(s, 2, request.characterId);
        sqlite3_bind_text(s, 3, request.requestId.c_str(), static_cast<int>(request.requestId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_text(
            s, 4, request.requestHash.c_str(), static_cast<int>(request.requestHash.size()), SQLITE_TRANSIENT);
        sqlite3_bind_blob(s, 5, resultPayload.data(), static_cast<int>(resultPayload.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int64(s, 6, request.createdAt != 0 ? request.createdAt : now);
        checkSqlite(sqlite3_step(s), mDb, "insertRejectedCraftRequest");
        const bool inserted = sqlite3_changes(mDb) != 0;
        sqlite3_finalize(s);
        return inserted;
    }

    bool PlayerDatabase::insertRejectedServerRecordRequest(std::string_view source,
        const CraftRequestRecord& request, std::string_view resultPayload)
    {
        const int64_t now = static_cast<int64_t>(std::time(nullptr));
        sqlite3_stmt* s = prepare(
            "INSERT OR IGNORE INTO server_record_requests(source, request_id, request_hash, status,"
            " result_payload, created_at, updated_at) VALUES(?1, ?2, ?3, 'rejected', ?4, ?5, ?5)");
        sqlite3_bind_text(s, 1, source.data(), static_cast<int>(source.size()), SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 2, request.requestId.c_str(), static_cast<int>(request.requestId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_text(
            s, 3, request.requestHash.c_str(), static_cast<int>(request.requestHash.size()), SQLITE_TRANSIENT);
        sqlite3_bind_blob(s, 4, resultPayload.data(), static_cast<int>(resultPayload.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int64(s, 5, request.createdAt != 0 ? request.createdAt : now);
        checkSqlite(sqlite3_step(s), mDb, "insertRejectedServerRecordRequest");
        const bool inserted = sqlite3_changes(mDb) != 0;
        sqlite3_finalize(s);
        return inserted;
    }

    void PlayerDatabase::completeCraftRequest(int64_t accountId, int64_t characterId, std::string_view requestId,
        std::string_view requestHash, std::string_view status, std::string_view resultPayload)
    {
        if (status != "accepted" && status != "rejected")
            throw std::invalid_argument("[PlayerDB] craft request terminal status must be accepted or rejected");

        sqlite3_stmt* s = prepare(
            "UPDATE craft_requests SET status=?1, result_payload=?2, updated_at=?3"
            " WHERE account_id=?4 AND character_id=?5 AND request_id=?6 AND request_hash=?7 AND status='pending'");
        sqlite3_bind_text(s, 1, status.data(), static_cast<int>(status.size()), SQLITE_TRANSIENT);
        sqlite3_bind_blob(s, 2, resultPayload.data(), static_cast<int>(resultPayload.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int64(s, 3, static_cast<int64_t>(std::time(nullptr)));
        sqlite3_bind_int64(s, 4, accountId);
        sqlite3_bind_int64(s, 5, characterId);
        sqlite3_bind_text(s, 6, requestId.data(), static_cast<int>(requestId.size()), SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 7, requestHash.data(), static_cast<int>(requestHash.size()), SQLITE_TRANSIENT);
        checkSqlite(sqlite3_step(s), mDb, "completeCraftRequest");
        const bool updated = sqlite3_changes(mDb) != 0;
        sqlite3_finalize(s);
        if (!updated)
            throw std::runtime_error("[PlayerDB] craft request completion did not match one pending request");
    }

    PlayerTopicState PlayerDatabase::loadPlayerTopicState(int64_t characterId)
    {
        PlayerTopicState state;
        sqlite3_stmt* revision = prepare(
            "SELECT s.revision FROM characters c LEFT JOIN character_topic_state s ON s.character_id=c.id"
            " WHERE c.id=?1");
        sqlite3_bind_int64(revision, 1, characterId);
        if (sqlite3_step(revision) == SQLITE_ROW && sqlite3_column_type(revision, 0) != SQLITE_NULL)
        {
            const sqlite3_int64 value = sqlite3_column_int64(revision, 0);
            if (value < 0)
            {
                sqlite3_finalize(revision);
                throw std::runtime_error("[PlayerDB] invalid persisted topic revision");
            }
            state.revision = static_cast<std::uint64_t>(value);
        }
        sqlite3_finalize(revision);

        sqlite3_stmt* topics = prepare(
            "SELECT topic_id FROM character_known_topics WHERE character_id=?1 ORDER BY topic_id");
        sqlite3_bind_int64(topics, 1, characterId);
        while (sqlite3_step(topics) == SQLITE_ROW)
        {
            const char* id = reinterpret_cast<const char*>(sqlite3_column_text(topics, 0));
            if (id)
                state.knownTopicIds.emplace_back(id);
        }
        sqlite3_finalize(topics);
        if (const TopicStateError error = validatePlayerTopicState(state); error != TopicStateError::None)
        {
            throw std::runtime_error(
                "[PlayerDB] invalid persisted topic state: " + std::string(getTopicStateErrorCode(error)));
        }
        return state;
    }

    TopicMutationResult PlayerDatabase::addKnownTopics(
        int64_t characterId, uint64_t expectedRevision, const std::vector<std::string>& topicIds)
    {
        const std::vector<std::string> additions = canonicalizeTopicIds(topicIds);
        PlayerTopicState proposal;
        proposal.revision = expectedRevision;
        proposal.knownTopicIds = additions;
        if (const TopicStateError error = validatePlayerTopicState(proposal); error != TopicStateError::None)
        {
            throw std::runtime_error(
                "[PlayerDB] invalid topic proposal: " + std::string(getTopicStateErrorCode(error)));
        }
        exec("BEGIN IMMEDIATE");
        try
        {
            TopicMutationResult result;
            result.state = loadPlayerTopicState(characterId);
            if (result.state.revision != expectedRevision)
            {
                result.status = TopicMutationStatus::StaleRevision;
                exec("COMMIT");
                return result;
            }

            std::vector<std::string> merged = result.state.knownTopicIds;
            merged.insert(merged.end(), additions.begin(), additions.end());
            merged = canonicalizeTopicIds(merged);
            PlayerTopicState candidate = result.state;
            candidate.knownTopicIds = merged;
            if (const TopicStateError error = validatePlayerTopicState(candidate); error != TopicStateError::None)
            {
                throw std::runtime_error(
                    "[PlayerDB] invalid resulting topic state: " + std::string(getTopicStateErrorCode(error)));
            }
            if (merged == result.state.knownTopicIds)
            {
                result.status = TopicMutationStatus::Idempotent;
                exec("COMMIT");
                return result;
            }
            if (result.state.revision >= MaximumPersistedRevision)
                throw std::runtime_error("[PlayerDB] topic state limit exceeded");

            sqlite3_stmt* insert = prepare(
                "INSERT OR IGNORE INTO character_known_topics(character_id, topic_id) VALUES(?1, ?2)");
            for (const std::string& id : additions)
            {
                sqlite3_bind_int64(insert, 1, characterId);
                sqlite3_bind_text(insert, 2, id.c_str(), static_cast<int>(id.size()), SQLITE_TRANSIENT);
                checkSqlite(sqlite3_step(insert), mDb, "addKnownTopics(topic)");
                sqlite3_reset(insert);
                sqlite3_clear_bindings(insert);
            }
            sqlite3_finalize(insert);

            ++result.state.revision;
            result.state.knownTopicIds = std::move(merged);
            sqlite3_stmt* update = prepare(
                "INSERT INTO character_topic_state(character_id, revision, updated_at) VALUES(?1, ?2, ?3)"
                " ON CONFLICT(character_id) DO UPDATE SET revision=excluded.revision, updated_at=excluded.updated_at");
            sqlite3_bind_int64(update, 1, characterId);
            sqlite3_bind_int64(update, 2, static_cast<sqlite3_int64>(result.state.revision));
            sqlite3_bind_int64(update, 3, static_cast<sqlite3_int64>(std::time(nullptr)));
            checkSqlite(sqlite3_step(update), mDb, "addKnownTopics(state)");
            sqlite3_finalize(update);

            result.status = TopicMutationStatus::Committed;
            exec("COMMIT");
            return result;
        }
        catch (...)
        {
            try
            {
                exec("ROLLBACK");
            }
            catch (...)
            {
            }
            throw;
        }
    }

    PlayerFactionState PlayerDatabase::loadPlayerFactionState(int64_t characterId)
    {
        sqlite3_stmt* revision = prepare(
            "SELECT s.revision FROM characters c LEFT JOIN character_faction_state s ON s.character_id=c.id"
            " WHERE c.id=?1 LIMIT 1");
        sqlite3_bind_int64(revision, 1, characterId);
        const int revisionRc = sqlite3_step(revision);
        if (revisionRc != SQLITE_ROW)
        {
            sqlite3_finalize(revision);
            if (revisionRc == SQLITE_DONE)
                throw std::runtime_error("[PlayerDB] character not found while loading faction state");
            checkSqlite(revisionRc, mDb, "loadPlayerFactionState(revision)");
        }

        PlayerFactionState state;
        if (sqlite3_column_type(revision, 0) != SQLITE_NULL)
        {
            const sqlite3_int64 value = sqlite3_column_int64(revision, 0);
            if (value < 0)
            {
                sqlite3_finalize(revision);
                throw std::runtime_error("[PlayerDB] invalid persisted faction revision");
            }
            state.revision = static_cast<std::uint64_t>(value);
        }
        sqlite3_finalize(revision);

        sqlite3_stmt* factions = prepare(
            "SELECT faction_id, rank, reputation, expelled FROM character_factions"
            " WHERE character_id=?1 ORDER BY faction_id");
        sqlite3_bind_int64(factions, 1, characterId);
        while (sqlite3_step(factions) == SQLITE_ROW)
        {
            const char* id = reinterpret_cast<const char*>(sqlite3_column_text(factions, 0));
            PlayerFactionEntry entry;
            entry.factionId = id ? id : "";
            entry.rank = sqlite3_column_int(factions, 1);
            entry.reputation = sqlite3_column_int(factions, 2);
            entry.expelled = sqlite3_column_int(factions, 3) != 0;
            state.factions.push_back(std::move(entry));
        }
        sqlite3_finalize(factions);
        if (const FactionError error = validatePlayerFactionState(state); error != FactionError::None)
        {
            throw std::runtime_error(
                "[PlayerDB] invalid persisted faction state: " + std::string(getFactionErrorCode(error)));
        }
        return state;
    }

    PlayerCrimeState PlayerDatabase::loadPlayerCrimeState(int64_t characterId)
    {
        sqlite3_stmt* statement = prepare(
            "SELECT s.bounty, s.current_crime_id, s.paid_crime_id, s.revision"
            " FROM characters c LEFT JOIN character_crime_state s ON s.character_id=c.id"
            " WHERE c.id=?1 LIMIT 1");
        sqlite3_bind_int64(statement, 1, characterId);
        const int rc = sqlite3_step(statement);
        if (rc != SQLITE_ROW)
        {
            sqlite3_finalize(statement);
            if (rc == SQLITE_DONE)
                throw std::runtime_error("[PlayerDB] character not found while loading crime state");
            checkSqlite(rc, mDb, "loadPlayerCrimeState");
        }

        PlayerCrimeState state;
        if (sqlite3_column_type(statement, 0) != SQLITE_NULL)
        {
            if (sqlite3_column_type(statement, 0) != SQLITE_INTEGER
                || sqlite3_column_type(statement, 1) != SQLITE_INTEGER
                || sqlite3_column_type(statement, 2) != SQLITE_INTEGER
                || sqlite3_column_type(statement, 3) != SQLITE_INTEGER)
            {
                sqlite3_finalize(statement);
                throw std::runtime_error("[PlayerDB] persisted crime state has non-integer fields");
            }

            const sqlite3_int64 bounty = sqlite3_column_int64(statement, 0);
            const sqlite3_int64 currentCrimeId = sqlite3_column_int64(statement, 1);
            const sqlite3_int64 paidCrimeId = sqlite3_column_int64(statement, 2);
            const sqlite3_int64 revision = sqlite3_column_int64(statement, 3);
            if (bounty < 0 || bounty > std::numeric_limits<std::int32_t>::max()
                || currentCrimeId < -1 || currentCrimeId > std::numeric_limits<std::int32_t>::max()
                || paidCrimeId < -1 || paidCrimeId > std::numeric_limits<std::int32_t>::max()
                || revision < 0)
            {
                sqlite3_finalize(statement);
                throw std::runtime_error("[PlayerDB] persisted crime state is out of range");
            }
            state.bounty = static_cast<std::int32_t>(bounty);
            state.currentCrimeId = static_cast<std::int32_t>(currentCrimeId);
            state.paidCrimeId = static_cast<std::int32_t>(paidCrimeId);
            state.revision = static_cast<std::uint64_t>(revision);
        }
        sqlite3_finalize(statement);
        if (const CrimeError error = validatePlayerCrimeState(state); error != CrimeError::None)
            throw std::runtime_error("[PlayerDB] invalid persisted crime state: " + std::string(getCrimeErrorCode(error)));
        return state;
    }

    std::optional<SemanticRequestRecord> PlayerDatabase::loadSemanticRequest(std::string_view service,
        int64_t accountId, int64_t characterId, std::string_view requestId)
    {
        sqlite3_stmt* statement = prepare(
            "SELECT request_hash, status, error_code, result_payload, source, created_at, updated_at"
            " FROM semantic_requests"
            " WHERE service=?1 AND account_id=?2 AND character_id=?3 AND request_id=?4 LIMIT 1");
        sqlite3_bind_text(statement, 1, service.data(), static_cast<int>(service.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int64(statement, 2, accountId);
        sqlite3_bind_int64(statement, 3, characterId);
        sqlite3_bind_text(statement, 4, requestId.data(), static_cast<int>(requestId.size()), SQLITE_TRANSIENT);

        std::optional<SemanticRequestRecord> result;
        if (sqlite3_step(statement) == SQLITE_ROW)
        {
            auto text = [&](int column) {
                const auto* value = reinterpret_cast<const char*>(sqlite3_column_text(statement, column));
                return value ? std::string(value, static_cast<std::size_t>(sqlite3_column_bytes(statement, column)))
                             : std::string{};
            };
            SemanticRequestRecord record;
            record.service = std::string(service);
            record.accountId = accountId;
            record.characterId = characterId;
            record.requestId = std::string(requestId);
            record.requestHash = text(0);
            record.status = text(1);
            record.errorCode = static_cast<std::uint16_t>(sqlite3_column_int(statement, 2));
            const void* payload = sqlite3_column_blob(statement, 3);
            const int payloadSize = sqlite3_column_bytes(statement, 3);
            if (payload != nullptr && payloadSize > 0)
                record.resultPayload.assign(static_cast<const char*>(payload), static_cast<std::size_t>(payloadSize));
            record.source = text(4);
            record.createdAt = sqlite3_column_int64(statement, 5);
            record.updatedAt = sqlite3_column_int64(statement, 6);
            result = std::move(record);
        }
        sqlite3_finalize(statement);
        return result;
    }

    bool PlayerDatabase::insertRejectedSemanticRequest(const SemanticRequestRecord& request)
    {
        if (request.service.empty() || request.accountId <= 0 || request.characterId <= 0
            || request.requestId.empty() || request.requestHash.empty() || request.resultPayload.empty()
            || request.status != "rejected")
            throw std::invalid_argument("[PlayerDB] invalid rejected semantic request");

        const int64_t now = static_cast<int64_t>(std::time(nullptr));
        sqlite3_stmt* statement = prepare(
            "INSERT OR IGNORE INTO semantic_requests(service, account_id, character_id, request_id, request_hash,"
            " status, error_code, result_payload, source, created_at, updated_at)"
            " VALUES(?1, ?2, ?3, ?4, ?5, 'rejected', ?6, ?7, ?8, ?9, ?9)");
        sqlite3_bind_text(statement, 1, request.service.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(statement, 2, request.accountId);
        sqlite3_bind_int64(statement, 3, request.characterId);
        sqlite3_bind_text(statement, 4, request.requestId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 5, request.requestHash.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(statement, 6, request.errorCode);
        sqlite3_bind_blob(statement, 7, request.resultPayload.data(),
            static_cast<int>(request.resultPayload.size()), SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 8, request.source.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(statement, 9, request.createdAt != 0 ? request.createdAt : now);
        checkSqlite(sqlite3_step(statement), mDb, "insertRejectedSemanticRequest");
        const bool inserted = sqlite3_changes(mDb) != 0;
        sqlite3_finalize(statement);
        return inserted;
    }

    CrimeCommitResult PlayerDatabase::commitPlayerCrimeMutationInTransaction(const CrimeMutationCommit& commit)
    {
        if (commit.service.empty() || commit.service.size() > 64 || commit.service.find('\0') != std::string::npos
            || commit.accountId <= 0 || commit.characterId <= 0 || commit.requestId.empty()
            || commit.requestHash.empty() || commit.resultPayload.empty() || commit.source.empty()
            || validatePlayerCrimeState(commit.resultingState) != CrimeError::None
            || commit.expectedRevision > MaximumPersistedRevision
            || (commit.resultingState.revision != commit.expectedRevision
                && (commit.expectedRevision >= MaximumPersistedRevision
                    || commit.resultingState.revision != commit.expectedRevision + 1)))
            throw std::invalid_argument("[PlayerDB] invalid player crime mutation commit");

        CrimeCommitResult result;
        sqlite3_stmt* existing = prepare(
            "SELECT request_hash, result_payload FROM semantic_requests"
            " WHERE service=?1 AND account_id=?2 AND character_id=?3 AND request_id=?4");
        sqlite3_bind_text(existing, 1, commit.service.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(existing, 2, commit.accountId);
        sqlite3_bind_int64(existing, 3, commit.characterId);
        sqlite3_bind_text(existing, 4, commit.requestId.c_str(), -1, SQLITE_TRANSIENT);
        const int existingRc = sqlite3_step(existing);
        if (existingRc == SQLITE_ROW)
        {
            const char* hash = reinterpret_cast<const char*>(sqlite3_column_text(existing, 0));
            const bool sameHash = hash != nullptr && commit.requestHash == hash;
            const void* payload = sqlite3_column_blob(existing, 1);
            const int payloadSize = sqlite3_column_bytes(existing, 1);
            if (payload != nullptr && payloadSize > 0)
                result.storedResultPayload.assign(
                    static_cast<const char*>(payload), static_cast<std::size_t>(payloadSize));
            sqlite3_finalize(existing);
            result.status = sameHash ? CrimeCommitStatus::DuplicateRequest
                                     : CrimeCommitStatus::DuplicateRequestConflict;
            return result;
        }
        checkSqlite(existingRc, mDb, "commitPlayerCrimeMutation(existing)");
        sqlite3_finalize(existing);

        sqlite3_stmt* identity = prepare(
            "SELECT 1 FROM characters WHERE id=?1 AND account_id=?2 LIMIT 1");
        sqlite3_bind_int64(identity, 1, commit.characterId);
        sqlite3_bind_int64(identity, 2, commit.accountId);
        const int identityRc = sqlite3_step(identity);
        sqlite3_finalize(identity);
        if (identityRc != SQLITE_ROW)
            throw std::runtime_error("[PlayerDB] crime mutation character/account mismatch");

        result.currentState = loadPlayerCrimeState(commit.characterId);
        if (result.currentState.revision != commit.expectedRevision)
        {
            result.status = CrimeCommitStatus::StaleRevision;
            return result;
        }

        const bool advancesRevision = commit.resultingState.revision == commit.expectedRevision + 1;
        if (!advancesRevision && commit.resultingState != result.currentState)
            throw std::invalid_argument("[PlayerDB] revision-preserving crime commit changed state");

        const int64_t now = static_cast<int64_t>(std::time(nullptr));
        if (advancesRevision)
        {
            sqlite3_stmt* state = prepare(
                "INSERT INTO character_crime_state(character_id, bounty, current_crime_id, paid_crime_id, revision,"
                " updated_at) VALUES(?1, ?2, ?3, ?4, ?5, ?6)"
                " ON CONFLICT(character_id) DO UPDATE SET bounty=excluded.bounty,"
                " current_crime_id=excluded.current_crime_id, paid_crime_id=excluded.paid_crime_id,"
                " revision=excluded.revision, updated_at=excluded.updated_at"
                " WHERE character_crime_state.revision=?7");
            sqlite3_bind_int64(state, 1, commit.characterId);
            sqlite3_bind_int(state, 2, commit.resultingState.bounty);
            sqlite3_bind_int(state, 3, commit.resultingState.currentCrimeId);
            sqlite3_bind_int(state, 4, commit.resultingState.paidCrimeId);
            sqlite3_bind_int64(state, 5, static_cast<sqlite3_int64>(commit.resultingState.revision));
            sqlite3_bind_int64(state, 6, now);
            sqlite3_bind_int64(state, 7, static_cast<sqlite3_int64>(commit.expectedRevision));
            checkSqlite(sqlite3_step(state), mDb, "commitPlayerCrimeMutation(state)");
            const bool stateWritten = sqlite3_changes(mDb) == 1;
            sqlite3_finalize(state);
            if (!stateWritten)
                throw std::runtime_error("[PlayerDB] crime revision changed during commit");

            if (commit.failurePoint == CrimeCommitFailurePoint::AfterStateWrite)
                throw std::runtime_error("[PlayerDB] injected failure after crime state write");
        }

        sqlite3_stmt* request = prepare(
            "INSERT INTO semantic_requests(service, account_id, character_id, request_id, request_hash, status,"
            " error_code, result_payload, source, created_at, updated_at)"
            " VALUES(?1, ?2, ?3, ?4, ?5, 'accepted', 0, ?6, ?7, ?8, ?8)");
        sqlite3_bind_text(request, 1, commit.service.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(request, 2, commit.accountId);
        sqlite3_bind_int64(request, 3, commit.characterId);
        sqlite3_bind_text(request, 4, commit.requestId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(request, 5, commit.requestHash.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_blob(request, 6, commit.resultPayload.data(),
            static_cast<int>(commit.resultPayload.size()), SQLITE_TRANSIENT);
        sqlite3_bind_text(request, 7, commit.source.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(request, 8, now);
        checkSqlite(sqlite3_step(request), mDb, "commitPlayerCrimeMutation(request)");
        sqlite3_finalize(request);

        result.status = CrimeCommitStatus::Committed;
        result.currentState = commit.resultingState;
        return result;
    }

    CrimeCommitResult PlayerDatabase::commitPlayerCrimeMutation(const CrimeMutationCommit& commit)
    {
        exec("BEGIN IMMEDIATE");
        try
        {
            CrimeCommitResult result = commitPlayerCrimeMutationInTransaction(commit);
            exec("COMMIT");
            return result;
        }
        catch (...)
        {
            try { exec("ROLLBACK"); } catch (...) {}
            throw;
        }
    }

    FactionCommitResult PlayerDatabase::commitPlayerFactionMutation(const FactionMutationCommit& commit)
    {
        if (commit.accountId <= 0 || commit.characterId <= 0 || commit.requestId.empty()
            || commit.requestHash.empty() || commit.resultPayload.empty() || commit.source.empty()
            || validatePlayerFactionState(commit.resultingState) != FactionError::None
            || commit.expectedRevision >= MaximumPersistedRevision
            || commit.resultingState.revision != commit.expectedRevision + 1)
            throw std::invalid_argument("[PlayerDB] invalid player faction mutation commit");

        FactionCommitResult result;
        exec("BEGIN IMMEDIATE");
        try
        {
            sqlite3_stmt* existing = prepare(
                "SELECT request_hash, result_payload FROM semantic_requests"
                " WHERE service='faction' AND account_id=?1 AND character_id=?2 AND request_id=?3");
            sqlite3_bind_int64(existing, 1, commit.accountId);
            sqlite3_bind_int64(existing, 2, commit.characterId);
            sqlite3_bind_text(existing, 3, commit.requestId.c_str(), -1, SQLITE_TRANSIENT);
            const int existingRc = sqlite3_step(existing);
            if (existingRc == SQLITE_ROW)
            {
                const char* hash = reinterpret_cast<const char*>(sqlite3_column_text(existing, 0));
                const bool sameHash = hash != nullptr && commit.requestHash == hash;
                const void* payload = sqlite3_column_blob(existing, 1);
                const int payloadSize = sqlite3_column_bytes(existing, 1);
                if (payload != nullptr && payloadSize > 0)
                    result.storedResultPayload.assign(
                        static_cast<const char*>(payload), static_cast<std::size_t>(payloadSize));
                sqlite3_finalize(existing);
                exec("ROLLBACK");
                result.status = sameHash ? FactionCommitStatus::DuplicateRequest
                                         : FactionCommitStatus::DuplicateRequestConflict;
                return result;
            }
            checkSqlite(existingRc, mDb, "commitPlayerFactionMutation(existing)");
            sqlite3_finalize(existing);

            sqlite3_stmt* identity = prepare(
                "SELECT 1 FROM characters WHERE id=?1 AND account_id=?2 LIMIT 1");
            sqlite3_bind_int64(identity, 1, commit.characterId);
            sqlite3_bind_int64(identity, 2, commit.accountId);
            const int identityRc = sqlite3_step(identity);
            sqlite3_finalize(identity);
            if (identityRc != SQLITE_ROW)
                throw std::runtime_error("[PlayerDB] faction mutation character/account mismatch");

            result.currentState = loadPlayerFactionState(commit.characterId);
            if (result.currentState.revision != commit.expectedRevision)
            {
                exec("ROLLBACK");
                result.status = FactionCommitStatus::StaleRevision;
                return result;
            }

            const int64_t now = static_cast<int64_t>(std::time(nullptr));
            sqlite3_stmt* state = prepare(
                "INSERT INTO character_faction_state(character_id, revision, updated_at) VALUES(?1, ?2, ?3)"
                " ON CONFLICT(character_id) DO UPDATE SET revision=excluded.revision, updated_at=excluded.updated_at"
                " WHERE character_faction_state.revision=?4");
            sqlite3_bind_int64(state, 1, commit.characterId);
            sqlite3_bind_int64(state, 2, static_cast<sqlite3_int64>(commit.resultingState.revision));
            sqlite3_bind_int64(state, 3, now);
            sqlite3_bind_int64(state, 4, static_cast<sqlite3_int64>(commit.expectedRevision));
            checkSqlite(sqlite3_step(state), mDb, "commitPlayerFactionMutation(state)");
            const bool stateWritten = sqlite3_changes(mDb) == 1;
            sqlite3_finalize(state);
            if (!stateWritten)
                throw std::runtime_error("[PlayerDB] faction revision changed during commit");

            sqlite3_stmt* clear = prepare("DELETE FROM character_factions WHERE character_id=?1");
            sqlite3_bind_int64(clear, 1, commit.characterId);
            checkSqlite(sqlite3_step(clear), mDb, "commitPlayerFactionMutation(clear)");
            sqlite3_finalize(clear);

            sqlite3_stmt* insert = prepare(
                "INSERT INTO character_factions(character_id, faction_id, rank, reputation, expelled)"
                " VALUES(?1, ?2, ?3, ?4, ?5)");
            for (const PlayerFactionEntry& entry : commit.resultingState.factions)
            {
                sqlite3_bind_int64(insert, 1, commit.characterId);
                sqlite3_bind_text(insert, 2, entry.factionId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(insert, 3, entry.rank);
                sqlite3_bind_int(insert, 4, entry.reputation);
                sqlite3_bind_int(insert, 5, entry.expelled ? 1 : 0);
                checkSqlite(sqlite3_step(insert), mDb, "commitPlayerFactionMutation(entry)");
                sqlite3_reset(insert);
                sqlite3_clear_bindings(insert);
            }
            sqlite3_finalize(insert);

            if (commit.failurePoint == FactionCommitFailurePoint::AfterStateWrite)
                throw std::runtime_error("[PlayerDB] injected failure after faction state write");

            sqlite3_stmt* request = prepare(
                "INSERT INTO semantic_requests(service, account_id, character_id, request_id, request_hash, status,"
                " error_code, result_payload, source, created_at, updated_at)"
                " VALUES('faction', ?1, ?2, ?3, ?4, 'accepted', 0, ?5, ?6, ?7, ?7)");
            sqlite3_bind_int64(request, 1, commit.accountId);
            sqlite3_bind_int64(request, 2, commit.characterId);
            sqlite3_bind_text(request, 3, commit.requestId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(request, 4, commit.requestHash.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_blob(request, 5, commit.resultPayload.data(),
                static_cast<int>(commit.resultPayload.size()), SQLITE_TRANSIENT);
            sqlite3_bind_text(request, 6, commit.source.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(request, 7, now);
            checkSqlite(sqlite3_step(request), mDb, "commitPlayerFactionMutation(request)");
            sqlite3_finalize(request);

            exec("COMMIT");
            result.status = FactionCommitStatus::Committed;
            result.currentState = commit.resultingState;
            return result;
        }
        catch (...)
        {
            try
            {
                exec("ROLLBACK");
            }
            catch (...)
            {
            }
            throw;
        }
    }

    uint64_t PlayerDatabase::loadInventoryRevision(int64_t characterId)
    {
        sqlite3_stmt* s = prepare("SELECT inventory_revision FROM characters WHERE id=?1");
        sqlite3_bind_int64(s, 1, characterId);
        const int rc = sqlite3_step(s);
        if (rc != SQLITE_ROW)
        {
            sqlite3_finalize(s);
            throw std::runtime_error("[PlayerDB] character not found while loading inventory revision");
        }
        const uint64_t revision = static_cast<uint64_t>(sqlite3_column_int64(s, 0));
        sqlite3_finalize(s);
        return revision;
    }

    DynamicRecordCommitStatus PlayerDatabase::commitDynamicRecordRequest(const DynamicRecordCommit& commit)
    {
        const bool serverRequest = !commit.serverSource.empty();
        const bool validIdentity = serverRequest
            ? commit.accountId == 0 && commit.characterId == 0
            : commit.accountId > 0 && commit.characterId > 0;
        if (!validIdentity || commit.requestId.empty() || commit.requestHash.empty() || commit.resultPayload.empty())
            throw std::invalid_argument("[PlayerDB] invalid dynamic record commit identity");

        exec("BEGIN IMMEDIATE");
        try
        {
            sqlite3_stmt* existing = prepare(serverRequest
                    ? "SELECT request_hash FROM server_record_requests WHERE source=?1 AND request_id=?2"
                    : "SELECT request_hash FROM craft_requests"
                      " WHERE account_id=?1 AND character_id=?2 AND request_id=?3");
            if (serverRequest)
            {
                sqlite3_bind_text(existing, 1, commit.serverSource.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(existing, 2, commit.requestId.c_str(), -1, SQLITE_TRANSIENT);
            }
            else
            {
                sqlite3_bind_int64(existing, 1, commit.accountId);
                sqlite3_bind_int64(existing, 2, commit.characterId);
                sqlite3_bind_text(existing, 3, commit.requestId.c_str(), -1, SQLITE_TRANSIENT);
            }
            const int existingRc = sqlite3_step(existing);
            if (existingRc == SQLITE_ROW)
            {
                const char* hash = reinterpret_cast<const char*>(sqlite3_column_text(existing, 0));
                const bool sameHash = hash != nullptr && commit.requestHash == hash;
                sqlite3_finalize(existing);
                exec("ROLLBACK");
                return sameHash ? DynamicRecordCommitStatus::DuplicateRequest
                                : DynamicRecordCommitStatus::DuplicateRequestConflict;
            }
            checkSqlite(existingRc, mDb, "commitDynamicRecordRequest(existing)");
            sqlite3_finalize(existing);

            if (!serverRequest)
            {
                sqlite3_stmt* revisionStmt = prepare(
                    "SELECT inventory_revision FROM characters WHERE id=?1 AND account_id=?2");
                sqlite3_bind_int64(revisionStmt, 1, commit.characterId);
                sqlite3_bind_int64(revisionStmt, 2, commit.accountId);
                const int revisionRc = sqlite3_step(revisionStmt);
                if (revisionRc != SQLITE_ROW)
                {
                    sqlite3_finalize(revisionStmt);
                    if (revisionRc == SQLITE_DONE)
                        throw std::runtime_error("[PlayerDB] dynamic record commit character/account mismatch");
                    checkSqlite(revisionRc, mDb, "commitDynamicRecordRequest(revision)");
                }
                const uint64_t currentRevision = static_cast<uint64_t>(sqlite3_column_int64(revisionStmt, 0));
                sqlite3_finalize(revisionStmt);
                if (currentRevision != commit.expectedInventoryRevision)
                {
                    exec("ROLLBACK");
                    return DynamicRecordCommitStatus::StaleInventoryRevision;
                }
            }

            const int64_t now = static_cast<int64_t>(std::time(nullptr));
            sqlite3_stmt* persisted = prepare(
                "INSERT INTO world_dynamic_records(record_type, record_id, record_scope, record_data, created_at,"
                " updated_at, schema_version) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7)"
                " ON CONFLICT(record_type, record_id) DO UPDATE SET"
                " record_scope=excluded.record_scope, record_data=excluded.record_data,"
                " updated_at=excluded.updated_at, schema_version=excluded.schema_version");
            sqlite3_stmt* catalog = prepare(
                "INSERT INTO world_dynamic_record_catalog(record_type, record_id, record_scope, is_persistent,"
                " created_at, updated_at, definition_fingerprint, creator_account_id, creator_character_id,"
                " creation_source, schema_version, validation_version)"
                " VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12)"
                " ON CONFLICT(record_type, record_id) DO UPDATE SET"
                " record_scope=excluded.record_scope, is_persistent=excluded.is_persistent,"
                " updated_at=excluded.updated_at, definition_fingerprint=excluded.definition_fingerprint,"
                " creator_account_id=excluded.creator_account_id,"
                " creator_character_id=excluded.creator_character_id, creation_source=excluded.creation_source,"
                " schema_version=excluded.schema_version, validation_version=excluded.validation_version");
            sqlite3_stmt* clearDependencies = prepare(
                "DELETE FROM world_dynamic_record_links"
                " WHERE link_kind='record_dependency' AND owner_a=?1 AND owner_b=?2 AND owner_c=''");
            sqlite3_stmt* dependency = prepare(
                "INSERT OR REPLACE INTO world_dynamic_record_links(record_id, link_kind, owner_a, owner_b, owner_c,"
                " owner_index) VALUES(?1, 'record_dependency', ?2, ?3, '', ?4)");

            for (const DynamicRecordCommitEntry& entry : commit.records)
            {
                const PersistedDynamicRecord& record = entry.record;
                const DynamicRecordCatalogEntry& metadata = entry.catalog;
                if (record.recordType.empty() || record.recordId.empty())
                    throw std::invalid_argument("[PlayerDB] atomic record commits require identified records");

                if (metadata.persistent)
                {
                    sqlite3_bind_text(persisted, 1, record.recordType.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(persisted, 2, record.recordId.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(persisted, 3, record.recordScope.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_blob(
                        persisted, 4, record.data.data(), static_cast<int>(record.data.size()), SQLITE_TRANSIENT);
                    sqlite3_bind_int64(persisted, 5, record.createdAt != 0 ? record.createdAt : now);
                    sqlite3_bind_int64(persisted, 6, record.updatedAt != 0 ? record.updatedAt : now);
                    sqlite3_bind_int(persisted, 7, record.schemaVersion);
                    checkSqlite(sqlite3_step(persisted), mDb, "commitDynamicRecordRequest(record)");
                    sqlite3_reset(persisted);
                    sqlite3_clear_bindings(persisted);
                }
                else
                    deleteDynamicRecord(record.recordType, record.recordId);

                sqlite3_bind_text(catalog, 1, metadata.recordType.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(catalog, 2, metadata.recordId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(catalog, 3, metadata.recordScope.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(catalog, 4, metadata.persistent ? 1 : 0);
                sqlite3_bind_int64(catalog, 5, metadata.createdAt != 0 ? metadata.createdAt : now);
                sqlite3_bind_int64(catalog, 6, metadata.updatedAt != 0 ? metadata.updatedAt : now);
                sqlite3_bind_text(catalog, 7, metadata.definitionFingerprint.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(catalog, 8, metadata.creatorAccountId);
                sqlite3_bind_int64(catalog, 9, metadata.creatorCharacterId);
                sqlite3_bind_text(catalog, 10, metadata.creationSource.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(catalog, 11, metadata.schemaVersion);
                sqlite3_bind_int(catalog, 12, metadata.validationVersion);
                checkSqlite(sqlite3_step(catalog), mDb, "commitDynamicRecordRequest(catalog)");
                sqlite3_reset(catalog);
                sqlite3_clear_bindings(catalog);

                sqlite3_bind_text(clearDependencies, 1, record.recordType.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(clearDependencies, 2, record.recordId.c_str(), -1, SQLITE_TRANSIENT);
                checkSqlite(sqlite3_step(clearDependencies), mDb, "commitDynamicRecordRequest(clearDependencies)");
                sqlite3_reset(clearDependencies);
                sqlite3_clear_bindings(clearDependencies);

                int64_t dependencyIndex = 0;
                for (const std::string& dependencyId : entry.dependencyRecordIds)
                {
                    sqlite3_bind_text(dependency, 1, dependencyId.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(dependency, 2, record.recordType.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(dependency, 3, record.recordId.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int64(dependency, 4, dependencyIndex++);
                    checkSqlite(sqlite3_step(dependency), mDb, "commitDynamicRecordRequest(dependency)");
                    sqlite3_reset(dependency);
                    sqlite3_clear_bindings(dependency);
                }
            }
            sqlite3_finalize(persisted);
            sqlite3_finalize(catalog);
            sqlite3_finalize(clearDependencies);
            sqlite3_finalize(dependency);

            if (commit.inventory)
            {
                const std::string characterKey = std::to_string(commit.characterId);
                sqlite3_stmt* clearInventory = prepare("DELETE FROM character_inventory WHERE character_id=?1");
                sqlite3_bind_int64(clearInventory, 1, commit.characterId);
                checkSqlite(sqlite3_step(clearInventory), mDb, "commitDynamicRecordRequest(clearInventory)");
                sqlite3_finalize(clearInventory);

                sqlite3_stmt* insertInventory = prepare(
                    "INSERT INTO character_inventory(character_id, item_index, ref_id, item_count, charge,"
                    " enchantment_charge, soul, instance_id) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)");
                for (std::size_t i = 0; i < commit.inventory->size(); ++i)
                {
                    const Item& item = (*commit.inventory)[i];
                    sqlite3_bind_int64(insertInventory, 1, commit.characterId);
                    sqlite3_bind_int(insertInventory, 2, static_cast<int>(i));
                    sqlite3_bind_text(insertInventory, 3, item.refId.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int(insertInventory, 4, item.count);
                    sqlite3_bind_int(insertInventory, 5, item.charge);
                    sqlite3_bind_double(insertInventory, 6, item.enchantmentCharge);
                    sqlite3_bind_text(insertInventory, 7, item.soul.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int64(insertInventory, 8, item.instanceId);
                    checkSqlite(sqlite3_step(insertInventory), mDb, "commitDynamicRecordRequest(inventory)");
                    sqlite3_reset(insertInventory);
                    sqlite3_clear_bindings(insertInventory);
                }
                sqlite3_finalize(insertInventory);

                sqlite3_stmt* clearLinks = prepare(
                    "DELETE FROM world_dynamic_record_links"
                    " WHERE link_kind='inventory_item' AND owner_a=?1 AND owner_b='' AND owner_c=''");
                sqlite3_bind_text(clearLinks, 1, characterKey.c_str(), -1, SQLITE_TRANSIENT);
                checkSqlite(sqlite3_step(clearLinks), mDb, "commitDynamicRecordRequest(clearInventoryLinks)");
                sqlite3_finalize(clearLinks);

                sqlite3_stmt* insertLink = prepare(
                    "INSERT OR REPLACE INTO world_dynamic_record_links(record_id, link_kind, owner_a, owner_b, owner_c,"
                    " owner_index) VALUES(?1, 'inventory_item', ?2, '', '', ?3)");
                for (std::size_t i = 0; i < commit.inventory->size(); ++i)
                {
                    const Item& item = (*commit.inventory)[i];
                    sqlite3_bind_text(insertLink, 1, item.refId.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(insertLink, 2, characterKey.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int64(insertLink, 3, item.instanceId != 0 ? item.instanceId : static_cast<int64_t>(i));
                    checkSqlite(sqlite3_step(insertLink), mDb, "commitDynamicRecordRequest(inventoryLink)");
                    sqlite3_reset(insertLink);
                    sqlite3_clear_bindings(insertLink);
                }
                sqlite3_finalize(insertLink);
            }

            if (!serverRequest)
            {
                sqlite3_stmt* updateRevision = prepare(
                    "UPDATE characters SET inventory_revision=?1, inventory_saved=MAX(inventory_saved, ?2),"
                    " last_seen=?3 WHERE id=?4 AND account_id=?5 AND inventory_revision=?6");
                sqlite3_bind_int64(updateRevision, 1, static_cast<sqlite3_int64>(commit.resultingInventoryRevision));
                sqlite3_bind_int(updateRevision, 2, commit.inventory ? 1 : 0);
                sqlite3_bind_int64(updateRevision, 3, now);
                sqlite3_bind_int64(updateRevision, 4, commit.characterId);
                sqlite3_bind_int64(updateRevision, 5, commit.accountId);
                sqlite3_bind_int64(updateRevision, 6, static_cast<sqlite3_int64>(commit.expectedInventoryRevision));
                checkSqlite(sqlite3_step(updateRevision), mDb, "commitDynamicRecordRequest(updateRevision)");
                if (sqlite3_changes(mDb) != 1)
                    throw std::runtime_error("[PlayerDB] inventory revision changed during dynamic record commit");
                sqlite3_finalize(updateRevision);
            }

            if (commit.characterStats)
            {
                // Server-authoritative crafting commits skill progression and
                // level state inside the same transaction as the records,
                // inventory, and journal row.
                writeCharacterStatsRows(commit.characterId, *commit.characterStats);
                sqlite3_stmt* mark = prepare(
                    "UPDATE characters SET stats_saved=1, level=?1, level_progress=?2"
                    " WHERE id=?3 AND account_id=?4");
                sqlite3_bind_int(mark, 1, commit.characterStats->level);
                sqlite3_bind_double(mark, 2, commit.characterStats->levelProgress);
                sqlite3_bind_int64(mark, 3, commit.characterId);
                sqlite3_bind_int64(mark, 4, commit.accountId);
                checkSqlite(sqlite3_step(mark), mDb, "commitDynamicRecordRequest(characterStats)");
                if (sqlite3_changes(mDb) != 1)
                    throw std::runtime_error("[PlayerDB] character identity changed during dynamic record commit");
                sqlite3_finalize(mark);
            }

            sqlite3_stmt* request = prepare(serverRequest
                    ? "INSERT INTO server_record_requests(source, request_id, request_hash, status, result_payload,"
                      " created_at, updated_at) VALUES(?1, ?2, ?3, 'accepted', ?4, ?5, ?5)"
                    : "INSERT INTO craft_requests(account_id, character_id, request_id, request_hash, status,"
                      " result_payload, created_at, updated_at) VALUES(?1, ?2, ?3, ?4, 'accepted', ?5, ?6, ?6)");
            if (serverRequest)
            {
                sqlite3_bind_text(request, 1, commit.serverSource.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(request, 2, commit.requestId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(request, 3, commit.requestHash.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_blob(request, 4, commit.resultPayload.data(),
                    static_cast<int>(commit.resultPayload.size()), SQLITE_TRANSIENT);
                sqlite3_bind_int64(request, 5, now);
            }
            else
            {
                sqlite3_bind_int64(request, 1, commit.accountId);
                sqlite3_bind_int64(request, 2, commit.characterId);
                sqlite3_bind_text(request, 3, commit.requestId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(request, 4, commit.requestHash.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_blob(request, 5, commit.resultPayload.data(),
                    static_cast<int>(commit.resultPayload.size()), SQLITE_TRANSIENT);
                sqlite3_bind_int64(request, 6, now);
            }
            checkSqlite(sqlite3_step(request), mDb, "commitDynamicRecordRequest(request)");
            sqlite3_finalize(request);

            exec("COMMIT");
            return DynamicRecordCommitStatus::Committed;
        }
        catch (...)
        {
            try
            {
                exec("ROLLBACK");
            }
            catch (...)
            {
            }
            throw;
        }
    }

    std::vector<DynamicRecordCatalogEntry> PlayerDatabase::loadDynamicRecordCatalog()
    {
        sqlite3_stmt* s = prepare(
            "SELECT c.record_type, c.record_id, c.record_scope, c.is_persistent, c.created_at, c.updated_at,"
            " c.definition_fingerprint, c.creator_account_id, c.creator_character_id, c.creation_source,"
            " c.schema_version, c.validation_version,"
            " COALESCE(("
            "   SELECT COUNT(*) FROM world_dynamic_record_links l WHERE l.record_id = c.record_id"
            " ), 0)"
            " FROM world_dynamic_record_catalog c ORDER BY c.created_at, c.record_type, c.record_id");

        std::vector<DynamicRecordCatalogEntry> records;
        while (sqlite3_step(s) == SQLITE_ROW)
        {
            DynamicRecordCatalogEntry record;
            auto textCol = [&](int i) -> std::string {
                const char* t = reinterpret_cast<const char*>(sqlite3_column_text(s, i));
                return t ? t : "";
            };

            record.recordType = textCol(0);
            record.recordId = textCol(1);
            record.recordScope = textCol(2);
            record.persistent = sqlite3_column_int(s, 3) != 0;
            record.createdAt = sqlite3_column_int64(s, 4);
            record.updatedAt = sqlite3_column_int64(s, 5);
            record.definitionFingerprint = textCol(6);
            record.creatorAccountId = sqlite3_column_int64(s, 7);
            record.creatorCharacterId = sqlite3_column_int64(s, 8);
            record.creationSource = textCol(9);
            record.schemaVersion = static_cast<uint16_t>(sqlite3_column_int(s, 10));
            record.validationVersion = static_cast<uint16_t>(sqlite3_column_int(s, 11));
            record.linkCount = sqlite3_column_int64(s, 12);
            records.push_back(std::move(record));
        }

        sqlite3_finalize(s);
        return records;
    }

    void PlayerDatabase::upsertDynamicRecordCatalog(const DynamicRecordCatalogEntry& record)
    {
        sqlite3_stmt* s = prepare(
            "INSERT INTO world_dynamic_record_catalog(record_type, record_id, record_scope, is_persistent, created_at, "
            "updated_at, definition_fingerprint, creator_account_id, creator_character_id, creation_source,"
            "schema_version, validation_version)"
            " VALUES(?1, ?2, ?3, ?4, COALESCE(?5, strftime('%s', 'now')), ?6, ?7, ?8, ?9, ?10, ?11, ?12)"
            " ON CONFLICT(record_type, record_id) DO UPDATE SET"
            " record_scope=excluded.record_scope,"
            " is_persistent=excluded.is_persistent,"
            " updated_at=excluded.updated_at,"
            " definition_fingerprint=CASE WHEN excluded.definition_fingerprint=''"
            "   THEN world_dynamic_record_catalog.definition_fingerprint ELSE excluded.definition_fingerprint END,"
            " creator_account_id=CASE WHEN world_dynamic_record_catalog.creator_account_id=0"
            "   THEN excluded.creator_account_id ELSE world_dynamic_record_catalog.creator_account_id END,"
            " creator_character_id=CASE WHEN world_dynamic_record_catalog.creator_character_id=0"
            "   THEN excluded.creator_character_id ELSE world_dynamic_record_catalog.creator_character_id END,"
            " creation_source=CASE WHEN world_dynamic_record_catalog.creation_source=''"
            "   THEN excluded.creation_source ELSE world_dynamic_record_catalog.creation_source END,"
            " schema_version=MAX(world_dynamic_record_catalog.schema_version, excluded.schema_version),"
            " validation_version=MAX(world_dynamic_record_catalog.validation_version, excluded.validation_version)");

        const int64_t createdAt = record.createdAt != 0 ? record.createdAt : static_cast<int64_t>(std::time(nullptr));
        const int64_t updatedAt = record.updatedAt != 0 ? record.updatedAt : static_cast<int64_t>(std::time(nullptr));

        sqlite3_bind_text(s, 1, record.recordType.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 2, record.recordId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 3, record.recordScope.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(s, 4, record.persistent ? 1 : 0);
        sqlite3_bind_int64(s, 5, createdAt);
        sqlite3_bind_int64(s, 6, updatedAt);
        sqlite3_bind_text(s, 7, record.definitionFingerprint.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(s, 8, record.creatorAccountId);
        sqlite3_bind_int64(s, 9, record.creatorCharacterId);
        sqlite3_bind_text(s, 10, record.creationSource.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(s, 11, record.schemaVersion);
        sqlite3_bind_int(s, 12, record.validationVersion);
        checkSqlite(sqlite3_step(s), mDb, "upsertDynamicRecordCatalog");
        sqlite3_finalize(s);
    }

    void PlayerDatabase::deleteDynamicRecordCatalog(std::string_view recordType, std::string_view recordId)
    {
        sqlite3_stmt* s = prepare("DELETE FROM world_dynamic_record_catalog WHERE record_type=?1 AND record_id=?2");
        sqlite3_bind_text(s, 1, recordType.data(), static_cast<int>(recordType.size()), SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 2, recordId.data(), static_cast<int>(recordId.size()), SQLITE_TRANSIENT);
        checkSqlite(sqlite3_step(s), mDb, "deleteDynamicRecordCatalog");
        sqlite3_finalize(s);
    }

    void PlayerDatabase::deleteDynamicRecordLinks(std::string_view recordId)
    {
        sqlite3_stmt* s = prepare("DELETE FROM world_dynamic_record_links WHERE record_id=?1");
        sqlite3_bind_text(s, 1, recordId.data(), static_cast<int>(recordId.size()), SQLITE_TRANSIENT);
        checkSqlite(sqlite3_step(s), mDb, "deleteDynamicRecordLinks");
        sqlite3_finalize(s);
    }

    void PlayerDatabase::upsertSpawnedActorDynamicRecordLink(
        std::string_view recordId, std::string_view cellId, uint32_t mpNum)
    {
        if (recordId.empty() || cellId.empty() || mpNum == 0)
            return;

        const std::string ownerA = std::to_string(mpNum);

        sqlite3_stmt* insertLink = prepare(
            "INSERT OR REPLACE INTO world_dynamic_record_links(record_id, link_kind, owner_a, owner_b, owner_c, "
            "owner_index)"
            " VALUES(?1, ?2, ?3, ?4, ?5, ?6)");
        insertDynamicRecordLink(mDb, insertLink, recordId, "spawned_actor", ownerA, cellId, "", 0);
        sqlite3_finalize(insertLink);
    }

    void PlayerDatabase::deleteSpawnedActorDynamicRecordLink(uint32_t mpNum, std::string_view cellId)
    {
        if (mpNum == 0)
            return;

        const std::string ownerA = std::to_string(mpNum);

        sqlite3_stmt* s = prepare(
            "DELETE FROM world_dynamic_record_links"
            " WHERE link_kind=?1 AND owner_a=?2 AND (?3='' OR owner_b=?3)");
        sqlite3_bind_text(s, 1, "spawned_actor", -1, SQLITE_STATIC);
        sqlite3_bind_text(s, 2, ownerA.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 3, cellId.data(), static_cast<int>(cellId.size()), SQLITE_TRANSIENT);
        checkSqlite(sqlite3_step(s), mDb, "deleteSpawnedActorDynamicRecordLink");
        sqlite3_finalize(s);
    }

    std::size_t PlayerDatabase::deleteSpawnedActorDynamicRecordLinksForCell(std::string_view cellId)
    {
        if (cellId.empty())
            return 0;

        sqlite3_stmt* s = prepare(
            "DELETE FROM world_dynamic_record_links WHERE link_kind=?1 AND owner_b=?2");
        sqlite3_bind_text(s, 1, "spawned_actor", -1, SQLITE_STATIC);
        sqlite3_bind_text(s, 2, cellId.data(), static_cast<int>(cellId.size()), SQLITE_TRANSIENT);
        checkSqlite(sqlite3_step(s), mDb, "deleteSpawnedActorDynamicRecordLinksForCell");
        const std::size_t removed = static_cast<std::size_t>(sqlite3_changes(mDb));
        sqlite3_finalize(s);
        return removed;
    }

    void PlayerDatabase::clearSpawnedActorDynamicRecordLinks()
    {
        sqlite3_stmt* s = prepare("DELETE FROM world_dynamic_record_links WHERE link_kind=?1");
        sqlite3_bind_text(s, 1, "spawned_actor", -1, SQLITE_STATIC);
        checkSqlite(sqlite3_step(s), mDb, "clearSpawnedActorDynamicRecordLinks");
        sqlite3_finalize(s);
    }

    void PlayerDatabase::replaceDynamicRecordDependencies(std::string_view ownerRecordType,
        std::string_view ownerRecordId, const std::vector<std::string>& dependencyRecordIds)
    {
        sqlite3_stmt* clearLinks = prepare(
            "DELETE FROM world_dynamic_record_links"
            " WHERE link_kind=?1 AND owner_a=?2 AND owner_b=?3 AND owner_c=?4");
        clearDynamicRecordLinksForOwner(mDb, clearLinks, "record_dependency", ownerRecordType, ownerRecordId, "");
        sqlite3_finalize(clearLinks);

        if (dependencyRecordIds.empty())
            return;

        sqlite3_stmt* insertLink = prepare(
            "INSERT OR REPLACE INTO world_dynamic_record_links(record_id, link_kind, owner_a, owner_b, owner_c, "
            "owner_index)"
            " VALUES(?1, ?2, ?3, ?4, ?5, ?6)");

        int64_t ownerIndex = 0;
        for (const auto& dependencyRecordId : dependencyRecordIds)
        {
            insertDynamicRecordLink(mDb, insertLink, dependencyRecordId, "record_dependency", ownerRecordType,
                ownerRecordId, "", ownerIndex++);
        }

        sqlite3_finalize(insertLink);
    }

    std::vector<int64_t> PlayerDatabase::listCharactersWithSavedItems()
    {
        sqlite3_stmt* s
            = prepare("SELECT id FROM characters WHERE inventory_saved != 0 OR equipment_saved != 0 ORDER BY id");

        std::vector<int64_t> ids;
        while (sqlite3_step(s) == SQLITE_ROW)
            ids.push_back(sqlite3_column_int64(s, 0));

        sqlite3_finalize(s);
        return ids;
    }

    std::optional<std::string> PlayerDatabase::loadCharacterLuaStorageValue(
        int64_t characterId, std::string_view storageNamespace, std::string_view key)
    {
        if (characterId <= 0 || storageNamespace.empty() || key.empty())
            return std::nullopt;

        sqlite3_stmt* s = prepare(
            "SELECT value FROM character_lua_storage"
            " WHERE character_id=?1 AND storage_namespace=?2 AND storage_key=?3");
        sqlite3_bind_int64(s, 1, characterId);
        sqlite3_bind_text(s, 2, storageNamespace.data(), static_cast<int>(storageNamespace.size()), SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 3, key.data(), static_cast<int>(key.size()), SQLITE_TRANSIENT);

        std::optional<std::string> result;
        const int rc = sqlite3_step(s);
        if (rc == SQLITE_ROW)
        {
            const void* blob = sqlite3_column_blob(s, 0);
            const int bytes = sqlite3_column_bytes(s, 0);
            result = blob && bytes > 0 ? std::string(static_cast<const char*>(blob), static_cast<std::size_t>(bytes))
                                       : std::string();
        }
        else
            checkSqlite(rc, mDb, "loadCharacterLuaStorageValue");

        sqlite3_finalize(s);
        return result;
    }

    void PlayerDatabase::saveCharacterLuaStorageValue(
        int64_t characterId, std::string_view storageNamespace, std::string_view key, std::string_view value)
    {
        if (characterId <= 0 || storageNamespace.empty() || key.empty())
            return;

        sqlite3_stmt* s = prepare(
            "INSERT INTO character_lua_storage(character_id, storage_namespace, storage_key, value, updated_at)"
            " VALUES(?1, ?2, ?3, ?4, ?5)"
            " ON CONFLICT(character_id, storage_namespace, storage_key) DO UPDATE SET"
            " value=excluded.value, updated_at=excluded.updated_at");
        sqlite3_bind_int64(s, 1, characterId);
        sqlite3_bind_text(s, 2, storageNamespace.data(), static_cast<int>(storageNamespace.size()), SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 3, key.data(), static_cast<int>(key.size()), SQLITE_TRANSIENT);
        sqlite3_bind_blob(s, 4, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int64(s, 5, static_cast<int64_t>(std::time(nullptr)));
        checkSqlite(sqlite3_step(s), mDb, "saveCharacterLuaStorageValue");
        sqlite3_finalize(s);
    }

    bool PlayerDatabase::deleteCharacterLuaStorageValue(
        int64_t characterId, std::string_view storageNamespace, std::string_view key)
    {
        if (characterId <= 0 || storageNamespace.empty() || key.empty())
            return false;

        sqlite3_stmt* s = prepare(
            "DELETE FROM character_lua_storage"
            " WHERE character_id=?1 AND storage_namespace=?2 AND storage_key=?3");
        sqlite3_bind_int64(s, 1, characterId);
        sqlite3_bind_text(s, 2, storageNamespace.data(), static_cast<int>(storageNamespace.size()), SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 3, key.data(), static_cast<int>(key.size()), SQLITE_TRANSIENT);
        checkSqlite(sqlite3_step(s), mDb, "deleteCharacterLuaStorageValue");
        const bool removed = sqlite3_changes(mDb) > 0;
        sqlite3_finalize(s);
        return removed;
    }

    std::vector<DatabaseTableInfo> PlayerDatabase::listBrowsableTables()
    {
        std::vector<DatabaseTableInfo> results;
        results.reserve(sizeof(kBrowsableTableDefs) / sizeof(kBrowsableTableDefs[0]));

        for (const BrowsableTableDef& entry : kBrowsableTableDefs)
        {
            const std::string sql = "SELECT COUNT(*) FROM " + std::string(entry.name);
            sqlite3_stmt* s = prepare(sql.c_str());

            DatabaseTableInfo info;
            info.name = entry.name;
            if (sqlite3_step(s) == SQLITE_ROW)
                info.rowCount = sqlite3_column_int64(s, 0);

            sqlite3_finalize(s);
            results.push_back(std::move(info));
        }

        return results;
    }

    std::optional<DatabaseBrowsePage> PlayerDatabase::browseTable(
        std::string_view tableName, int64_t offset, int64_t limit)
    {
        const BrowsableTableDef* definition = findBrowsableTableDef(tableName);
        if (!definition)
            return std::nullopt;

        offset = std::max<int64_t>(0, offset);
        limit = std::clamp<int64_t>(limit <= 0 ? 100 : limit, 1, 500);

        DatabaseBrowsePage page;
        page.tableName = definition->name;
        page.offset = offset;
        page.limit = limit;

        {
            const std::string countSql = "SELECT COUNT(*) FROM " + std::string(definition->name);
            sqlite3_stmt* countStmt = prepare(countSql.c_str());
            if (sqlite3_step(countStmt) == SQLITE_ROW)
                page.totalRows = sqlite3_column_int64(countStmt, 0);
            sqlite3_finalize(countStmt);
        }

        const std::string querySql = "SELECT * FROM " + std::string(definition->name) + " ORDER BY "
            + definition->orderBy + " LIMIT ?1 OFFSET ?2";
        sqlite3_stmt* s = prepare(querySql.c_str());
        sqlite3_bind_int64(s, 1, limit);
        sqlite3_bind_int64(s, 2, offset);

        const int columnCount = sqlite3_column_count(s);
        page.columns.reserve(static_cast<std::size_t>(columnCount));
        for (int i = 0; i < columnCount; ++i)
            page.columns.emplace_back(sqlite3_column_name(s, i));

        int rc = SQLITE_ROW;
        while ((rc = sqlite3_step(s)) == SQLITE_ROW)
        {
            std::vector<std::optional<std::string>> row;
            row.reserve(static_cast<std::size_t>(columnCount));

            for (int i = 0; i < columnCount; ++i)
                row.push_back(sqliteColumnToString(s, i));

            page.rows.push_back(std::move(row));
        }

        sqlite3_finalize(s);
        checkSqlite(rc, mDb, "browseTable");
        return page;
    }

    int64_t PlayerDatabase::addKeypair(int64_t accountId, std::string_view publicKey, std::string_view label)
    {
        sqlite3_stmt* s = prepare(
            "INSERT INTO account_keypairs(account_id, public_key, label, created_at)"
            " VALUES(?1, ?2, ?3, ?4)");
        sqlite3_bind_int64(s, 1, accountId);
        sqlite3_bind_text(s, 2, publicKey.data(), static_cast<int>(publicKey.size()), SQLITE_STATIC);
        sqlite3_bind_text(s, 3, label.data(), static_cast<int>(label.size()), SQLITE_STATIC);
        sqlite3_bind_int64(s, 4, static_cast<int64_t>(std::time(nullptr)));
        checkSqlite(sqlite3_step(s), mDb, "insert keypair");
        sqlite3_finalize(s);
        const int64_t id = sqlite3_last_insert_rowid(mDb);
        Log(Debug::Info) << "[PlayerDB] keypair registered for account=" << accountId << " label='" << label << "'";
        return id;
    }

    std::vector<PlayerDatabase::KeypairEntry> PlayerDatabase::listKeypairs(int64_t accountId)
    {
        std::vector<KeypairEntry> results;
        sqlite3_stmt* s = prepare("SELECT id, public_key, label FROM account_keypairs WHERE account_id=?1");
        sqlite3_bind_int64(s, 1, accountId);
        while (sqlite3_step(s) == SQLITE_ROW)
        {
            KeypairEntry e;
            e.id = sqlite3_column_int64(s, 0);
            auto col = [&](int i) -> std::string {
                const char* t = reinterpret_cast<const char*>(sqlite3_column_text(s, i));
                return t ? t : "";
            };
            e.publicKey = col(1);
            e.label = col(2);
            results.push_back(std::move(e));
        }
        sqlite3_finalize(s);
        return results;
    }

    int64_t PlayerDatabase::lookupAccountByKeypair(std::string_view publicKey)
    {
        sqlite3_stmt* s = prepare("SELECT account_id FROM account_keypairs WHERE public_key=?1 LIMIT 1");
        sqlite3_bind_text(s, 1, publicKey.data(), static_cast<int>(publicKey.size()), SQLITE_STATIC);
        const int rc = sqlite3_step(s);
        const int64_t id = (rc == SQLITE_ROW) ? sqlite3_column_int64(s, 0) : -1;
        sqlite3_finalize(s);
        return id;
    }

    std::string PlayerDatabase::getUsernameForAccount(int64_t accountId)
    {
        sqlite3_stmt* s = prepare("SELECT username FROM accounts WHERE id=?1 LIMIT 1");
        sqlite3_bind_int64(s, 1, accountId);
        const int rc = sqlite3_step(s);
        std::string name;
        if (rc == SQLITE_ROW)
        {
            const char* t = reinterpret_cast<const char*>(sqlite3_column_text(s, 0));
            if (t)
                name = t;
        }
        sqlite3_finalize(s);
        return name;
    }

    void PlayerDatabase::removeKeypair(std::string_view publicKey)
    {
        sqlite3_stmt* s = prepare("DELETE FROM account_keypairs WHERE public_key=?1");
        sqlite3_bind_text(s, 1, publicKey.data(), static_cast<int>(publicKey.size()), SQLITE_STATIC);
        checkSqlite(sqlite3_step(s), mDb, "removeKeypair");
        sqlite3_finalize(s);
    }

    std::vector<PlayerDatabase::CharacterSummary> PlayerDatabase::listCharacters(int64_t accountId)
    {
        std::vector<CharacterSummary> results;
        sqlite3_stmt* s = prepare(
            "SELECT name, race, class_name, last_seen, is_new "
            "FROM characters WHERE account_id=?1 ORDER BY last_seen DESC");
        sqlite3_bind_int64(s, 1, accountId);
        while (sqlite3_step(s) == SQLITE_ROW)
        {
            CharacterSummary cs;
            auto col = [&](int i) -> std::string {
                const char* t = reinterpret_cast<const char*>(sqlite3_column_text(s, i));
                return t ? t : "";
            };
            cs.name = col(0);
            cs.race = col(1);
            cs.className = col(2);
            int64_t ts = sqlite3_column_int64(s, 3);
            cs.lastSeen = ts ? std::to_string(ts) : "";
            cs.isNew = sqlite3_column_int(s, 4) != 0;
            results.push_back(std::move(cs));
        }
        sqlite3_finalize(s);
        return results;
    }

} // namespace mwmp
