#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

//==============================================================================
/** Posti integrati (NAM e pedali), con la categoria del sottomenu "Built-in effects". */
namespace BuiltIns
{
    struct Entry
    {
        juce::PluginDescription description;
        juce::String menuCategory;
    };

    const std::vector<Entry>& all();
    bool exists (const juce::String& identifier);
}

//==============================================================================
/** Formato "Built-in" per AudioPluginFormatManager: così i posti integrati passano dalla stessa
    strada dei VST3 (creazione, preset, riuso dei nodi, nome del plugin). */
class BuiltInPluginFormat final : public juce::AudioPluginFormat
{
public:
    static constexpr const char* formatName = "Built-in";

    juce::String getName() const override                                  { return formatName; }
    void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results, const juce::String& id) override;
    bool fileMightContainThisPluginType (const juce::String& id) override  { return BuiltIns::exists (id); }
    juce::String getNameOfPluginFromIdentifier (const juce::String& id) override { return id; }
    bool pluginNeedsRescanning (const juce::PluginDescription&) override   { return false; }
    bool doesPluginStillExist (const juce::PluginDescription& d) override  { return BuiltIns::exists (d.fileOrIdentifier); }
    bool canScanForPlugins() const override                                { return false; }
    bool isTrivialToScan() const override                                  { return true; }
    juce::StringArray searchPathsForPlugins (const juce::FileSearchPath&, bool, bool) override { return {}; }
    juce::FileSearchPath getDefaultLocationsToSearch() override            { return {}; }
    bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override { return false; }

private:
    void createPluginInstance (const juce::PluginDescription&, double, int, PluginCreationCallback) override;
};
