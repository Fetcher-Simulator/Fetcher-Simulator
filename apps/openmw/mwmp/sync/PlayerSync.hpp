#ifndef OPENMW_MWMP_SYNC_PLAYERSYNC_HPP
#define OPENMW_MWMP_SYNC_PLAYERSYNC_HPP

#include <cstdint>
#include <deque>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include <osg/Vec3f>

#include <components/openmw-mp/Base/BasePlayer.hpp>
#include <components/openmw-mp/InventorySync.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/PlayerCrimeState.hpp>
#include <components/openmw-mp/PlayerFactionState.hpp>
#include <components/openmw-mp/PlayerTopicState.hpp>
#include <components/openmw-mp/SemanticService.hpp>
#include <components/openmw-mp/SpellbookSync.hpp>

namespace MWMechanics { class NpcStats; }
namespace MWWorld { class ESMStore; class Player; class Ptr; }

namespace mwmp
{
    void applyAuthoritativeCrimeState(
        MWMechanics::NpcStats& npcStats, MWWorld::Player& player, const PlayerCrimeState& state);
    PlayerFactionState capturePlayerFactionState(
        const MWMechanics::NpcStats& npcStats, std::uint64_t revision);
    void applyPlayerFactionState(MWMechanics::NpcStats& npcStats, const PlayerFactionState& state);

    class NetworkClient;
    class Protocol;

    // -----------------------------------------------------------------------
    // PlayerSync — tracks the local player's state and sends delta updates
    // to the server at the appropriate frequency.
    //
    // High-frequency (unreliable, ~20 Hz): position, velocity, anim flags
    // On-change (reliable):               equipment, stats, cell change
    // Low-frequency (reliable, ~1 Hz):    dynamic stats (HP/MP/FP)
    // -----------------------------------------------------------------------
    class PlayerSync
    {
    public:
        PlayerSync(NetworkClient& client, Protocol& protocol);

        // Called every engine frame
        void update(float dt);

        // Bind to the actual player Ptr once the world is loaded
        void setPlayer(const MWWorld::Ptr& player);

        // Force-flush all state immediately (e.g. just after connect)
        void forceFullSync(bool includeInventoryAndEquipment = true, bool includeSpellbook = true);
        void flushPersistentStats();
        void notifyLocalHit(const MWWorld::Ptr& victim, float damage, bool healthDamage, bool knocked,
            const osg::Vec3f& hitPos, int attackType = 0, float attackStrength = 0.f,
            const std::string& onStrikeEnchantment = {});
        void notifyLocalAttackRelease(float attackStrength);
        void noteRemotePlayerHit(uint32_t attackerGuid);
        void notifyLocalCastRelease(
            const std::string& spellId, const std::string& castAnimation, const MWWorld::Ptr& target);

        // Server told us our position was wrong - snap to authoritative value
        void applyServerPositionCorrection(const BasePlayer& authoritative);
        void applyServerCellChange(const BasePlayer& authoritative);
        void queueAuthoritativeEquipment(const BasePlayer& authoritative);
        void queueAuthoritativeInventory(const BasePlayer& authoritative);
        void queueAuthoritativeSpellbook(const BasePlayer& authoritative);
        void queueAuthoritativeStats(const BasePlayer& authoritative);
        void applyAuthoritativeStatsToPlayer();
        void onDynamicRecordsChanged();
        void queueAuthoritativeJournal(const BasePlayer& authoritative);
        void queueRestoredStats(const BasePlayer& restored);
        void applyRestoredStatsToPlayer();
        void applyServerDeath(const BasePlayer& state);

        // Accessors used by Networking dispatcher
        BasePlayer& localPlayer() { return mLocal; }
        std::optional<std::int64_t> authoritativeInventoryCount(std::string_view refId) const;
        void resetInventoryAuthorityState();

        // Drop all per-session spellbook sync state (revision token, baseline,
        // in-flight gate). Called on disconnect so no state leaks into the next
        // connection.
        void resetSpellbookSyncState();
        void resetJournalSyncState();
        void resetCrimeStateSync();
        void resetFactionStateSync();
        void resetTopicStateSync();
        void resetMechanicsSnapshotState();
        RevisionDecision receiveAuthoritativeCrimeState(PlayerCrimeState state);
        RevisionDecision receiveAuthoritativeFactionResult(PlayerFactionState state,
            std::string_view requestId, bool accepted, FactionError error);
        RevisionDecision receiveAuthoritativeTopicState(PlayerTopicState state);
        bool hasAuthoritativeCrimeState() const { return mCrimeStateGate.hasState(); }
        bool hasAuthoritativeFactionState() const { return mFactionStateGate.hasState(); }
        bool hasAuthoritativeTopicState() const { return mTopicStateGate.hasState(); }
        void applyAuthoritativeCrimeStateToPlayer();
        void applyAuthoritativeFactionState();
        void applyAuthoritativeTopicState();
        static bool journalDefinitionsAvailable(const MWWorld::ESMStore& store,
            const BasePlayer::JournalChanges& changes, std::string* missingQuest = nullptr,
            std::string* missingInfo = nullptr);

