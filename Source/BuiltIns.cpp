#include "BuiltIns.h"
#include "NamProcessor.h"
#include "Pedals/Pedals.h"

using namespace juce;

namespace BuiltIns
{
    const std::vector<Entry>& all()
    {
        static const std::vector<Entry> entries = []
        {
            std::vector<Entry> e;
            e.push_back ({ NamProcessor::getDescription(), "Amp / Cab" });
            for (auto& p : Pedals::all())
                e.push_back ({ PedalProcessor::describe (p.info), p.info.menuCategory });
            return e;
        }();
        return entries;
    }

    bool exists (const String& identifier)
    {
        for (auto& e : all())
            if (e.description.fileOrIdentifier == identifier)
                return true;
        return false;
    }
}

void BuiltInPluginFormat::findAllTypesForFile (OwnedArray<PluginDescription>& results, const String& id)
{
    for (auto& e : BuiltIns::all())
        if (e.description.fileOrIdentifier == id)
            results.add (new PluginDescription (e.description));
}

void BuiltInPluginFormat::createPluginInstance (const PluginDescription& d, double, int, PluginCreationCallback callback)
{
    if (d.fileOrIdentifier == NamProcessor::identifier)
        callback (std::make_unique<NamProcessor>(), {});
    else if (auto* pedal = Pedals::find (d.fileOrIdentifier))
        callback (pedal->create(), {});
    else
        callback (nullptr, "Unknown built-in plugin: " + d.fileOrIdentifier);
}
