# Objectifs — GameExemple : fret spatial

**Le jeu** : on est à pied, en vue FPS, dans un entrepôt sur Terre. On attrape
des marchandises, on les range dans la soute d'une fusée, on fait le plein, on
monte l'échelle. La vue bascule en caméra orbitale autour de la fusée. On
décolle, on transfère vers la Lune, et il faut se poser en douceur sur la
piste. Trois astres en tout : la Terre, la Lune, et une petite lune.

Le cœur du jeu est le même que celui d'Euro Truck : **on sent la charge**. La
masse embarquée et sa position changent le delta-v disponible, le couple au
décollage et la difficulté de l'atterrissage. Tout le reste est du décor autour
de ça.

**« Physique réaliste » veut dire : les bonnes équations, pas les bonnes
distances.** Gravité newtonienne, orbites, transferts, atterrissage propulsif —
tout est simulé pour de vrai. Mais le système est compressé (§1.2) : à l'échelle
réelle un transfert Terre-Lune dure trois jours, et le `float` 32 bits du moteur
n'a plus qu'une résolution de 23 m à cette distance.

---

## 0. Inventaire

**Déjà là** : Jolt via `PhysicsWorld`, `RigidBody_C` avec formes multiples et
`gravityFactor_` par corps, pas de temps fixe (`Game::fixedUpdate`), raycast
scène (`SpatialIndex::raycast` → `entt::entity`), `DebugDraw` (lignes, sphères,
boîtes), caméras multiples via `Camera_C::active_`, `Time::scale_`, hot reload
du jeu.

**Manque, côté moteur** — détaillé dans les sections, récapitulé en §8 :

1. appliquer forces et impulsions depuis le jeu sans toucher Jolt à la main ;
2. capteurs (volumes déclencheurs) et événements de contact ;
3. origine flottante ;
4. contrôleur de personnage ;
5. HUD depuis la DLL du jeu (`Batap_Engine` est une lib **statique**, donc la
   DLL a sa propre copie du contexte ImGui — appeler ImGui depuis le jeu
   dessinerait dans un contexte mort).

---

## 1. Le socle : gravité et échelle

Rien ne peut être testé avant ces deux points, et ils conditionnent tout le
reste.

### 1.1 Gravité vers les astres

Jolt applique une gravité globale constante, inutilisable ici. Tous les corps
dynamiques passent à `gravityFactor_ = 0` et le jeu applique la sienne.

**Acté : somme des forces des trois astres, pas de sphères d'influence.** Les
coniques raccordées de KSP existent pour prédire analytiquement une
trajectoire ; ici on intègre numériquement de toute façon, et trois corps par
objet dynamique ne coûtent rien. C'est plus réaliste *et* plus court à écrire.

- [ ] **1.** `CelestialBody_C` — `mu_` (soit G·M directement, pour ne pas
      traîner de très grands nombres) et rayon. L'astre est un corps Jolt
      statique de forme `Sphere` : analytique, exacte, aucune tessellation même
      à 100 km de rayon.
- [ ] **2.** `Gravity_S` dans `fixedUpdate` — pour chaque corps dynamique,
      `F = Σ mu_i · m · d_i / |d_i|³`, appliquée via l'API de §8.1. À valider :
      un corps lâché à la bonne vitesse reste en orbite circulaire sur
      plusieurs tours.
- [ ] **3. Les astres sont sur rails.** Leurs orbites ne sont **pas** simulées :
      une orbite intégrée dérive, et voir la Lune s'échapper au bout de vingt
      minutes n'apporte rien. Position = fonction analytique du temps (cercle,
      angle = ω·t), corps `Kinematic`.

### 1.2 Échelle

Chiffres de départ, à régler au jeu :

| | rayon | g surface | orbite |
|---|---|---|---|
| Terre | 100 km | 9.81 | — |
| Lune | 30 km | 1.6 | 600 km |
| Petite lune | 10 km | 0.3 | 250 km |

À 100 km de rayon, la vitesse orbitale rasante `sqrt(mu/R)` vaut ≈ 990 m/s —
des nombres lisibles sur un HUD. Critère de réglage des distances : **un
transfert doit durer 2 à 5 minutes** avec une accélération du temps modérée
(§6.4).

### 1.3 Origine flottante — **(moteur)**

Le vrai risque technique du projet. Le `float` a une résolution relative de
6e-8 : à 600 km de l'origine, le pas de position vaut 4 cm, et la fusée tremble
sur sa piste.

- [ ] **1.** Quand la fusée dépasse ~5 km de l'origine, translater toute la
      scène : `Transform_C` des racines et positions des corps Jolt. Quelques
      centaines d'objets, c'est une boucle triviale.
