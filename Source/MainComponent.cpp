#include "MainComponent.h"

using namespace juce;

//==============================================================================
StageLookAndFeel::StageLookAndFeel()
{
    setColourScheme (LookAndFeel_V4::getMidnightColourScheme());

    setColour (ResizableWindow::backgroundColourId, Palette::background);
    setColour (TextButton::buttonColourId,          Palette::panelLight);
    setColour (TextButton::buttonOnColourId,        Palette::accent);
    setColour (TextButton::textColourOffId,         Palette::text);
    setColour (TextButton::textColourOnId,          Colours::black);
    setColour (Label::textColourId,                 Palette::textDim);
    setColour (PopupMenu::backgroundColourId,       Palette::panel);
    setColour (PopupMenu::textColourId,             Palette::text);
    setColour (PopupMenu::highlightedBackgroundColourId, Palette::accent);
    setColour (PopupMenu::highlightedTextColourId,  Colours::black);
    setColour (PopupMenu::headerTextColourId,       Palette::accent);
    setColour (AlertWindow::backgroundColourId,     Palette::panel);
    setColour (AlertWindow::textColourId,           Palette::text);
}

Font StageLookAndFeel::getTextButtonFont (TextButton&, int buttonHeight)
{
    return Font (FontOptions (jmin (20.0f, (float) buttonHeight * 0.42f), Font::bold));
}

Font StageLookAndFeel::getPopupMenuFont()
{
    return Font (FontOptions (21.0f));
}

void StageLookAndFeel::getIdealPopupMenuItemSize (const String& text, bool isSeparator, int standardMenuItemHeight,
                                                  int& idealWidth, int& idealHeight)
{
    LookAndFeel_V4::getIdealPopupMenuItemSize (text, isSeparator, standardMenuItemHeight, idealWidth, idealHeight);

    if (! isSeparator)
    {
        idealHeight = jmax (idealHeight, 48);   // voci alte: facili da toccare
        idealWidth  = jmax (idealWidth, 280);
    }
}

//==============================================================================
void SlotView::paint (Graphics& g)
{
    auto& engine = owner.getEngine();
    auto bounds = getLocalBounds().toFloat().reduced (2.0f);

    const bool empty  = engine.isSlotEmpty (index);
    const bool active = engine.isSlotActive (index);

    if (empty)
    {
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (bounds, 12.0f);

        Path outline;
        outline.addRoundedRectangle (bounds.reduced (4.0f), 10.0f);
        const float dashes[] = { 8.0f, 6.0f };
        PathStrokeType (1.5f).createDashedStroke (outline, outline, dashes, 2);
        g.setColour (Palette::off);
        g.fillPath (outline);

        g.setColour (Palette::textDim);
        g.setFont (FontOptions (18.0f));
        g.drawText ("+ vuoto", bounds, Justification::centred);
    }
    else
    {
        g.setColour (active ? Palette::panelLight : Palette::panel);
        g.fillRoundedRectangle (bounds, 12.0f);

        // barra di stato in alto
        auto bar = bounds.removeFromTop (8.0f).reduced (12.0f, 0.0f).withTrimmedLeft (22.0f).translated (0.0f, 10.0f);
        g.setColour (active ? Palette::on : Palette::off);
        g.fillRoundedRectangle (bar, 3.0f);

        g.setColour (active ? Palette::text : Palette::textDim);
        g.setFont (FontOptions (20.0f, Font::bold));
        g.drawFittedText (engine.getSlotName (index), bounds.reduced (10.0f).toNearestInt(),
                          Justification::centred, 3);

        // chi lo controlla
        String info;
        if (engine.getSlotUserBypass (index))
            info = "BYPASS";
        else
            for (int b = 0; b < Engine::numButtons; ++b)
                if (engine.getButton (b).slots.contains (index))
                    info << (info.isEmpty() ? "" : " + ") << engine.getButton (b).label;

        if (info.isNotEmpty())
        {
            g.setFont (FontOptions (14.0f));
            g.setColour (engine.getSlotUserBypass (index) ? Palette::danger : Palette::accent);
            g.drawText (info, getLocalBounds().reduced (10, 8), Justification::centredBottom);
        }
    }

    g.setColour (Palette::textDim);
    g.setFont (FontOptions (15.0f, Font::bold));
    g.drawText (String (index + 1), getLocalBounds().reduced (12, 12), Justification::topLeft);
}

