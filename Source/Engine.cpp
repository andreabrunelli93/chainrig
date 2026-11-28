#include "Engine.h"
#include "PluginScanner.h"
#include "BuiltIns.h"

using namespace juce;

namespace
{
    constexpr const char* presetExtension = ".chainrig";
    constexpr const char* setlistExtension = ".chainset";

    const char* defaultButtonLabels[] = { "BOOST", "DRIVE", "DELAY", "FX" };
}

//==============================================================================
//==============================================================================
namespace
{
    constexpr double tailHoldSeconds = 9.0, tailFadeSeconds = 3.0;
    constexpr int maxTails = 4;   // cambi preset molto rapidi: le code più vecchie lasciano il posto
}

struct Engine::TailFader final : public AudioProcessor
{
    TailFader() : AudioProcessor (BusesProperties().withInput  ("In",  AudioChannelSet::stereo(), true)
                                                     .withOutput ("Out", AudioChannelSet::stereo(), true)) {}

    void prepareToPlay (double sr, int) override
    {
        holdSamples = (int64) (sr * tailHoldSeconds);
        fadeSamples = jmax ((int64) 1, (int64) (sr * tailFadeSeconds));
    }

    void processBlock (AudioBuffer<float>& buffer, MidiBuffer&) override
    {
        const int n = buffer.getNumSamples();

        // guadagno all'inizio e alla fine del blocco: piatto, poi rampa lineare fino a zero
        const auto gainAt = [this] (int64 pos)
        {
            if (pos <= holdSamples) return 1.0f;
            return jmax (0.0f, 1.0f - (float) (pos - holdSamples) / (float) fadeSamples);
        };

        const float g0 = gainAt (position), g1 = gainAt (position + n);
        position += n;

        if (exactlyEqual (g0, 1.0f) && exactlyEqual (g1, 1.0f))
            return;

        if (exactlyEqual (g0, 0.0f) && exactlyEqual (g1, 0.0f))
            buffer.clear();
        else
            buffer.applyGainRamp (0, n, g0, g1);
    }

    const String getName() const override                        { return "Tail fader"; }
    void releaseResources() override                             {}
    double getTailLengthSeconds() const override                 { return 0.0; }
    bool acceptsMidi() const override                            { return false; }
    bool producesMidi() const override                           { return false; }
    AudioProcessorEditor* createEditor() override                { return nullptr; }
    bool hasEditor() const override                              { return false; }
    int getNumPrograms() override                                { return 1; }
    int getCurrentProgram() override                             { return 0; }
    void setCurrentProgram (int) override                        {}
    const String getProgramName (int) override                   { return {}; }
    void changeProgramName (int, const String&) override         {}
    void getStateInformation (MemoryBlock&) override             {}
    void setStateInformation (const void*, int) override         {}

private:
    int64 position = 0, holdSamples = 0, fadeSamples = 1;   // solo thread audio (dopo prepareToPlay)
};

//==============================================================================
struct Engine::MonoBypassFix final : public AudioProcessor
{
    MonoBypassFix() : AudioProcessor (BusesProperties().withInput  ("In",  AudioChannelSet::stereo(), true)
                                                        .withOutput ("Out", AudioChannelSet::stereo(), true)) {}

    std::atomic<bool> slotBypassed { false };

    void processBlock (AudioBuffer<float>& buffer, MidiBuffer&) override
    {
        if (slotBypassed.load() && buffer.getNumChannels() >= 2)
            buffer.copyFrom (1, 0, buffer, 0, 0, buffer.getNumSamples());
    }

    const String getName() const override                        { return "Mono bypass fix"; }
    void prepareToPlay (double, int) override                    {}
    void releaseResources() override                             {}
    double getTailLengthSeconds() const override                 { return 0.0; }
    bool acceptsMidi() const override                            { return false; }
    bool producesMidi() const override                           { return false; }
    AudioProcessorEditor* createEditor() override                { return nullptr; }
    bool hasEditor() const override                              { return false; }
    int getNumPrograms() override                                { return 1; }
    int getCurrentProgram() override                             { return 0; }
    void setCurrentProgram (int) override                        {}
    const String getProgramName (int) override                   { return {}; }
    void changeProgramName (int, const String&) override         {}
    void getStateInformation (MemoryBlock&) override             {}
    void setStateInformation (const void*, int) override         {}
};

//==============================================================================
namespace
{
    constexpr double chainFadeOutMs = 8.0, chainFadeInMs = 30.0;
    constexpr int chainSilentBlocks = 2;   // dopo graph.rebuild(): il thread audio passa alla catena nuova al blocco dopo
}

struct Engine::ChainFader final : public AudioProcessor
{
    ChainFader() : AudioProcessor (BusesProperties().withInput  ("In",  AudioChannelSet::stereo(), true)
                                                     .withOutput ("Out", AudioChannelSet::stereo(), true)) {}

    /** Thread dell'interfaccia: abbassa; isSilent() dice quando è arrivato a zero. */
    void fadeOut()
    {
        silent = false;
        wantOut = true;
    }

    /** Thread dell'interfaccia, dopo lo scambio: resta muto per qualche blocco, poi rialza. */
    void fadeIn()
    {
        silentBlocks = chainSilentBlocks;
        wantOut = false;
    }

    bool isSilent() const   { return silent.load(); }

    void prepareToPlay (double sr, int) override
    {
        outStep = (float) (1.0 / jmax (1.0, sr * chainFadeOutMs / 1000.0));
        inStep  = (float) (1.0 / jmax (1.0, sr * chainFadeInMs  / 1000.0));
    }

    void processBlock (AudioBuffer<float>& buffer, MidiBuffer&) override
    {
        const int n = buffer.getNumSamples();
        float target = 1.0f;

        if (wantOut.load())
            target = 0.0f;
        else if (silentBlocks.load() > 0)
        {
            --silentBlocks;
            target = 0.0f;
        }

        const float start = gain;

        if (target < gain)  gain = jmax (0.0f, gain - outStep * (float) n);
        else                gain = jmin (1.0f, gain + inStep  * (float) n);

        if (exactlyEqual (start, 1.0f) && exactlyEqual (gain, 1.0f))
            return;

        if (exactlyEqual (start, 0.0f) && exactlyEqual (gain, 0.0f))
            buffer.clear();
        else
            buffer.applyGainRamp (0, n, start, gain);

        silent = wantOut.load() && exactlyEqual (gain, 0.0f);
    }

    const String getName() const override                        { return "Chain fader"; }
    void releaseResources() override                             {}
    double getTailLengthSeconds() const override                 { return 0.0; }
    bool acceptsMidi() const override                            { return false; }
    bool producesMidi() const override                           { return false; }
    AudioProcessorEditor* createEditor() override                { return nullptr; }
    bool hasEditor() const override                              { return false; }
    int getNumPrograms() override                                { return 1; }
    int getCurrentProgram() override                             { return 0; }
    void setCurrentProgram (int) override                        {}
    const String getProgramName (int) override                   { return {}; }
    void changeProgramName (int, const String&) override         {}
    void getStateInformation (MemoryBlock&) override             {}
    void setStateInformation (const void*, int) override         {}

private:
    std::atomic<bool> wantOut { false }, silent { false };
    std::atomic<int> silentBlocks { 0 };
    float gain = 1.0f, outStep = 0.01f, inStep = 0.001f;   // solo thread audio (dopo prepareToPlay)
};

