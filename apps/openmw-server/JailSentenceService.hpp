#ifndef OPENMW_APPS_OPENMW_SERVER_JAILSENTENCESERVICE_H
#define OPENMW_APPS_OPENMW_SERVER_JAILSENTENCESERVICE_H

#include <cstdint>
#include <functional>
#include <vector>

#include <components/openmw-mp/Base/BaseObject.hpp>
#include <components/openmw-mp/Base/BaseStructs.hpp>
#include <components/openmw-mp/StolenItems.hpp>

namespace mwmp
{
    enum class JailSentencePlanError
    {
        None,
        InvalidInput,
        InstanceIdUnavailable,
        EvidenceUnavailable,
    };

    struct JailSentencePlan
    {
        JailSentencePlanError error = JailSentencePlanError::None;
        std::vector<Item> inventory;
        std::vector<EquipmentItem> equipment;
        ContainerRecord evidence;
        std::vector<StolenItemMutation> stolenItemMutations;
        std::vector<Item> confiscatedItems;
        bool equipmentChanged = false;

        bool inventoryChanged() const { return !confiscatedItems.empty(); }
    };

    class JailSentenceService
    {
    public:
        using AllocateInstanceId = std::function<std::uint32_t()>;

        // Consumes vanilla aggregate stolen counts across inventory order. A
        // partial stack keeps its original player instance id; the evidence
        // fragment receives a fresh id so identity remains unique.
        static JailSentencePlan planConfiscation(const std::vector<Item>& inventory,
            const std::vector<EquipmentItem>& equipment, const std::vector<StolenItemRecord>& stolenItems,
            const ContainerRecord& evidence, const AllocateInstanceId& allocateInstanceId);
    };
}

#endif
