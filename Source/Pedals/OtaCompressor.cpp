#include "OtaCompressor.h"
#include "Taper.h"

using namespace juce;

namespace
{
    constexpr double k = 1e3, u = 1e-6, nF = 1e-9;
    constexpr double potMin = 1.0;
    constexpr int updateInterval = 32;
    constexpr double thermal = 0.02585;

    constexpr double supply = 9.0;
    constexpr double iabcResistor = 27 * k;        // R12 verso il pin 5
    constexpr double iabcDrop = 1.3;               // Vbe dell'inseguitore + pin 5 (un diodo sopra V-)
    constexpr double sustainPot = 500 * k;          // lineare
    constexpr double transistorBeta = 500.0;        // NPN ad alto guadagno

    // uscita dell'OTA: Vb = 2,93 V su un'alimentazione 0-9 V, quindi c'è più spazio verso l'alto
    constexpr double railDown = 2.3, railUp = 5.0;

    inline double rail (double x, double limit) noexcept   // lineare fino a poco prima del limite
    {
        const double t = (x / limit) * (x / limit);
        const double t4 = t * t;
        return x / std::sqrt (std::sqrt (std::sqrt (1.0 + t4 * t4)));
    }

    // livello d'uscita: come nel pedale vero l'uscita è bassa (serve LEVEL alto per arrivare al bypass);
    // qui 6 dB in più della scala fisica (0,4 = 1/2,5 V), e LEVEL predefinito 8.
    // Misurato: SUSTAIN 5, LEVEL 8, sweep a -18 dBFS -> vicino al bypass (vedi nota del circuito)
    constexpr double outputScale = 0.8;

    // BLEND: il pulito entra nella rete d'uscita (C15, R20, LEVEL) insieme all'uscita dell'OTA, così
    // LEVEL resta il volume generale. Ampiezza scelta perché con LEVEL predefinito (8) il pulito da
    // solo esca al livello del bypass: uscita = sorgente x f x 50k / (10k + 50k) a centro banda.
    // Polarità: l'OTA lavora sull'ingresso invertente (vedi la rete d'ingresso), quindi il pulito
    // va invertito per sommarsi in fase (misurato: a BLEND 5 nessuna cancellazione).
    constexpr double defaultLevel = 8.0;
    constexpr double dryPolarity = -1.0;
    inline double dryGain() noexcept
    {
        const double f = Taper::audio (defaultLevel);
        return (10 * k + 50 * k) / (f * 50 * k) / (OtaCompressor::inputVolts * outputScale);
    }
}

OtaCompressor::Info OtaCompressor::getInfo()
{
    Info i;
    i.identifier = "ChainRig OTA Compressor";
    i.name = "Dyna Comp";   // nome scelto dall'utente (marchio MXR/Dunlop): identificatore neutro
    i.descriptiveName = "OTA compressor with transistor envelope detector (built-in)";
    i.category = "Compressor";
    i.menuCategory = "Dynamics";
    i.uniqueId = 0x4f544331;   // "OTC1"
    i.oversamplingOrder = 1;

    i.look.enclosureTop = Colour (0xffc8322b);
    i.look.enclosureBottom = Colour (0xff8e1d19);
    i.look.ink = Colour (0xfff4ead8);
    i.look.knob = Colour (0xff151515);
    i.look.pointer = Colour (0xfff4ead8);
    i.look.subtitle = "OTA COMPRESSOR";
    i.look.wear = 0.3f;
    return i;
}

OtaCompressor::OtaCompressor() : PedalProcessor (getInfo())
{
    sustain = addKnob ("sustain", "Sustain", 5.0f);
    level   = addKnob ("level",   "Level",   (float) defaultLevel);
    blend   = addKnob ("blend",   "Blend",   10.0f);   // 10 = solo compresso, come il pedale originale (e i preset vecchi)

    // ingresso: C1, R2, inseguitore Q1, poi la rete dei due ingressi dell'OTA
    {
        auto& c = input;
        const int in = c.addNode(), n1 = c.addNode(), b = c.addNode(), e = c.addNode(),
                  inv = c.addNode(), ninv = c.addNode(), n7 = c.addNode();
        c.addInput (in);
        c.addCapacitor (in, n1, 10 * nF);            // C1
        c.addResistor (n1, b, 10 * k);               // R2
        c.addResistor (b, 0, 1000 * k);              // R3, polarizzazione verso Vb
        c.addResistor (b, 0, (transistorBeta + 1.0) * 10 * k);   // ingresso dell'inseguitore Q1 (beta x R5)
        c.addOpAmp (e, b, e);                        // Q1 come inseguitore ideale
        c.addCapacitor (e, inv, 1 * u);              // C6
        c.addResistor (inv, 0, 1000 * k);            // R8
        c.addResistor (inv, ninv, 2 * k);            // Tr1 (bilanciamento tra i due ingressi)
        c.addResistor (ninv, 0, 1000 * k);           // R7
        c.addResistor (ninv, n7, 15 * k);            // R11
        c.addCapacitor (ninv, n7, 10 * nF);          // C10
        c.addCapacitor (n7, 0, 1 * u);               // C8
        c.addOutput (inv);
        c.addOutput (ninv);
    }

    // carico dell'OTA: la corrente I su 150k || 1n diventa la sorgente di Thevenin I x 150k con 150k in serie
    {
        auto& c = load;
        const int src = c.addNode(), o = c.addNode();
        c.addInput (src);
        c.addResistor (src, o, 150 * k);             // R14
        c.addCapacitor (o, 0, 1 * nF);               // C11
        c.addOutput (o);
    }

    // uscita dall'emettitore del separatore di fase: C15, R20, LEVEL
    {
        auto& c = out;
        const int src = c.addNode(), n9 = c.addNode(), n11 = c.addNode(), o = c.addNode();
        c.addInput (src);
        c.addCapacitor (src, n9, 47 * nF);           // C15
        c.addResistor (n9, n11, 10 * k);             // R20
        levelA = c.addResistor (n11, o, 25 * k);     // LEVEL (50k log)
        levelB = c.addResistor (o, 0, 25 * k);
        c.addOutput (o);
    }
}

