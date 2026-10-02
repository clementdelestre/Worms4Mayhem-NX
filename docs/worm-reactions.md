# Réactions des vers (acting W4M)

Dans W4M, tout ce que font les vers hors de leur contrôle vient de `Data/Tweak/WORMACTING.XOM`. Ce fichier est une banque de 142 petits films EFMV (`EFMV_MovieContainer`, docs/w4m-map.md §19).

Chaque film porte le nom de son déclencheur, sous la forme `<Déclencheur><N><variante>`. Il contient des pistes. Le nom d'une piste est une chaîne de critères de casting, par exemple `Crit Special`, `Foe1 OnScreen Near1 Idle` ou `Friend-1`. Chaque piste porte des événements horodatés en ms :
- `WormEmote` : émotion du visage, gardée jusqu'à la suivante, même après la fin du film ;
- `PlayAnimation` / `StopAnimation` : un geste du corps ;
- `TriggerSpeech` : une catégorie de voix `.lsd` ;
- `WormLookAt` / `WormGestureAt` : la tête / les bras se tournent vers l'acteur d'une autre piste ;
- `ThreatenWorm`, `SpawnParticle`.

## Données et code

- **Données.** Les films ne sont plus recopiés dans le code. `tools/w4m-re/acting.py` les exporte depuis l'installation du joueur vers `client/assets/acting.txt` (gitignoré, comme les modèles et les voix). Sans ce fichier, aucune scène n'est jouée.
- **Moteur.** `client/src/acting.cpp` reproduit `WXSceneManagerService` : le classement, le casting et le lecteur. Tout se passe au rendu : le sim n'a aucun état nouveau et rien n'est checksummé. Les tirages (mélange des candidats, émotion par défaut, rotation des déclencheurs d'ambiance) viennent d'un hash de la seed, de l'index du ver et de `g.clock`.
- **Rendu.** `client/src/models.cpp` (`Models::Layers`) superpose deux couches au clip du corps, comme `WormPoseManager` (0x59da40) : le visage et la tête. Un ver mort ou mourant (hp ≤ 0) n'en a aucune : il ne montre que son clip (FallDrown, Wave).
- **Clips.** `tools/w4m-models` (`WORM_CLIPS`) exporte 41 clips de plus, dont 13 émotions. Chaque émotion est exportée avec sa bouche (`Happy+HappyMouth`).
- **Taille.** Les canaux immobiles d'un clip ne gardent qu'une clé. `worm.glb` passe ainsi de 12,6 à 13,5 Mo malgré les clips ajoutés. `Point` est coupé à 8 s (`Point@8`) : W4M le tient 26,7 s, mais au-delà de 0,17 s il ne fait que cligner des yeux.

Sources :
- **XOM** : `WORMACTING.XOM` décodé ;
- **exe** : `WormsMayhem.exe`, adresse donnée ;
- **lsd** : catégories de `Data/Audio/Speech/*.lsd` ;
- **wiki** : https://worms.miraheze.org/wiki/Speech ;
- **choix** : non vérifié dans W4M.

## Couches d'animation (WormPoseManager)

| Couche | W4M | Ici | Source |
|---|---|---|---|
| Visage | Chaque émotion a deux clips : `X` (sourcils, paupières, petits décalages de la tête et des épaules) et `XMouth` (lèvres). Le planificateur mélange canal par canal : un geste qui anime un canal du visage le garde. | Les os du visage (`upperlip*`, `bottomlip*`, `mouthmiddle*`, `eyetop*`, `eyebrow*`) prennent l'émotion, posée par rapport à la tête du geste. C'est vrai sauf pour les os que le geste déplace lui-même par rapport à Base : ce masque est calculé au chargement, par clip. | `w4m-models --list` (`W4M_CHANNELS`) |
| Émotion par défaut | Chaque ver reçoit `Angry` ou `Frown` à pile ou face, et la garde jusqu'au premier `WormEmote`. `Default` revient à elle. | Pareil, par hash. | exe 0x5a595d |
| Tête (LookAt) | `HeadRotY` ±60°. `HeadRotX` : de 70° vers le bas à 45° vers le haut, avec t = 1 + angle/(π/2). La cible est visée depuis un point 9 unités au-dessus des pieds. La tête approche son angle de 0,1 par image de 20 ms. | Rotation de la tête, du visage et de HatLocator autour de l'articulation de la tête (retrouvée depuis HatLocator). Le sens « haut » a été vérifié sur capture. | exe 0x59be40, 0x59d470 ; clés des clips. Loi de lissage 0x47a1a0 non décodée |
| Bras (GestureAt) | `Left/RightArmRotX/Y` ±90°. Les clés de Y sont miroir. L'angle vient de la direction de la cible dans le repère du ver, pondérée par des poids d'acting. | Non fait. Les poids (+0x134..+0x198) ne sont pas décodés. Une version avec un poids de 1 détachait les mains du corps (FallDrown, Wave), elle a été retirée. | exe 0x59c3e0, 0x59b870 |
| Yeux | `Eyes_LR/UD` : décalage de texture (`$animTex`). | Non fait. | – |
| Teinte | Couleur = 1 + 2·(wSick·(Sick.Colour/255 − 0,5) + wAbd·(Abducted.Colour/255 − 0,5)). Le vert est calculé en /255·0,5, comme dans l'exe. Sick.Colour = (120, 120, 110), Abducted.Colour = (0, 120, 255). | Multiplie la teinte d'équipe. Le poids monte et descend au même rythme que la tête (choix). | TWEAK.XOM ; exe 0x5a1bb1 |

## Casting (WXSceneManagerService)

- **Classement (0x60e100, 0x60d5e0, 0x60c410).** Chaque film est rangé sous chaque déclencheur dont son nom contient le nom (sans casse). N est le nombre qui suit ce nom. Une liste est triée par N décroissant. Après chaque déclenchement, chaque groupe de même N tourne d'un cran, ce qui alterne les variantes a/b/c.
- **Critères (0x60d640, 0x60c480).** Ce sont des sous-chaînes sans casse suivies d'un `atoi`. Exemples : `Seer0` vaut `See0`, et `Goodie` ne vaut pas `Goodies`.
  - `See`, `Blind`, `InFront` : un cône de 70°.
  - `Near N,R` : le plus proche à moins de 20·R unités, R = 20 par défaut, soit 20 m.
  - `-1` : l'équipe active pour Friend/Foe, et la caméra pour See, Near et LookAt (supposé).
  - Un ver malade ne joue que dans une piste `Sick`, et inversement. Un ver enlevé ne joue que dans une piste `Abducted`.
  - Une piste `Idle` refuse un ver déjà en scène, sauf s'il s'ennuie (90 s sans événement physique, supposé : 0x5a47d0) et que le déclencheur l'autorise.
  - `Safe` refuse un ver menacé (`ThreatenWorm`).
- **Choix (0x60d830).**
  - Une piste `Payload` prend la charge. Une piste `Active` prend le ver actif : si l'un manque, le film est rejeté.
  - Une piste `Special` puise dans la réserve spéciale. Selon le déclencheur, un échec la rejette ou non.
  - Les autres pistes puisent dans la réserve générale. Un échec rejette le film seulement si la piste est `Crit`.
  - Le premier film qui caste au moins un acteur est joué.
- **Priorités.** Il n'y a pas de comparaison numérique. Un nouveau film arrête tout film en cours d'un ver qu'il caste (0x60b750). Il faut toutefois que la piste l'admette : `Idle` (voir plus haut) et `Safe` filtrent. La fin d'un film libère le ver et sa menace (0x60bf70). L'émotion et le regard restent.
- **Réserves par déclencheur (table de saut 0x60e818).** Cela résout une question ouverte de w4m-map §21. Les déclencheurs 0x1e et 0x2e sont Missed et Retreat.

| Cas | Déclencheurs | Réserve spéciale / générale | Options (A, B, C) |
|---|---|---|---|
| 0 | TimedPayload*, CrateDrop | vers à moins de 100 unités (5 m) de la charge / les autres. Les films s'enchaînent tant qu'il reste des vers menacés. | 1, 1, 1 |
| 1 | BlastSplat, FallSplat, Death, Collect, Blasted, Poisoned, Zap | [sujet] / tous sauf le sujet et l'actif | 1, 1, 1 |
| 2 | Idle, Sick, Abducted, ItemReact, Thinking | – / tous les acteurs (objets compris) sauf l'actif | 0, 0, 1 |
| 3, 8, 9 | DamageInflicted, FirstBlood, MaxDamage (Revenge) | vers blessés ce tour / les autres. Ensuite, DamageSilent pour chaque autre ver blessé. | 0, 0, 1 puis 1, 1, 1 |
| 4, 6, 7 | ShortOnTime, StartTurn, Waiting, Airstrike, Missed, Targeted, Taunt*, Retreat, Boring, SkipGo, WeaponFired | – / tous sauf l'actif. WeaponFired apporte sa charge. | Boring 0, 1, 1 ; SkipGo 0, 1, 1 ; autres 1, 1, 1 |
| 5 | Mistake | [sujet] / tous sauf le sujet | 1, 1, 1 |
| 10 | Victory | équipe du gagnant / les autres | 1, 1, 0 |
| 11 | Bored | vers qui s'ennuient, sauf l'actif | 0, 0, 1 |
| 12 | DamageSilent (direct), Punch, WormBounce, FireDamage, Titter, Grenade* | rien n'est casté | – |

Options : A = pose la priorité et efface « s'ennuie », B = un ver en scène qui s'ennuie peut jouer une piste Idle, C = l'échec d'une piste Special rejette le film.

## Déclencheurs

| Déclencheur | Envoi W4M | Ici | Statut |
|---|---|---|---|
| TimedPayloadFive..One | La charge s'arrête (une fois). Le déclencheur dépend des secondes entières restantes : 0 → One, 3 → Four, 4 ou plus → Five. Source : 0x577181. | Obus à mèche posé ou arrêté. Mine armée : choix. | fait |
| CrateDrop | 0x5c4710 | À l'atterrissage de la caisse (supposé). Il faut un ver à moins de 5 m. | fait |
| Idle, Sick, Abducted, Bored | Toutes les 300 à 600 ms, par rotation (0x5b3534, table 0x9200dc). Exemples : Idle0/10, c'est l'actif près d'un ami ou d'un ennemi à 20 m ; Idle20/50/100, ce sont des duos et des duels Gunslinger ou FlickBogey ; Sick10, c'est un ver empoisonné. | Pareil. La condition de garde de 0x5b3280 n'est pas décodée : elle est ignorée. | fait |
| Thinking | Aucun envoi dans l'exe PC : aucun appel à 0x4d3410 avec 0x2f. | Jamais déclenché. Les films restent dans `acting.txt`. La particule `WXP_WormThinking` n'est donc jamais émise. | comme W4M (mort) |
| ItemReact | Aucun envoi dans l'exe PC (ni appel direct, ni table d'ambiance 0x9200dc). | Jamais déclenché. | comme W4M (mort) |
| StartTurn | Le ver devient actif (0x5a421e). | TurnStart | fait |
| WeaponFired, Airstrike, SkipGo | 0x585e3d, Bomber 0x54d7c0, 0x588066 | Événement Fire | fait |
| Targeted | 100 ms dans le champ de la caméra de visée (mode 1, à la première personne), puis 3000 ms de délai par ver (0x5a2600). | Visée à la première personne (`Controls::firstPerson`) | fait |
| Blasted | Impulsion avec vy > 0,1 unité/ms (§11, événement 13). | Ver blessé qui décolle à plus de 5 m/s | fait |
| BlastSplat | Atterrissage dur après un vol (0x5a3d4d) | Fin d'un vol Blasted | fait |
| FallSplat | Aucun envoi dans l'exe PC (pas d'appel à 0x4d3410 avec 6). | Jamais déclenché. | comme W4M (mort) |
| FastRecover | Aucun déclencheur ne porte ce nom : film mort | – | sans objet |
| Death | Début des convulsions de mort (0x5a3ffb). Les vers noyés n'y passent pas. | `g.dying()` hors noyade. Death15/20 « Sick » pour un ver empoisonné. | fait |
| Poisoned | 0x5a1a89 | Le poison augmente | fait |
| Mistake, FirstBlood, MaxDamage, DamageInflicted, Boring | Sur `GameLogic.ApplyDamage` (0x5b22a0), dans cet ordre : Mistake si dégâts aux amis > dégâts aux ennemis / 3 et le tireur s'est blessé ; FirstBlood pour la première mort de la partie ; MaxDamage si un ver est dans le cœur de l'explosion, 1,2·(portée − d)/portée ≥ 1 (0x5ae6b5) ; Boring si personne n'est touché et qu'aucune charge n'est partie ; DamageInflicted sinon. | Au passage en Settle | fait |
| Boring (NoDamageA/B) | Sa piste `Special` n'a aucun candidat (réserve spéciale vide), et les autres pistes visent la piste 0 vide. Le film est donc toujours rejeté. | Même logique : il ne joue jamais. | comme W4M (mort) |
| Missed | Personne n'est touché et une charge est partie (0x50fe0d) | Au passage en Settle | fait |
| ShortOnTime, Waiting | L'horloge affichée passe à 5 s, puis à 15 s (HudClockEntity 0x5f0191, 0x5f01db) | Pareil | fait. Waiting n'est plus « immobile 12 s » |
| Retreat | Minuterie de retraite (0x50f44b) | Début de la phase Retreat | fait |
| Collect | 0x5cb6a9 | Événement Collect | fait |
| Victory | Fin de partie | GameOver. Les perdants vivants jouent les pistes Foe0 (Sad, WhatWereYouThinking, SighAndShakeHead, Doh). | fait |
| Zap, Abducted | La sortie du rayon d'enlèvement pose le drapeau « enlevé » (0x547d39). Il est effacé en 0x5adcf0, voisin du soin du poison. | Fire de l'Alien Abduction : vers dans le rayon. Une caisse de soin l'efface (supposé). Zap au même moment (supposé). Teinte Abducted.Colour. | fait |
| Taunt*, Titter, Grenade*, WormBounce | Table 0x95f1a8 remplie à l'exécution, ou gestionnaire vide | – | non fait |

