#include "Main.hpp"
#include "Identity.hpp"
#include "MpNetworkBridge.hpp"
#include "alchemy/AlchemyCreationManager.hpp"
#include "enchanting/EnchantingCreationManager.hpp"
#include "records/RecordCreationManager.hpp"
#include "records/ResolvedContentFingerprint.hpp"
#include "sha256.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>

#include <stdexcept>
#include <string_view>

#include <components/debug/debuglog.hpp>
#include <components/files/collections.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/version/version.hpp>
#include <components/vfs/manager.hpp>
#include <components/openmw-mp/MasterServerProtocol.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerPosition.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerCellChange.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerStatsDynamic.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerBaseInfo.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerBounty.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerFaction.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerTopic.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerEquipment.hpp>
#include <components/openmw-mp/Packets/Player/PacketChatMessage.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerDeath.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerResurrect.hpp>
#include <components/openmw-mp/Packets/System/PacketGameSettings.hpp>
#include <components/openmw-mp/Packets/System/PacketHandshake.hpp>
#include <components/openmw-mp/Packets/System/PacketServerLuaPackage.hpp>
#include <components/openmw-mp/Packets/Worldstate/PacketRecordDynamic.hpp>
#include <components/openmw-mp/Packets/Worldstate/PacketRuntimeContentBootstrapComplete.hpp>
#include <components/openmw-mp/Packets/Records/PacketRecordCreateResult.hpp>
#include <components/openmw-mp/Packets/Records/PacketAlchemyResult.hpp>
#include <components/openmw-mp/Packets/Records/PacketEnchantingResult.hpp>
#include <components/openmw-mp/Packets/Worldstate/PacketWorldTime.hpp>
#include <components/openmw-mp/Packets/Object/PacketDoorState.hpp>
#include <components/openmw-mp/Packets/Object/PacketObjectPlace.hpp>
#include <components/openmw-mp/Packets/Object/PacketObjectDelete.hpp>
#include <components/openmw-mp/Packets/Object/PacketObjectCount.hpp>
#include <components/openmw-mp/Packets/Object/PacketObjectMove.hpp>
#include <components/openmw-mp/Packets/Object/PacketContainer.hpp>
#include <components/openmw-mp/Packets/Object/PacketWorldItemTake.hpp>
#include <components/openmw-mp/Packets/Object/PacketInventoryTake.hpp>
#include <components/openmw-mp/Packets/Object/PacketInventoryPut.hpp>
#include <components/openmw-mp/Packets/Object/PacketBarter.hpp>
#include <components/openmw-mp/Packets/Object/PacketCrimeInteraction.hpp>
#include <components/openmw-mp/Packets/Player/PacketGuardArrest.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerAnimFlags.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerAnimPlay.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerAttack.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerCast.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerSpeech.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerVehicleState.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerInventory.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerInventoryTransferSound.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerJournal.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerSpellbook.hpp>
#include <components/openmw-mp/Packets/Lua/PacketLuaEvent.hpp>
#include <components/openmw-mp/Packets/Lua/PacketLuaStorage.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorAI.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorAnimFlags.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorAnimPlay.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorAttack.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorAttackV2.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorSpeech.hpp>
#include <components/openmw-mp/Packets/Actor/PacketCrimeReaction.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorAuthority.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorCast.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorCellChange.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorCombatRequest.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorCombatResult.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorDeath.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorEquipment.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorIdentity.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorList.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorPosition.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorPositionV2.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorPresentationV2.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorStatsDynamic.hpp>

#include "network/Client.hpp"
#include "network/Protocol.hpp"
#include "sync/PlayerSync.hpp"
#include "sync/RemotePlayer.hpp"
#include "sync/ActorSync.hpp"
#include "sync/CellSync.hpp"
#include "sync/ObjectSync.hpp"
#include "sync/WorldObjectSync.hpp"
#include "sync/WorldStateSync.hpp"
#include "gui/ChatWindow.hpp"

#include <components/openmw-mp/Packets/Player/PacketPlayerCharGen.hpp>
#include "../mwbase/environment.hpp"
#include "../mwbase/dialoguemanager.hpp"
#include "../mwbase/luamanager.hpp"
#include "../mwbase/scriptmanager.hpp"
#include "../mwbase/statemanager.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwphysics/surfphysics.hpp"
#include "../mwbase/world.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwgui/inventorywindow.hpp"
#include <components/vfs/pathutil.hpp>
#include "../mwgui/mode.hpp"
#include "../mwworld/player.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwdialogue/scripttest.hpp"
#include <sstream>
#include <filesystem>
#include <components/esm/position.hpp>
#include <components/esm/refid.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadclas.hpp>

namespace mwmp
{
namespace
{
    const Files::Collections* sFileCollections = nullptr;

    bool startsWithNoCase(std::string_view value, std::string_view prefix)
    {
        if (value.size() < prefix.size())
            return false;

        for (std::size_t i = 0; i < prefix.size(); ++i)
        {
            const auto left = static_cast<unsigned char>(value[i]);
            const auto right = static_cast<unsigned char>(prefix[i]);
            if (std::tolower(left) != std::tolower(right))
                return false;
        }

        return true;
    }