void SlotView::mouseUp (const MouseEvent& e)
{
    if (e.mouseWasClicked())
        owner.showSlotMenu (index);
}

//==============================================================================
void BigButton::paint (Graphics& g)
{
    const auto& button = owner.getEngine().getButton (index);
    auto bounds = getLocalBounds().toFloat().reduced (6.0f);

    g.setColour (button.active ? Palette::on.withAlpha (0.18f) : Palette::panel);
    g.fillRoundedRectangle (bounds, 18.0f);

    g.setColour (button.active ? Palette::on : Palette::off);
    g.drawRoundedRectangle (bounds, 18.0f, button.active ? 4.0f : 2.0f);

    // LED
    const auto led = Rectangle<float> (22.0f, 22.0f).withCentre ({ bounds.getCentreX(), bounds.getY() + 34.0f });
    g.setColour (button.active ? Palette::on : Palette::off);
    g.fillEllipse (led);
    if (button.active)
    {
        g.setColour (Palette::on.withAlpha (0.35f));
        g.fillEllipse (led.expanded (8.0f));
    }

    g.setColour (button.active ? Palette::text : Palette::textDim);
    g.setFont (FontOptions (jmin (34.0f, bounds.getHeight() * 0.22f), Font::bold));
    g.drawFittedText (button.label, bounds.reduced (12.0f).withTrimmedTop (40.0f).toNearestInt(),
                      Justification::centred, 2);

    if (owner.isEditMode())
    {
        g.setColour (Palette::accent);
        g.setFont (FontOptions (15.0f));
        const String hint = button.slots.isEmpty() ? "tocca per assegnare i posti"
                                                   : "posti: " + [&] { auto sorted = button.slots; sorted.sort(); StringArray s; for (auto i : sorted) s.add (String (i + 1)); return s.joinIntoString (", "); }();
        g.drawText (hint, bounds.reduced (10.0f).toNearestInt(), Justification::centredBottom);
    }
}

void BigButton::mouseDown (const MouseEvent&)
{
    owner.bigButtonPressed (index);
}

//==============================================================================
void TunerOverlay::visibilityChanged()
{
    if (isVisible())
    {
        history.assign (engine.getTuner().getSampleRateForAnalysis() > 50000.0 ? 4096 : 2048, 0.0f);
        incoming.assign (16384, 0.0f);
        recent.clear();
        displayFreq = 0.0f;
        startTimerHz (20);
    }
    else
    {
        stopTimer();
    }
}

void TunerOverlay::timerCallback()
{
    auto& tuner = engine.getTuner();
    const int got = tuner.pull (incoming.data(), (int) incoming.size());

    if (got > 0)
    {
        const int n = (int) history.size();
        if (got >= n)
        {
            std::copy (incoming.begin() + (got - n), incoming.begin() + got, history.begin());
        }
        else
        {
            std::move (history.begin() + got, history.end(), history.begin());
            std::copy (incoming.begin(), incoming.begin() + got, history.end() - got);
        }
    }

    const float freq = detectPitchYin (history.data(), (int) history.size(), tuner.getSampleRateForAnalysis());

    if (freq > 0.0f)
    {
        framesWithoutSignal = 0;
        recent.add (freq);
        if (recent.size() > 5)
            recent.remove (0);

        auto sorted = recent;
        sorted.sort();
        displayFreq = sorted[sorted.size() / 2];
    }
    else if (++framesWithoutSignal > 10)   // mezzo secondo senza segnale: svuota
    {
        recent.clear();
        displayFreq = 0.0f;
    }

    repaint();
}

