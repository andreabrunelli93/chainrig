#pragma once

#include "PedalProcessor.h"
#include <functional>

//==============================================================================
/** Elenco dei pedali integrati. Per aggiungerne uno: una riga in Pedals.cpp. */
namespace Pedals
{
    struct Entry
    {
        PedalProcessor::Info info;
        std::function<std::unique_ptr<PedalProcessor>()> create;
    };

    const std::vector<Entry>& all();
    const Entry* find (const juce::String& identifier);
}
