#include "Look.h"

using namespace juce;

//==============================================================================
String Fonts::sansName()
{
   #if JUCE_WINDOWS
    return "Bahnschrift";        // incluso in Windows 10/11, largo e leggibile
   #elif JUCE_MAC
    return "Avenir Next";
   #else
    return Font::getDefaultSansSerifFontName();
   #endif
}

Font Fonts::display (float height, bool bold)
{
    return Font (FontOptions (sansName(), height, bold ? Font::bold : Font::plain)).withExtraKerningFactor (0.06f);
}

Font Fonts::text (float height, bool bold)
{
    return Font (FontOptions (sansName(), height, bold ? Font::bold : Font::plain));
}

Font Fonts::mono (float height)
{
   #if JUCE_WINDOWS
    return Font (FontOptions ("Consolas", height, Font::plain));
   #elif JUCE_MAC
    return Font (FontOptions ("Menlo", height, Font::plain));
   #else
    return Font (FontOptions (Font::getDefaultMonospacedFontName(), height, Font::plain));
   #endif
}

//==============================================================================
namespace
{
    constexpr float pi = MathConstants<float>::pi;

    /** Disegna un tracciato pensato in un quadrato 24x24 dentro `area`. */
    void strokeIcon (Graphics& g, const Path& p, Rectangle<float> area, Colour colour, float stroke, bool fill = false)
    {
        const float size = jmin (area.getWidth(), area.getHeight());
        const float scale = size / 24.0f;
        const auto box = area.withSizeKeepingCentre (size, size);

        auto t = p;
        t.applyTransform (AffineTransform::scale (scale).translated (box.getX(), box.getY()));

        g.setColour (colour);
        if (fill)
            g.fillPath (t);
        else
            g.strokePath (t, PathStrokeType (stroke * scale, PathStrokeType::curved, PathStrokeType::rounded));
    }
}

void Icons::draw (Graphics& g, Engine::SlotKind kind, Rectangle<float> area, Colour colour, float stroke)
{
    using K = Engine::SlotKind;
    Path p;

    switch (kind)
    {
        case K::amp:   // testata con manopole e griglia
            p.addRoundedRectangle (2.0f, 5.0f, 20.0f, 15.0f, 2.5f);
            p.addEllipse (5.5f, 8.0f, 2.6f, 2.6f);
            p.addEllipse (10.7f, 8.0f, 2.6f, 2.6f);
            p.addEllipse (15.9f, 8.0f, 2.6f, 2.6f);
            p.startNewSubPath (5.0f, 14.5f); p.lineTo (19.0f, 14.5f);
            p.startNewSubPath (5.0f, 17.0f); p.lineTo (19.0f, 17.0f);
            break;

        case K::ir:    // cassa: cono dell'altoparlante
            p.addRoundedRectangle (3.0f, 3.0f, 18.0f, 18.0f, 3.0f);
            p.addEllipse (6.0f, 6.0f, 12.0f, 12.0f);
            p.addEllipse (9.8f, 9.8f, 4.4f, 4.4f);
            break;

        case K::drive: // onda tagliata
            p.startNewSubPath (2.0f, 12.0f);
            p.lineTo (5.0f, 12.0f); p.lineTo (7.0f, 6.0f); p.lineTo (11.0f, 6.0f);
            p.lineTo (13.0f, 18.0f); p.lineTo (17.0f, 18.0f); p.lineTo (19.0f, 12.0f); p.lineTo (22.0f, 12.0f);
            break;

        case K::compressor: // frecce che schiacciano verso il centro
            p.startNewSubPath (12.0f, 2.5f);  p.lineTo (12.0f, 9.0f);
            p.startNewSubPath (9.0f, 6.0f);   p.lineTo (12.0f, 9.0f); p.lineTo (15.0f, 6.0f);
            p.startNewSubPath (12.0f, 21.5f); p.lineTo (12.0f, 15.0f);
            p.startNewSubPath (9.0f, 18.0f);  p.lineTo (12.0f, 15.0f); p.lineTo (15.0f, 18.0f);
            p.startNewSubPath (4.0f, 12.0f);  p.lineTo (20.0f, 12.0f);
            break;

        case K::eq:    // tre cursori
            p.startNewSubPath (6.0f, 3.0f);  p.lineTo (6.0f, 21.0f);
            p.startNewSubPath (12.0f, 3.0f); p.lineTo (12.0f, 21.0f);
            p.startNewSubPath (18.0f, 3.0f); p.lineTo (18.0f, 21.0f);
            p.addRoundedRectangle (3.5f, 13.0f, 5.0f, 3.5f, 1.2f);
            p.addRoundedRectangle (9.5f, 6.5f, 5.0f, 3.5f, 1.2f);
            p.addRoundedRectangle (15.5f, 10.5f, 5.0f, 3.5f, 1.2f);
            break;

        case K::modulation: // sinusoide
            p.startNewSubPath (2.0f, 12.0f);
            p.cubicTo (5.0f, 3.0f, 9.0f, 3.0f, 12.0f, 12.0f);
            p.cubicTo (15.0f, 21.0f, 19.0f, 21.0f, 22.0f, 12.0f);
            break;

        case K::delay: // ripetizioni che si spengono
            p.addEllipse (2.5f, 8.0f, 8.0f, 8.0f);
            p.addEllipse (12.0f, 9.5f, 5.0f, 5.0f);
            p.addEllipse (18.5f, 10.8f, 2.4f, 2.4f);
            break;

        case K::reverb: // onde che si allargano
            p.addEllipse (3.0f, 17.0f, 3.0f, 3.0f);
            p.addCentredArc (4.5f, 18.5f, 6.0f, 6.0f, 0.0f, 0.0f, pi * 0.5f, true);
            p.addCentredArc (4.5f, 18.5f, 11.0f, 11.0f, 0.0f, 0.0f, pi * 0.5f, true);
            p.addCentredArc (4.5f, 18.5f, 16.0f, 16.0f, 0.0f, 0.0f, pi * 0.5f, true);
            break;

        case K::fx:
        case K::automatic: // scintilla
            p.startNewSubPath (12.0f, 3.0f);
            p.lineTo (14.0f, 10.0f); p.lineTo (21.0f, 12.0f); p.lineTo (14.0f, 14.0f);
            p.lineTo (12.0f, 21.0f); p.lineTo (10.0f, 14.0f); p.lineTo (3.0f, 12.0f); p.lineTo (10.0f, 10.0f);
            p.closeSubPath();
            break;
    }

    strokeIcon (g, p, area, colour, stroke);
}

