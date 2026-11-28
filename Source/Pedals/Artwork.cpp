#include "Artwork.h"

using namespace juce;

namespace
{
    /** Arto o coda: spezzata ammorbidita (curve tra i punti medi), resa come tratto pieno. */
    Path limb (std::initializer_list<Point<float>> points, float width)
    {
        const std::vector<Point<float>> p (points);
        Path line;
        line.startNewSubPath (p.front());
        for (size_t i = 1; i + 1 < p.size(); ++i)
            line.quadraticTo (p[i], (p[i] + p[i + 1]) * 0.5f);
        line.lineTo (p.back());

        Path stroked;
        PathStrokeType (width, PathStrokeType::curved, PathStrokeType::rounded).createStrokedPath (stroked, line);
        return stroked;
    }

    Path ellipse (float x, float y, float w, float h)
    {
        Path e;
        e.addEllipse (x, y, w, h);
        return e;
    }
}

juce::Rectangle<float> Artwork::getBounds (const Drawing& d)
{
    juce::Rectangle<float> r;
    for (auto& p : d.fills)
        r = r.isEmpty() ? p.getBounds() : r.getUnion (p.getBounds());
    return r;
}

Artwork::Drawing Artwork::centurionCentaur()
{
    Drawing d;
    auto& parts = d.fills;

    // corpo del cavallo: groppa, ventre, petto
    parts.push_back (ellipse (44, 100, 50, 48));
    parts.push_back (ellipse (56, 104, 100, 46));
    parts.push_back (ellipse (124, 98, 44, 50));

    // zampe posteriori, anteriore poggiata, anteriore alzata
    parts.push_back (limb ({ { 62, 136 }, { 54, 160 }, { 64, 178 }, { 61, 192 } }, 9.0f));
    parts.push_back (limb ({ { 80, 140 }, { 74, 162 }, { 84, 178 }, { 82, 192 } }, 8.0f));
    parts.push_back (limb ({ { 140, 140 }, { 143, 166 }, { 140, 192 } }, 8.5f));
    parts.push_back (limb ({ { 154, 136 }, { 172, 150 }, { 166, 170 } }, 8.0f));
    for (auto [x, y] : { std::pair { 61.0f, 193.0f }, { 82.0f, 193.0f }, { 140.0f, 193.0f } })
        parts.push_back (ellipse (x - 6.5f, y - 4.0f, 13.0f, 7.0f));
    parts.push_back (ellipse (159.0f, 167.0f, 12.0f, 7.0f));

    // coda
    parts.push_back (limb ({ { 50, 112 }, { 30, 116 }, { 30, 138 }, { 33, 158 }, { 22, 172 } }, 7.0f));

    // busto umano, collo, testa e naso
    {
        Path torso;
        torso.startNewSubPath (134, 114);
        torso.lineTo (161, 114);
        torso.quadraticTo (167, 92, 160, 74);
        torso.quadraticTo (152, 63, 141, 66);
        torso.quadraticTo (131, 74, 134, 92);
        torso.closeSubPath();
        parts.push_back (torso);
    }
    {
        Path neck;
        neck.addRectangle (143.0f, 56.0f, 10.0f, 12.0f);
        parts.push_back (neck);
    }
    parts.push_back (ellipse (139, 40, 20, 20));
    {
        Path nose;
        nose.addTriangle (157, 47, 163, 53, 157, 55);
        parts.push_back (nose);
    }

    // elmo da centurione: calotta e cresta trasversale a ventaglio
    parts.push_back (ellipse (137, 36, 24, 15));
    {
        Path crest;
        crest.startNewSubPath (132, 42);
        crest.quadraticTo (149, 10, 167, 40);
        crest.quadraticTo (150, 26, 132, 42);
        crest.closeSubPath();
        parts.push_back (crest);
    }

    // braccio avanti con la lancia, scudo tondo sull'altro braccio
    parts.push_back (limb ({ { 155, 74 }, { 169, 86 }, { 179, 72 } }, 7.0f));
    {
        const Point<float> butt (128, 152), top (186, 30);
        parts.push_back (limb ({ butt, top }, 3.6f));

        const auto dir = (top - butt) / top.getDistanceFrom (butt);
        const Point<float> side (-dir.y, dir.x);
        Path head;
        head.addTriangle (top + dir * 16.0f, top + side * 5.0f, top - side * 5.0f);
        parts.push_back (head);
    }
    parts.push_back (ellipse (120, 70, 32, 32));

    // dettagli in negativo: bordo e borchia dello scudo, cintura, linea dell'elmo, zampe staccate dal corpo
    const auto cut = [&d] (Path p, float width)
    {
        Path stroked;
        PathStrokeType (width, PathStrokeType::curved, PathStrokeType::rounded).createStrokedPath (stroked, p);
        d.cuts.push_back (stroked);
    };
    {
        Path rim;
        rim.addEllipse (124.5f, 74.5f, 23.0f, 23.0f);
        cut (rim, 1.6f);
        Path boss;
        boss.addEllipse (133.0f, 83.0f, 6.0f, 6.0f);
        cut (boss, 1.4f);
    }
    {
        Path belt;
        belt.startNewSubPath (150.0f, 108.0f);
        belt.quadraticTo (156.0f, 109.5f, 163.0f, 107.0f);
        cut (belt, 1.6f);
    }
    {
        Path helmet;
        helmet.startNewSubPath (138.0f, 46.0f);
        helmet.quadraticTo (149.0f, 49.0f, 160.0f, 45.0f);
        cut (helmet, 1.4f);
        Path cheek;
        cheek.startNewSubPath (146.0f, 47.0f);
        cheek.lineTo (146.0f, 56.0f);
        cut (cheek, 1.3f);
    }
    {
        Path hind;
        hind.startNewSubPath (58.0f, 138.0f);
        hind.quadraticTo (68.0f, 148.0f, 82.0f, 146.0f);
        cut (hind, 1.4f);
        Path fore;
        fore.startNewSubPath (136.0f, 140.0f);
        fore.quadraticTo (146.0f, 146.0f, 158.0f, 140.0f);
        cut (fore, 1.4f);
    }
    return d;
}

