#ifndef OPENMW_MP_ACTORRELIABLEEVENT_HPP
#define OPENMW_MP_ACTORRELIABLEEVENT_HPP

namespace mwmp
{
    inline bool canRelayReliableActorEvent(bool identitySent, bool /*identityAcked*/)
    {
        // Reliable ActorIdentity and reliable actor events share ordered delivery.
        // The identity only needs to have been sent, not round-trip acknowledged.
        return identitySent;
    }
}

#endif