Engine::Engine()
{
    PropertiesFile::Options options;
    options.applicationName     = "ChainRig";
    options.filenameSuffix      = ".settings";
    options.folderName          = "ChainRig";
    options.osxLibrarySubFolder = "Application Support";
    appProperties.setStorageParameters (options);

    addDefaultFormatsToManager (formatManager);
    formatManager.addFormat (std::make_unique<BuiltInPluginFormat>());   // posti integrati (NAM, pedali)

    if (auto xml = props().getXmlValue ("pluginList"))
        knownPlugins.recreateFromXml (*xml);

    knownPlugins.addChangeListener (this);

    // Ogni plugin viene esaminato in un processo separato: se si blocca o va in crash
    // finisce tra gli esclusi e ChainRig continua
    knownPlugins.setCustomScanner (std::make_unique<PluginScanner::OutOfProcessScanner>());

    inputChannel = props().getIntValue ("inputChannel", 0);

    for (int i = 0; i < numButtons; ++i)
        buttons[(size_t) i].label = defaultButtonLabels[i];

    // Grafo fisso: ingresso -> accordatore -> [posti] -> uscita
    using IO = AudioProcessorGraph::AudioGraphIOProcessor;
    inputNode  = graph.addNode (std::make_unique<IO> (IO::audioInputNode))->nodeID;
    outputNode = graph.addNode (std::make_unique<IO> (IO::audioOutputNode))->nodeID;

    auto tunerProcessor = std::make_unique<TunerProcessor>();
    tuner = tunerProcessor.get();
    tunerNode = graph.addNode (std::move (tunerProcessor))->nodeID;

    auto fader = std::make_unique<ChainFader>();
    chainFader = fader.get();
    chainFaderNode = graph.addNode (std::move (fader))->nodeID;

    graph.setPlayHead (&playHead);   // i nodi ricevono questo tempo (vedi TempoPlayHead)
    player.setProcessor (&graph);
    tuner->inputGainDb = (float) props().getDoubleValue ("inputGainDb", 0.0);
    setOutputGainDb ((float) props().getDoubleValue ("outputGainDb", 0.0));

    // Scheda audio: ripristina l'ultima configurazione, altrimenti preferisci ASIO
    auto savedDevice = props().getXmlValue ("audioDevice");
    deviceManager.initialise (2, 2, savedDevice.get(), true);

    if (savedDevice == nullptr)
    {
        for (auto* type : deviceManager.getAvailableDeviceTypes())
        {
            if (type->getTypeName() == "ASIO")
            {
                type->scanForDevices();
                if (! type->getDeviceNames (true).isEmpty())
                    deviceManager.setCurrentAudioDeviceType ("ASIO", true);
            }
        }
    }

    deviceManager.addAudioCallback (&recorder);
    lastAudioSetupKey = getAudioSetupKey();
    deviceManager.addChangeListener (this);

    enableAllMidiInputs();
    deviceManager.addMidiInputDeviceCallback ({}, this);

    rewire();

    if (! restoreFromRecovery())
    {
        const File lastPreset (props().getValue ("lastPreset"));
        if (lastPreset.existsAsFile())
        {
            String errors;
            loadPreset (lastPreset, errors);
        }
    }

    // Scaletta della sessione precedente, sulla stessa canzone se il preset è ancora quello
    if (const File lastSetlist (props().getValue ("setlist")); lastSetlist.existsAsFile())
    {
        currentSetlistFile = lastSetlist;
        setlist = readSetlist (lastSetlist);
        setlistIndex = props().getIntValue ("setlistIndex", -1);
        syncSetlistIndex();
    }

    recorder.setInputChannel (inputChannel);
    updateDeviceInfo();
    startTimer (500);    // diagnostica audio; ogni 2 s controlla nuove periferiche MIDI (es. pedaliera Bluetooth)
}

Engine::~Engine()
{
    stopTimer();
    saveSettings();
    getRecoveryFile().deleteFile();   // chiusura normale: niente da ripristinare

    deviceManager.removeMidiInputDeviceCallback ({}, this);
    recorder.stop();
    deviceManager.removeAudioCallback (&recorder);
    deviceManager.removeChangeListener (this);
    knownPlugins.removeChangeListener (this);

    player.setProcessor (nullptr);
    graph.clear();
    deviceManager.closeAudioDevice();
}

PropertiesFile& Engine::props()
{
    return *appProperties.getUserSettings();
}

File Engine::getDeadMansPedalFile() const
{
    return File::getSpecialLocation (File::userApplicationDataDirectory)
               .getChildFile ("ChainRig").getChildFile ("ScanCrashList.txt");
}

File Engine::getPresetFolder() const
{
    auto folder = File::getSpecialLocation (File::userDocumentsDirectory)
                      .getChildFile ("ChainRig").getChildFile ("Preset");
    folder.createDirectory();
    return folder;
}

void Engine::saveSettings()
{
    if (auto xml = deviceManager.createStateXml())
        props().setValue ("audioDevice", xml.get());

    if (auto xml = knownPlugins.createXml())
        props().setValue ("pluginList", xml.get());

    props().setValue ("inputChannel", inputChannel);
    props().setValue ("lastPreset", currentPresetFile.getFullPathName());
    props().saveIfNeeded();
}

//==============================================================================
void Engine::changeListenerCallback (ChangeBroadcaster* source)
{
    if (source == &deviceManager)
    {
        // Lo stesso messaggio arriva anche quando si collega o scollega una periferica MIDI:
        // in quel caso la scheda audio è la stessa e non serve ricostruire né fermare REC
        if (const auto key = getAudioSetupKey(); key == lastAudioSetupKey)
        {
            enableAllMidiInputs();
            sendChangeMessage();
            return;
        }
        else
        {
            lastAudioSetupKey = key;
        }

        // La scheda audio è cambiata: i canali di ingresso/uscita del grafo potrebbero essere diversi
        rewire();

        // Frequenza o buffer possono essere cambiati: chiudi il file per non mescolare formati
        recorder.stop();
        updateDeviceInfo();

        if (auto xml = deviceManager.createStateXml())
            props().setValue ("audioDevice", xml.get());
        props().saveIfNeeded();
    }
    else if (source == &knownPlugins)
    {
        if (auto xml = knownPlugins.createXml())
            props().setValue ("pluginList", xml.get());
        props().saveIfNeeded();
    }

    sendChangeMessage();
}

//==============================================================================
std::unique_ptr<AudioPluginInstance> Engine::createInstance (const PluginDescription& desc, String& error,
                                                            PluginDescription& usedDescription)
{
    auto* device = deviceManager.getCurrentAudioDevice();
    const double sampleRate = device != nullptr ? device->getCurrentSampleRate() : 48000.0;
    const int blockSize     = device != nullptr ? device->getCurrentBufferSizeSamples() : 256;

    // Se il plugin è nella lista scansionata usa quella descrizione (percorso aggiornato)
    PluginDescription toLoad = desc;
    if (auto known = knownPlugins.getTypeForIdentifierString (desc.createIdentifierString()))
        toLoad = *known;

    usedDescription = toLoad;
    auto instance = formatManager.createPluginInstance (toLoad, sampleRate, blockSize, error);

    if (instance != nullptr)
        configureBusLayout (*instance);

    return instance;
}

void Engine::configureBusLayout (AudioPluginInstance& p)
{
    p.disableNonMainBuses();

    using CS = AudioChannelSet;
    const std::pair<CS, CS> candidates[] = { { CS::stereo(), CS::stereo() },
                                             { CS::mono(),   CS::stereo() },
                                             { CS::mono(),   CS::mono()   } };

    for (const auto& [in, out] : candidates)
    {
        auto layout = p.getBusesLayout();

        if (layout.outputBuses.isEmpty())
            return;

        if (! layout.inputBuses.isEmpty())
            layout.inputBuses.getReference (0) = in;

        layout.outputBuses.getReference (0) = out;

        if (p.setBusesLayout (layout))
            return;
    }
    // Nessuna delle configurazioni è accettata: resta quella predefinita del plugin.
}

bool Engine::loadPluginIntoSlot (int slot, const PluginDescription& desc, String& error)
{
    if (! isPositiveAndBelow (slot, numSlots))
        return false;

    PluginDescription used;
    auto instance = createInstance (desc, error, used);
    if (instance == nullptr)
        return false;

    installInSlot (slot, std::move (instance), used, UpdateKind::none);
    rewire();
    markDirty();
    sendChangeMessage();
    return true;
}

// Nel registro: utile per capire i problemi di canali (mono/stereo)
static void logPluginChannels (const String& name, const AudioProcessor& p)
{
    PluginScanner::log ("PLUGIN " + name + ": " + String (p.getTotalNumInputChannels()) + " in, "
                        + String (p.getTotalNumOutputChannels()) + " out");
}

void Engine::installInSlot (int slot, std::unique_ptr<AudioPluginInstance> instance,
                            const PluginDescription& description, UpdateKind kind)
{
    removeSlotNode (slot, kind);

    if (auto node = graph.addNode (std::move (instance), std::nullopt, kind))
    {
        slots[(size_t) slot].node = node->nodeID;
        slots[(size_t) slot].description = description;

        logPluginChannels (description.name, *node->getProcessor());
        slots[(size_t) slot].userBypassed = false;
        listenTo (node->nodeID);
        slots[(size_t) slot].customName.clear();
        slots[(size_t) slot].kind = SlotKind::automatic;
    }
}

void Engine::removeSlotNode (int slot, UpdateKind kind)
{
    auto& s = slots[(size_t) slot];

    if (! s.node.has_value())
        return;

    if (auto* node = graph.getNodeForId (*s.node))
    {
        node->getProcessor()->removeListener (this);

        if (onProcessorAboutToBeDeleted != nullptr)
            onProcessorAboutToBeDeleted (node->getProcessor());
    }

    // Il grafo tiene vivo il nodo finché il thread audio lo sta ancora usando
    graph.removeNode (*s.node, kind);
    s.node.reset();
    s.userBypassed = false;
    s.customName.clear();
    s.kind = SlotKind::automatic;
}