    std::string normalizeSpeechSoundPath(std::string soundPath)
    {
        std::replace(soundPath.begin(), soundPath.end(), '/', '\\');
        if (startsWithNoCase(soundPath, "sound\\") || !startsWithNoCase(soundPath, "vo\\"))
            return soundPath;

        return "Sound\\" + soundPath;
    }
}

Main* Main::sInstance = nullptr;

// ---------------------------------------------------------------------------
Main& Main::get()
{
    if (!sInstance)
        throw std::runtime_error("mwmp::Main not initialised");
    return *sInstance;
}

void Main::sendActorCombatRequest(const MWWorld::Ptr& victim, float damage, bool healthDamage, bool knocked,
    const osg::Vec3f& hitPos, int attackType, float attackStrength)
{
    if (mActorSync)
        mActorSync->sendCombatRequest(victim, damage, healthDamage, knocked, hitPos, attackType, attackStrength);
}

void Main::sendActorNpcPlayerHit(uint32_t victimGuid, const MWWorld::Ptr& npcAttacker, float damage, bool healthDamage,
    bool isDead, int attackType)
{
    if (mActorSync)
        mActorSync->sendNpcPlayerDamage(victimGuid, damage, healthDamage, isDead, attackType, npcAttacker);
}

bool Main::requestCrimeMutation(CrimeMutationKind kind, std::int64_t value, std::string source)
{
    if (!mWorldReady || !mClient || !mPlayerSync)
        return false;

    CrimeMutationRequest request;
    request.requestId = mCrimeMutationRequestPrefix + '-'
        + std::to_string(mNextCrimeMutationRequest++);
    request.kind = kind;
    request.value = value;
    request.source = std::move(source);
    if (validateCrimeMutationRequest(request) != CrimeError::None)
        return false;

    mPendingCrimeMutations.push_back(std::move(request));
    sendNextCrimeMutation();
    return true;
}

bool Main::requestCrimeInteraction(CrimeInteractionKind kind, const MWWorld::Ptr& target)
{
    if (!mWorldReady || !mClient || target.isEmpty() || !target.isInCell()
        || target.getCell() == nullptr || target.getCell()->getCell() == nullptr)
        return false;

    CrimeInteractionRequest request;
    request.requestId = mCrimeMutationRequestPrefix + "-interaction-"
        + std::to_string(mNextCrimeMutationRequest++);
    request.kind = kind;
    request.refId = target.getCellRef().getRefId().serializeText();
    const ESM::RefNum refNum = target.getCellRef().getRefNum();
    request.refNum = refNum.mIndex;
    request.refContentFile = refNum.mContentFile;
    const MWWorld::Cell* cell = target.getCell()->getCell();
    request.cellId = cell->isExterior()
        ? "EXT:" + std::to_string(cell->getGridX()) + ',' + std::to_string(cell->getGridY())
        : std::string(cell->getNameId());
    if (!validateCrimeInteractionRequest(request))
        return false;
    PacketCrimeInteraction packet;
    packet.request = std::move(request);
    mClient->sendReliable(packet.encode());
    return true;
}

void Main::beginGuardArrestDialogue(const MWWorld::Ptr& guard)
{
    mGuardArrestDialogue = {};
    if (!mWorldReady || !mActorSync || guard.isEmpty() || !guard.isInCell()
        || !guard.getClass().isNpc() || !guard.getClass().isClass(guard, "Guard")
        || guard.getCell() == nullptr || guard.getCell()->getCell() == nullptr)
        return;

    const MWWorld::Cell* cell = guard.getCell()->getCell();
    const std::string cellId = cell->isExterior()
        ? "EXT:" + std::to_string(cell->getGridX()) + ',' + std::to_string(cell->getGridY())
        : std::string(cell->getNameId());
    const ActorInstanceId actorNetId = mActorSync->actorNetIdForPtr(cellId, guard);
    const std::uint32_t migrationGeneration = mActorSync->actorMigrationGenerationForPtr(cellId, guard);
    if (!isValidActorInstanceId(actorNetId) || migrationGeneration == 0)
    {
        Log(Debug::Warning) << "[MP] Guard arrest dialogue missing canonical actor identity"
                            << " cell=" << cellId
                            << " actorNetId=" << actorNetId
                            << " migrationGeneration=" << migrationGeneration
                            << " guard=" << guard.toString();
        return;
    }

    mGuardArrestDialogue.active = true;
    mGuardArrestDialogue.cellId = cellId;
    mGuardArrestDialogue.actorNetId = actorNetId;
    mGuardArrestDialogue.migrationGeneration = migrationGeneration;
}

void Main::endGuardArrestDialogue()
{
    mGuardArrestDialogue = {};
}

bool Main::requestGuardArrest(GuardArrestAction action)
{
    if (!mGuardArrestDialogue.active || !mWorldReady || !mClient || !mPlayerSync)
    {
        Log(Debug::Warning) << "[MP] Guard arrest request unavailable"
                            << " active=" << mGuardArrestDialogue.active
                            << " worldReady=" << mWorldReady
                            << " client=" << static_cast<bool>(mClient)
                            << " playerSync=" << static_cast<bool>(mPlayerSync);
        return false;
    }

    GuardArrestRequest request;
    request.requestId = mCrimeMutationRequestPrefix + "-guard-arrest-"
        + std::to_string(mNextCrimeMutationRequest++);
    request.action = action;
    request.cellId = mGuardArrestDialogue.cellId;
    request.actorNetId = mGuardArrestDialogue.actorNetId;
    request.migrationGeneration = mGuardArrestDialogue.migrationGeneration;
    request.expectedCrimeRevision = mPlayerSync->localPlayer().crimeState.revision;
    request.expectedInventoryRevision = mPlayerSync->localPlayer().inventoryChanges.revision;
    if (!validateGuardArrestRequest(request))
    {
        Log(Debug::Warning) << "[MP] Guard arrest request failed local validation"
                            << " request=" << request.requestId
                            << " action=" << static_cast<unsigned>(request.action)
                            << " cell=" << request.cellId
                            << " actorNetId=" << request.actorNetId
                            << " migrationGeneration=" << request.migrationGeneration
                            << " crimeRevision=" << request.expectedCrimeRevision
                            << " inventoryRevision=" << request.expectedInventoryRevision;
        return false;
    }

    PacketGuardArrest packet;
    packet.mode = PacketGuardArrest::Mode::Request;
    packet.request = request;
    mPendingGuardArrestRequests.emplace(request.requestId, action);
    mClient->sendReliable(packet.encode());
    Log(Debug::Info) << "[MP] Guard arrest request=" << request.requestId
                     << " action=" << static_cast<unsigned>(action)
                     << " actorNetId=" << request.actorNetId
                     << " cell=" << request.cellId;
    return true;
}

bool Main::reportGuardArrestReach(const MWWorld::Ptr& guard, std::uint32_t offenderGuid)
{
    if (!mWorldReady || !mClient || !mActorSync || !mPlayerSync || guard.isEmpty()
        || !guard.isInCell() || guard.getCell() == nullptr || guard.getCell()->getCell() == nullptr
        || offenderGuid == 0 || offenderGuid == mPlayerSync->localPlayer().guid)
        return false;

    const MWWorld::Cell* cell = guard.getCell()->getCell();
    GuardArrestReach reach;
    reach.cellId = cell->isExterior()
        ? "EXT:" + std::to_string(cell->getGridX()) + ',' + std::to_string(cell->getGridY())
        : std::string(cell->getNameId());
    reach.actorNetId = mActorSync->actorNetIdForPtr(reach.cellId, guard);
    reach.migrationGeneration = mActorSync->actorMigrationGenerationForPtr(reach.cellId, guard);
    reach.offenderGuid = offenderGuid;
    if (!validateGuardArrestReach(reach))
        return false;

    PacketGuardArrest packet;
    packet.mode = PacketGuardArrest::Mode::Reach;
    packet.reach = reach;
    mClient->sendReliable(packet.encode());
    Log(Debug::Info) << "[MP] Guard arrest reach reported"
                     << " actorNetId=" << reach.actorNetId
                     << " offenderGuid=" << reach.offenderGuid
                     << " cell=" << reach.cellId;
    return true;
}

void Main::receiveGuardArrestPrompt(const GuardArrestReach& prompt)
{
    if (!mWorldReady || !mActorSync || !mPlayerSync || !validateGuardArrestReach(prompt))
        return;
    if (prompt.offenderGuid != mPlayerSync->localPlayer().guid)
    {
        disconnect("Guard arrest prompt offender identity mismatch");
        return;
    }
    if (!mPlayerSync->hasAuthoritativeCrimeState() || mPlayerSync->localPlayer().crimeState.bounty <= 0)
    {
        Log(Debug::Warning) << "[MP] Ignoring stale guard arrest prompt with no authoritative bounty"
                            << " actorNetId=" << prompt.actorNetId;
        return;
    }

    MWWorld::Ptr guard = mActorSync->getActorByNetId(prompt.actorNetId);
    if (guard.isEmpty() || !guard.isInCell() || guard.getCell() == nullptr || guard.getCell()->getCell() == nullptr
        || !guard.getClass().isNpc() || !guard.getClass().isClass(guard, "Guard"))
    {
        Log(Debug::Warning) << "[MP] Guard arrest prompt guard unavailable"
                            << " actorNetId=" << prompt.actorNetId
                            << " cell=" << prompt.cellId;
        return;
    }

    const MWWorld::Cell* cell = guard.getCell()->getCell();
    const std::string cellId = cell->isExterior()
        ? "EXT:" + std::to_string(cell->getGridX()) + ',' + std::to_string(cell->getGridY())
        : std::string(cell->getNameId());
    if (cellId != prompt.cellId
        || mActorSync->actorMigrationGenerationForPtr(cellId, guard) != prompt.migrationGeneration)
    {
        Log(Debug::Warning) << "[MP] Guard arrest prompt canonical guard mismatch"
                            << " actorNetId=" << prompt.actorNetId
                            << " cell=" << prompt.cellId;
        return;
    }

    MWBase::WindowManager* windowManager = MWBase::Environment::get().getWindowManager();
    if (windowManager->containsMode(MWGui::GM_Dialogue))
    {
        Log(Debug::Warning) << "[MP] Guard arrest prompt deferred by existing dialogue"
                            << " actorNetId=" << prompt.actorNetId;
        return;
    }

    windowManager->pushGuiMode(MWGui::GM_Dialogue, guard);
    Log(Debug::Info) << "[MP] Applied server-routed guard arrest prompt"
                     << " actorNetId=" << prompt.actorNetId
                     << " offenderGuid=" << prompt.offenderGuid
                     << " cell=" << prompt.cellId;
}

void Main::sendNextCrimeMutation()
{
    if (!mCrimeMutationInFlight.empty() || mPendingCrimeMutations.empty() || !mPlayerSync)
        return;

    CrimeMutationRequest& request = mPendingCrimeMutations.front();
    request.expectedRevision = mPlayerSync->localPlayer().crimeState.revision;
    PacketPlayerBounty packet;
    packet.mode = PacketPlayerBounty::Mode::Proposal;
    packet.request = request;
    packet.setPlayer(&mPlayerSync->localPlayer());
    mCrimeMutationInFlight = request.requestId;
    mClient->sendReliable(packet.encode());
}

void Main::finishCrimeMutation(std::string_view requestId, bool accepted, CrimeError error)
{
    if (requestId.empty())
        return;
    if (requestId != mCrimeMutationInFlight || mPendingCrimeMutations.empty()
        || mPendingCrimeMutations.front().requestId != requestId)
    {
        Log(Debug::Warning) << "[MP] Ignoring unmatched crime mutation result request=" << requestId;
        return;
    }

    Log(accepted ? Debug::Info : Debug::Warning)
        << "[MP] Crime mutation result request=" << requestId
        << " accepted=" << accepted << " error=" << getCrimeErrorCode(error);
    mPendingCrimeMutations.pop_front();
    mCrimeMutationInFlight.clear();
    sendNextCrimeMutation();
}

void Main::finishGuardArrest(const GuardArrestResult& result)
{
    const auto pending = mPendingGuardArrestRequests.find(result.requestId);
    if (pending == mPendingGuardArrestRequests.end())
    {
        Log(Debug::Warning) << "[MP] Ignoring unmatched guard arrest result request=" << result.requestId;
        return;
    }
    if (pending->second != result.action)
    {
        mPendingGuardArrestRequests.erase(pending);
        disconnect("Guard arrest result action mismatch");
        return;
    }
    mPendingGuardArrestRequests.erase(pending);

    const RevisionDecision decision = mPlayerSync->receiveAuthoritativeCrimeState(result.crimeState);
    if (decision == RevisionDecision::Conflict)
    {
        disconnect("Conflicting authoritative guard arrest crime state");
        return;
    }

    Log(result.accepted ? Debug::Info : Debug::Warning)
        << "[MP] Guard arrest result request=" << result.requestId
        << " action=" << static_cast<unsigned>(result.action)
        << " accepted=" << result.accepted
        << " error=" << getGuardArrestErrorCode(result.error)
        << " bounty=" << result.crimeState.bounty
        << " goldPaid=" << result.goldPaid
        << " sentenceDays=" << result.sentenceDays;

    const MWBase::Environment& environment = MWBase::Environment::get();
    if (environment.getStateManager()->getState() != MWBase::StateManager::State_Running)
        return;

    if (!result.accepted)
    {
        environment.getDialogueManager()->goodbyeSelected();
        if (environment.getWindowManager()->containsMode(MWGui::GM_Dialogue))
            environment.getWindowManager()->removeGuiMode(MWGui::GM_Dialogue);
        return;
    }

    if (result.action == GuardArrestAction::Surrender)
    {
        environment.getWorld()->goToJailAuthoritative(static_cast<int>(result.sentenceDays));
        return;
    }

    environment.getDialogueManager()->goodbyeSelected();
    if (environment.getWindowManager()->containsMode(MWGui::GM_Dialogue))
        environment.getWindowManager()->removeGuiMode(MWGui::GM_Dialogue);
}

bool Main::isInitialised()
{
    return sInstance != nullptr;
}

// ---------------------------------------------------------------------------
bool Main::init(const std::string& host, uint16_t port,
                const std::string& playerName,
                const std::string& passwordHash,
                bool isRegistration,
                bool useKeypair,
                const std::string& autoCharacterName,
                bool compileAllScripts,
                bool compileAllDialogue,
                const Compiler::Extensions* compilerExtensions,
                int warningsMode)
{
    if (sInstance)
    {
        Log(Debug::Warning) << "[MP] Main::init called while already initialised";
        return true;
    }

    try
    {
        sInstance = new Main();
        sInstance->mPlayerName     = playerName;
        sInstance->mPasswordHash   = passwordHash;
        sInstance->mIsRegistration = isRegistration;
        sInstance->mUseKeypair     = useKeypair;
        sInstance->mAutoCharacterName = autoCharacterName;
        sInstance->mCompileAllScriptsAfterBootstrap = compileAllScripts;
        sInstance->mCompileAllDialogueAfterBootstrap = compileAllDialogue;
        sInstance->mCompilerExtensions = compilerExtensions;
        sInstance->mCompilerWarningsMode = warningsMode;
        sInstance->mHost           = host;
        sInstance->mPort           = port;
        sInstance->mPlayerSync->localPlayer().name = playerName;
        MWBase::Environment::get().getLuaManager()->prepareMultiplayerPlayerStorage();

        // Attempt connection
        sInstance->mClient->setStateChangeCallback(
            [](ConnectionState /*old*/, ConnectionState newState)
            {
                if (newState == ConnectionState::Connected)
                    Main::get().onConnected();
                else if (newState == ConnectionState::Disconnected)
                    Main::get().onDisconnected();
            });

        sInstance->mClient->setMessageCallback(
            [](const uint8_t* data, size_t size)
            {
                Main::get().getProtocol().dispatch(data, size);
            });

        sInstance->registerProtocolHandlers();

        if (!sInstance->mClient->connect(host, port))
        {
            Log(Debug::Error) << "[MP] Failed to initiate connection to "
                              << host << ":" << port;
            delete sInstance;
            sInstance = nullptr;
            return false;
        }

        Log(Debug::Info) << "[MP] Main initialised, connecting to "
                         << host << ":" << port;
        if (!autoCharacterName.empty())
        {
            Log(Debug::Info) << "[MP] Auto character selection armed"
                             << " account=" << playerName
                             << " character=" << autoCharacterName;
        }
        return true;
    }
    catch (const std::exception& e)
    {
        Log(Debug::Error) << "[MP] Main::init exception: " << e.what();
        delete sInstance;
        sInstance = nullptr;
        return false;
    }
}

// ---------------------------------------------------------------------------
void Main::destroy()
{
    delete sInstance;
    sInstance = nullptr;
    Log(Debug::Info) << "[MP] Main destroyed";
}

// ---------------------------------------------------------------------------
Main::Main()
{
    std::random_device random;
    const auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    std::ostringstream crimePrefix;
    crimePrefix << "client-crime-" << timestamp << '-' << random() << random();
    mCrimeMutationRequestPrefix = crimePrefix.str();

    mClient        = std::make_unique<NetworkClient>();
    mProtocol      = std::make_unique<Protocol>();
    mPlayerSync    = std::make_unique<PlayerSync>(*mClient, *mProtocol);
    mPlayerList    = std::make_unique<PlayerList>();
    mActorSync     = std::make_unique<ActorSync>(*mClient);
    mCellSync      = std::make_unique<CellSync>(*mClient);
    mObjectSync    = std::make_unique<ObjectSync>(*mClient);
    mWorldObjectSync = std::make_unique<WorldObjectSync>(*mClient);
    mWorldStateSync= std::make_unique<WorldStateSync>(*mClient);
    mRecordCreationManager = std::make_unique<RecordCreationManager>(*mClient);
    mAlchemyCreationManager = std::make_unique<AlchemyCreationManager>(*mClient, *mRecordCreationManager);
    mEnchantingCreationManager
        = std::make_unique<EnchantingCreationManager>(*mClient, *mRecordCreationManager);
    mWorldStateSync->setDynamicRecordChangeCallback(
        [this] {
            mRecordCreationManager->notifyRecordStoreChanged();
            mPlayerSync->onDynamicRecordsChanged();
            mPlayerList->onDynamicRecordsChanged();
            mWorldObjectSync->onDynamicRecordsChanged();
        });
    mChatWindow = std::make_unique<ChatWindow>(*mClient);
    mNetworkBridge = std::make_unique<MpNetworkBridge>();
}

Main::~Main()
{
    if (mClient && mClient->isConnected())
    {
        if (mPlayerSync)
            mPlayerSync->flushPersistentStats();
        mClient->disconnect("Client shutdown");
    }
}

// ---------------------------------------------------------------------------
void Main::frame(float dt)
{
    if (!mClient) return;

    const auto frameStarted = std::chrono::steady_clock::now();
    mClient->update();
    const auto clientUpdateFinished = std::chrono::steady_clock::now();
    if (mNetworkBridge && mClient->isConnected())
        mNetworkBridge->drainOutgoing(*mClient);
    const auto bridgeFinished = std::chrono::steady_clock::now();

    // Handle unexpected server disconnect - return player to main menu.
    if (mUnexpectedDisconnect)
    {
        mUnexpectedDisconnect = false;
        mCharGenWatching = false;
        Log(Debug::Warning) << "[MP] Unexpected disconnect Ã¢â‚¬â€ returning to main menu";
        MWBase::Environment::get().getStateManager()->returnToMainMenu();
        clearServerLuaPackageSession();
        return;
    }

    if (mServerLuaCleanupPending)
        clearServerLuaPackageSession();

    if (!mClient->isConnected()) return;

    tryAutoEnterWorld();
    const auto autoEnterFinished = std::chrono::steady_clock::now();
    if (!mClient->isConnected()) return;

    // Advance remote vehicle roots first so a local passenger and the rendered
    // truck consume the same interpolation sample in this frame. Follow remote
    // passengers after local driver state has been captured for the same reason.
    mPlayerList->updateNonPassengers(dt);
    const auto playerListPreFinished = std::chrono::steady_clock::now();
    mPlayerSync->update(dt);
    const auto playerSyncFinished = std::chrono::steady_clock::now();
    mPlayerList->updatePassengers(dt);
    const auto playerListFinished = std::chrono::steady_clock::now();
    mActorSync->update(dt);
    const auto actorSyncFinished = std::chrono::steady_clock::now();
    mObjectSync->update(dt);
    mWorldObjectSync->update(dt);
    mWorldStateSync->update(dt);
    mRecordCreationManager->update();
    mAlchemyCreationManager->update();
    mEnchantingCreationManager->update();
    const auto worldSyncFinished = std::chrono::steady_clock::now();

    mChatWindow->update(dt);
    pollChargenAppearance(dt);
    const auto frameFinished = std::chrono::steady_clock::now();

    const double totalMs = std::chrono::duration<double, std::milli>(
        frameFinished - frameStarted).count();
    if (totalMs >= 16.0)
    {
        // The logger writes synchronously; keep frame timing diagnostics out of
        // normal production logs so reporting a hitch cannot extend the hitch.
        Log(Debug::Verbose)
            << "[MPDIAG] Multiplayer frame phases"
            << " totalMs=" << totalMs
            << " clientUpdateMs=" << std::chrono::duration<double, std::milli>(
                clientUpdateFinished - frameStarted).count()
            << " bridgeMs=" << std::chrono::duration<double, std::milli>(
                bridgeFinished - clientUpdateFinished).count()
            << " autoEnterMs=" << std::chrono::duration<double, std::milli>(
                autoEnterFinished - bridgeFinished).count()
            << " playerSyncMs=" << std::chrono::duration<double, std::milli>(
                playerSyncFinished - playerListPreFinished).count()
            << " playerListMs="
            << (std::chrono::duration<double, std::milli>(
                    playerListPreFinished - autoEnterFinished).count()
                + std::chrono::duration<double, std::milli>(
                    playerListFinished - playerSyncFinished).count())
            << " actorSyncMs=" << std::chrono::duration<double, std::milli>(
                actorSyncFinished - playerListFinished).count()
            << " worldSyncMs=" << std::chrono::duration<double, std::milli>(
                worldSyncFinished - actorSyncFinished).count()
            << " chatAndChargenMs=" << std::chrono::duration<double, std::milli>(
                frameFinished - worldSyncFinished).count();
    }

    // -- Chargen completion watcher ------------------------------------------
    // Fires once when the player is in a cell after chargen dialogs are shown.
    if (mCharGenWatching && mIsNewCharacter)
    {
        try
        {
            // Cell-presence is robust: fires regardless of how chargen ends.
            // bypass=true already set sCharGenState=-1 so we can't use that.
            const bool inCell = MWBase::Environment::get()
                                    .getWorld()->getPlayerPtr().isInCell();
            if (inCell)
            {
                mCharGenWatching = false;
                mIsNewCharacter  = false;

                Log(Debug::Info) << "[MP] Chargen complete Ã¢â‚¬â€ notifying server";

                sendChargenUpdate(true, "complete", true);

            }

        }
        catch (const std::exception& e)
        {
            Log(Debug::Warning) << "[MP] chargen watcher error: " << e.what();
            mCharGenWatching = false;
        }
    }
}

// ---------------------------------------------------------------------------
void Main::postMechanicsUpdate()
{
    if (mClient && mClient->isConnected() && mPlayerList)
        mPlayerList->refreshLocalDriverPassengerAttachments();
}

// ---------------------------------------------------------------------------
void Main::postWorldUpdate()
{
    if (!mClient || !mClient->isConnected())
        return;

    if (mActorSync)
        mActorSync->updateLoadedCellBootstrapVisuals();
}

// ---------------------------------------------------------------------------
void Main::postViewerUpdateTraversal()
{
    if (!mClient || !mClient->isConnected())
        return;

    if (mActorSync)
        mActorSync->revealBootstrapDeathVisualsAfterViewerUpdate();
}

// ---------------------------------------------------------------------------
bool Main::captureCurrentChargenData(const char* context)
{
    try
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        if (!world)
            return false;

        MWWorld::Ptr playerPtr = world->getPlayerPtr();
        if (playerPtr.isEmpty())
            return false;

        const auto* npcRef = playerPtr.get<ESM::NPC>();
        if (!npcRef || !npcRef->mBase)
            return false;

        const ESM::NPC* npc = npcRef->mBase;
        BasePlayer& local = mPlayerSync->localPlayer();

        local.race = npc->mRace.serializeText();
        local.headMesh = npc->mHead.serializeText();
        local.hairMesh = npc->mHair.serializeText();
        local.isMale = npc->isMale();

        const ESM::Class* cls = world->getStore().get<ESM::Class>().search(npc->mClass);
        if (cls)
        {
            local.charClass = *cls;
            local.charClass.mId = npc->mClass;
        }
        else
            local.charClass.mId = npc->mClass;

        local.birthSign = world->getPlayer().getBirthSign().serializeText();
        return true;
    }
    catch (const std::exception& e)
    {
        Log(Debug::Warning) << "[MP] Could not read chargen data for " << context << ": " << e.what();
        return false;
    }
}