    private:
        // ---- per-frame checks ----
        void tickPosition(float dt);
        void tickDynamicStats(float dt);
        void tickJournal();
        void tickFactions();
        void tickTopics();
        void tickSpellbook(float dt);

        // ---- send helpers ----
        void sendPosition(bool reliable);
        void sendCellChange();
        void sendLoadedActorCells(bool force = false);
        void sendEquipment();
        void sendInventory();
        void sendSpellbook();
        void sendJournal();
        void sendAnimFlags(float dt);
        void sendAnimPlay();
        void sendAttack();
        void sendCast();
        void sendDynamicStats();
        void sendBaseInfo();
        bool sendDeath();
        void sendResurrect();
        void sendMechanicsSnapshot(const MWWorld::Ptr& player, bool reliable);
        void respawnLocally(const MWWorld::Ptr& player);

        // ---- change detection ----
        bool positionChanged()    const;
        bool cellChanged()        const;
        bool equipmentChanged()   const;
        bool inventoryChanged()   const;
        bool spellbookChanged()   const;
        bool dynamicStatsChanged() const;
        bool animFlagsChanged()    const;

        // ---- utilities ----
        void snapshotPosition();
        void snapshotCell();
        void snapshotEquipment();
        void snapshotInventory();
        void snapshotSpellbook();
        void snapshotDynamicStats();
        void capturePersistentStats(const MWWorld::Ptr& player);
        void captureEquipment(const MWWorld::Ptr& player);
        void captureInventory(const MWWorld::Ptr& player);
        void captureSpellbook(const MWWorld::Ptr& player);
        void captureJournalSnapshot();
        // Client spellbook sync: learn/detect the learned spell set (ST_Spell
        // records only), propose canonical Sets on change, and reconcile with
        // the server's authoritative Set. See SpellbookSync.hpp for the shared
        // constants and helpers.
        void collectLearnedSpellIds(const MWWorld::Ptr& player, std::vector<std::string>& out) const;
        void applyAuthoritativeSpellbook(const MWWorld::Ptr& player);
        std::vector<std::string> collectLoadedActorCellIds() const;
        void applyPendingAuthoritativeState(const MWWorld::Ptr& player);
        uint32_t resolveTargetMpNum(const MWWorld::Ptr& victim) const;
        void sendCastPacket(
            const std::string& spellId, const std::string& castAnimation, bool release, const MWWorld::Ptr& target);

        NetworkClient& mClient;
        Protocol&      mProtocol;

        BasePlayer     mLocal;          // live mirror of local player state
        bool           mPlayerReady = false;
        BasePlayer     mPendingRestoredStats;
        bool           mHasPendingRestoredStats = false;
        BasePlayer     mPendingAuthoritativeStats;
        bool           mHasPendingAuthoritativeStats = false;
        int            mLastLoggedPersistentStrength = -1;
        float          mLastLoggedPersistentBlunt = -1.f;

        // --- send-rate accumulators ---
        float mPositionTimer    = 0.f;
        float mStatsTimer       = 0.f;
        float mMechanicsSnapshotTimer = 0.f;
        float mPositionDiagTimer = 0.f;
        float mPositionDiagFrameDtMax = 0.f;
        std::size_t mPositionDiagFrames = 0;
        std::size_t mPositionDiagSendOpportunities = 0;
        std::size_t mPositionDiagUnchanged = 0;
        std::size_t mPositionDiagSends = 0;

        //60 Hz breaks footstep cadence...need to test with Lerping
        //static constexpr float POSITION_RATE = 0.166f; // 60 Hz
        static constexpr float POSITION_RATE = 0.033f; // 30 Hz
        static constexpr float STATS_RATE    = 0.25f;  // 4 Hz on change
        static constexpr float MECHANICS_SNAPSHOT_RATE = 0.25f;

        // --- last-sent snapshots for delta detection ---
        struct PositionSnapshot { float pos[3]; float rot[3]; float velocity[3]; };
        PositionSnapshot mLastPos{};

        struct CellSnapshot { std::string cellName; bool isExterior; int gx; int gy; };
        CellSnapshot mLastCell{};
        std::vector<std::string> mLastLoadedActorCells;
        uint32_t mLoadedActorCellsSequence = 0;

        std::array<EquipmentItem, BasePlayer::NUM_EQUIPMENT_SLOTS> mLastEquip{};
        std::vector<Item> mLastInventory;
        InventoryRevisionGate mInventoryRevisionGate;
        float mInventoryChargeSyncTimer = 0.f;

