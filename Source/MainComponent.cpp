#include "MainComponent.h"
#include "PluginChooser.h"

using namespace juce;

//==============================================================================
namespace
{
    String twoDigits (int n)   { return String (n).paddedLeft ('0', 2); }

    /** "slots 01 · 02", "slot 03" oppure "no slots". */
    String describeSlots (Array<int> slots)
    {
        if (slots.isEmpty())
            return "no slots";

        slots.sort();
        StringArray parts;
        for (auto s : slots)
            parts.add (twoDigits (s + 1));

        return (slots.size() == 1 ? "slot " : "slots ") + parts.joinIntoString (String::fromUTF8 (" · "));
    }

    void drawLed (Graphics& g, Rectangle<float> led, bool on, Colour colour)
    {
        if (on)
        {
            g.setColour (colour.withAlpha (0.3f));
            g.fillEllipse (led.expanded (led.getWidth() * 0.45f));
            g.setColour (colour);
            g.fillEllipse (led);
        }
        else
        {
            g.setColour (Palette::off);
            g.drawEllipse (led.reduced (1.0f), jmax (1.5f, led.getWidth() * 0.11f));
        }
    }
}

//==============================================================================
void SlotView::paint (Graphics& g)
{
    auto& engine = owner.getEngine();
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    const auto number = twoDigits (index + 1);

    if (engine.isSlotEmpty (index))
    {
        Path outline;
        outline.addRoundedRectangle (bounds.reduced (1.0f), 16.0f);
        const float dashes[] = { 7.0f, 6.0f };
        PathStrokeType (1.5f).createDashedStroke (outline, outline, dashes, 2);
        g.setColour (Palette::borderLight);
        g.fillPath (outline);

        g.setColour (Palette::textFaint);
        g.setFont (Fonts::mono (13.0f));
        g.drawText (number, bounds.reduced (18.0f, 16.0f), Justification::topLeft);

        const String label ("ADD");
        const auto font = Fonts::display (13.0f);
        const float textW = GlyphArrangement::getStringWidth (font, label);
        const float x = bounds.getCentreX() - (18.0f + 8.0f + textW) * 0.5f;

        Icons::draw (g, Icons::Ui::plus, Rectangle<float> (18.0f, 18.0f).withCentre ({ x + 9.0f, bounds.getCentreY() }),
                     Palette::textFaint);
        g.setFont (font);
        g.drawText (label, Rectangle<float> (textW + 2.0f, 20.0f).withPosition (x + 26.0f, bounds.getCentreY() - 10.0f),
                    Justification::centredLeft, false);
        return;
    }

    const bool active   = engine.isSlotActive (index);
    const bool bypassed = engine.getSlotUserBypass (index);
    const auto kind     = engine.getSlotKind (index);

    // fondo: in rilievo se suona, piatto e scuro se no
    if (active)
        g.setGradientFill (ColourGradient (Palette::panelLight, 0.0f, bounds.getY(), Palette::card, 0.0f, bounds.getBottom(), false));
    else
        g.setColour (Palette::cardOff);

    g.fillRoundedRectangle (bounds, 16.0f);
    g.setColour (active ? Palette::borderLight : Palette::border);
    g.drawRoundedRectangle (bounds, 16.0f, 1.0f);

    if (active)   // linea luminosa in alto
    {
        auto line = Rectangle<float> (bounds.getX() + 18.0f, bounds.getY(), bounds.getWidth() - 36.0f, 3.0f);
        g.setColour (Palette::accent.withAlpha (0.22f));
        g.fillRoundedRectangle (line.expanded (4.0f, 2.0f).translated (0.0f, 2.0f), 4.0f);
        g.setColour (Palette::accent);
        g.fillRoundedRectangle (line, 1.5f);
    }

    auto area = bounds.reduced (18.0f, 16.0f);

    // riga in alto: numero e tipo
    auto topRow = area.removeFromTop (22.0f);
    g.setColour (active ? Palette::textDim : Palette::textFaint);
    g.setFont (Fonts::mono (13.0f));
    g.drawText (number, topRow, Justification::centredLeft);

    const auto tag = Engine::getSlotKindTag (kind);
    const auto tagFont = Fonts::display (10.5f);
    const float tagW = GlyphArrangement::getStringWidth (tagFont, tag) + 14.0f;
    const auto tagBox = topRow.removeFromRight (tagW).withSizeKeepingCentre (tagW, 20.0f);

    if (active)
    {
        g.setColour (Palette::text);
        g.fillRoundedRectangle (tagBox, 4.0f);
        g.setColour (Palette::background);
    }
    else
    {
        g.setColour (Palette::off);
        g.drawRoundedRectangle (tagBox, 4.0f, 1.0f);
        g.setColour (Palette::textDim);
    }
    g.setFont (tagFont);
    g.drawText (tag, tagBox, Justification::centred);

    // icona del tipo
    area.removeFromTop (12.0f);
    const float iconSize = jlimit (24.0f, 42.0f, area.getHeight() * 0.28f);
    Icons::draw (g, kind, area.removeFromTop (iconSize).withWidth (iconSize),
                 active ? Palette::text : Palette::textFaint, 1.6f);

    // in basso: stato, poi sottotitolo e nome
    auto statusRow = area.removeFromBottom (18.0f);
    area.removeFromBottom (10.0f);

    const auto custom = engine.getSlotCustomName (index);
    const auto subtitle = custom.isNotEmpty() ? engine.getSlotPluginName (index) : engine.getSlotManufacturer (index);
    g.setColour (active ? Palette::textDim : Palette::textFaint);
    g.setFont (Fonts::text (13.5f));
    g.drawText (subtitle, area.removeFromBottom (18.0f), Justification::centredLeft, true);

    area.removeFromBottom (4.0f);
    g.setColour (active ? Palette::text : Palette::textDim);
    g.setFont (Fonts::display (jlimit (16.0f, 24.0f, area.getWidth() * 0.12f)));
    g.drawFittedText (engine.getSlotName (index).toUpperCase(), area.toNearestInt(), Justification::bottomLeft, 2, 0.75f);

    // stato, con i pedali che lo controllano
    StringArray controllers;
    for (int b = 0; b < Engine::numButtons; ++b)
        if (engine.getButton (b).slots.contains (index))
            controllers.add ("SWITCH " + String (b + 1));

    String status = bypassed ? "BYPASS" : (active ? "ON" : "OFF");
    if (! controllers.isEmpty())
        status << String::fromUTF8 ("  ·  ") << controllers.joinIntoString (" + ");

    drawLed (g, Rectangle<float> (8.0f, 8.0f).withCentre ({ statusRow.getX() + 4.0f, statusRow.getCentreY() }),
             active, Palette::accent);
    g.setColour (active ? Palette::accent : Palette::textDim);
    g.setFont (Fonts::display (11.0f));
    g.drawText (status, statusRow.withTrimmedLeft (16.0f), Justification::centredLeft, true);
}

void SlotView::mouseDown (const MouseEvent&)
{
    dragging = false;
}

void SlotView::mouseDrag (const MouseEvent& e)
{
    if (owner.getEngine().isSlotEmpty (index))
        return;

    if (! dragging && e.getDistanceFromDragStart() > 12)
    {
        dragging = true;
        owner.slotDragStarted (index, e.getMouseDownPosition());
    }

    if (dragging)
        owner.slotDragged (e.getEventRelativeTo (&owner).getPosition());
}

void SlotView::mouseUp (const MouseEvent& e)
{
    if (dragging)
    {
        dragging = false;
        owner.slotDropped();
        return;
    }

    if (e.mouseWasClicked())
        owner.showSlotMenu (index);
}

