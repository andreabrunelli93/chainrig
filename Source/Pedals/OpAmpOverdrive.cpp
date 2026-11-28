#include "OpAmpOverdrive.h"
#include "Taper.h"

using namespace juce;

namespace
{
    constexpr double k = 1e3, u = 1e-6, nF = 1e-9, pF = 1e-12;
    constexpr double potMin = 1.0;
    constexpr int updateInterval = 32;
    constexpr double railVolts = 3.2;   // JRC4558 a 9 V, Vref 4,5 V

    inline double rail (double x, double limit) noexcept
    {
        const double t = (x / limit) * (x / limit);
        const double t4 = t * t;
        return x / std::sqrt (std::sqrt (std::sqrt (1.0 + t4 * t4)));
    }

    // Curva del DRIVE misurata il 27/11/2026 contro 5 catture NAM di un esemplare originale del 1981
    // (drive 2-4-6-8-10, stessa catena, sweep a -80 dBFS: guadagni relativi -13,5 / -9,4 / -5,2 / -1,2 / 0 dB
    // a 1 kHz), invertendo la netlist: frazione 0,110 / 0,246 / 0,477 / 0,846 / 1. Più dolce di un logaritmico A.
    double driveTaper (double knob) noexcept
    {
        static constexpr double x[] = { 0.0, 2.0, 4.0, 6.0, 8.0, 10.0 };
        static constexpr double y[] = { 0.0, 0.1095, 0.2459, 0.4769, 0.8455, 1.0 };
        static constexpr double m[] = { 0.0548, 0.0615, 0.0919, 0.1499, 0.1308, 0.0772 };
        return Taper::measured (x, y, m, knob);
    }

    // livello d'uscita: 6 dB sopra la scala fisica (0,4 = 1/2,5 V), LEVEL predefinito 8 (come Dyna Comp).
    // Misurato: DRIVE 5, TONE 5, LEVEL 8, sweep a -18 dBFS -> vicino al bypass (vedi nota del circuito)
    constexpr double outputScale = 0.8;
}

OpAmpOverdrive::Info OpAmpOverdrive::getInfo()
{
    Info i;
    i.identifier = "ChainRig Op-Amp Overdrive";
    i.name = "Tube Screamer";   // nome scelto dall'utente (marchio Ibanez): identificatore neutro
    i.descriptiveName = "Op-amp overdrive with soft diode clipping in the feedback loop (built-in)";
    i.category = "Overdrive";
    i.menuCategory = "Distortion / Overdrive";
    i.uniqueId = 0x4f414f31;   // "OAO1"
    i.oversamplingOrder = 1;

    i.look.enclosureTop = Colour (0xff5f9a4a);
    i.look.enclosureBottom = Colour (0xff3e6e32);
    i.look.ink = Colour (0xfff1ead2);
    i.look.knob = Colour (0xff141414);
    i.look.pointer = Colour (0xfff1ead2);
    i.look.subtitle = "OVERDRIVE";
    i.look.wear = 0.2f;
    // DRIVE e LEVEL grandi agli angoli in alto, TONE piccola in mezzo più in basso, placca sotto il nome
    i.look.knobPlaces = { { 0.22f, 70.0f, 118.0f }, { 0.5f, 128.0f, 78.0f }, { 0.78f, 70.0f, 118.0f } };
    i.look.footPlate = true;
    return i;
}