void Engine::clearSlot (int slot)
{
    if (! isPositiveAndBelow (slot, numSlots))
        return;

    removeSlotNode (slot, UpdateKind::none);
    rewire();
    markDirty();
    sendChangeMessage();
}

void Engine::swapSlots (int a, int b)
{
    if (! isPositiveAndBelow (a, numSlots) || ! isPositiveAndBelow (b, numSlots) || a == b)
        return;

    std::swap (slots[(size_t) a], slots[(size_t) b]);

    // Anche le assegnazioni dei pulsanti seguono il plugin
    for (auto& button : buttons)
    {
        for (auto* list : { &button.slots, &button.sceneOn })
        {
            const bool hasA = list->contains (a);
            const bool hasB = list->contains (b);
            list->removeAllInstancesOf (a);
            list->removeAllInstancesOf (b);
            if (hasA) list->add (b);
            if (hasB) list->add (a);
        }
    }

    rewire();
    markDirty();
    sendChangeMessage();
}

String Engine::getButtonDisplayName (int index) const
{
    if (! isPositiveAndBelow (index, numButtons))
        return {};

    const auto& button = buttons[(size_t) index];

    if (button.mode == ButtonMode::scene)
        return button.label;

    auto sorted = button.slots;
    sorted.sort();

    StringArray names;
    for (auto s : sorted)
        if (! isSlotEmpty (s))
            names.add (getSlotName (s));

    if (names.isEmpty())   return button.label;
    if (names.size() <= 2) return names.joinIntoString (" + ");
    return names[0] + " + " + String (names.size() - 1);
}

void Engine::moveSlot (int from, int to)
{
    if (! isPositiveAndBelow (from, numSlots) || ! isPositiveAndBelow (to, numSlots) || from == to || isSlotEmpty (from))
        return;

    // order[nuovo indice] = vecchio indice
    std::vector<int> order;
    for (int i = 0; i < numSlots; ++i)
        order.push_back (i);

    if (isSlotEmpty (to))
    {
        std::swap (order[(size_t) from], order[(size_t) to]);
    }
    else
    {
        order.erase (order.begin() + from);
        order.insert (order.begin() + to, from);
    }

    std::array<Slot, numSlots> moved;
    std::array<int, numSlots> oldToNew {};

    for (int i = 0; i < numSlots; ++i)
    {
        moved[(size_t) i] = slots[(size_t) order[(size_t) i]];
        oldToNew[(size_t) order[(size_t) i]] = i;
    }

    slots = moved;

    for (auto& button : buttons)
        for (auto* list : { &button.slots, &button.sceneOn })
            for (auto& s : *list)
                s = oldToNew[(size_t) s];

    rewire();
    markDirty();
    sendChangeMessage();
}

//==============================================================================
void Engine::connectNodes (NodeID from, NodeID to, int maxDestChannels)
{
    auto* src = graph.getNodeForId (from);
    auto* dst = graph.getNodeForId (to);

    if (src == nullptr || dst == nullptr)
        return;

    const int outs = src->getProcessor()->getTotalNumOutputChannels();
    const int ins  = jmin (dst->getProcessor()->getTotalNumInputChannels(), maxDestChannels);

    if (outs <= 0 || ins <= 0)
        return;

    // mono -> stereo: duplica il canale; stereo -> mono: usa il sinistro
    for (int c = 0; c < ins; ++c)
        graph.addConnection ({ { from, jmin (c, outs - 1) }, { to, c } }, UpdateKind::none);
}

String Engine::getAudioSetupKey() const
{
    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
        return {};

    const auto setup = deviceManager.getAudioDeviceSetup();
    return device->getTypeName() + "|" + device->getName() + "|" + String (setup.sampleRate) + "|" + String (setup.bufferSize)
         + "|" + setup.inputChannels.toString (2) + "|" + setup.outputChannels.toString (2)
         + "|" + String (device->getActiveInputChannels().countNumberOfSetBits())
         + "|" + String (device->getActiveOutputChannels().countNumberOfSetBits());
}

bool Engine::fadeOutChain()
{
    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr || ! device->isPlaying() || chainFader == nullptr)
        return false;

    const double blockMs = 1000.0 * device->getCurrentBufferSizeSamples() / jmax (1.0, device->getCurrentSampleRate());
    const auto deadline = Time::getMillisecondCounterHiRes() + chainFadeOutMs + 4.0 * blockMs + 20.0;

    chainFader->fadeOut();

    // pochi millisecondi: il thread audio abbassa la catena vecchia mentre questo aspetta
    while (! chainFader->isSilent())
    {
        if (Time::getMillisecondCounterHiRes() > deadline)
            return true;   // la scheda non chiama più il callback: si va avanti comunque

        Thread::sleep (1);
    }

    return true;
}

void Engine::rewire()
{
    Array<NodeID> chain;
    for (const auto& s : slots)
        if (s.node.has_value())
            chain.add (*s.node);

    // cambia l'ordine o l'insieme dei plugin: abbassa prima dello scambio, così non scatta
    const auto startMs = Time::getMillisecondCounterHiRes();
    const bool faded = chain != lastChain && fadeOutChain();
    const auto silentAtMs = Time::getMillisecondCounterHiRes();
    lastChain = chain;

    for (const auto& c : graph.getConnections())
        graph.removeConnection (c, UpdateKind::none);

    for (auto id : monoFixNodes)
        graph.removeNode (id, UpdateKind::none);

    monoFixNodes.clear();
    monoFix.fill (nullptr);

    // Ingresso della chitarra (mono) -> entrambi i canali dell'accordatore
    if (auto* in = graph.getNodeForId (inputNode))
    {
        const int numIns = in->getProcessor()->getTotalNumOutputChannels();

        if (numIns > 0)
        {
            const int ch = jlimit (0, numIns - 1, inputChannel);
            graph.addConnection ({ { inputNode, ch }, { tunerNode, 0 } }, UpdateKind::none);
            graph.addConnection ({ { inputNode, ch }, { tunerNode, 1 } }, UpdateKind::none);
        }
    }

    NodeID previous = tunerNode;

    for (int i = 0; i < numSlots; ++i)
    {
        const auto& s = slots[(size_t) i];

        if (! s.node.has_value())
            continue;

        connectNodes (previous, *s.node, 2);
        previous = *s.node;

        // mono in, stereo out: in bypass il destro resterebbe muto (vedi MonoBypassFix)
        if (auto* node = graph.getNodeForId (*s.node))
        {
            auto* p = node->getProcessor();

            if (p->getTotalNumInputChannels() == 1 && p->getTotalNumOutputChannels() >= 2)
            {
                auto fix = std::make_unique<MonoBypassFix>();
                monoFix[(size_t) i] = fix.get();
                const auto fixId = graph.addNode (std::move (fix), {}, UpdateKind::none)->nodeID;
                monoFixNodes.add (fixId);
                connectNodes (previous, fixId, 2);
                previous = fixId;
            }
        }
    }

    connectNodes (previous, chainFaderNode, 2);
    connectNodes (chainFaderNode, outputNode, 2);   // solo le uscite 1-2 della scheda

    // code del preset precedente: plugin (senza ingresso) -> sfumatura -> uscita, sommate alla catena
    for (const auto& tail : tails)
    {
        connectNodes (tail.plugin, tail.fader, 2);
        connectNodes (tail.fader, outputNode, 2);
    }

    updateBypassStates();
    graph.rebuild();   // prepara i plugin nuovi (prepareToPlay) e consegna la catena al thread audio

    if (faded)
    {
        chainFader->fadeIn();
        const auto endMs = Time::getMillisecondCounterHiRes();
        PluginScanner::log ("FADE fade-out " + String (silentAtMs - startMs, 1) + " ms, rebuild "
                            + String (endMs - silentAtMs, 1) + " ms");
    }
}

void Engine::updateBypassStates()
{
    for (int i = 0; i < numSlots; ++i)
        if (const auto& s = slots[(size_t) i]; s.node.has_value())
            if (auto* node = graph.getNodeForId (*s.node))
            {
                const bool shouldBypass = ! isSlotActive (i);
                if (node->userRequestedBypass() != shouldBypass)
                    node->setBypassed (shouldBypass);

                if (auto* fix = monoFix[(size_t) i])
                    fix->slotBypassed = shouldBypass;
            }
}

