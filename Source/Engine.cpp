#include "Engine.h"
#include "PluginScanner.h"

using namespace juce;

namespace
{
    constexpr const char* presetExtension = ".chainrig";

    const char* defaultButtonLabels[] = { "BOOST", "DRIVE", "DELAY", "FX" };
}

//==============================================================================
Engine::Engine()
{
    PropertiesFile::Options options;
    options.applicationName     = "ChainRig";
    options.filenameSuffix      = ".settings";
    options.folderName          = "ChainRig";
    options.osxLibrarySubFolder = "Application Support";
    appProperties.setStorageParameters (options);

    addDefaultFormatsToManager (formatManager);

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

    player.setProcessor (&graph);

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
    deviceManager.addChangeListener (this);

    enableAllMidiInputs();
    deviceManager.addMidiInputDeviceCallback ({}, this);

    rewire();

    const File lastPreset (props().getValue ("lastPreset"));
    if (lastPreset.existsAsFile())
    {
        String errors;
        loadPreset (lastPreset, errors);
    }

    updateDeviceInfo();
    startTimer (500);    // diagnostica audio; ogni 2 s controlla nuove periferiche MIDI (es. pedaliera Bluetooth)
}

Engine::~Engine()
{
    stopTimer();
    saveSettings();

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
    sendChangeMessage();
    return true;
}

void Engine::installInSlot (int slot, std::unique_ptr<AudioPluginInstance> instance,
                            const PluginDescription& description, UpdateKind kind)
{
    removeSlotNode (slot, kind);

    if (auto node = graph.addNode (std::move (instance), std::nullopt, kind))
    {
        slots[(size_t) slot].node = node->nodeID;
        slots[(size_t) slot].description = description;
        slots[(size_t) slot].userBypassed = false;
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
        if (onProcessorAboutToBeDeleted != nullptr)
            onProcessorAboutToBeDeleted (node->getProcessor());

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
        const bool hasA = button.slots.contains (a);
        const bool hasB = button.slots.contains (b);
        button.slots.removeAllInstancesOf (a);
        button.slots.removeAllInstancesOf (b);
        if (hasA) button.slots.add (b);
        if (hasB) button.slots.add (a);
    }

    rewire();
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

void Engine::rewire()
{
    for (const auto& c : graph.getConnections())
        graph.removeConnection (c, UpdateKind::none);

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

    for (const auto& s : slots)
    {
        if (s.node.has_value())
        {
            connectNodes (previous, *s.node, 2);
            previous = *s.node;
        }
    }

    connectNodes (previous, outputNode, 2);   // solo le uscite 1-2 della scheda

    updateBypassStates();
    graph.rebuild();
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
    if (has ({ "tone3000", "neural amp", " nam", "amp sim", "amplifier" }))  return SlotKind::amp;
    if (has ({ "impulse", "cab", " ir ", "ir loader", "speaker" }))         return SlotKind::ir;
    if (has ({ "reverb", "verb", "plate", "room", "hall", "shimmer", "supermassive", "bigsky", "crystalline" }))
        return SlotKind::reverb;
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
        case SlotKind::fx:
        case SlotKind::automatic:  break;
    }
    return "FX";
}

String Engine::getSlotKindName (SlotKind k)
{
    switch (k)
    {
        case SlotKind::automatic:  return "Automatico";
        case SlotKind::amp:        return "Amplificatore";
        case SlotKind::ir:         return "Cassa / IR";
        case SlotKind::drive:      return "Drive / distorsore";
        case SlotKind::compressor: return "Compressore";
        case SlotKind::eq:         return "Equalizzatore";
        case SlotKind::modulation: return "Modulazione";
        case SlotKind::delay:      return "Delay";
        case SlotKind::reverb:     return "Riverbero";
        case SlotKind::fx:         break;
    }
    return "Altro effetto";
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
    sendChangeMessage();
}

bool Engine::isSlotControlledByButton (int slot) const
{
    for (const auto& b : buttons)
        if (b.slots.contains (slot))
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
        if (b.active && b.slots.contains (slot))
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
    if (isPositiveAndBelow (index, numButtons))
        setButtonActive (index, ! buttons[(size_t) index].active);
}

void Engine::setButtonActive (int index, bool active)
{
    if (! isPositiveAndBelow (index, numButtons))
        return;

    buttons[(size_t) index].active = active;
    updateBypassStates();
    sendChangeMessage();
}

void Engine::setButtonLabel (int index, const String& label)
{
    if (! isPositiveAndBelow (index, numButtons))
        return;

    buttons[(size_t) index].label = label.trim().toUpperCase();
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
    sendChangeMessage();
}

