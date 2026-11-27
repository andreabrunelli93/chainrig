#include "VcaTremolo.h"
#include "Taper.h"

using namespace juce;

namespace
{
    // periodo dell'LFO: 90-900 ms (costruttore). Con RATE 100k lineare in serie a Rfissa il periodo è
    // proporzionale a Rfissa + Rpot: rapporto 10 -> Rfissa = 100k / 9 (scelta che rispetta il campo)
    constexpr double minPeriod = 0.090;
    constexpr double rateFixed = 100e3 / 9.0, ratePot = 100e3;

    // WAVE: guadagno dello stadio che squadra il triangolo, 1 (triangolo) … 25 (quasi quadra)
    constexpr double waveMaxGain = 25.0;

    // R8 100k x C4 0,1 µF (con R10 1M in parallelo) sul comando del VCA
    constexpr double controlTau = (100e3 * 1e6 / (100e3 + 1e6)) * 0.1e-6;

    // tosatura morbida sui rail dell'op-amp (lineare fino a poco prima di ±1)
    inline double railClip (double x) noexcept
    {
        const double x2 = x * x, x6 = x2 * x2 * x2;
        return x / std::pow (1.0 + x6, 1.0 / 6.0);
    }
}

VcaTremolo::Info VcaTremolo::getInfo()
{
    Info i;
    i.identifier = "ChainRig VCA Tremolo";
    i.name = "TR-2 Tremolo";   // nome scelto dall'utente; identificatore neutro
    i.descriptiveName = "VCA tremolo with triangle-to-square LFO (built-in)";
    i.category = "Modulation";
    i.menuCategory = "Modulation";
    i.uniqueId = 0x54524d31;   // "TRM1"
    i.oversamplingOrder = 0;   // VCA lineare: nessuna armonica da ripiegare

    i.look.enclosureTop = Colour (0xff23a99c);
    i.look.enclosureBottom = Colour (0xff137167);
    i.look.ink = Colour (0xff10201e);
    i.look.knob = Colour (0xff121212);
    i.look.pointer = Colour (0xfff2efe6);
    i.look.subtitle = "VCA TREMOLO";
    i.look.wear = 0.15f;
    return i;
}

VcaTremolo::VcaTremolo() : PedalProcessor (getInfo())
{
    rate  = addKnob ("rate",  "Rate",  5.0f);
    wave  = addKnob ("wave",  "Wave",  3.0f);
    depth = addKnob ("depth", "Depth", 7.0f);
}

void VcaTremolo::prepareCircuit (double r)
{
    sampleRate = r;
    for (auto [s, p] : { std::pair { &rateSmoothed, rate }, std::pair { &waveSmoothed, wave }, std::pair { &depthSmoothed, depth } })
    {
        s->reset (r, 0.05);
        s->setCurrentAndTargetValue (p->get());
    }
    triangle = -1.0;
    direction = 1.0;
    control = 1.0;
    controlCoeff = 1.0 - std::exp (-1.0 / (controlTau * r));
}

void VcaTremolo::processCircuit (float* samples, int n)
{
    rateSmoothed.setTargetValue (rate->get());
    waveSmoothed.setTargetValue (wave->get());
    depthSmoothed.setTargetValue (depth->get());

    for (int i = 0; i < n; ++i)
    {
        // LFO: rampa a pendenza costante tra -1 e +1 (integratore + comparatore); in senso orario più veloce
        const double period = minPeriod * (rateFixed + ratePot * (1.0 - rateSmoothed.getNextValue() / 10.0)) / rateFixed;
        triangle += direction * 4.0 / (period * sampleRate);
        if (triangle >= 1.0)  { triangle = 2.0 - triangle;  direction = -1.0; }
        if (triangle <= -1.0) { triangle = -2.0 - triangle; direction = 1.0; }

        // WAVE: amplifica e tosa (100k logaritmico)
        const double gain = 1.0 + (waveMaxGain - 1.0) * Taper::audio (waveSmoothed.getNextValue());
        const double shape = railClip (gain * triangle);

        // DEPTH: guadagno da 1 (massimo dell'LFO) fino a 1 - depth (minimo), poi il filtro sul comando
        const double d = depthSmoothed.getNextValue() / 10.0;
        const double target = 1.0 - d * 0.5 * (1.0 - shape);
        control += (target - control) * controlCoeff;

        samples[i] = (float) (samples[i] * control);
    }
}
