# Portage du SCSP : suivi

Suivi du portage du SCSP (Yamaha YMF292-F) de Mednafen vers sm2-emu, en gardant
l'implémentation actuelle (issue de MAME) comme référence sélectionnable.

- Référence actuelle (« mame ») : [src/hw/scsp.cpp](src/hw/scsp.cpp),
  [src/hw/scsp.h](src/hw/scsp.h), [src/hw/scsp_dsp.cpp](src/hw/scsp_dsp.cpp),
  [src/hw/scsp_dsp.h](src/hw/scsp_dsp.h).
- Source étudiée (« mednafen ») : Mednafen 1.32.1, `src/ss/scsp.h`, `src/ss/scsp.inc`,
  `src/ss/sound.cpp` (copie locale : `E:\Arcade\emulateurs\mednafen\src\mednafen-1.32.1\src\ss`).

## Objectif

Disposer de deux cœurs SCSP interchangeables :

- **mame** : le cœur actuel, qui reste le cœur par défaut tant que l'autre n'est pas validé ;
- **mednafen** : le nouveau cœur, plus proche du matériel (EG, LFO, FM, DSP, timers,
  interruptions, MIDI).

On bascule de l'un à l'autre par configuration, pour comparer le rendu jeu par jeu,
sans toucher au reste de la carte son (68000, timing, DSB, balance par jeu).

## ⚠️ Licence : décision à prendre avant d'écrire le cœur

| Projet | Licence |
|---|---|
| sm2-emu | BSD-3-Clause + clause « pas d'usage commercial sans accord de l'auteur » |
| MAME (base du cœur actuel) | BSD-3-Clause, compatible |
| Mednafen | **GPL-2.0-or-later** |

La GPL interdit d'ajouter des restrictions, et la clause non commerciale de sm2-emu en
est une. Recopier ou traduire ligne à ligne `scsp.inc` produirait donc un binaire
qu'on ne peut pas distribuer légalement. Pistes :

1. **Réimplémentation indépendante (recommandée)** : écrire le cœur à partir de la
   description du comportement matériel (ce fichier, les docs Yamaha/Sega, des tests),
   en se servant de Mednafen comme documentation et non comme source à recopier.
2. **Demander une autorisation** à l'équipe Mednafen pour cette partie.
3. **Backend GPL optionnel** (`SM2_SCSP_MEDNAFEN=ON`, désactivé par défaut) : acceptable
   pour des essais locaux seulement, un binaire qui l'inclut ne serait pas distribuable.

À trancher avec l'auteur du projet amont (dmanlfc). Je ne suis pas juriste : c'est un
avis, pas une consultation.

- [ ] Décision licence prise : ______

## Architecture de la bascule

### Point d'intégration unique

Seule [Model2Sound](src/hw/model2_sound.h) instancie le SCSP (`m_scsp`). `main.cpp` lit
en plus `scsp().stats()` pour le rapport sans affichage. Ce qu'utilise la carte son,
et donc ce que déclare [ScspCore](src/hw/scsp_core.h) :

| Appel | Rôle |
|---|---|
| constructeur `(ScspMemory&, clock)`, `reset()` | construction, remise à zéro |
| `read(offset)`, `write(offset, data, mem_mask)` | registres, adresse en mots (accès octet par masque) |
| `generate(out, 1)` | un sample stéréo à 44 100 Hz, appelé tous les 256 cycles du 68000 |
| `midi_in(byte)`, `set_midi_out_handler` | liaison série avec la carte CPU |
| `set_irq_handler(level, assert)` | interruptions vers le 68000 |
| `set_slot_gains(gains[32])` | balance de volume par jeu |
| `sample_rate()`, `active_slots()` | fréquence, et nombre de voix pour la contention de bus ([model2_sound.cpp:296](src/hw/model2_sound.cpp:296)) |
| `stats()`, `serialize(Archive&)` | test sans affichage, save states |

### Découpage

```
src/hw/scsp_core.h        interface ScspCore (les appels ci-dessus, virtuels)       ✅ étape 1
src/hw/scsp.h/.cpp        ScspMame : le cœur actuel, corps de fonctions inchangés  ✅ étape 1
src/hw/scsp_dsp.h/.cpp    DSP du cœur mame, inchangé
src/hw/scsp_mdfn.h/.cpp   ScspMednafen : le nouveau cœur (slots, EG, LFO, DSP, timers, MIDI)
```