void OtaCompressor::applyKnobs (bool force)
{
    if (! force && ! sustainSmoothed.isSmoothing() && ! levelSmoothed.isSmoothing())
        return;

    // SUSTAIN a 10 = resistenza zero = più corrente all'OTA = più guadagno e più compressione
    sustainOhms = sustainPot * (1.0 - sustainSmoothed.getCurrentValue() / 10.0) + potMin;

    const double f = Taper::audio (levelSmoothed.getCurrentValue());
    out.setResistance (levelA, 50 * k * (1.0 - f) + potMin);
    out.setResistance (levelB, 50 * k * f + potMin);
    out.update();
}

void OtaCompressor::prepareCircuit (double rate)
{
    for (auto [s, p] : { std::pair { &sustainSmoothed, sustain }, std::pair { &levelSmoothed, level },
                         std::pair { &blendSmoothed, blend } })
    {
        s->reset (rate, 0.05);
        s->setCurrentAndTargetValue (p->get());
    }

    input.prepare (rate);
    load.prepare (rate);
    out.prepare (rate);

    DetectorBranch::Parts collector;        // dal collettore: 10k di R19
    collector.sourceOhms = 10 * k;
    DetectorBranch::Parts emitter;          // dall'emettitore: impedenza bassa
    emitter.sourceOhms = 150.0;
    fromCollector.prepare (collector, rate);
    fromEmitter.prepare (emitter, rate);

    envelope.prepare (supply, 150 * k, 10 * u, transistorBeta, rate);   // R18, C16, Q4
    applyKnobs (true);

    // a riposo: il condensatore carico al +9 meno la caduta dovuta alla corrente di base dell'inseguitore
    iabc = jmax (0.0, (supply - iabcDrop) / (sustainOhms + iabcResistor));
    for (int i = 0; i < 64; ++i)
        envelope.process (0.0, iabc);
    untilUpdate = 0;
}

void OtaCompressor::processCircuit (float* samples, int n)
{
    sustainSmoothed.setTargetValue (sustain->get());
    levelSmoothed.setTargetValue (level->get());
    blendSmoothed.setTargetValue (blend->get());
    const double dry = dryPolarity * dryGain();

    double pins[2], v;

    for (int i = 0; i < n; ++i)
    {
        if (--untilUpdate <= 0)
        {
            applyKnobs (false);
            untilUpdate = updateInterval;
        }
        sustainSmoothed.getNextValue();
        levelSmoothed.getNextValue();

        // ingresso -> tensioni sui due pin dell'OTA
        const double x = (double) samples[i] * inputVolts;
        input.process (&x, pins);

        // OTA: corrente d'uscita, limitata da Iabc (Iabc dal campione prima: l'inviluppo è lento)
        const double current = iabc * std::tanh ((pins[1] - pins[0]) / (2.0 * thermal));
        const double thevenin = current * 150 * k;
        load.process (&thevenin, &v);
        const double ota = v >= 0.0 ? rail (v, railUp) : rail (v, railDown);

        // separatore di fase: emettitore = +ota, collettore = -ota; rivelatori sui due
        const double ic = fromCollector.process (-ota) + fromEmitter.process (ota);
        const double venv = envelope.process (ic, iabc);
        iabc = jmax (0.0, (venv - iabcDrop) / (sustainOhms + iabcResistor));

        // BLEND lineare tra pulito (ingresso in volt) e compresso, prima di LEVEL
        const double b = blendSmoothed.getNextValue() / 10.0;
        const double mixed = b * ota + (1.0 - b) * dry * x;
        out.process (&mixed, &v);
        samples[i] = (float) (v * outputScale);

        iabcMin = jmin (iabcMin, iabc);
        iabcSum += iabc;
        ++samplesSeen;
    }
}

String OtaCompressor::getDiagnostics() const
{
    const auto iterations = (double) (fromCollector.iterations + fromEmitter.iterations);
    return "Iabc medio " + String (iabcSum / jmax ((int64) 1, samplesSeen) * 1e6, 1) + " uA, minimo "
         + String (iabcMin * 1e6, 1) + " uA; Newton rivelatori "
         + String (iterations / jmax (1.0, 2.0 * (double) samplesSeen), 2) + " iterazioni medie";
}