//==============================================================================
void BigButton::paint (Graphics& g)
{
    auto& engine = owner.getEngine();
    const auto& button = engine.getButton (index);
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    const bool on   = button.active;
    const bool edit = owner.isEditMode();

    if (on)
        g.setGradientFill (ColourGradient (Palette::warm, 0.0f, bounds.getY(), Colour (0xff16140f), 0.0f, bounds.getBottom(), false));
    else
        g.setColour (Palette::cardOff);

    g.fillRoundedRectangle (bounds, 18.0f);
    g.setColour (edit ? Palette::accent : (on ? Palette::warmBorder : Palette::border));
    g.drawRoundedRectangle (bounds, 18.0f, edit ? 2.0f : 1.0f);

    const float labelH = jlimit (22.0f, 36.0f, bounds.getHeight() * 0.18f);
    const float blockH = 18.0f + 14.0f + labelH + 14.0f + 14.0f;
    float y = bounds.getCentreY() - blockH * 0.5f;

    drawLed (g, Rectangle<float> (18.0f, 18.0f).withCentre ({ bounds.getCentreX(), y + 9.0f }), on, Palette::accent);
    y += 18.0f + 14.0f;

    g.setColour (on ? Colour (0xfff6efe2) : Palette::textDim);
    g.setFont (Fonts::display (labelH));
    g.drawFittedText (engine.getButtonDisplayName (index).toUpperCase(),
                      Rectangle<float> (bounds.getX() + 12.0f, y, bounds.getWidth() - 24.0f, labelH + 4.0f).toNearestInt(),
                      Justification::centred, 1, 0.45f);
    y += labelH + 14.0f;

    String sub;
    if (edit)
        sub = button.slots.isEmpty() ? "tap to assign slots"
                                     : describeSlots (button.slots) + String::fromUTF8 ("  ·  tap to edit");
    else
        sub = describeSlots (button.slots) + String::fromUTF8 ("  ·  CC ") + String (engine.getButtonCC (index));

    g.setColour (edit ? Palette::accent : (on ? Palette::warmText : Palette::textFaint));
    g.setFont (Fonts::mono (12.0f));
    g.drawText (sub, Rectangle<float> (bounds.getX() + 8.0f, y, bounds.getWidth() - 16.0f, 16.0f), Justification::centred, true);
}

void BigButton::mouseDown (const MouseEvent&)
{
    owner.bigButtonPressed (index);
}

//==============================================================================
void PresetSelector::setPreset (const String& newName, int newNumber, int newTotal, bool newUnsaved)
{
    if (newName == name && newNumber == number && newTotal == total && newUnsaved == unsaved)
        return;

    name = newName;
    number = newNumber;
    total = newTotal;
    unsaved = newUnsaved;
    repaint();
}

int PresetSelector::zoneAt (Point<int> p) const
{
    if (p.x < 64)               return -1;
    if (p.x > getWidth() - 64)  return 1;
    return 0;
}

void PresetSelector::mouseDown (const MouseEvent& e)
{
    pressedZone = zoneAt (e.getPosition());
    repaint();
}

void PresetSelector::mouseUp (const MouseEvent& e)
{
    const int zone = pressedZone;
    pressedZone = -2;
    repaint();

    if (! getLocalBounds().contains (e.getPosition()) || zoneAt (e.getPosition()) != zone)
        return;

    if (zone == -1 && onPrev) onPrev();
    if (zone == 1  && onNext) onNext();
    if (zone == 0  && onMenu) onMenu();
}

void PresetSelector::paint (Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    g.setColour (Palette::panel);
    g.fillRoundedRectangle (bounds, 14.0f);

    if (pressedZone >= -1)
    {
        auto zone = pressedZone == -1 ? bounds.withWidth (64.0f)
                  : pressedZone == 1  ? bounds.withLeft (bounds.getRight() - 64.0f)
                                      : bounds.reduced (64.0f, 0.0f);
        g.saveState();
        Path clip;
        clip.addRoundedRectangle (bounds, 14.0f);
        g.reduceClipRegion (clip);
        g.setColour (Palette::panelLight);
        g.fillRect (zone);
        g.restoreState();
    }

    g.setColour (Palette::border);
    g.drawRoundedRectangle (bounds, 14.0f, 1.0f);
    g.drawVerticalLine (64, bounds.getY(), bounds.getBottom());
    g.drawVerticalLine (getWidth() - 65, bounds.getY(), bounds.getBottom());

    Icons::draw (g, Icons::Ui::prev, Rectangle<float> (22.0f, 22.0f).withCentre ({ 32.0f, bounds.getCentreY() }), Palette::text);
    Icons::draw (g, Icons::Ui::next, Rectangle<float> (22.0f, 22.0f).withCentre ({ bounds.getRight() - 32.0f, bounds.getCentreY() }), Palette::text);

    auto text = bounds.reduced (84.0f, 10.0f);
    auto topLine = text.removeFromTop (16.0f);
    g.setColour (Palette::textDim);
    g.setFont (Fonts::mono (11.0f));
    const String position = number > 0 ? "PRESET " + twoDigits (number) + " / " + twoDigits (total) : String ("NEW PRESET");
    g.drawText (position, topLine, Justification::centredLeft);

    if (unsaved)
    {
        const float x = topLine.getX() + GlyphArrangement::getStringWidth (Fonts::mono (11.0f), position) + 10.0f;
        g.setColour (Palette::accent);
        g.drawText (String::fromUTF8 ("·  UNSAVED"), topLine.withLeft (x), Justification::centredLeft);
    }

    auto nameArea = text;
    if (unsaved)   // pallino ambra prima del nome
    {
        auto dot = nameArea.removeFromLeft (18.0f);
        g.setColour (Palette::accent);
        g.fillEllipse (Rectangle<float> (9.0f, 9.0f).withCentre ({ dot.getX() + 4.5f, dot.getCentreY() }));
    }

    g.setColour (Palette::text);
    g.setFont (Fonts::display (22.0f));
    g.drawFittedText (name.toUpperCase(), nameArea.toNearestInt(), Justification::centredLeft, 1, 0.7f);
}

//==============================================================================
void StatusDisplay::setLines (const String& first, const String& second, bool highlight)
{
    if (first == line1 && second == line2 && highlight == highlighted)
        return;

    line1 = first;
    line2 = second;
    highlighted = highlight;
    repaint();
}

void StatusDisplay::paint (Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced (8.0f, 0.0f);
    auto top = area.removeFromTop (area.getHeight() * 0.5f);

    g.setFont (Fonts::mono (12.0f));
    g.setColour (highlighted ? Palette::accent : Palette::text);
    g.drawText (line1, top.withTrimmedBottom (2.0f), Justification::bottomRight, true);
    g.setColour (Palette::textDim);
    g.drawText (line2, area.withTrimmedTop (2.0f), Justification::topRight, true);
}

//==============================================================================
void TunerOverlay::visibilityChanged()
{
    if (isVisible())
    {
        history.assign (engine.getTuner().getSampleRateForAnalysis() > 50000.0 ? 4096 : 2048, 0.0f);
        incoming.assign (16384, 0.0f);
        recent.clear();
        displayFreq = 0.0f;
        startTimerHz (20);
    }
    else
    {
        stopTimer();
    }
}

void TunerOverlay::timerCallback()
{
    auto& tuner = engine.getTuner();
    const int got = tuner.pull (incoming.data(), (int) incoming.size());

    if (got > 0)
    {
        const int n = (int) history.size();
        if (got >= n)
        {
            std::copy (incoming.begin() + (got - n), incoming.begin() + got, history.begin());
        }
        else
        {
            std::move (history.begin() + got, history.end(), history.begin());
            std::copy (incoming.begin(), incoming.begin() + got, history.end() - got);
        }
    }

    const float freq = detectPitchYin (history.data(), (int) history.size(), tuner.getSampleRateForAnalysis());

    if (freq > 0.0f)
    {
        framesWithoutSignal = 0;
        recent.add (freq);
        if (recent.size() > 5)
            recent.remove (0);

        auto sorted = recent;
        sorted.sort();
        displayFreq = sorted[sorted.size() / 2];
    }
    else if (++framesWithoutSignal > 10)   // mezzo secondo senza segnale: svuota
    {
        recent.clear();
        displayFreq = 0.0f;
    }

    repaint();
}

