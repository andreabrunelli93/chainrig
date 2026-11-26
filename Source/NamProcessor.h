#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <memory>

//==============================================================================
/** Parametro di un posto integrato (AudioPluginInstance accetta solo HostedAudioProcessorParameter).
    get() è nel valore reale (dB, oppure 0/1 per gli interruttori). */
class NamParameter final : public juce::HostedAudioProcessorParameter
{
public:
    NamParameter (const juce::String& parameterId, const juce::String& parameterName,
                  juce::NormalisableRange<float> r, float defaultRealValue, const juce::String& unit, bool boolean = false)
        : range (r), id (parameterId), name (parameterName), label (unit),
          defaultValue (r.convertTo0to1 (defaultRealValue)), isBool (boolean), value (defaultValue) {}

    float get() const                    { return range.convertFrom0to1 (value.load()); }
    bool isOn() const                    { return value.load() >= 0.5f; }
    void set (float realValue)           { setValueNotifyingHost (range.convertTo0to1 (range.snapToLegalValue (realValue))); }

    juce::String getParameterID() const override        { return id; }
    float getValue() const override                     { return value.load(); }
    void setValue (float v) override                    { value = v; }
    float getDefaultValue() const override              { return defaultValue; }
    juce::String getName (int maximumLength) const override { return name.substring (0, maximumLength); }
    juce::String getLabel() const override              { return label; }
    juce::String getText (float v, int) const override
    {
        return isBool ? juce::String (v >= 0.5f ? "On" : "Off") : juce::String (range.convertFrom0to1 (v), 1);
    }
    float getValueForText (const juce::String& text) const override
    {
        if (isBool)
            return text.equalsIgnoreCase ("on") || text.getIntValue() != 0 ? 1.0f : 0.0f;
        return range.convertTo0to1 (range.snapToLegalValue (text.getFloatValue()));
    }
    bool isBoolean() const override                     { return isBool; }
    bool isDiscrete() const override                    { return isBool; }
    int getNumSteps() const override                    { return isBool ? 2 : juce::AudioProcessor::getDefaultNumParameterSteps(); }

    const juce::NormalisableRange<float> range;

private:
    const juce::String id, name, label;
    const float defaultValue;
    const bool isBool;
    std::atomic<float> value;
};

//==============================================================================
/** Posto "NAM" integrato: una cattura Neural Amp Modeler (.nam, anche A2) seguita da una IR
    facoltativa, con gain d'ingresso e d'uscita. Suona il canale sinistro e lo manda su entrambi.

    Le catture si caricano sul thread dell'interfaccia; il thread audio le prende con uno scambio
    atomico e passa dalla vecchia alla nuova con una dissolvenza incrociata di un blocco. */
