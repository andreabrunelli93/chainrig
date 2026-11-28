#include "VcaCompressor.h"

using namespace juce;

namespace
{
    // soglia del rivelatore (picco raddrizzato, dBFS) e scale delle manopole: scelte dichiarate.
    // Con INPUT 5 (+5 dB) una chitarra a -18 dBFS di picco riceve circa 8 dB di riduzione a 4:1
    // (il punto di partenza "Added Control" del manuale).
    constexpr double thresholdDb = -24.0;
    constexpr double inputMinDb = -10.0, inputMaxDb = 20.0;
    constexpr double outputMinDb = -20.0, outputMaxDb = 20.0;
    constexpr double attackMin = 50e-6, attackMax = 50e-3;     // manuale: 50 µs - 50 ms
    constexpr double releaseMin = 50e-3, releaseMax = 1.0;     // manuale: 50 ms - 1 s
    constexpr double tiltMaxDb = 6.0;                          // TONE agli estremi: ±6 dB agli acuti, ∓6 ai bassi
    constexpr double tiltPivotHz = 500.0;                      // manuale
    constexpr double sidechainHz = 100.0;
    constexpr float ratios[] = { 2.0f, 4.0f, 10.0f };
    constexpr int toneUpdateInterval = 32;

    inline double knobDb (float knob, double lo, double hi) noexcept   { return lo + (hi - lo) * knob / 10.0; }

    // tempi logaritmici sulla manopola (in senso orario più lenti, come nel manuale)
    inline double knobTime (float knob, double lo, double hi) noexcept { return lo * std::pow (hi / lo, knob / 10.0); }

    // coefficiente per Eulero implicito: c' = (c + a target) / (1 + a) decade come exp(-t / tau)
    inline double implicitCoeff (double tau, double rate) noexcept     { return std::exp (1.0 / (tau * rate)) - 1.0; }
}

VcaCompressor::Info VcaCompressor::getInfo()
{
    Info i;
    i.identifier = "ChainRig VCA Compressor";
    i.name = "Empress Compressor";   // nome scelto dall'utente; progetto originale, identificatore neutro
    i.descriptiveName = "Studio-style VCA compressor with parallel mix and tilt EQ (built-in)";
    i.category = "Compressor";
    i.menuCategory = "Dynamics";
    i.uniqueId = 0x56434131;   // "VCA1"
    i.oversamplingOrder = 0;   // VCA lineare: niente armoniche da ripiegare

    i.look.enclosureTop = Colour (0xff2357b0);
    i.look.enclosureBottom = Colour (0xff12306b);
    i.look.ink = Colour (0xffff8c1a);
    i.look.knob = Colour (0xff141518);
    i.look.pointer = Colour (0xffff8c1a);
    i.look.led = Colour (0xffff8c1a);
    i.look.subtitle = "STUDIO VCA";

    // due file di tre manopole: INPUT OUTPUT MIX / ATTACK RELEASE TONE
    i.look.width = 660;
    i.look.height = 720;
    i.look.knobPlaces = { { 0.2f, 84, 92 }, { 0.5f, 84, 92 }, { 0.8f, 84, 92 },
                          { 0.2f, 256, 92 }, { 0.5f, 256, 92 }, { 0.8f, 256, 92 } };
    i.look.nameTop = 432.0f;
    return i;
}

VcaCompressor::VcaCompressor() : PedalProcessor (getInfo())
{
    input   = addKnob ("input",   "Input",   5.0f);
    output  = addKnob ("output",  "Output",  5.8f);   // circa il livello del bypass con le altre predefinite (misurato)
    mix     = addKnob ("mix",     "Mix",     10.0f);  // 10 = solo compresso
    attack  = addKnob ("attack",  "Attack",  3.0f);   // 0,4 ms
    release = addKnob ("release", "Release", 4.0f);   // 0,17 s
    tone    = addKnob ("tone",    "Tone",    5.0f);   // 5 = piatto
    ratio   = addSelector ("ratio", "Ratio", { "2:1", "4:1", "10:1" }, 1);
    sidechainFilter = addSwitch ("schpf", "SC HPF", false);
}

void VcaCompressor::prepareCircuit (double rate)
{
    sampleRate = rate;
    for (auto [s, p] : { std::pair { &inputSmoothed, input }, std::pair { &outputSmoothed, output },
                         std::pair { &mixSmoothed, mix }, std::pair { &toneSmoothed, tone } })
    {
        s->reset (rate, 0.05);
        s->setCurrentAndTargetValue (p->get());
    }

    // passa-alto Butterworth del secondo ordine (bilineare) per il rivelatore
    {
        const double k = std::tan (MathConstants<double>::pi * sidechainHz / rate), q = MathConstants<double>::sqrt2 / 2.0;
        const double norm = 1.0 / (1.0 + k / q + k * k);
        auto& f = sidechainHighPass;
        f.b0 = norm;
        f.b1 = -2.0 * norm;
        f.b2 = norm;
        f.a1 = 2.0 * (k * k - 1.0) * norm;
        f.a2 = (1.0 - k / q + k * k) * norm;
        f.reset();
    }

    tilt.reset();
    lastTilt = 1e9;
    updateTone (tiltMaxDb * (tone->get() - 5.0) / 5.0);
    reduction = 0.0;
    untilToneUpdate = 0;
}

