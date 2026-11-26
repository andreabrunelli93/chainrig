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
- Rinominare i posti e sceglierne il tipo, che decide icona ed etichetta (aggiunto su richiesta dell'utente).
- Barra laterale con il livello d'uscita e la spia CLIP, che resta accesa fino al tocco (aggiunta su richiesta dell'utente).
- Pulsante REC: registra l'uscita in WAV nella cartella Download (aggiunto su richiesta dell'utente).

Nella catena l'utente userà: catture NAM A2 (plugin TONE3000 o NAM), IR, Valhalla, Strymon BigSky.

## Struttura
- `Source/Engine.*`: AudioDeviceManager + AudioProcessorPlayer + AudioProcessorGraph.
  Il grafo va da ingresso → TunerProcessor → posti → uscita (solo canali 1-2).
  - Le modifiche alla struttura si fanno con `UpdateKind::none` e poi `rewire()`, che chiama `graph.rebuild()`.
  - Il bypass è `Node::setBypassed`.
  - Il grafo ha un `TempoPlayHead` con il BPM del preset (chiave `bpm`, predefinito 120). Senza, il player di JUCE passa una posizione senza tempo: i VST3 ricevono tempo 0 e un delay sincronizzato (ValhallaDelay) andava al tempo massimo, come un looper di 10-20 s.
  - Ogni posto ha `customName` e `SlotKind` (amp, ir, drive, compressor, eq, modulation, delay, reverb, fx oppure automatic, che viene ricavato da categoria e nome). Nei preset sono le chiavi `label` e `kind`.
  - Ogni posto salva la `PluginDescription` usata per crearlo: serve per i preset, perché quella dell'istanza VST3 punta al file `.so`/`.dll` interno e non al bundle.
  - Il MIDI viene ricevuto sul thread MIDI e passato al thread dell'interfaccia con `callAsync` + `WeakReference`.
  - Impostazioni in `ChainRig.settings`, con le chiavi `cc_button1..4`, `cc_tuner`, `cc_nextPreset`, `cc_prevPreset` e `midiMode`.
- `Source/Recorder.h`: è lui il callback della scheda audio. Avvolge l'`AudioProcessorPlayer`, poi copia le uscite 1-2 in un
  `ThreadedWriter` (WAV 24 bit, `~/Downloads`). Usa un `ScopedTryLock` per non bloccare mai il thread audio. Si ferma
  quando cambia la scheda audio. Calcola anche il picco per il misuratore (`takePeak`/`takeClipped`, atomici) letto da
  `OutputMeter` in MainComponent.
- `Source/PluginChooser.*`: finestra "Scegli plugin" (IMPOSTAZIONI). Elenca i file VST3 con una casella ciascuno. APPLICA toglie i
  tipi dei file senza spunta e chiama `scanAndAddFile` (su un suo thread) solo per i file spuntati e mancanti. L'utente ha
  tutti i plugin di Logic: vuole importare solo quelli che usa dal vivo. La lista JUCE completa resta sotto "Avanzate...".
- `Source/PluginScanner.h`: ricerca dei plugin fuori processo. `KnownPluginList::setCustomScanner` avvia
  `ChainRig --scan-plugin <formato> <file> <risultato.xml>` per ogni file, con un timeout di 30 s. In caso di timeout o crash
  il file finisce tra gli esclusi. La copia figlia non crea l'Engine, nasconde l'icona nel Dock e chiude subito.
  Su questo Mac `WaveShell1-VST3 16.0` si blocca anche da solo.
- `Source/Tuner.h`: nodo di passaggio, FIFO lock-free verso l'interfaccia, `detectPitchYin`, testato con errore sotto 0,1 cent da 82 Hz a 988 Hz a 44,1, 48 e 96 kHz.
- `Source/Look.*`: Palette (grafite e un solo accento ambra), Fonts (di sistema: Bahnschrift su Windows, Avenir Next su Mac, Consolas/Menlo per i numeri), Icons (tracciati 24x24 per i tipi di posto e per l'interfaccia), StageLookAndFeel, StageButton. Il design di riferimento è nell'artifact "ChainRig – nuovo design" (1368x912 = Surface Pro 7 al 200%).
- `Source/MainComponent.*`: interfaccia touch: PresetSelector, StatusDisplay (due righe), SlotView (numero, tipo con icona, nome, stato), BigButton (pedali con LED), OutputMeter, TunerOverlay (nota grande, scala a 41 segmenti, corde), PluginWindow sempre in primo piano (per la modalità schermo intero), menu a comparsa grandi.
  - I letterali con caratteri non ASCII (per esempio "·") vanno passati con `String::fromUTF8`: `String(const char*)` li legge come ASCII.
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
- Diagnostica audio: `Recorder` misura quanto dura ogni blocco rispetto al tempo disponibile. `Engine::checkAudioHealth` (ogni 500 ms) mette nella barra di stato "picco %" e "xrun" e scrive gli eventi in `scan.log` (righe AUDIO). Il 25/11/2026 sul Mac, con buffer 128 a 44,1 kHz, BABY Audio Crystalline da solo dava circa 18 blocchi al minuto oltre il tempo (fino al 209%). La catena vuota e i singoli TONE3000 e ValhallaDelay ne davano 0. CoreAudio non conta questi casi come xrun.
- Quando carichi o sostituisci un plugin c'è un attimo di silenzio. Si può migliorare preparando il plugin in anticipo e facendo una dissolvenza.
- Se un plugin va in crash, si chiude tutto il programma.
- Se un plugin mono-in/stereo-out è in bypass, il canale destro resta muto.
- Sul Mac non vengono caricati gli Audio Unit (`JUCE_PLUGINHOST_AU=0`). Per aggiungerli bisogna creare le istanze in modo asincrono, perché gli AUv3 lo richiedono.
- I preset salvano i percorsi dei file `.nam` e delle IR dentro lo stato del plugin: passando da Mac a Windows vanno ricaricati.

## Lingua
Interfaccia, commenti e README in italiano.
