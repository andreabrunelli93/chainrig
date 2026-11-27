#include "Pedals.h"
#include "TransparentDrive.h"
#include "OtaCompressor.h"
#include "OpAmpOverdrive.h"
#include "BbdChorus.h"
#include "StereoChorus.h"
#include "VcaCompressor.h"
#include "TapeMachine.h"

namespace Pedals
{
    const std::vector<Entry>& all()
    {
        static const std::vector<Entry> entries
        {
            { TransparentDrive::getInfo(), [] { return std::make_unique<TransparentDrive>(); } },
            { OtaCompressor::getInfo(),    [] { return std::make_unique<OtaCompressor>(); } },
            { VcaCompressor::getInfo(),    [] { return std::make_unique<VcaCompressor>(); } },
            { OpAmpOverdrive::getInfo(),   [] { return std::make_unique<OpAmpOverdrive>(); } },
            { BbdChorus::getInfo(),        [] { return std::make_unique<BbdChorus>(); } },
            { StereoChorus::getInfo(),     [] { return std::make_unique<StereoChorus>(); } },
            { TapeMachine::getInfo(),      [] { return std::make_unique<TapeMachine>(); } },
        };
        return entries;
    }

    const Entry* find (const juce::String& identifier)
    {
        for (auto& e : all())
            if (e.info.identifier == identifier)
                return &e;
        return nullptr;
    }
}