void TunerOverlay::paint (Graphics& g)
{
    const auto full = getLocalBounds().toFloat();
    g.setGradientFill (ColourGradient (Colour (0xff14161a), full.getCentreX(), full.getHeight() * 0.42f,
                                       Colour (0xff070809), full.getCentreX() + full.getWidth() * 0.6f, full.getHeight() * 0.42f, true));
    g.fillAll();

    auto area = full.reduced (24.0f, 28.0f);

    // intestazione: titolo a sinistra, "uscita muta" a destra
    auto header = area.removeFromTop (40.0f);
    Icons::draw (g, Icons::Ui::tuningFork, header.removeFromLeft (20.0f).withSizeKeepingCentre (20.0f, 20.0f), Palette::textDim, 1.8f);
    header.removeFromLeft (10.0f);
    g.setColour (Palette::textDim);
    g.setFont (Fonts::display (12.0f));
    g.drawText ("TUNER", header.withWidth (200.0f), Justification::centredLeft);

    {
        const String muted ("OUTPUT MUTED");
        const auto font = Fonts::display (12.0f);
        const float w = GlyphArrangement::getStringWidth (font, muted) + 16.0f + 10.0f + 32.0f;
        auto pill = header.removeFromRight (w).withSizeKeepingCentre (w, 36.0f);
        g.setColour (Colour (0xff2a1412));
        g.fillRoundedRectangle (pill, 18.0f);
        g.setColour (Colour (0xff5a2620));
        g.drawRoundedRectangle (pill, 18.0f, 1.0f);
        auto inner = pill.reduced (16.0f, 0.0f);
        Icons::draw (g, Icons::Ui::mute, inner.removeFromLeft (16.0f).withSizeKeepingCentre (16.0f, 16.0f), Colour (0xffffb4a9), 2.0f);
        inner.removeFromLeft (10.0f);
        g.setColour (Colour (0xffffb4a9));
        g.setFont (font);
        g.drawText (muted, inner, Justification::centredLeft);
    }

    // pulsante in basso (tutto lo schermo chiude, ma così si vede)
    auto closeRow = area.removeFromBottom (64.0f);
    auto closeBox = closeRow.withSizeKeepingCentre (jmin (440.0f, closeRow.getWidth()), 64.0f);
    g.setColour (Palette::panel);
    g.fillRoundedRectangle (closeBox, 14.0f);
    g.setColour (Palette::borderLight);
    g.drawRoundedRectangle (closeBox, 14.0f, 1.0f);
    g.setColour (Palette::text);
    g.setFont (Fonts::display (15.0f));
    g.drawText (String::fromUTF8 ("TAP TO CLOSE  ·  CC ") + String (engine.getTunerCC()), closeBox, Justification::centred);
    area.removeFromBottom (24.0f);

    if (displayFreq <= 0.0f)
    {
        g.setColour (Palette::textDim);
        g.setFont (Fonts::display (40.0f));
        g.drawText ("PLAY A STRING", area, Justification::centred);
        return;
    }

    static const char* names[]   = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    const double midi = 69.0 + 12.0 * std::log2 (displayFreq / 440.0);
    const int nearest = roundToInt (midi);
    const float cents = (float) ((midi - nearest) * 100.0);
    const int noteIndex = ((nearest % 12) + 12) % 12;
    const int octave = nearest / 12 - 1;
    const float absCents = std::abs (cents);
    const auto colour = absCents < 3.0f ? Palette::on : (absCents < 15.0f ? Palette::accent : Palette::danger);

    // blocco centrale: nota, valori, scala, corde
    const float noteSize = jmin (300.0f, area.getHeight() * 0.45f);
    const float blockH = noteSize * 0.86f + 18.0f + 28.0f + 40.0f + 64.0f + 26.0f + 36.0f + 44.0f;
    auto block = area.withSizeKeepingCentre (area.getWidth(), jmin (area.getHeight(), blockH));

    auto noteRow = block.removeFromTop (noteSize * 0.86f);
    {
        const String note (names[noteIndex]);
        const auto noteFont = Fonts::display (noteSize);
        const auto octFont = Fonts::display (noteSize * 0.24f, false);
        const float noteW = GlyphArrangement::getStringWidth (noteFont, note);
        const float octW = GlyphArrangement::getStringWidth (octFont, String (octave));
        const float x = noteRow.getCentreX() - (noteW + 6.0f + octW) * 0.5f;

        g.setColour (colour);
        g.setFont (noteFont);
        g.drawText (note, Rectangle<float> (noteW + 4.0f, noteRow.getHeight()).withPosition (x, noteRow.getY()),
                    Justification::centredLeft, false);
        g.setColour (Palette::textDim);
        g.setFont (octFont);
        g.drawText (String (octave), Rectangle<float> (octW + 4.0f, noteSize * 0.3f).withPosition (x + noteW + 6.0f, noteRow.getY() + noteSize * 0.08f),
                    Justification::centredLeft, false);
    }

    block.removeFromTop (18.0f);
    auto info = block.removeFromTop (28.0f);
    {
        const String left = String (displayFreq, 2) + " Hz";
        const String right = (cents >= 0.0f ? String ("+") : String (CharPointer_UTF8 ("\xe2\x88\x92")))
                                + String (std::abs (cents), 1) + " cent";
        g.setFont (Fonts::mono (20.0f));
        g.setColour (Palette::textDim);
        g.drawText (left, info.withRight (info.getCentreX() - 16.0f), Justification::centredRight);
        g.setColour (colour);
        g.drawText (right, info.withLeft (info.getCentreX() + 16.0f), Justification::centredLeft);
    }

    // scala a segmenti da -50 a +50 cent (41 segmenti da 2,5 cent)
    block.removeFromTop (40.0f);
    auto scale = block.removeFromTop (64.0f).withSizeKeepingCentre (jmin (960.0f, area.getWidth() * 0.8f), 64.0f);
    {
        constexpr int numSegments = 41;
        const float gap = 5.0f;
        const float segW = (scale.getWidth() - gap * (numSegments - 1)) / numSegments;
        const int lit = jlimit (0, numSegments - 1, roundToInt (cents / 2.5f) + numSegments / 2);

        for (int i = 0; i < numSegments; ++i)
        {
            float h = 34.0f;
            Colour c (0xff2a2d33);

            if (i == 10 || i == 30)           { h = 46.0f; c = Palette::off; }
            if (std::abs (i - lit) == 1)      { h = 40.0f; c = colour.withAlpha (0.4f); }
            if (i == numSegments / 2)         { h = 64.0f; c = Palette::text; }
            if (i == lit && i != numSegments / 2) { h = 52.0f; c = colour; }

            auto seg = Rectangle<float> (scale.getX() + (float) i * (segW + gap), scale.getCentreY() - h * 0.5f, segW, h);

            if (i == lit)
            {
                g.setColour (colour.withAlpha (0.3f));
                g.fillRoundedRectangle (seg.expanded (3.0f), 4.0f);
                if (i == numSegments / 2)
                    c = colour;   // intonato: la tacca centrale si colora
            }

            g.setColour (c);
            g.fillRoundedRectangle (seg, 2.0f);
        }

        block.removeFromTop (10.0f);
        auto labels = block.removeFromTop (16.0f).withX (scale.getX()).withWidth (scale.getWidth());
        g.setFont (Fonts::mono (13.0f));
        const char* texts[] = { "-50", "-25", "0", "+25", "+50" };
        for (int i = 0; i < 5; ++i)
        {
            const float x = labels.getX() + labels.getWidth() * (float) i / 4.0f;
            g.setColour (i == 2 ? Palette::text : Palette::textFaint);
            g.drawText (texts[i], Rectangle<float> (60.0f, labels.getHeight()).withCentre ({ x, labels.getCentreY() }), Justification::centred);
        }
    }

    // corde dell'accordatura standard: si accende quella più vicina
    block.removeFromTop (36.0f);
    {
        const int strings[] = { 40, 45, 50, 55, 59, 64 };   // E2 A2 D3 G3 B3 E4
        const float chipW = 64.0f, chipGap = 10.0f;
        auto row = block.removeFromTop (44.0f).withSizeKeepingCentre (6.0f * chipW + 5.0f * chipGap, 44.0f);

        for (int i = 0; i < 6; ++i)
        {
            auto chip = Rectangle<float> (row.getX() + (float) i * (chipW + chipGap), row.getY(), chipW, 44.0f);
            const bool match = strings[i] == nearest;

            if (match)
            {
                g.setColour (colour);
                g.fillRoundedRectangle (chip, 10.0f);
                g.setColour (Palette::background);
            }
            else
            {
                g.setColour (Palette::borderLight);
                g.drawRoundedRectangle (chip.reduced (0.5f), 10.0f, 1.0f);
                g.setColour (Palette::textDim);
            }

            g.setFont (Fonts::display (18.0f));
            g.drawText (names[strings[i] % 12], chip, Justification::centred);
        }
    }
}

