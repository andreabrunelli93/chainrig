#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <functional>

//==============================================================================
/** Controlla in background l'ultima release pubblicata su GitHub.

    Legge solo l'API pubblica delle release (nessun dato inviato oltre alla richiesta)
    e chiama `onResult` sul thread dell'interfaccia. */
class UpdateChecker final : private juce::Thread
{
public:
    struct Result
    {
        bool ok = false;               // risposta letta correttamente
        bool newer = false;            // c'è una versione più recente di quella in uso
        juce::String latestVersion;    // es. "0.3.0"
        juce::String pageUrl;          // pagina della release
    };

    static constexpr const char* releasesApi = "https://api.github.com/repos/andreabrunelli93/chainrig/releases/latest";
    static constexpr const char* releasesPage = "https://github.com/andreabrunelli93/chainrig/releases/latest";

    UpdateChecker() : Thread ("Update check") {}
    ~UpdateChecker() override { stopThread (6000); }

    /** Avvia un controllo (se ce n'è già uno in corso, non fa nulla). */
    void check (const juce::String& currentVersion, std::function<void (Result)> onResultIn)
    {
        if (isThreadRunning())
            return;

        current = currentVersion;
        onResult = std::move (onResultIn);
        startThread (Priority::low);
    }

    /** Confronta versioni "1.2.3" (accetta anche il prefisso "v"). */
    static bool isNewer (const juce::String& candidate, const juce::String& installed)
    {
        const auto parts = [] (juce::String v)
        {
            v = v.trim().trimCharactersAtStart ("vV");
            juce::StringArray tokens;
            tokens.addTokens (v, ".", {});
            std::array<int, 3> n { 0, 0, 0 };
            for (int i = 0; i < 3 && i < tokens.size(); ++i)
                n[(size_t) i] = tokens[i].getIntValue();
            return n;
        };

        return parts (candidate) > parts (installed);
    }

private:
    void run() override
    {
        Result result;

        const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                 .withConnectionTimeoutMs (6000)
                                 .withExtraHeaders ("Accept: application/vnd.github+json\r\nUser-Agent: ChainRig");

        if (auto stream = juce::URL (releasesApi).createInputStream (options))
        {
            const auto json = juce::JSON::parse (stream->readEntireStreamAsString());
            const auto tag = json["tag_name"].toString();

            if (tag.isNotEmpty())
            {
                result.ok = true;
                result.latestVersion = tag.trimCharactersAtStart ("vV");
                result.pageUrl = json["html_url"].toString().isNotEmpty() ? json["html_url"].toString()
                                                                         : juce::String (releasesPage);
                result.newer = isNewer (tag, current);
            }
        }

        if (threadShouldExit())
            return;

        auto callback = onResult;
        juce::MessageManager::callAsync ([callback, result] { if (callback) callback (result); });
    }

    juce::String current;
    std::function<void (Result)> onResult;
};