- `Model2Sound` détient un `std::unique_ptr<ScspCore>` ([model2_sound.h](src/hw/model2_sound.h)),
  pour l'instant toujours un `ScspMame`. L'étape 2 le fera choisir par la configuration.
  Le coût d'un appel virtuel par sample est négligeable (44 100 appels/s).
- `ScspCore::Stats` et les types de callbacks (`IrqHandler`, `MainIrqHandler`,
  `MidiOutHandler`) vivent dans l'interface ; `write()` n'a plus de masque par défaut
  (`Model2Sound` le passe toujours).
- Le cœur actuel garde ses noms et sa structure, pour rester comparable à MAME.
- **La bascule s'applique au chargement du jeu (ou au reset)**, pas à chaud : l'état
  interne d'un cœur ne se transpose pas dans l'autre.

### Sélection

- [ ] Clé `scsp_core = mame | mednafen` dans `sm2-emu.ini`, `mame` par défaut. Penser aux
      trois endroits : `struct Config` ([config.h](src/core/config.h)), lecture/écriture
      ([config.cpp](src/core/config.cpp)), et la recopie manuelle dans `options.config`
      ([main.cpp:807-917](src/main.cpp:807)).
- [ ] Option de ligne de commande `--scsp-core <mame|mednafen>` pour les comparaisons
      sans affichage.
- [ ] Menu déroulant dans l'onglet Son de l'interface (optionnel).
- [ ] Option CMake `SM2_SCSP_MEDNAFEN` pour exclure le nouveau cœur de la compilation
      (voir la section Licence).

### Save states

- [ ] `Model2Sound::serialize` écrit un identifiant de cœur avant l'état du SCSP.
      Charger un état produit par l'autre cœur est refusé avec un message clair.
- [ ] Incrémenter `kFormatVersion` ([archive.h:125](src/core/archive.h:125)), puisque la
      disposition change.

### Adaptations nécessaires côté nouveau cœur

- **Mémoire** : Mednafen possède sa propre RAM. Ici la RAM de 512 Ko appartient à
  `Model2Sound` et est partagée avec le 68000. Tous les accès passent par `ScspMemory`,
  qui mappe aussi la ROM de samples en 0x80000–0xFFFFF ([model2_sound.cpp:509](src/hw/model2_sound.cpp:509)).
- **Registres** : Mednafen expose `RW<T, IsWrite>(adresse, valeur)` en octets ou en mots ;
  sm2 appelle `write(offset_mot, data, mem_mask)`. Il faut un adaptateur, ou un accès
  unique à masque.
- **Interruptions** : Mednafen calcule directement le niveau 0–7 vers le 68000. Il suffit
  d'appeler `irq(level, true)`, ou `irq(0, false)` quand le niveau retombe à 0.
  L'interruption vers le CPU principal (MCIEB/MCIPD) n'est pas câblée sur Model 2.
- **MIDI** : Mednafen transmet bit à bit avec un diviseur ; ici l'octet est livré entier
  après la durée d'un octet. Même cadence visible par l'hôte.
- **Sortie** : ne pas reprendre la mise à l'échelle 27/32 de la Saturn (`sound.cpp`, hors
  du cœur). Garder la gestion DAC 16/18 bits.
- **Spécificités Model 2 à conserver** : EXTS à 0 ; écritures en 0x7C0–0x7FF ignorées (DoA) ;
  gains par slot (`set_slot_gains`) ; compteurs `Stats`.
