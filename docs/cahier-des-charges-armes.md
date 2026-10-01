# Cahier des charges des armes, aligné sur Worms 4: Mayhem

Référence : les données du jeu (`Data/Tweak/WEAPTWK.XOM`, `TWEAK.XOM`, textes d'aide `English.xom`), recoupées avec le wiki Worms et les guides Steam. Quand les sources se contredisent, les données du jeu priment. Le détail, les sources et l'état d'implémentation sont dans [weapons-audit.md](weapons-audit.md).

Règles communes :
- Les dégâts indiqués sont des **maximums**, au centre de l'explosion. Ils diminuent avec la distance.
- Après une attaque, le joueur dispose du **temps de retraite** du style de jeu (3 s par défaut) pour se déplacer. Il ne peut plus attaquer.
- Le tour ne se termine qu'une fois tous les projectiles retombés et les explosions finies. Les PV se décomptent ensuite.

## 1. Balistique et tir direct

**Bazooka**
- Visée en 1re personne. Maintenir le tir pour charger la jauge de puissance.
- Sensible au vent. Explose à l'impact.
- Dégâts max : 50 PV.

**Missile à tête chercheuse (Homing Missile)**
- Choix de la cible au réticule, puis tir chargé en 1re personne.
- Insensible au vent.
- Vol balistique pendant 1,25 s, puis guidage vers la cible pendant 5 s. Il retombe ensuite en balistique.
- Dégâts max : 50 PV.

**Fusil de sniper**
- La lunette avec zoom ne s'affiche qu'en maintenant L.
- Trajectoire rectiligne, insensible au vent et à la gravité. Un seul tir.
- Dégâts : 40 PV, avec un fort recul.

**Fusil à pompe (Shotgun)**
- Visée libre en 1re personne. 2 tirs par tour, et on peut se déplacer entre les deux.
- Dégâts : 25 PV par tir.

**Flèche empoisonnée (Poison Arrow)**
- Visée à l'arc en 1re personne. Sensible au vent.
- Dégâts à l'impact : 10 PV.
- La cible est empoisonnée et perd 10 PV par tour, sans jamais descendre sous 1 PV.
- Le poison ne se transmet pas aux autres worms. Seule une caisse de santé le soigne.

## 2. Grenades et explosifs

**Grenade**
- Visée en 1re personne. Retardateur réglable de 1 à 5 s à la croix haut/bas, 3 s par défaut, mémorisé pour l'équipe.
- Rebondit, et explose exactement à la fin du décompte.
- Dégâts max : 55 PV.

**Grenade à fragmentation (Cluster Grenade)**
- Retardateur réglable de 1 à 5 s.
- À l'explosion, elle libère 5 fragments.
- Dégâts : 15 PV pour l'explosion, 15 PV par fragment.

**Sainte Grenade (Holy Hand Grenade)**
- Visée en 1re personne. Pas de retardateur.
- Rebondit très peu. Une fois arrêtée, elle joue le « Hallelujah » puis explose 2 s plus tard.
- Dégâts max : 80 PV, sur un grand rayon.

**Bombe banane (Banana Bomb)**
- Retardateur réglable de 1 à 5 s. Rebondit beaucoup.
- À l'explosion, elle libère 5 bananes secondaires.
- Dégâts : 50 PV pour l'explosion, 60 PV par banane.

**Dynamite**
- Aucun viseur : elle se pose aux pieds du worm.
- Mèche fixe de 7 s, sans compte à rebours affiché. Le joueur peut s'enfuir pendant ce temps (marcher, sauter), mais plus attaquer.
- Le tour se termine à l'explosion.
- Dégâts max : 75 PV.

**Bonbonne de gaz (Gas Canister)**
- Retardateur **fixe** de 5 s (non réglable).
- L'explosion ne fait aucun dégât direct, mais libère un nuage de 5 m de rayon qui dure 8 s et dérive avec le vent. Tout worm qui y passe est empoisonné (10 PV par tour).

**Mine**
- Posée au sol, elle s'arme puis explose quand un worm approche, après un délai de 1 à 5 s.
- 10 % des mines ne marchent pas : à la fin du délai, elles font un petit nuage de fumée et restent inertes pour de bon.
- Dégâts max : 40 PV.

> **Retiré du cahier :** la bombe collante (Sticky Bomb) n'existe pas dans W4M. Elle vient d'autres épisodes.

## 3. Corps à corps

Pour chaque coup ci-dessous : contact direct, face à la cible.

**Pichenette (Prod)**
- 5 PV de dégâts. Pousse légèrement la cible.

**Batte de baseball**
- 20 PV de dégâts.
- Projette la cible dans la direction visée, à grande vitesse.

**Coup de poing de feu (Fire Punch)**
- 16 PV de dégâts.
- Le worm bondit à la verticale, poing en flammes, en creusant le terrain au-dessus de lui.
- La cible est projetée en l'air.

**Clou de queue (Tail Nail)**
- 15 PV de dégâts. Enfonce la cible dans le sol.
- La cible clouée ne peut plus marcher ni sauter, ni utiliser d'utilitaire, d'animal ou d'arme de mêlée. Elle peut tourner et tirer.
- Elle est libérée quand une explosion creuse le sol à ses pieds.

> **Retiré du cahier :** l'épée et le bouclier (Sword and Shield) n'existent pas dans W4M. La réduction de dégâts appartient à l'Armure (voir §5).

## 4. Frappes aériennes et spéciales

**Attaque aérienne (Air Strike)**
- Choix du point d'impact et du sens de passage de l'avion.
- Largue des missiles en ligne le long de cet axe. Les données en prévoient 6, les guides disent 5.
- Dégâts : 25 PV par missile.

**Bovine Blitz (Super Airstrike)**
- Choix du point de passage et du sens, puis l'avion est **piloté** au stick pendant 14 s.
- Chaque appui sur le tir largue une vache, 3 au plus, espacées d'au moins 0,8 s. Les vaches descendent lentement (parachute) et explosent à l'impact.
- Dégâts max : 80 PV par vache.

**Frappe Fatkins (Fatkins Strike)**
- Choix du point de chute.
- Un worm géant tombe et rebondit plusieurs fois. Chaque rebond fait un grand cratère.
- Dégâts max : 75 PV.

**Âne en béton (Concrete Donkey)**
- Choix du point de chute.
- Il tombe du ciel et pilonne le terrain à répétition, en descendant jusqu'à ce qu'il coule dans l'eau.
- Dégâts max : 80 PV par impact.

**Enlèvement extraterrestre (Alien Abduction)**
- Choix de la cible.
- Une soucoupe soulève le worm et lui retire la moitié de ses PV actuels.
- Il **n'est pas** téléporté ailleurs.

**Mouton**
- Lancé au sol, il marche droit devant lui en sautant les obstacles.
- Le tir le fait exploser, sinon il explose seul.
- Dégâts max : 75 PV.

**Super Mouton (Super Sheep)**
- Lancé au sol, il marche. Un appui sur le tir le transforme en mouton volant, piloté au stick en 3e personne. S'il n'a pas décollé au bout de 5 s, il explose.
- Un nouvel appui, ou un contact avec le terrain ou un worm, le fait exploser. Sinon il explose seul au bout de 25 s de vol.
- Dégâts max : 75 PV.

**Vieille dame (Old Woman)**
- Elle marche. Elle se dirige au stick et explose sur commande avec le tir.
- Chaque worm ennemi qu'elle bouscule perd de 1 à 8 munitions d'une arme tirée au hasard dans le stock de son équipe. L'équipe du lanceur les récupère.
- Dégâts max : 75 PV.

**Scouser gonflable (Inflatable Scouser)**
- Il marche et se dirige au stick. Il avale le premier worm qu'il touche, se gonfle et l'emporte dans les airs au gré du vent.
- Au bout de 5 s, il éclate : 40 PV pour le worm avalé, qui tombe. S'il n'a rien avalé, il éclate sans dégâts.

**Inondation (Flood)**
- Le niveau de l'eau monte d'environ 2,15 m.

**Tourelle (Sentry Gun)**
- Posée au sol, elle reste active pendant les tours adverses.
- Elle tire une rafale sur un worm ennemi visible à moins de 15 m.
- Dégâts : 25 PV par rafale. Recharge : 10 s.

## 5. Utilitaires et déplacement

**Jetpack**
- Le carburant ne s'use que pendant la poussée. Les données donnent 7 500 unités, qui ne correspondent pas aux « 30 » du cahier d'origine.
- Ne termine pas le tour. En vol, on peut sélectionner et utiliser une arme : lâcher une dynamite, une grenade, tirer au bazooka…
- Après l'attaque, le temps de retraite s'enclenche (3 s par défaut). On peut encore voler avec le carburant restant.

**Corde ninja (grappin)**
- Viseur en 1re personne pour ancrer le grappin. On se balance, et on règle la longueur jusqu'à 22,5 m.
- 5 lancers de grappin par tour.
- Le grappin accroche aussi les caisses, les mines et les barils, et les tire vers le worm.
- On peut utiliser une arme en pleine corde.
- Ne termine pas le tour.

**Parachute**
- Ouverture manuelle en chute. S'il est sélectionné, il s'ouvre tout seul lors d'une chute dangereuse.
- Dérive avec le vent. Il se referme en touchant le sol.

**Poutre (Girder)**
- Aperçu 3D semi-transparent. On choisit l'orientation, puis on pose une poutre d'acier indestructible.
- Elle termine le tour, sauf si le style de jeu l'interdit (option « les poutres ne terminent pas le tour »).

**Sélection de worm, Passer son tour, Abandon**
- Comme dans le jeu.

**Armure (Armour)**
- Le worm ne subit plus que 25 % des dégâts des explosions et des balles, et 40 % du recul, jusqu'à la fin de la partie.
- Le poison, les chutes, la mêlée et le Scouser ne sont pas réduits. Ne termine pas le tour.

**Ultimate Mayhem uniquement** : kit de pont, jumelles, Bubble Trouble, potion d'Icare, Buffalo of Lies.

> **Retiré du cahier :** le téléporteur n'est pas un utilitaire du joueur dans W4M ; le jeu a des télépads fixes sur certaines cartes. Il est gardé chez nous comme bonus, mais n'est pas proposé par défaut.