// ---------------------------------------------------------------------------
std::string Main::currentChargenDataKey()
{
    const BasePlayer& local = mPlayerSync->localPlayer();
    std::ostringstream key;
    key << local.race << '\n'
        << local.headMesh << '\n'
        << local.hairMesh << '\n'
        << local.isMale << '\n'
        << local.charClass.mId.serializeText() << '\n'
        << local.charClass.mName << '\n'
        << local.birthSign;
    return key.str();
}

// ---------------------------------------------------------------------------
void Main::sendChargenUpdate(bool complete, const char* reason, bool includeInventoryAndEquipment)
{
    if (!mClient || !mClient->isConnected())
        return;

    if (!captureCurrentChargenData(reason))
        return;

    PacketPlayerCharGen pkt;
    pkt.setPlayer(&mPlayerSync->localPlayer());
    pkt.isComplete = complete;
    mClient->sendReliable(pkt.encode());

    const BasePlayer& local = mPlayerSync->localPlayer();
    Log(Debug::Info) << "[MP] Chargen update sent: reason=" << reason
                     << " complete=" << complete
                     << " race=" << local.race
                     << " head=" << local.headMesh
                     << " hair=" << local.hairMesh
                     << " class=" << local.charClass.mId.toString()
                     << " birthSign=" << local.birthSign;

    mLastCharGenDataKey = currentChargenDataKey();
    mPlayerSync->forceFullSync(includeInventoryAndEquipment);
}

// ---------------------------------------------------------------------------
void Main::pollChargenAppearance(float dt)
{
    if (!mIsNewCharacter || !mClient || !mClient->isConnected())
    {
        mCharGenAppearanceSyncTimer = 0.f;
        return;
    }

    mCharGenAppearanceSyncTimer -= dt;
    if (mCharGenAppearanceSyncTimer > 0.f)
        return;
    mCharGenAppearanceSyncTimer = 0.25f;

    if (!captureCurrentChargenData("live"))
        return;

    const std::string key = currentChargenDataKey();
    if (mLastCharGenDataKey.empty())
    {
        mLastCharGenDataKey = key;
        return;
    }

    if (key != mLastCharGenDataKey)
        sendChargenUpdate(false, "live", false);
}

// ---------------------------------------------------------------------------
void Main::onConnected()
{
    Log(Debug::Info) << "[MP] Connected Ã¢â‚¬â€ sending handshake";

    mCharacterDataReady = false;
    mResolvedContentFingerprint.clear();
    mBootstrapCompilationComplete = false;
    mRuntimeContentBootstrapGate.reset();
    mAuthoritativeStateBootstrapGate.reset();
    mServerLuaPackageTransfer.reset();
    mServerLuaPackagesStaged = false;
    mPendingCrimeMutations.clear();
    mCrimeMutationInFlight.clear();
    mPendingGuardArrestRequests.clear();
    mGuardArrestDialogue = {};
    mWorldStateSync->resetSessionState();
    mObjectSync->resetSessionState();
    mWorldObjectSync->resetSessionState();
    mPlayerSync->resetInventoryAuthorityState();
    mPlayerSync->resetCrimeStateSync();
    mPlayerSync->resetFactionStateSync();
    mPlayerSync->resetTopicStateSync();
    mPlayerSync->resetMechanicsSnapshotState();

    // Build and send handshake
    PacketHandshake hs;
    hs.clientVersion   = std::string(MultiplayerBuildVersion);
    hs.protocolVersion = MultiplayerProtocolVersion;
    hs.playerName      = mPlayerName;
    hs.passwordHash    = mPasswordHash;
    hs.isRegistration  = mIsRegistration;
    hs.actorSyncProtocolVersion = ActorSyncProtocolVersionV2;
    hs.openMWLuaApiVersion = Version::getLuaApiRevision();

    try
    {
        hs.resolvedContentFingerprint
            = MWMP::resolvedContentFingerprint(*MWBase::Environment::get().getESMStore());
        mResolvedContentFingerprint = hs.resolvedContentFingerprint;
    }
    catch (const std::exception& e)
    {
        Log(Debug::Error) << "[MP] Failed to fingerprint resolved content: " << e.what();
    }

    const auto& contentFiles = MWBase::Environment::get().getWorld()->getContentFiles();
    hs.plugins.reserve(contentFiles.size());
    for (const std::string& filename : contentFiles)
    {
        PacketHandshake::PluginEntry plugin;
        plugin.filename = filename;
        try
        {
            if (!sFileCollections)
                throw std::runtime_error("active file collections are unavailable");

            const std::filesystem::path path = sFileCollections->getPath(filename);
            std::ifstream stream(path, std::ios::binary);
            if (!stream)
                throw std::runtime_error("could not open resolved content file");
            plugin.sha256 = crypto::sha256hex(stream);
            if (!stream.eof())
                throw std::runtime_error("error while reading content file");
        }
        catch (const std::exception& e)
        {
            Log(Debug::Error) << "[MP] Failed to hash content file '" << filename << "': " << e.what();
        }
        hs.plugins.push_back(std::move(plugin));
    }
    Log(Debug::Info) << "[MP] Handshake includes " << hs.plugins.size() << " content-file SHA-256 checksums";

    try
    {
        const VFS::Manager& vfs = *MWBase::Environment::get().getResourceSystem()->getVFS();
        const ESM::LuaScriptsCfg scripts = MWBase::Environment::get().getESMStore()->getLuaScriptsCfg();
        hs.luaScripts.reserve(scripts.mScripts.size());
        for (const ESM::LuaScriptCfg& script : scripts.mScripts)
        {
            PacketHandshake::PluginEntry entry;
            entry.filename = script.mScriptPath.value();
            Files::IStreamPtr stream = vfs.get(script.mScriptPath);
            entry.sha256 = crypto::sha256hex(*stream);
            if (!stream->eof())
                throw std::runtime_error("error while reading Lua script '" + entry.filename + "'");
            hs.luaScripts.push_back(std::move(entry));
        }
    }
    catch (const std::exception& e)
    {
        Log(Debug::Error) << "[MP] Failed to build Lua content manifest: " << e.what();
        hs.luaScripts.clear();
    }
    Log(Debug::Info) << "[MP] Resolved content fingerprint=" << hs.resolvedContentFingerprint
                     << " Lua scripts=" << hs.luaScripts.size();
    
    if (mUseKeypair)
    {
        mLocalPublicKey = Identity::getPublicKeyBase64(mHost, mPort);
        if (!mLocalPublicKey.empty())
        {
            hs.publicKey = mLocalPublicKey;
            mIsLinked    = true;
            Log(Debug::Info) << "[MP] Sending keypair auth for " << mPlayerName;
        }
        else
            mIsLinked = false;
    }
    else
    {
        mLocalPublicKey.clear();
        mIsLinked = false;
        Log(Debug::Info) << "[MP] Connected to server";
    }
    mClient->sendReliable(hs.encode());
}

