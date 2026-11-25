#include "PluginChooser.h"
#include "MainComponent.h"   // Palette

using namespace juce;

//==============================================================================
PluginChooser::PluginChooser (Engine& e, std::function<void()> advanced)
    : Thread ("Ricerca plugin scelti"), engine (e), showAdvancedList (std::move (advanced))
{
    title.setText ("Scegli i plugin per il palco", dontSendNotification);
    title.setFont (Fonts::display (28.0f));
    title.setColour (Label::textColourId, Palette::text);
    addAndMakeVisible (title);

    summary.setFont (Fonts::mono (13.0f));
    summary.setColour (Label::textColourId, Palette::textDim);
    addAndMakeVisible (summary);

    filter.setFont (Fonts::text (20.0f));
    filter.setIndents (16, 14);
    filter.setTextToShowWhenEmpty ("Filtra per nome (es. valhalla, tone)", Palette::textFaint);
    filter.setColour (TextEditor::backgroundColourId, Palette::background);
    filter.setColour (TextEditor::textColourId, Palette::text);
    filter.setColour (TextEditor::outlineColourId, Palette::borderLight);
    filter.onTextChange = [this] { applyFilter(); };
    addAndMakeVisible (filter);

    listBox.setRowHeight (60);
    listBox.setColour (ListBox::backgroundColourId, Palette::cardOff);
    addAndMakeVisible (listBox);

    applyButton.setColour (TextButton::buttonColourId, Palette::accent);
    applyButton.setColour (TextButton::textColourOffId, Palette::background);
    applyButton.onClick = [this] { apply(); };
    addAndMakeVisible (applyButton);

    advancedButton.onClick = [this] { if (showAdvancedList) showAdvancedList(); };
    addAndMakeVisible (advancedButton);

    // Toglie la spunta a tutti i plugin visibili (rispetta il filtro)
    noneButton.onClick = [this]
    {
        for (auto index : visibleRows)
            entries.getReference (index).chosen = false;

        listBox.repaint();
        updateSummary();
    };
    addAndMakeVisible (noneButton);

    findFiles();
    setSize (880, 760);
}

PluginChooser::~PluginChooser()
{
    // Lo scanner controlla currentThreadShouldExit(): il plugin in esame viene chiuso subito
    stopThread (5000);
}

AudioPluginFormat* PluginChooser::getFormat() const
{
    for (auto* format : engine.formatManager.getFormats())
        if (format->getName() == "VST3")
            return format;

    return nullptr;
}

void PluginChooser::findFiles()
{
    entries.clear();

    auto* format = getFormat();
    if (format == nullptr)
        return;

    auto path = PluginListComponent::getLastSearchPath (engine.props(), *format);
    if (path.getNumPaths() == 0)
        path = format->getDefaultLocationsToSearch();

    for (auto& file : format->searchPathsForPlugins (path, true, true))
    {
        Entry entry;
        entry.file = file;
        entry.name = File::createFileWithoutCheckingPath (file).getFileNameWithoutExtension();
        entries.add (entry);
    }

    std::sort (entries.begin(), entries.end(), [] (const Entry& a, const Entry& b)
    {
        return a.name.compareNatural (b.name) < 0;
    });

    updateStates();

    for (auto& entry : entries)
        entry.chosen = (entry.state == State::inList);

    applyFilter();
}

void PluginChooser::updateStates()
{
    const auto& blacklist = engine.knownPlugins.getBlacklistedFiles();

    for (auto& entry : entries)
    {
        if (engine.knownPlugins.getTypeForFile (entry.file) != nullptr)
            entry.state = State::inList;
        else if (blacklist.contains (entry.file))
            entry.state = State::excluded;
        else
            entry.state = State::notInList;
    }
}

void PluginChooser::applyFilter()
{
    visibleRows.clear();
    const auto text = filter.getText().trim();

    for (int i = 0; i < entries.size(); ++i)
        if (text.isEmpty() || entries.getReference (i).name.containsIgnoreCase (text))
            visibleRows.add (i);

    listBox.updateContent();
    listBox.repaint();
    updateSummary();
}

void PluginChooser::updateSummary()
{
    if (isThreadRunning())
        return;

    int chosen = 0;
    for (auto& entry : entries)
        chosen += entry.chosen ? 1 : 0;

    summary.setText (String (chosen) + " spuntati su " + String (entries.size())
                         + " file VST3. Premi APPLICA per aggiornare la lista.",
                     dontSendNotification);
}

//==============================================================================
int PluginChooser::getNumRows()
{
    return visibleRows.size();
}