//==============================================================================
bool Engine::isSlotEmpty (int slot) const
{
    return ! isPositiveAndBelow (slot, numSlots) || ! slots[(size_t) slot].node.has_value();
}

AudioProcessor* Engine::getSlotProcessor (int slot) const
{
    if (isSlotEmpty (slot))
        return nullptr;

    if (auto* node = graph.getNodeForId (*slots[(size_t) slot].node))
        return node->getProcessor();

    return nullptr;
}

String Engine::getSlotName (int slot) const
{
    if (isSlotEmpty (slot))
        return {};

    const auto& custom = slots[(size_t) slot].customName;
    return custom.isNotEmpty() ? custom : getSlotPluginName (slot);
}

String Engine::getSlotPluginName (int slot) const
{
    if (auto* p = getSlotProcessor (slot))
        return p->getName();
    return {};
}

String Engine::getSlotManufacturer (int slot) const
{
    return isSlotEmpty (slot) ? String() : slots[(size_t) slot].description.manufacturerName;
}

String Engine::getSlotCustomName (int slot) const
{
    return isPositiveAndBelow (slot, numSlots) ? slots[(size_t) slot].customName : String();
}

void Engine::setSlotCustomName (int slot, const String& name)
{
    if (isSlotEmpty (slot))
        return;

    slots[(size_t) slot].customName = name.trim();
    markDirty();
    sendChangeMessage();
}

Engine::SlotKind Engine::getSlotKindSetting (int slot) const
{
    return isPositiveAndBelow (slot, numSlots) ? slots[(size_t) slot].kind : SlotKind::automatic;
}

Engine::SlotKind Engine::getSlotKind (int slot) const
{
    const auto kind = getSlotKindSetting (slot);
    return kind == SlotKind::automatic ? guessSlotKind (slot) : kind;
}

void Engine::setSlotKind (int slot, SlotKind kind)
{
    if (isSlotEmpty (slot))
        return;

    slots[(size_t) slot].kind = kind;
    markDirty();
    sendChangeMessage();
}

Engine::SlotKind Engine::guessSlotKind (int slot) const
{
    if (isSlotEmpty (slot))
        return SlotKind::fx;

    const auto& d = slots[(size_t) slot].description;
    const auto text = (d.category + " " + d.name + " " + d.descriptiveName).toLowerCase();

    const auto has = [&text] (std::initializer_list<const char*> words)
    {
        for (auto* w : words)
            if (text.contains (w))
                return true;
        return false;
    };

    // L'ordine conta: prima i nomi più specifici
    if (has ({ "neural amp", " nam", "amp sim", "amplifier" }))  return SlotKind::amp;
    if (has ({ "impulse", "cab", " ir ", "ir loader", "speaker" }))         return SlotKind::ir;
    if (has ({ "reverb", "verb", "plate", "room", "hall", "shimmer" }))
        return SlotKind::reverb;
    if (d.category.equalsIgnoreCase ("tape") || has ({ "tape machine", "tape sat" })) return SlotKind::tape;
    if (has ({ "delay", "echo" }))                                          return SlotKind::delay;
    if (has ({ "dynamics", "compress", "limit" }))                          return SlotKind::compressor;
    if (has ({ "eq", "equal", "filter" }))                                  return SlotKind::eq;
    if (has ({ "distort", "drive", "fuzz", "saturat", "overdrive" }))       return SlotKind::drive;
    if (has ({ "modulat", "chorus", "flang", "phaser", "tremolo", "vibrato" })) return SlotKind::modulation;
    if (has ({ "amp" }))                                                    return SlotKind::amp;
    return SlotKind::fx;
}

String Engine::getSlotKindTag (SlotKind k)
{
    switch (k)
    {
        case SlotKind::amp:        return "AMP";
        case SlotKind::ir:         return String::fromUTF8 ("CAB · IR");
        case SlotKind::drive:      return "DRIVE";
        case SlotKind::compressor: return "COMP";
        case SlotKind::eq:         return "EQ";
        case SlotKind::modulation: return "MOD";
        case SlotKind::delay:      return "DELAY";
        case SlotKind::reverb:     return "REVERB";
        case SlotKind::tape:       return "TAPE";
        case SlotKind::fx:
        case SlotKind::automatic:  break;
    }
    return "FX";
}

String Engine::getSlotKindName (SlotKind k)
{
    switch (k)
    {
        case SlotKind::automatic:  return "Automatic";
        case SlotKind::amp:        return "Amp";
        case SlotKind::ir:         return "Cab / IR";
        case SlotKind::drive:      return "Drive / distortion";
        case SlotKind::compressor: return "Compressor";
        case SlotKind::eq:         return "EQ";
        case SlotKind::modulation: return "Modulation";
        case SlotKind::delay:      return "Delay";
        case SlotKind::reverb:     return "Reverb";
        case SlotKind::tape:       return "Tape";
        case SlotKind::fx:         break;
    }
    return "Other effect";
}

String Engine::getSlotKindId (SlotKind k)
{
    switch (k)
    {
        case SlotKind::amp:        return "amp";
        case SlotKind::ir:         return "ir";
        case SlotKind::drive:      return "drive";
        case SlotKind::compressor: return "compressor";
        case SlotKind::eq:         return "eq";
        case SlotKind::modulation: return "modulation";
        case SlotKind::delay:      return "delay";
        case SlotKind::reverb:     return "reverb";
        case SlotKind::tape:       return "tape";
        case SlotKind::fx:         return "fx";
        case SlotKind::automatic:  break;
    }
    return "auto";
}

Engine::SlotKind Engine::getSlotKindFromId (const String& id)
{
    for (auto k : allSlotKinds)
        if (getSlotKindId (k) == id)
            return k;

    return SlotKind::automatic;
}

bool Engine::getSlotUserBypass (int slot) const
{
    return ! isSlotEmpty (slot) && slots[(size_t) slot].userBypassed;
}

void Engine::setSlotUserBypass (int slot, bool bypassed)
{
    if (isSlotEmpty (slot))
        return;

    slots[(size_t) slot].userBypassed = bypassed;
    updateBypassStates();
    markDirty();
    sendChangeMessage();
}

bool Engine::isSlotControlledByButton (int slot) const
{
    for (const auto& b : buttons)
        if (b.mode == ButtonMode::toggle && b.slots.contains (slot))
            return true;
    return false;
}

bool Engine::isSlotActive (int slot) const
{
    if (isSlotEmpty (slot) || slots[(size_t) slot].userBypassed)
        return false;

    if (! isSlotControlledByButton (slot))
        return true;

    // Controllato da uno o più pulsanti: suona se almeno uno è acceso
    for (const auto& b : buttons)
        if (b.mode == ButtonMode::toggle && b.active && b.slots.contains (slot))
            return true;

    return false;
}

//==============================================================================
const Engine::TouchButton& Engine::getButton (int index) const
{
    return buttons[(size_t) jlimit (0, numButtons - 1, index)];
}

void Engine::toggleButton (int index)
{
    if (! isPositiveAndBelow (index, numButtons))
        return;

    if (buttons[(size_t) index].mode == ButtonMode::scene)
        recallScene (index);   // una scena si richiama sempre, non si "spegne"
    else
        setButtonActive (index, ! buttons[(size_t) index].active);
}

void Engine::setButtonActive (int index, bool active)
{
    if (! isPositiveAndBelow (index, numButtons))
        return;

    if (buttons[(size_t) index].mode == ButtonMode::scene)
    {
        if (active)
            recallScene (index);
        return;
    }

    buttons[(size_t) index].active = active;
    updateBypassStates();
    recoveryPending = true;
    sendChangeMessage();
}

void Engine::setButtonLabel (int index, const String& label)
{
    if (! isPositiveAndBelow (index, numButtons))
        return;

    buttons[(size_t) index].label = label.trim().toUpperCase();
    markDirty();
    sendChangeMessage();
}

void Engine::setButtonMode (int index, ButtonMode mode)
{
    if (! isPositiveAndBelow (index, numButtons) || buttons[(size_t) index].mode == mode)
        return;

    auto& b = buttons[(size_t) index];
    b.mode = mode;
    b.active = false;

    if (mode == ButtonMode::scene)
    {
        b.slots.clear();   // una scena non "possiede" posti: li accende o spegne tutti
        if (b.label == defaultButtonLabels[index])
            b.label = "SCENE " + String::charToString ((juce_wchar) ('A' + index));
        storeScene (index);   // parte dallo stato attuale, così non è vuota
    }
    else if (b.label.startsWith ("SCENE "))
    {
        b.label = defaultButtonLabels[index];
    }

    updateBypassStates();
    markDirty();
    sendChangeMessage();
}

