#pragma once

#include "Engine.h"
#include <atomic>

//==============================================================================
/** Finestra "Scegli plugin": elenca i file VST3 delle cartelle dei plugin con una
    casella ciascuno. Con "Applica" vengono esaminati (fuori processo) solo quelli
    spuntati e tolti dalla lista quelli non spuntati. Così i plugin che non servono
    dal vivo non vengono mai aperti. */
class PluginChooser final : public juce::Component,
                            private juce::ListBoxModel,
                            private juce::Thread,
                            private juce::Timer
{
public:
    PluginChooser (Engine&, std::function<void()> showAdvancedList);
    ~PluginChooser() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum class State { notInList, inList, excluded };

    struct Entry
    {
        juce::String file, name;
        State state = State::notInList;
        bool chosen = false;
    };

    // ListBoxModel
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;

    // Thread: esamina i file spuntati che non sono ancora nella lista
    void run() override;
    void timerCallback() override;

    juce::AudioPluginFormat* getFormat() const;
    void findFiles();
    void updateStates();
    void applyFilter();
    void apply();
    void setBusy (bool);
    void updateSummary();

    Engine& engine;
    std::function<void()> showAdvancedList;

    juce::Array<Entry> entries;
    juce::Array<int> visibleRows;   // indici in `entries` che passano il filtro

    juce::Label title, summary;
    juce::TextEditor filter;
    juce::ListBox listBox { {}, this };
    juce::TextButton applyButton { "APPLICA" }, advancedButton { "Avanzate..." }, noneButton { "Nessuno" };

    // Stato della ricerca, letto dal Timer sul thread dell'interfaccia
    juce::StringArray filesToScan;
    std::atomic<int> scanIndex { 0 };
    juce::CriticalSection nameLock;
    juce::String scanningName;
    int removedCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginChooser)
};