void PluginChooser::paintListBoxItem (int row, Graphics& g, int width, int height, bool)
{
    if (! isPositiveAndBelow (row, visibleRows.size()))
        return;

    const auto& entry = entries.getReference (visibleRows[row]);
    auto bounds = Rectangle<float> ((float) width, (float) height).reduced (6.0f, 3.0f);

    if (entry.chosen)
    {
        g.setColour (Palette::panelLight);
        g.fillRoundedRectangle (bounds, 12.0f);
        g.setColour (Palette::borderLight);
        g.drawRoundedRectangle (bounds, 12.0f, 1.0f);
    }

    auto area = bounds.reduced (16.0f, 0.0f);

    // casella
    auto box = area.removeFromLeft (28.0f).withSizeKeepingCentre (28.0f, 28.0f);
    if (entry.chosen)
    {
        g.setColour (Palette::accent);
        g.fillRoundedRectangle (box, 7.0f);
        Path tick;
        tick.startNewSubPath (box.getX() + 7.0f, box.getCentreY());
        tick.lineTo (box.getX() + 12.0f, box.getBottom() - 8.0f);
        tick.lineTo (box.getRight() - 6.0f, box.getY() + 8.0f);
        g.setColour (Palette::background);
        g.strokePath (tick, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    else
    {
        g.setColour (Palette::off);
        g.drawRoundedRectangle (box.reduced (1.0f), 7.0f, 2.0f);
    }
    area.removeFromLeft (16.0f);

    // cosa succede premendo APPLICA
    String status;
    Colour statusColour = Palette::textDim;

    if (entry.state == State::inList)
    {
        status = entry.chosen ? "NELLA LISTA" : "DA TOGLIERE";
        statusColour = entry.chosen ? Palette::on : Colour (0xffff8a7a);
    }
    else if (entry.chosen)
    {
        status = entry.state == State::excluded ? "DA RIPROVARE" : "DA AGGIUNGERE";
        statusColour = Palette::accent;
    }
    else if (entry.state == State::excluded)
    {
        status = "ESCLUSO: SI BLOCCA O VA IN CRASH";
        statusColour = Palette::danger;
    }

    g.setFont (Fonts::display (11.0f));
    g.setColour (statusColour);
    g.drawText (status, area.removeFromRight (280.0f), Justification::centredRight);

    g.setFont (Fonts::text (19.0f, entry.chosen));
    g.setColour (entry.chosen ? Palette::text : Colour (0xffb9bcc3));
    g.drawText (entry.name, area.withTrimmedRight (12.0f), Justification::centredLeft, true);
}

void PluginChooser::listBoxItemClicked (int row, const MouseEvent&)
{
    if (isThreadRunning() || ! isPositiveAndBelow (row, visibleRows.size()))
        return;

    auto& entry = entries.getReference (visibleRows[row]);
    entry.chosen = ! entry.chosen;
    listBox.repaintRow (row);
    updateSummary();
}

//==============================================================================
void PluginChooser::apply()
{
    if (isThreadRunning())
        return;

    // Toglie dalla lista i plugin dei file non spuntati
    removedCount = 0;

    for (auto& entry : entries)
    {
        if (entry.chosen || entry.state != State::inList)
            continue;

        for (auto& type : engine.knownPlugins.getTypes())
        {
            if (type.fileOrIdentifier == entry.file)
            {
                engine.knownPlugins.removeType (type);
                ++removedCount;
            }
        }
    }

    // Esamina quelli spuntati che mancano (anche gli esclusi: l'utente vuole riprovarli)
    filesToScan.clear();

    for (auto& entry : entries)
    {
        if (! entry.chosen || entry.state == State::inList)
            continue;

        if (entry.state == State::excluded)
            engine.knownPlugins.removeFromBlacklist (entry.file);

        filesToScan.add (entry.file);
    }

    scanIndex = 0;
    setBusy (true);
    startThread();
    startTimer (200);
}

void PluginChooser::run()
{
    auto* format = getFormat();

    for (int i = 0; i < filesToScan.size() && format != nullptr && ! threadShouldExit(); ++i)
    {
        {
            const ScopedLock sl (nameLock);
            scanningName = File::createFileWithoutCheckingPath (filesToScan[i]).getFileNameWithoutExtension();
        }

        scanIndex = i;

        OwnedArray<PluginDescription> found;
        engine.knownPlugins.scanAndAddFile (filesToScan[i], true, found, *format);
    }

    scanIndex = filesToScan.size();
    engine.knownPlugins.scanFinished();
}

void PluginChooser::timerCallback()
{
    if (isThreadRunning())
    {
        String name;
        {
            const ScopedLock sl (nameLock);
            name = scanningName;
        }

        summary.setText ("Esamino " + String (scanIndex.load() + 1) + " di " + String (filesToScan.size())
                             + ": " + name + "  (al massimo 30 s per plugin)",
                         dontSendNotification);
        return;
    }

    stopTimer();
    setBusy (false);
    updateStates();

    int added = 0, failed = 0;

    for (auto& entry : entries)
    {
        if (! filesToScan.contains (entry.file))
            continue;

        if (entry.state == State::inList)
            ++added;
        else
            ++failed;

        entry.chosen = (entry.state == State::inList);
    }

    listBox.repaint();

    String text = "Fatto: " + String (added) + " aggiunti";
    if (removedCount > 0) text << ", " << removedCount << " tolti";
    if (failed > 0)       text << ", " << failed << " non caricati (vedi scan.log)";
    summary.setText (text + ".", dontSendNotification);
}

void PluginChooser::setBusy (bool busy)
{
    applyButton.setEnabled (! busy);
    advancedButton.setEnabled (! busy);
    filter.setEnabled (! busy);
    noneButton.setEnabled (! busy);
    applyButton.setButtonText (busy ? "ATTENDI..." : "APPLICA");
}

//==============================================================================
void PluginChooser::paint (Graphics& g)
{
    g.fillAll (Palette::cardOff);

    // linea sopra i pulsanti
    g.setColour (Palette::border);
    g.drawHorizontalLine (applyButton.getY() - 14, 24.0f, (float) getWidth() - 24.0f);
}

void PluginChooser::resized()
{
    auto area = getLocalBounds().reduced (24);

    title.setBounds (area.removeFromTop (40));
    area.removeFromTop (12);
    filter.setBounds (area.removeFromTop (56));
    area.removeFromTop (14);

    auto bottom = area.removeFromBottom (56);
    area.removeFromBottom (14);
    applyButton.setBounds (bottom.removeFromRight (200));
    bottom.removeFromRight (12);
    advancedButton.setBounds (bottom.removeFromRight (150));
    noneButton.setBounds (bottom.removeFromLeft (150));

    area.removeFromBottom (8);
    summary.setBounds (area.removeFromBottom (28));
    area.removeFromBottom (4);
    listBox.setBounds (area);
}
