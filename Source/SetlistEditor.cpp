#include "SetlistEditor.h"

using namespace juce;

namespace
{
    void styleEditor (TextEditor& editor, float fontSize)
    {
        editor.setFont (Fonts::text (fontSize));
        editor.setIndents (14, 12);
        editor.setColour (TextEditor::backgroundColourId, Palette::background);
        editor.setColour (TextEditor::textColourId, Palette::text);
        editor.setColour (TextEditor::outlineColourId, Palette::borderLight);
    }
}

//==============================================================================
/** Una canzone: numero, nome, preset, su, giù, togli. */
struct SetlistEditor::Row final : public Component
{
    Row (SetlistEditor& o, int i) : owner (o), index (i)
    {
        auto& entry = owner.entries.getReference (index);

        number.setText (String (index + 1).paddedLeft ('0', 2), dontSendNotification);
        number.setFont (Fonts::mono (16.0f));
        number.setColour (Label::textColourId, Palette::textDim);
        addAndMakeVisible (number);

        styleEditor (song, 18.0f);
        song.setText (entry.song, false);
        song.setTextToShowWhenEmpty ("Song name (optional)", Palette::textFaint);
        song.onTextChange = [this] { owner.entries.getReference (index).song = song.getText().trim(); };
        addAndMakeVisible (song);

        // i preset nell'ordine della cartella; uno che non esiste più resta visibile come "(missing)"
        int id = 1, selected = 0;
        for (const auto& name : owner.presetNames)
        {
            preset.addItem (name, id);
            if (name == entry.preset)
                selected = id;
            ++id;
        }

        if (selected == 0 && entry.preset.isNotEmpty())
        {
            preset.addItem (entry.preset + " (missing)", id);
            selected = id;
        }

        preset.setSelectedId (selected, dontSendNotification);
        preset.setTextWhenNothingSelected ("Choose a preset");
        preset.onChange = [this]
        {
            if (const int sel = preset.getSelectedId(); sel > 0 && sel <= owner.presetNames.size())
                owner.entries.getReference (index).preset = owner.presetNames[sel - 1];
        };
        addAndMakeVisible (preset);

        up.setTitle ("Move up");
        down.setTitle ("Move down");
        remove.setTitle ("Remove song");
        up.setEnabled (index > 0);
        down.setEnabled (index < owner.entries.size() - 1);
        up.onClick     = [this] { owner.move (index, -1); };
        down.onClick   = [this] { owner.move (index, 1); };
        remove.onClick = [this]
        {
            // questa riga verrà distrutta: togli la canzone dopo l'evento
            MessageManager::callAsync ([safe = SafePointer<SetlistEditor> (&owner), row = index]
            {
                if (safe != nullptr)
                {
                    safe->entries.remove (row);
                    safe->rebuildRows();
                }
            });
        };
        for (auto* b : { &up, &down, &remove })
            addAndMakeVisible (b);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (0, 4);
        number.setBounds (area.removeFromLeft (40));
        remove.setBounds (area.removeFromRight (52));
        area.removeFromRight (8);
        down.setBounds (area.removeFromRight (52));
        area.removeFromRight (6);
        up.setBounds (area.removeFromRight (52));
        area.removeFromRight (12);
        preset.setBounds (area.removeFromRight (jmin (300, area.getWidth() / 2)));
        area.removeFromRight (12);
        song.setBounds (area);
    }

    SetlistEditor& owner;
    const int index;
    Label number;
    TextEditor song;
    ComboBox preset;
    StageButton up { "", Icons::Ui::up }, down { "", Icons::Ui::down }, remove { "", Icons::Ui::close };
};