void VcaCompressor::updateTone (double tiltDb)
{
    if (std::abs (tiltDb - lastTilt) < 1e-4)
        return;
    lastTilt = tiltDb;

    // tilt del primo ordine con perno a 500 Hz: H(s) = sqrt(G) (s + w/sqrt(G)) / (s + w sqrt(G)),
    // G = rapporto acuti/bassi; |H| = 1 a 500 Hz, 1/sqrt(G) ai bassi, sqrt(G) agli acuti
    const double w = MathConstants<double>::twoPi * tiltPivotHz;
    const double k = w / std::tan (w / (2.0 * sampleRate));   // bilineare con il perno esatto
    const double rootG = Decibels::decibelsToGain (tiltDb);   // sqrt(G): tiltDb per parte
    const double zero = w / rootG, pole = w * rootG;
    const double norm = 1.0 / (k + pole);
    tilt.b0 = rootG * (k + zero) * norm;
    tilt.b1 = rootG * (zero - k) * norm;
    tilt.b2 = 0.0;
    tilt.a1 = (pole - k) * norm;
    tilt.a2 = 0.0;
}

void VcaCompressor::processCircuit (float* samples, int n)
{
    inputSmoothed.setTargetValue (input->get());
    outputSmoothed.setTargetValue (output->get());
    mixSmoothed.setTargetValue (mix->get());
    toneSmoothed.setTargetValue (tone->get());

    const double r = ratios[jlimit (0, 2, roundToInt (ratio->get()))];
    const double attackCoeff = implicitCoeff (knobTime (attack->get(), attackMin, attackMax), sampleRate);
    const double releaseCoeff = implicitCoeff (knobTime (release->get(), releaseMin, releaseMax), sampleRate);
    const bool filterSidechain = sidechainFilter->isOn();

    for (int i = 0; i < n; ++i)
    {
        if (--untilToneUpdate <= 0)
        {
            updateTone (tiltMaxDb * (toneSmoothed.getCurrentValue() - 5.0) / 5.0);
            untilToneUpdate = toneUpdateInterval;
        }
        toneSmoothed.getNextValue();

        const double dry = samples[i];
        const double x = dry * Decibels::decibelsToGain (knobDb (inputSmoothed.getNextValue(), inputMinDb, inputMaxDb));

        // rivelatore (raddrizzatore) sull'ingresso del VCA, eventualmente filtrato: in retroazione
        // il livello che vede è quello dell'uscita, cioè questo meno la riduzione c (in dB)
        const double sc = filterSidechain ? sidechainHighPass.process (x) : x;
        const double over = 20.0 * std::log10 (std::abs (sc) + 1e-12) - thresholdDb;   // L - soglia

        // c' = c + a (f(c') - c'), f(c) = (R - 1) max(0, over - c): risolta esattamente (f è lineare
        // a tratti e decrescente, quindi la soluzione è unica). Steady state: c = (1 - 1/R) over.
        // Attacco se c va verso l'alto (c sotto l'equilibrio), rilascio altrimenti.
        const double target = over > 0.0 ? (1.0 - 1.0 / r) * over : 0.0;
        const double a = reduction < target ? attackCoeff : releaseCoeff;
        double next = (reduction + a * (r - 1.0) * over) / (1.0 + a * r);   // ramo sopra la soglia
        if (over <= 0.0 || next > over)                                      // ramo sotto la soglia
            next = reduction / (1.0 + a);
        reduction = jmax (0.0, next);

        const double wet = x * Decibels::decibelsToGain (-reduction);

        // MIX lineare tra pulito (prima di INPUT) e compresso, poi TONE e OUTPUT
        const double m = mixSmoothed.getNextValue() / 10.0;
        const double mixed = tilt.process (m * wet + (1.0 - m) * dry);
        samples[i] = (float) (mixed * Decibels::decibelsToGain (knobDb (outputSmoothed.getNextValue(), outputMinDb, outputMaxDb)));

        maxReduction = jmax (maxReduction, reduction);
    }

    gainReductionDb = (float) reduction;
}

String VcaCompressor::getDiagnostics() const
{
    return "riduzione massima " + String (maxReduction, 1) + " dB, ultima " + String (gainReductionDb.load(), 1) + " dB";
}
