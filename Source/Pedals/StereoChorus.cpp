#include "StereoChorus.h"
#include "Taper.h"

using namespace juce;

namespace
{
    constexpr double k = 1e3, u = 1e-6, nF = 1e-9, pF = 1e-12;
    constexpr int stages = 1024;   // MN3007

    // Clock: VCO lineare (corrente di un BC557 su 470 pF). Dal manuale: ritardo massimo 15 ms →
    // clock minimo 512 / 15 ms = 34,1 kHz. Con WIDTH al massimo l'LFO porta il clock fino a 3 volte
    // (5 ms). Scala esatta del VCO non ricavata dallo schema (sfocato): scelta dichiarata.
    constexpr double minClock = stages / (2.0 * 0.015);
    constexpr double maxClockRatio = 3.0;

    // LFO: comparatori LM339 con RC (SPEED 1M C, C112 1 µF): il condensatore va verso un livello oltre
    // le soglie e si inverte quando le raggiunge → tratti esponenziali. Obiettivo a 2x la soglia.
    // Frequenza dal manuale: 0,1 … 10 Hz, curva esponenziale (potenziometro C).
    constexpr double lfoMinHz = 0.1, lfoMaxHz = 10.0;
    const double ln3 = std::log (3.0);

    // R141 47k e C115 3,3 µF tra WIDTH e la base del generatore di corrente: filtro di 0,15 s
    constexpr double controlTau = 47e3 * 3.3e-6;

    // BBD: livello massimo (morbido) e rumore (MN3007: S/N circa 76 dB su ~0,78 V efficaci)
    constexpr double bbdLimit = 1.4;
    constexpr double bbdNoiseRms = 0.78 * 1.6e-4;

    constexpr double companderReference = 0.35;   // volt medi raddrizzati a guadagno unitario
    constexpr double outputScale = 0.4;            // scala fisica (1 / 2,5 V)

    inline double softLimit (double x, double limit) noexcept
    {
        const double t = (x / limit) * (x / limit);
        return x / std::sqrt (std::sqrt (1.0 + t * t));
    }
}

StereoChorus::Info StereoChorus::getInfo()
{
    Info i;
    i.identifier = "ChainRig Stereo BBD Chorus";
    i.name = "SCF Chorus";   // nome scelto dall'utente (marchio TC Electronic): identificatore neutro
    i.descriptiveName = "Stereo bucket-brigade chorus with compander noise reduction (built-in)";
    i.category = "Modulation";
    i.menuCategory = "Modulation";
    i.uniqueId = 0x53424331;   // "SBC1"
    i.oversamplingOrder = 1;
    i.stereoOutput = true;

    i.look.enclosureTop = Colour (0xff2b2d31);
    i.look.enclosureBottom = Colour (0xff17181b);
    i.look.ink = Colour (0xffd8b25c);
    i.look.knob = Colour (0xff0f0f10);
    i.look.pointer = Colour (0xffd8b25c);
    i.look.subtitle = "STEREO CHORUS";
    i.look.wear = 0.15f;
    return i;
}

StereoChorus::StereoChorus() : PedalProcessor (getInfo())
{
    speed     = addKnob ("speed",     "Speed",     3.0f);
    width     = addKnob ("width",     "Width",     4.0f);
    intensity = addKnob ("intensity", "Intensity", 10.0f);   // asciutto e bagnato alla pari: il chorus più evidente
    gain      = addKnob ("gain",      "Gain",      3.0f);
    stereo    = addSwitch ("stereo",  "Stereo",    false);

    // ingresso: C1 47n, R1 1M verso il riferimento, R2 4k7, C2 220p, buffer U1.1
    {
        auto& c = input;
        const int in = c.addNode(), a = c.addNode(), b = c.addNode(), o = c.addNode();
        c.addInput (in);
        c.addCapacitor (in, a, 47 * nF);
        c.addResistor (a, 0, 1000 * k);
        c.addResistor (a, b, 4.7 * k);
        c.addCapacitor (b, 0, 220 * pF);
        c.addOpAmp (o, b, o);
        c.addOutput (o);
    }

    // filtro prima del compressore (U101.1): R104 10k, C102 4n7 in retroazione, R105 4k3, C103 2n2
    {
        auto& c = preFilter;
        const int in = c.addNode(), j = c.addNode(), kk = c.addNode(), o = c.addNode();
        c.addInput (in);
        c.addResistor (in, j, 10 * k);
        c.addCapacitor (j, o, 4.7 * nF);
        c.addResistor (j, kk, 4.3 * k);
        c.addCapacitor (kk, 0, 2.2 * nF);
        c.addOpAmp (o, kk, o);
        c.addOutput (o);
    }

    // anti-aliasing (LM13600 come inseguitore): 36k, 36k, C12 470p in retroazione, C13 220p
    {
        auto& c = antiAlias;
        const int in = c.addNode(), j = c.addNode(), kk = c.addNode(), o = c.addNode();
        c.addInput (in);
        c.addResistor (in, j, 36 * k);
        c.addCapacitor (j, o, 470 * pF);
        c.addResistor (j, kk, 36 * k);
        c.addCapacitor (kk, 0, 220 * pF);
        c.addOpAmp (o, kk, o);
        c.addOutput (o);
    }

    // ricostruzione: R23 24k, R26 24k, C18 1n5 in retroazione, C17 470p, poi R27 24k + C20 470p
    {
        auto& c = recon;
        const int in = c.addNode(), j = c.addNode(), kk = c.addNode(), o = c.addNode(), r = c.addNode();
        c.addInput (in);
        c.addResistor (in, j, 24 * k);
        c.addCapacitor (j, o, 1.5 * nF);
        c.addResistor (j, kk, 24 * k);
        c.addCapacitor (kk, 0, 470 * pF);
        c.addOpAmp (o, kk, o);
        c.addResistor (o, r, 24 * k);
        c.addCapacitor (r, 0, 470 * pF);
        c.addOutput (r);
    }

    path = SystemStats::getEnvironmentVariable ("CHAINRIG_LAB_PATH", "0").getIntValue();
}

