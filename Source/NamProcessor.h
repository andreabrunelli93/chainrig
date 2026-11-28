#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "BuiltInParameter.h"
#include <atomic>
#include <memory>

using NamParameter = BuiltInParameter;

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
