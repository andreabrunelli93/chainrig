#include "PolyOctaver.h"

using namespace juce;

namespace
{
    // volumi: curva quadratica (a metà -12 dB), 10 = guadagno unitario
    inline float level (float knob) noexcept   { const float k = knob / 10.0f; return k * k; }
}

PolyOctaver::Info PolyOctaver::getInfo()
{
    Info i;
    i.identifier = "ChainRig Polyphonic Octaver";
    i.name = "Micro POG";   // nome scelto dall'utente; progetto originale, identificatore neutro
    i.descriptiveName = "Original polyphonic octave generator in the style of a digital octave pedal (built-in)";
    i.category = "Pitch";
    i.menuCategory = "Pitch";
    i.uniqueId = 0x504f4331;   // "POC1"
    i.oversamplingOrder = 0;   // le ottave sopra arrivano a 12 kHz: nessun ripiegamento a 44,1/48 kHz

    i.look.enclosureTop = Colour (0xffe9e6df);
    i.look.enclosureBottom = Colour (0xffc9c4ba);
    i.look.ink = Colour (0xff1d2b6b);
    i.look.knob = Colour (0xff1b1b1b);
    i.look.pointer = Colour (0xfff2efe6);
    i.look.led = Colour (0xffff3b2f);
    i.look.brushed = false;
    i.look.subtitle = "POLYPHONIC OCTAVE GENERATOR";
    i.look.wear = 0.04f;
    i.look.width = 860;
    return i;
}

PolyOctaver::PolyOctaver() : PedalProcessor (getInfo())
{
    dry    = addKnob ("dry",    "Dry",        10.0f);
    sub    = addKnob ("sub",    "Sub Octave", 7.0f);
    up     = addKnob ("up",     "Octave Up",  0.0f);
    filter = addKnob ("filter", "Filter",     10.0f);
}

void PolyOctaver::updateFilter (double knob)
{
    if (std::abs (knob - lastFilter) < 0.01)
        return;
    lastFilter = knob;

    // 200 Hz … 20 kHz esponenziale; 10 = aperto (oltre la banda delle ottave)
    const double fc = jmin (0.45 * sampleRate, 200.0 * std::pow (100.0, knob / 10.0));
    const double w = MathConstants<double>::twoPi * fc / sampleRate;
    const double alpha = std::sin (w) / (2.0 * 0.7071), c = std::cos (w), a0 = 1.0 + alpha;
    b0 = b2 = (1.0 - c) * 0.5 / a0;
    b1 = (1.0 - c) / a0;
    a1 = -2.0 * c / a0;
    a2 = (1.0 - alpha) / a0;
}

void PolyOctaver::prepareCircuit (double rate)
{
    sampleRate = rate;
    bank.prepare (rate);
    for (auto [s, p] : { std::pair { &drySmoothed, dry }, std::pair { &subSmoothed, sub }, std::pair { &upSmoothed, up } })
    {
        s->reset (rate, 0.03);
        s->setCurrentAndTargetValue (level (p->get()));
    }
    lastFilter = -1.0;
    updateFilter (filter->get());
    z1 = z2 = 0.0;
}

void PolyOctaver::processCircuit (float* samples, int n)
{
    drySmoothed.setTargetValue (level (dry->get()));
    subSmoothed.setTargetValue (level (sub->get()));
    upSmoothed.setTargetValue (level (up->get()));
    updateFilter (filter->get());

    for (int i = 0; i < n; ++i)
    {
        float down, octaveUp;
        bank.process (samples[i], down, octaveUp);

        const double wet = subSmoothed.getNextValue() * down + upSmoothed.getNextValue() * octaveUp;
        const double filtered = b0 * wet + z1;
        z1 = b1 * wet - a1 * filtered + z2;
        z2 = b2 * wet - a2 * filtered;

        samples[i] = (float) (drySmoothed.getNextValue() * samples[i] + filtered);
    }
}

String PolyOctaver::getDiagnostics() const
{
    String s = String (bank.getNumBands()) + " bande; potenza della somma:";
    for (double f : { 60.0, 100.0, 300.0, 1000.0, 3000.0, 5000.0 })
        s << " " << String (f, 0) << " Hz " << String (bank.powerResponseDb (f), 2) << " dB";
    return s;
}
