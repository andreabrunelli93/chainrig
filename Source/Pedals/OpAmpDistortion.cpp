#include "OpAmpDistortion.h"
#include "Taper.h"

using namespace juce;

namespace
{
    constexpr double k = 1e3, nF = 1e-9, pF = 1e-12;
    constexpr double potMin = 1.0;
    constexpr int updateInterval = 32;

    // TL072 a 9,26 V con Vref 4,63 V: l'uscita arriva a circa 1,5 V dai rail
    constexpr double railVolts = 3.0;
    inline double rail (double x, double limit) noexcept   // ginocchio abbastanza duro
    {
        const double t = (x / limit) * (x / limit);
        const double t4 = t * t;
        return x / std::sqrt (std::sqrt (std::sqrt (1.0 + t4 * t4)));
    }

    // 1N914 (modello SPICE comune: Is 2,52 nA, n 1,752)
    constexpr double diodeIs = 2.52e-9, diodeN = 1.752;

    // uscita: scala fisica 0,4 (1 / 2,5 V); il pedale ha poco volume in uscita (scelta misurata sotto)
    constexpr double outputScale = 0.8;
}

OpAmpDistortion::Info OpAmpDistortion::getInfo()
{
    Info i;
    i.identifier = "ChainRig Dual Op-Amp Distortion";
    i.name = "ShredMaster";   // nome scelto dall'utente; identificatore neutro
    i.descriptiveName = "High-gain op-amp distortion with contour control (built-in)";
    i.category = "Distortion";
    i.menuCategory = "Distortion / Overdrive";
    i.uniqueId = 0x44535431;   // "DST1"
    i.oversamplingOrder = 2;   // scelto dalla misura dell'aliasing (vedi nota)

    i.look.enclosureTop = Colour (0xff3a3f47);
    i.look.enclosureBottom = Colour (0xff1c1f24);
    i.look.ink = Colour (0xffeceae4);
    i.look.knob = Colour (0xff101010);
    i.look.pointer = Colour (0xffe8442c);
    i.look.led = Colour (0xffff3b2f);
    i.look.brushed = true;
    i.look.subtitle = "DISTORTION";
    i.look.wear = 0.2f;
    i.look.width = 880;
    i.look.knobPlaces = { { 0.12f, 86, 96 }, { 0.31f, 86, 96 }, { 0.5f, 86, 96 }, { 0.69f, 86, 96 }, { 0.88f, 86, 96 } };
    return i;
}

