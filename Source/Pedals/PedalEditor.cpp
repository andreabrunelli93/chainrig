#include "PedalProcessor.h"
#include "../Look.h"

using namespace juce;

namespace
{
    // angoli delle manopole (quelli predefiniti di Slider: da circa le 7 alle 5)
    constexpr float startAngle = MathConstants<float>::pi * 1.2f;
    constexpr float endAngle   = MathConstants<float>::pi * 2.8f;
    constexpr float cacheScale = 2.0f;   // il corpo è disegnato una volta, a doppia risoluzione

    /** Rumore deterministico (sempre lo stesso pedale "usato"): valore 0-1 per un punto intero. */
    float hash (int x, int y, uint32 seed)
    {
        uint32 h = (uint32) x * 374761393u + (uint32) y * 668265263u + seed * 2246822519u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return (float) (h ^ (h >> 16)) / 4294967295.0f;
    }

    /** Rumore a valori interpolati, liscio: chiazze di grandezza ~1 unità. */
    float valueNoise (float x, float y, uint32 seed)
    {
        const int ix = (int) std::floor (x), iy = (int) std::floor (y);
        const float fx = x - (float) ix, fy = y - (float) iy;
        const float sx = fx * fx * (3.0f - 2.0f * fx), sy = fy * fy * (3.0f - 2.0f * fy);
        const float a = hash (ix, iy, seed), b = hash (ix + 1, iy, seed);
        const float c = hash (ix, iy + 1, seed), d = hash (ix + 1, iy + 1, seed);
        return jmap (sy, jmap (sx, a, b), jmap (sx, c, d));
    }

    /** Manopola nera da pedale: gonna, faccia con riflesso, linea di riferimento chiara. */
    class PedalKnobLook final : public LookAndFeel_V4
    {
    public:
        explicit PedalKnobLook (const PedalProcessor::Look& l) : look (l) {}

        void drawRotarySlider (Graphics& g, int x, int y, int width, int height, float pos,
                               float start, float end, Slider&) override
        {
            const auto box = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (6.0f);
            const float size = jmin (box.getWidth(), box.getHeight());
            const auto knob = box.withSizeKeepingCentre (size, size);
            const auto c = knob.getCentre();
            const float angle = start + pos * (end - start);

            g.setColour (Colours::black.withAlpha (0.35f));
            g.fillEllipse (knob.translated (0.0f, size * 0.05f).expanded (1.0f));

            g.setGradientFill (ColourGradient (look.knob.brighter (0.35f), c.x, knob.getY(),
                                               look.knob.darker (0.6f), c.x, knob.getBottom(), false));
            g.fillEllipse (knob);

            const auto face = knob.reduced (size * 0.1f);
            g.setGradientFill (ColourGradient (look.knob.brighter (0.55f), face.getX() + face.getWidth() * 0.3f, face.getY(),
                                               look.knob, face.getCentreX(), face.getBottom(), true));
            g.fillEllipse (face);
            g.setColour (Colours::black.withAlpha (0.5f));
            g.drawEllipse (face, 1.0f);

            const float r1 = size * 0.12f, r2 = size * 0.44f;
            Path line;
            line.startNewSubPath (c.x + r1 * std::sin (angle), c.y - r1 * std::cos (angle));
            line.lineTo (c.x + r2 * std::sin (angle), c.y - r2 * std::cos (angle));
            g.setColour (look.pointer);
            g.strokePath (line, PathStrokeType (size * 0.05f, PathStrokeType::curved, PathStrokeType::rounded));
        }

    private:
        PedalProcessor::Look look;
    };
}

//==============================================================================
/** Interruttore del pedale: pulsante inciso con la spia, colori del pedale. */
class PedalSwitch final : public Button
{
public:
    PedalSwitch (const String& text, const PedalProcessor::Look& l) : Button (text), look (l)
    {
        setClickingTogglesState (true);
    }