## Voix hors scène

| Voix | Source | Ici |
|---|---|---|
| Punch | MeleeWeaponLogicEntity 0x568270 : un Fire Punch (kMeleeFirepunch) dit « Punch » au lieu de « WeaponFired » | main.cpp onEvent Fire |
| Revenge | lsd et wiki seulement. W4M n'envoie jamais le déclencheur 0x1d et n'a pas de film Revenge. | Jamais dit. |
| EnemyDeath, CrateDrop, ShallowDrown | lsd, pas de film | Comme avant |
| FriendlyDeath, Victory | Dans les films | main.cpp onEvent les joue toujours (à l'explosion, à la fin de partie). Les films les taisent pour éviter un doublon. |

Une seule réplique joue à la fois, avec 0,9 s entre deux (choix). Nouvelles catégories importées (`tools/w4m-import` `VOICES`) : revenge, punch, damageb (DamageInflictedB), nodamagea, nodamageb, maxdamage, pointandlaugh.

## Particules (PARTTWK.XOM)

Les vitesses sont données en unités par image de 20 ms. Cette unité est supposée.

| Émetteur | Données | Ici |
|---|---|---|
| WXP_WormThinking | WXSprite22 « ? », origine (0, −4, 0) ± 4 au HatLocator, 1 par 950 ms, vie 1000 ms, taille 11 ± 3, vitesse (0, 0,3, 0) | Non émise : seuls les films Thinking la demandent, et ils ne sont jamais déclenchés. |
| WXP_Vomit_Fluid | WXSprite23, après 2000 ms, 3 gouttes, vie 600 ± 200 ms, taille 0,75, vitesse (0, 0,5, 3) ± (1, 0,2, 0,2), couleur (0,9, 0,7, 0,4) → (0,7, 0,75, 0,2) | Au VomitLocator, (0, −15, 2) unités sous HatLocator. La taille est ×4 pour rester visible (choix). |
| WXP_Sneeze | WXSprite23, après 100 ms, 8 gouttes, vie 400 ± 200 ms, taille 0,9, vitesse (0, −0,05, 0,5) | Pareil |

## Autres animations

- **Saut sur un rebord (Vaulting 0x5aca80).** W4M déplace le ver vers la cible de 4 unités par image, 250 ms au plus, sans collision, et joue le clip `Vault` (événement 9). Le sim monte d'un coup. Le rendu trace donc le ver en retard de ce décalage, qui se résorbe à 10 m/s, et joue `Vault`. Choix : seulement au rendu, pour garder la prédiction de l'IA (`walkStep`) et l'état du sim inchangés.
- **Parachute (WAE_Parachute 0x58f180, ParachuteLogicEntity 0x578b77).**
  - La voile joue `FireParachute` à l'ouverture.
  - Ensuite, la voile et le ver jouent `ParachuteLR` au temps 1 − lr.
  - lr = (3·lr + cible)/4 par image. La cible vaut ±0,75 selon le sens de la dérive latérale au-delà de 0,01 unité/ms ; ce sens n'est pas vérifié.
  - `ParachuteWobble`, ajouté par-dessus dans W4M, n'est pas superposé ici : un seul clip par modèle.
- **Objets sur forte pente (sim).** Au sol, au-delà de 60° (WXWorm.SlideAngle_Default), un objet suit la même loi que le ver qui glisse (`wormBody`) : gravité le long de la pente, puis friction SlideFriction 0,95 par image de 20 ms (0,9582 par tick). C'est déterministe. Test : `sim_check` (`checkTunnelling`, pentes de 70° et 30°).

## Limites

- Les yeux (`Eyes_LR/UD`) et les bras (GestureAt) ne sont pas faits.
- `SpawnAccessory` n'est pas fait : l'antenne `Particle.WXPMesh36` des films Abducted20c/30a, et `WXM_Telepath` (un clip d'objet).
- Clips nommés mais absents du fichier : PointAndWave, ShakeFist, CoverHead, WaveANdPoint, ANgry. Ils ne jouent rien, et W4M ne les trouve pas non plus (supposé : sensible à la casse).
- La garde du cycle d'ambiance (0x5b3280, [esp+0x30]) n'est pas décodée.
- Une piste `Interesting` n'est jamais castée (aucun objet marqué), donc Idle200a ne joue pas.