void Engine::storeScene (int index)
{
    if (! isPositiveAndBelow (index, numButtons))
        return;

    auto& b = buttons[(size_t) index];
    b.sceneOn.clear();

    for (int s = 0; s < numSlots; ++s)
        if (! isSlotEmpty (s) && ! slots[(size_t) s].userBypassed)
            b.sceneOn.add (s);

    // salvare lo stato attuale lo rende la scena attiva
    for (auto& other : buttons)
        if (other.mode == ButtonMode::scene)
            other.active = (&other == &b);

    markDirty();
    sendChangeMessage();
}

void Engine::recallScene (int index)
{
    if (! isPositiveAndBelow (index, numButtons) || buttons[(size_t) index].mode != ButtonMode::scene)
        return;

    const auto& scene = buttons[(size_t) index].sceneOn;

    for (int s = 0; s < numSlots; ++s)
        if (! isSlotEmpty (s))
            slots[(size_t) s].userBypassed = ! scene.contains (s);

    for (int i = 0; i < numButtons; ++i)
        if (buttons[(size_t) i].mode == ButtonMode::scene)
            buttons[(size_t) i].active = (i == index);

    updateBypassStates();
    recoveryPending = true;   // come i pedali: è uso dal vivo, non una modifica del preset
    sendChangeMessage();
}

void Engine::setButtonControlsSlot (int index, int slot, bool controls)
{
    if (! isPositiveAndBelow (index, numButtons) || ! isPositiveAndBelow (slot, numSlots))
        return;

    auto& b = buttons[(size_t) index];
    b.slots.removeAllInstancesOf (slot);
    if (controls)
        b.slots.add (slot);

    updateBypassStates();
    markDirty();
    sendChangeMessage();
}

//==============================================================================
// MIDI: numeri CC modificabili nel file di impostazioni (ChainRig.settings)
int Engine::getButtonCC (int index) const   { return appProperties.getUserSettings()->getIntValue ("cc_button" + String (index + 1), 80 + index); }
int Engine::getTapTempoCC() const           { return appProperties.getUserSettings()->getIntValue ("cc_tapTempo", 87); }
int Engine::getTunerCC() const              { return appProperties.getUserSettings()->getIntValue ("cc_tuner", 84); }
int Engine::getNextPresetCC() const         { return appProperties.getUserSettings()->getIntValue ("cc_nextPreset", 85); }
int Engine::getPrevPresetCC() const         { return appProperties.getUserSettings()->getIntValue ("cc_prevPreset", 86); }

Engine::MidiMode Engine::getMidiMode() const
{
    return appProperties.getUserSettings()->getValue ("midiMode", "momentary") == "absolute" ? MidiMode::absolute
                                                                                             : MidiMode::momentary;
}

void Engine::setMidiMode (MidiMode mode)
{
    props().setValue ("midiMode", mode == MidiMode::absolute ? "absolute" : "momentary");
    props().saveIfNeeded();
    sendChangeMessage();
}

void Engine::enableAllMidiInputs()
{
    knownMidiInputs.clear();

    for (const auto& device : MidiInput::getAvailableDevices())
    {
        knownMidiInputs.add (device.identifier);
        if (! deviceManager.isMidiInputDeviceEnabled (device.identifier))
            deviceManager.setMidiInputDeviceEnabled (device.identifier, true);
    }
}

void Engine::timerCallback()
{
    checkAudioHealth();

    // un plugin ha cambiato parametri o stato (manopole nella sua finestra)
    if (pluginStateChanged.exchange (false) && Time::getMillisecondCounter() > ignorePluginChangesUntil)
        markDirty();

    writeRecoveryIfNeeded();
    removeExpiredTails();

    if (++timerTicks % 4 != 0)
        return;

    StringArray current;
    for (const auto& device : MidiInput::getAvailableDevices())
        current.add (device.identifier);

    if (current != knownMidiInputs)
        enableAllMidiInputs();
}

void Engine::handleIncomingMidiMessage (MidiInput*, const MidiMessage& message)
{
    // Thread MIDI: non toccare nulla qui, passa al thread dell'interfaccia
    WeakReference<Engine> weakThis (this);

    if (message.isController())
    {
        const int cc = message.getControllerNumber();
        const int value = message.getControllerValue();

        MessageManager::callAsync ([weakThis, cc, value]
        {
            if (auto* self = weakThis.get())
                self->handleControllerOnMessageThread (cc, value);
        });
    }
    else if (message.isProgramChange())
    {
        const int program = message.getProgramChangeNumber();

        MessageManager::callAsync ([weakThis, program]
        {
            if (auto* self = weakThis.get())
            {
                if (self->isSetlistActive())   // in scaletta: Program Change = numero della canzone
                {
                    String errors;
                    if (isPositiveAndBelow (program, self->setlist.size()))
                        self->loadSetlistEntry (program, errors);
                    return;
                }

                const auto files = self->getPresetFiles();
                if (isPositiveAndBelow (program, files.size()))
                {
                    String errors;
                    self->loadPreset (files[program], errors);
                }
            }
        });
    }
}

void Engine::handleControllerOnMessageThread (int cc, int value)
{
    const bool pressed = value >= 64;
    const bool momentary = getMidiMode() == MidiMode::momentary;

    // momentary: ogni pressione (valore >= 64) alterna lo stato, il rilascio si ignora
    // absolute: il valore stesso decide acceso (>= 64) o spento (< 64)
    auto apply = [&] (bool current, auto&& setter)
    {
        if (momentary) { if (pressed) setter (! current); }
        else           { setter (pressed); }
    };

    for (int i = 0; i < numButtons; ++i)
        if (cc == getButtonCC (i))
            apply (buttons[(size_t) i].active, [this, i] (bool on) { setButtonActive (i, on); });

    if (cc == getTunerCC())
        apply (tunerActive, [this] (bool on) { setTunerActive (on); });

    if (pressed && cc == getTapTempoCC())
        tapTempo();

    if (pressed && cc == getNextPresetCC()) loadAdjacentPreset (1);
    if (pressed && cc == getPrevPresetCC()) loadAdjacentPreset (-1);
}

//==============================================================================
void Engine::setTunerActive (bool active)
{
    tunerActive = active;
    tuner->capturing = active;
    tuner->muted = active;     // uscita muta mentre accordi
    sendChangeMessage();
}

//==============================================================================
void Engine::setInputChannel (int channelIndex)
{
    inputChannel = jmax (0, channelIndex);
    recorder.setInputChannel (inputChannel);
    props().setValue ("inputChannel", inputChannel);
    rewire();
    sendChangeMessage();
}

bool Engine::isSpilloverEnabled() const   { return appProperties.getUserSettings()->getBoolValue ("spillover", true); }

void Engine::setSpilloverEnabled (bool enabled)
{
    props().setValue ("spillover", enabled);
    props().saveIfNeeded();
    sendChangeMessage();
}

//==============================================================================
void Engine::keepAsTail (int slot)
{
    auto& s = slots[(size_t) slot];

    if (! s.node.has_value())
        return;

    while (tails.size() >= maxTails)
        removeTail (0, UpdateKind::none);

    if (auto* node = graph.getNodeForId (*s.node))
    {
        node->getProcessor()->removeListener (this);   // non è più parte del preset

        if (onProcessorAboutToBeDeleted != nullptr)    // chiude la sua finestra, se aperta
            onProcessorAboutToBeDeleted (node->getProcessor());
    }

    Tail tail;
    tail.plugin = *s.node;
    tail.fader = graph.addNode (std::make_unique<TailFader>(), std::nullopt, UpdateKind::none)->nodeID;
    tail.removeAtMs = Time::getMillisecondCounter() + (uint32) ((tailHoldSeconds + tailFadeSeconds) * 1000.0) + 500;
    tails.add (tail);

    s.node.reset();
    s.userBypassed = false;
    s.customName.clear();
    s.kind = SlotKind::automatic;
}

void Engine::removeTail (int index, UpdateKind kind)
{
    const auto tail = tails[index];
    tails.remove (index);

    if (auto* node = graph.getNodeForId (tail.plugin); node != nullptr && onProcessorAboutToBeDeleted != nullptr)
        onProcessorAboutToBeDeleted (node->getProcessor());

    graph.removeNode (tail.fader, UpdateKind::none);
    graph.removeNode (tail.plugin, kind);
}

