#ifndef OPENMW_MP_PLAYERAPPEARANCE_HPP
#define OPENMW_MP_PLAYERAPPEARANCE_HPP

#include <components/esm3/loadbody.hpp>
#include <string>

namespace mwmp
{
    struct PlayerAppearance
    {
        std::string race;
        std::string head;
        std::string hair;
        bool isMale = true;
    };

    inline bool validAppearancePart(const ESM::BodyPart& part, ESM::BodyPart::MeshPart kind)
    {
        // Deliberately allow mixing race/sex metadata. The chosen records must
        // still be real, playable, non-vampire skin parts of the correct slot.
        return part.mData.mPart == kind && part.mData.mType == ESM::BodyPart::MT_Skin
            && !(part.mData.mFlags & ESM::BodyPart::BPF_NotPlayable) && !part.mData.mVampire && !part.mModel.empty();
    }

    inline bool validAppearanceId(const std::string& id, bool optional = false)
    {
        if (id.empty())
            return optional;
        if (id.size() > 256)
            return false;
        for (unsigned char c : id)
            if (c < 32 || c == 127)
                return false;
        return true;
    }
}
#endif