void TunerOverlay::mouseUp (const MouseEvent&)
{
    engine.setTunerActive (false);
}

//==============================================================================
PluginWindow::PluginWindow (AudioProcessor& processor, std::function<void()> closeCallback)
    : DocumentWindow (processor.getName(), Palette::panel, DocumentWindow::closeButton),
      onClose (std::move (closeCallback))
{
    setUsingNativeTitleBar (true);

    AudioProcessorEditor* editor = processor.hasEditor() ? processor.createEditorAndMakeActive() : nullptr;
    if (editor == nullptr)
        editor = new GenericAudioProcessorEditor (processor);

    setContentOwned (editor, true);
    setResizable (editor->isResizable(), false);
    setAlwaysOnTop (true);   // sopra la finestra a schermo intero
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}

void PluginWindow::closeButtonPressed()
{
    if (onClose != nullptr)
        onClose();   // il proprietario elimina questa finestra
}

//==============================================================================
namespace
{
    constexpr float meterFloorDb = -48.0f;
    constexpr float meterTopDb   = 0.0f;

    float dbToMeter (float db)
    {
        return jlimit (0.0f, 1.0f, (db - meterFloorDb) / (meterTopDb - meterFloorDb));
    }

    float gainToMeter (float gain)
    {
        return dbToMeter (Decibels::gainToDecibels (gain, meterFloorDb - 1.0f));
    }
}

void OutputMeter::timerCallback()
{
    auto& recorder = engine.getRecorder();
    const auto now = Time::getMillisecondCounter();

    for (int ch = 0; ch < 2; ++ch)
    {
        const float peak = recorder.takePeak (ch);

        // salita immediata, discesa graduale (circa 20 dB al secondo)
        level[ch] = jmax (peak, level[ch] * 0.86f);

        if (peak >= hold[ch] || now > holdTime[ch])
        {
            hold[ch] = peak >= hold[ch] ? peak : hold[ch] * 0.9f;
            if (peak >= hold[ch])
                holdTime[ch] = now + 1500;
        }

        maxPeak = jmax (maxPeak, peak);
    }

    if (recorder.takeClipped())
        clipLatched = true;

    repaint();
}

void OutputMeter::mouseDown (const MouseEvent&)
{
    clipLatched = false;
    maxPeak = 0.0f;
    repaint();
}

void OutputMeter::paint (Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (Palette::cardOff);
    g.fillRoundedRectangle (bounds, 16.0f);
    g.setColour (Palette::border);
    g.drawRoundedRectangle (bounds, 16.0f, 1.0f);

    auto area = bounds.reduced (10.0f, 12.0f);

    // spia CLIP in alto: resta accesa finché non tocchi la barra
    auto clipBox = area.removeFromTop (36.0f);
    g.setColour (clipLatched ? Palette::danger : Colour (0xff1a1c20));
    g.fillRoundedRectangle (clipBox, 8.0f);
    if (! clipLatched)
    {
        g.setColour (Palette::off);
        g.drawRoundedRectangle (clipBox.reduced (0.5f), 8.0f, 1.0f);
    }
    g.setColour (clipLatched ? Colours::white : Palette::textDim);
    g.setFont (Fonts::display (11.0f));
    g.drawText ("CLIP", clipBox, Justification::centred);

    // picco massimo in basso
    auto valueBox = area.removeFromBottom (40.0f);
    const float maxDb = Decibels::gainToDecibels (maxPeak, -100.0f);
    g.setColour (maxDb >= 0.0f ? Palette::danger : (maxDb > -6.0f ? Palette::accent : Palette::text));
    g.setFont (Fonts::mono (15.0f));
    g.drawText (maxPeak > 0.0f ? String (maxDb, 1) : String ("-inf"),
                valueBox.removeFromTop (22.0f), Justification::centred);
    g.setColour (Palette::textFaint);
    g.setFont (Fonts::display (9.0f));
    g.drawText ("PEAK dB", valueBox, Justification::centred);

    area.removeFromTop (10.0f);
    area.removeFromBottom (10.0f);

    // due barre (L e R) a destra, scala a sinistra
    const float barW = 12.0f, gap = 6.0f;
    auto bars = area.removeFromRight (barW * 2.0f + gap);
    area.removeFromRight (6.0f);

    g.setFont (Fonts::mono (10.0f));
    for (float db : { 0.0f, -6.0f, -12.0f, -24.0f, -36.0f, -48.0f })
    {
        const float y = bars.getBottom() - dbToMeter (db) * bars.getHeight();
        g.setColour (Palette::textFaint);
        g.drawText (String ((int) db), Rectangle<float> (area.getWidth(), 12.0f).withPosition (area.getX(), y - 6.0f),
                    Justification::centredRight);
    }

    for (int ch = 0; ch < 2; ++ch)
    {
        auto bar = Rectangle<float> (bars.getX() + (float) ch * (barW + gap), bars.getY(), barW, bars.getHeight());
        g.setColour (Colour (0xff08090a));
        g.fillRoundedRectangle (bar, 3.0f);

        const float h = gainToMeter (level[ch]) * bar.getHeight();
        auto filled = bar.withTop (bar.getBottom() - h);

        // verde fino a -12, giallo fino a -3, rosso sopra
        ColourGradient gradient (Palette::danger, bar.getX(), bar.getY(), Palette::on, bar.getX(), bar.getBottom(), false);
        gradient.addColour (1.0 - dbToMeter (-3.0f),  Colour (0xfff2c14e));
        gradient.addColour (1.0 - dbToMeter (-12.0f), Palette::on);
        g.setGradientFill (gradient);
        g.fillRoundedRectangle (filled, 3.0f);

        if (hold[ch] > 0.0f)   // tenuta del picco
        {
            const float y = bar.getBottom() - gainToMeter (hold[ch]) * bar.getHeight();
            g.setColour (hold[ch] >= 1.0f ? Palette::danger : Palette::text);
            g.fillRect (bar.getX(), y - 1.0f, bar.getWidth(), 2.0f);
        }
    }
}

