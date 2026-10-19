#ifndef OPENMW_MWSOUND_PROCEDURALDSP_H
#define OPENMW_MWSOUND_PROCEDURALDSP_H

#include <cmath>

namespace MWSound::ProceduralDsp
{
    // Far below the audible range, but above both float and double subnormals.
    // Recursive smoothers otherwise approach zero forever and can become much
    // more expensive after changing patches on CPUs with gradual underflow.
    template <class T>
    T silence(T value)
    {
        return std::abs(value) < T(1e-20) ? T(0) : value;
    }

    template <class T>
    T smooth(T current, T target, T coefficient)
    {
        return silence(current + (target - current) * coefficient);
    }
}

#endif
