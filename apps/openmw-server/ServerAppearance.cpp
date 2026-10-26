#include "Server.hpp"
#include "ServerContentRegistry.hpp"

#include <apps/openmw/mwworld/esmstore.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerBaseInfo.hpp>

namespace mwmp
{
    bool MPServer::setPlayerAppearance(
        uint32_t guid, int64_t characterId, const PlayerAppearance& value, std::string& error)
    {
        auto reject = [&](const char* message) {
            error = message;
            return false;
        };
        auto* client = findClientByGuid(guid);
        if (!client || client->dbCharacterId != characterId || characterId <= 0 || !client->charSelectComplete
            || !client->player.charGenComplete || client->dbChargenCompletePending || !mPlayerDb || !mContentRegistry)
            return reject("Character is not ready for an appearance edit.");
        if (client->player.dynamicStats.health.current <= 0)
            return reject("You cannot edit appearance while dead.");
        if (!validAppearanceId(value.race) || !validAppearanceId(value.head) || !validAppearanceId(value.hair, true))
            return reject("Invalid appearance record ID.");

        try
        {
            const auto& store = mContentRegistry->store();
            const auto* race = store.get<ESM::Race>().search(ESM::RefId::deserializeText(value.race));
            if (!race
                || (!(race->mData.mFlags & ESM::Race::Playable)
                    && race->mId != ESM::RefId::deserializeText(client->player.race)))
                return reject("This race is not playable on this server.");
            const auto* head = store.get<ESM::BodyPart>().search(ESM::RefId::deserializeText(value.head));
            if (!head || !validAppearancePart(*head, ESM::BodyPart::MP_Head)
                || !mContentRegistry->hasModel(head->mModel.getOriginal()))
                return reject("Select a playable head available on this server.");
            const ESM::BodyPart* hair = nullptr;
            if (!value.hair.empty())
            {
                hair = store.get<ESM::BodyPart>().search(ESM::RefId::deserializeText(value.hair));
                if (!hair || !validAppearancePart(*hair, ESM::BodyPart::MP_Hair)
                    || !mContentRegistry->hasModel(hair->mModel.getOriginal()))
                    return reject("Select playable hair available on this server.");
            }
            ESM::NpcAppearance body = value.body;
            if (body.animationRig > 2 || !body.parts[0].empty() || !body.parts[1].empty())
                return reject("Invalid body appearance.");
            if (!body.bodyRace.empty())
            {
                const auto* donor = store.get<ESM::Race>().search(body.bodyRace);
                if (!donor || !(donor->mData.mFlags & ESM::Race::Playable))
                    return reject("Select a playable body race.");
                body.bodyRace = donor->mId;
            }
            for (size_t i = 2; i < body.parts.size(); ++i)
            {
                if (body.parts[i].empty())
                    continue;
                const auto* part = store.get<ESM::BodyPart>().search(body.parts[i]);
                if (!part || !validAppearancePart(*part, static_cast<ESM::BodyPart::MeshPart>(i))
                    || ESM::isFirstPersonBodyPart(*part) || !mContentRegistry->hasModel(part->mModel.getOriginal()))
                    return reject("Select a playable skin part of the correct body slot.");
                body.parts[i] = part->mId;
            }
            const std::string raceId = race->mId.serializeText();
            const std::string headId = head->mId.serializeText();
            const std::string hairId = hair ? hair->mId.serializeText() : std::string();
            // One UPDATE saves identity and presentation. Never call chargen or
            // rebuild the character's stats, learned spells, class or birthsign.
            if (!mPlayerDb->saveCharacterAppearance(characterId, raceId, headId, hairId, value.isMale, body))
                return reject("Character appearance could not be saved.");
            client->player.race = raceId;
            client->player.headMesh = headId;
            client->player.hairMesh = hairId;
            client->player.isMale = value.isMale;
            client->player.appearance = body;
            PacketPlayerBaseInfo packet;
            packet.setPlayer(&client->player);
            broadcastToAll(packet.encode());
            error.clear();
            return true;
        }
        catch (const std::exception&)
        {
            return reject("The server could not validate or save this appearance.");
        }
    }
}