OpAmpDistortion::OpAmpDistortion() : PedalProcessor (getInfo())
{
    gain    = addKnob ("gain",    "Gain",    5.0f);
    bass    = addKnob ("bass",    "Bass",    5.0f);
    contour = addKnob ("contour", "Contour", 5.0f);
    treble  = addKnob ("treble",  "Treble",  5.0f);
    volume  = addKnob ("volume",  "Volume",  5.0f);

    // A: ingresso e IC1A
    {
        auto& c = stageA;
        const int in = c.addNode(), a = c.addNode(), n = c.addNode(), m = c.addNode(), p = c.addNode(), o1 = c.addNode();
        c.addInput (in);
        c.addResistor (in, 0, 2200 * k);             // R1
        c.addCapacitor (in, a, 10 * nF);             // C1
        c.addResistor (a, 0, 1000 * k);              // R2 verso Vref
        c.addOpAmp (n, a, o1);                       // IC1A
        c.addCapacitor (n, o1, 100 * pF);            // C2
        c.addCapacitor (n, m, 47 * nF);              // C3
        c.addResistor (m, 0, 3.3 * k);               // R4
        c.addResistor (n, p, 3.3 * k);               // R3
        gainA = c.addResistor (p, o1, 50 * k);       // GAIN: cursore -> pin 1 (retroazione)
        c.addOutput (o1);
    }

    // B: resto del GAIN, C4, R5, IC1B
    {
        auto& c = stageB;
        const int in = c.addNode(), q = c.addNode(), r = c.addNode(), n2 = c.addNode(), o2 = c.addNode();
        c.addInput (in);
        gainB = c.addResistor (in, q, 50 * k);       // GAIN: cursore -> pin 3 (in serie)
        c.addCapacitor (q, r, 68 * nF);              // C4
        c.addResistor (r, n2, 10 * k);               // R5 (stock)
        c.addOpAmp (n2, 0, o2);                      // IC1B
        c.addResistor (n2, o2, 680 * k);             // R6
        c.addCapacitor (n2, o2, 47 * pF);            // C5
        c.addOutput (o2);
    }

    // C: diodi verso Vref, tone stack, IC2A
    {
        auto& c = stageC;
        const int in = c.addNode(), s = c.addNode(), x = c.addNode(), t3 = c.addNode(), tw = c.addNode(), w = c.addNode(),
                  y = c.addNode(), b1 = c.addNode(), n3 = c.addNode(), m3 = c.addNode(), o3 = c.addNode();
        c.addInput (in);
        c.addCapacitor (in, s, 100 * nF);            // C6
        c.addResistor (s, x, 10 * k);                // R7
        c.setDiodePair (x, diodeIs, diodeN);         // D1, D2 verso Vref
        c.addCapacitor (x, t3, 22 * nF);             // C7
        trebleTop = c.addResistor (t3, tw, 12.5 * k);    // TREBLE 25k lineare, lato C7
        trebleBottom = c.addResistor (tw, w, 12.5 * k);  // lato basso
        c.addResistor (x, y, 6.8 * k);               // R8
        c.addCapacitor (y, w, 220 * nF);             // C8
        c.addCapacitor (y, b1, 22 * nF);             // C9
        bassR = c.addResistor (w, b1, 50 * k);       // BASS 100k log a reostato: più resistenza = più bassi
        c.addResistor (b1, 0, 1 * k);                // R9
        c.addOpAmp (n3, tw, o3);                     // IC2A
        c.addResistor (n3, o3, 220 * k);             // R10
        c.addCapacitor (n3, o3, 2.2 * nF);           // C10
        c.addResistor (n3, m3, 47 * k);              // R11
        c.addCapacitor (m3, 0, 220 * nF);            // C11
        c.addOutput (o3);
    }

    // D: contour (T a ponte) e IC2B
    {
        auto& c = stageD;
        const int in = c.addNode(), p4 = c.addNode(), q4 = c.addNode(), s4 = c.addNode(), c1 = c.addNode(), c3 = c.addNode(),
                  uu = c.addNode(), n4 = c.addNode(), o4 = c.addNode();
        c.addInput (in);
        c.addResistor (in, p4, 100);                 // R12
        c.addResistor (p4, q4, 33 * k);              // R13
        c.addResistor (q4, s4, 33 * k);              // R14
        c.addCapacitor (p4, s4, 1 * nF);             // C14
        c.addCapacitor (p4, c1, 100 * nF);           // C12
        contourA = c.addResistor (c1, 0, 50 * k);    // CONTOUR pin 1 - cursore (in senso orario cresce: più scavato)
        c.addCapacitor (q4, c3, 47 * nF);            // C13
        contourB = c.addResistor (c3, 0, 50 * k);    // pin 3 - cursore
        c.addCapacitor (s4, uu, 220 * nF);           // C15
        c.addResistor (uu, n4, 100 * k);             // R15
        c.addOpAmp (n4, 0, o4);                      // IC2B
        c.addResistor (n4, o4, 100 * k);             // R16
        c.addCapacitor (n4, o4, 1 * nF);             // C16 (stock)
        c.addOutput (o4);
    }

    // E: VOLUME, C17, R17
    {
        auto& c = stageE;
        const int in = c.addNode(), lv = c.addNode(), o = c.addNode();
        c.addInput (in);
        volumeA = c.addResistor (in, lv, 50 * k);
        volumeB = c.addResistor (lv, 0, 50 * k);
        c.addCapacitor (lv, o, 220 * nF);            // C17
        c.addResistor (o, 0, 1000 * k);              // R17
        c.addOutput (o);
    }
}