void StereoChorus::prepareCircuit (double r)
{
    sampleRate = r;
    for (auto [s, p] : { std::pair { &speedSmoothed, speed }, std::pair { &widthSmoothed, width },
                         std::pair { &intensitySmoothed, intensity }, std::pair { &gainSmoothed, gain } })
    {
        s->reset (r, 0.05);
        s->setCurrentAndTargetValue (p->get());
    }

    input.prepare (r);
    preFilter.prepare (r);
    antiAlias.prepare (r);
    recon.prepare (r);
    compressor.prepare (CompanderSection::Mode::compress, r, 1 * u, companderReference);   // C8 1 µF
    expander.prepare (CompanderSection::Mode::expand, r, 1 * u, companderReference);       // C24 1 µF
    bbd.prepare (r, 0.03, stages);
    lfoValue = -1.0;
    lfoTarget = 2.0;
    control = 0.0;
}

void StereoChorus::processCircuit (float* samples, int n)
{
    // non usato: il pedale è stereo (PedalProcessor chiama processCircuitStereo)
    processCircuitStereo (samples, samples, n);
}

void StereoChorus::processCircuitStereo (float* left, float* right, int n)
{
    speedSmoothed.setTargetValue (speed->get());
    widthSmoothed.setTargetValue (width->get());
    intensitySmoothed.setTargetValue (intensity->get());
    gainSmoothed.setTargetValue (gain->get());

    const bool still = path >= 3, linear = path == 4;
    const bool stereoOn = stereo->isOn();
    double v;

    for (int i = 0; i < n; ++i)
    {
        // LFO a comparatori: esponenziale verso ±2, inversione a ±1; mezzo periodo = tau ln 3
        const double hz = lfoMinHz * std::pow (lfoMaxHz / lfoMinHz, speedSmoothed.getNextValue() / 10.0);
        const double tau = 1.0 / (2.0 * hz * ln3);
        lfoValue += (lfoTarget - lfoValue) * (1.0 - std::exp (-1.0 / (tau * sampleRate)));
        if (lfoTarget > 0.0 && lfoValue >= 1.0) lfoTarget = -2.0;
        if (lfoTarget < 0.0 && lfoValue <= -1.0) lfoTarget = 2.0;

        // WIDTH (25k lineare) e filtro RC verso il generatore di corrente; clock lineare nella tensione
        const double w = widthSmoothed.getNextValue() / 10.0;
        control += (w * 0.5 * (lfoValue + 1.0) - control) * (1.0 - std::exp (-1.0 / (controlTau * sampleRate)));
        const double clock = minClock * (1.0 + (maxClockRatio - 1.0) * (still ? 0.0 : control));
        const double delay = bbd.delayForClock (clock);
        minDelay = jmin (minDelay, delay);
        maxDelay = jmax (maxDelay, delay);

        // ingresso
        const double x = (double) left[i] * inputVolts;
        double dry;
        input.process (&x, &dry);

        // bagnato: GAIN (0 … +15 dB, logaritmico), filtro, compressore, BBD, ricostruzione, espansore
        const double g = Decibels::decibelsToGain (15.0 * gainSmoothed.getNextValue() / 10.0);
        const double pre = dry * g;
        preFilter.process (&pre, &v);
        double c = linear ? v : compressor.process (v);
        antiAlias.process (&c, &v);
        double b = (double) bbd.process ((float) (linear ? v : softLimit (v, bbdLimit)), delay);
        if (! linear)
            b += bbdNoiseRms * (noise.nextFloat() * 2.0 - 1.0) * 1.732;   // rumore bianco uniforme, stessa potenza
        recon.process (&b, &v);
        const double wet = (linear ? v : expander.process (v)) / g;

        // INTENSITY (25k logaritmico): livello del bagnato; uscite: asciutto ± bagnato
        const double mix = Taper::audio (intensitySmoothed.getNextValue());
        const double d = path == 2 || path >= 3 ? 0.0 : dry;
        const double e = path == 1 ? 0.0 : wet * (path >= 2 ? 1.0 : mix);
        // STEREO acceso: bagnato in controfase tra L e R come nel pedale (sommando le uscite l'effetto
        // sparisce); spento: tutte e due asciutto + bagnato (come usare una sola uscita del pedale)
        left[i] = (float) ((d + e) * outputScale);
        right[i] = (float) ((stereoOn ? d - e : d + e) * outputScale);
    }
}

String StereoChorus::getDiagnostics() const
{
    return "ritardo " + String (minDelay * 1e3, 2) + " … " + String (maxDelay * 1e3, 2) + " ms";
}