void Engine::removeExpiredTails()
{
    const auto now = Time::getMillisecondCounter();
    bool removed = false;

    for (int i = tails.size(); --i >= 0;)
        if (now >= tails.getReference (i).removeAtMs)
        {
            removeTail (i, UpdateKind::none);
            removed = true;
        }

    if (removed)
    {
        graph.rebuild();
        PluginScanner::log ("TAILS faded out, " + String (tails.size()) + " still playing");
    }
}

void Engine::setOutputGainDb (float db)
{
    outputGainDb = jlimit (minOutputGainDb, maxOutputGainDb, db);
    recorder.setOutputGain (outputGainDb <= minOutputGainDb ? 0.0f : Decibels::decibelsToGain (outputGainDb));
    props().setValue ("outputGainDb", (double) outputGainDb);
}

void Engine::setInputGainDb (float db)
{
    tuner->inputGainDb = jlimit (-24.0f, 24.0f, db);
    props().setValue ("inputGainDb", (double) tuner->inputGainDb.load());
    sendChangeMessage();
}

void Engine::setInputMuted (bool shouldMute)
{
    tuner->inputMuted = shouldMute;
    sendChangeMessage();
}

int Engine::getNumDeviceInputs() const
{
    if (auto* node = graph.getNodeForId (inputNode))
        return node->getProcessor()->getTotalNumOutputChannels();
    return 0;
}

String Engine::getStatusText() const
{
    if (deviceName.isEmpty())
        return "No audio device";

    // due righe: scheda e formato; latenza, carico e buchi
    return deviceName + String::fromUTF8 ("  ·  ") + String (deviceSampleRate / 1000.0, 1)
         + String::fromUTF8 (" kHz  ·  buffer ") + String (deviceBufferSize)
         + "\nlatency " + String (deviceLatencyMs, 1) + String::fromUTF8 (" ms  ·  CPU ")
         + String (roundToInt (deviceManager.getCpuUsage() * 100.0)) + String::fromUTF8 ("%  ·  peak ")
         + String (roundToInt (recentPeakLoad * 100.0f)) + String::fromUTF8 ("%  ·  xrun ")
         + String (loggedXruns + loggedOverloads);
}

void Engine::updateDeviceInfo()
{
    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
    {
        deviceName.clear();
        return;
    }

    deviceName = device->getName();
    deviceSampleRate = device->getCurrentSampleRate();
    deviceBufferSize = device->getCurrentBufferSizeSamples();

    const int totalLatency = deviceBufferSize * 2 + device->getInputLatencyInSamples() + device->getOutputLatencyInSamples();
    deviceLatencyMs = deviceSampleRate > 0 ? 1000.0 * totalLatency / deviceSampleRate : 0.0;

    PluginScanner::log ("AUDIO  start: " + deviceName + ", " + String (deviceSampleRate, 0) + " Hz, buffer "
                        + String (deviceBufferSize) + ", latency " + String (deviceLatencyMs, 1) + " ms");
}

void Engine::checkAudioHealth()
{
    // carico massimo degli ultimi 2 s (4 letture)
    loadWindow.add (recorder.takeMaxLoad());
    while (loadWindow.size() > 4)
        loadWindow.remove (0);

    recentPeakLoad = 0.0f;
    for (auto l : loadWindow)
        recentPeakLoad = jmax (recentPeakLoad, l);

    // Livelli (solo con CHAINRIG_LEVEL_LOG=1): per capire da dove arriva un suono a riposo
    {
        const float in = recorder.takeInputPeak(), out = recorder.takeOutputPeak();
        static const bool levelLog = SystemStats::getEnvironmentVariable ("CHAINRIG_LEVEL_LOG", {}) == "1";
        if (levelLog)
            PluginScanner::log ("LEVELS  input " + String (Decibels::gainToDecibels (in, -120.0f), 1)
                                + " dB  output " + String (Decibels::gainToDecibels (out, -120.0f), 1) + " dB");
    }

    auto* device = deviceManager.getCurrentAudioDevice();
    const int xruns = device != nullptr ? jmax (0, device->getXRunCount()) : 0;
    const int overloads = recorder.getOverloads();
    const int late = recorder.getLateCallbacks();

    if (xruns != loggedXruns || overloads != loggedOverloads || late != loggedLate)
    {
        PluginScanner::log ("AUDIO  device xruns " + String (xruns) + " (+" + String (xruns - loggedXruns) + ")"
                            + ", chain over budget " + String (overloads) + " (+" + String (overloads - loggedOverloads) + ")"
                            + ", late blocks " + String (late) + " (+" + String (late - loggedLate) + ")"
                            + ", max load " + String (roundToInt (recentPeakLoad * 100.0f)) + "%"
                            + ", CPU " + String (roundToInt (deviceManager.getCpuUsage() * 100.0)) + "%");
        loggedXruns = xruns;
        loggedOverloads = overloads;
        loggedLate = late;
    }
}

//==============================================================================
// Modifiche non salvate e recupero
void Engine::audioProcessorParameterChanged (AudioProcessor*, int, float)
{
    pluginStateChanged = true;   // qualsiasi thread: solo un flag, lo legge il timer
}

void Engine::audioProcessorChanged (AudioProcessor*, const ChangeDetails& details)
{
    if (details.parameterInfoChanged || details.programChanged || details.nonParameterStateChanged)
        pluginStateChanged = true;
}

void Engine::listenTo (NodeID id)
{
    if (auto* node = graph.getNodeForId (id))
        node->getProcessor()->addListener (this);
}

void Engine::markDirty()
{
    recoveryPending = true;

    if (! dirty)
    {
        dirty = true;
        sendChangeMessage();
    }
}

void Engine::markClean()
{
    dirty = false;
    recoveryPending = true;
    pluginStateChanged = false;
    // dopo un caricamento alcuni plugin segnalano parametri cambiati: non sono modifiche dell'utente
    ignorePluginChangesUntil = Time::getMillisecondCounter() + 1500;
}

File Engine::getRecoveryFile() const
{
    return appProperties.getUserSettings()->getFile().getSiblingFile ("recovery.chainrig");
}

void Engine::writeRecoveryIfNeeded()
{
    const auto now = Time::getMillisecondCounter();

    if (! recoveryPending || now - lastRecoveryWrite < 10000)
        return;

    auto state = createPresetState();
    if (auto* obj = state.getDynamicObject())
    {
        obj->setProperty ("recoveryOf", currentPresetFile.getFullPathName());
        obj->setProperty ("dirty", dirty);
    }

    getRecoveryFile().replaceWithText (JSON::toString (state));
    recoveryPending = false;
    lastRecoveryWrite = now;
}

bool Engine::restoreFromRecovery()
{
    const auto recovery = getRecoveryFile();
    const auto loading = recovery.withFileExtension ("loading");

    // Il ripristino precedente ha fatto chiudere ChainRig (un plugin va in crash): non riprovare
    if (loading.existsAsFile())
    {
        loading.deleteFile();
        recovery.deleteFile();
        startupMessage = "Restoring the previous session made ChainRig close, so it was skipped this time. "
                         "A plugin in the chain probably crashes: check your last preset.";
        PluginScanner::log ("RECOVERY skipped: the previous attempt crashed");
        return false;
    }

    if (! recovery.existsAsFile())
        return false;

    // Se il ripristino si blocca, al prossimo avvio resta "loading" e lo saltiamo
    const auto savedAt = recovery.getLastModificationTime();
    recovery.moveFileTo (loading);

    const var root = JSON::parse (loading);
    if (! root.isObject())
    {
        loading.deleteFile();
        return false;
    }

    String errors;
    loadPresetState (root, errors, "recovery");
    loading.deleteFile();

    currentPresetFile = File (root["recoveryOf"].toString());
    markClean();
    dirty = (bool) root["dirty"];

    startupMessage = "ChainRig closed unexpectedly. The chain was restored as it was at "
                   + savedAt.formatted ("%H:%M:%S") + ".";
    if (dirty)
        startupMessage << " There were unsaved changes: they are back, remember to save.";
    if (errors.isNotEmpty())
        startupMessage << "\n\n" << errors;

    PluginScanner::log ("RECOVERY restored the session saved at " + savedAt.formatted ("%H:%M:%S"));
    return true;
}

//==============================================================================
// Preset: file JSON leggibili, uno per preset, nella cartella Documenti/ChainRig/Preset
Array<File> Engine::getPresetFiles() const
{
    auto files = getPresetFolder().findChildFiles (File::findFiles, false, String ("*") + presetExtension);
    files.sort();
    return files;
}

File Engine::getPresetFileByName (const String& name) const
{
    return getPresetFolder().getChildFile (File::createLegalFileName (name) + presetExtension);
}