void TunerOverlay::paint (Graphics& g)
{
    g.fillAll (Palette::background.withAlpha (0.97f));

    auto area = getLocalBounds().reduced (40);

    g.setColour (Palette::textDim);
    g.setFont (FontOptions (18.0f));
    g.drawText ("ACCORDATORE  -  uscita muta  -  tocca per chiudere", area.removeFromBottom (30), Justification::centred);

    if (displayFreq <= 0.0f)
    {
        g.setFont (FontOptions (40.0f));
        g.drawText ("suona una corda", area, Justification::centred);
        return;
    }

    static const char* names[]   = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const char* namesIt[] = { "Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si" };

    const double midi = 69.0 + 12.0 * std::log2 (displayFreq / 440.0);
    const int nearest = roundToInt (midi);
    const float cents = (float) ((midi - nearest) * 100.0);
    const int noteIndex = ((nearest % 12) + 12) % 12;
    const bool inTune = std::abs (cents) < 3.0f;

    const auto colour = inTune ? Palette::on : (std::abs (cents) < 15.0f ? Palette::accent : Palette::danger);

    auto top = area.removeFromTop (area.getHeight() / 2);
    g.setColour (colour);
    g.setFont (FontOptions ((float) top.getHeight() * 0.8f, Font::bold));
    g.drawText (names[noteIndex], top, Justification::centred);

    g.setColour (Palette::textDim);
    g.setFont (FontOptions (24.0f));
    g.drawText (String (namesIt[noteIndex]) + String (nearest / 12 - 1) + "   " + String (displayFreq, 1) + " Hz   "
                    + (cents >= 0 ? "+" : "") + String (cents, 1) + " cent",
                area.removeFromTop (40), Justification::centred);

    // barra -50 ... +50 cent
    auto meter = area.removeFromTop (70).reduced (area.getWidth() / 8, 15).toFloat();
    g.setColour (Palette::panel);
    g.fillRoundedRectangle (meter, 8.0f);

    g.setColour (Palette::on.withAlpha (0.25f));
    g.fillRect (meter.withSizeKeepingCentre (meter.getWidth() * 0.06f, meter.getHeight()));

    const float x = meter.getCentreX() + jlimit (-1.0f, 1.0f, cents / 50.0f) * meter.getWidth() * 0.5f;
    g.setColour (colour);
    g.fillRoundedRectangle (Rectangle<float> (10.0f, meter.getHeight() + 16.0f).withCentre ({ x, meter.getCentreY() }), 4.0f);
}

void TunerOverlay::mouseUp (const MouseEvent&)
{
    engine.setTunerActive (false);
}

//==============================================================================
PluginWindow::PluginWindow (AudioProcessor& processor, std::function<void()> closeCallback)
    : DocumentWindow (processor.getName(), Palette::panel, DocumentWindow::closeButton),
      onClose (std::move (closeCallback))
{
    setUsingNativeTitleBar (true);

    AudioProcessorEditor* editor = processor.hasEditor() ? processor.createEditorAndMakeActive() : nullptr;
    if (editor == nullptr)
        editor = new GenericAudioProcessorEditor (processor);

    setContentOwned (editor, true);
    setResizable (editor->isResizable(), false);
    setAlwaysOnTop (true);   // sopra la finestra a schermo intero
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}

void PluginWindow::closeButtonPressed()
{
    if (onClose != nullptr)
        onClose();   // il proprietario elimina questa finestra
}