    void paintButton (Graphics& g, bool isMouseOver, bool isDown) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (look.enclosureBottom);   // pieno: l'usura del corpo non deve vedersi sotto la scritta
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (Colours::black.withAlpha (isDown ? 0.45f : 0.3f));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (look.ink.withAlpha (isMouseOver ? 0.9f : 0.6f));
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.2f);

        const bool on = getToggleState();
        const auto led = juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ r.getX() + 20.0f, r.getCentreY() });
        if (on)
        {
            g.setColour (look.led.withAlpha (0.35f));
            g.fillEllipse (led.expanded (4.0f));
        }
        g.setColour (on ? look.led : Colours::black.withAlpha (0.5f));
        g.fillEllipse (led);

        g.setColour (look.ink);
        g.setFont (Fonts::display (14.0f));
        g.drawText (getButtonText().toUpperCase(), r.withTrimmedLeft (34.0f).withTrimmedRight (12.0f), Justification::centred);
    }

private:
    PedalProcessor::Look look;
};

//==============================================================================
/** Selettore a più posizioni: stesso pulsante inciso, scritta "NOME VALORE"; un clic passa alla
    posizione dopo (dall'ultima torna alla prima). */
class PedalSelector final : public Button
{
public:
    PedalSelector (const PedalProcessor::Selector& s, const PedalProcessor::Look& l)
        : Button (s.parameter->getName (32)), selector (s), look (l) {}

    void paintButton (Graphics& g, bool isMouseOver, bool isDown) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (look.enclosureBottom);   // pieno: l'usura del corpo non deve vedersi sotto la scritta
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (Colours::black.withAlpha (isDown ? 0.45f : 0.3f));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (look.ink.withAlpha (isMouseOver ? 0.9f : 0.6f));
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.2f);

        auto text = r.reduced (14.0f, 0.0f);
        g.setColour (look.ink.withAlpha (0.7f));
        g.setFont (Fonts::display (14.0f));
        g.drawText (getButtonText().toUpperCase(), text, Justification::centredLeft);
        g.setColour (look.ink);
        g.setFont (Fonts::mono (16.0f));
        shown = selector.getIndex();
        g.drawText (selector.choices[shown], text, Justification::centredRight);
    }

    void clicked() override
    {
        auto* p = selector.parameter;
        p->beginChangeGesture();
        p->set ((float) ((selector.getIndex() + 1) % selector.choices.size()));
        p->endChangeGesture();
        repaint();
    }

    /** Dal timer della finestra: ridisegna solo se la posizione è cambiata da fuori (preset, MIDI). */
    void sync()
    {
        if (selector.getIndex() != shown)
            repaint();
    }

private:
    PedalProcessor::Selector selector;
    PedalProcessor::Look look;
    int shown = -1;
};

//==============================================================================
/** Finestra di un pedale integrato, disegnata come un pedale: corpo, spia, manopole 0-10 in fila
    con le tacche incise, disegno facoltativo, nome; facoltativamente consumato dall'uso.
    I colori vengono da PedalProcessor::Info::look. Tutto ciò che non cambia (corpo, scritte, usura)
    è disegnato una volta in un'immagine; a ogni repaint si aggiungono solo i valori. */
