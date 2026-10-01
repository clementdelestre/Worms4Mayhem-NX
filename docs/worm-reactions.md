# Réactions des vers (acting W4M)

Dans W4M, tout ce que font les vers non actifs vient de `Data/Tweak/WORMACTING.XOM`. Ce fichier est une banque de 142 petits films EFMV (`EFMV_MovieContainer`).

Chaque film porte le nom de son déclencheur, sous la forme `<Déclencheur><N><variante>`. Il contient des pistes. Le nom d'une piste est une chaîne de critères de casting, par exemple `Crit Special`, `Foe1 OnScreen Near1 Idle` ou `Friend-1`. Chaque piste porte des événements horodatés en ms :
- `WormEmote` : émotion faciale, gardée en boucle jusqu'à la suivante ;
- `PlayAnimation` : un clip de geste ;
- `TriggerSpeech` : une catégorie de voix `.lsd` ;
- `WormLookAt` et `WormGestureAt` : le ver regarde ou désigne une cible ;
- `ThreatenWorm`, `SpawnParticle`, `Stop`.

Le moteur (`WXSceneManagerService`, `WXActor.cpp`) choisit le film et distribue les rôles. Jetons de critères relevés dans l'exe : `Distraction Goodies Threat Safe Interesting Targeted OnScreen Active Behind InFront Special Near Blind Abducted Sick Friend Crit` ; les données ajoutent `Foe Enemy See LOS Idle Payload`. Les scripts Lua (`stdvs.lub`) ne jouent aucune animation en match : seuls les cinématiques et les outtakes en jouent.

Notre version est dans `client/src/acting.cpp` : les pistes transcrites, puis un lecteur de scènes. Tout se passe au rendu : le sim n'a aucun état nouveau et rien n'est checksummé. Les variantes sont tirées par un hash de seed, index du ver et `g.clock`, pas par `rand()`. Les clips viennent de `worm.glb` (`tools/w4m-models`, `WORM_CLIPS`). Les voix viennent de `voices/<banque>/<nom>.ogg` (`tools/w4m-import`, `VOICES`).

## Limites de l'adaptation

- **Une seule couche.** Un geste remplace l'émotion le temps du clip. W4M superpose le visage (Emote) et le corps (Play).
- **Émotions non exportées.** Happy, Angry, Frown, Grumpy, Default, Normal, Disgust, Interested, Curious, Patronising, EvilGrin et Awestruck jouent surtout sur le visage, presque invisible à distance de jeu. Elles deviennent « effacer l'émotion » (`"-"`).
- **Émotions exportées :** Scared, Terror, Nervous, CowerEmote, CantLook, Sad, Ill.
- **Gestes absents du fichier.** Certains films nomment des clips qui n'existent pas : `CoverHead` (TimedPayloadThree0), `ShakeFist` (Victory). Ils ne jouent rien dans W4M non plus et sont ignorés ici.
- **Clips de remplacement :** Taunt1 remplace Taunt2/3, Chuckle remplace Chuckle2, Sneeze remplace Sneeze2.
- **Clips non exportés et donc ignorés :** Point, PointAndWave, WaveAndPoint, Puzzled, YouLookBad, Search, PolishEyebrow, FakeShotgun, CountFingers, ThumbBlank, Gunslinger1/2, FlickBogey, Guilty, LiveLongAndProsper, Thinking, Tantrum.
- **LookAt / GestureAt non faits.** Le ver ne tourne pas la tête vers la bombe ou le tireur.
- **Rayon « Near » inconnu.** Aucune valeur n'a été trouvée dans les tweaks ni dans l'exe, sauf `Near0,4` et `Near0,20` (unité inconnue). Les rayons utilisés ici sont choisis à la main et signalés « choix ».
- **Voix.** Une seule réplique à la fois (0,9 s entre deux), toutes équipes confondues.

Sources :
- **XOM** : `WORMACTING.XOM` décodé (nom du film).
- **exe** : chaînes de `WormsMayhem.exe`.
- **lsd** : catégories de `Data/Audio/Speech/*.lsd`. Les 33 banques ont les mêmes 43 catégories.
- **forum** : archive du forum Team17, http://tim32.org/~muzer/t17-archive/forum.team17.com/archive/index.php/t-32651.html
- **wiki** : https://worms.miraheze.org/wiki/Speech
- **HG101** : http://www.hardcoregaming101.net/worms-3d/amp/

Statuts :
- **fait** : implémenté ;
- **partiel** : implémenté avec un écart, décrit dans la ligne ;
- **manquant** : pas fait.

## Charge posée ou grenade près d'un ver

Déclencheur moteur `TimedPayloadOne..Five`, `GrenadeOne..Five`. N vaut le nombre de secondes de mèche restantes, plafonné à 5 (inférence).