Artwork::Drawing Artwork::tapeReels()
{
    Drawing d;
    constexpr float r = 44.0f, cy = 82.0f;
    for (const float cx : { 52.0f, 148.0f })
    {
        Path disc;
        disc.addEllipse (cx - r, cy - r, 2 * r, 2 * r);
        d.fills.push_back (disc);

        // tre finestre tra i raggi, il mozzo e il foro centrale
        for (int w = 0; w < 3; ++w)
        {
            const float a = juce::MathConstants<float>::twoPi * (float) w / 3.0f + 0.25f;
            Path window;
            window.addPieSegment (cx - r + 7, cy - r + 7, 2 * r - 14, 2 * r - 14, a, a + 1.55f, 0.42f);
            d.cuts.push_back (window);
        }
        Path hub;
        hub.addEllipse (cx - 6.0f, cy - 6.0f, 12.0f, 12.0f);
        d.cuts.push_back (hub);
    }

    // nastro: dal bordo esterno di ogni bobina giù ai rulli guida, poi dritto sopra la testina
    Path tape;
    tape.startNewSubPath (8.5f, 84.0f);
    tape.lineTo (18.0f, 160.0f);
    tape.lineTo (182.0f, 160.0f);
    tape.lineTo (191.5f, 84.0f);
    Path stroke;
    PathStrokeType (4.0f, PathStrokeType::mitered, PathStrokeType::butt).createStrokedPath (stroke, tape);
    d.fills.push_back (stroke);

    for (const float x : { 18.0f, 182.0f })   // rulli guida
    {
        Path roller;
        roller.addEllipse (x - 8.0f, 152.0f, 16.0f, 16.0f);
        d.fills.push_back (roller);
        Path pin;
        pin.addEllipse (x - 2.5f, 157.5f, 5.0f, 5.0f);
        d.cuts.push_back (pin);
    }

    // testine sotto il nastro: cancellazione, registrazione, lettura
    for (const float x : { 62.0f, 92.0f, 122.0f })
    {
        Path head;
        head.addRoundedRectangle (x, 164.0f, 16.0f, 20.0f, 3.0f);
        d.fills.push_back (head);
        Path headGap;
        headGap.addRectangle (x + 7.0f, 164.0f, 2.0f, 8.0f);
        d.cuts.push_back (headGap);
    }
    return d;
}