//==============================================================================
InputStrip::InputStrip (Engine& e) : engine (e)
{
    gainKnob.setRange (-24.0, 24.0, 0.5);
    gainKnob.setDoubleClickReturnValue (true, 0.0);
    gainKnob.setMouseDragSensitivity (300);
    gainKnob.setTitle ("Input gain");
    gainKnob.onValueChange = [this]
    {
        engine.setInputGainDb ((float) gainKnob.getValue());
        repaint();
    };
    addAndMakeVisible (gainKnob);

    muteButton.setTitle ("Input mute");
    muteButton.onClick = [this] { engine.setInputMuted (! engine.isInputMuted()); };
    addAndMakeVisible (muteButton);

    refresh();
    startTimerHz (30);
}

void InputStrip::refresh()
{
    gainKnob.setValue (engine.getInputGainDb(), dontSendNotification);
    muteButton.setToggleState (engine.isInputMuted(), dontSendNotification);
    repaint();
}

void InputStrip::resized()
{
    auto area = getLocalBounds().toFloat().reduced (10.0f, 12.0f);

    muteButton.setBounds (area.removeFromBottom (48.0f).toNearestInt());
    area.removeFromBottom (8.0f);
    gainTextArea = area.removeFromBottom (18.0f);
    gainKnob.setBounds (area.removeFromBottom (64.0f).toNearestInt());
    area.removeFromBottom (10.0f);
    valueArea = area.removeFromBottom (40.0f);
    area.removeFromBottom (6.0f);
    meterArea = area;
}

void InputStrip::timerCallback()
{
    auto& tuner = engine.getTuner();
    const float peak = tuner.takeInputPeak();
    const auto now = Time::getMillisecondCounter();

    level = jmax (peak, level * 0.86f);

    if (peak >= hold)          { hold = peak; holdTime = now + 1500; }
    else if (now > holdTime)   { hold *= 0.9f; }

    maxPeak = jmax (maxPeak, peak);

    if (tuner.takeInputClipped())
        clipLatched = true;

    repaint (meterArea.getSmallestIntegerContainer().expanded (2));
    repaint (valueArea.getSmallestIntegerContainer());
}

void InputStrip::mouseDown (const MouseEvent&)
{
    clipLatched = false;
    maxPeak = 0.0f;
    repaint();
}

void InputStrip::paint (Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (Palette::cardOff);
    g.fillRoundedRectangle (bounds, 16.0f);
    g.setColour (Palette::border);
    g.drawRoundedRectangle (bounds, 16.0f, 1.0f);

    auto area = meterArea;
    const bool muted = engine.isInputMuted();

    // spia CLIP
    auto clipBox = area.removeFromTop (36.0f);
    g.setColour (clipLatched ? Palette::danger : Colour (0xff1a1c20));
    g.fillRoundedRectangle (clipBox, 8.0f);
    if (! clipLatched)
    {
        g.setColour (Palette::off);
        g.drawRoundedRectangle (clipBox.reduced (0.5f), 8.0f, 1.0f);
    }
    g.setColour (clipLatched ? Colours::white : Palette::textDim);
    g.setFont (Fonts::display (11.0f));
    g.drawText ("CLIP", clipBox, Justification::centred);
    area.removeFromTop (10.0f);

    // barra unica (l'ingresso è mono) a destra, scala a sinistra
    auto bar = area.removeFromRight (20.0f);
    area.removeFromRight (6.0f);

    g.setFont (Fonts::mono (10.0f));
    g.setColour (Palette::textFaint);
    for (float db : { 0.0f, -6.0f, -12.0f, -24.0f, -36.0f, -48.0f })
    {
        const float y = bar.getBottom() - dbToMeter (db) * bar.getHeight();
        g.drawText (String ((int) db), Rectangle<float> (area.getWidth(), 12.0f).withPosition (area.getX(), y - 6.0f),
                    Justification::centredRight);
    }

    g.setColour (Colour (0xff08090a));
    g.fillRoundedRectangle (bar, 3.0f);

    const float h = gainToMeter (level) * bar.getHeight();
    ColourGradient gradient (Palette::danger, bar.getX(), bar.getY(), Palette::on, bar.getX(), bar.getBottom(), false);
    gradient.addColour (1.0 - dbToMeter (-3.0f),  Colour (0xfff2c14e));
    gradient.addColour (1.0 - dbToMeter (-12.0f), Palette::on);
    g.setGradientFill (gradient);
    g.setOpacity (muted ? 0.35f : 1.0f);
    g.fillRoundedRectangle (bar.withTop (bar.getBottom() - h), 3.0f);
    g.setOpacity (1.0f);

    if (hold > 0.0f)
    {
        const float y = bar.getBottom() - gainToMeter (hold) * bar.getHeight();
        g.setColour (hold >= 1.0f ? Palette::danger : Palette::text);
        g.fillRect (bar.getX(), y - 1.0f, bar.getWidth(), 2.0f);
    }

    // picco massimo
    auto value = valueArea;
    const float maxDb = Decibels::gainToDecibels (maxPeak, -100.0f);
    g.setColour (maxDb >= 0.0f ? Palette::danger : (maxDb > -6.0f ? Palette::accent : Palette::text));
    g.setFont (Fonts::mono (15.0f));
    g.drawText (maxPeak > 0.0f ? String (maxDb, 1) : String ("-inf"),
                value.removeFromTop (22.0f), Justification::centred);
    g.setColour (Palette::textFaint);
    g.setFont (Fonts::display (9.0f));
    g.drawText ("INPUT dB", value, Justification::centred);

    // valore del gain sotto la manopola
    const float gainDb = engine.getInputGainDb();
    g.setColour (exactlyEqual (gainDb, 0.0f) ? Palette::textDim : Palette::accent);
    g.setFont (Fonts::mono (12.0f));
    g.drawText ((gainDb > 0.0f ? "+" : "") + String (gainDb, 1) + " dB", gainTextArea, Justification::centred);
}

//==============================================================================
MainComponent::MainComponent (Engine& e) : engine (e)
{
    LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
    setLookAndFeel (&lookAndFeel);

    for (auto* c : std::initializer_list<Component*> { &presetSelector, &tunerButton, &recButton, &statusDisplay,
                                                       &editButton, &settingsButton })
        addAndMakeVisible (c);

    presetSelector.onPrev  = [this] { confirmPresetSwitch ([this] { engine.loadAdjacentPreset (-1); }); };
    presetSelector.onNext  = [this] { confirmPresetSwitch ([this] { engine.loadAdjacentPreset (1); }); };
    presetSelector.onMenu  = [this] { showPresetMenu(); };
    tunerButton.onClick    = [this] { engine.setTunerActive (! engine.isTunerActive()); };
    editButton.onClick     = [this] { editMode = ! editMode; refresh(); };
    settingsButton.onClick = [this] { showSettingsMenu(); };
    settingsButton.setTitle ("Settings");
    recButton.onClick      = [this] { toggleRecording(); };

    for (int i = 0; i < Engine::numSlots; ++i)
        addAndMakeVisible (slotViews.add (new SlotView (*this, i)));

    for (int i = 0; i < Engine::numButtons; ++i)
        addAndMakeVisible (bigButtons.add (new BigButton (*this, i)));

    addAndMakeVisible (outputMeter);
    addAndMakeVisible (inputStrip);
    addChildComponent (tunerOverlay);

    engine.onProcessorAboutToBeDeleted = [this] (AudioProcessor* p) { pluginWindows.erase (p); };
    engine.addChangeListener (this);

    setSize (1368, 912);   // tablet 3:2 (2736x1824) al 200%
    refresh();
    updateStatus();
    startTimerHz (4);

    // ripristino dopo una chiusura inattesa: avvisa quando la finestra è visibile
    if (const auto message = engine.takeStartupMessage(); message.isNotEmpty())
    {
        SafePointer<MainComponent> safeThis (this);
        MessageManager::callAsync ([safeThis, message]
        {
            if (safeThis != nullptr)
                AlertWindow::showAsync (MessageBoxOptions::makeOptionsOk (MessageBoxIconType::InfoIcon,
                                                                          "Session restored", message),
                                        nullptr);
        });
    }
}

