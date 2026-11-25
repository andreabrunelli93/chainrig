#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

//==============================================================================
/** Ricerca dei plugin in un processo separato.

    Ogni file di plugin viene aperto da una seconda copia di ChainRig avviata con
        --scan-plugin <formato> <file> <file-risultato>
    Se il plugin si blocca o va in crash, muore solo quella copia: il programma
    principale aspetta al massimo `timeoutMs`, mette il file tra gli esclusi
    e passa al successivo. */
namespace PluginScanner
{
    inline constexpr const char* argument = "--scan-plugin";
    inline constexpr int timeoutMs = 30000;

    /** Vero se questo processo è stato avviato solo per esaminare un plugin. */
    inline bool isScannerProcess()
    {
        return juce::JUCEApplicationBase::getCommandLineParameterArray().contains (argument);
    }

    /** Processo figlio: esamina il file e scrive le PluginDescription in XML.
        Restituisce il codice di uscita (0 = esaminato, anche se non contiene plugin). */
    inline int runScannerProcess()
    {
        const auto args = juce::JUCEApplicationBase::getCommandLineParameterArray();
        const int index = args.indexOf (argument);

        if (index < 0 || index + 3 >= args.size())
            return 2;

        const auto formatName = args[index + 1];
        const auto fileOrIdentifier = args[index + 2];
        const juce::File resultFile (args[index + 3]);

        juce::AudioPluginFormatManager formatManager;
        juce::addDefaultFormatsToManager (formatManager);

        juce::XmlElement root ("PLUGINS");

        for (auto* format : formatManager.getFormats())
        {
            if (format->getName() != formatName)
                continue;

            juce::OwnedArray<juce::PluginDescription> found;
            format->findAllTypesForFile (found, fileOrIdentifier);

            for (auto* desc : found)
                if (desc != nullptr)
                    root.addChildElement (desc->createXml().release());
        }

        // Il file viene scritto solo alla fine: se manca, il figlio è morto a metà
        return root.writeTo (resultFile) ? 0 : 3;
    }

    //==============================================================================
    /** Lato programma principale: KnownPluginList lo chiama dal thread della ricerca. */
    class OutOfProcessScanner final : public juce::KnownPluginList::CustomScanner
    {
    public:
        bool findPluginTypesFor (juce::AudioPluginFormat& format,
                                 juce::OwnedArray<juce::PluginDescription>& result,
                                 const juce::String& fileOrIdentifier) override
        {
            juce::TemporaryFile resultFile (".xml");

            const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
            juce::StringArray command { exe.getFullPathName(), argument, format.getName(),
                                        fileOrIdentifier, resultFile.getFile().getFullPathName() };

            juce::ChildProcess child;

            if (! child.start (command, 0))
                return false;

            // Attende a piccoli passi per poter interrompere se l'utente preme Annulla
            for (int waited = 0; child.isRunning(); waited += 100)
            {
                if (shouldExit() || waited >= timeoutMs)
                {
                    child.kill();
                    return shouldExit();   // annullato: niente esclusione; timeout: escluso
                }

                juce::Thread::sleep (100);
            }

            if (child.getExitCode() != 0)
                return false;

            auto xml = juce::parseXML (resultFile.getFile());

            if (xml == nullptr || ! xml->hasTagName ("PLUGINS"))
                return false;   // crash a metà (su macOS un segnale può dare codice 0)

            for (auto* e : xml->getChildIterator())
            {
                juce::PluginDescription desc;

                if (desc.loadFromXml (*e))
                    result.add (new juce::PluginDescription (desc));
            }

            return true;
        }
    };
}
