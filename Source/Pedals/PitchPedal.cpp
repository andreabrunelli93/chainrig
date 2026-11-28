#include "PitchPedal.h"
#include "Expression.h"

using namespace juce;

namespace
{
    // intervallo in semitoni a pedale tutto giù, nell'ordine dei modi
    constexpr double intervals[] = { 24, 12, 7, 5, -2, -5, -7, -12, -24, -36 };

    // il pedale d'espressione MIDI ha 128 passi: su 24 semitoni sono 0,19 semitoni l'uno e si
    // sentirebbero a scalini. Filtro a un polo sui semitoni (dominio logaritmico)
    constexpr double smoothSeconds = 0.015;
}

const StringArray& PitchPedal::modeNames()
{
    static const StringArray names { "2 OCT UP", "OCT UP", "5TH UP", "4TH UP", "2ND DOWN",
                                     "4TH DOWN", "5TH DOWN", "OCT DOWN", "2 OCT DOWN", "DIVE BOMB" };
    return names;
}

PitchPedal::Info PitchPedal::getInfo()
{
    Info i;
    i.identifier = "ChainRig Pitch Shifter";
    i.name = "Whammy";   // nome scelto dall'utente; progetto originale, identificatore neutro
    i.descriptiveName = "Original pitch-shift pedal in the style of a whammy, expression-pedal controlled (built-in)";
    i.category = "Pitch";
    i.menuCategory = "Pitch";
    i.uniqueId = 0x50534831;   // "PSH1"
    i.oversamplingOrder = 0;   // nessuna distorsione: l'oversampling aggiungerebbe solo latenza e CPU

    i.look.enclosureTop = Colour (0xffd8262e);
    i.look.enclosureBottom = Colour (0xff98141b);
    i.look.ink = Colour (0xfff4efe6);
    i.look.knob = Colour (0xff141414);
    i.look.pointer = Colour (0xfff4efe6);
    i.look.led = Colour (0xff35ff6a);
    i.look.subtitle = "PITCH SHIFTER";
    i.look.wear = 0.15f;
    return i;
}

PitchPedal::PitchPedal() : PedalProcessor (getInfo())
{
    treadle = addKnob ("treadle", "Treadle", 0.0f);
    mode = addSelector ("mode", "Mode", modeNames(), 1);
}

float PitchPedal::currentTreadle() const noexcept
{
    const float e = ExpressionPedal::value.load (std::memory_order_relaxed);
    return e >= 0.0f ? e : treadle->get() / 10.0f;
}

float PitchPedal::getKnobDisplayValue (int index) const
{
    return index == 0 ? currentTreadle() * 10.0f : PedalProcessor::getKnobDisplayValue (index);
}

void PitchPedal::prepareCircuit (double rate)
{
    sampleRate = rate;
    smoothCoeff = 1.0 - std::exp (-1.0 / (smoothSeconds * rate));
    shifter.prepare (rate);
    semitones = intervals[(size_t) jlimit (0, 9, roundToInt (mode->get()))] * currentTreadle();
}

void PitchPedal::processCircuit (float* samples, int n)
{
    const double target = intervals[(size_t) jlimit (0, 9, roundToInt (mode->get()))] * (double) currentTreadle();

    for (int i = 0; i < n; ++i)
    {
        semitones += smoothCoeff * (target - semitones);
        if (std::abs (semitones - target) < 1e-4)
            semitones = target;   // a riposo esattamente all'unisono: ritardo puro, nessuna modulazione

        const double ratio = semitones == 0.0 ? 1.0 : std::exp2 (semitones / 12.0);
        samples[i] = shifter.process (samples[i], ratio);
    }
}

String PitchPedal::getDiagnostics() const
{
    return "giunture " + String (shifter.splices);
}
