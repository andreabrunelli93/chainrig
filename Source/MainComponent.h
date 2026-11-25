#pragma once

#include "Engine.h"
#include <map>

//==============================================================================
/** Aspetto scuro con testi e menu grandi, pensato per il touch sul palco. */
class StageLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    StageLookAndFeel();

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getPopupMenuFont() override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                    int& idealWidth, int& idealHeight) override;
};

//==============================================================================
namespace Palette
{
    const juce::Colour background   { 0xff121419 };
    const juce::Colour panel        { 0xff1e2129 };
    const juce::Colour panelLight   { 0xff2a2e38 };
    const juce::Colour text         { 0xffe8e9ed };
    const juce::Colour textDim      { 0xff8a8f9c };
    const juce::Colour on           { 0xff3ddc84 };
    const juce::Colour off          { 0xff4a4f5c };
    const juce::Colour accent       { 0xffffb347 };
    const juce::Colour danger       { 0xffff5c5c };
}

class MainComponent;

//==============================================================================
class SlotView final : public juce::Component
{
public:
    SlotView (MainComponent& o, int i) : owner (o), index (i) {}
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    MainComponent& owner;
    const int index;
};

//==============================================================================
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
    void bigButtonPressed (int index);
    void setFullScreen (bool);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void refresh();
    void showPresetMenu();
    void showSettingsMenu();
    void showBigButtonEditMenu (int index);
    void showAudioSettings();
    void showPluginChooser();
    void showPluginList();
    void openPluginWindow (juce::AudioProcessor*);
    void askText (const juce::String& title, const juce::String& initial, std::function<void (juce::String)> onOk);
    void showError (const juce::String& title, const juce::String& message);
    void launchDialog (const juce::String& title, juce::Component* content);

    StageLookAndFeel lookAndFeel;   // deve vivere più a lungo di tutti i componenti figli
    Engine& engine;
    bool editMode = false;

    juce::TextButton prevButton { "<" }, nextButton { ">" }, presetButton, tunerButton { "ACCORDATORE" },
                     editButton { "MODIFICA" }, settingsButton { "IMPOSTAZIONI" };
    juce::Label statusLabel;

    juce::OwnedArray<SlotView> slotViews;
    juce::OwnedArray<BigButton> bigButtons;
    TunerOverlay tunerOverlay { engine };

    std::map<juce::AudioProcessor*, std::unique_ptr<PluginWindow>> pluginWindows;
    std::unique_ptr<juce::AlertWindow> textInput;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
