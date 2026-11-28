#pragma once

#include <atomic>

//==============================================================================
/** Pedale d'espressione MIDI (impostazione `cc_expression`, predefinito CC 11), condiviso da tutti i
    pedali integrati che lo usano. Lo scrive il thread MIDI, lo leggono i thread audio e dell'interfaccia:
    non passa dal thread dell'interfaccia (nessun ritardo) e non è un parametro (non entra nei preset e
    non segna il preset come modificato). */
namespace ExpressionPedal
{
    inline std::atomic<float> value { -1.0f };   // 0 … 1; -1 = nessun messaggio ricevuto da quando l'app è aperta
}