//==============================================================================
MainComponent::MainComponent (Engine& e) : engine (e)
{
    LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
    setLookAndFeel (&lookAndFeel);

    for (auto* b : { &prevButton, &nextButton, &presetButton, &tunerButton, &editButton, &settingsButton })
        addAndMakeVisible (b);

    prevButton.onClick     = [this] { engine.loadAdjacentPreset (-1); };
    nextButton.onClick     = [this] { engine.loadAdjacentPreset (1); };
    presetButton.onClick   = [this] { showPresetMenu(); };
    tunerButton.onClick    = [this] { engine.setTunerActive (! engine.isTunerActive()); };
    editButton.onClick     = [this] { editMode = ! editMode; refresh(); };
    settingsButton.onClick = [this] { showSettingsMenu(); };

    statusLabel.setFont (FontOptions (15.0f));
    statusLabel.setJustificationType (Justification::centredRight);
    addAndMakeVisible (statusLabel);

    for (int i = 0; i < Engine::numSlots; ++i)
        addAndMakeVisible (slotViews.add (new SlotView (*this, i)));

    for (int i = 0; i < Engine::numButtons; ++i)
        addAndMakeVisible (bigButtons.add (new BigButton (*this, i)));

    addChildComponent (tunerOverlay);

    engine.onProcessorAboutToBeDeleted = [this] (AudioProcessor* p) { pluginWindows.erase (p); };
    engine.addChangeListener (this);

    setSize (1280, 800);
    refresh();
    startTimerHz (4);
}