MainComponent::~MainComponent()
{
    stopTimer();
    engine.removeChangeListener (this);
    engine.onProcessorAboutToBeDeleted = nullptr;
    pluginWindows.clear();
    textInput.reset();

    setLookAndFeel (nullptr);
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

//==============================================================================
void MainComponent::paint (Graphics& g)
{
    g.fillAll (Palette::background);

    // intestazione della catena: titolo e riepilogo
    int active = 0, bypassed = 0, empty = 0;
    for (int i = 0; i < Engine::numSlots; ++i)
    {
        if (engine.isSlotEmpty (i))              ++empty;
        else if (engine.getSlotUserBypass (i))   ++bypassed;
        else if (engine.isSlotActive (i))        ++active;
    }

    auto header = chainHeader.toFloat().reduced (4.0f, 0.0f);
    g.setColour (Palette::textDim);
    g.setFont (Fonts::display (12.0f));
    g.drawText ("SIGNAL CHAIN", header, Justification::centredLeft);

    g.setFont (Fonts::mono (12.0f));
    g.drawText (String (active) + " on  " + String::fromUTF8 ("·") + "  " + String (bypassed) + " bypassed  " + String::fromUTF8 ("·") + "  "
                    + String (empty) + " empty",
                header, Justification::centredRight);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    area.removeFromLeft (24);
    area.removeFromRight (24);
    area.removeFromTop (20);
    area.removeFromBottom (24);

    // barra superiore
    auto top = area.removeFromTop (68);
    presetSelector.setBounds (top.removeFromLeft (jmin (460, top.getWidth() / 3)));
    top.removeFromLeft (12);
    tunerButton.setBounds (top.removeFromLeft (176));
    top.removeFromLeft (12);
    recButton.setBounds (top.removeFromLeft (150));
    settingsButton.setBounds (top.removeFromRight (68));
    top.removeFromRight (12);
    editButton.setBounds (top.removeFromRight (132));
    top.removeFromRight (4);
    statusDisplay.setBounds (top);

    area.removeFromTop (20);

    // pedali in basso
    auto pedals = area.removeFromBottom (jlimit (140, 210, getHeight() * 22 / 100));
    const int gap = 14;
    const int pw = (pedals.getWidth() - gap * (Engine::numButtons - 1)) / Engine::numButtons;
    for (int i = 0; i < Engine::numButtons; ++i)
    {
        bigButtons[i]->setBounds (pedals.removeFromLeft (pw));
        pedals.removeFromLeft (gap);
    }

    area.removeFromBottom (20);

    // misuratore d'uscita a destra, ingresso (gain e MUTE) a sinistra
    outputMeter.setBounds (area.removeFromRight (84));
    area.removeFromRight (20);
    inputStrip.setBounds (area.removeFromLeft (84));
    area.removeFromLeft (20);

    // catena: intestazione e due righe da cinque
    chainHeader = area.removeFromTop (22);
    area.removeFromTop (14);

    const int perRow = Engine::numSlots / 2;
    const int sw = (area.getWidth() - gap * (perRow - 1)) / perRow;
    const int sh = (area.getHeight() - gap) / 2;

    for (int row = 0; row < 2; ++row)
    {
        auto rowArea = area.removeFromTop (sh);
        area.removeFromTop (gap);

        for (int c = 0; c < perRow; ++c)
        {
            slotViews[row * perRow + c]->setBounds (rowArea.removeFromLeft (sw));
            rowArea.removeFromLeft (gap);
        }
    }

    tunerOverlay.setBounds (getLocalBounds());
}

//==============================================================================
void MainComponent::changeListenerCallback (ChangeBroadcaster*)
{
    refresh();
}

void MainComponent::timerCallback()
{
    updateRecButton();
    updateStatus();
}

void MainComponent::updateStatus()
{
    if (lastRecording != File() && Time::getMillisecondCounter() < lastRecordingShownUntil)
    {
        statusDisplay.setLines ("Saved to Downloads", lastRecording.getFileName(), true);
        return;
    }

    const auto lines = StringArray::fromLines (engine.getStatusText());
    statusDisplay.setLines (lines[0], lines[1], false);
}

void MainComponent::toggleRecording()
{
    auto& recorder = engine.getRecorder();

    if (recorder.isRecording())
    {
        recorder.stop();   // il messaggio "Saved to Downloads" lo mostra updateRecButton()
    }
    else
    {
        String error;
        if (! recorder.start (error))
            showError ("Recording", error);
    }

    timerCallback();
}

void MainComponent::updateRecButton()
{
    auto& recorder = engine.getRecorder();
    const bool recording = recorder.isRecording();

    // Appena finita (dal pulsante o dal motore, per un cambio di scheda audio): mostra il file
    if (! recording && recButton.getToggleState())
    {
        lastRecording = recorder.getLastSavedFile();
        lastRecordingShownUntil = Time::getMillisecondCounter() + 6000;
    }

    recButton.setToggleState (recording, dontSendNotification);

    if (recording)
    {
        const int s = (int) recorder.getSeconds();
        recButton.setValueText (String::formatted ("%d:%02d", s / 60, s % 60));
    }
    else
    {
        recButton.setValueText ({});
    }
}

void MainComponent::refresh()
{
    const auto files = engine.getPresetFiles();
    const int number = files.indexOf (engine.getCurrentPresetFile()) + 1;
    presetSelector.setPreset (engine.getCurrentPresetName(), number, files.size(), engine.hasUnsavedChanges());

    tunerButton.setToggleState (engine.isTunerActive(), dontSendNotification);
    editButton.setToggleState (editMode, dontSendNotification);

    tunerOverlay.setVisible (engine.isTunerActive());
    if (engine.isTunerActive())
        tunerOverlay.toFront (false);

    for (auto* s : slotViews)  s->repaint();
    for (auto* b : bigButtons) b->repaint();
    inputStrip.refresh();
    repaint();
}

void MainComponent::setFullScreen (bool shouldBeFullScreen)
{
    auto* top = getTopLevelComponent();
    Desktop::getInstance().setKioskModeComponent (shouldBeFullScreen ? top : nullptr, false);
    engine.props().setValue ("fullscreen", shouldBeFullScreen);
    engine.props().saveIfNeeded();
}

//==============================================================================
void MainComponent::showSlotMenu (int slot)
{
    const auto types = engine.knownPlugins.getTypes();
    const bool empty = engine.isSlotEmpty (slot);

    PopupMenu pluginMenu;
    KnownPluginList::addToMenu (pluginMenu, types, KnownPluginList::sortByManufacturer);

    enum { openEditor = 1, toggleBypass, renameSlot, moveLeft, moveRight, removePlugin, scanPlugins, kindBase = 200 };

    PopupMenu menu;
    menu.addSectionHeader ("Slot " + String (slot + 1) + (empty ? String() : " - " + engine.getSlotName (slot)));

    if (types.isEmpty() && empty)
    {
        menu.addItem (scanPlugins, "No plugins in the list: choose plugins...");
    }
    else if (empty)
    {
        menu.addSubMenu ("Choose plugin", pluginMenu);
    }
    else
    {
        // Tipo: decide icona ed etichetta. "Automatic" lo ricava dal plugin.
        PopupMenu kindMenu;
        const auto setting = engine.getSlotKindSetting (slot);
        kindMenu.addItem (kindBase, "Automatic (" + Engine::getSlotKindName (engine.guessSlotKind (slot)) + ")",
                          true, setting == Engine::SlotKind::automatic);
        kindMenu.addSeparator();
        for (auto k : Engine::allSlotKinds)
            kindMenu.addItem (kindBase + (int) k, Engine::getSlotKindName (k), true, setting == k);

        menu.addItem (openEditor, "Open plugin editor");
        menu.addItem (toggleBypass, engine.getSlotUserBypass (slot) ? "Enable" : "Bypass");
        menu.addSeparator();
        menu.addItem (renameSlot, "Rename...");
        menu.addSubMenu ("Type: " + Engine::getSlotKindName (engine.getSlotKind (slot)), kindMenu);
        menu.addSeparator();
        if (! types.isEmpty())
            menu.addSubMenu ("Replace with", pluginMenu);
        menu.addItem (moveLeft,  "Move left", slot > 0);
        menu.addItem (moveRight, "Move right",   slot < Engine::numSlots - 1);
        menu.addSeparator();
        menu.addItem (removePlugin, "Remove");
    }

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (slotViews[slot]),
                        [this, slot, types] (int result)
    {
        if (result == 0)
            return;

        const int chosen = KnownPluginList::getIndexChosenByMenu (types, result);
        if (chosen >= 0)
        {
            String error;
            if (! engine.loadPluginIntoSlot (slot, types.getReference (chosen), error))
                showError ("Could not load the plugin", types.getReference (chosen).name + "\n\n" + error);
            return;
        }

        if (result >= kindBase && result <= kindBase + (int) Engine::SlotKind::fx)
        {
            engine.setSlotKind (slot, (Engine::SlotKind) (result - kindBase));
            return;
        }

        switch (result)
        {
            case openEditor:   openPluginWindow (engine.getSlotProcessor (slot)); break;
            case toggleBypass: engine.setSlotUserBypass (slot, ! engine.getSlotUserBypass (slot)); break;
            case renameSlot:
                askText ("Slot name (leave empty to use the plugin name)", engine.getSlotName (slot),
                         [this, slot] (String text)
                         {
                             engine.setSlotCustomName (slot, text == engine.getSlotPluginName (slot) ? String() : text);
                         },
                         true);
                break;
            case moveLeft:     engine.swapSlots (slot, slot - 1); break;
            case moveRight:    engine.swapSlots (slot, slot + 1); break;
            case removePlugin: engine.clearSlot (slot); break;
            case scanPlugins:  showPluginChooser(); break;
            default: break;
        }
    });
}

