#include "Pedals.h"
#include "TransparentDrive.h"

namespace Pedals
{
    const std::vector<Entry>& all()
    {
        static const std::vector<Entry> entries
        {
            { TransparentDrive::getInfo(), [] { return std::make_unique<TransparentDrive>(); } },
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
