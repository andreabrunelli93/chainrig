#include "TransparentDrive.h"
#include "Artwork.h"

using namespace juce;

namespace
{
    constexpr double k = 1e3, u = 1e-6, nF = 1e-9, pF = 1e-12;
    constexpr double potMin = 1.0;            // niente resistenze a zero agli estremi dei potenziometri
    constexpr int updateInterval = 32;        // campioni sovracampionati tra due ricalcoli mentre gira una manopola

    // Uscita dell'op-amp vicino ai rail: lineare fino a poco prima del limite, poi piatta.
    // x / (1 + (x/L)^8)^(1/8): tre radici quadrate, niente pow e niente ritardo (l'ADAA ritarderebbe
    // di mezzo campione solo il ramo che distorce, sfasando gli acuti rispetto ai rami puliti; e
    // l'aliasing che resta viene dai diodi, non da qui: misurato).
    inline double rail (double x, double limit) noexcept
    {
        const double t = (x / limit) * (x / limit);
        const double t4 = t * t;
        return x / std::sqrt (std::sqrt (std::sqrt (1.0 + t4 * t4)));
    }

    // Curva della manopola GAIN (posizione 0-10 -> frazione del doppio potenziometro). Misurata il
    // 27/11/2026 confrontando il modello con 12 catture NAM calibrate di un clone: a basso livello
    // ogni cattura si riproduce (entro 0,1-0,7 dB) con il gain del modello a 0 / 0,5 / ~3,7 / 9,25 /
    // 9,75 per le manopole 0 / 2 / 5 / 8 / 10. Curva a S: poco gain nella prima metà, poi sale.
    // Hermite monotona (Fritsch-Carlson) tra i punti misurati.
    double gainTaper (double knob) noexcept
    {
        static constexpr double x[] = { 0.0, 2.0, 5.0, 8.0, 10.0 };
        static constexpr double y[] = { 0.0, 0.05, 0.37, 0.925, 0.975 };
        static constexpr double m[] = { 0.025, 0.0658, 0.1458, 0.073, 0.0174 };   // pendenze, già limitate

        knob = jlimit (0.0, 10.0, knob);
        int i = 0;
        while (i < 3 && knob > x[i + 1])
            ++i;

        const double h = x[i + 1] - x[i], t = (knob - x[i]) / h, t2 = t * t, t3 = t2 * t;
        return (2 * t3 - 3 * t2 + 1) * y[i] + (t3 - 2 * t2 + t) * h * m[i]
             + (-2 * t3 + 3 * t2) * y[i + 1] + (t3 - t2) * h * m[i + 1];
    }

    constexpr double railLow = 3.0;    // TL072 a 9 V con Vb = 4,5 V
    constexpr double railHigh = 12.0;  // TL072 a +18 / -9 V

    // Livello d'uscita: con le manopole predefinite il volume resta vicino al bypass (misurato)
    // (misurato: sweep a -18 dBFS, GAIN 3, TREBLE 5, OUTPUT 5 -> +0,6 dB rispetto al bypass)
    constexpr double outputScale = 0.16;
}

TransparentDrive::Info TransparentDrive::getInfo()
{
    Info i;
    i.identifier = "ChainRig Transparent Drive";
    i.name = "Centurion Klone";
    i.descriptiveName = "Transparent overdrive: clean blend with germanium clipping (built-in)";
    i.category = "Overdrive";
    i.menuCategory = "Distortion / Overdrive";
    i.uniqueId = 0x54445231;   // "TDR1"
    i.oversamplingOrder = 1;

    // corpo color oro spazzolato, scritte brune incise, manopole nere
    i.look.enclosureTop = Colour (0xffe3c77f);
    i.look.enclosureBottom = Colour (0xffb38d43);
    i.look.ink = Colour (0xff3b2a12);
    i.look.knob = Colour (0xff17140f);
    i.look.pointer = Colour (0xfff3ead3);
    i.look.brushed = true;
    i.look.subtitle = "TRANSPARENT OVERDRIVE";
    i.look.emblem = Artwork::centurionCentaur();
    i.look.wear = 0.7f;   // usato, vernice scrostata e inchiostro consumato
    return i;
}

