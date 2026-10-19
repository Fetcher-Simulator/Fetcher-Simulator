#ifndef OPENMW_MWSOUND_PROCEDURALSYNTH_EFFECTS_H
#define OPENMW_MWSOUND_PROCEDURALSYNTH_EFFECTS_H

#include <algorithm>
#include <array>
#include <cmath>
#include "proceduraldsp.hpp"

namespace MWSound
{
    // A bounded, allocation-free insert, independent of Lua, OpenAL and scene nodes.
    // One instance belongs to one instrument, after its voices have been summed.
    class ProceduralSynthEffects
    {
    public:
        struct Settings
        {
            int mode = 0, cabinet = 0;
            double drive = 0, tone = 12000, mix = 1, output = 1;
            double bass = 0, mid = 0, treble = 0;
        };
        ProceduralSynthEffects()
        {
            // 4x interpolation/decimation, 65-tap Blackman-windowed sinc. The
            // interpolation is polyphase; decimation computes only kept samples.
            // Each filter delays 8 base-rate samples. Parallel dry is delayed 16.
            double sum = 0;
            for (std::size_t i = 0; i < mFir.size(); ++i)
            {
                const double n = static_cast<double>(i) - 32;
                const double sinc = n == 0 ? 0.225 : std::sin(Pi * 0.225 * n) / (Pi * n);
                mFir[i] = sinc * (0.42 - 0.5 * std::cos(2 * Pi * i / 64) + 0.08 * std::cos(4 * Pi * i / 64));
                sum += mFir[i];
            }
            for (double& tap : mFir)
                tap /= sum;
        }

        double process(double input, const Settings& p)
        {
            const bool active = p.mode != 0 || p.cabinet != 0 || std::abs(p.bass) + std::abs(p.mid)
                + std::abs(p.treble) > 0.00001 || std::abs(p.output - 1) > 0.00001;
            mBlend = ProceduralDsp::smooth(mBlend, active && p.mix > 0.000001 ? 1.0 : 0.0, 0.004);
            if (mBlend < 0.000001)
            {
                if (mWasActive)
                {
                    mUp.fill(0); mDown.fill(0); mDry.fill(0); mCab.fill(0);
                    mBass = mUpper = mTone = mDcIn = mDcOut = 0;
                    mWasActive = false;
                }
                return input; // Legacy patches retain the exact old signal path.
            }
            mWasActive = true;
            if ((mFrames++ & 15) == 0)
            {
                mGain = std::pow(10.0, p.drive / 20);
                mCompensation = 1 / std::sqrt(mGain);
                mToneCoefficient = 1 - std::exp(-2 * Pi * p.tone / 48000);
                mEq = { std::pow(10.0, p.bass / 20), std::pow(10.0, p.mid / 20), std::pow(10.0, p.treble / 20) };
            }
            for (std::size_t i = 0; i < mWeights.size(); ++i)
                mWeights[i] = ProceduralDsp::smooth(mWeights[i], static_cast<int>(i) == p.mode ? 1.0 : 0.0, 0.004);
            for (std::size_t i = 0; i < mCabWeights.size(); ++i)
                mCabWeights[i] = ProceduralDsp::smooth(mCabWeights[i], static_cast<int>(i) == p.cabinet ? 1.0 : 0.0, 0.004);
            mDry[mUpPosition] = input;
            const double dry = mDry[(mUpPosition + 1) % mDry.size()];
            mBass = zap(mBass + 0.032195 * (input - mBass)); // 250 Hz
            mUpper = zap(mUpper + 0.279043 * (input - mUpper)); // 2500 Hz
            mUp[mUpPosition] = mBass * mEq[0] + (mUpper - mBass) * mEq[1] + (input - mUpper) * mEq[2];
            double wet = 0;
            for (std::size_t phase = 0; phase < 4; ++phase)
            {
                double up = 0;
                for (std::size_t tap = phase; tap < mFir.size(); tap += 4)
                    up += 4 * mFir[tap] * mUp[(mUpPosition + mUp.size() - tap / 4) % mUp.size()];
                // Use the synth's nominal pre-volume level for drive calibration.
                const double driven = up * 0.18 * mGain;
                const double shaped = up * mWeights[0] + mCompensation / 0.18 * (
                    mWeights[1] * std::tanh(driven)
                    + mWeights[2] * std::clamp(driven, -1.0, 1.0)
                    + mWeights[3] * (std::tanh(driven * 3 + 0.4) - std::tanh(0.4)) / 1.5);
                mDown[mDownPosition] = shaped;
                if (phase == 0)
                    for (std::size_t tap = 0; tap < mFir.size(); ++tap)
                        wet += mFir[tap] * mDown[(mDownPosition + mDown.size() - tap) % mDown.size()];
                mDownPosition = (mDownPosition + 1) % mDown.size();
            }
            mUpPosition = (mUpPosition + 1) % mUp.size();
            // Block fuzz DC and remove inaudible rumble before speaker coloration.
            const double dc = wet - mDcIn + 0.99738543 * mDcOut; // 20 Hz
            mDcIn = wet; mDcOut = zap(dc);
            mTone = zap(mTone + mToneCoefficient * (dc - mTone));
            wet = mTone * mCabWeights[0];
            // Simple speaker bandwidth models, not cabinet IR convolution.
            constexpr std::array<double, 3> coefficients{ 0.544062, 0.480297, 0.407615 }; // 6 / 5 / 4 kHz
            for (std::size_t cabinet = 0; cabinet < 3; ++cabinet)
            {
                double sample = mTone;
                for (std::size_t pole = 0; pole < 4; ++pole)
                {
                    auto& state = mCab[cabinet * 4 + pole];
                    state = zap(state + coefficients[cabinet] * (sample - state));
                    sample = state;
                }
                wet += sample * mCabWeights[cabinet + 1];
            }
            const double result = dry + (wet * p.output - dry) * p.mix;
            return input + (result - input) * mBlend;
        }

    private:
        static constexpr double Pi = 3.14159265358979323846;
        static double zap(double x) { return std::abs(x) < 1e-20 ? 0 : x; }
        std::array<double, 65> mFir{}, mDown{};
        std::array<double, 17> mUp{}, mDry{};
        std::array<double, 4> mWeights{ 1, 0, 0, 0 }, mCabWeights{ 1, 0, 0, 0 };
        std::array<double, 12> mCab{};
        std::array<double, 3> mEq{ 1, 1, 1 };
        std::size_t mUpPosition = 0, mDownPosition = 0;
        unsigned mFrames = 0;
        bool mWasActive = false;
        double mBlend = 0, mBass = 0, mUpper = 0, mTone = 0, mDcIn = 0, mDcOut = 0;
        double mGain = 1, mCompensation = 1, mToneCoefficient = 1;
    };
}
#endif