Condition (choix) : un projectile à mèche (`Kind::Shell`, `fuse > 0`) est arrêté ou posé à moins de `radius + 1` m d'un ver qui n'est pas actif. Le ver réagit une fois par menace. Une mine armée compte aussi, à moins de 3 m. Le « Foe » est l'ennemi le plus proche du ver menacé, à moins de 10 m.

| Film | Ver menacé (anim, voix) | Spectateur | Source | Statut |
|---|---|---|---|---|
| TimedPayloadFive0a | Startled 0,08 s → Scared → Shake_Fist + ShakeFist (2,05 s) → Cover_Head (3,66 s) → Terror → fin 7,47 s | ennemi : Titter + Titter (2,1 s) | XOM | fait |
| TimedPayloadFive0b | Startled + Startled → Scared → Gasp + Gasp (0,62 s) → Cover_Head (2,76 s) | ennemi : Titter + Titter, puis Sad | XOM | fait |
| TimedPayloadFive0c | Startled → Scared → **Blow** (souffle la mèche, 0,6 s) → fin 6,24 s | ennemi : Chuckle (2,1 s), Cover_Head (4,76 s) | XOM ; forum : « he tries to blow it away » | fait (capture `dyn_zoom.png`) |
| TimedPayloadFive10 | Scared → Shriek + Shriek (1,03 s) → CantLook | ami proche : Scared, Shriek + Shriek | XOM | fait |
| TimedPayloadFour0 | Disbelief + Disbelief → Pray (3,1 s) | – | XOM | fait |
| TimedPayloadThree0 | Scared → CantLook (0,35 s) → fin 3 s | – | XOM | fait |
| TimedPayloadTwo0 | Terror → voix Startled (0,59 s) → CantLook → Shriek + Shriek (2,79 s) | – | XOM | fait |
| TimedPayloadOne0 | Startled + voix Disbelief → Terror → Scared → fin 3,87 s | – | XOM | fait |
| Grenade* | mêmes scènes, avec en plus la voix **GrenadeLanded** au début | – | XOM, lsd | fait, la voix est jouée au cast |
| TimedPayloadFive5 / 20a / 20b (`Blind0` : la victime ne voit pas la bombe, Search → Shriek ; un ami fait WaveAndPoint « derrière toi ! ») | – | – | XOM | manquant : pas de test de vue, clips absents |

## Animaux et vieille dame qui approchent