TransparentDrive::TransparentDrive() : PedalProcessor (getInfo())
{
    gain   = addKnob ("gain",   "Gain",   3.0f);
    treble = addKnob ("treble", "Treble", 5.0f);
    output = addKnob ("output", "Output", 5.0f);

    // A: ingresso -> buffer U1A -> FF-1 (uscita: tensione su R19 verso il sommatore) e stadio di guadagno U1B
    {
        auto& c = input;
        const int in = c.addNode(), n3 = c.addNode(), n4 = c.addNode(), n7 = c.addNode(), n8 = c.addNode(),
                  n9 = c.addNode(), n10 = c.addNode(), n11 = c.addNode(), n12 = c.addNode(), n13 = c.addNode(),
                  n14 = c.addNode();
        c.addInput (in);
        c.addResistor (n3, in, 10 * k);       // R1
        c.addCapacitor (n4, n3, 100 * nF);    // C1
        c.addResistor (n4, 0, 1000 * k);      // R2
        c.addOpAmp (n7, n4, n7);              // U1A, buffer
        c.addCapacitor (n8, n7, 100 * nF);    // C3
        c.addResistor (n9, n8, 10 * k);       // R6
        c.addCapacitor (n9, n8, 68 * nF);     // C5
        gainA1 = c.addResistor (n9, 0, 50 * k);   // GAIN, sezione 1: ingresso di U1B verso Vb
        c.addResistor (n8, n10, 1.5 * k);     // R7
        c.addCapacitor (n10, 0, 1 * u);       // C16
        c.addResistor (n10, 0, 15 * k);       // R19, verso la massa virtuale del sommatore
        c.addOpAmp (n13, n9, n14);            // U1B
        c.addResistor (n14, n13, 422 * k);    // R12
        c.addCapacitor (n14, n13, 390 * pF);  // C8
        c.addResistor (n13, n12, 15 * k);     // R11
        c.addCapacitor (n13, n12, 82 * nF);   // C7
        c.addResistor (n12, n11, 2 * k);      // R10
        gainB1 = c.addResistor (n11, 0, 50 * k);  // GAIN, sezione 1: ramo del guadagno
        c.addOutput (n7);
        c.addOutput (n14);
        c.addOutput (n10);
    }

    // B: ramo pulito (FF-2), clipping e sommatore U2A
    {
        auto& c = clipper;
        const int m7 = c.addNode(), m14 = c.addNode(), m10 = c.addNode(), m25 = c.addNode(), m26 = c.addNode(),
                  m27 = c.addNode(), m15 = c.addNode(), m16 = c.addNode(), m17 = c.addNode(), m28 = c.addNode(),
                  m29 = c.addNode(), m18 = c.addNode(), m19 = c.addNode();
        c.addInput (m7);
        c.addInput (m14);
        c.addInput (m10);
        c.addResistor (m7, m25, 5.1 * k);     // R5
        c.addCapacitor (m7, m25, 68 * nF);    // C4
        c.addResistor (m25, 0, 1.5 * k);      // R8
        c.addCapacitor (m25, m26, 390 * nF);  // C6
        c.addResistor (m26, 0, 1 * k);        // R9
        gainA2 = c.addResistor (m25, m27, 50 * k);  // GAIN, sezione 2: con il gain al massimo il nodo 27 va a Vb
        gainB2 = c.addResistor (m27, 0, 50 * k);
        c.addCapacitor (m14, m15, 1 * u);     // C9
        c.addResistor (m15, m16, 1 * k);      // R13
        c.setDiodePair (m16, 2e-7, 1.3);      // D2, D3: 1N34A
        c.addCapacitor (m16, m17, 1 * u);     // C10
        c.addResistor (m17, m18, 47 * k);     // R16
        c.addCapacitor (m17, m28, 2.2 * nF);  // C11
        c.addResistor (m28, m27, 22 * k);     // R15
        c.addResistor (m18, m27, 27 * k);     // R17
        c.addResistor (m27, m29, 12 * k);     // R18
        c.addCapacitor (m29, m18, 27 * nF);   // C12
        c.addResistor (m10, m18, 15 * k);     // R19 (FF-1)
        c.addOpAmp (m18, 0, m19);             // U2A
        c.addResistor (m19, m18, 392 * k);    // R20
        c.addCapacitor (m19, m18, 820 * pF);  // C13
        c.addOutput (m19);
    }

    // C: treble attivo U2B
    {
        auto& c = tone;
        const int in = c.addNode(), k32 = c.addNode(), k30 = c.addNode(), k31 = c.addNode(), k20 = c.addNode(),
                  k21 = c.addNode();
        c.addInput (in);
        c.addResistor (in, k32, 1.8 * k);     // R21
        trebleA = c.addResistor (k32, k30, 5 * k);  // TREBLE: cursore verso R21 = più acuti
        trebleB = c.addResistor (k30, k31, 5 * k);
        c.addResistor (k20, in, 100 * k);     // R22
        c.addCapacitor (k20, k30, 3.9 * nF);  // C14
        c.addResistor (k21, k20, 100 * k);    // R24
        c.addResistor (k21, k31, 4.7 * k);    // R23
        c.addOpAmp (k20, 0, k21);             // U2B
        c.addOutput (k21);
    }

    // D: condensatore d'uscita e volume
    {
        auto& c = out;
        const int in = c.addNode(), j22 = c.addNode(), j23 = c.addNode(), j24 = c.addNode();
        c.addInput (in);
        c.addCapacitor (in, j22, 4.7 * u);    // C15
        c.addResistor (j22, j23, 560);        // R25
        volumeA = c.addResistor (j23, j24, 5 * k);  // OUTPUT
        volumeB = c.addResistor (j24, 0, 5 * k);
        c.addResistor (j24, 0, 100 * k);      // R28, carico
        c.addOutput (j24);
    }
}