// ---------------------------------------------------------------------------
void Main::onDisconnected()
{
    Log(Debug::Warning) << "[MP] Disconnected from server";
    if (mRecordCreationManager)
        mRecordCreationManager->cancelAll();
    if (mAlchemyCreationManager)
        mAlchemyCreationManager->cancelAll();
    if (mEnchantingCreationManager)
        mEnchantingCreationManager->cancelAll();
    // If we were already in-world, request a main-menu return on the next frame.
    // Do NOT touch engine state here - this fires inside mClient->update() and
    // must remain engine-API-free to stay thread-safe.
    if (mWorldReady)
        mUnexpectedDisconnect = true;
    mWorldReady         = false;
    mCharacterDataReady = false;
    mBootstrapCompilationComplete = false;
    mRuntimeContentBootstrapGate.reset();
    mAuthoritativeStateBootstrapGate.reset();
    mServerLuaPackageTransfer.reset();
    mServerLuaPackagesStaged = false;
    mPendingCrimeMutations.clear();
    mCrimeMutationInFlight.clear();
    mPendingGuardArrestRequests.clear();
    mGuardArrestDialogue = {};
    mServerLuaCleanupPending = true;
    mHasSavedSpellbook  = false;
    mCharacterId        = 0;
    mCharSelectError.clear();
    mCharacterName.clear();
    mCharacterList.clear();
    mAutoCharacterSelectSent = false;
    mAutoEnterPending = false;
    mAutoEnterAllowNewCharacterUi = false;
    mIsLinked       = false;
    mLocalPublicKey.clear();
    mGuardArrestMode = GuardArrestMode::Combat;
    MWPhysics::resetSurfPhysicsSettings();
    // Clear all per-session actor/cell tracking so that stale MWWorld::Ptr
    // references from the now-dying game world are never accessed on reconnect.
    if (mActorSync)
        mActorSync->resetSessionState();
    if (mWorldStateSync)
        mWorldStateSync->resetSessionState();
    if (mObjectSync)
        mObjectSync->resetSessionState();
    if (mWorldObjectSync)
        mWorldObjectSync->resetSessionState();
    // Spellbook sync state is per-session: the authoritative revision token,
    // baseline and in-flight gate must not leak into the next connection.
    if (mPlayerSync)
    {
        mPlayerSync->resetSpellbookSyncState();
        mPlayerSync->resetJournalSyncState();
        mPlayerSync->resetCrimeStateSync();
        mPlayerSync->resetFactionStateSync();
        mPlayerSync->resetTopicStateSync();
        mPlayerSync->resetMechanicsSnapshotState();
    }
}

// ---------------------------------------------------------------------------
/*static*/
void Main::setStaticKeysDir(const std::filesystem::path& dir)
{
    Identity::setKeysDir(dir);
}

// ---------------------------------------------------------------------------
/*static*/
void Main::setFileCollections(const Files::Collections* collections)
{
    sFileCollections = collections;
}

// ---------------------------------------------------------------------------
bool Main::isNetworkDisconnected() const
{
    return mClient->getState() == ConnectionState::Disconnected;
}

// ---------------------------------------------------------------------------
/*static*/
bool Main::isConnected()
{
    return sInstance && sInstance->mClient && sInstance->mClient->isConnected();
}

// ---------------------------------------------------------------------------
void Main::disconnect(const std::string& reason)
{
    if (mClient && mClient->isConnected())
    {
        if (mPlayerSync)
            mPlayerSync->flushPersistentStats();
        MWBase::Environment::get().getLuaManager()->requestMultiplayerPlayerScriptsCheckpoint();
        mClient->disconnect(reason);
    }
}

// ---------------------------------------------------------------------------
void Main::sendCharacterSelect(const std::string& charName, bool isNew)
{
    if (!mClient)
        return;

    mCharSelectError.clear();
    mCharacterDataReady = false;
    mBootstrapCompilationComplete = false;
    mRuntimeContentBootstrapGate.reset();
    mAuthoritativeStateBootstrapGate.reset();
    mWorldStateSync->beginRuntimeContentBootstrap();
    mPlayerSync->resetInventoryAuthorityState();
    mPlayerSync->resetCrimeStateSync();
    mPlayerSync->resetFactionStateSync();
    mPlayerSync->resetTopicStateSync();
    mCharacterId = 0;

    PacketCharacterSelect pkt;
    pkt.charName = charName;
    pkt.isNew = isNew;
    mClient->sendReliable(pkt.encode());

    Log(Debug::Info) << "[MP] Sent CharacterSelect: '" << charName << "' isNew=" << isNew;
}

// ---------------------------------------------------------------------------
void Main::tryAutoSelectCharacter()
{
    if (mAutoCharacterName.empty() || mAutoCharacterSelectSent)
        return;

    const auto characterIt = std::find_if(mCharacterList.begin(), mCharacterList.end(),
        [&](const CharacterEntry& entry) { return entry.name == mAutoCharacterName; });
    if (characterIt == mCharacterList.end())
    {
        mRejectReason = "Auto character '" + mAutoCharacterName + "' was not found on account '" + mPlayerName + "'.";
        Log(Debug::Error) << "[MP] " << mRejectReason;
        disconnect(mRejectReason);
        return;
    }

    mAutoCharacterSelectSent = true;
    mAutoEnterAllowNewCharacterUi = characterIt->isNew;
    Log(Debug::Info) << "[MP] Auto-selecting character '" << characterIt->name
                     << "' incomplete=" << characterIt->isNew;
    sendCharacterSelect(characterIt->name, false);
}

// ---------------------------------------------------------------------------
bool Main::enterSelectedCharacterWorld(bool allowNewCharacterUi)
{
    MWBase::WindowManager* windowManager = MWBase::Environment::get().getWindowManager();

    const bool isNew = isNewCharacter();
    const std::string spawnCell = getSpawnCell();
    const std::string worldName = getCharacterName().empty()
        ? getPlayerSync().localPlayer().name
        : getCharacterName();

    if (isNew && !allowNewCharacterUi)
    {
        Log(Debug::Error) << "[MP] Auto-enter refused incomplete/new character '" << worldName << "'";
        disconnect("Auto-enter refused incomplete character");
        return false;
    }

    const auto normalizedIdentityPart = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(),
            [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    };
    const std::string storageNamespace = crypto::sha256hex(
        normalizedIdentityPart(mHost) + ":" + std::to_string(mPort) + "\n" + normalizedIdentityPart(mPlayerName));
    const std::string characterKey = mCharacterId > 0
        ? "id-" + std::to_string(mCharacterId)
        : "name-" + crypto::sha256hex(normalizedIdentityPart(worldName));
    std::string storageError;
    if (!MWBase::Environment::get().getLuaManager()->bindMultiplayerPlayerStorage(
            storageNamespace, characterKey, worldName, !isNew, storageError))
    {
        Log(Debug::Error) << "[MP] " << storageError;
        windowManager->messageBox(storageError);
        return false;
    }

    windowManager->removeGuiMode(MWGui::GM_MainMenu);

    if (isNew)
    {
        Log(Debug::Info) << "[MP] New character - spawning in: " << spawnCell;
        mLastCharGenDataKey.clear();
        mCharGenAppearanceSyncTimer = 0.f;
        MWBase::Environment::get().getStateManager()->newGame(true);
        applySelectedCharacterSpawn(spawnCell, "new character");
        getPlayerSync().applyAuthoritativeCrimeStateToPlayer();
        windowManager->updatePlayer();
        if (!worldName.empty())
            MWBase::Environment::get().getMechanicsManager()->setPlayerName(worldName);
        Log(Debug::Info) << "[MP] New character initial world sync";
        getPlayerSync().forceFullSync(false);
        windowManager->setCharGenCompleteCallback(
            []() {
                if (Main::isInitialised())
                {
                    Log(Debug::Info) << "[MP] Chargen complete - arming watcher";
                    Main::get().startWatchingCharGen();
                }
                MWBase::Environment::get().getWindowManager()->setNewGame(false);
            });
        windowManager->startCharGen();
        windowManager->pushGuiMode(MWGui::GM_Race);
        return true;
    }

    Log(Debug::Info) << "[MP] Returning player - restoring in: " << spawnCell;
    MWBase::Environment::get().getStateManager()->newGame(true);
    windowManager->updatePlayer();

    if (!worldName.empty())
        MWBase::Environment::get().getMechanicsManager()->setPlayerName(worldName);

    auto mechanicsManager = MWBase::Environment::get().getMechanicsManager();
    try
    {
        const std::string race = getRestoredRace();
        const std::string head = getRestoredHeadMesh();
        const std::string hair = getRestoredHairMesh();
        const bool isMale = getRestoredIsMale();
        const std::string className = getRestoredClassName();
        const std::string birthSign = getRestoredBirthSign();

        if (!race.empty())
        {
            mechanicsManager->setPlayerRace(ESM::RefId::deserializeText(race), isMale,
                ESM::RefId::deserializeText(head), ESM::RefId::deserializeText(hair));
            windowManager->getInventoryWindow()->rebuildAvatar();
        }
        if (!className.empty())
        {
            ESM::Class playerClass;
            playerClass.mName = className;
            playerClass.mData = getPlayerSync().localPlayer().charClass.mData;
            playerClass.mRecordFlags = 0;
            mechanicsManager->setPlayerClass(playerClass);
        }
        if (!birthSign.empty())
            mechanicsManager->setPlayerBirthsign(ESM::RefId::deserializeText(birthSign));
    }
    catch (const std::exception& e)
    {
        Log(Debug::Warning) << "[MP] Chargen restore error: " << e.what();
    }

    applySelectedCharacterSpawn(spawnCell, "returning player");

    getPlayerSync().applyAuthoritativeCrimeStateToPlayer();
    getPlayerSync().applyRestoredStatsToPlayer();
    MWBase::Environment::get().getLuaManager()->restorePendingMultiplayerPlayerScripts();
    Log(Debug::Info) << "[MP] Returning player restore complete - sending full sync";
    // A returning character with a persisted spellbook must wait for the
    // server's authoritative Set before the client establishes or sends a
    // local learned-spell baseline. CharacterData and PlayerSpellbook use
    // independent reliable lanes, so the restore may arrive after world
    // entry even though the server sent it immediately after CharacterData.
    getPlayerSync().forceFullSync(false, !mHasSavedSpellbook);
    return true;
}

// ---------------------------------------------------------------------------
void Main::applySelectedCharacterSpawn(const std::string& spawnCell, const char* context)
{
    const std::string targetCell = spawnCell.empty() ? "toddtest" : spawnCell;
    const float sx = getSpawnX();
    const float sy = getSpawnY();
    const float sz = getSpawnZ();
    const float rx = getSpawnRotX();
    const float ry = getSpawnRotY();
    const float rz = getSpawnRotZ();
    const bool hasSavedPos = sx != 0.f || sy != 0.f || sz != 0.f;

    MWBase::World* world = MWBase::Environment::get().getWorld();
    ESM::Position dest {};

    int exteriorGridX = 0;
    int exteriorGridY = 0;
    const bool isExteriorCellKey = targetCell.rfind("EXT:", 0) == 0;
    if (isExteriorCellKey)
    {
        if (std::sscanf(targetCell.c_str(), "EXT:%d,%d", &exteriorGridX, &exteriorGridY) != 2)
            throw std::runtime_error("Invalid saved exterior cell key: " + targetCell);

        // Player database exterior locations use the canonical EXT:x,y key.
        // Resolve that key directly instead of passing it through the named-cell
        // lookup, which treats it as an interior name and aborts the restore.
        dest.pos[0] = sx;
        dest.pos[1] = sy;
        dest.pos[2] = sz;
        dest.rot[0] = rx;
        dest.rot[1] = ry;
        dest.rot[2] = rz;
        world->changeToCell(
            ESM::Cell::generateIdForCell(true, {}, exteriorGridX, exteriorGridY), dest, true);
    }
    else
    {
        const auto interiorId = world->findInteriorPosition(targetCell, dest);
        if (!interiorId.empty())
        {
            if (hasSavedPos)
            {
                dest.pos[0] = sx;
                dest.pos[1] = sy;
                dest.pos[2] = sz;
                dest.rot[0] = rx;
                dest.rot[1] = ry;
                dest.rot[2] = rz;
            }
            world->changeToCell(interiorId, dest, true);
        }
        else
        {
            const auto exteriorId = world->findExteriorPosition(targetCell, dest);
            if (!exteriorId.empty())
            {
                if (hasSavedPos)
                {
                    dest.pos[0] = sx;
                    dest.pos[1] = sy;
                    dest.pos[2] = sz;
                    dest.rot[0] = rx;
                    dest.rot[1] = ry;
                    dest.rot[2] = rz;
                }
                world->changeToCell(exteriorId, dest, true);
            }
            else
                world->changeToInteriorCell(targetCell, dest, true);
        }
    }

    Log(Debug::Info) << "[MP] Applied " << context << " spawn: cell=" << targetCell
                     << " pos=(" << dest.pos[0] << "," << dest.pos[1] << "," << dest.pos[2] << ")"
                     << " rot=(" << dest.rot[0] << "," << dest.rot[1] << "," << dest.rot[2] << ")";
}

