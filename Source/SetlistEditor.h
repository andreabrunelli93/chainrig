#pragma once

#include "Engine.h"
#include "Look.h"

//==============================================================================
/** Finestra per creare o modificare una scaletta: nome, poi una riga per canzone
    (nome della canzone, preset, su/giù, togli). SALVA scrive il file e la rende attiva. */
class SetlistEditor final : public juce::Component
{
public:
    /** file vuoto = nuova scaletta. */
    SetlistEditor (Engine&, const juce::File& existingFile);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Row;

    void rebuildRows();
    void move (int index, int delta);
    void save();
    void close();

    Engine& engine;
    juce::File originalFile;
    juce::Array<Engine::SetlistEntry> entries;
    juce::StringArray presetNames;

    juce::Label title, nameLabel, hint;
    juce::TextEditor nameEditor;
    juce::Viewport viewport;
    juce::Component rowsHolder;
    juce::OwnedArray<Row> rows;
    juce::TextButton addButton { "+ ADD SONG" }, saveButton { "SAVE" }, cancelButton { "Cancel" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SetlistEditor)
};