void TransparentDrive::applyKnobs (bool force)
{
    const bool moving = gainSmoothed.isSmoothing() || trebleSmoothed.isSmoothing() || outputSmoothed.isSmoothing();
    if (! force && ! moving)
        return;

    // posizione 0-10; treble e volume lineari, gain con la curva misurata
    const double g = gainTaper (gainSmoothed.getCurrentValue());
    input.setResistance (gainA1, 100 * k * g + potMin);
    input.setResistance (gainB1, 100 * k * (1.0 - g) + potMin);
    clipper.setResistance (gainA2, 100 * k * g + potMin);
    clipper.setResistance (gainB2, 100 * k * (1.0 - g) + potMin);

    const double t = trebleSmoothed.getCurrentValue() / 10.0;
    tone.setResistance (trebleA, 10 * k * (1.0 - t) + potMin);
    tone.setResistance (trebleB, 10 * k * t + potMin);

    const double v = outputSmoothed.getCurrentValue() / 10.0;
    out.setResistance (volumeA, 10 * k * (1.0 - v) + potMin);
    out.setResistance (volumeB, 10 * k * v + potMin);

    input.update();
    clipper.update();
    tone.update();
    out.update();
}

void TransparentDrive::prepareCircuit (double rate)
{
    for (auto [s, p] : { std::pair { &gainSmoothed, gain }, std::pair { &trebleSmoothed, treble },
                         std::pair { &outputSmoothed, output } })
    {
        s->reset (rate, 0.05);
        s->setCurrentAndTargetValue (p->get());
    }

    input.prepare (rate);
    clipper.prepare (rate);
    tone.prepare (rate);
    out.prepare (rate);

    applyKnobs (true);
    untilUpdate = 0;
}

void TransparentDrive::processCircuit (float* samples, int n)
{
    gainSmoothed.setTargetValue (gain->get());
    trebleSmoothed.setTargetValue (treble->get());
    outputSmoothed.setTargetValue (output->get());

    double a[3], b[3], y;

    for (int i = 0; i < n; ++i)
    {
        if (--untilUpdate <= 0)
        {
            applyKnobs (false);
            untilUpdate = updateInterval;
        }

        gainSmoothed.getNextValue();
        trebleSmoothed.getNextValue();
        outputSmoothed.getNextValue();

        const double x = (double) samples[i] * inputVolts;
        input.process (&x, a);                     // a = { U1A, U1B, FF-1 }

        b[0] = a[0];
        b[1] = rail (a[1], railLow);
        b[2] = a[2];
        clipper.process (b, &y);
        newtonIterations += clipper.getLastIterations();
        newtonMax = jmax (newtonMax, clipper.getLastIterations());
        ++newtonSamples;

        double z = rail (y, railHigh);
        tone.process (&z, &y);
        z = rail (y, railHigh);
        out.process (&z, &y);

        samples[i] = (float) (y * outputScale);
    }
}