// ---------------------------------------------------------------------------
void Main::tryAutoEnterWorld()
{
    if (!mAutoEnterPending || !mCharacterDataReady)
        return;

    const bool allowNewCharacterUi = mAutoEnterAllowNewCharacterUi;
    mAutoEnterPending = false;
    mAutoEnterAllowNewCharacterUi = false;
    enterSelectedCharacterWorld(allowNewCharacterUi);
}

// ---------------------------------------------------------------------------
void Main::tryFinalizePendingCharacterData()
{
    // Runtime definitions/policy and authoritative gameplay state remain
    // separate bootstrap domains and meet only at this final world-entry gate.
    if (auto contentReady = mRuntimeContentBootstrapGate.takeReadyCharacterData())
        mAuthoritativeStateBootstrapGate.retainCharacterData(std::move(*contentReady));
    mAuthoritativeStateBootstrapGate.setStateReady(
        mPlayerSync->hasAuthoritativeCrimeState() && mPlayerSync->hasAuthoritativeFactionState()
        && mPlayerSync->hasAuthoritativeTopicState());
    auto characterData = mAuthoritativeStateBootstrapGate.takeReadyCharacterData();
    if (characterData)
        finalizeCharacterData(std::move(*characterData));
}

// ---------------------------------------------------------------------------
void Main::tryActivateServerLuaPackages()
{
    if (!mRuntimeContentBootstrapGate.isRuntimeContentReady()
        || mRuntimeContentBootstrapGate.isServerLuaReady()
        || mRuntimeContentBootstrapGate.state()
            == RuntimeContentBootstrapGate<PacketCharacterData>::State::Failed)
        return;

    MWBase::LuaManager* luaManager = MWBase::Environment::get().getLuaManager();
    if (luaManager->hasActiveMultiplayerLuaPackages())
    {
        mRuntimeContentBootstrapGate.finishServerLua(true);
        tryFinalizePendingCharacterData();
        return;
    }
    if (!mServerLuaPackagesStaged)
        return;
    if (!luaManager->hasStagedMultiplayerLuaPackages())
    {
        failServerLuaPackageBootstrap("staged package readiness was lost before activation");
        return;
    }

    std::string error;
    if (!luaManager->activateStagedMultiplayerLuaPackages(error))
    {
        failServerLuaPackageBootstrap("activation failed: " + error);
        return;
    }
    mRuntimeContentBootstrapGate.finishServerLua(true);
    Log(Debug::Info) << "[ServerLuaPackages] Activated after runtime content bootstrap";
    tryFinalizePendingCharacterData();
}

// ---------------------------------------------------------------------------
void Main::failRuntimeContentBootstrap(std::string error)
{
    mRuntimeContentBootstrapGate.finishRuntimeContent(false, std::move(error));
    mCharacterDataReady = false;
    mRejectReason = "Runtime content bootstrap failed: " + mRuntimeContentBootstrapGate.error();
    Log(Debug::Error) << "[MP] " << mRejectReason;
    disconnect(mRejectReason);
}

// ---------------------------------------------------------------------------
void Main::failServerLuaPackageBootstrap(std::string error)
{
    mRuntimeContentBootstrapGate.finishServerLua(false, error);
    mServerLuaPackageTransfer.reset();
    mServerLuaPackagesStaged = false;
    mCharacterDataReady = false;
    mRejectReason = "Server Lua package bootstrap failed: " + std::move(error);
    Log(Debug::Error) << "[MP] " << mRejectReason;
    disconnect(mRejectReason);
}

// ---------------------------------------------------------------------------
void Main::clearServerLuaPackageSession()
{
    mServerLuaCleanupPending = false;
    mServerLuaPackageTransfer.reset();
    mServerLuaPackagesStaged = false;
    MWBase::Environment::get().getLuaManager()->clearMultiplayerLuaPackages();
}

// ---------------------------------------------------------------------------
void Main::finalizeCharacterData(PacketCharacterData cd)
{
    mIsNewCharacter = cd.isNewCharacter;
    mHasSavedSpellbook = cd.hasSavedSpellbook;
    mCharacterId    = cd.characterId;
    mCharacterName  = cd.characterName;
    // Update the sync layer name to the character slot name so
    // PacketPlayerBaseInfo (sent by forceFullSync in CharacterSelectDialog
    // after setPlayerRace()) broadcasts the correct name to other players.
    if (!cd.characterName.empty())
        mPlayerSync->localPlayer().name = cd.characterName;
    mSpawnCell      = cd.spawnCell;
    mSpawnPos[0] = cd.spawnX;
    mSpawnPos[1] = cd.spawnY;
    mSpawnPos[2] = cd.spawnZ;
    mSpawnRot[0] = cd.spawnRotX;
    mSpawnRot[1] = cd.spawnRotY;
    mSpawnRot[2] = cd.spawnRotZ;

    mRestoredRace      = cd.race;
    mRestoredHeadMesh  = cd.headMesh;
    mRestoredHairMesh  = cd.hairMesh;
    mRestoredIsMale    = cd.isMale;
    mRestoredClassId   = cd.classId;
    mRestoredClassName = cd.className;
    mRestoredBirthSign = cd.birthSign;
    mRestoredClassData = cd.classData;
    BasePlayer& localPlayer = mPlayerSync->localPlayer();
    localPlayer.hasSavedStats = cd.hasSavedStats;
    localPlayer.dynamicStats = cd.dynamicStats;
    localPlayer.attributes = cd.attributes;
    localPlayer.skills = cd.skills;
    localPlayer.level = cd.level;
    localPlayer.levelProgress = cd.levelProgress;
    if (cd.hasSavedStats)
    {
        BasePlayer restoredStats;
        restoredStats.hasSavedStats = true;
        restoredStats.dynamicStats = cd.dynamicStats;
        restoredStats.attributes = cd.attributes;
        restoredStats.skills = cd.skills;
        restoredStats.level = cd.level;
        restoredStats.levelProgress = cd.levelProgress;
        mPlayerSync->queueRestoredStats(restoredStats);
    }

    if (!cd.classData.empty())
    {
        std::istringstream ss(cd.classData);
        char sep;
        auto& d = localPlayer.charClass.mData;
        ss >> d.mSpecialization;
        for (auto& v : d.mAttribute)  { ss >> sep >> v; }
        for (auto& row : d.mSkills)   for (auto& v : row) { ss >> sep >> v; }
        ss >> sep >> d.mIsPlayable;
        ss >> sep >> d.mServices;
        localPlayer.charClass.mName = cd.className;
    }

    Log(Debug::Info) << "[MP] CharacterData finalized after runtime content bootstrap: newChar="
                     << (mIsNewCharacter ? "yes" : "no")
                     << " charId=" << mCharacterId
                     << " charName=" << mCharacterName
                     << " cell=" << mSpawnCell
                     << " pos=(" << mSpawnPos[0] << "," << mSpawnPos[1] << "," << mSpawnPos[2] << ")"
                     << " rot=(" << mSpawnRot[0] << "," << mSpawnRot[1] << "," << mSpawnRot[2] << ")"
                     << " race=" << mRestoredRace
                     << " class=" << mRestoredClassName;

    // Run optional whole-content diagnostics only after typed SCPT and
    // Dialogue overlays have reached the effective store.
    if (!mBootstrapCompilationComplete)
    {
        if (mCompileAllScriptsAfterBootstrap)
        {
            const auto result = MWBase::Environment::get().getScriptManager()->compileAll();
            if (result.first)
                Log(Debug::Info) << "compiled " << result.second << " of " << result.first
                                 << " scripts after multiplayer content bootstrap ("
                                 << 100 * static_cast<double>(result.second) / result.first << "%)";
        }
        if (mCompileAllDialogueAfterBootstrap && mCompilerExtensions != nullptr)
        {
            const auto result = MWDialogue::ScriptTest::compileAll(
                mCompilerExtensions, mCompilerWarningsMode);
            if (result.first)
                Log(Debug::Info) << "compiled " << result.second << " of " << result.first
                                 << " dialogue scripts after multiplayer content bootstrap ("
                                 << 100 * static_cast<double>(result.second) / result.first << "%)";
        }
        mBootstrapCompilationComplete = true;
    }

    mCharacterDataReady = true;
    if (!mAutoCharacterName.empty())
    {
        if (mIsNewCharacter)
            mAutoEnterAllowNewCharacterUi = true;
        mAutoEnterPending = true;
    }
    // NOTE: do NOT call forceFullSync() here for returning players.
    // At this point world->getPlayerPtr() still has the blank template
    // NPC record - setPlayerRace() has not been called yet.
    // CharacterSelectDialog::startReturningPlayer() calls forceFullSync()
    // *after* setPlayerRace() so the BaseInfo packet carries the real
    // race/head/hair. New characters use the chargen-complete watcher.
}