void OpAmpDistortion::applyKnobs (bool force)
{
    if (! force && ! gainSmoothed.isSmoothing() && ! bassSmoothed.isSmoothing() && ! contourSmoothed.isSmoothing()
        && ! trebleSmoothed.isSmoothing() && ! volumeSmoothed.isSmoothing())
        return;

    // GAIN: le due parti dello stesso potenziometro, sempre aggiornate insieme (somma 100k)
    const double g = gainSmoothed.getCurrentValue() / 10.0;
    stageA.setResistance (gainA, 100 * k * g + potMin);
    stageB.setResistance (gainB, 100 * k * (1.0 - g) + potMin);

    const double t = trebleSmoothed.getCurrentValue() / 10.0;
    stageC.setResistance (trebleTop, 25 * k * (1.0 - t) + potMin);
    stageC.setResistance (trebleBottom, 25 * k * t + potMin);
    stageC.setResistance (bassR, 100 * k * Taper::audio (bassSmoothed.getCurrentValue()) + potMin);

    const double ct = contourSmoothed.getCurrentValue() / 10.0;
    stageD.setResistance (contourA, 100 * k * ct + potMin);
    stageD.setResistance (contourB, 100 * k * (1.0 - ct) + potMin);

    const double v = Taper::audio (volumeSmoothed.getCurrentValue());
    stageE.setResistance (volumeA, 100 * k * (1.0 - v) + potMin);
    stageE.setResistance (volumeB, 100 * k * v + potMin);

    stageA.update();
    stageB.update();
    stageC.update();
    stageD.update();
    stageE.update();
}

void OpAmpDistortion::prepareCircuit (double rate)
{
    for (auto [s, p] : { std::pair { &gainSmoothed, gain }, std::pair { &bassSmoothed, bass }, std::pair { &contourSmoothed, contour },
                         std::pair { &trebleSmoothed, treble }, std::pair { &volumeSmoothed, volume } })
    {
        s->reset (rate, 0.05);
        s->setCurrentAndTargetValue (p->get());
    }

    stageA.prepare (rate);
    stageB.prepare (rate);
    stageC.prepare (rate);
    stageD.prepare (rate);
    stageE.prepare (rate);
    applyKnobs (true);
    untilUpdate = 0;
}

void OpAmpDistortion::processCircuit (float* samples, int n)
{
    gainSmoothed.setTargetValue (gain->get());
    bassSmoothed.setTargetValue (bass->get());
    contourSmoothed.setTargetValue (contour->get());
    trebleSmoothed.setTargetValue (treble->get());
    volumeSmoothed.setTargetValue (volume->get());

    double v, w;
    for (int i = 0; i < n; ++i)
    {
        if (--untilUpdate <= 0)
        {
            applyKnobs (false);
            untilUpdate = updateInterval;
        }
        for (auto* s : { &gainSmoothed, &bassSmoothed, &contourSmoothed, &trebleSmoothed, &volumeSmoothed })
            s->getNextValue();

        const double x = (double) samples[i] * inputVolts;
        stageA.process (&x, &v);
        w = rail (v, railVolts);
        stageB.process (&w, &v);
        w = rail (v, railVolts);
        stageC.process (&w, &v);
        newtonIterations += stageC.getLastIterations();
        ++newtonSamples;
        w = rail (v, railVolts);
        stageD.process (&w, &v);
        w = rail (v, railVolts);
        stageE.process (&w, &v);
        samples[i] = (float) (v * outputScale);
    }
}

String OpAmpDistortion::getDiagnostics() const
{
    return "Newton (diodi) " + String (newtonIterations / jmax ((int64) 1, newtonSamples), 2) + " iterazioni medie";
}
