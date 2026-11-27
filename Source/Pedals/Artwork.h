#pragma once

#include <juce_graphics/juce_graphics.h>

//==============================================================================
/** Disegni incisi sul corpo dei pedali integrati, come Path in un riquadro 200 x 200
    (si scalano con Path::getTransformToScaleToFit). Disegni originali. */
namespace Artwork
{
    /** Parti del disegno: ognuna va riempita a sé (unite in un solo Path, dove si sovrappongono
        con versi diversi la regola "non zero" lascerebbe dei buchi). */
    struct Drawing
    {
        std::vector<juce::Path> fills;   // parti piene, in inchiostro
        std::vector<juce::Path> cuts;    // dettagli in negativo (colore del corpo), sopra le parti piene

        bool empty() const { return fills.empty(); }
    };

    /** Riquadro che contiene tutte le parti. */
    juce::Rectangle<float> getBounds (const Drawing&);

    /** Centauro centurione: elmo romano con cresta, lancia e scudo, zampa anteriore alzata. */
    Drawing centurionCentaur();
}
