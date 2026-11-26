#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <functional>
#include <optional>
#include "Tuner.h"
#include "Recorder.h"

//==============================================================================
/** Il motore: scheda audio, catena di plugin, pulsanti, MIDI, preset.

    Tutti i metodi pubblici vanno chiamati dal thread dell'interfaccia
    (message thread). Il thread audio vede solo l'AudioProcessorGraph. */
class Engine final : public juce::ChangeBroadcaster,
                     private juce::ChangeListener,
                     private juce::MidiInputCallback,
                     private juce::Timer
{
public:
    static constexpr int numSlots   = 10;
    static constexpr int numButtons = 4;

    struct TouchButton
    {
        juce::String label;
        bool active = false;
        juce::Array<int> slots;   // posti controllati: accesi solo se il pulsante è attivo
    };

    enum class MidiMode { momentary, absolute };

    /** Che cosa fa un posto: decide l'icona e l'etichetta. "automatic" lo ricava dal plugin. */
    enum class SlotKind { automatic, amp, ir, drive, compressor, eq, modulation, delay, reverb, fx };
    static constexpr SlotKind allSlotKinds[] = { SlotKind::amp, SlotKind::ir, SlotKind::drive, SlotKind::compressor,
                                                 SlotKind::eq, SlotKind::modulation, SlotKind::delay,
                                                 SlotKind::reverb, SlotKind::fx };

    static juce::String getSlotKindTag (SlotKind);    // breve, per l'etichetta: "AMP", "CAB · IR"...
    static juce::String getSlotKindName (SlotKind);   // per i menu: "Amplificatore"...
    static juce::String getSlotKindId (SlotKind);     // nei preset: "amp", "ir"...
    static SlotKind getSlotKindFromId (const juce::String&);

    Engine();
    ~Engine() override;

    //==============================================================================
    juce::AudioDeviceManager deviceManager;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;

    juce::PropertiesFile& props();
    juce::File getDeadMansPedalFile() const;
    juce::File getPresetFolder() const;

    /** Chiamata prima di distruggere un plugin (per chiudere la sua finestra). */
    std::function<void (juce::AudioProcessor*)> onProcessorAboutToBeDeleted;

    //==============================================================================
    // Posti della catena
    bool loadPluginIntoSlot (int slot, const juce::PluginDescription&, juce::String& error);
    void clearSlot (int slot);
    void swapSlots (int a, int b);

    bool isSlotEmpty (int slot) const;
    juce::AudioProcessor* getSlotProcessor (int slot) const;
    juce::String getSlotName (int slot) const;            // nome scelto dall'utente, altrimenti quello del plugin
    juce::String getSlotPluginName (int slot) const;
    juce::String getSlotManufacturer (int slot) const;
    juce::String getSlotCustomName (int slot) const;
    void setSlotCustomName (int slot, const juce::String& name);
    SlotKind getSlotKind (int slot) const;                // risolto: mai "automatic"
    SlotKind getSlotKindSetting (int slot) const;         // come l'ha scelto l'utente (anche "automatic")
    SlotKind guessSlotKind (int slot) const;              // ricavato da categoria e nome del plugin
    void setSlotKind (int slot, SlotKind);
    bool getSlotUserBypass (int slot) const;
    void setSlotUserBypass (int slot, bool bypassed);
    bool isSlotActive (int slot) const;          // suona davvero (né bypass né pulsante spento)
    bool isSlotControlledByButton (int slot) const;

    //==============================================================================
    // Pulsanti touch / MIDI
    const TouchButton& getButton (int index) const;
    void toggleButton (int index);
    void setButtonActive (int index, bool active);
    void setButtonLabel (int index, const juce::String& label);
    void setButtonControlsSlot (int index, int slot, bool controls);

    int getButtonCC (int index) const;
    int getTunerCC() const;
    int getNextPresetCC() const;
    int getPrevPresetCC() const;
    MidiMode getMidiMode() const;
    void setMidiMode (MidiMode);

    //==============================================================================
    // Accordatore
    TunerProcessor& getTuner()                    { return *tuner; }
    bool isTunerActive() const                    { return tunerActive; }
    void setTunerActive (bool);

    //==============================================================================
    // Preset
    juce::Array<juce::File> getPresetFiles() const;
    bool savePreset (const juce::File&);
    bool loadPreset (const juce::File&, juce::String& errors);
    void loadAdjacentPreset (int delta);
    void newEmptyPreset();
    juce::File getCurrentPresetFile() const       { return currentPresetFile; }

    /** Tempo comunicato ai plugin (delay e riverberi sincronizzati). Salvato nel preset. */
    double getBpm() const                         { return playHead.bpm.load(); }
    void setBpm (double bpm);
    juce::String getCurrentPresetName() const;

    //==============================================================================
    // Varie
    int getInputChannel() const                   { return inputChannel; }
    void setInputChannel (int channelIndex);
    int getNumDeviceInputs() const;
    juce::String getStatusText() const;

    /** Registratore dell'uscita: salva un WAV nella cartella Download. */
    Recorder& getRecorder()             { return recorder; }
    void saveSettings();

private:
    struct Slot
    {
        std::optional<juce::AudioProcessorGraph::NodeID> node;
        juce::PluginDescription description;   // quella usata per crearlo (salvata nei preset)
        bool userBypassed = false;
        juce::String customName;               // vuoto = nome del plugin
        SlotKind kind = SlotKind::automatic;
    };

    using NodeID = juce::AudioProcessorGraph::NodeID;
    using UpdateKind = juce::AudioProcessorGraph::UpdateKind;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void timerCallback() override;

    void handleControllerOnMessageThread (int cc, int value);
    void installInSlot (int slot, std::unique_ptr<juce::AudioPluginInstance>, const juce::PluginDescription&, UpdateKind);
    void removeSlotNode (int slot, UpdateKind);
    void rewire();
    void connectNodes (NodeID from, NodeID to, int maxDestChannels);
    void updateBypassStates();
    void enableAllMidiInputs();
    static void configureBusLayout (juce::AudioPluginInstance&);
    std::unique_ptr<juce::AudioPluginInstance> createInstance (const juce::PluginDescription&, juce::String& error,
                                                               juce::PluginDescription& usedDescription);

    /** Senza un BPM i plugin VST3 ricevono "tempo non valido" (0): alcuni delay sincronizzati
        allora portano il tempo al massimo e sembrano un looper di 10-20 secondi. */
    struct TempoPlayHead final : public juce::AudioPlayHead
    {
        std::atomic<double> bpm { 120.0 };

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (bpm.load());
            info.setTimeSignature (TimeSignature { 4, 4 });
            info.setIsPlaying (false);
            return info;
        }
    };

    mutable juce::ApplicationProperties appProperties;
    TempoPlayHead playHead;                     // deve vivere più a lungo del grafo
    juce::AudioProcessorGraph graph;
    juce::AudioProcessorPlayer player;
    Recorder recorder { player };   // avvolge il player: è lui il callback della scheda audio

    NodeID inputNode, outputNode, tunerNode;
    TunerProcessor* tuner = nullptr;

    std::array<Slot, numSlots> slots;
    std::array<TouchButton, numButtons> buttons;
    bool tunerActive = false;
    int inputChannel = 0;
    juce::File currentPresetFile;

    juce::StringArray knownMidiInputs;

    // Stato della scheda letto solo quando cambia: interrogare CoreAudio/ASIO di continuo
    // dal thread dell'interfaccia può disturbare il thread audio
    void updateDeviceInfo();
    void checkAudioHealth();
    juce::String deviceName;
    double deviceSampleRate = 0.0, deviceLatencyMs = 0.0;
    int deviceBufferSize = 0;

    float recentPeakLoad = 0.0f;              // carico massimo negli ultimi 2 s
    juce::Array<float> loadWindow;            // ultime letture (ogni 500 ms)
    int loggedXruns = 0, loggedOverloads = 0, loggedLate = 0;
    int timerTicks = 0;

    JUCE_DECLARE_WEAK_REFERENCEABLE (Engine)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Engine)
};
