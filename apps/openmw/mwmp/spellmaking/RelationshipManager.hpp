#pragma once
#include <chrono>
#include <deque>
#include <functional>
#include <optional>
#include <unordered_map>
#include <components/openmw-mp/PersuasionProtocol.hpp>
#include <components/enchanting/EnchantingMechanics.hpp>
#include "../../mwworld/ptr.hpp"

namespace mwmp
{
    class RelationshipManager
    {
    public:
        using Completion=std::function<void(const PersuasionResult&)>;
        void begin(const MWWorld::Ptr&);
        void close();
        bool persuade(const MWWorld::Ptr&,int type,Completion);
        void onResult(const PersuasionResult&);
        void update();
        void clear();
        int baseDisposition(const MWWorld::Ptr&,bool temporary=true) const;
        Crafting::EnchantingBarterInput pricing(const MWWorld::Ptr&) const;
    private:
        struct Pending { PersuasionRequest request; Completion completion; std::optional<PersuasionResult> result;
            std::chrono::steady_clock::time_point lastSend; bool sent=false; };
        bool enqueue(const MWWorld::Ptr&,PersuasionAction,Completion={});
        std::uint64_t actorId(const MWWorld::Ptr&) const;
        std::unordered_map<std::uint64_t,PersuasionResult> mStates;
        std::deque<Pending> mPending;
        MWWorld::Ptr mActor;
    };
}