| Déclencheur | Anim, voix | Condition | Source | Statut |
|---|---|---|---|---|
| Mouton, Super mouton, vieille dame à moins de `radius + 1` m | scène TimedPayloadTwo0 (Terror, CantLook, Shriek + Shriek) | choix : W4M n'a pas de film pour ce cas, et la scène TimedPayload est aussi utilisée pour les charges posées | inférence | fait (capture `sheep.png`) |
| Mine armée à moins de 3 m | scène TimedPayload selon la mèche restante | choix | inférence (props `THREAT` de l'exe) | fait |
| Objet de décor dangereux, à bonus ou à distraire (ItemReact10a-g : Scared + Startled, Happy + WaveAndPoint, ScratchHead) | – | flags de prop THREAT / GOODIES / DISTRACT | XOM, exe | manquant : pas de props marqués |

## Ver visé par l'actif

Condition (choix) : le ver est dans l'axe horizontal du tir (moins de 14°, entre 1 et 40 m) pendant 0,5 s, phase Aim, hors hot seat, avec une arme d'attaque. Une fois par tour.

| Film | Anim | Source | Statut |
|---|---|---|---|
| Targeted0a (ennemi) | CowerEmote → Scared (4,14 s) → Wipe_Brow (« ouf », 4,74 s) → fin 8,3 s | XOM ; forum (« holds up its hands or scared face », « phew ») | fait |
| Targeted10a (ennemi) | Terror → **Indicate** (montre le tireur, 0,8 s) → Startled → ShakeHead | XOM | fait (capture `aim.png`) |
| Targeted0b (ami visé) | ShakeHead (1,14 s) → Salute (7,85 s) | XOM ; forum (« shakes head in confusion ») | partiel : Puzzled non exporté |
| Targeted10a, 2ᵉ ennemi proche (`Foe0 Near1`) qui s'effraie aussi | – | XOM | manquant |
| Traitor au visé : il désigne son voisin | – | forum | manquant |

## Projectiles qui arrivent et frappes aériennes

| Déclencheur | Anim, voix | Condition | Source | Statut |
|---|---|---|---|---|
| WeaponFired10 (tir d'une arme d'attaque) | tous les ennemis du tireur : Scared (0,7 s) → Gasp (2,2 s) ou Watch_Distant → fin 7,5 s | événement Fire | XOM | fait |
| WeaponFired10, le tireur lui-même (voix Titter) | – | – | XOM | manquant : voix jugée hors de propos |
| WeaponFired10, les amis du tireur (Point, Search) | – | – | XOM | manquant : clips non exportés |
| Airstrike10 (Frappe aérienne, Super frappe, âne, Fatkins) | un ennemi « Crit » : Scared + voix **Incoming** → Shriek (7,56 s) ; les autres ennemis : Scared → ShakeHead ou Shriek (5,4-6,7 s) → Cover_Head (8,4-8,9 s) ; les amis : Watch_Distant (3,74 s) → Scared | Fire d'un `Kind::Airstrike` ou `Kind::Donkey` | XOM ; wiki (« Incoming: air strike approaching ») | fait |
| Inondation, enlèvement | aucun film Flood ; enlèvement : Abducted5-30 et Zap (LiveLongAndProsper, Scared, Terror) | – | XOM | manquant : clips absents, enlèvement en DLC |
| Mort subite (montée de l'eau) | aucun film dédié | – | XOM | sans objet |

## Coups, vol et atterrissage

| Film | Anim, voix | Condition | Source | Statut |
|---|---|---|---|---|
| Touché (flinch) | Hit_Front | Hurt | existant | fait (main.cpp `animEvent`) |
| DamageInflicted0a, tir ami | victime : voix **Traitor** (3,18 s) ; amis proches de la victime : SeeImpact → Doh → WhatWereYouThinking ; ennemis proches : Chuckle + Titter → Cheer | Hurt infligé par l'équipe active, hors tick de poison | XOM | fait |
| DamageInflicted, ennemi touché | camp du tireur : voix **DamageInflictedA** (une fois par tour) ; mêmes spectateurs | idem | XOM | fait |
| DamageInflicted, victime ennemie (voix DamageInflictedB) | – | – | XOM | manquant : déjà beaucoup de voix sur un coup |
| Blasted10 (projeté) | victime : voix **Nooo** (+ Sad) ; ami proche : CantLook ; ennemi proche : Scared → PointAndLaugh | vitesse horizontale > 7 m/s en l'air (choix) | XOM | fait |
| WormBounce10 (atterrissage lourd) | voix **WormBounce** ; ami : Gasp + Gasp → Sad ; ennemi : PointAndLaugh | vitesse de chute < -9 m/s à l'atterrissage (choix) | XOM | fait |
| FallSplat0, BlastSplat0, FastRecover0a/b, MaxDamage10 | SeeImpact, Doh, Titter, Chuckle2, SighAndShakeHead, Cheer | – | XOM | manquant : déclencheurs fins non distingués |
| Vol | Blastflight2 | – | existant | fait (Blastflight3-5 non utilisés) |
| Poing, batte (voix Punch) | – | – | lsd | manquant |

## Poison, peu de PV et tour qui arrive

| Film | Anim, voix | Condition | Source | Statut |
|---|---|---|---|---|
| Poisoned5a / 5b | Ill → ClutchChest + voix ClutchChest, ou Ill → Vomit (particule Vomit_Fluid) | le poison du ver augmente | XOM | fait, sans la particule |
| Sick10a-c (idle malade) | Ill en boucle ; Vomit, Sneeze + voix Sneeze (1,2 / 8,9 / 17,9 s) | le ver est empoisonné et au repos | XOM ; tweak `Worm.Poison.Default` 10 | fait. Manque la teinte `Sick.Colour`, la particule `WXP_Sneeze`, les réactions des amis (YouLookBad) |
| Peu de PV | Wounded | hp < 25 | existant | fait. Aucun film W4M pour ce cas |
| Tour qui arrive | aucun film pour les vers non actifs ; StartTurn5-30 pour l'actif (Salute, WiggleBrows, malade : Sneeze2) | – | XOM | voix StartTurn existante. Gestes de début de tour manquants |
| ShortOnTime5 | voix **ShortOnTime** | moins de 5 s de tour (choix, la valeur W4M est inconnue) | XOM | fait |
| Waiting5 | voix **Waiting** | l'actif est immobile 12 s en visée (choix) | XOM | fait. Le WaveAndPoint des amis manque |

## Attente et ennui (vers non actifs)

| Film | Anim, voix | Condition | Source | Statut |
|---|---|---|---|---|
| Idle20a-f | Wave, Yawn2 + voix Yawn, Salute, Thumbs_Up, Cheer, Watch_Distant (Nervous) | toutes les 14-30 s par ver, au repos (choix ; W4M exige aussi `OnScreen`) | XOM | fait |
| Bored0a-c | Bored + voix SadSigh → Sad, Yawn, Yawn2 + voix Yawn | dans le même tirage que Idle20 | XOM | fait |
| ScratchHead, Yawn | anciennes fidgets, dans le même tirage | – | existant | fait |
| Idle0a/b/d (l'actif approche un ami) | Salute, Cheer, ClaspHands | l'actif passe à moins de 3 m (choix) ; une fois par tour | XOM | fait |
| Idle10a-c (l'actif approche un ennemi) | BringItOn → Taunt1 ; Taunt1 + voix Taunt ; Startled + Startled → Shake_Fist + ShakeFist | idem | XOM ; forum (« shakes his fists ») | fait |
| Idle50, Idle100, Idle200 | duels Gunslinger, FlickBogey, Guilty, Titter | paires de vers | XOM ; forum (high-five, duel au doigt) | manquant : clips non exportés |
| Thinking0-10 (tour du CPU) | Thinking + particule | – | XOM | manquant |
| Retreat0a-c (l'actif en retraite) | WiggleBrows, Nod | – | XOM | manquant |

## Mort d'un ver ou d'un coéquipier

| Film | Anim, voix | Condition | Source | Statut |
|---|---|---|---|---|
| Death5a-e | Sad → WhatWereYouThinking, SighAndShakeHead, Salute, ClutchChest ou Doh ; voix **FriendlyDeath** | `g.dying() == i` : la caméra est sur le ver, avant l'explosion (voir docs/death-sequence.md). Ensuite Wave | XOM | fait. La voix reste jouée à l'explosion (main.cpp onEvent Death) |
| Death15a / 20a (ver empoisonné) | Ill → Vomit | idem, ver empoisonné | XOM | fait |
| Death10 (amis proches) | Sad | ami à moins de 8 m | XOM | fait. Le Happy des ennemis n'est pas exporté |
| FirstBlood10 | voix **FirstBlood** par l'équipe active ; l'ami le plus proche du tireur : Cheer | première mort ennemie du match | XOM | fait |
| EnemyDeath | voix **EnemyDeath** par l'équipe active | les morts ennemies suivantes | lsd (pas de film : jouée par le code de jeu, inférence) | fait |
| Noyade | voix **ShallowDrown** (`Voice::Drown`, à défaut la voix `death`) puis FallDrown | événement Splash d'un ver | lsd | fait |

## Tir raté, erreur, abandon

| Film | Anim, voix | Condition | Source | Statut |
|---|---|---|---|---|
| Missed10a | l'ennemi le plus proche du tireur : Wipe_Brow + voix **Missed** (1,02 s) ; un ami proche : SighAndShakeHead | passage en Settle sans aucun ver touché | XOM ; wiki | fait |
| Mistake0a | le tireur : Sad → Doh + voix **Mistake** (2 s) ; ennemi proche : PointAndLaugh ; ami : SighAndShakeHead | seule sa propre équipe a été touchée | XOM | fait |
| Boring0a (NoDamageA/B) | WhatWereYouThinking | – | XOM | manquant : déclencheur incertain |
| SkipGo5-20 | SighAndShakeHead + voix **SkipGo** | Fire d'un Skip Go ou d'un Surrender | XOM | fait |
| Revenge (voix seule) | – | toucher son dernier agresseur | lsd ; wiki | manquant : le sim ne suit `lastHitTeam` qu'en Highlander |

## Caisses

| Film | Anim, voix | Condition | Source | Statut |
|---|---|---|---|---|
| Collect5/10 | voix **Collect** | événement Collect | XOM | fait |
| CrateDrop10a | voix **CrateDrop** (équipe active) ; gestes : WaveAndPoint, Gasp, Tantrum, ShakeHead | événement CrateDrop | XOM, lsd | partiel : la voix seule |

## Victoire et défaite

| Film | Anim, voix | Condition | Source | Statut |
|---|---|---|---|---|
| Victory (gagnants) | Cheer ×2-3, ou ClaspHands ×3, sur 20 s, puis Victorious_Grin ; voix Victory | GameOver | XOM | fait. La voix reste celle de onEvent |
| Victory, perdants (`Foe0` : Sad, WhatWereYouThinking, SighAndShakeHead, SadSigh, Doh) | – | – | XOM | manquant : en Deathmatch, les perdants sont morts |

## Voix importées

Catégories importées (`tools/w4m-import` `VOICES`, `audio.h` `Voice`) :
- fire, hurt, death, victory, jump, idle ;
- startled, grenade (GrenadeLanded), shriek, gasp, shakefist, titter, disbelief, incoming, missed, mistake, traitor, damage (DamageInflictedA), firstblood, enemydeath, sadsigh, yawn, sneeze, clutchchest, nooo, bounce (WormBounce), taunt, waiting, shortontime, skipgo, collect, cratedrop, drown (ShallowDrown).

Non importées : DamageInflictedB, NoDamageA/B, MaxDamage, Revenge, Punch, PullNinjaRope, Cheer, PointAndLaugh, Wipebrow.

Les banques de repli `romfs/voices/male` et `female` n'ont que les 6 premières catégories : les autres répliques sont muettes.