int MainComponent::slotAt (Point<int> p) const
{
    for (int i = 0; i < slotViews.size(); ++i)
        if (slotViews[i]->getBounds().expanded (7).contains (p))
            return i;
    return -1;
}

void MainComponent::slotDragStarted (int slot, Point<int> grabOffset)
{
    dragSource = slot;
    dragTarget = -1;
    dragGrabOffset = grabOffset;
    dragImage = slotViews[slot]->createComponentSnapshot (slotViews[slot]->getLocalBounds(), true);
    slotViews[slot]->setAlpha (0.3f);   // il posto d'origine resta, attenuato
}

void MainComponent::slotDragged (Point<int> position)
{
    dragPosition = position;
    const int target = slotAt (position);
    dragTarget = (target == dragSource) ? -1 : target;
    repaint();
}

void MainComponent::slotDropped()
{
    const int from = dragSource, to = dragTarget;

    if (from >= 0)
        slotViews[from]->setAlpha (1.0f);

    dragSource = dragTarget = -1;
    dragImage = {};

    if (from >= 0 && to >= 0)
        engine.moveSlot (from, to);   // manda un change message: refresh() ridisegna tutto

    refresh();
}

void MainComponent::paintOverChildren (Graphics& g)
{
    if (dragSource < 0)
        return;

    // destinazione: bordo ambra e cosa succederà
    if (dragTarget >= 0)
    {
        const auto r = slotViews[dragTarget]->getBounds().toFloat().expanded (3.0f);
        g.setColour (Palette::accent);
        g.drawRoundedRectangle (r, 18.0f, 3.0f);

        const String hint = engine.isSlotEmpty (dragTarget) ? "MOVE HERE" : "INSERT HERE";
        const auto font = Fonts::display (12.0f);
        const float w = GlyphArrangement::getStringWidth (font, hint) + 24.0f;
        auto pill = Rectangle<float> (w, 28.0f).withCentre ({ r.getCentreX(), r.getBottom() - 30.0f });
        g.fillRoundedRectangle (pill, 14.0f);
        g.setColour (Palette::background);
        g.setFont (font);
        g.drawText (hint, pill, Justification::centred);
    }

    // il posto segue il dito, leggermente trasparente e con un'ombra
    if (dragImage.isValid())
    {
        const auto topLeft = dragPosition - dragGrabOffset;
        const auto area = Rectangle<float> ((float) topLeft.x, (float) topLeft.y,
                                            (float) dragImage.getWidth(), (float) dragImage.getHeight());
        g.setColour (Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (area.translated (0.0f, 10.0f).expanded (4.0f), 20.0f);
        g.setOpacity (0.92f);
        g.drawImage (dragImage, area);
    }
}

void MainComponent::bigButtonPressed (int index)
{
    if (editMode)
        showBigButtonEditMenu (index);
    else
        engine.toggleButton (index);
}

void MainComponent::showBigButtonEditMenu (int index)
{
    const auto& button = engine.getButton (index);

    enum { renameId = 1, slotBase = 100 };

    PopupMenu menu;
    menu.addSectionHeader ("Footswitch " + String (index + 1) + "  (MIDI CC " + String (engine.getButtonCC (index)) + ")");
    menu.addItem (renameId, "Name when no slots are assigned (now: " + button.label + ")...");
    menu.addSectionHeader ("Slots it switches on/off");

    for (int s = 0; s < Engine::numSlots; ++s)
    {
        const String name = engine.isSlotEmpty (s) ? String ("(empty)") : engine.getSlotName (s);
        menu.addItem (slotBase + s, String (s + 1) + ".  " + name, true, button.slots.contains (s));
    }

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (bigButtons[index]),
                        [this, index] (int result)
    {
        if (result == renameId)
        {
            askText ("Footswitch name when no slots are assigned", engine.getButton (index).label,
                     [this, index] (String text) { engine.setButtonLabel (index, text); });
        }
        else if (result >= slotBase)
        {
            const int s = result - slotBase;
            engine.setButtonControlsSlot (index, s, ! engine.getButton (index).slots.contains (s));
            showBigButtonEditMenu (index);   // riapri: comodo per assegnare più posti
        }
    });
}

//==============================================================================
void MainComponent::showPresetMenu()
{
    enum { save = 1, saveAs, newPreset, openFolder, tempo, presetBase = 1000 };

    const auto files = engine.getPresetFiles();
    const auto current = engine.getCurrentPresetFile();

    PopupMenu menu;
    menu.addItem (save, "Save", current.existsAsFile());
    menu.addItem (saveAs, "Save as...");
    menu.addItem (newPreset, "New empty preset");
    menu.addItem (openFolder, "Open preset folder");
    menu.addSeparator();
    menu.addItem (tempo, "Tempo: " + String (roundToInt (engine.getBpm())) + " BPM...");

    if (! files.isEmpty())
    {
        menu.addSectionHeader ("Preset");
        for (int i = 0; i < files.size(); ++i)
            menu.addItem (presetBase + i, files[i].getFileNameWithoutExtension(), true, files[i] == current);
    }

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (presetSelector),
                        [this, files] (int result)
    {
        if (result == save)
        {
            engine.savePreset (engine.getCurrentPresetFile());
        }
        else if (result == saveAs)
        {
            saveCurrentPresetAs (nullptr);
        }
        else if (result == newPreset)
        {
            confirmPresetSwitch ([this] { engine.newEmptyPreset(); });
        }
        else if (result == tempo)
        {
            askText ("Tempo for synced delays and reverbs (BPM)", String (roundToInt (engine.getBpm())),
                     [this] (String text)
                     {
                         if (const double bpm = text.getDoubleValue(); bpm > 0.0)
                             engine.setBpm (bpm);
                     });
        }
        else if (result == openFolder)
        {
            engine.getPresetFolder().startAsProcess();
        }
        else if (result >= presetBase && result - presetBase < files.size())
        {
            const auto file = files[result - presetBase];
            confirmPresetSwitch ([this, file]
            {
                String errors;
                if (! engine.loadPreset (file, errors))
                    showError ("Some plugins could not be loaded", errors);
            });
        }
    });
}