MainComponent::~MainComponent()
{
    stopTimer();
    engine.removeChangeListener (this);
    engine.onProcessorAboutToBeDeleted = nullptr;
    pluginWindows.clear();
    textInput.reset();

    setLookAndFeel (nullptr);
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

//==============================================================================
void MainComponent::paint (Graphics& g)
{
    g.fillAll (Palette::background);

    // freccette del flusso del segnale tra i posti
    g.setColour (Palette::off);
    for (int i = 0; i < Engine::numSlots - 1; ++i)
    {
        if (i == Engine::numSlots / 2 - 1)
            continue;   // fine riga

        auto a = slotViews[i]->getBounds();
        const float x = (float) a.getRight() + 2.0f;
        const float y = (float) a.getCentreY();

        Path arrow;
        arrow.addTriangle (x, y - 7.0f, x, y + 7.0f, x + 9.0f, y);
        g.fillPath (arrow);
    }
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (12);

    // barra superiore
    auto top = area.removeFromTop (60);
    prevButton.setBounds (top.removeFromLeft (64).reduced (3));
    presetButton.setBounds (top.removeFromLeft (300).reduced (3));
    nextButton.setBounds (top.removeFromLeft (64).reduced (3));
    top.removeFromLeft (12);
    tunerButton.setBounds (top.removeFromLeft (180).reduced (3));
    settingsButton.setBounds (top.removeFromRight (170).reduced (3));
    editButton.setBounds (top.removeFromRight (140).reduced (3));
    statusLabel.setBounds (top.reduced (8, 0));

    area.removeFromTop (12);

    // pulsanti grandi in basso
    auto bottom = area.removeFromBottom (jmax (150, area.getHeight() * 32 / 100));
    const int bw = bottom.getWidth() / Engine::numButtons;
    for (int i = 0; i < Engine::numButtons; ++i)
        bigButtons[i]->setBounds (bottom.removeFromLeft (bw));

    area.removeFromBottom (10);

    // posti: due righe da cinque
    const int perRow = Engine::numSlots / 2;
    const int rowHeight = area.getHeight() / 2;
    for (int row = 0; row < 2; ++row)
    {
        auto rowArea = area.removeFromTop (rowHeight).reduced (0, 5);
        const int sw = rowArea.getWidth() / perRow;
        for (int c = 0; c < perRow; ++c)
            slotViews[row * perRow + c]->setBounds (rowArea.removeFromLeft (sw).reduced (8, 0));
    }

    tunerOverlay.setBounds (getLocalBounds());
}

//==============================================================================
void MainComponent::changeListenerCallback (ChangeBroadcaster*)
{
    refresh();
}

void MainComponent::timerCallback()
{
    statusLabel.setText (engine.getStatusText(), dontSendNotification);
}

void MainComponent::refresh()
{
    presetButton.setButtonText (engine.getCurrentPresetName());
    tunerButton.setToggleState (engine.isTunerActive(), dontSendNotification);
    editButton.setToggleState (editMode, dontSendNotification);

    tunerOverlay.setVisible (engine.isTunerActive());
    if (engine.isTunerActive())
        tunerOverlay.toFront (false);

    for (auto* s : slotViews)  s->repaint();
    for (auto* b : bigButtons) b->repaint();
    repaint();
}

void MainComponent::setFullScreen (bool shouldBeFullScreen)
{
    auto* top = getTopLevelComponent();
    Desktop::getInstance().setKioskModeComponent (shouldBeFullScreen ? top : nullptr, false);
    engine.props().setValue ("fullscreen", shouldBeFullScreen);
    engine.props().saveIfNeeded();
}

//==============================================================================
void MainComponent::showSlotMenu (int slot)
{
    const auto types = engine.knownPlugins.getTypes();
    const bool empty = engine.isSlotEmpty (slot);

    PopupMenu pluginMenu;
    KnownPluginList::addToMenu (pluginMenu, types, KnownPluginList::sortByManufacturer);

    enum { openEditor = 1, toggleBypass, moveLeft, moveRight, removePlugin, scanPlugins };

    PopupMenu menu;
    menu.addSectionHeader ("Posto " + String (slot + 1) + (empty ? String() : " - " + engine.getSlotName (slot)));

    if (types.isEmpty())
    {
        menu.addItem (scanPlugins, "Nessun plugin trovato: cerca plugin...");
    }
    else if (empty)
    {
        menu.addSubMenu ("Scegli plugin", pluginMenu);
    }
    else
    {
        menu.addItem (openEditor, "Apri interfaccia del plugin");
        menu.addItem (toggleBypass, engine.getSlotUserBypass (slot) ? "Riattiva" : "Bypass");
        menu.addSubMenu ("Sostituisci con", pluginMenu);
        menu.addItem (moveLeft,  "Sposta indietro", slot > 0);
        menu.addItem (moveRight, "Sposta avanti",   slot < Engine::numSlots - 1);
        menu.addSeparator();
        menu.addItem (removePlugin, "Rimuovi");
    }

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (slotViews[slot]),
                        [this, slot, types] (int result)
    {
        if (result == 0)
            return;

        const int chosen = KnownPluginList::getIndexChosenByMenu (types, result);
        if (chosen >= 0)
        {
            String error;
            if (! engine.loadPluginIntoSlot (slot, types.getReference (chosen), error))
                showError ("Impossibile caricare il plugin", types.getReference (chosen).name + "\n\n" + error);
            return;
        }

        switch (result)
        {
            case openEditor:   openPluginWindow (engine.getSlotProcessor (slot)); break;
            case toggleBypass: engine.setSlotUserBypass (slot, ! engine.getSlotUserBypass (slot)); break;
            case moveLeft:     engine.swapSlots (slot, slot - 1); break;
            case moveRight:    engine.swapSlots (slot, slot + 1); break;
            case removePlugin: engine.clearSlot (slot); break;
            case scanPlugins:  showPluginList(); break;
            default: break;
        }
    });
}

void MainComponent::bigButtonPressed (int index)
{
    if (editMode)
        showBigButtonEditMenu (index);
    else
        engine.toggleButton (index);
}