- [ ] **2.** Conséquence à ne pas manquer : **plus aucune position monde ne peut
      être mise en cache d'une frame à l'autre.** Tout ce qui garde un `v3f`
      absolu (cible de navigation, point d'amarrage) doit être recalé au
      rebasage, ou stocké relativement à une entité.

---

## 2. La fusée

- [ ] **1.** `Rocket_C` — poussée max, débit de carburant, carburant courant,
      masse à vide. `RigidBody_C` avec les formes du fuselage et des pattes.
- [ ] **2.** `Rocket_S` dans `fixedUpdate` — poussée le long de l'axe local,
      appliquée **au point du moteur** et pas au centre de masse : c'est ce qui
      fait qu'une charge décentrée donne un couple. Consommation
      `= poussée · dt · k`, et la masse du corps baisse au fur et à mesure.
- [ ] **3. RCS et assistance de rotation.** Piloter en 3D sans frottement est
      désagréable sans aide : par défaut, lâcher les commandes amortit la
      rotation jusqu'à l'arrêt. Désactivable. Ce n'est pas de la triche, c'est
      ce que fait le vrai pilote automatique.
- [ ] **4.** Panne sèche = plus de poussée. Pas de game over, juste une
      trajectoire qu'on ne contrôle plus.

---

## 3. Le personnage à pied — **(moteur)**

- [ ] **1.** `CharacterVirtual` de Jolt, déjà dans la lib. Un `Character_C`
      (vitesse, hauteur, rayon) et le système qui le pousse.
- [ ] **2.** Son vecteur *up* doit être recalculé chaque frame vers le centre de
      l'astre le plus proche, sinon marcher sur une sphère ne marche pas.
- [ ] **3.** Caméra FPS attachée à la tête, souris = orientation. Distincte de
      `FreeCamController_S`, qui vole et ne collisionne pas.

---

## 4. Marchandises et soute

- [ ] **1.** `Carryable_C` sur les caisses. `SpatialIndex::raycast` depuis la
      caméra, touche *E* pour attraper.
- [ ] **2.** Objet porté → `Kinematic`, tenu devant la caméra. Relâché →
      `Dynamic`, avec la vitesse du personnage. Deux lignes, et ça suffit à ce
      que porter une caisse soit satisfaisant.
- [ ] **3.** `CargoBay_C` sur la fusée : un volume. Une caisse lâchée dedans est
      *rangée*.
- [ ] **4. Une caisse rangée devient enfant `Kinematic` de la fusée et sa masse
      s'ajoute à celle du corps.** Le piège sinon : des caisses libres dans une
      soute qui accélère à 3 g rebondissent en permanence, le solveur souffre et
      le vol devient du bruit. On garde quand même ce qu'on cherchait — la masse
      totale et le centre de masse changent vraiment.
- [ ] **5.** Recalculer le centre de masse du corps à chaque chargement et
      déchargement. C'est ce qui rend une soute mal équilibrée punitive.

---

## 5. Le sol : entrepôt, pompe, échelle, bascule de vue

- [ ] **1.** Scène `scenes/Spatial/` — entrepôt statique, caisses, pompe, fusée
      sur son pas de tir, échelle.
- [ ] **2.** Ravitaillement : à portée de la pompe, une touche remplit le
      réservoir progressivement. Le carburant est une masse — faire le plein
      alourdit.
- [ ] **3.** L'échelle : un volume de montée, le personnage y monte à vitesse
      constante en ignorant la gravité.
- [ ] **4.** Entrer dans la fusée → personnage désactivé, `Camera_C::active_`
      bascule sur la caméra orbitale. L'inverse à l'arrivée.
- [ ] **5.** Détection de proximité : **en v0 une simple distance suffit**
      (pompe, échelle, cockpit). Les vrais capteurs (§8.2) ne deviennent
      nécessaires que pour la soute et la piste.

---

## 6. Le vol

- [ ] **1.** `OrbitCamera_C` et son système — caméra qui tourne autour de la
      fusée à la souris, distance à la molette. Proche de
      `FreeCamController_S`, mais contrainte à une cible.
- [ ] **2. Trajectoire prédite** — sans elle on pilote à l'aveugle et ce n'est
      pas jouable. Ré-intégrer la position de la fusée sur N pas en avant avec
      la même gravité, moteurs éteints, et tracer la polyligne avec
      `DebugDraw`. Quelques centaines de pas, gratuit. C'est le vrai gameplay :
      on manœuvre en regardant la courbe changer.
- [ ] **3. HUD** — vitesse relative à l'astre visé, altitude sol, carburant,
      angle par rapport à la verticale locale. Voir §8.5 pour le problème de
      contexte ImGui.
- [ ] **4. Accélération du temps** — un transfert dure des minutes.
      `Time::scale_` existe, mais multiplier le pas fixe casse l'intégration.
      Donc : ×10 au maximum, et seulement **loin de toute surface** ; retour
      imposé à ×1 en approche.

---

## 7. L'atterrissage

C'est le moment du jeu, l'équivalent de la marche arrière au quai dans Euro
Truck. Tout le reste est calme, ici on retient son souffle.

- [ ] **1.** `LandingPad_C` : la piste et son rayon d'acceptation.
- [ ] **2.** Conditions de réussite, évaluées au contact : vitesse verticale
      sous un seuil, vitesse horizontale sous un seuil, angle par rapport à la
      verticale locale sous un seuil, et centre de la fusée sur la piste. Les
      seuils se durcissent avec la charge embarquée — c'est là que la masse
      transportée se paie.
- [ ] **3.** Échec = on rebondit, on verse. Pas d'écran de défaite : la fusée
      couchée sur le flanc dit tout.
- [ ] **4.** *Plus tard* — pattes cassables, réservoir qui fuit. Pas en v0.

---

## 8. Briques moteur à ajouter

Elles vivent dans `src/Engine`, pas dans `GameExemple`, et resservent à
n'importe quel jeu.

- [ ] **1. Forces et impulsions depuis le gameplay.** Aujourd'hui il faut passer
      par `world.physics().bodies()` et `rb.bodyId_` à la main. Une poignée de
      fonctions sur `EntityHandle` (`addForce`, `addTorque`, `addImpulse`,
      `velocity`, `setVelocity`) — même logique de façade que `batap.h`.
- [ ] **2. Capteurs et contacts.** Un `bool sensor_` dans `RigidBody_C` (Jolt :
      `IsSensor`), plus un `ContactListener` qui remplit une liste d'événements
      consommable dans `fixedUpdate`. Nécessaire pour la soute et la piste ; le
      reste s'en passe avec des distances.
- [ ] **3. Origine flottante** (§1.3).
- [ ] **4. Contrôleur de personnage** (§3).
- [ ] **5. HUD depuis la DLL du jeu.** `Batap_Engine` est une lib **statique** :
      la DLL du jeu en a sa propre copie, donc son propre contexte ImGui global,
      et un `ImGui::Begin` depuis `MyGame` ne dessinerait rien. Deux sorties —
      transmettre le contexte de l'hôte (`ImGui::SetCurrentContext` au
      chargement de la DLL, dans `GameModule`), ou passer le moteur en lib
      partagée. La première est locale et suffit ; à trancher au moment du HUD,
      pas avant.