//==============================================================================
File Engine::getSetlistFolder() const
{
    auto folder = File::getSpecialLocation (File::userDocumentsDirectory)
                      .getChildFile ("ChainRig").getChildFile ("Setlists");
    folder.createDirectory();
    return folder;
}

Array<File> Engine::getSetlistFiles() const
{
    auto files = getSetlistFolder().findChildFiles (File::findFiles, false, String ("*") + setlistExtension);
    files.sort();
    return files;
}

Array<Engine::SetlistEntry> Engine::readSetlist (const File& file)
{
    Array<SetlistEntry> entries;
    const var root = JSON::parse (file);

    if (auto* songs = root["songs"].getArray())
        for (const auto& s : *songs)
            entries.add ({ s["song"].toString(), s["preset"].toString() });

    return entries;
}

bool Engine::saveSetlist (const File& fileIn, const Array<SetlistEntry>& entries)
{
    const auto file = fileIn.withFileExtension (setlistExtension);

    Array<var> songs;
    for (const auto& e : entries)
    {
        DynamicObject::Ptr o = new DynamicObject();
        o->setProperty ("song", e.song);
        o->setProperty ("preset", e.preset);
        songs.add (var (o.get()));
    }

    DynamicObject::Ptr root = new DynamicObject();
    root->setProperty ("version", 1);
    root->setProperty ("songs", songs);

    if (! file.replaceWithText (JSON::toString (var (root.get()))))
        return false;

    // se è la scaletta in uso (o una nuova), diventa quella attiva restando sulla canzone attuale
    const auto previousSong = isPositiveAndBelow (setlistIndex, setlist.size()) ? setlist[setlistIndex] : SetlistEntry {};
    currentSetlistFile = file;
    setlist = entries;
    setlistIndex = -1;

    for (int i = 0; i < setlist.size(); ++i)
        if (setlist[i].song == previousSong.song && setlist[i].preset == previousSong.preset)
            setlistIndex = i;

    syncSetlistIndex();
    sendChangeMessage();
    return true;
}

bool Engine::openSetlist (const File& file)
{
    if (! file.existsAsFile())
        return false;

    currentSetlistFile = file;
    setlist = readSetlist (file);
    setlistIndex = -1;
    syncSetlistIndex();
    sendChangeMessage();
    return true;
}

void Engine::closeSetlist()
{
    currentSetlistFile = File();
    setlist.clear();
    setlistIndex = -1;
    saveSetlistPosition();
    sendChangeMessage();
}

bool Engine::loadSetlistEntry (int index, String& errors)
{
    if (! isPositiveAndBelow (index, setlist.size()))
        return false;

    const auto file = getPresetFileByName (setlist[index].preset);

    if (! file.existsAsFile())   // resta sulla canzone attuale
    {
        errors = "Preset not found: " + setlist[index].preset;
        return false;
    }

    setlistIndex = index;   // prima del caricamento: syncSetlistIndex la tiene se il preset è giusto
    return loadPreset (file, errors);
}

String Engine::getCurrentSongName() const
{
    if (! isPositiveAndBelow (setlistIndex, setlist.size()))
        return {};

    const auto& e = setlist[setlistIndex];
    return e.song.isNotEmpty() ? e.song : e.preset;
}

void Engine::syncSetlistIndex()
{
    if (! isSetlistActive())
        return;

    const auto presetName = currentPresetFile.getFileNameWithoutExtension();

    // la canzone attuale va bene se usa il preset caricato; altrimenti la prima che lo usa
    if (! (isPositiveAndBelow (setlistIndex, setlist.size()) && setlist[setlistIndex].preset == presetName))
    {
        setlistIndex = -1;
        for (int i = 0; i < setlist.size(); ++i)
            if (setlist[i].preset == presetName) { setlistIndex = i; break; }
    }

    saveSetlistPosition();
}

void Engine::saveSetlistPosition()
{
    props().setValue ("setlist", currentSetlistFile == File() ? String() : currentSetlistFile.getFullPathName());
    props().setValue ("setlistIndex", setlistIndex);
}

String Engine::getCurrentPresetName() const
{
    return currentPresetFile == File() ? String ("(new preset)") : currentPresetFile.getFileNameWithoutExtension();
}

var Engine::createPresetState() const
{
    auto root = std::make_unique<DynamicObject>();
    root->setProperty ("version", 1);
    root->setProperty ("bpm", getBpm());

    Array<var> slotList;
    for (int i = 0; i < numSlots; ++i)
    {
        auto slotObj = std::make_unique<DynamicObject>();

        if (auto* p = getSlotProcessor (i))
        {
            if (auto xml = slots[(size_t) i].description.createXml())
                slotObj->setProperty ("plugin", xml->toString (XmlElement::TextFormat().singleLine()));

            MemoryBlock state;
            p->getStateInformation (state);
            slotObj->setProperty ("name", p->getName());
            slotObj->setProperty ("state", state.toBase64Encoding());
            slotObj->setProperty ("bypassed", slots[(size_t) i].userBypassed);
            slotObj->setProperty ("label", slots[(size_t) i].customName);
            slotObj->setProperty ("kind", getSlotKindId (slots[(size_t) i].kind));
        }
        else
        {
            slotObj->setProperty ("empty", true);
        }

        slotList.add (var (slotObj.release()));
    }
    root->setProperty ("slots", slotList);

    Array<var> buttonList;
    for (const auto& b : buttons)
    {
        auto buttonObj = std::make_unique<DynamicObject>();
        buttonObj->setProperty ("label", b.label);
        buttonObj->setProperty ("active", b.active);

        Array<var> controlled;
        for (auto s : b.slots)
            controlled.add (s);
        buttonObj->setProperty ("slots", controlled);
        buttonObj->setProperty ("mode", b.mode == ButtonMode::scene ? "scene" : "switch");

        Array<var> sceneOn;
        for (auto s : b.sceneOn)
            sceneOn.add (s);
        buttonObj->setProperty ("sceneOn", sceneOn);

        buttonList.add (var (buttonObj.release()));
    }
    root->setProperty ("buttons", buttonList);

    return var (root.release());
}

bool Engine::savePreset (const File& fileIn)
{
    const auto file = fileIn.withFileExtension (presetExtension);

    if (! file.replaceWithText (JSON::toString (createPresetState())))
        return false;

    currentPresetFile = file;
    syncSetlistIndex();
    props().setValue ("lastPreset", file.getFullPathName());
    props().saveIfNeeded();
    markClean();
    sendChangeMessage();
    return true;
}

bool Engine::loadPreset (const File& file, String& errors)
{
    const var root = JSON::parse (file);

    if (! root.isObject())
    {
        errors = "Invalid preset file: " + file.getFileName();
        return false;
    }

    const bool ok = loadPresetState (root, errors, file.getFileNameWithoutExtension());

    currentPresetFile = file;
    syncSetlistIndex();
    props().setValue ("lastPreset", file.getFullPathName());
    props().saveIfNeeded();
    markClean();
    sendChangeMessage();
    return ok;
}

