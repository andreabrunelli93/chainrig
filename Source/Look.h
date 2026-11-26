#pragma once

#include "Engine.h"
#include <optional>

//==============================================================================
/** Colori dell'interfaccia: grafite quasi nero e un solo accento (ambra) per ciò che suona. */
namespace Palette
{
    const juce::Colour background  { 0xff0b0c0e };
    const juce::Colour panel       { 0xff15171a };   // barra in alto, finestre
    const juce::Colour panelLight  { 0xff1d2024 };   // posto attivo (alto)
    const juce::Colour card        { 0xff16181b };   // posto attivo (basso)
    const juce::Colour cardOff     { 0xff121417 };   // posto spento, misuratore
    const juce::Colour border      { 0xff23262b };
    const juce::Colour borderLight { 0xff2c3036 };
    const juce::Colour text        { 0xffe9e6df };
    const juce::Colour textDim     { 0xff8d9098 };
    const juce::Colour textFaint   { 0xff6d7078 };
    const juce::Colour accent      { 0xfff2a93b };   // acceso / attivo
    const juce::Colour on          { 0xff3ecf7a };   // intonato, livello sano, "nella lista"
    const juce::Colour off         { 0xff3a3e45 };
    const juce::Colour danger      { 0xffff4d3d };
    const juce::Colour warm        { 0xff231d12 };   // pedale acceso
    const juce::Colour warmBorder  { 0xff4a3a1c };
    const juce::Colour warmText    { 0xffb3a68e };
}

//==============================================================================
/** Caratteri di sistema, così non serve installare nulla:
    Bahnschrift su Windows, Avenir Next su Mac; Consolas/Menlo per i numeri. */
namespace Fonts
{
    juce::String sansName();
    juce::Font display (float height, bool bold = true);   // titoli e pulsanti, in maiuscolo
    juce::Font text (float height, bool bold = false);
    juce::Font mono (float height);
}

//==============================================================================
/** Icone a tratto disegnate con Path, per i tipi di posto e per l'interfaccia. */
namespace Icons
{
    enum class Ui { prev, next, up, down, tuningFork, settings, plus, mute, close };

    void draw (juce::Graphics&, Engine::SlotKind, juce::Rectangle<float>, juce::Colour, float stroke = 1.8f);
    void draw (juce::Graphics&, Ui, juce::Rectangle<float>, juce::Colour, float stroke = 2.0f);
}

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
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float startAngle, float endAngle, juce::Slider&) override;
};

//==============================================================================
/** Pulsante della barra in alto: pannello arrotondato, icona facoltativa, testo largo. */
class StageButton final : public juce::Button
{
public:
    enum class Style { normal, record, danger };   // danger: rosso quando è attivo (es. MUTE)

    StageButton (const juce::String& label, std::optional<Icons::Ui> icon = std::nullopt, Style style = Style::normal);

    void setLabel (const juce::String&);
    void setValueText (const juce::String&);   // es. il tempo di registrazione, in monospazio

protected:
    void paintButton (juce::Graphics&, bool isMouseOver, bool isDown) override;

private:
    juce::String label, valueText;
    std::optional<Icons::Ui> icon;
    Style style;
};
