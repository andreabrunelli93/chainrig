#include "MainComponent.h"

using namespace juce;

//==============================================================================
class MainWindow final : public DocumentWindow
{
public:
    explicit MainWindow (Engine& engine)
        : DocumentWindow ("ChainRig", Palette::background, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);

        auto* content = new MainComponent (engine);
        setContentOwned (content, true);
        setResizable (true, true);
        setResizeLimits (900, 600, 10000, 10000);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);

        if (engine.props().getBoolValue ("fullscreen", false))
            content->setFullScreen (true);
    }

    ~MainWindow() override
    {
        if (Desktop::getInstance().getKioskModeComponent() == this)
            Desktop::getInstance().setKioskModeComponent (nullptr, false);
    }

    void closeButtonPressed() override
    {
        JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};

//==============================================================================
class ChainRigApplication final : public JUCEApplication
{
public:
    const String getApplicationName() override       { return JUCE_APPLICATION_NAME_STRING; }
    const String getApplicationVersion() override    { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override       { return false; }

    void initialise (const String&) override
    {
        engine = std::make_unique<Engine>();
        mainWindow = std::make_unique<MainWindow> (*engine);
    }

    void shutdown() override
    {
        mainWindow.reset();   // prima l'interfaccia (chiude le finestre dei plugin)...
        engine.reset();       // ...poi il motore audio
    }

    void systemRequestedQuit() override              { quit(); }
    void anotherInstanceStarted (const String&) override {}

private:
    std::unique_ptr<Engine> engine;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (ChainRigApplication)