- **`active_slots()`** : la contention du bus a été ajustée sur House of the Dead avec le
  décompte de voix du cœur mame. Pour le nouveau cœur, il faut une définition équivalente
  (par exemple, slots dont l'EG est sous 0x3C0 et qui lisent encore la mémoire), puis
  vérifier à nouveau le tempo de HotD.

## Méthode de comparaison A/B

Le mode sans affichage existe déjà : `--boot-test N` fait tourner N images, et
`--dump-audio fichier.wav` enregistre tout le son. Le générateur de bruit du cœur
mame a une graine fixe, donc deux exécutions identiques donnent le même WAV, à
condition de partir d'une NVRAM vierge à chaque fois.

[tools/scsp_ab.ps1](tools/scsp_ab.ps1) fait tout cela pour une liste de jeux (NVRAM
vierge, `--config` séparé pour ne pas réécrire le `sm2-emu.ini` de l'utilisateur,
empreintes SHA-256 des WAV dans `build/scsp_ab/<tag>.hashes`) et compare deux tags :

```
pwsh tools/scsp_ab.ps1 -Tag before -Roms <dossier des ROMs>
pwsh tools/scsp_ab.ps1 -Tag after  -Roms <dossier des ROMs> -Against before
pwsh tools/scsp_ab.ps1 -Tag mdfn   -Roms <dossier des ROMs> -Extra '--scsp-core','mednafen'
```

`tools/` est ignoré par le `.gitignore` du projet : `git add -f tools/scsp_ab.ps1`
pour versionner le script.

La liste par défaut compte 16 jeux dont le programme son tourne dans les 1 500
premières images (26 s) : vf2, hotd, stcc, vstriker, dynamcop, skytargt, fvipers,
zerogun, von, gunblade, vcop2, sgt24h, topskatr, dynabb97, bel, motoraid. Indy 500,
Last Bronx, Sega Water Ski et Planet Harriers restent muets sur cette durée.

- [x] Comparaison au bit près (empreintes), pour les étapes censées être inaudibles.
- [ ] Comparaison mesurée (niveau RMS, pic, spectre) et écoute alternée, pour le
      nouveau cœur.
- [x] Étape 1 validée : WAV **identiques au bit près** avant et après le passage par
      l'interface `ScspCore`.

## Plan par étapes

| # | Étape | Statut | Notes |
|---|---|---|---|
| 0 | Décision licence | à faire | voir plus haut |
| 1 | Interface `ScspCore` ; le cœur actuel devient `ScspMame` | ✅ fait | 20 WAV identiques au bit près, save states OK |
| 2 | Sélection (ini, CLI, CMake) + identifiant de cœur dans les save states | à faire | |
| 3 | Squelette du nouveau cœur : registres, DMA, timers, interruptions, MIDI | à faire | objectif : le driver son démarre, même muet |
| 4 | Lecture des slots : phase, interpolation, boucles, 8/16 bits, bruit LFSR, SBCTL | à faire | |
| 5 | EG + TL + ALFO dans le domaine de l'atténuation, MVOL logarithmique, pan/SDL | à faire | le plus audible |
| 6 | LFO : compteur partagé, période exacte, LFORE | à faire | |
| 7 | FM : pile de sons avec le retard de 4 slots | à faire | |
| 8 | DSP : 128 pas toujours exécutés, pipeline mémoire, EFREG | à faire | |
| 9 | Moniteur MSLC, interruption par sample (0x400), drapeaux et FIFO MIDI de 4 octets | à faire | |
| 10 | Réglages Model 2 : balance par jeu, contention du bus | à faire | peut demander de réajuster les gains |
| 11 | Validation par jeu, puis choix du cœur par défaut | à faire | |

## Différences relevées entre les deux cœurs

Relevées lors de la comparaison initiale du 2026-09-29. « sm2 » désigne le cœur mame actuel.

### Enveloppe (EG) et volume
- sm2 : vitesses tirées de tables en millisecondes ; attaque **linéaire** en amplitude
  ([scsp.cpp:1376](src/hw/scsp.cpp:1376)) ; key scaling = octave + 2×KRS + bit 9 de FNS
  ([scsp.cpp:559](src/hw/scsp.cpp:559)) ; EG, TL et ALFO multipliés séparément ;
  slot coupé à la fin du release.
- Mednafen (`scsp.inc` RunEG, l. 1006) : EG cadencé par le compteur global ; attaque
  exponentielle ; KRS + octave borné à 0–0xF ; EG, TL et ALFO **additionnés** puis
  plafonnés à 0x3FF ; lecture mémoire coupée au-delà de 0x3C0 ; les 32 slots tournent
  toujours.
- MVOL : linéaire dans sm2 (`MVOL/15`, [scsp.cpp:843](src/hw/scsp.cpp:843)),
  logarithmique chez Mednafen (environ 3 dB par pas).

### Modulation FM
- sm2 : pas de retard de pipeline ([scsp.cpp:1255](src/hw/scsp.cpp:1255)) ; le code
  `SCSP_FM_DELAY` est mort (`m_DELAYBUF` n'existe pas).
- Mednafen : sortie d'un slot écrite dans la pile 4 slots plus tard (`SoundStackDelayer`) ;
  l'échantillon suivant de l'interpolation utilise la phase du slot suivant.
- SBCTL appliqué après l'interpolation (sm2) contre avant (Mednafen) ; bruit xorshift
  (sm2) contre LFSR matériel de 17 bits (Mednafen).

### LFO
- sm2 : fréquence en Hz, phases séparées PLFO/ALFO, n'avance que pour les slots actifs,
  **LFORE ignoré** ([scsp.cpp:79](src/hw/scsp.cpp:79)).
- Mednafen : compteur 8 bits partagé, période exacte, LFORE géré, profondeur du vibrato
  fonction de FNS.

### DSP
- sm2 : ne démarre qu'après une écriture en 0xBF0 et s'arrête à `LastStep`, calculé une
  seule fois ([scsp_dsp.cpp:319](src/hw/scsp_dsp.cpp:319)).
- sm2 : `return` au milieu du programme si IRA > 0x31 ([scsp_dsp.cpp:190](src/hw/scsp_dsp.cpp:190)),
  ce qui saute la décrémentation de DEC et la remise à zéro de MIXS.
- sm2 : accès mémoire autorisés seulement aux pas impairs (contournement pour DoA,
  l. 278) et immédiats ; Mednafen modélise le retard de lecture et d'écriture.
- sm2 : ACC, FRC, Y_REG et ADRS_REG remis à 0 à chaque sample ; EFREG remis à zéro puis
  cumulé (`+=`). Mednafen conserve les registres et remplace EFREG.

### Timers, interruptions, MIDI
- Timers à un coup dans sm2 ([scsp.cpp:454](src/hw/scsp.cpp:454)) ; libres et calés sur
  le compteur global chez Mednafen.
- Niveau d'interruption : cascade de priorités MAME dans sm2 ([scsp.cpp:359](src/hw/scsp.cpp:359)),
  encodage SCILV exact et interruption par sample (0x400) chez Mednafen.
- MIDI : FIFO de 32 octets dans sm2 (4 sur le matériel) ; **drapeaux du registre 0x404
  jamais mis à jour** ([scsp.cpp:1017](src/hw/scsp.cpp:1017)).
- DMA : dans sm2, les registres d'adresse ne sont restaurés que dans un sens
  ([scsp.cpp:1520](src/hw/scsp.cpp:1520)) ; Mednafen ne les modifie jamais.

### Déjà au niveau (ou en avance) dans sm2
- Un sample tous les 256 cycles du 68000, synchronisé au sample près (comme Mednafen).
- Cycles d'attente et contention du bus RAM/SCSP pour le 68000 : absents de Mednafen
  (TODO chez eux).
- Fenêtre de ROM de samples propre au Model 2.

## Validation par jeu

Légende : ✅ correct, ⚠️ différence audible, ❌ cassé, — non testé.

| Jeu | Particularité | mame | mednafen | Remarques |
|---|---|---|---|---|
| hotd | FM, tempo (contention ajustée dessus) | — | — | |
| vf2 | réverbération DSP | — | — | |
| doa | DSP, MADRS | — | — | |
| daytona | Model 2 d'origine | — | — | |
| srallyc | MSLC (bips de fin de musique) | — | — | |
| stcc | DSB (musique MPEG) | — | — | seulement les effets passent par le SCSP |
| indy500 | | — | — | |
| vcop | Model 2 d'origine | — | — | |
| vstriker | MSLC | — | — | |
| lastbrnx | | — | — | |

## Journal

- **2026-09-29** : création du fichier. Comparaison initiale des deux cœurs, choix d'une
  interface `ScspCore` avec bascule au chargement du jeu, point licence relevé.
- **2026-09-29** : étape 1. Nouvelle interface [scsp_core.h](src/hw/scsp_core.h) ; `Scsp`
  renommé `ScspMame` et dérivé de `ScspCore` (seules les déclarations et le préfixe
  `ScspMame::` changent, les corps de fonctions restent ceux de MAME) ; `Model2Sound`
  passe par un `std::unique_ptr<ScspCore>`. Validation : 20 jeux capturés sur 1 500
  images avant et après, WAV identiques au bit près (16 sonores, 4 muets), et
  `--savestate-test 900` PASS sur vf2 et hotd. Format des save states inchangé.
  Ajout de [tools/scsp_ab.ps1](tools/scsp_ab.ps1).