class PedalEditor final : public AudioProcessorEditor,
                          private Timer
{
public:
    explicit PedalEditor (PedalProcessor& p)
        : AudioProcessorEditor (p), pedal (p), look (p.getInfo().look), knobLook (look)
    {
        for (auto* param : pedal.getKnobs())
        {
            auto* knob = knobs.add (new Slider());
            knob->setLookAndFeel (&knobLook);
            knob->setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
            knob->setRotaryParameters (startAngle, endAngle, true);
            knob->setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
            knob->setRange (param->range.start, param->range.end, 0.1);
            knob->setDoubleClickReturnValue (true, param->getDefaultRealValue());
            knob->setMouseDragSensitivity (300);
            knob->setTitle (param->getName (32));
            knob->setValue (param->get(), dontSendNotification);
            knob->onDragStart  = [param] { param->beginChangeGesture(); };
            knob->onDragEnd    = [param] { param->endChangeGesture(); };
            knob->onValueChange = [this, knob, param]
            {
                param->set ((float) knob->getValue());
                repaint();
            };
            addAndMakeVisible (knob);
        }

        for (auto* param : pedal.getSwitches())
        {
            auto* sw = switches.add (new PedalSwitch (param->getName (32), look));
            sw->setToggleState (param->isOn(), dontSendNotification);
            sw->onClick = [param, sw]
            {
                param->beginChangeGesture();
                param->set (sw->getToggleState() ? 1.0f : 0.0f);
                param->endChangeGesture();
            };
            addAndMakeVisible (sw);
        }

        for (auto& s : pedal.getSelectors())
            addAndMakeVisible (selectors.add (new PedalSelector (s, look)));

        startTimerHz (10);   // parametri cambiati da fuori (preset, MIDI)
        setSize (look.width > 0 ? look.width : jmax (640, 150 * knobs.size() + 260),
                 look.height > 0 ? look.height : (look.emblem.empty() && ! look.footPlate ? 590 : 720));
    }

    ~PedalEditor() override
    {
        for (auto* k : knobs)
            k->setLookAndFeel (nullptr);
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Palette::panel);
        g.drawImage (enclosure, getLocalBounds().toFloat());

        // valori delle manopole (l'unica scritta che cambia)
        for (int i = 0; i < knobs.size(); ++i)
        {
            const auto b = knobs[i]->getBounds().toFloat();
            g.setColour (look.ink.withAlpha (0.65f));
            g.setFont (Fonts::mono (15.0f));
            g.drawText (String (knobs[i]->getValue(), 1), valueArea (b), Justification::centred);
        }
    }

    void resized() override
    {
        body = getLocalBounds().reduced (60, 24).toFloat();

        const int n = jmax (1, knobs.size());
        const int size = 112;
        const float step = (body.getWidth() - 50.0f) / (float) n;
        const int y = (int) body.getY() + 86;

        for (int i = 0; i < knobs.size(); ++i)
        {
            if (i < (int) look.knobPlaces.size())
            {
                const auto& place = look.knobPlaces[(size_t) i];
                const int s = roundToInt (place.size);
                const int cx = (int) (body.getX() + place.x * body.getWidth());
                knobs[i]->setBounds (cx - s / 2, (int) (body.getY() + place.top), s, s);
            }
            else
            {
                const int cx = (int) (body.getX() + 25.0f + step * ((float) i + 0.5f));
                knobs[i]->setBounds (cx - size / 2, y, size, size);
            }
        }

        // selettori e interruttori in fila in basso al centro, sopra il bordo del corpo
        {
            Array<Component*> row;
            for (auto* s : selectors) row.add (s);
            for (auto* s : switches)  row.add (s);
            const int w = selectors.isEmpty() ? 150 : 170, h = 40, gap = 16;
            const int total = row.size() * w + jmax (0, row.size() - 1) * gap;
            int x = (int) body.getCentreX() - total / 2;
            for (auto* sw : row)
            {
                sw->setBounds (x, (int) body.getBottom() - 78, w, h);
                x += w + gap;
            }
        }

        // placca: dal fondo delle etichette in giù, lasciando spazio al nome
        if (look.footPlate)
        {
            float bottom = 0.0f;
            for (auto* k : knobs)
                bottom = jmax (bottom, (float) k->getBottom() - body.getY());
            nameTop = bottom + 62.0f;
            plate = body.withTrimmedTop (nameTop + 112.0f).reduced (44.0f, 0.0f).withTrimmedBottom (40.0f);
        }

        renderEnclosure();
    }