void Icons::draw (Graphics& g, Ui icon, Rectangle<float> area, Colour colour, float stroke)
{
    Path p;

    switch (icon)
    {
        case Ui::prev:
            p.startNewSubPath (15.0f, 6.0f); p.lineTo (9.0f, 12.0f); p.lineTo (15.0f, 18.0f);
            break;

        case Ui::next:
            p.startNewSubPath (9.0f, 6.0f); p.lineTo (15.0f, 12.0f); p.lineTo (9.0f, 18.0f);
            break;

        case Ui::tuningFork:
            p.startNewSubPath (9.0f, 3.0f);
            p.lineTo (9.0f, 11.0f);
            p.addCentredArc (12.0f, 11.0f, 3.0f, 3.0f, 0.0f, pi * 1.5f, pi * 0.5f, false);
            p.lineTo (15.0f, 3.0f);
            p.startNewSubPath (12.0f, 14.0f); p.lineTo (12.0f, 21.0f);
            break;

        case Ui::settings:
            p.startNewSubPath (4.0f, 7.0f);  p.lineTo (14.0f, 7.0f);
            p.startNewSubPath (18.0f, 7.0f); p.lineTo (20.0f, 7.0f);
            p.addEllipse (14.0f, 5.0f, 4.0f, 4.0f);
            p.startNewSubPath (4.0f, 17.0f);  p.lineTo (8.0f, 17.0f);
            p.startNewSubPath (12.0f, 17.0f); p.lineTo (20.0f, 17.0f);
            p.addEllipse (8.0f, 15.0f, 4.0f, 4.0f);
            break;

        case Ui::plus:
            p.startNewSubPath (12.0f, 5.0f); p.lineTo (12.0f, 19.0f);
            p.startNewSubPath (5.0f, 12.0f); p.lineTo (19.0f, 12.0f);
            break;

        case Ui::mute:
            p.startNewSubPath (11.0f, 5.0f);
            p.lineTo (6.0f, 9.0f); p.lineTo (3.0f, 9.0f); p.lineTo (3.0f, 15.0f); p.lineTo (6.0f, 15.0f); p.lineTo (11.0f, 19.0f);
            p.closeSubPath();
            p.startNewSubPath (16.0f, 9.0f); p.lineTo (22.0f, 15.0f);
            p.startNewSubPath (22.0f, 9.0f); p.lineTo (16.0f, 15.0f);
            break;

        case Ui::close:
            p.startNewSubPath (6.0f, 6.0f);  p.lineTo (18.0f, 18.0f);
            p.startNewSubPath (18.0f, 6.0f); p.lineTo (6.0f, 18.0f);
            break;
    }

    strokeIcon (g, p, area, colour, stroke);
}