void MainComponent::showBigButtonEditMenu (int index)
{
    const auto& button = engine.getButton (index);

    enum { renameId = 1, slotBase = 100 };

    PopupMenu menu;
    menu.addSectionHeader ("Pulsante " + String (index + 1) + "  (MIDI CC " + String (engine.getButtonCC (index)) + ")");
    menu.addItem (renameId, "Rinomina...");
    menu.addSectionHeader ("Posti che accende/spegne");

    for (int s = 0; s < Engine::numSlots; ++s)
    {
        const String name = engine.isSlotEmpty (s) ? String ("(vuoto)") : engine.getSlotName (s);
        menu.addItem (slotBase + s, String (s + 1) + ".  " + name, true, button.slots.contains (s));
    }

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (bigButtons[index]),
                        [this, index] (int result)
    {
        if (result == renameId)
        {
            askText ("Nome del pulsante", engine.getButton (index).label,
                     [this, index] (String text) { engine.setButtonLabel (index, text); });
        }
        else if (result >= slotBase)
        {
            const int s = result - slotBase;
            engine.setButtonControlsSlot (index, s, ! engine.getButton (index).slots.contains (s));
            showBigButtonEditMenu (index);   // riapri: comodo per assegnare più posti
        }
    });
}

//==============================================================================
void MainComponent::showPresetMenu()
{
    enum { save = 1, saveAs, newPreset, openFolder, presetBase = 1000 };

    const auto files = engine.getPresetFiles();
    const auto current = engine.getCurrentPresetFile();

    PopupMenu menu;
    menu.addItem (save, "Salva", current.existsAsFile());
    menu.addItem (saveAs, "Salva come...");
    menu.addItem (newPreset, "Nuovo preset vuoto");
    menu.addItem (openFolder, "Apri cartella dei preset");

    if (! files.isEmpty())
    {
        menu.addSectionHeader ("Preset");
        for (int i = 0; i < files.size(); ++i)
            menu.addItem (presetBase + i, files[i].getFileNameWithoutExtension(), true, files[i] == current);
    }

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (presetButton),
                        [this, files] (int result)
    {
        if (result == save)
        {
            engine.savePreset (engine.getCurrentPresetFile());
        }
        else if (result == saveAs)
        {
            askText ("Salva preset come", engine.getCurrentPresetFile().getFileNameWithoutExtension(),
                     [this] (String name)
            {
                const auto file = engine.getPresetFolder().getChildFile (File::createLegalFileName (name));
                if (! engine.savePreset (file))
                    showError ("Salvataggio non riuscito", file.getFullPathName());
            });
        }
        else if (result == newPreset)
        {
            engine.newEmptyPreset();
        }
        else if (result == openFolder)
        {
            engine.getPresetFolder().startAsProcess();
        }
        else if (result >= presetBase && result - presetBase < files.size())
        {
            String errors;
            if (! engine.loadPreset (files[result - presetBase], errors))
                showError ("Alcuni plugin non sono stati caricati", errors);
        }
    });
}

void MainComponent::showSettingsMenu()
{
    enum { audioSettings = 1, pluginList, midiMomentary, midiAbsolute, fullScreen, quit, inputBase = 100 };

    PopupMenu inputMenu;
    const int numInputs = engine.getNumDeviceInputs();
    for (int i = 0; i < jmax (1, numInputs); ++i)
        inputMenu.addItem (inputBase + i, "Ingresso " + String (i + 1), i < numInputs, engine.getInputChannel() == i);

    PopupMenu midiMenu;
    midiMenu.addItem (midiMomentary, "Ogni pressione alterna on/off", true, engine.getMidiMode() == Engine::MidiMode::momentary);
    midiMenu.addItem (midiAbsolute,  "Il valore decide (127 = on, 0 = off)", true, engine.getMidiMode() == Engine::MidiMode::absolute);
    midiMenu.addSeparator();
    midiMenu.addItem (-1, "Pulsanti: CC " + String (engine.getButtonCC (0)) + "-" + String (engine.getButtonCC (Engine::numButtons - 1))
                            + "   Accordatore: CC " + String (engine.getTunerCC()), false);
    midiMenu.addItem (-2, "Preset: CC " + String (engine.getNextPresetCC()) + " avanti, CC " + String (engine.getPrevPresetCC())
                            + " indietro, Program Change = n. preset", false);

    const bool isFullScreen = Desktop::getInstance().getKioskModeComponent() != nullptr;

    PopupMenu menu;
    menu.addItem (audioSettings, "Scheda audio...");
    menu.addItem (pluginList, "Cerca / gestisci plugin...");
    menu.addSubMenu ("Ingresso chitarra", inputMenu);
    menu.addSubMenu ("Pedaliera MIDI", midiMenu);
    menu.addItem (fullScreen, "Schermo intero", true, isFullScreen);
    menu.addSeparator();
    menu.addItem (quit, "Esci");

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (settingsButton),
                        [this, isFullScreen] (int result)
    {
        switch (result)
        {
            case audioSettings: showAudioSettings(); break;
            case pluginList:    showPluginList(); break;
            case midiMomentary: engine.setMidiMode (Engine::MidiMode::momentary); break;
            case midiAbsolute:  engine.setMidiMode (Engine::MidiMode::absolute); break;
            case fullScreen:    setFullScreen (! isFullScreen); break;
            case quit:          JUCEApplication::getInstance()->systemRequestedQuit(); break;
            default:
                if (result >= inputBase)
                    engine.setInputChannel (result - inputBase);
                break;
        }
    });
}