OpAmpOverdrive::OpAmpOverdrive() : PedalProcessor (getInfo())
{
    drive = addKnob ("drive", "Drive", 5.0f);
    tone  = addKnob ("tone",  "Tone",  5.0f);
    level = addKnob ("level", "Level", 8.0f);

    // ingresso e stadio di clipping (op-amp ideali: l'ingresso (+) non carica l'inseguitore)
    {
        auto& c = clipper;
        const int in = c.addNode(), n2 = c.addNode(), b = c.addNode(), e = c.addNode(), p1 = c.addNode(),
                  n5 = c.addNode(), n6 = c.addNode(), n7 = c.addNode(), clip = c.addNode();
        c.addInput (in);
        c.addCapacitor (in, n2, 20 * nF);            // 0,02 µF d'ingresso
        c.addResistor (n2, b, 1 * k);                // 1k in serie
        c.addResistor (b, 0, 510 * k);               // polarizzazione verso 4,5 V
        c.addResistor (b, 0, 3000 * k);              // ingresso dell'inseguitore (beta ~300 x 10k)
        c.addOpAmp (e, b, e);                        // Q1
        c.addCapacitor (e, p1, 1 * u);               // 1 µF verso il clipping
        c.addResistor (p1, 0, 10 * k);               // 10k verso 4,5 V
        c.addOpAmp (n5, p1, clip);                   // op-amp 1, non invertente
        driveR = c.addResistor (clip, n6, 250 * k);  // DRIVE 500k log
        c.addResistor (n6, n5, 51 * k);
        c.addCapacitor (clip, n5, 51 * pF);
        c.setDiodePair (clip, n5, 1.5e-10, 1.752);    // diodi al silicio in retroazione: Is tarata sulla cattura calibrata (drive 0, -30 dBFS: THD 0,9%)
        c.addCapacitor (n5, n7, 47 * nF);            // passa-alto 720 Hz
        c.addResistor (n7, 0, 4.7 * k);
        c.addOutput (clip);
    }

    // passa-basso 723 Hz e tono attivo (op-amp 2 non invertente)
    {
        auto& c = toneStage;
        const int in = c.addNode(), n8 = c.addNode(), n10 = c.addNode(), n11 = c.addNode(), n12 = c.addNode(),
                  n9 = c.addNode();
        c.addInput (in);
        c.addResistor (in, n8, 1 * k);
        c.addCapacitor (n8, 0, 220 * nF);
        c.addResistor (n8, 0, 10 * k);
        toneA = c.addResistor (n8, n11, 10 * k);     // TONE, lato (+)
        toneB = c.addResistor (n11, n10, 10 * k);    // TONE, lato (-): cursore qui = più acuti
        c.addCapacitor (n11, n12, 220 * nF);
        c.addResistor (n12, 0, 220);
        c.addOpAmp (n10, n8, n9);
        c.addResistor (n9, n10, 1 * k);
        c.addOutput (n9);
    }

    // volume, inseguitore d'uscita, 100 ohm + 10 µF + 10k
    {
        auto& c = out;
        const int in = c.addNode(), n13 = c.addNode(), n14 = c.addNode(), lv = c.addNode(), n15 = c.addNode(),
                  e2 = c.addNode(), n16 = c.addNode(), o = c.addNode();
        c.addInput (in);
        c.addCapacitor (in, n13, 1 * u);
        c.addResistor (n13, n14, 1 * k);
        levelA = c.addResistor (n14, lv, 50 * k);    // LEVEL 100k log
        levelB = c.addResistor (lv, 0, 50 * k);
        c.addResistor (lv, 0, 510 * k);
        c.addCapacitor (lv, n15, 1 * u);
        c.addResistor (n15, 0, 510 * k);
        c.addOpAmp (e2, n15, e2);                    // Q2
        c.addResistor (e2, n16, 100);
        c.addCapacitor (n16, o, 10 * u);
        c.addResistor (o, 0, 10 * k);
        c.addOutput (o);
    }
}

void OpAmpOverdrive::applyKnobs (bool force)
{
    if (! force && ! driveSmoothed.isSmoothing() && ! toneSmoothed.isSmoothing() && ! levelSmoothed.isSmoothing())
        return;

    clipper.setResistance (driveR, 500 * k * driveTaper (driveSmoothed.getCurrentValue()) + potMin);

    const double t = toneSmoothed.getCurrentValue() / 10.0;   // lineare
    toneStage.setResistance (toneA, 20 * k * t + potMin);
    toneStage.setResistance (toneB, 20 * k * (1.0 - t) + potMin);

    const double v = Taper::audio (levelSmoothed.getCurrentValue());   // 100k logaritmico (geofex)
    out.setResistance (levelA, 100 * k * (1.0 - v) + potMin);
    out.setResistance (levelB, 100 * k * v + potMin);

    clipper.update();
    toneStage.update();
    out.update();
}

void OpAmpOverdrive::prepareCircuit (double rate)
{
    for (auto [s, p] : { std::pair { &driveSmoothed, drive }, std::pair { &toneSmoothed, tone },
                         std::pair { &levelSmoothed, level } })
    {
        s->reset (rate, 0.05);
        s->setCurrentAndTargetValue (p->get());
    }

    clipper.prepare (rate);
    toneStage.prepare (rate);
    out.prepare (rate);
    applyKnobs (true);
    untilUpdate = 0;
}

void OpAmpOverdrive::processCircuit (float* samples, int n)
{
    driveSmoothed.setTargetValue (drive->get());
    toneSmoothed.setTargetValue (tone->get());
    levelSmoothed.setTargetValue (level->get());

    double v, w;
    for (int i = 0; i < n; ++i)
    {
        if (--untilUpdate <= 0)
        {
            applyKnobs (false);
            untilUpdate = updateInterval;
        }
        driveSmoothed.getNextValue();
        toneSmoothed.getNextValue();
        levelSmoothed.getNextValue();

        const double x = (double) samples[i] * inputVolts;
        clipper.process (&x, &v);
        newtonIterations += clipper.getLastIterations();
        ++newtonSamples;

        w = rail (v, railVolts);
        toneStage.process (&w, &v);
        w = rail (v, railVolts);
        out.process (&w, &v);
        samples[i] = (float) (v * outputScale);
    }
}

String OpAmpOverdrive::getDiagnostics() const
{
    return "Newton: " + String ((double) newtonIterations / jmax ((int64) 1, newtonSamples), 2) + " iterazioni medie";
}
