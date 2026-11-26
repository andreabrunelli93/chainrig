#pragma once

#include "Engine.h"
#include "Look.h"
#include "UpdateChecker.h"
#include <map>

class MainComponent;

//==============================================================================
/** Un posto della catena: numero, tipo con icona, nome, stato (attivo / bypass / spento). */
class SlotView final : public juce::Component
{
public:
    SlotView (MainComponent& o, int i) : owner (o), index (i) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;   // trascinare = spostare il posto
    void mouseUp (const juce::MouseEvent&) override;

private:
    MainComponent& owner;
    const int index;
    bool dragging = false;
};

//==============================================================================
/** Uno dei 4 pedali grandi: LED, nome, posti controllati e CC. */
class BigButton final : public juce::Component
{
public:
    BigButton (MainComponent& o, int i) : owner (o), index (i) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;   // reagisce al tocco, non al rilascio

private:
    MainComponent& owner;
    const int index;
};

//==============================================================================
/** Preset: freccia indietro | numero e nome | freccia avanti, in un unico pannello. */
class PresetSelector final : public juce::Component
{
public:
    std::function<void()> onPrev, onNext, onMenu;

    /** position: riga piccola in alto ("PRESET 01 / 05", "SONG 03 / 12 · CLEAN"). */
    void setPreset (const juce::String& name, const juce::String& position, bool unsaved);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    int zoneAt (juce::Point<int>) const;   // -1 indietro, 0 nome, 1 avanti

    juce::String name, position;
    bool unsaved = false;
    int pressedZone = -2;
};

//==============================================================================
/** Scheda audio e latenza, su due righe compatte allineate a destra. */
class StatusDisplay final : public juce::Component
{
public:
    std::function<void()> onClick;   // tocco sulla seconda riga quando mostra un avviso

    void setLines (const juce::String& first, const juce::String& second, bool highlightFirst, bool highlightSecond = false);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::String line1, line2;
    bool highlighted = false, secondHighlighted = false;
};

//==============================================================================
/** Barra laterale con il livello d'uscita (L/R), la tenuta del picco, la spia CLIP
    e la manopola del volume generale. La spia resta accesa finché non la tocchi. */
class OutputMeter final : public juce::Component, private juce::Timer
{
public:
    explicit OutputMeter (Engine&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    Engine& engine;
    juce::Slider volumeKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Rectangle<float> meterArea, volumeTextArea;
    float level[2] {}, hold[2] {};
    juce::uint32 holdTime[2] {};
    float maxPeak = 0.0f;      // massimo dall'ultimo tocco, in lineare
    bool clipLatched = false;
};

//==============================================================================
/** Barra a sinistra: livello d'ingresso (dopo il gain) con spia CLIP, manopola del gain e MUTE. */
class InputStrip final : public juce::Component, private juce::Timer
{
public:
    explicit InputStrip (Engine&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;   // tocco sulla barra: azzera CLIP e picco
    void refresh();

private:
    void timerCallback() override;

    Engine& engine;
    juce::Slider gainKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    StageButton muteButton { "MUTE", std::nullopt, StageButton::Style::danger };
    juce::Rectangle<float> meterArea, valueArea, gainTextArea;
    float level = 0.0f, hold = 0.0f, maxPeak = 0.0f;
    juce::uint32 holdTime = 0;
    bool clipLatched = false;
};

//==============================================================================
class TunerOverlay final : public juce::Component, private juce::Timer
{
public:
    explicit TunerOverlay (Engine& e) : engine (e) {}
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void visibilityChanged() override;

private:
    void timerCallback() override;

    Engine& engine;
    std::vector<float> history, incoming;
    juce::Array<float> recent;           // ultime frequenze valide, per la mediana
    float displayFreq = 0.0f;
    int framesWithoutSignal = 0;
};

//==============================================================================
class PluginWindow final : public juce::DocumentWindow
{
public:
    PluginWindow (juce::AudioProcessor&, std::function<void()> onClose);
    void closeButtonPressed() override;

private:
    std::function<void()> onClose;
};

//==============================================================================
class MainComponent final : public juce::Component,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    explicit MainComponent (Engine&);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    Engine& getEngine()                 { return engine; }
    bool isEditMode() const             { return editMode; }

    void showSlotMenu (int slot);

    // Trascinamento dei posti (coordinate di MainComponent)
    void slotDragStarted (int slot, juce::Point<int> grabOffset);
    void slotDragged (juce::Point<int> position);
    void slotDropped();
    int getDragSource() const           { return dragSource; }
    void paintOverChildren (juce::Graphics&) override;
    void bigButtonPressed (int index);
    void setFullScreen (bool);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void refresh();
    void showPresetMenu();
    void showSettingsMenu();
    void showSetlistEditor (const juce::File& existing);
    static void openDonationPage();
    void showBigButtonEditMenu (int index);
    void showAudioSettings();
    void showPluginChooser();
    void showPluginList();
    void openPluginWindow (juce::AudioProcessor*);
    void askText (const juce::String& title, const juce::String& initial, std::function<void (juce::String)> onOk,
                  bool allowEmpty = false);
    void showError (const juce::String& title, const juce::String& message);
    void launchDialog (const juce::String& title, juce::Component* content);
    void toggleRecording();
    void updateRecButton();
    void updateStatus();

    /** Esegue `switchPreset` subito, oppure prima chiede se salvare le modifiche. */
    void confirmPresetSwitch (std::function<void()> switchPreset);
    void saveCurrentPresetAs (std::function<void()> afterSave);

    // Aggiornamenti: controllo dell'ultima release su GitHub
    juce::String getCurrentVersion() const;
    void checkForUpdates (bool manual);
    void showUpdateDialog();
    UpdateChecker updateChecker;
    UpdateChecker::Result availableUpdate;   // .newer = c'è un avviso da mostrare

    StageLookAndFeel lookAndFeel;   // deve vivere più a lungo di tutti i componenti figli
    Engine& engine;
    bool editMode = false;

    PresetSelector presetSelector;
    StageButton tunerButton    { "TUNER", Icons::Ui::tuningFork },
                recButton      { "REC", std::nullopt, StageButton::Style::record },
                tapButton      { "TAP" },
                editButton     { "EDIT" },
                settingsButton { "", Icons::Ui::settings };
    StatusDisplay statusDisplay;
    juce::Rectangle<int> chainHeader;      // "SIGNAL CHAIN" e riepilogo

    int dragSource = -1, dragTarget = -1;
    juce::Point<int> dragPosition, dragGrabOffset;
    juce::Image dragImage;
    int slotAt (juce::Point<int>) const;

    juce::File lastRecording;              // ultimo file salvato, mostrato per qualche secondo
    juce::uint32 lastRecordingShownUntil = 0;

    juce::OwnedArray<SlotView> slotViews;
    juce::OwnedArray<BigButton> bigButtons;
    TunerOverlay tunerOverlay { engine };
    OutputMeter outputMeter { engine };
    struct CoffeeButton final : juce::Button   // sotto il misuratore: logo Buy Me a Coffee, apre la pagina delle donazioni
    {
        CoffeeButton();
        void paintButton (juce::Graphics&, bool isMouseOver, bool isDown) override;
        juce::Image logo;
    } coffeeButton;
    InputStrip inputStrip { engine };

    std::map<juce::AudioProcessor*, std::unique_ptr<PluginWindow>> pluginWindows;
    std::unique_ptr<juce::AlertWindow> textInput;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