//==============================================================================
void MainComponent::launchDialog (const String& title, Component* content)
{
    DialogWindow::LaunchOptions options;
    options.content.setOwned (content);
    options.dialogTitle = title;
    options.dialogBackgroundColour = Palette::panel;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;

    if (auto* window = options.launchAsync())
        window->setAlwaysOnTop (true);
}

void MainComponent::showAudioSettings()
{
    auto* selector = new AudioDeviceSelectorComponent (engine.deviceManager, 1, 2, 2, 2,
                                                       true, false, false, false);
    selector->setItemHeight (36);
    selector->setSize (640, 560);
    launchDialog ("Scheda audio", selector);
}

void MainComponent::showPluginList()
{
    auto* list = new PluginListComponent (engine.formatManager, engine.knownPlugins,
                                          engine.getDeadMansPedalFile(), &engine.props(), true);
    list->setSize (760, 560);
    launchDialog ("Plugin (usa 'Opzioni' > 'Cerca nuovi plugin')", list);
}

void MainComponent::openPluginWindow (AudioProcessor* processor)
{
    if (processor == nullptr)
        return;

    if (auto it = pluginWindows.find (processor); it != pluginWindows.end())
    {
        it->second->toFront (true);
        return;
    }

    SafePointer<MainComponent> safeThis (this);
    pluginWindows[processor] = std::make_unique<PluginWindow> (*processor, [safeThis, processor]
    {
        // chiudi in modo asincrono: la finestra non può cancellarsi mentre gestisce il suo evento
        MessageManager::callAsync ([safeThis, processor]
        {
            if (safeThis != nullptr)
                safeThis->pluginWindows.erase (processor);
        });
    });
}

void MainComponent::askText (const String& title, const String& initial, std::function<void (String)> onOk)
{
    textInput = std::make_unique<AlertWindow> (title, String(), MessageBoxIconType::NoIcon);
    textInput->addTextEditor ("text", initial);
    textInput->addButton ("OK", 1, KeyPress (KeyPress::returnKey));
    textInput->addButton ("Annulla", 0, KeyPress (KeyPress::escapeKey));
    textInput->setAlwaysOnTop (true);

    SafePointer<MainComponent> safeThis (this);
    textInput->enterModalState (true, ModalCallbackFunction::create ([safeThis, onOk] (int result)
    {
        if (safeThis == nullptr || safeThis->textInput == nullptr)
            return;

        const auto text = safeThis->textInput->getTextEditorContents ("text").trim();
        safeThis->textInput->setVisible (false);

        if (result == 1 && text.isNotEmpty())
            onOk (text);
    }), false);
}

void MainComponent::showError (const String& title, const String& message)
{
    AlertWindow::showAsync (MessageBoxOptions()
                                .withIconType (MessageBoxIconType::WarningIcon)
                                .withTitle (title)
                                .withMessage (message)
                                .withButton ("OK", 1),
                            nullptr);
}
