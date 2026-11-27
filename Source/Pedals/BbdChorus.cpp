#include "BbdChorus.h"

using namespace juce;

namespace
{
    constexpr double k = 1e3, u = 1e-6, nF = 1e-9, pF = 1e-12;
    constexpr int stages = 1024;   // MN3007

    // Clock (CD4047 con 39k e 150 pF): centro dalla formula del datasheet 1 / (4,4 R C) = 38,9 kHz,
    // cioè 13,2 ms di ritardo. La tensione di controllo (triangolo dell'LFO attraverso DEPTH) sposta la
    // frequenza in proporzione: con DEPTH al massimo ±21% (ampiezza del triangolo ~0,96 V su 4,5 V,
    // dal comparatore R27 120k / R28 470k). Legge approssimata: da verificare a orecchio.
    constexpr double centerClock = 1.0 / (4.4 * 39e3 * 150e-12);
    constexpr double maxClockSwing = 0.213;

    // LFO: integratore (C13 2,2 µF) + comparatore (R27 120k, R28 470k): f = (R28/R27) / (4 R C13),
    // R = RATE (1M) + R26 47k: da 0,42 a 9,5 Hz. RATE con curva esponenziale tra i due estremi.
    constexpr double lfoMin = (470.0 / 120.0) / (4.0 * 1047e3 * 2.2e-6);
    constexpr double lfoMax = (470.0 / 120.0) / (4.0 * 47e3 * 2.2e-6);

    constexpr double outputScale = 0.4;   // scala fisica (1/2,5 V): il pedale è a guadagno ~1
}

BbdChorus::Info BbdChorus::getInfo()
{
    Info i;
    i.identifier = "ChainRig BBD Chorus";
    i.name = "Small Clone";   // nome scelto dall'utente (marchio Electro-Harmonix): identificatore neutro
    i.descriptiveName = "Bucket-brigade chorus with triangle LFO (built-in)";
    i.category = "Modulation";
    i.menuCategory = "Modulation";
    i.uniqueId = 0x42424331;   // "BBC1"
    i.oversamplingOrder = 1;   // 2x: a 48 kHz i filtri con Q alto (picco a 5 kHz) si deformerebbero

    i.look.enclosureTop = Colour (0xff3d6fb0);
    i.look.enclosureBottom = Colour (0xff254a7d);
    i.look.ink = Colour (0xfff2efe6);
    i.look.knob = Colour (0xff151515);
    i.look.pointer = Colour (0xfff2efe6);
    i.look.subtitle = "BBD CHORUS";
    i.look.wear = 0.2f;
    return i;
}