// ---------------------------------------------------------------------------
void Main::registerProtocolHandlers()
{
    auto& proto = *mProtocol;

    // --- Handshake response ---
    proto.registerHandler(PacketType::HandshakeResponse,
        [this](const uint8_t* data, size_t size)
        {
            PacketHandshakeResponse rsp;
            if (!rsp.decode(data, size)) return;

            if (!rsp.accepted)
            {
                mRejectReason = rsp.rejectReason;
                Log(Debug::Error) << "[MP] Server rejected handshake: " << mRejectReason;
                mClient->disconnect("Rejected: " + mRejectReason);
                return;
            }

            if (rsp.protocolVersion != MultiplayerProtocolVersion)
            {
                mRejectReason = "Multiplayer protocol mismatch: server=" + std::to_string(rsp.protocolVersion)
                    + " client=" + std::to_string(MultiplayerProtocolVersion);
                Log(Debug::Error) << "[MP] " << mRejectReason;
                mClient->disconnect(mRejectReason);
                return;
            }

            if (rsp.contentManifestVersion != ContentManifestVersion
                || rsp.contentApiVersion != ContentApiVersion
                || rsp.dynamicRecordWireVersion != records::CurrentWireVersion
                || rsp.capabilityManifestVersion != RuntimeRecordCapabilityManifestVersion
                || rsp.serverLuaPackageManifestVersion != serverlua::ServerLuaPackageManifestVersion
                || rsp.multiplayerLuaApiVersion != serverlua::MultiplayerLuaApiVersion
                || rsp.openMWLuaApiVersion != static_cast<std::uint32_t>(Version::getLuaApiRevision()))
            {
                mRejectReason = "Server content/runtime/Lua manifest versions are incompatible";
                Log(Debug::Error) << "[MP] " << mRejectReason;
                mClient->disconnect(mRejectReason);
                return;
            }

            // The resolved store cannot change between our handshake and this response.
            // Reuse the fingerprint already sent instead of hashing the full ESM store
            // again inside the network packet callback. Recompute only if the initial
            // fingerprint attempt failed, preserving the previous recovery behavior.
            if (mResolvedContentFingerprint.empty() && !rsp.resolvedContentFingerprint.empty())
                mResolvedContentFingerprint
                    = MWMP::resolvedContentFingerprint(*MWBase::Environment::get().getESMStore());
            if (!rsp.resolvedContentFingerprint.empty()
                && rsp.resolvedContentFingerprint != mResolvedContentFingerprint)
            {
                mRejectReason = "Resolved content changed during connection setup";
                Log(Debug::Error) << "[MP] " << mRejectReason;
                mClient->disconnect(mRejectReason);
                return;
            }

            mPlayerSync->localPlayer().guid = rsp.assignedGuid;

            Log(Debug::Info) << "[MP] Handshake accepted, guid=" << rsp.assignedGuid
                             << " server=" << rsp.serverVersion
                             << " protocol=" << rsp.protocolVersion
                             << " actorSyncProtocol=" << rsp.actorSyncProtocolVersion;
            // mWorldReady is set when PacketCharacterList arrives.
        });

    proto.registerHandler(PacketType::ServerLuaPackageManifest,
        [this](const uint8_t* data, size_t size)
        {
            if (mServerLuaPackagesStaged
                || mServerLuaPackageTransfer.state() != serverlua::PackageTransfer::State::Empty)
            {
                failServerLuaPackageBootstrap("duplicate package manifest");
                return;
            }
            PacketServerLuaPackageManifest packet;
            if (!packet.decode(data, size))
            {
                failServerLuaPackageBootstrap("malformed package manifest");
                return;
            }
            const std::uint64_t generation = packet.packageSet.generation;
            if (!mServerLuaPackageTransfer.begin(std::move(packet.packageSet),
                    static_cast<std::uint32_t>(Version::getLuaApiRevision()),
                    serverlua::MultiplayerLuaApiVersion))
            {
                failServerLuaPackageBootstrap(mServerLuaPackageTransfer.error());
                return;
            }
            Log(Debug::Info) << "[ServerLuaPackages] Receiving generation="
                             << generation;
        });

    proto.registerHandler(PacketType::ServerLuaPackageChunk,
        [this](const uint8_t* data, size_t size)
        {
            PacketServerLuaPackageChunk packet;
            if (!packet.decode(data, size))
            {
                failServerLuaPackageBootstrap("malformed package chunk");
                return;
            }
            if (!mServerLuaPackageTransfer.receive(packet.generation, packet.packageId,
                    packet.packageHash, packet.filePath, packet.offset, packet.bytes))
                failServerLuaPackageBootstrap(mServerLuaPackageTransfer.error());
        });

    proto.registerHandler(PacketType::ServerLuaPackageBootstrapComplete,
        [this](const uint8_t* data, size_t size)
        {
            PacketServerLuaPackageBootstrapComplete packet;
            if (!packet.decode(data, size))
            {
                failServerLuaPackageBootstrap("malformed package completion marker");
                return;
            }
            if (!mServerLuaPackageTransfer.finish(packet.generation, packet.packageSetHash,
                    static_cast<std::uint32_t>(Version::getLuaApiRevision()),
                    serverlua::MultiplayerLuaApiVersion))
            {
                failServerLuaPackageBootstrap(mServerLuaPackageTransfer.error());
                return;
            }
            auto packageSet = mServerLuaPackageTransfer.takeReadyPackageSet();
            if (!packageSet)
            {
                failServerLuaPackageBootstrap("verified package set was not available for staging");
                return;
            }
            std::string error;
            if (!MWBase::Environment::get().getLuaManager()->stageMultiplayerLuaPackages(
                    std::move(*packageSet), error))
            {
                failServerLuaPackageBootstrap("Lua staging failed: " + error);
                return;
            }
            mServerLuaPackagesStaged = true;
            Log(Debug::Info) << "[ServerLuaPackages] Bootstrap staged and awaiting pre-world activation";
            tryActivateServerLuaPackages();
        });

    // --- Ed25519 challenge - server sends 32-byte nonce, we sign and respond ---
    proto.registerHandler(PacketType::Challenge,
        [this](const uint8_t* data, size_t size)
        {
            Log(Debug::Info) << "[MP] Challenge received, signing nonce";
            PacketChallenge pkt;
            if (!pkt.decode(data, size))
            {
                Log(Debug::Error) << "[MP] Challenge decode FAILED";
                return;
            }
            uint8_t sig[64] = {};
            if (!Identity::sign(mHost, mPort, pkt.nonce, sig))
            {
                Log(Debug::Error) << "[MP] Identity::sign FAILED for "
                                  << mHost << ":" << mPort;
                mClient->disconnect("No keypair");
                return;
            }
            PacketChallengeResponse rsp;
            std::memcpy(rsp.signature, sig, 64);
            mClient->sendReliable(rsp.encode());
            Log(Debug::Info) << "[MP] Challenge response sent";
            });
    Log(Debug::Info) << "[MP] Challenge handler registered, type="
                     << static_cast<int>(PacketType::Challenge);

// --- Character list - arrives immediately after accepted handshake ---
    proto.registerHandler(PacketType::CharacterList,
        [this](const uint8_t* data, size_t size)
        {
            PacketCharacterList pkt;
            if (!pkt.decode(data, size)) return;

            mCharacterList = pkt.characters;
            Log(Debug::Info) << "[MP] Received character list: "
                             << mCharacterList.size() << " character(s)";
            for (const auto& entry : mCharacterList)
            {
                Log(Debug::Info) << "[MP] Character entry"
                                 << " name=" << entry.name
                                 << " isNew=" << entry.isNew
                                 << " race=" << entry.race
                                 << " class=" << entry.className;
            }

            // Signal the active character-selection flow that the connection is ready so it can
            // open CharacterSelectDialog.
            mWorldReady = true;
            tryAutoSelectCharacter();
        });

    // --- Character select error - server rejected the CharacterSelect request ---
    proto.registerHandler(PacketType::CharacterSelectError,
        [this](const uint8_t* data, size_t size)
        {
            PacketCharacterSelectError err;
            if (!err.decode(data, size)) return;
            mCharSelectError = err.reason;
            Log(Debug::Warning) << "[MP] CharacterSelect rejected: " << mCharSelectError;
            if (!mAutoCharacterName.empty())
            {
                mRejectReason = mCharSelectError;
                disconnect("Auto CharacterSelect rejected: " + mCharSelectError);
            }
        });

    
    // --- Delete character response ---
    proto.registerHandler(PacketType::DeleteCharResponse,
        [this](const uint8_t* data, size_t size)
        {
            PacketDeleteCharResponse rsp;
            if (!rsp.decode(data, size)) return;
            mDeleteCharResponse = rsp;
            mDeleteCharResponseReady = true;
            Log(Debug::Info) << "[MP] DeleteCharResponse: success=" << rsp.success
                             << " char='" << rsp.charName << "'"
                             << (rsp.success ? "" : " error=" + rsp.error);
        });
// --- Character data - arrives after client sends PacketCharacterSelect ---
    proto.registerHandler(PacketType::CharacterData,
        [this](const uint8_t* data, size_t size)
        {
            PacketCharacterData cd;
            if (!cd.decode(data, size)) return;
            mRuntimeContentBootstrapGate.retainCharacterData(std::move(cd));
            tryFinalizePendingCharacterData();
        });

    // --- Remote player position ---
    proto.registerHandler(PacketType::PlayerPosition,
        [this](const uint8_t* data, size_t size)
        {
            BasePlayer tmp;
            PacketPlayerPosition pkt;
            pkt.setPlayer(&tmp);
            if (!pkt.decode(data, size)) return;
            if (tmp.guid == mPlayerSync->localPlayer().guid)
            {
                mPlayerSync->applyServerPositionCorrection(tmp);
                return;
            }
            if (tmp.guid == 0) return;

            auto* rp = mPlayerList->getPlayer(tmp.guid);
            if (rp) rp->onPositionUpdate(tmp, pkt.getSequence());
        });

    // --- Remote player cell change ---
    proto.registerHandler(PacketType::PlayerCellChange,
        [this](const uint8_t* data, size_t size)
        {
            BasePlayer tmp;
            PacketPlayerCellChange pkt;
            pkt.setPlayer(&tmp);
            if (!pkt.decode(data, size)) return;
            if (tmp.guid == mPlayerSync->localPlayer().guid)
            {
                mPlayerSync->applyServerCellChange(tmp);
                return;
            }

            auto* rp = mPlayerList->getPlayer(tmp.guid);
            if (rp) rp->onCellChange(tmp, pkt.getSequence());
        });

    // --- Remote player base info (join / appearance) ---
    proto.registerHandler(PacketType::PlayerBaseInfo,
        [this](const uint8_t* data, size_t size)
        {
            BasePlayer tmp;
            PacketPlayerBaseInfo pkt;
            pkt.setPlayer(&tmp);
            if (!pkt.decode(data, size)) return;

            if (tmp.guid == 0)
            {
                Log(Debug::Warning) << "[MP] BaseInfo received with guid=0 for player '"
                                    << tmp.name << "' Ã¢â‚¬â€ ignoring (stale pre-handshake packet)";
                return;
            }

            // If this is our own guid, update localPlayer name so outgoing
            // chat and base-info broadcasts use the current nickname.
            if (tmp.guid == mPlayerSync->localPlayer().guid)
            {
                mPlayerSync->localPlayer().name = tmp.name;
                return;
            }

            RemotePlayer* rp = mPlayerList->getPlayer(tmp.guid);
            if (!rp)
            {
                mPlayerList->addPlayer(tmp.guid, tmp.name);
                Log(Debug::Info) << "[MP] Player joined: " << tmp.name
                                 << " (guid=" << tmp.guid << ")";
                // Populate appearance on the new RemotePlayer immediately -
                // addPlayer() only sets the name; race/head/hair live in onBaseInfoUpdate.
                rp = mPlayerList->getPlayer(tmp.guid);
            }
            // Always apply appearance (covers both new join and live updates)
            if (rp) rp->onBaseInfoUpdate(tmp);
        });

    // --- Remote player equipment ---
    proto.registerHandler(PacketType::PlayerEquipment,
        [this](const uint8_t* data, size_t size)
        {
            BasePlayer tmp;
            PacketPlayerEquipment pkt;
            pkt.setPlayer(&tmp);
            if (!pkt.decode(data, size)) return;
            if (tmp.guid == mPlayerSync->localPlayer().guid)
            {
                mPlayerSync->queueAuthoritativeEquipment(tmp);
                return;
            }

            auto* rp = mPlayerList->getPlayer(tmp.guid);
            if (rp) rp->onEquipmentUpdate(tmp);
        });

    // --- Remote player dynamic stats ---
    proto.registerHandler(PacketType::PlayerStatsDynamic,
        [this](const uint8_t* data, size_t size)
        {
            BasePlayer tmp;
            PacketPlayerStatsDynamic pkt;
            pkt.setPlayer(&tmp);
            if (!pkt.decode(data, size)) return;
            if (tmp.guid == mPlayerSync->localPlayer().guid)
            {
                // The server pushed authoritative statistics (for example the
                // alchemy skill progression awarded by server-authoritative
                // crafting). Apply them to the local player.
                mPlayerSync->queueAuthoritativeStats(tmp);
                return;
            }

            auto* rp = mPlayerList->getPlayer(tmp.guid);
            if (rp) rp->onStatsDynamicUpdate(tmp);
        });

    // --- Disconnect (player left) ---
    proto.registerHandler(PacketType::Disconnect,
        [this](const uint8_t* data, size_t size)
        {
            PacketDisconnect pkt;
            if (!pkt.decode(data, size)) return;
            if (mPlayerList->getPlayer(pkt.guid))
            {
                Log(Debug::Info) << "[MP] Player disconnected guid=" << pkt.guid;
                mPlayerList->removePlayer(pkt.guid);
            }
        });

    // --- Chat message ---
    proto.registerHandler(PacketType::ChatMessage,
        [this](const uint8_t* data, size_t size)
        {
            BasePlayer tmp;
            PacketChatMessage pkt;
            pkt.setPlayer(&tmp);
            if (!pkt.decode(data, size)) return;
            // Show message from any player including own echo from server
            mChatWindow->addMessage(tmp.name, pkt.message, pkt.channel);
        });

        // --- World time ---
    proto.registerHandler(PacketType::WorldTime,
        [this](const uint8_t* data, size_t size)
        {
            PacketWorldTime pkt;
            if (!pkt.decode(data, size)) return;
            mWorldStateSync->onServerTimeUpdate(pkt.time, pkt.timeScale);
        });

    proto.registerHandler(PacketType::GameSettings,
        [this](const uint8_t* data, size_t size)
        {
            PacketGameSettings pkt;
            if (!pkt.decode(data, size)) return;

            MWPhysics::setSurfPhysicsSettings(pkt.settings);
            mGuardArrestMode = pkt.guardArrestMode;
            Log(Debug::Info) << "[MP] Applied surf settings for cell=" << pkt.settings.cellId
                             << " enabled=" << (pkt.settings.enabled ? "true" : "false")
                             << " guardArrestMode="
                             << (mGuardArrestMode == GuardArrestMode::Dialogue ? "dialogue" : "combat");
        });

    // --- World weather ---
    proto.registerHandler(PacketType::WorldWeather,
        [this](const uint8_t* data, size_t size)
        {
            PacketWorldWeather pkt;
            if (!pkt.decode(data, size)) return;
            mWorldStateSync->onServerWeatherUpdate(
                pkt.currentWeather, pkt.nextWeather,
                pkt.transitionFactor, pkt.regionName);
        });

    // --- Door state ---
    proto.registerHandler(PacketType::DoorState,
        [this](const uint8_t* data, size_t size)
        {
            PacketDoorState pkt;
            if (!pkt.decode(data, size)) return;
            for (const auto& d : pkt.doors)
                mObjectSync->onServerDoorState(
                    d.cellId, d.refId, d.refNum, d.isOpen, d.isLocked, d.lockLevel, d.revision);
        });

    proto.registerHandler(PacketType::RecordDynamic,
        [this](const uint8_t* data, size_t size)
        {
            PacketRecordDynamic pkt;
            if (!pkt.decode(data, size))
            {
                mWorldStateSync->noteRuntimeContentBootstrapError("malformed RecordDynamic bootstrap packet");
                return;
            }
            mWorldStateSync->onServerRecordDynamic(pkt.action, pkt.recordType, std::move(pkt.entries));
        });

    proto.registerHandler(PacketType::RuntimeContentBootstrapComplete,
        [this](const uint8_t* data, size_t size)
        {
            PacketRuntimeContentBootstrapComplete packet;
            if (!packet.decode(data, size))
            {
                failRuntimeContentBootstrap("malformed RuntimeContentBootstrapComplete packet");
                return;
            }

            const WorldStateSync::RuntimeContentBootstrapResult result
                = mWorldStateSync->finishRuntimeContentBootstrap();
            if (!result.complete)
            {
                failRuntimeContentBootstrap(result.error);
                return;
            }

            mRuntimeContentBootstrapGate.finishRuntimeContent(true);
            Log(Debug::Info) << "[MP] Runtime content bootstrap complete";
            tryActivateServerLuaPackages();
            tryFinalizePendingCharacterData();
        });

    proto.registerHandler(PacketType::PlayerBounty,
        [this](const uint8_t* data, size_t size)
        {
            BasePlayer authoritative;
            PacketPlayerBounty packet;
            packet.setPlayer(&authoritative);
            if (!packet.decode(data, size) || packet.mode != PacketPlayerBounty::Mode::Result)
            {
                disconnect("Malformed authoritative player crime state");
                return;
            }
            if (authoritative.guid != mPlayerSync->localPlayer().guid)
            {
                disconnect("Authoritative player crime state identity mismatch");
                return;
            }

            const RevisionDecision decision
                = mPlayerSync->receiveAuthoritativeCrimeState(authoritative.crimeState);
            if (decision == RevisionDecision::Conflict)
            {
                disconnect("Conflicting authoritative player crime revision");
                return;
            }
            finishCrimeMutation(packet.resultRequestId, packet.accepted, packet.error);
            tryFinalizePendingCharacterData();
        });

    proto.registerHandler(PacketType::GuardArrest,
        [this](const uint8_t* data, size_t size)
        {
            PacketGuardArrest packet;
            if (!packet.decode(data, size))
            {
                disconnect("Malformed authoritative guard arrest packet");
                return;
            }
            if (packet.mode == PacketGuardArrest::Mode::Result)
            {
                finishGuardArrest(packet.result);
                return;
            }
            if (packet.mode == PacketGuardArrest::Mode::Prompt)
            {
                receiveGuardArrestPrompt(packet.reach);
                return;
            }
            disconnect("Unexpected authoritative guard arrest packet mode");
        });

    proto.registerHandler(PacketType::PlayerTopic,
        [this](const uint8_t* data, size_t size)
        {
            BasePlayer authoritative;
            PacketPlayerTopic packet;
            packet.setPlayer(&authoritative);
            if (!packet.decode(data, size) || packet.action != PacketPlayerTopic::Action::Set)
            {
                disconnect("Malformed authoritative player topic state");
                return;
            }
            if (authoritative.guid != mPlayerSync->localPlayer().guid)
            {
                disconnect("Authoritative player topic state identity mismatch");
                return;
            }

            const RevisionDecision decision
                = mPlayerSync->receiveAuthoritativeTopicState(std::move(authoritative.topicState));
            if (decision == RevisionDecision::Conflict)
            {
                disconnect("Conflicting authoritative player topic revision");
                return;
            }
            tryFinalizePendingCharacterData();
        });

    proto.registerHandler(PacketType::PlayerFaction,
        [this](const uint8_t* data, size_t size)
        {
            BasePlayer authoritative;
            PacketPlayerFaction packet;
            packet.setPlayer(&authoritative);
            if (!packet.decode(data, size) || packet.mode != PacketPlayerFaction::Mode::Result)
            {
                disconnect("Malformed authoritative player faction result");
                return;
            }
            if (authoritative.guid != mPlayerSync->localPlayer().guid)
            {
                disconnect("Authoritative player faction state identity mismatch");
                return;
            }
            const RevisionDecision decision = mPlayerSync->receiveAuthoritativeFactionResult(
                std::move(authoritative.factionState), packet.resultRequestId, packet.accepted, packet.error);
            if (decision == RevisionDecision::Conflict)
            {
                disconnect("Conflicting authoritative player faction revision");
                return;
            }
            tryFinalizePendingCharacterData();
        });

    proto.registerHandler(PacketType::RecordCreateResult,
        [this](const uint8_t* data, size_t size)
        {
            PacketRecordCreateResult packet;
            if (!packet.decode(data, size))
                return;
            mRecordCreationManager->onResult(std::move(packet.result));
        });

    proto.registerHandler(PacketType::AlchemyResult,
        [this](const uint8_t* data, size_t size)
        {
            PacketAlchemyResult packet;
            if (!packet.decode(data, size))
                return;
            mAlchemyCreationManager->onResult(std::move(packet.result));
        });

    proto.registerHandler(PacketType::EnchantingResult,
        [this](const uint8_t* data, size_t size)
        {
            PacketEnchantingResult packet;
            if (!packet.decode(data, size))
                return;
            mEnchantingCreationManager->onResult(std::move(packet.result));
        });

    // --- Persisted / relayed world objects ---
    proto.registerHandler(PacketType::ObjectPlace,
        [this](const uint8_t* data, size_t size)
        {
            PacketObjectPlace pkt;
            if (!pkt.decode(data, size)) return;
            mWorldObjectSync->onServerObjectPlace(
                pkt.object.mpNum, pkt.object.refId, pkt.object.count,
                pkt.object.position, pkt.object.cellId, pkt.authorityGeneration);
        });

    proto.registerHandler(PacketType::ObjectDelete,
        [this](const uint8_t* data, size_t size)
        {
            PacketObjectDelete pkt;
            if (!pkt.decode(data, size)) return;
            PlacedObjectIdentity identity;
            identity.cellId = pkt.cellId;
            identity.refId = pkt.refId;
            identity.refIndex = pkt.refNum;
            identity.refContentFile = pkt.refContentFile;
            identity.mpNum = pkt.mpNum;
            identity.kind = pkt.mpNum != 0
                ? PlacedObjectKind::ServerPlaced : PlacedObjectKind::ContentReference;
            mWorldObjectSync->onServerObjectDelete(identity);
        });

    proto.registerHandler(PacketType::ObjectCount,
        [this](const uint8_t* data, size_t size)
        {
            PacketObjectCount packet;
            if (!packet.decode(data, size))
                return;
            mWorldObjectSync->onServerObjectCount(packet.object, packet.count);
        });

    proto.registerHandler(PacketType::WorldItemTakeResult,
        [this](const uint8_t* data, size_t size)
        {
            PacketWorldItemTakeResult packet;
            if (!packet.decode(data, size))
                return;
            mWorldObjectSync->onServerWorldItemTakeResult(packet.result);
        });

    proto.registerHandler(PacketType::InventoryTakeResult,
        [this](const uint8_t* data, size_t size)
        {
            PacketInventoryTakeResult packet;
            if (!packet.decode(data, size))
            {
                disconnect("Malformed authoritative inventory take result");
                return;
            }
            mWorldObjectSync->onServerInventoryTakeResult(packet.result);
        });

    proto.registerHandler(PacketType::InventoryTakeBatchResult,
        [this](const uint8_t* data, size_t size)
        {
            PacketInventoryTakeBatchResult packet;
            if (!packet.decode(data, size))
            {
                disconnect("Malformed authoritative inventory take batch result");
                return;
            }
            mWorldObjectSync->onServerInventoryTakeBatchResult(packet.result);
        });

    proto.registerHandler(PacketType::InventoryPutResult,
        [this](const uint8_t* data, size_t size)
        {
            PacketInventoryPutResult packet;
            if (!packet.decode(data, size))
            {
                disconnect("Malformed authoritative inventory put result");
                return;
            }
            mWorldObjectSync->onServerInventoryPutResult(packet.result);
        });

    proto.registerHandler(PacketType::InventoryTransferSound,
        [this](const uint8_t* data, size_t size)
        {
            PacketPlayerInventoryTransferSound packet;
            if (!packet.decode(data, size))
            {
                disconnect("Malformed authoritative inventory transfer sound");
                return;
            }
            if (packet.event.actorGuid == mPlayerSync->localPlayer().guid)
                return;
            if (auto* remote = mPlayerList->getPlayer(packet.event.actorGuid))
                remote->onInventoryTransferSound(packet.event);
        });

    proto.registerHandler(PacketType::BarterResult,
        [this](const uint8_t* data, size_t size)
        {
            PacketBarterResult packet;
            if (!packet.decode(data, size))
            {
                disconnect("Malformed authoritative barter result");
                return;
            }
            mWorldObjectSync->onServerBarterResult(packet.result);
        });

    proto.registerHandler(PacketType::ObjectMove,
        [this](const uint8_t* data, size_t size)
        {
            PacketObjectMove pkt;
            if (!pkt.decode(data, size)) return;
            mWorldObjectSync->onServerObjectMove(pkt.mpNum, pkt.cellId, pkt.position);
        });

    proto.registerHandler(PacketType::Container,
        [this](const uint8_t* data, size_t size)
        {
            PacketContainer pkt;
            if (!pkt.decode(data, size)) return;
            mWorldObjectSync->onServerContainer(
                pkt.container,
                static_cast<ContainerAction>(pkt.mAction), pkt.authorityGeneration, pkt.bootstrapSequence);
        });

    // --- Remote player animation flags ---
    proto.registerHandler(PacketType::PlayerAnimFlags,
        [this](const uint8_t* data, size_t size)
        {
            BasePlayer tmp;
            PacketPlayerAnimFlags pkt;
            pkt.setPlayer(&tmp);
            if (!pkt.decode(data, size)) return;
            if (tmp.guid == mPlayerSync->localPlayer().guid) return;

            auto* rp = mPlayerList->getPlayer(tmp.guid);
            if (rp) rp->onAnimFlagsUpdate(tmp, pkt.getSequence());
        });

    // --- Remote player one-shot animation ---
    proto.registerHandler(PacketType::PlayerAnimPlay,
        [this](const uint8_t* data, size_t size)
        {
            BasePlayer tmp;
            PacketPlayerAnimPlay pkt;
            pkt.setPlayer(&tmp);
            if (!pkt.decode(data, size)) return;
            if (tmp.guid == mPlayerSync->localPlayer().guid) return;

            auto* rp = mPlayerList->getPlayer(tmp.guid);
            if (rp) rp->onAnimPlay(tmp);
        });

    // --- Remote player attack event ---
    proto.registerHandler(PacketType::PlayerAttack,
        [this](const uint8_t* data, size_t size)
        {
            BasePlayer tmp;
            PacketPlayerAttack pkt;
            pkt.setPlayer(&tmp);
            if (!pkt.decode(data, size)) return;
            if (tmp.guid == mPlayerSync->localPlayer().guid) return;

            auto* rp = mPlayerList->getPlayer(tmp.guid);
            if (rp) rp->onAttack(tmp);
        });

    // --- Server-authored player vehicle mode/profile state ---
    proto.registerHandler(PacketType::PlayerVehicleState,
        [this](const uint8_t* data, size_t size)
        {
            BasePlayer tmp;
            PacketPlayerVehicleState packet;
            packet.setPlayer(&tmp);
            if (!packet.decode(data, size))
                return;

            if (tmp.guid == mPlayerSync->localPlayer().guid)
            {
                mPlayerSync->applyServerVehicleState(tmp);
                return;
            }

            if (auto* remote = mPlayerList->getPlayer(tmp.guid))
                remote->onVehicleState(tmp);
        });

    // --- Player speech event ---
    proto.registerHandler(PacketType::PlayerSpeech,
        [this](const uint8_t* data, size_t size)
        {
            BasePlayer tmp;
            PacketPlayerSpeech pkt;
            pkt.setPlayer(&tmp);
            if (!pkt.decode(data, size) || tmp.speechSound.empty()) return;
            tmp.speechSound = normalizeSpeechSoundPath(tmp.speechSound);

            if (tmp.guid == mPlayerSync->localPlayer().guid)
            {
                MWBase::World* world = MWBase::Environment::get().getWorld();
                MWBase::SoundManager* sound = MWBase::Environment::get().getSoundManager();
                if (world && sound)
                    sound->say(world->getPlayerPtr(), VFS::Path::Normalized(tmp.speechSound));
                return;
            }

            auto* rp = mPlayerList->getPlayer(tmp.guid);
            if (rp) rp->onSpeech(tmp);
        });

    // --- Remote player cast event ---
    proto.registerHandler(PacketType::PlayerCast,
        [this](const uint8_t* data, size_t size)
        {
            BasePlayer tmp;
            PacketPlayerCast pkt;
            pkt.setPlayer(&tmp);
            if (!pkt.decode(data, size)) return;
            if (tmp.guid == mPlayerSync->localPlayer().guid) return;

            auto* rp = mPlayerList->getPlayer(tmp.guid);
            if (rp) rp->onCast(tmp);
        });

    // --- Remote player cosmetic inventory delta ---
    proto.registerHandler(PacketType::PlayerInventory,
        [this](const uint8_t* data, size_t size)
        {
            using Clock = std::chrono::steady_clock;
            const auto started = Clock::now();

            BasePlayer tmp;
            PacketPlayerInventory pkt;
            pkt.setPlayer(&tmp);
            if (!pkt.decode(data, size)) return;
            const auto decoded = Clock::now();

            const bool localInventory = tmp.guid == mPlayerSync->localPlayer().guid;
            if (localInventory)
            {
                mRecordCreationManager->setInventoryRevision(tmp.inventoryChanges.revision);
                mPlayerSync->queueAuthoritativeInventory(tmp);
            }
            else
            {
                auto* rp = mPlayerList->getPlayer(tmp.guid);
                if (rp) rp->onInventoryUpdate(tmp);
            }

            const auto finished = Clock::now();
            const double totalMs = std::chrono::duration<double, std::milli>(finished - started).count();
            if (totalMs >= 8.0)
            {
                Log(totalMs >= 50.0 ? Debug::Warning : Debug::Info)
                    << "[MPDIAG] PlayerInventory handler breakdown"
                    << " guid=" << tmp.guid
                    << " local=" << localInventory
                    << " action=" << static_cast<int>(tmp.inventoryChanges.action)
                    << " items=" << tmp.inventoryChanges.items.size()
                    << " bytes=" << size
                    << " decodeMs=" << std::chrono::duration<double, std::milli>(decoded - started).count()
                    << " applyMs=" << std::chrono::duration<double, std::milli>(finished - decoded).count()
                    << " totalMs=" << totalMs;
            }
        });

    proto.registerHandler(PacketType::PlayerJournal,
        [this](const uint8_t* data, size_t size)
        {
            BasePlayer tmp;
            PacketPlayerJournal pkt;
            pkt.setPlayer(&tmp);
            if (!pkt.decode(data, size))
                return;
            if (tmp.guid != mPlayerSync->localPlayer().guid)
                return;
            mPlayerSync->queueAuthoritativeJournal(tmp);
        });

    // --- Server-authoritative learned spellbook ---
    proto.registerHandler(PacketType::PlayerSpellbook,
        [this](const uint8_t* data, size_t size)
        {
            BasePlayer tmp;
            PacketPlayerSpellbook pkt;
            pkt.setPlayer(&tmp);
            if (!pkt.decode(data, size))
            {
                Log(Debug::Warning) << "[MP] Rejected malformed PlayerSpellbook packet";
                return;
            }
            const uint64_t localGuid = mPlayerSync->localPlayer().guid;
            if (tmp.guid != localGuid)
            {
                Log(Debug::Warning) << "[MP] Rejected PlayerSpellbook for non-local guid"
                                    << " packetGuid=" << tmp.guid
                                    << " localGuid=" << localGuid
                                    << " revision=" << tmp.spellbookChanges.revision
                                    << " spells=" << tmp.spellbookChanges.spellIds.size();
                return;
            }
            mPlayerSync->queueAuthoritativeSpellbook(tmp);
        });

    proto.registerHandler(PacketType::PlayerDeath,
        [this](const uint8_t* data, size_t size)
        {
            BasePlayer tmp;
            PacketPlayerDeath pkt;
            pkt.setPlayer(&tmp);
            if (!pkt.decode(data, size)) return;
            if (tmp.guid == mPlayerSync->localPlayer().guid)
            {
                mPlayerSync->applyServerDeath(tmp);
                return;
            }

            auto* rp = mPlayerList->getPlayer(tmp.guid);
            if (rp) rp->onDeath(tmp);
        });

    proto.registerHandler(PacketType::PlayerResurrect,
        [this](const uint8_t* data, size_t size)
        {
            BasePlayer tmp;
            PacketPlayerResurrect pkt;
            pkt.setPlayer(&tmp);
            if (!pkt.decode(data, size)) return;
            if (tmp.guid == mPlayerSync->localPlayer().guid) return;

            auto* rp = mPlayerList->getPlayer(tmp.guid);
            if (rp) rp->onResurrect(tmp);
        });

    proto.registerHandler(PacketType::ActorAuthority,
        [this](const uint8_t* data, size_t size)
        {
            ActorList tmp;
            PacketActorAuthority pkt;
            pkt.setActorList(&tmp);
            if (!pkt.decode(data, size)) return;
            mActorSync->onAuthorityUpdate(tmp);
        });

    proto.registerHandler(PacketType::ActorList,
        [this](const uint8_t* data, size_t size)
        {
            ActorList tmp;
            PacketActorList pkt;
            pkt.setActorList(&tmp);
            if (!pkt.decode(data, size)) return;
            mActorSync->onActorListUpdate(tmp);
        });

    proto.registerHandler(PacketType::ActorIdentity,
        [this](const uint8_t* data, size_t size)
        {
            ActorIdentityList tmp;
            PacketActorIdentity pkt;
            pkt.setIdentityList(&tmp);
            if (!pkt.decode(data, size)) return;
            mActorSync->onActorIdentityUpdate(tmp);
        });

    proto.registerHandler(PacketType::ActorPosition,
        [this](const uint8_t* data, size_t size)
        {
            ActorList tmp;
            PacketActorPosition pkt;
            pkt.setActorList(&tmp);
            if (!pkt.decode(data, size)) return;
            mActorSync->onActorPositionUpdate(tmp);
        });

    proto.registerHandler(PacketType::ActorPositionV2,
        [this](const uint8_t* data, size_t size)
        {
            ActorPositionV2List tmp;
            PacketActorPositionV2 pkt;
            pkt.setPositionList(&tmp);
            if (!pkt.decode(data, size)) return;
            mActorSync->onActorPositionV2Update(tmp);
        });

    proto.registerHandler(PacketType::ActorPresentationV2,
        [this](const uint8_t* data, size_t size)
        {
            ActorPresentationV2List tmp;
            PacketActorPresentationV2 pkt;
            pkt.setPresentationList(&tmp);
            if (!pkt.decode(data, size)) return;
            mActorSync->onActorPresentationV2Update(tmp);
        });

    proto.registerHandler(PacketType::ActorAnimFlags,
        [this](const uint8_t* data, size_t size)
        {
            ActorList tmp;
            PacketActorAnimFlags pkt;
            pkt.setActorList(&tmp);
            if (!pkt.decode(data, size)) return;
            mActorSync->onActorAnimFlagsUpdate(tmp);
        });

    proto.registerHandler(PacketType::ActorAnimPlay,
        [this](const uint8_t* data, size_t size)
        {
            ActorList tmp;
            PacketActorAnimPlay pkt;
            pkt.setActorList(&tmp);
            if (!pkt.decode(data, size)) return;
            mActorSync->onActorAnimPlay(tmp);
        });

    proto.registerHandler(PacketType::ActorAttack,
        [this](const uint8_t* data, size_t size)
        {
            ActorList tmp;
            PacketActorAttack pkt;
            pkt.setActorList(&tmp);
            if (!pkt.decode(data, size)) return;
            mActorSync->onActorAttack(tmp);
        });

    proto.registerHandler(PacketType::ActorAttackV2,
        [this](const uint8_t* data, size_t size)
        {
            ActorAttackV2List tmp;
            PacketActorAttackV2 pkt;
            pkt.setAttackList(&tmp);
            if (!pkt.decode(data, size)) return;
            mActorSync->onActorAttackV2(tmp);
        });

    proto.registerHandler(PacketType::ActorSpeech,
        [this](const uint8_t* data, size_t size)
        {
            ActorSpeechList tmp;
            PacketActorSpeech pkt;
            pkt.setSpeechList(&tmp);
            if (!pkt.decode(data, size)) return;
            mActorSync->onActorSpeech(tmp);
        });

    proto.registerHandler(PacketType::CrimeReaction,
        [this](const uint8_t* data, size_t size)
        {
            PacketCrimeReaction pkt;
            if (!pkt.decode(data, size)) return;
            mActorSync->onCrimeReaction(pkt.directive);
        });

    proto.registerHandler(PacketType::ActorCast,
        [this](const uint8_t* data, size_t size)
        {
            ActorList tmp;
            PacketActorCast pkt;
            pkt.setActorList(&tmp);
            if (!pkt.decode(data, size)) return;
            mActorSync->onActorCast(tmp);
        });

    proto.registerHandler(PacketType::ActorCellChange,
        [this](const uint8_t* data, size_t size)
        {
            ActorList tmp;
            PacketActorCellChange pkt;
            pkt.setActorList(&tmp);
            if (!pkt.decode(data, size)) return;
            mActorSync->onActorCellChange(tmp);
        });

    proto.registerHandler(PacketType::ActorDeath,
        [this](const uint8_t* data, size_t size)
        {
            ActorList tmp;
            PacketActorDeath pkt;
            pkt.setActorList(&tmp);
            if (!pkt.decode(data, size)) return;
            mActorSync->onActorDeath(tmp);
        });

    proto.registerHandler(PacketType::ActorEquipment,
        [this](const uint8_t* data, size_t size)
        {
            ActorList tmp;
            PacketActorEquipment pkt;
            pkt.setActorList(&tmp);
            if (!pkt.decode(data, size)) return;
            mActorSync->onActorEquipment(tmp);
        });

    proto.registerHandler(PacketType::ActorStatsDynamic,
        [this](const uint8_t* data, size_t size)
        {
            ActorList tmp;
            PacketActorStatsDynamic pkt;
            pkt.setActorList(&tmp);
            if (!pkt.decode(data, size)) return;
            mActorSync->onActorStatsDynamic(tmp);
        });

    proto.registerHandler(PacketType::ActorAI,
        [this](const uint8_t* data, size_t size)
        {
            ActorList tmp;
            PacketActorAI pkt;
            pkt.setActorList(&tmp);
            if (!pkt.decode(data, size)) return;
            mActorSync->onActorAI(tmp);
        });

    proto.registerHandler(PacketType::ActorCombatRequest,
        [this](const uint8_t* data, size_t size)
        {
            ActorList tmp;
            PacketActorCombatRequest pkt;
            pkt.setActorList(&tmp);
            if (!pkt.decode(data, size)) return;
            mActorSync->onActorCombatRequest(tmp);
        });

    proto.registerHandler(PacketType::ActorCombatResult,
        [this](const uint8_t* data, size_t size)
        {
            ActorList tmp;
            PacketActorCombatResult pkt;
            pkt.setActorList(&tmp);
            if (!pkt.decode(data, size)) return;
            mActorSync->onActorCombatResult(tmp);
        });

    proto.registerHandler(PacketType::PacketLuaEvent,
        [this](const uint8_t* data, size_t size)
        {
            PacketLuaEvent pkt;
            if (!pkt.decode(data, size)) return;

            if (mNetworkBridge)
                mNetworkBridge->queueInbound({ pkt.pid, std::move(pkt.eventName), std::move(pkt.eventData) });
        });

    proto.registerHandler(PacketType::PacketLuaStorage,
        [this](const uint8_t* data, size_t size)
        {
            PacketLuaStorage pkt;
            if (!pkt.decode(data, size)) return;

            if (mNetworkBridge)
                mNetworkBridge->queueStorage(pkt.action, std::move(pkt.section), std::move(pkt.entries));
        });

    Log(Debug::Info) << "[MP] Protocol handlers registered";
}

} // namespace mwmp
