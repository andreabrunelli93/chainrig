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
                     private juce::AudioProcessorListener,
                     private juce::MidiInputCallback,
                     private juce::Timer
{
public:
    static constexpr int numSlots   = 10;
    static constexpr int numButtons = 4;

    /** switch: accende e spegne i posti assegnati.
        scene: richiama un'istantanea di quali posti sono accesi (una sola scena attiva alla volta).
        kill: finché è premuto (touch o MIDI, valore >= 64) taglia la chitarra all'ingresso, come il kill switch
              sulla chitarra: le code di delay e riverbero continuano. */
    enum class ButtonMode { toggle, scene, kill };

    struct TouchButton
    {
        juce::String label;
        bool active = false;          // switch: acceso; scena: è la scena attiva
        ButtonMode mode = ButtonMode::toggle;
        juce::Array<int> slots;       // switch: posti controllati (accesi solo se il pulsante è attivo)
        juce::Array<int> sceneOn;     // scena: posti accesi; gli altri posti pieni vanno in bypass
    };

    enum class MidiMode { momentary, absolute };

    /** Che cosa fa un posto: decide l'icona e l'etichetta. "automatic" lo ricava dal plugin. */
    enum class SlotKind { automatic, amp, ir, drive, compressor, eq, modulation, delay, reverb, tape, fx };   // fx sempre l'ultimo
    static constexpr SlotKind allSlotKinds[] = { SlotKind::amp, SlotKind::ir, SlotKind::drive, SlotKind::compressor,
                                                 SlotKind::eq, SlotKind::modulation, SlotKind::delay,
                                                 SlotKind::reverb, SlotKind::tape, SlotKind::fx };

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

    /** Sposta un posto: su un posto vuoto ci va e basta; su uno occupato si inserisce lì
        e quelli in mezzo scorrono di uno. Le assegnazioni dei pedali seguono i plugin. */
    void moveSlot (int from, int to);

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

    /** Nome mostrato sul pedale: quello dei posti che controlla ("Drive", "Drive + Delay",
        "Drive + 2"); se non controlla posti pieni, la sua etichetta (BOOST, DRIVE...). */
    juce::String getButtonDisplayName (int index) const;
    void toggleButton (int index);
    void setButtonActive (int index, bool active);
    void setButtonLabel (int index, const juce::String& label);
    void setButtonControlsSlot (int index, int slot, bool controls);
    void setButtonMode (int index, ButtonMode mode);
    void storeScene (int index);      // salva nella scena lo stato on/off attuale dei posti
    void recallScene (int index);     // applica la scena (istantaneo, nessun plugin ricaricato)

    int getButtonCC (int index) const;
    int getTunerCC() const;
    int getTapTempoCC() const;
    int getNextPresetCC() const;
    int getPrevPresetCC() const;
    int getExpressionCC() const   { return expressionCC.load(); }   // pedale d'espressione (Whammy)
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
    juce::File getPresetFileByName (const juce::String& name) const;

    //==============================================================================
    // Scaletta: elenco ordinato di canzoni, ognuna con il suo preset. Quando è attiva,
    // < >, i CC avanti/indietro e il Program Change seguono la scaletta (senza ricominciare
    // dall'inizio alla fine). File .chainset (JSON) in Documents/ChainRig/Setlists.
    struct SetlistEntry
    {
        juce::String song;      // può essere vuoto: si mostra il nome del preset
        juce::String preset;    // nome del preset (file senza estensione): vale su Mac e Windows
    };

    juce::File getSetlistFolder() const;
    juce::Array<juce::File> getSetlistFiles() const;
    bool isSetlistActive() const                  { return currentSetlistFile != juce::File(); }
    juce::File getCurrentSetlistFile() const      { return currentSetlistFile; }
    const juce::Array<SetlistEntry>& getSetlist() const { return setlist; }
    int getSetlistIndex() const                   { return setlistIndex; }   // -1: preset fuori scaletta
    juce::String getCurrentSongName() const;

    static juce::Array<SetlistEntry> readSetlist (const juce::File&);
    bool saveSetlist (const juce::File&, const juce::Array<SetlistEntry>&);   // la rende anche attiva
    bool openSetlist (const juce::File&);
    void closeSetlist();
    bool loadSetlistEntry (int index, juce::String& errors);

    /** Vero se la catena è cambiata dall'ultimo salvataggio o caricamento
        (accendere/spegnere i pedali non conta: è l'uso normale dal vivo). */
    bool hasUnsavedChanges() const                { return dirty; }

    /** Messaggio da mostrare all'avvio (ripristino dopo una chiusura inattesa), poi si svuota. */
    juce::String takeStartupMessage()             { return std::exchange (startupMessage, {}); }

    /** Tempo comunicato ai plugin (delay e riverberi sincronizzati). Salvato nel preset. */
    double getBpm() const                         { return playHead.bpm.load(); }
    void setBpm (double bpm);

    /** Tap tempo (touch o pedale MIDI): la media degli ultimi intervalli diventa il BPM.
        Come i pedali, non conta come modifica non salvata. */
    void tapTempo();
    juce::String getCurrentPresetName() const;

    //==============================================================================
    // Varie
    int getInputChannel() const                   { return inputChannel; }

    // Stadio d'ingresso: gain (salvato nelle impostazioni, non nel preset) e MUTE
    float getInputGainDb() const                  { return tuner->inputGainDb.load(); }
    void setInputGainDb (float db);
    bool isInputMuted() const                     { return tuner->inputMuted.load(); }
    void setInputMuted (bool);
    void setInputChannel (int channelIndex);

    /** Code di delay e riverberi al cambio preset: quelli che suonavano continuano
        (con le impostazioni vecchie, senza ingresso) e sfumano, mentre il preset nuovo è già attivo. */
    bool isSpilloverEnabled() const;
    void setSpilloverEnabled (bool);

    // Volume d'uscita generale (salvato nelle impostazioni). minOutputGainDb = muto.
    static constexpr float minOutputGainDb = -60.0f, maxOutputGainDb = 12.0f;
    float getOutputGainDb() const                 { return outputGainDb; }
    void setOutputGainDb (float db);
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

    /** Un plugin con ingresso mono e uscita stereo, in bypass, lascia muto il canale destro
        (il bypass di JUCE copia solo i canali che hanno un ingresso). Dopo questi plugin c'è
        un piccolo nodo che, solo quando il posto è in bypass, copia il sinistro nel destro. */
    struct MonoBypassFix;

    /** Coda di un delay/riverbero del preset precedente: il plugin resta nel grafo senza
        ingresso, passa per un TailFader e va dritto all'uscita finché non è sfumato. */
    struct TailFader;
    struct Tail
    {
        NodeID plugin, fader;
        juce::uint32 removeAtMs = 0;
    };
    juce::Array<Tail> tails;
    void keepAsTail (int slot);
    void removeTail (int index, UpdateKind);
    void removeExpiredTails();
    std::array<MonoBypassFix*, numSlots> monoFix {};
    juce::Array<NodeID> monoFixNodes;

    /** In fondo alla catena (prima dell'uscita, le code restano fuori): quando cambia l'ordine
        dei plugin, rewire() abbassa la catena, scambia il grafo e la rialza. Niente scatti. */
    struct ChainFader;
    ChainFader* chainFader = nullptr;
    NodeID chainFaderNode;
    juce::Array<NodeID> lastChain;   // i nodi della catena all'ultimo rewire()
    bool fadeOutChain();
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

    // CC dei pedali in modo kill (-1 = nessuno), letti dal thread MIDI per tagliare subito senza passare
    // dal thread dell'interfaccia (che può essere occupato, per esempio mentre carica un preset)
    std::array<std::atomic<int>, numButtons> killCCs { -1, -1, -1, -1 };
    void refreshKillState();
    std::atomic<int> expressionCC { 11 };

    std::array<Slot, numSlots> slots;
    std::array<TouchButton, numButtons> buttons;
    bool tunerActive = false;
    int inputChannel = 0;
    juce::File currentPresetFile;

    juce::File currentSetlistFile;
    juce::Array<SetlistEntry> setlist;
    int setlistIndex = -1;
    void syncSetlistIndex();
    void saveSetlistPosition();

    juce::StringArray knownMidiInputs;
    juce::String lastAudioSetupKey;          // per riconoscere i change message dovuti solo al MIDI
    juce::String getAudioSetupKey() const;
    float outputGainDb = 0.0f;
    juce::Array<juce::uint32> tapTimes;

    // Preset come dati (usati sia per i file .chainrig sia per il recupero)
    juce::var createPresetState() const;
    bool loadPresetState (const juce::var& root, juce::String& errors, const juce::String& nameForLog);

    // Modifiche non salvate e recupero dopo una chiusura inattesa
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override;
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override;
    void markDirty();
    void markClean();
    void listenTo (juce::AudioProcessorGraph::NodeID);
    juce::File getRecoveryFile() const;
    void writeRecoveryIfNeeded();
    bool restoreFromRecovery();

    bool dirty = false;
    std::atomic<bool> pluginStateChanged { false };   // impostato anche dal thread audio
    juce::uint32 ignorePluginChangesUntil = 0;
    bool recoveryPending = false;
    juce::uint32 lastRecoveryWrite = 0;
    juce::String startupMessage;

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