//==============================================================================
SetlistEditor::SetlistEditor (Engine& e, const File& existingFile) : engine (e), originalFile (existingFile)
{
    for (const auto& f : engine.getPresetFiles())
        presetNames.add (f.getFileNameWithoutExtension());

    if (existingFile.existsAsFile())
        entries = Engine::readSetlist (existingFile);

    title.setText (existingFile.existsAsFile() ? "Edit setlist" : "New setlist", dontSendNotification);
    title.setFont (Fonts::display (28.0f));
    title.setColour (Label::textColourId, Palette::text);
    addAndMakeVisible (title);

    nameLabel.setText ("Name", dontSendNotification);
    nameLabel.setFont (Fonts::display (14.0f));
    nameLabel.setColour (Label::textColourId, Palette::textDim);
    addAndMakeVisible (nameLabel);

    styleEditor (nameEditor, 20.0f);
    nameEditor.setText (existingFile.getFileNameWithoutExtension(), false);
    nameEditor.setTextToShowWhenEmpty ("e.g. Friday gig", Palette::textFaint);
    addAndMakeVisible (nameEditor);

    hint.setText (String::fromUTF8 ("< > and the MIDI next/previous pedals follow this order. Program Change 0 = first song. "
                                    "A preset can be used by more than one song."),
                  dontSendNotification);
    hint.setFont (Fonts::text (14.0f));
    hint.setColour (Label::textColourId, Palette::textFaint);
    addAndMakeVisible (hint);

    viewport.setViewedComponent (&rowsHolder, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (10);
    addAndMakeVisible (viewport);

    addButton.onClick = [this]
    {
        // la nuova canzone parte dal preset caricato: il caso più comune quando la scaletta si costruisce provando
        entries.add ({ {}, engine.getCurrentPresetFile().getFileNameWithoutExtension() });
        rebuildRows();
        viewport.setViewPosition (0, rowsHolder.getHeight());
        if (auto* last = rows.getLast())
            last->song.grabKeyboardFocus();
    };
    addAndMakeVisible (addButton);

    saveButton.setColour (TextButton::buttonColourId, Palette::accent);
    saveButton.setColour (TextButton::textColourOffId, Palette::background);
    saveButton.onClick = [this] { save(); };
    addAndMakeVisible (saveButton);

    cancelButton.onClick = [this] { close(); };
    addAndMakeVisible (cancelButton);

    rebuildRows();
    setSize (900, 760);
}

void SetlistEditor::rebuildRows()
{
    rows.clear();

    for (int i = 0; i < entries.size(); ++i)
        rowsHolder.addAndMakeVisible (rows.add (new Row (*this, i)));

    resized();
}

void SetlistEditor::move (int index, int delta)
{
    const int target = index + delta;

    if (! isPositiveAndBelow (index, entries.size()) || ! isPositiveAndBelow (target, entries.size()))
        return;

    // il pulsante premuto appartiene a una riga che verrà distrutta: ricostruisci dopo
    MessageManager::callAsync ([safe = SafePointer<SetlistEditor> (this), index, target]
    {
        if (safe != nullptr)
        {
            safe->entries.swap (index, target);
            safe->rebuildRows();
        }
    });
}

void SetlistEditor::save()
{
    const auto name = File::createLegalFileName (nameEditor.getText().trim());

    if (name.isEmpty())
    {
        nameEditor.grabKeyboardFocus();
        return;
    }

    // canzoni senza preset: non si possono suonare, meglio non salvarle
    Array<Engine::SetlistEntry> toSave;
    for (const auto& e : entries)
        if (e.preset.isNotEmpty())
            toSave.add (e);

    const auto file = engine.getSetlistFolder().getChildFile (name + ".chainset");

    if (! engine.saveSetlist (file, toSave))
    {
        AlertWindow::showAsync (MessageBoxOptions().withIconType (MessageBoxIconType::WarningIcon)
                                    .withTitle ("Could not save the setlist")
                                    .withMessage (file.getFullPathName())
                                    .withButton ("OK", 1), nullptr);
        return;
    }

    // rinominata: il vecchio file non serve più
    if (originalFile.existsAsFile() && originalFile != file)
        originalFile.deleteFile();

    close();
}

void SetlistEditor::close()
{
    if (auto* window = findParentComponentOfClass<DialogWindow>())
        window->exitModalState (0);
}

void SetlistEditor::paint (Graphics& g)
{
    g.fillAll (Palette::cardOff);
    g.setColour (Palette::border);
    g.drawHorizontalLine (saveButton.getY() - 14, 24.0f, (float) getWidth() - 24.0f);
}

void SetlistEditor::resized()
{
    auto area = getLocalBounds().reduced (24);

    title.setBounds (area.removeFromTop (40));
    area.removeFromTop (12);

    auto nameRow = area.removeFromTop (56);
    nameLabel.setBounds (nameRow.removeFromLeft (70));
    nameEditor.setBounds (nameRow);
    area.removeFromTop (8);
    hint.setBounds (area.removeFromTop (40));
    area.removeFromTop (8);

    auto bottom = area.removeFromBottom (56);
    area.removeFromBottom (28);
    saveButton.setBounds (bottom.removeFromRight (200));
    bottom.removeFromRight (12);
    cancelButton.setBounds (bottom.removeFromRight (150));
    addButton.setBounds (bottom.removeFromLeft (200));

    viewport.setBounds (area);

    const int rowHeight = 64;
    const int width = area.getWidth() - viewport.getScrollBarThickness() - 4;
    rowsHolder.setSize (width, jmax (area.getHeight(), rows.size() * rowHeight));

    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, i * rowHeight, width, rowHeight);
}
