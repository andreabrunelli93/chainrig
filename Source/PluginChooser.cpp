#include "PluginChooser.h"
#include "MainComponent.h"   // Palette

using namespace juce;

//==============================================================================
PluginChooser::PluginChooser (Engine& e, std::function<void()> advanced)
    : Thread ("Ricerca plugin scelti"), engine (e), showAdvancedList (std::move (advanced))
{
    title.setText ("Spunta i plugin che vuoi usare dal vivo", dontSendNotification);
    title.setFont (FontOptions (20.0f, Font::bold));
    title.setColour (Label::textColourId, Palette::text);
    addAndMakeVisible (title);

    summary.setFont (FontOptions (16.0f));
    summary.setColour (Label::textColourId, Palette::textDim);
    addAndMakeVisible (summary);

    filter.setFont (FontOptions (20.0f));
    filter.setTextToShowWhenEmpty ("Filtra per nome (es. valhalla, tone)", Palette::textDim);
    filter.setColour (TextEditor::backgroundColourId, Palette::panelLight);
    filter.setColour (TextEditor::textColourId, Palette::text);
    filter.setColour (TextEditor::outlineColourId, Palette::off);
    filter.onTextChange = [this] { applyFilter(); };
    addAndMakeVisible (filter);

    listBox.setRowHeight (48);
    listBox.setColour (ListBox::backgroundColourId, Palette::panel);
    addAndMakeVisible (listBox);

    applyButton.setColour (TextButton::buttonColourId, Palette::accent);
    applyButton.setColour (TextButton::textColourOffId, Colours::black);
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
    setSize (720, 640);
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
    auto bounds = Rectangle<int> (width, height);

    if (row % 2 == 1)
    {
        g.setColour (Palette::panelLight.withAlpha (0.35f));
        g.fillRect (bounds);
    }

    // casella
    auto box = bounds.removeFromLeft (height).reduced (11).toFloat();
    g.setColour (entry.chosen ? Palette::accent : Palette::off);
    if (entry.chosen)
    {
        g.fillRoundedRectangle (box, 5.0f);
        Path tick;
        tick.startNewSubPath (box.getX() + box.getWidth() * 0.22f, box.getCentreY());
        tick.lineTo (box.getX() + box.getWidth() * 0.42f, box.getBottom() - box.getHeight() * 0.25f);
        tick.lineTo (box.getRight() - box.getWidth() * 0.2f, box.getY() + box.getHeight() * 0.25f);
        g.setColour (Colours::black);
        g.strokePath (tick, PathStrokeType (3.0f));
    }
    else
    {
        g.drawRoundedRectangle (box, 5.0f, 2.0f);
    }

    // stato a destra
    String status;
    Colour statusColour = Palette::textDim;

    switch (entry.state)
    {
        case State::inList:   status = "nella lista"; statusColour = Palette::on; break;
        case State::excluded: status = "escluso: si blocca o va in crash"; statusColour = Palette::danger; break;
        case State::notInList: break;
    }

    g.setFont (FontOptions (15.0f));
    g.setColour (statusColour);
    g.drawText (status, bounds.removeFromRight (260).withTrimmedRight (12), Justification::centredRight);

    g.setFont (FontOptions (19.0f));
    g.setColour (Palette::text);
    g.drawText (entry.name, bounds.withTrimmedLeft (4), Justification::centredLeft, true);
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
    g.fillAll (Palette::panel);
}

void PluginChooser::resized()
{
    auto area = getLocalBounds().reduced (16);

    title.setBounds (area.removeFromTop (32));
    area.removeFromTop (8);
    filter.setBounds (area.removeFromTop (44));
    area.removeFromTop (8);

    auto bottom = area.removeFromBottom (56);
    applyButton.setBounds (bottom.removeFromRight (200));
    bottom.removeFromRight (12);
    advancedButton.setBounds (bottom.removeFromRight (150));
    noneButton.setBounds (bottom.removeFromLeft (150));

    area.removeFromBottom (8);
    summary.setBounds (area.removeFromBottom (28));
    area.removeFromBottom (4);
    listBox.setBounds (area);
}