//==============================================================================
StageLookAndFeel::StageLookAndFeel()
{
    setColourScheme (LookAndFeel_V4::getMidnightColourScheme());
    setDefaultSansSerifTypefaceName (Fonts::sansName());

    setColour (ResizableWindow::backgroundColourId, Palette::background);
    setColour (DocumentWindow::backgroundColourId,  Palette::background);
    setColour (TextButton::buttonColourId,          Palette::panel);
    setColour (TextButton::buttonOnColourId,        Palette::accent);
    setColour (TextButton::textColourOffId,         Palette::text);
    setColour (TextButton::textColourOnId,          Palette::background);
    setColour (Label::textColourId,                 Palette::textDim);
    setColour (PopupMenu::backgroundColourId,       Palette::panel);
    setColour (PopupMenu::textColourId,             Palette::text);
    setColour (PopupMenu::highlightedBackgroundColourId, Palette::accent);
    setColour (PopupMenu::highlightedTextColourId,  Palette::background);
    setColour (PopupMenu::headerTextColourId,       Palette::textDim);
    setColour (AlertWindow::backgroundColourId,     Palette::panel);
    setColour (AlertWindow::textColourId,           Palette::text);
    setColour (AlertWindow::outlineColourId,        Palette::borderLight);
    setColour (TextEditor::backgroundColourId,      Palette::background);
    setColour (TextEditor::textColourId,            Palette::text);
    setColour (TextEditor::outlineColourId,         Palette::borderLight);
    setColour (TextEditor::focusedOutlineColourId,  Palette::accent);
    setColour (TextEditor::highlightColourId,       Palette::accent.withAlpha (0.35f));
    setColour (CaretComponent::caretColourId,       Palette::accent);
    setColour (ListBox::backgroundColourId,         Palette::cardOff);
    setColour (ComboBox::backgroundColourId,        Palette::panel);
    setColour (ComboBox::outlineColourId,           Palette::borderLight);
    setColour (ScrollBar::thumbColourId,            Palette::off);
}

Font StageLookAndFeel::getTextButtonFont (TextButton&, int buttonHeight)
{
    return Fonts::display (jmin (16.0f, (float) buttonHeight * 0.36f));
}