BbdChorus::BbdChorus() : PedalProcessor (getInfo())
{
    rate  = addKnob ("rate",  "Rate",  3.0f);
    depth = addKnob ("depth", "Depth", 7.0f);

    // ingresso ed enfasi: C1 33n, R2 220k verso VB, R3 1k, IC1A non invertente, R4 6k8 + C2 10n, R5 33k
    {
        auto& c = emphasis;
        const int in = c.addNode(), n1 = c.addNode(), p = c.addNode(), m = c.addNode(), n4 = c.addNode(), o = c.addNode();
        c.addInput (in);
        c.addResistor (in, 0, 1000 * k);       // R1
        c.addCapacitor (in, n1, 33 * nF);      // C1
        c.addResistor (n1, 0, 220 * k);        // R2
        c.addResistor (n1, p, 1 * k);          // R3
        c.addOpAmp (m, p, o);                  // IC1A
        c.addResistor (m, n4, 6.8 * k);        // R4
        c.addCapacitor (n4, 0, 10 * nF);       // C2
        c.addResistor (o, m, 33 * k);          // R5
        c.addOutput (o);
    }

    // anti-aliasing: C5 1µ, trimmer di polarizzazione (100k a metà: 25k verso massa AC), R12 3k3, C6 3n3,
    // Sallen-Key R13 10k, R14 10k, C7 15n verso l'emettitore di Q1, C8 470p, Q1 inseguitore
    {
        auto& c = antiAlias;
        const int in = c.addNode(), a = c.addNode(), b = c.addNode(), j = c.addNode(), kk = c.addNode(), q = c.addNode();
        c.addInput (in);
        c.addCapacitor (in, a, 1 * u);         // C5
        c.addResistor (a, 0, 25 * k);          // T1 (100k a metà)
        c.addResistor (a, b, 3.3 * k);         // R12
        c.addCapacitor (b, 0, 3.3 * nF);       // C6
        c.addResistor (b, j, 10 * k);          // R13
        c.addCapacitor (j, q, 15 * nF);        // C7
        c.addResistor (j, kk, 10 * k);         // R14
        c.addCapacitor (kk, 0, 470 * pF);      // C8
        c.addOpAmp (q, kk, q);                 // Q1
        c.addOutput (q);
    }

    // ricostruzione: R18 10k, C9 4n7, Sallen-Key R19 39k, R20 39k, C10 2n7 verso l'emettitore di Q2, C11 180p
    {
        auto& c = recon;
        const int in = c.addNode(), s = c.addNode(), t = c.addNode(), v = c.addNode(), q = c.addNode();
        c.addInput (in);
        c.addResistor (in, s, 10 * k);         // R18
        c.addCapacitor (s, 0, 4.7 * nF);       // C9
        c.addResistor (s, t, 39 * k);          // R19
        c.addCapacitor (t, q, 2.7 * nF);       // C10
        c.addResistor (t, v, 39 * k);          // R20
        c.addCapacitor (v, 0, 180 * pF);       // C11
        c.addOpAmp (q, v, q);                  // Q2
        c.addOutput (q);
    }

    // miscela: asciutto R6 22k, bagnato R23 20k, R7 10k verso IC1B invertente; R8 33k || (R9 6k8 + C3 10n);
    // uscita con 1 µF su 100k
    {
        auto& c = mixer;
        const int dry = c.addNode(), wet = c.addNode(), x = c.addNode(), m = c.addNode(), r9 = c.addNode(),
                  o = c.addNode(), out = c.addNode();
        c.addInput (dry);
        c.addInput (wet);
        c.addResistor (dry, x, 22 * k);        // R6
        c.addResistor (wet, x, 20 * k);        // R23 (miscela)
        c.addResistor (x, m, 10 * k);          // R7
        c.addOpAmp (m, 0, o);                  // IC1B
        c.addResistor (o, m, 33 * k);          // R8
        c.addResistor (m, r9, 6.8 * k);        // R9
        c.addCapacitor (r9, o, 10 * nF);       // C3
        c.addCapacitor (o, out, 1 * u);
        c.addResistor (out, 0, 100 * k);
        c.addOutput (out);
    }

    path = SystemStats::getEnvironmentVariable ("CHAINRIG_LAB_PATH", "0").getIntValue();
}

double BbdChorus::rateHz (double knob) const noexcept
{
    return lfoMin * std::pow (lfoMax / lfoMin, jlimit (0.0, 10.0, knob) / 10.0);
}

void BbdChorus::prepareCircuit (double r)
{
    sampleRate = r;
    for (auto [s, p] : { std::pair { &rateSmoothed, rate }, std::pair { &depthSmoothed, depth } })
    {
        s->reset (r, 0.05);
        s->setCurrentAndTargetValue (p->get());
    }

    emphasis.prepare (r);
    antiAlias.prepare (r);
    recon.prepare (r);
    mixer.prepare (r);
    bbd.prepare (r, 0.06, stages);
    lfo.prepare (r);
    lfo.reset();
    control = 0.0;
}

void BbdChorus::processCircuit (float* samples, int n)
{
    rateSmoothed.setTargetValue (rate->get());
    depthSmoothed.setTargetValue (depth->get());

    double e, a, w, in2[2], y;
    for (int i = 0; i < n; ++i)
    {
        const double d = depthSmoothed.getNextValue() / 10.0;   // DEPTH 10k lineare
        const double tri = lfo.process (rateHz (rateSmoothed.getNextValue()));

        // C16 10 µF sul nodo di controllo, pilotato dal cursore di DEPTH (resistenza d(1-d) x 10k):
        // a metà corsa filtra l'LFO veloce, agli estremi no
        const double tau = jmax (1e-5, d * (1.0 - d) * 10e3 * 10e-6);
        control += (d * tri - control) * (1.0 - std::exp (-1.0 / (tau * sampleRate)));

        const double clock = centerClock * (1.0 + maxClockSwing * (path == 3 ? 0.0 : control));
        const double delay = bbd.delayForClock (clock);
        minDelay = jmin (minDelay, delay);
        maxDelay = jmax (maxDelay, delay);

        const double x = (double) samples[i] * inputVolts;
        emphasis.process (&x, &e);
        antiAlias.process (&e, &a);
        const double bbdOut = (double) bbd.process ((float) a, delay);
        recon.process (&bbdOut, &w);

        in2[0] = path == 2 || path == 3 ? 0.0 : e;
        in2[1] = path == 1 ? 0.0 : w;
        mixer.process (in2, &y);
        samples[i] = (float) (y * outputScale);
    }
}

String BbdChorus::getDiagnostics() const
{
    return "ritardo " + String (minDelay * 1e3, 2) + " … " + String (maxDelay * 1e3, 2) + " ms";
}