//==============================================================================
// MIDI: numeri CC modificabili nel file di impostazioni (ChainRig.settings)
int Engine::getButtonCC (int index) const   { return appProperties.getUserSettings()->getIntValue ("cc_button" + String (index + 1), 80 + index); }
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
    props().setValue ("inputChannel", inputChannel);
    rewire();
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
        return "Nessuna scheda audio";

    // due righe: scheda e formato; latenza, carico e buchi
    return deviceName + String::fromUTF8 ("  ·  ") + String (deviceSampleRate / 1000.0, 1).replace (".", ",")
         + String::fromUTF8 (" kHz  ·  buffer ") + String (deviceBufferSize)
         + "\nlatenza " + String (deviceLatencyMs, 1).replace (".", ",") + String::fromUTF8 (" ms  ·  CPU ")
         + String (roundToInt (deviceManager.getCpuUsage() * 100.0)) + String::fromUTF8 ("%  ·  picco ")
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

    PluginScanner::log ("AUDIO  avvio: " + deviceName + ", " + String (deviceSampleRate, 0) + " Hz, buffer "
                        + String (deviceBufferSize) + ", latenza " + String (deviceLatencyMs, 1) + " ms");
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

    auto* device = deviceManager.getCurrentAudioDevice();
    const int xruns = device != nullptr ? jmax (0, device->getXRunCount()) : 0;
    const int overloads = recorder.getOverloads();
    const int late = recorder.getLateCallbacks();

    if (xruns != loggedXruns || overloads != loggedOverloads || late != loggedLate)
    {
        PluginScanner::log ("AUDIO  xrun scheda " + String (xruns) + " (+" + String (xruns - loggedXruns) + ")"
                            + ", catena oltre il tempo " + String (overloads) + " (+" + String (overloads - loggedOverloads) + ")"
                            + ", blocchi in ritardo " + String (late) + " (+" + String (late - loggedLate) + ")"
                            + ", carico max " + String (roundToInt (recentPeakLoad * 100.0f)) + "%"
                            + ", CPU " + String (roundToInt (deviceManager.getCpuUsage() * 100.0)) + "%");
        loggedXruns = xruns;
        loggedOverloads = overloads;
        loggedLate = late;
    }
}

//==============================================================================
// Preset: file JSON leggibili, uno per preset, nella cartella Documenti/ChainRig/Preset
Array<File> Engine::getPresetFiles() const
{
    auto files = getPresetFolder().findChildFiles (File::findFiles, false, String ("*") + presetExtension);
    files.sort();
    return files;
}

String Engine::getCurrentPresetName() const
{
    return currentPresetFile == File() ? String ("(nuovo preset)") : currentPresetFile.getFileNameWithoutExtension();
}

bool Engine::savePreset (const File& fileIn)
{
    const auto file = fileIn.withFileExtension (presetExtension);

    auto root = std::make_unique<DynamicObject>();
    root->setProperty ("version", 1);

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

        buttonList.add (var (buttonObj.release()));
    }
    root->setProperty ("buttons", buttonList);

    if (! file.replaceWithText (JSON::toString (var (root.release()))))
        return false;

    currentPresetFile = file;
    props().setValue ("lastPreset", file.getFullPathName());
    props().saveIfNeeded();
    sendChangeMessage();
    return true;
}

bool Engine::loadPreset (const File& file, String& errors)
{
    const var root = JSON::parse (file);

    if (! root.isObject())
    {
        errors = "File preset non valido: " + file.getFileName();
        return false;
    }

    // Svuota tutto senza ricostruire il grafo a ogni passo
    for (int i = 0; i < numSlots; ++i)
        removeSlotNode (i, UpdateKind::none);

    if (const auto* slotList = root["slots"].getArray())
    {
        for (int i = 0; i < jmin (numSlots, slotList->size()); ++i)
        {
            const var& s = slotList->getReference (i);
            const String pluginXml = s["plugin"].toString();

            if (pluginXml.isEmpty())
                continue;

            PluginDescription desc;
            if (auto xml = parseXML (pluginXml); xml == nullptr || ! desc.loadFromXml (*xml))
            {
                errors << "Posto " << (i + 1) << ": descrizione plugin illeggibile\n";
                continue;
            }

            String error;
            PluginDescription used;
            auto instance = createInstance (desc, error, used);

            if (instance == nullptr)
            {
                errors << "Posto " << (i + 1) << ": impossibile caricare " << desc.name
                       << (error.isNotEmpty() ? " (" + error + ")" : String()) << "\n";
                continue;
            }

            MemoryBlock state;
            if (state.fromBase64Encoding (s["state"].toString()) && state.getSize() > 0)
                instance->setStateInformation (state.getData(), (int) state.getSize());

            installInSlot (i, std::move (instance), used, UpdateKind::none);
            slots[(size_t) i].userBypassed = (bool) s["bypassed"];
            slots[(size_t) i].customName   = s["label"].toString();
            slots[(size_t) i].kind         = getSlotKindFromId (s["kind"].toString());
        }
    }

    if (const auto* buttonList = root["buttons"].getArray())
    {
        for (int i = 0; i < jmin (numButtons, buttonList->size()); ++i)
        {
            const var& b = buttonList->getReference (i);
            auto& button = buttons[(size_t) i];

            button.label  = b["label"].toString();
            button.active = (bool) b["active"];
            button.slots.clear();

            if (const auto* controlled = b["slots"].getArray())
                for (const auto& s : *controlled)
                    if (isPositiveAndBelow ((int) s, numSlots))
                        button.slots.addIfNotAlreadyThere ((int) s);
        }
    }

    currentPresetFile = file;
    props().setValue ("lastPreset", file.getFullPathName());
    props().saveIfNeeded();

    rewire();
    sendChangeMessage();
    return errors.isEmpty();
}

void Engine::loadAdjacentPreset (int delta)
{
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
        buttons[(size_t) i].slots.clear();
    }

    currentPresetFile = File();
    rewire();
    sendChangeMessage();
}