void MainComponent::saveCurrentPresetAs (std::function<void()> afterSave)
{
    askText ("Save preset as", engine.getCurrentPresetFile().getFileNameWithoutExtension(),
             [this, afterSave] (String name)
    {
        const auto file = engine.getPresetFolder().getChildFile (File::createLegalFileName (name));
        if (! engine.savePreset (file))
            showError ("Save failed", file.getFullPathName());
        else if (afterSave != nullptr)
            afterSave();
    });
}

void MainComponent::confirmPresetSwitch (std::function<void()> switchPreset)
{
    if (! engine.hasUnsavedChanges())
    {
        switchPreset();
        return;
    }

    const auto options = MessageBoxOptions()
                             .withIconType (MessageBoxIconType::QuestionIcon)
                             .withTitle ("Unsaved changes")
                             .withMessage ("The preset \"" + engine.getCurrentPresetName() + "\" has unsaved changes.")
                             .withButton ("Save and switch", 1)
                             .withButton ("Switch without saving", 2)
                             .withButton ("Cancel", 0);

    SafePointer<MainComponent> safeThis (this);
    AlertWindow::showAsync (options, [safeThis, switchPreset] (int result)
    {
        if (safeThis == nullptr)
            return;

        if (result == 1)   // salva e cambia
        {
            const auto current = safeThis->engine.getCurrentPresetFile();

            if (current.existsAsFile())
            {
                if (safeThis->engine.savePreset (current))
                    switchPreset();
                else
                    safeThis->showError ("Save failed", current.getFullPathName());
            }
            else
            {
                safeThis->saveCurrentPresetAs (switchPreset);   // preset nuovo: prima serve un nome
            }
        }
        else if (result == 2)   // cambia senza salvare
        {
            switchPreset();
        }
    });
}

void MainComponent::showSettingsMenu()
{
    enum { audioSettings = 1, pluginList, midiMomentary, midiAbsolute, fullScreen, quit, inputBase = 100 };

    PopupMenu inputMenu;
    const int numInputs = engine.getNumDeviceInputs();
    for (int i = 0; i < jmax (1, numInputs); ++i)
        inputMenu.addItem (inputBase + i, "Input " + String (i + 1), i < numInputs, engine.getInputChannel() == i);

    PopupMenu midiMenu;
    midiMenu.addItem (midiMomentary, "Momentary: each press toggles", true, engine.getMidiMode() == Engine::MidiMode::momentary);
    midiMenu.addItem (midiAbsolute,  "Absolute: the value decides (127 = on, 0 = off)", true, engine.getMidiMode() == Engine::MidiMode::absolute);
    midiMenu.addSeparator();
    midiMenu.addItem (-1, "Footswitches: CC " + String (engine.getButtonCC (0)) + "-" + String (engine.getButtonCC (Engine::numButtons - 1))
                            + "   Tuner: CC " + String (engine.getTunerCC()), false);
    midiMenu.addItem (-2, "Presets: CC " + String (engine.getNextPresetCC()) + " next, CC " + String (engine.getPrevPresetCC())
                            + " previous, Program Change = preset number", false);

    const bool isFullScreen = Desktop::getInstance().getKioskModeComponent() != nullptr;

    PopupMenu menu;
    menu.addSectionHeader (String ("ChainRig ") + JUCE_APPLICATION_VERSION_STRING);
    menu.addItem (audioSettings, "Audio device...");
    menu.addItem (pluginList, "Choose plugins...");
    menu.addSubMenu ("Guitar input", inputMenu);
    menu.addSubMenu ("MIDI foot controller", midiMenu);
    menu.addItem (fullScreen, "Full screen", true, isFullScreen);
    menu.addSeparator();
    menu.addItem (quit, "Quit");

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (settingsButton),
                        [this, isFullScreen] (int result)
    {
        switch (result)
        {
            case audioSettings: showAudioSettings(); break;
            case pluginList:    showPluginChooser(); break;
            case midiMomentary: engine.setMidiMode (Engine::MidiMode::momentary); break;
            case midiAbsolute:  engine.setMidiMode (Engine::MidiMode::absolute); break;
            case fullScreen:    setFullScreen (! isFullScreen); break;
            case quit:          JUCEApplication::getInstance()->systemRequestedQuit(); break;
            default:
                if (result >= inputBase)
                    engine.setInputChannel (result - inputBase);
                break;
        }
    });
}

//==============================================================================
void MainComponent::launchDialog (const String& title, Component* content)
{
    DialogWindow::LaunchOptions options;
    options.content.setOwned (content);
    options.dialogTitle = title;
    options.dialogBackgroundColour = Palette::panel;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;

    if (auto* window = options.launchAsync())
        window->setAlwaysOnTop (true);
}

void MainComponent::showAudioSettings()
{
    auto* selector = new AudioDeviceSelectorComponent (engine.deviceManager, 1, 2, 2, 2,
                                                       true, false, false, false);
    selector->setItemHeight (36);
    selector->setSize (640, 560);
    launchDialog ("Audio device", selector);
}

void MainComponent::showPluginChooser()
{
    SafePointer<MainComponent> safeThis (this);
    launchDialog ("Choose plugin", new PluginChooser (engine, [safeThis]
    {
        if (safeThis != nullptr)
            safeThis->showPluginList();
    }));
}

void MainComponent::showPluginList()
{
    auto* list = new PluginListComponent (engine.formatManager, engine.knownPlugins,
                                          engine.getDeadMansPedalFile(), &engine.props(), true);
    list->setSize (760, 560);
    launchDialog ("Full plugin list (advanced)", list);
}

void MainComponent::openPluginWindow (AudioProcessor* processor)
{
    if (processor == nullptr)
        return;

    if (auto it = pluginWindows.find (processor); it != pluginWindows.end())
    {
        it->second->toFront (true);
        return;
    }

    SafePointer<MainComponent> safeThis (this);
    pluginWindows[processor] = std::make_unique<PluginWindow> (*processor, [safeThis, processor]
    {
        // chiudi in modo asincrono: la finestra non può cancellarsi mentre gestisce il suo evento
        MessageManager::callAsync ([safeThis, processor]
        {
            if (safeThis != nullptr)
                safeThis->pluginWindows.erase (processor);
        });
    });
}

void MainComponent::askText (const String& title, const String& initial, std::function<void (String)> onOk,
                             bool allowEmpty)
{
    textInput = std::make_unique<AlertWindow> (title, String(), MessageBoxIconType::NoIcon);
    textInput->addTextEditor ("text", initial);
    textInput->addButton ("OK", 1, KeyPress (KeyPress::returnKey));
    textInput->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    textInput->setAlwaysOnTop (true);

    SafePointer<MainComponent> safeThis (this);
    textInput->enterModalState (true, ModalCallbackFunction::create ([safeThis, onOk, allowEmpty] (int result)
    {
        if (safeThis == nullptr || safeThis->textInput == nullptr)
            return;

        const auto text = safeThis->textInput->getTextEditorContents ("text").trim();
        safeThis->textInput->setVisible (false);

        if (result == 1 && (text.isNotEmpty() || allowEmpty))
            onOk (text);
    }), false);
}

void MainComponent::showError (const String& title, const String& message)
{
    AlertWindow::showAsync (MessageBoxOptions()
                                .withIconType (MessageBoxIconType::WarningIcon)
                                .withTitle (title)
                                .withMessage (message)
                                .withButton ("OK", 1),
                            nullptr);
}