class NamProcessor final : public juce::AudioPluginInstance,
                           private juce::Timer
{
public:
    NamProcessor();
    ~NamProcessor() override;

    static constexpr const char* formatName = "Built-in";
    static constexpr const char* identifier = "ChainRig NAM";
    static juce::PluginDescription getDescription();

    /** Cartella in cui cercare per nome i file che non si trovano più al percorso salvato
        (preset portati da un altro computer). */
    static juce::File getTonesFolder();

    //==============================================================================
    /** Thread dell'interfaccia. Restituisce false (e spiega in error) se il file non si carica. */
    bool loadModel (const juce::File&, juce::String& error);
    void clearModel();
    bool loadImpulseResponse (const juce::File&, juce::String& error);
    void clearImpulseResponse();

    juce::File getModelFile() const        { return modelFile; }
    juce::File getImpulseResponseFile() const { return irFile; }
    bool isModelMissing() const            { return modelFile != juce::File() && ! modelLoaded; }
    bool isImpulseResponseMissing() const  { return irFile != juce::File() && ! irLoaded; }

    /** Dati della cattura (dai metadati del file .nam). */
    struct ModelInfo
    {
        juce::String name, gear, modeledBy, architecture;
        juce::int64 toneId = 0;   // t3k_id: la cattura su TONE3000
        double sampleRate = 0, loudness = 0;
        bool hasLoudness = false, slimmable = false;
    };
    const ModelInfo& getModelInfo() const  { return modelInfo; }

    bool isLite() const                    { return lite; }
    void setLite (bool shouldBeLite);

    NamParameter* inputGain  = nullptr;   // dB
    NamParameter* outputGain = nullptr;   // dB
    NamParameter* normalize  = nullptr;   // porta la cattura a un volume di riferimento
    NamParameter* irEnabled  = nullptr;

    /** L'interfaccia si aggiorna quando cambia la cattura o la IR. */
    std::function<void()> onFilesChanged;

    //==============================================================================
    /** Nel posto compare il nome della cattura. */
    const juce::String getName() const override              { return modelFile == juce::File() ? juce::String ("NAM") : modelInfo.name; }
    void fillInPluginDescription (juce::PluginDescription&) const override;

    void prepareToPlay (double sampleRate, int maximumBlockSize) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    double getTailLengthSeconds() const override             { return 0.0; }
    bool acceptsMidi() const override                        { return false; }
    bool producesMidi() const override                       { return false; }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                          { return true; }
    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override                    {}
    const juce::String getProgramName (int) override         { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

private:
    struct Model;

    void timerCallback() override;
    void prepareModel (Model&) const;
    void publish (std::unique_ptr<Model>);
    void stateChanged();
    static juce::File findMovedFile (const juce::File&);

    // Thread dell'interfaccia
    juce::File modelFile, irFile;
    bool modelLoaded = false, irLoaded = false, lite = false;
    ModelInfo modelInfo;
    double preparedRate = 0;
    int preparedBlock = 0;

    // Passaggio di proprietà tra i thread: l'interfaccia mette in pending, l'audio lo prende,
    // l'audio mette il vecchio in retired e l'interfaccia lo cancella (timer)
    std::atomic<Model*> pending { nullptr }, retired { nullptr };
    std::atomic<bool> clearPending { false };
    Model* current = nullptr;   // solo thread audio (e prepareToPlay)

    juce::dsp::Convolution convolution;
    std::atomic<bool> irActive { false };
    juce::AudioBuffer<double> inDouble, outDouble, fadeDouble;
    juce::SmoothedValue<float> inGainSmoothed, outGainSmoothed, irMix;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NamProcessor)
};

//==============================================================================
/** Formato "Built-in" per AudioPluginFormatManager: così i posti integrati passano dalla stessa
    strada dei VST3 (creazione, preset, riuso dei nodi, nome del plugin). */
class BuiltInPluginFormat final : public juce::AudioPluginFormat
{
public:
    juce::String getName() const override                                  { return NamProcessor::formatName; }
    void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results, const juce::String& id) override
    {
        if (id == NamProcessor::identifier)
            results.add (new juce::PluginDescription (NamProcessor::getDescription()));
    }
    bool fileMightContainThisPluginType (const juce::String& id) override  { return id == NamProcessor::identifier; }
    juce::String getNameOfPluginFromIdentifier (const juce::String& id) override { return id; }
    bool pluginNeedsRescanning (const juce::PluginDescription&) override   { return false; }
    bool doesPluginStillExist (const juce::PluginDescription& d) override  { return d.fileOrIdentifier == NamProcessor::identifier; }
    bool canScanForPlugins() const override                                { return false; }
    bool isTrivialToScan() const override                                  { return true; }
    juce::StringArray searchPathsForPlugins (const juce::FileSearchPath&, bool, bool) override { return {}; }
    juce::FileSearchPath getDefaultLocationsToSearch() override            { return {}; }
    bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override { return false; }

private:
    void createPluginInstance (const juce::PluginDescription& d, double, int, PluginCreationCallback callback) override
    {
        if (d.fileOrIdentifier == NamProcessor::identifier)
            callback (std::make_unique<NamProcessor>(), {});
        else
            callback (nullptr, "Unknown built-in plugin: " + d.fileOrIdentifier);
    }
};