        // --- spellbook sync state ---
        BasePlayer::SpellbookChanges mAuthoritativeSpellbook;
        bool mPendingSpellbookRestore = false;
        bool mSpellbookInitialized = false;
        std::vector<std::string> mLastSpellbook;
        std::string mLastPendingSpellbookMissingSpellId;
        float mSpellbookTimer = 0.f;
        static constexpr float SPELLBOOK_RATE = 0.25f; // 4 Hz learned-set comparison
        SpellbookRevisionGate mSpellbookRevisionGate;
        RevisionedStateGate<PlayerCrimeState> mCrimeStateGate;
        RevisionedStateGate<PlayerFactionState> mFactionStateGate;
        std::string mFactionRequestPrefix;
        std::uint64_t mNextFactionRequest = 0;
        std::string mFactionMutationInFlight;
        std::optional<PlayerFactionState> mFactionProposalObservedState;
        std::optional<PlayerFactionState> mDeferredFactionState;
        RevisionedStateGate<PlayerTopicState> mTopicStateGate;
        bool mTopicMutationInFlight = false;

        struct StatsSnapshot
        {
            DynamicStats dynamicStats;
            std::array<Attribute, BasePlayer::NUM_ATTRIBUTES> attributes;
            std::array<Skill, BasePlayer::NUM_SKILLS> skills;
            int level = 1;
            float levelProgress = 0.f;
        };
        StatsSnapshot mLastStats{};

        AnimFlags mLastAnimFlags{};

        // Periodic full anim-flags resend — self-heals receiver stuck state
        // caused by UDP packet loss (delta-suppress means the corrective
        // packet never gets re-sent otherwise).
        float mAnimRefreshTimer = 0.f;
        static constexpr float ANIM_REFRESH_RATE = 2.0f;

        // Movement tap latch: holds the WASD gate open for a short window after the
        // last real movement-key press (any stance — walk, run, or sneak).
        // Quick taps release the key before sendAnimFlags runs, so by the time we
        // project velocity the gate says "no key held" even though the body still
        // has full momentum.  The latch keeps the projection alive until velocity
        // naturally decays, ensuring the remote NPC always sees at least one
        // non-zero fwd/side packet.  Applies to all stances so brief steps while
        // standing or running are as reliable as the already-fixed sneak taps.
        float mMoveLatch = 0.f;
        static constexpr float MOVE_LATCH_TIME = 0.10f; // 100 ms

        float mJumpGraceTimer = 0.f;
        float mJumpStallTimer = 0.f;
        float mCcJumpVisualLatch = 0.f;
        static constexpr float JUMP_GRACE_TIME = 0.12f;
        static constexpr float JUMP_STALL_TIME = 0.10f;
        static constexpr float CC_JUMP_VISUAL_LATCH_TIME = 0.08f;

        // Wall-block latch: once keys are held but velocity drops below the gate,
        // stay in the raw-input (wall-fallback) code path for this many seconds.
        // Prevents brief collision-slide residuals (~18–25 u/s) from flipping back
        // to velocity-projection mid-stride and oscillating the remote fwd/side.
        //float mWallBlockLatch = 0.f;
        //static constexpr float WALL_BLOCK_LATCH_TIME = 0.15f; // 150 ms
        // Previous-frame planar speed — needed to detect the above→below gate
        // transition that actually means "just hit a wall" vs fresh key press.
        //float mLastSpd = 0.f;

        // Last attack pressed state — detect edge (false→true) for send
        bool mLastAttackPressed = false;
        // A physical-projectile release is deferred for one frame so the local
        // CharacterController can publish the exact wind-up strength it used.
        bool mAwaitingRangedRelease = false;
        bool mLastCastingOrSpell = false;
        bool mLastWasDead = false;
        bool mDeathPacketSent = false;
        bool mRespawnPending = false;
        float mRespawnTimer = 0.f;
        static constexpr float RESPAWN_DELAY = 5.f;
        uint32_t mRecentPlayerAttackerGuid = 0;
        float mRecentPlayerAttackerTimer = 0.f;
        static constexpr float PLAYER_ATTACKER_CONTEXT_SECONDS = 15.f;

        uint32_t mSeqCounter = 0;
        uint32_t mMechanicsSnapshotSequence = 0;

        bool mPendingEquipmentRestore = false;
        bool mPendingInventoryRestore = false;
        bool mHasAuthoritativeInventory = false;
        std::array<EquipmentItem, BasePlayer::NUM_EQUIPMENT_SLOTS> mAuthoritativeEquipment{};
        BasePlayer::InventoryChanges mAuthoritativeInventory;
        std::deque<BasePlayer::JournalChanges> mPendingJournalChanges;
        bool mJournalAuthoritativeInitialized = false;
        std::unordered_set<std::string> mLastJournalEntries;
        std::unordered_map<std::string, int> mLastJournalIndices;
        std::string mLastPendingInventoryMissingRefId;
        std::string mLastPendingEquipmentMissingRefId;

        // Smoothed Z velocity - raw per-frame Z deltas are very spiky on stairs
        // (discrete step geometry produces alternating large/zero Z displacement).
        // An EMA smooths this into a steady signal so the receiver's Z dead-
        // reckoning produces smooth stair movement matching the local client.
        float mSmoothedVz = 0.f;
        static constexpr float VZ_SMOOTH_ALPHA = 0.25f; // EMA weight per frame
    };

} // namespace mwmp

#endif // OPENMW_MWMP_SYNC_PLAYERSYNC_HPP