bool Engine::loadPresetState (const var& root, String& errors, const String& nameForLog)
{
    const auto startMs = Time::getMillisecondCounterHiRes();
    setBpm (root.hasProperty ("bpm") ? (double) root["bpm"] : 120.0);

    // Cambio preset senza buco:
    //  1. i plugin già caricati e uguali vengono riusati (anche se cambiano posto) e ricevono solo il nuovo stato;
    //  2. quelli nuovi si creano prima, mentre la catena vecchia continua a suonare;
    //  3. solo alla fine la catena cambia, con un'unica ricostruzione del grafo.
    struct Planned
    {
        bool used = false;
        int reuseFrom = -1;                                   // posto attuale da riusare
        std::unique_ptr<AudioPluginInstance> instance;        // oppure plugin nuovo
        PluginDescription description;
        MemoryBlock state;
        bool bypassed = false;
        String label;
        SlotKind kind = SlotKind::automatic;
    };

    std::array<Planned, numSlots> plan;
    std::array<bool, numSlots> reused {};
    int numReused = 0, numCreated = 0;

    const auto samePlugin = [] (const PluginDescription& a, const PluginDescription& b)
    {
        return a.pluginFormatName == b.pluginFormatName && a.uniqueId == b.uniqueId && a.name == b.name;
    };

    // Stato attuale dei plugin caricati, letto una volta sola e solo se serve:
    // con lo stesso stato il plugin si riusa così com'è (niente ricaricamento, la coda continua)
    std::array<std::optional<MemoryBlock>, numSlots> currentStates;
    const auto currentState = [&] (int j) -> const MemoryBlock&
    {
        auto& cached = currentStates[(size_t) j];
        if (! cached.has_value())
        {
            cached.emplace();
            if (auto* proc = getSlotProcessor (j))
                proc->getStateInformation (*cached);
        }
        return *cached;
    };

    // delay e riverberi che suonavano: diventano code invece di essere riusati con un'altra impostazione
    const bool spillover = isSpilloverEnabled();
    std::array<bool, numSlots> tailCandidate {};
    for (int j = 0; j < numSlots; ++j)
        tailCandidate[(size_t) j] = spillover && ! isSlotEmpty (j) && isSlotActive (j)
                                    && (getSlotKind (j) == SlotKind::delay || getSlotKind (j) == SlotKind::reverb);

    if (const auto* slotList = root["slots"].getArray())
    {
        for (int i = 0; i < jmin (numSlots, slotList->size()); ++i)
        {
            const var& s = slotList->getReference (i);
            const String pluginXml = s["plugin"].toString();

            if (pluginXml.isEmpty())
                continue;

            auto& p = plan[(size_t) i];

            if (auto xml = parseXML (pluginXml); xml == nullptr || ! p.description.loadFromXml (*xml))
            {
                errors << "Slot " << (i + 1) << ": unreadable plugin description\n";
                continue;
            }

            p.state.fromBase64Encoding (s["state"].toString());
            p.bypassed = (bool) s["bypassed"];
            p.label    = s["label"].toString();
            p.kind     = getSlotKindFromId (s["kind"].toString());

            // stesso plugin già caricato? prima nello stesso posto, poi in qualsiasi altro
            const auto canReuse = [&] (int j)
            {
                return ! reused[(size_t) j] && slots[(size_t) j].node.has_value()
                        && samePlugin (slots[(size_t) j].description, p.description)
                        && (! tailCandidate[(size_t) j] || currentState (j) == p.state);
            };

            int from = canReuse (i) ? i : -1;
            for (int j = 0; j < numSlots && from < 0; ++j)
                if (canReuse (j))
                    from = j;

            if (from >= 0)
            {
                reused[(size_t) from] = true;
                p.reuseFrom = from;
                p.used = true;
                ++numReused;
                continue;
            }

            String error;
            PluginDescription usedDescription;
            p.instance = createInstance (p.description, error, usedDescription);

            if (p.instance == nullptr)
            {
                errors << "Slot " << (i + 1) << ": could not load " << p.description.name
                       << (error.isNotEmpty() ? " (" + error + ")" : String()) << "\n";
                continue;
            }

            p.description = usedDescription;
            if (p.state.getSize() > 0)
                p.instance->setStateInformation (p.state.getData(), (int) p.state.getSize());

            p.used = true;
            ++numCreated;
        }
    }

    // Da qui la catena cambia: tutto quello che era lento è già stato fatto
    std::array<Slot, numSlots> newSlots;

    for (int i = 0; i < numSlots; ++i)
        if (const auto& p = plan[(size_t) i]; p.used && p.reuseFrom >= 0)
            newSlots[(size_t) i] = slots[(size_t) p.reuseFrom];

    int numTails = 0;
    for (int j = 0; j < numSlots; ++j)
    {
        if (reused[(size_t) j])
            continue;

        if (tailCandidate[(size_t) j]) { keepAsTail (j); ++numTails; }
        else                           removeSlotNode (j, UpdateKind::none);
    }

    for (int i = 0; i < numSlots; ++i)
    {
        auto& p = plan[(size_t) i];
        auto& slot = newSlots[(size_t) i];

        if (! p.used)
            continue;

        if (p.reuseFrom >= 0)
        {
            // stesso plugin: basta applicare le impostazioni del preset (se sono diverse)
            if (auto* node = graph.getNodeForId (*slot.node);
                node != nullptr && p.state.getSize() > 0 && currentState (p.reuseFrom) != p.state)
                node->getProcessor()->setStateInformation (p.state.getData(), (int) p.state.getSize());
        }
        else if (auto node = graph.addNode (std::move (p.instance), std::nullopt, UpdateKind::none))
        {
            slot.node = node->nodeID;
            slot.description = p.description;
            listenTo (node->nodeID);
            logPluginChannels (p.description.name, *node->getProcessor());
        }
        else
        {
            continue;
        }

        slot.userBypassed = p.bypassed;
        slot.customName   = p.label;
        slot.kind         = p.kind;
    }

    slots = newSlots;

    if (const auto* buttonList = root["buttons"].getArray())
    {
        for (int i = 0; i < jmin (numButtons, buttonList->size()); ++i)
        {
            const var& b = buttonList->getReference (i);
            auto& button = buttons[(size_t) i];

            button.label  = b["label"].toString();
            button.active = (bool) b["active"];
            button.slots.clear();
            button.sceneOn.clear();
            button.mode = b["mode"].toString() == "scene" ? ButtonMode::scene : ButtonMode::toggle;

            if (const auto* controlled = b["slots"].getArray())
                for (const auto& s : *controlled)
                    if (isPositiveAndBelow ((int) s, numSlots))
                        button.slots.addIfNotAlreadyThere ((int) s);

            if (const auto* on = b["sceneOn"].getArray())
                for (const auto& s : *on)
                    if (isPositiveAndBelow ((int) s, numSlots))
                        button.sceneOn.addIfNotAlreadyThere ((int) s);
        }
    }

    rewire();

    PluginScanner::log ("PRESET " + nameForLog + ": " + String (numReused) + " plugins reused, "
                        + String (numCreated) + " new, " + String (numTails) + " tails, " + String (Time::getMillisecondCounterHiRes() - startMs, 0) + " ms");
    return errors.isEmpty();
}

void Engine::tapTempo()
{
    const auto now = Time::getMillisecondCounter();

    // dopo una pausa lunga si ricomincia a contare
    if (! tapTimes.isEmpty() && now - tapTimes.getLast() > 2000)
        tapTimes.clearQuick();

    tapTimes.add (now);

    while (tapTimes.size() > 5)   // media sugli ultimi 4 intervalli
        tapTimes.remove (0);

    if (tapTimes.size() < 2)
        return;

    const double interval = (double) (tapTimes.getLast() - tapTimes.getFirst()) / (double) (tapTimes.size() - 1);
    const double bpm = jlimit (30.0, 300.0, 60000.0 / interval);

    playHead.bpm = std::round (bpm);   // a tempo intero: è quello che si legge e si ricorda
    recoveryPending = true;      // come i pedali: niente "modifiche non salvate"
    sendChangeMessage();
}

void Engine::setBpm (double bpm)
{
    const double newBpm = jlimit (30.0, 300.0, bpm);

    if (! approximatelyEqual (newBpm, playHead.bpm.load()))
    {
        playHead.bpm = newBpm;
        markDirty();   // durante un caricamento markClean() lo annulla subito dopo
    }

    sendChangeMessage();
}

void Engine::loadAdjacentPreset (int delta)
{
    if (isSetlistActive())
    {
        if (setlist.isEmpty())
            return;

        // in scaletta niente giro dalla fine all'inizio: sul palco è più sicuro fermarsi.
        // Le canzoni il cui preset non esiste più vengono saltate.
        const int step = delta < 0 ? -1 : 1;
        int index = setlistIndex < 0 ? (step > 0 ? 0 : setlist.size() - 1) : setlistIndex + step;

        for (; isPositiveAndBelow (index, setlist.size()); index += step)
        {
            if (getPresetFileByName (setlist[index].preset).existsAsFile())
            {
                String errors;
                loadSetlistEntry (index, errors);
                return;
            }

            PluginScanner::log ("SETLIST song " + String (index + 1) + " skipped: preset not found: " + setlist[index].preset);
        }
        return;
    }

    const auto files = getPresetFiles();
    if (files.isEmpty())
        return;

    int index = files.indexOf (currentPresetFile);
    index = index < 0 ? 0 : (index + delta + files.size()) % files.size();

    String errors;
    loadPreset (files[index], errors);
}

void Engine::newEmptyPreset()
{
    for (int i = 0; i < numSlots; ++i)
        removeSlotNode (i, UpdateKind::none);

    for (int i = 0; i < numButtons; ++i)
    {
        buttons[(size_t) i].label = defaultButtonLabels[i];
        buttons[(size_t) i].active = false;
        buttons[(size_t) i].mode = ButtonMode::toggle;
        buttons[(size_t) i].slots.clear();
        buttons[(size_t) i].sceneOn.clear();
    }

    currentPresetFile = File();
    rewire();
    markClean();
    sendChangeMessage();
}
