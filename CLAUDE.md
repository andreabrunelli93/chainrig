# ChainRig: contesto per Claude Code

## Cos'è
Un host per plugin VST3 da usare dal vivo, scritto con JUCE 9 (CMake, C++20), per un chitarrista.
Deve fare "accendi e suoni" su un Microsoft Surface Pro 7 (i5-1035G4, 8 GB, Windows 11, interfaccia
Focusrite, driver ASIO). Lo sviluppo e le prove si fanno su un MacBook Pro con Apple Silicon (CoreAudio).

## Funzioni richieste (ambito deciso con l'utente: non allargarlo senza chiedere)
- Catena in serie da 10 posti, con plugin VST3 a scelta. I posti vuoti vengono saltati.
- Ricerca dei plugin, con la lista salvata.
- Bypass per ogni posto.
- Accordatore (YIN) che silenzia l'uscita.
- 4 pulsanti touch grandi che accendono o spengono i posti assegnati, comandabili anche da pedaliera MIDI (CC).
- Preset in file JSON.
- Bassa latenza (ASIO su Windows).

Nella catena l'utente userà: catture NAM A2 (plugin TONE3000 o NAM), IR, Valhalla, Strymon BigSky.

## Struttura
- `Source/Engine.*`: AudioDeviceManager + AudioProcessorPlayer + AudioProcessorGraph.
  Il grafo va da ingresso → TunerProcessor → posti → uscita (solo canali 1-2).
  - Le modifiche alla struttura si fanno con `UpdateKind::none` e poi `rewire()`, che chiama `graph.rebuild()`.
  - Il bypass è `Node::setBypassed`.
  - Ogni posto salva la `PluginDescription` usata per crearlo: serve per i preset, perché quella dell'istanza VST3 punta al file `.so`/`.dll` interno e non al bundle.
  - Il MIDI viene ricevuto sul thread MIDI e passato al thread dell'interfaccia con `callAsync` + `WeakReference`.
  - Impostazioni in `ChainRig.settings`, con le chiavi `cc_button1..4`, `cc_tuner`, `cc_nextPreset`, `cc_prevPreset` e `midiMode`.
- `Source/Tuner.h`: nodo di passaggio, FIFO lock-free verso l'interfaccia, `detectPitchYin`, testato con errore sotto 0,1 cent da 82 Hz a 988 Hz a 44,1, 48 e 96 kHz.
- `Source/MainComponent.*`: interfaccia touch con StageLookAndFeel scuro, SlotView, BigButton, TunerOverlay, PluginWindow sempre in primo piano (per la modalità schermo intero), menu a comparsa grandi.
- `.github/workflows/build-windows.yml`: compila l'exe per Windows su GitHub Actions.

## Compilare
- Mac: `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j8`, poi apri
  `build/ChainRig_artefacts/Release/ChainRig.app`. Serve il permesso per il microfono, già impostato nel CMake.
- Windows: `cmake -B build -G "Visual Studio 17 2022" -A x64 && cmake --build build --config Release`.
- JUCE viene scaricato con FetchContent (tag 9.0.6). Con `-DJUCE_PATH=...` puoi usare una copia locale.

## Stato
La versione 0.1 compila senza warning ed è stata provata su Linux in un container. Lì hanno funzionato caricamento dei plugin, bypass, pulsanti,
scambio dei posti e salvataggio/caricamento dei preset, usando l'AudioPluginExample di JUCE. Non è stata ancora provata con audio
reale, né su Mac, né su Windows.

## Limiti noti e idee per dopo
- Quando carichi o sostituisci un plugin c'è un attimo di silenzio. Si può migliorare preparando il plugin in anticipo e facendo una dissolvenza.
- Se un plugin va in crash, si chiude tutto il programma.
- Se un plugin mono-in/stereo-out è in bypass, il canale destro resta muto.
- Sul Mac non vengono caricati gli Audio Unit (`JUCE_PLUGINHOST_AU=0`). Per aggiungerli bisogna creare le istanze in modo asincrono, perché gli AUv3 lo richiedono.
- I preset salvano i percorsi dei file `.nam` e delle IR dentro lo stato del plugin: passando da Mac a Windows vanno ricaricati.

## Lingua
Interfaccia, commenti e README in italiano.