private:
    static juce::Rectangle<float> labelArea (juce::Rectangle<float> knob)
    {
        return knob.withY (knob.getBottom() + 18.0f).withHeight (20.0f).expanded (30.0f, 0.0f);
    }

    static juce::Rectangle<float> valueArea (juce::Rectangle<float> knob)
    {
        return labelArea (knob).translated (0.0f, 21.0f);
    }

    //==============================================================================
    void renderEnclosure()
    {
        const int w = roundToInt ((float) getWidth() * cacheScale), h = roundToInt ((float) getHeight() * cacheScale);
        if (w <= 0 || h <= 0)
            return;

        enclosure = Image (Image::ARGB, w, h, true);
        {
            Graphics g (enclosure);
            g.addTransform (AffineTransform::scale (cacheScale));
            paintBody (g);
            if (look.wear > 0.0f)
                paintWear (g);
        }

        // inchiostro su livelli a parte, consumati e poi appoggiati sul corpo: le tacche e le
        // etichette poco (devono restare leggibili), disegno e nome molto
        Graphics g (enclosure);
        for (const bool art : { false, true })
        {
            Image ink (Image::ARGB, w, h, true);
            {
                Graphics gi (ink);
                gi.addTransform (AffineTransform::scale (cacheScale));
                if (art) paintArt (gi); else paintInk (gi);
            }
            if (look.wear > 0.0f)
                wearInk (ink, art ? look.wear : look.wear * 0.25f);
            g.drawImageAt (ink, 0, 0);
        }
    }

    void paintBody (Graphics& g)
    {
        g.setColour (Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (body.translated (0.0f, 6.0f).expanded (2.0f), 22.0f);

        g.setGradientFill (ColourGradient (look.enclosureTop, body.getX(), body.getY(),
                                           look.enclosureBottom, body.getRight(), body.getBottom(), false));
        g.fillRoundedRectangle (body, 20.0f);

        if (look.brushed)
        {
            // metallo spazzolato: righe orizzontali sottili, sempre uguali (seme fisso)
            Graphics::ScopedSaveState state (g);
            Path clip;
            clip.addRoundedRectangle (body, 20.0f);
            g.reduceClipRegion (clip);

            Random random (7);
            for (float yy = body.getY(); yy < body.getBottom(); yy += 1.5f)
            {
                const bool light = random.nextBool();
                g.setColour ((light ? Colours::white : Colours::black).withAlpha (0.015f + 0.035f * random.nextFloat()));
                const float x0 = body.getX() + random.nextFloat() * body.getWidth() * 0.3f;
                g.drawLine (x0, yy, x0 + body.getWidth() * (0.4f + 0.6f * random.nextFloat()), yy, 0.6f);
            }
        }

        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.45f), body.getX(), body.getY(),
                                           Colours::black.withAlpha (0.35f), body.getX(), body.getBottom(), false));
        g.drawRoundedRectangle (body.reduced (1.0f), 19.0f, 2.0f);

        for (auto p : { body.getTopLeft() + Point<float> (20.0f, 20.0f), body.getTopRight() + Point<float> (-20.0f, 20.0f),
                        body.getBottomLeft() + Point<float> (20.0f, -20.0f), body.getBottomRight() + Point<float> (-20.0f, -20.0f) })
        {
            const auto screw = juce::Rectangle<float> (12.0f, 12.0f).withCentre (p);
            g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.8f), screw.getX(), screw.getY(),
                                               Colour (0xff6e6a62), screw.getRight(), screw.getBottom(), false));
            g.fillEllipse (screw);
            g.setColour (Colours::black.withAlpha (0.45f));
            g.drawEllipse (screw, 0.8f);
            g.drawLine (screw.getX() + 3.0f, screw.getCentreY(), screw.getRight() - 3.0f, screw.getCentreY(), 1.2f);
        }

        if (look.footPlate && ! plate.isEmpty())
        {
            // fessura scura attorno, poi la placca con smusso chiaro in alto e ombra in basso
            g.setColour (Colours::black.withAlpha (0.45f));
            g.fillRoundedRectangle (plate.expanded (5.0f), 12.0f);
            g.setGradientFill (ColourGradient (look.enclosureTop.brighter (0.08f), plate.getX(), plate.getY(),
                                               look.enclosureBottom.darker (0.15f), plate.getX(), plate.getBottom(), false));
            g.fillRoundedRectangle (plate, 9.0f);
            g.setColour (Colours::white.withAlpha (0.35f));
            g.drawLine (plate.getX() + 10.0f, plate.getY() + 1.5f, plate.getRight() - 10.0f, plate.getY() + 1.5f, 1.5f);
            g.setColour (Colours::black.withAlpha (0.35f));
            g.drawLine (plate.getX() + 10.0f, plate.getBottom() - 1.5f, plate.getRight() - 10.0f, plate.getBottom() - 1.5f, 2.0f);
            g.setColour (Colours::black.withAlpha (0.18f));
            g.drawRoundedRectangle (plate.reduced (10.0f), 6.0f, 1.0f);   // bordo dello stampo
        }

        // spia accesa (la finestra è aperta solo sui posti caricati)
        const auto led = juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ body.getCentreX(), body.getY() + 40.0f });
        g.setColour (look.led.withAlpha (0.25f));
        g.fillEllipse (led.expanded (9.0f));
        g.setColour (look.led.withAlpha (0.45f));
        g.fillEllipse (led.expanded (4.0f));
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.9f), led.getCentreX() - 2.0f, led.getCentreY() - 3.0f,
                                           look.led, led.getCentreX(), led.getBottom(), true));
        g.fillEllipse (led);
        g.setColour (Colours::black.withAlpha (0.4f));
        g.drawEllipse (led.expanded (1.5f), 1.2f);
    }

    /** Vernice scrostata (si vede l'alluminio), soprattutto sui bordi e in basso, e graffi. */
    void paintWear (Graphics& g)
    {
        Graphics::ScopedSaveState state (g);
        Path clip;
        clip.addRoundedRectangle (body, 20.0f);
        g.reduceClipRegion (clip);

        Random random (1934);
        const auto metal = [] (Graphics& gg, const Path& p, juce::Rectangle<float> b)
        {
            gg.setGradientFill (ColourGradient (Colour (0xffd9d5cb), b.getX(), b.getY(),
                                                Colour (0xffaaa59b), b.getRight(), b.getBottom(), false));
            gg.fillPath (p);
            gg.setColour (Colour (0xff4a3a1c).withAlpha (0.22f));   // bordo della vernice
            gg.strokePath (p, PathStrokeType (0.6f));
        };

        const auto chip = [&] (Point<float> c, float radius, float stretchX = 1.0f, float stretchY = 1.0f)
        {
            Path p;
            const int points = 16;
            for (int i = 0; i < points; ++i)
            {
                const float a = MathConstants<float>::twoPi * (float) i / (float) points;
                const float r = radius * (0.55f + 0.6f * random.nextFloat());
                const Point<float> q (c.x + r * std::cos (a) * stretchX, c.y + r * std::sin (a) * 0.8f * stretchY);
                if (i == 0) p.startNewSubPath (q); else p.lineTo (q);
            }
            p.closeSubPath();
            metal (g, p.createPathWithRoundedCorners (radius * 0.4f), p.getBounds());
        };

        // bordi e angoli: dove il pedale sbatte
        // bordi e angoli: dove il pedale sbatte; scrostature allungate lungo il bordo, quasi tutte vicine
        const int edgeChips = roundToInt (60.0f * look.wear);
        for (int i = 0; i < edgeChips; ++i)
        {
            const float t = random.nextFloat();
            const float inset = 1.0f + std::pow (random.nextFloat(), 4.0f) * 22.0f;
            const float radius = 1.5f + std::pow (random.nextFloat(), 3.0f) * 9.0f;
            const float stretch = 1.4f + random.nextFloat() * 1.6f;
            switch (random.nextInt (4))
            {
                case 0:  chip ({ body.getX() + t * body.getWidth(), body.getY() + inset }, radius, stretch, 1.0f); break;
                case 1:  chip ({ body.getX() + t * body.getWidth(), body.getBottom() - inset }, radius, stretch, 1.0f); break;
                case 2:  chip ({ body.getX() + inset, body.getY() + t * body.getHeight() }, radius, 1.0f, stretch); break;
                default: chip ({ body.getRight() - inset, body.getY() + t * body.getHeight() }, radius, 1.0f, stretch); break;
            }
        }

        // in basso al centro, dove arriva la scarpa: vernice consumata a strisce orizzontali
        for (int i = 0; i < roundToInt (16.0f * look.wear); ++i)
            chip ({ body.getCentreX() + (random.nextFloat() - 0.5f) * 170.0f,
                    body.getBottom() - 22.0f - random.nextFloat() * 36.0f },
                  1.5f + random.nextFloat() * 5.0f, 2.5f, 0.8f);

        // qualche colpo sparso sulla faccia
        for (int i = 0; i < roundToInt (8.0f * look.wear); ++i)
            chip ({ body.getX() + 30.0f + random.nextFloat() * (body.getWidth() - 60.0f),
                    body.getY() + 30.0f + random.nextFloat() * (body.getHeight() - 60.0f) },
                  1.0f + random.nextFloat() * 2.5f);

        // graffi: una riga chiara (metallo) con l'ombra scura accanto
        for (int i = 0; i < roundToInt (90.0f * look.wear); ++i)
        {
            const Point<float> a (body.getX() + random.nextFloat() * body.getWidth(),
                                  body.getY() + random.nextFloat() * body.getHeight());
            const float angle = random.nextFloat() * MathConstants<float>::pi;
            const float length = 6.0f + std::pow (random.nextFloat(), 2.0f) * 80.0f;
            const Point<float> b (a.x + length * std::cos (angle), a.y + length * std::sin (angle));
            const Point<float> mid ((a + b) * 0.5f + Point<float> ((random.nextFloat() - 0.5f) * 8.0f, (random.nextFloat() - 0.5f) * 8.0f));

            Path scratch;
            scratch.startNewSubPath (a);
            scratch.quadraticTo (mid, b);
            g.setColour (Colours::black.withAlpha (0.12f + 0.12f * random.nextFloat()));
            g.strokePath (scratch, PathStrokeType (0.9f), AffineTransform::translation (0.6f, 0.8f));
            g.setColour (Colours::white.withAlpha (0.25f + 0.3f * random.nextFloat()));
            g.strokePath (scratch, PathStrokeType (0.6f + 0.6f * random.nextFloat()));
        }

        // sporco verso i bordi
        g.setGradientFill (ColourGradient (Colours::transparentBlack, body.getCentreX(), body.getCentreY(),
                                           Colour (0xff2a2010).withAlpha (0.28f * look.wear), body.getX(), body.getY(), true));
        g.fillRoundedRectangle (body, 20.0f);
    }

    /** Inchiostro funzionale: tacche ed etichette delle manopole (resta leggibile). */
    void paintInk (Graphics& g)
    {
        const auto engrave = [&g, this] (const String& s, juce::Rectangle<float> area, const Font& font)
        {
            g.setFont (font);
            g.setColour (Colours::white.withAlpha (0.35f));   // bordo chiaro dell'incisione
            g.drawText (s, area.translated (0.0f, 1.2f), Justification::centred);
            g.setColour (look.ink);
            g.drawText (s, area, Justification::centred);
        };

        for (int i = 0; i < knobs.size(); ++i)
        {
            const auto b = knobs[i]->getBounds().toFloat();
            const auto c = b.getCentre();
            const float radius = b.getWidth() * 0.5f;

            g.setColour (look.ink.withAlpha (0.8f));
            for (int t = 0; t <= 10; ++t)
            {
                const float a = startAngle + (float) t / 10.0f * (endAngle - startAngle);
                const float r1 = radius + 3.0f, r2 = radius + (t % 5 == 0 ? 13.0f : 8.0f);
                g.drawLine (c.x + r1 * std::sin (a), c.y - r1 * std::cos (a),
                            c.x + r2 * std::sin (a), c.y - r2 * std::cos (a), t % 5 == 0 ? 2.2f : 1.4f);
            }

            engrave (pedal.getKnobs()[i]->getName (32).toUpperCase(), labelArea (b), Fonts::display (16.0f));
        }

    }

    /** Inchiostro decorativo (disegno, nome, sottotitolo): può consumarsi molto. */
    void paintArt (Graphics& g)
    {
        const auto engrave = [&g, this] (const String& s, juce::Rectangle<float> area, const Font& font)
        {
            g.setFont (font);
            g.setColour (Colours::white.withAlpha (0.35f));
            g.drawText (s, area.translated (0.0f, 1.2f), Justification::centred);
            g.setColour (look.ink);
            g.drawText (s, area, Justification::centred);
        };

        float top = look.nameTop > 0.0f ? look.nameTop : 310.0f;
        if (! look.emblem.empty())
        {
            const auto area = juce::Rectangle<float> (230.0f, 200.0f).withCentre ({ body.getCentreX(), body.getY() + 375.0f });
            const auto bounds = Artwork::getBounds (look.emblem);
            const auto fit = RectanglePlacement (RectanglePlacement::centred).getTransformToFit (bounds, area);
            g.setColour (Colours::white.withAlpha (0.3f));
            for (auto& part : look.emblem.fills)
                g.fillPath (part, fit.translated (0.0f, 1.2f));
            g.setColour (look.ink);
            for (auto& part : look.emblem.fills)
                g.fillPath (part, fit);
            g.setColour (look.enclosureTop.interpolatedWith (look.enclosureBottom, 0.5f));
            for (auto& part : look.emblem.cuts)
                g.fillPath (part, fit);
            top = 490.0f;
        }

        if (look.footPlate)
        {
            auto text = body.withTrimmedTop (nameTop).reduced (30.0f, 0.0f);
            engrave (pedal.getInfo().name.toUpperCase(), text.removeFromTop (54.0f), Fonts::display (44.0f));
            if (look.subtitle.isNotEmpty())
                engrave (look.subtitle, text.removeFromTop (30.0f), Fonts::display (14.0f));
            return;
        }

        auto text = body.withTrimmedTop (top).reduced (30.0f, 0.0f);
        const auto words = StringArray::fromTokens (pedal.getInfo().name.toUpperCase(), " ", "");
        engrave (words[0], text.removeFromTop (58.0f), Fonts::display (50.0f));
        if (words.size() > 1)
        {
            StringArray rest (words);
            rest.remove (0);
            engrave (rest.joinIntoString (" "), text.removeFromTop (40.0f), Fonts::display (32.0f));
        }

        if (look.subtitle.isNotEmpty())
        {
            text.removeFromTop (14.0f);
            g.setColour (look.ink.withAlpha (0.45f));
            g.drawLine (body.getCentreX() - 110.0f, text.getY(), body.getCentreX() + 110.0f, text.getY(), 1.0f);
            engrave (look.subtitle, text.removeFromTop (30.0f).translated (0.0f, 4.0f), Fonts::display (14.0f));
        }
    }

    /** Inchiostro consumato: chiazze dove è sparito (rumore liscio) e grana fine. */
    static void wearInk (Image& ink, float wear)
    {
        const float threshold = 0.45f * wear;
        Image::BitmapData data (ink, Image::BitmapData::readWrite);

        for (int y = 0; y < data.height; ++y)
        {
            for (int x = 0; x < data.width; ++x)
            {
                auto pixel = data.getPixelColour (x, y);
                if (pixel.getAlpha() == 0)
                    continue;

                const float n = 0.6f * valueNoise ((float) x / 70.0f, (float) y / 70.0f, 11)
                              + 0.4f * valueNoise ((float) x / 18.0f, (float) y / 18.0f, 23);
                float keep = jlimit (0.0f, 1.0f, (n - threshold) / 0.12f);
                if (hash (x, y, 5) < 0.18f * wear)
                    keep *= 0.35f;

                if (keep < 1.0f)
                    data.setPixelColour (x, y, pixel.withMultipliedAlpha (keep));
            }
        }
    }

    void timerCallback() override
    {
        bool changed = false;
        for (int i = 0; i < knobs.size(); ++i)
        {
            const double v = pedal.getKnobDisplayValue (i);
            if (! knobs[i]->isMouseButtonDown() && ! approximatelyEqual (knobs[i]->getValue(), v))
            {
                knobs[i]->setValue (v, dontSendNotification);
                changed = true;
            }
        }

        for (int i = 0; i < switches.size(); ++i)
            switches[i]->setToggleState (pedal.getSwitches()[i]->isOn(), dontSendNotification);
        for (auto* s : selectors)
            s->sync();

        if (changed)
            repaint();
    }

    PedalProcessor& pedal;
    PedalProcessor::Look look;
    PedalKnobLook knobLook;
    OwnedArray<Slider> knobs;
    OwnedArray<PedalSwitch> switches;
    OwnedArray<PedalSelector> selectors;
    juce::Rectangle<float> body, plate;
    float nameTop = 310.0f;
    Image enclosure;
};

AudioProcessorEditor* PedalProcessor::createEditor()
{
    return new PedalEditor (*this);
}