Font StageLookAndFeel::getPopupMenuFont()
{
    return Fonts::text (21.0f);
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

void StageLookAndFeel::drawButtonBackground (Graphics& g, Button& button, const Colour& backgroundColour,
                                             bool isMouseOver, bool isDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto colour = backgroundColour;

    if (isDown)            colour = colour.brighter (0.15f);
    else if (isMouseOver)  colour = colour.brighter (0.06f);
    if (! button.isEnabled()) colour = colour.withMultipliedAlpha (0.5f);

    g.setColour (colour);
    g.fillRoundedRectangle (bounds, 12.0f);

    if (! button.getToggleState() && backgroundColour == Palette::panel)
    {
        g.setColour (Palette::borderLight);
        g.drawRoundedRectangle (bounds, 12.0f, 1.0f);
    }
}

void StageLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int width, int height, float sliderPos,
                                         float startAngle, float endAngle, Slider& slider)
{
    const auto bounds = Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (4.0f);
    const float size = jmin (bounds.getWidth(), bounds.getHeight());
    const auto box = bounds.withSizeKeepingCentre (size, size);
    const auto centre = box.getCentre();
    const float radius = size * 0.5f;
    const float angle = startAngle + sliderPos * (endAngle - startAngle);

    // anello di fondo
    Path track;
    track.addCentredArc (centre.x, centre.y, radius - 3.0f, radius - 3.0f, 0.0f, startAngle, endAngle, true);
    g.setColour (Palette::off);
    g.strokePath (track, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));

    // arco del valore, dal centro (0 dB) se lo slider è simmetrico
    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float zeroPos = bipolar ? (float) slider.valueToProportionOfLength (0.0) : 0.0f;
    const float zeroAngle = startAngle + zeroPos * (endAngle - startAngle);

    if (std::abs (angle - zeroAngle) > 0.01f)
    {
        Path value;
        value.addCentredArc (centre.x, centre.y, radius - 3.0f, radius - 3.0f, 0.0f,
                             jmin (zeroAngle, angle), jmax (zeroAngle, angle), true);
        g.setColour (slider.isEnabled() ? Palette::accent : Palette::textFaint);
        g.strokePath (value, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // corpo della manopola e indicatore
    const auto knob = box.reduced (10.0f);
    g.setGradientFill (ColourGradient (Palette::panelLight, knob.getX(), knob.getY(),
                                       Palette::cardOff, knob.getX(), knob.getBottom(), false));
    g.fillEllipse (knob);
    g.setColour (Palette::borderLight);
    g.drawEllipse (knob, 1.0f);

    const float r1 = knob.getWidth() * 0.18f, r2 = knob.getWidth() * 0.42f;
    const Point<float> a (centre.x + r1 * std::sin (angle), centre.y - r1 * std::cos (angle));
    const Point<float> b (centre.x + r2 * std::sin (angle), centre.y - r2 * std::cos (angle));
    g.setColour (Palette::text);
    g.drawLine ({ a, b }, 3.0f);
}

//==============================================================================
StageButton::StageButton (const String& text, std::optional<Icons::Ui> i, Style s)
    : Button (text), label (text), icon (i), style (s)
{
}

void StageButton::setLabel (const String& text)
{
    if (label != text) { label = text; repaint(); }
}

void StageButton::setValueText (const String& text)
{
    if (valueText != text) { valueText = text; repaint(); }
}

void StageButton::paintButton (Graphics& g, bool isMouseOver, bool isDown)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const bool on = getToggleState();
    const bool recording = ((style == Style::record || style == Style::danger) && on);

    Colour bg = Palette::panel, border = Palette::border, fg = Palette::text;

    if (recording)       { bg = Colour (0xff2a1412); border = Colour (0xff5a2620); fg = Colour (0xffffd9d3); }
    else if (on)         { bg = Palette::accent;     border = Palette::accent;     fg = Palette::background; }

    if (isDown)           bg = bg.brighter (0.12f);
    else if (isMouseOver) bg = bg.brighter (0.04f);

    g.setColour (bg);
    g.fillRoundedRectangle (bounds, 14.0f);
    g.setColour (border);
    g.drawRoundedRectangle (bounds, 14.0f, 1.0f);

    // contenuto centrato: [icona o punto] testo [valore]
    const auto labelFont = Fonts::display (15.0f);
    const auto valueFont = Fonts::mono (15.0f);
    const float iconSize = (style == Style::record) ? 12.0f : (icon.has_value() ? 20.0f : 0.0f);
    const float gap = label.isEmpty() ? 0.0f : 10.0f;   // solo icona: centrata

    const float labelW = GlyphArrangement::getStringWidth (labelFont, label);
    const float valueW = valueText.isEmpty() ? 0.0f : GlyphArrangement::getStringWidth (valueFont, valueText);
    float total = labelW;
    if (iconSize > 0.0f)       total += iconSize + gap;
    if (valueText.isNotEmpty()) total += gap * 0.8f + valueW;

    float x = bounds.getCentreX() - total * 0.5f;
    const float cy = bounds.getCentreY();

    if (style == Style::record)
    {
        auto dot = Rectangle<float> (iconSize, iconSize).withCentre ({ x + iconSize * 0.5f, cy });
        if (recording)
        {
            g.setColour (Palette::danger.withAlpha (0.35f));
            g.fillEllipse (dot.expanded (5.0f));
        }
        g.setColour (Palette::danger);
        g.fillEllipse (dot);
        x += iconSize + gap;
    }
    else if (icon.has_value())
    {
        Icons::draw (g, *icon, Rectangle<float> (iconSize, iconSize).withCentre ({ x + iconSize * 0.5f, cy }), fg, 1.8f);
        x += iconSize + gap;
    }

    g.setColour (fg);
    g.setFont (labelFont);
    g.drawText (label, Rectangle<float> (labelW + 2.0f, bounds.getHeight()).withPosition (x, bounds.getY()),
                Justification::centredLeft, false);
    x += labelW + gap * 0.8f;

    if (valueText.isNotEmpty())
    {
        g.setFont (valueFont);
        g.drawText (valueText, Rectangle<float> (valueW + 2.0f, bounds.getHeight()).withPosition (x, bounds.getY()),
                    Justification::centredLeft, false);
    }
}