---

## Ordre

**v0 — le seul qui compte au départ.** Une sphère (la Terre), une fusée, de la
gravité vers le centre, de la poussée, l'amortissement de rotation, la caméra
orbitale. Décoller, tourner, revenir se poser. **Pas de personnage, pas de
cargaison, pas de Lune, pas d'entrepôt.** Si piloter la fusée est bon, le jeu
existe ; sinon, rien de ce qui suit ne le sauvera.

Il faut pour ça : §8.1, §1.1, §1.2, §2.1-3, §6.1.

**v1 — la charge.** La soute, les caisses, le centre de masse, le carburant.
Toujours sans personnage : on charge en cliquant sur les caisses depuis la vue
orbitale. C'est le cœur du jeu, et il est testable avant d'avoir un FPS.

**v2 — le voyage.** La Lune, la trajectoire prédite, le HUD, l'accélération du
temps, la piste et ses conditions. Et l'origine flottante, qui devient
obligatoire dès qu'on quitte la Terre.

**v3 — le FPS.** Le personnage, l'entrepôt, la pompe, l'échelle, la bascule de
vue. C'est ce qui donne le ton du jeu, mais c'est de l'habillage autour d'une
boucle qui doit déjà tourner.

---

## Pièges

- **Précision `float`** — §1.3. Ne pas le repousser « à plus tard » sans le
  savoir : tout code qui cache une position monde devra être repris.
- **Gravité globale de Jolt** — `gravityFactor_ = 0` sur chaque corps dynamique,
  sinon deux gravités s'additionnent.
- **Composants du jeu** — un nouveau `Foo_C` doit être inclus depuis
  `GameComponents.h` pour que la DLL l'enregistre dans son registre.
- **Zéro état statique dans la DLL** — la règle du hot reload vaut ici aussi :
  l'état du jeu (phase courante, fusée pilotée) vit dans des composants, pas
  dans des globales.
- **Le pas fixe est l'ami de la physique** — toute la logique de vol dans
  `fixedUpdate`, jamais dans `update`.
