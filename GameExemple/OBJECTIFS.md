# Objectifs — Livreur de l'espace

**Ce fichier dit quoi construire et dans quel ordre.**
[JEU.md](JEU.md) dit ce qu'est le jeu — le but, les mécaniques, la direction
artistique, les questions ouvertes. Rien de narratif ici, rien de technique
là-bas.

Ce qu'il faut en retenir pour lire la suite : un tout petit système solaire
(une étoile, une planète nue, puis la nôtre avec ses deux lunes), on habite la
grande lune dans un garage, on livre des colis, et **la progression est en
delta-v** — ce qui bloque n'est jamais une porte mais un manque de carburant.

**La physique doit sembler réaliste, pas l'être.** Les *lois* sont les vraies —
gravité newtonienne en 1/r², orbites, transferts, atterrissage propulsif, tout
est intégré pour de bon et rien n'est scripté. Les *constantes* sont choisies
pour le jeu. Personne ne peut voir qu'une constante est fausse ; tout le monde
sent qu'une trajectoire est vraie.

---

## 0. État

**Fait et mesuré** : gravité à N corps et astres qui s'attirent (§1.1), physique
de la fusée — poussée au point moteur, masse qui baisse, RCS, assistance (§2),
personnage qui marche sur une sphère et caméra FPS (§3), port d'objets au
ressort (§4). Côté moteur, les quatre briques de §8 : forces et impulsions sur
`EntityHandle`, capteurs et événements de contact, contrôleur de personnage,
ImGui depuis la DLL du jeu.

**Ce que le nouveau découpage change**, et rien de plus :

1. **L'échelle** (§1.2) — cinq corps au lieu de trois, et le critère de réglage
   n'est plus « un transfert dure une minute » mais « le buggy suffit chez soi
   et nulle part ailleurs ».
2. **La fusée devient un assemblage** (§2.5) — la physique déjà validée ne
   change pas, elle lit ses nombres des pièces au lieu de champs saisis.
3. **Le garage vit sur une lune qui orbite** (§5.1) — et ça, c'est le seul vrai
   blocage moteur que la nouvelle boucle introduit.

Sont **abandonnés** de l'ancien plan : l'entrepôt sur Terre, l'échelle à monter,
la pompe à essence comme étape séparée. Ils reviennent fondus dans le garage.

---

## 1. Le socle : gravité et échelle

### 1.1 Gravité vers les astres

Jolt applique une gravité globale constante, inutilisable ici : elle est mise
à zéro une fois pour toutes (`world.setGravity`) et le jeu applique la sienne.

**Acté : somme des forces de tous les astres, pas de sphères d'influence.** Les
coniques raccordées de KSP existent pour prédire analytiquement une
trajectoire ; ici on intègre numériquement de toute façon, et cinq corps par
objet dynamique ne coûtent rien. C'est plus juste *et* plus court à écrire.

- [x] **1.** `CelestialBody_C` + `CelestialBody_S` — fait. Le composant porte
      `mu_` (G·M directement, pour ne pas traîner de très grands nombres) et
      `radius_` ; le système **dérive le collider** : une sphère Jolt de
      `radius_`, analytique et exacte, aucune tessellation à faire. Le relief et
      le garage seront des meshes statiques posés dessus.
      **L'astre ne peut pas être `Dynamic`** : chaque corps qu'il attire lui
      repousse dessus par le contact, et un astre dynamique serait baladé par ce
      qui se pose sur lui. Le système le rétrograde donc ; `Kinematic` est le mode
      des astres qui orbitent (point 3), `Static` celui de l'ancre. Dériver le collider plutôt que de
      le saisir à la main rend l'erreur impossible et donne une seule source au
      rayon. Vérifié : astre créé `Dynamic` → rendu `Static`, déplacement
      0.000000 m avec une caisse posée dessus sur 400 ticks.
- [x] **2.** `Gravity_S` — fait, `F = Σ mu_i · m · d_i / |d_i|³` sur chaque
      corps dynamique, par la façade de §8.1. **La gravité globale de Jolt est
      mise à zéro** (`world.setGravity`) au lieu de `gravityFactor_ = 0` par
      corps : un réglage qui ne peut pas s'oublier sur une entité.
      Validé à la mesure, orbite circulaire à r=300 avec les constantes de
      §1.2 : le rayon oscille entre 299.491 et 300.514 (±0.17 %, l'ondulation
      du pas fixe) et la dérive après cinq tours vaut +0.0016 % **sans
      croître** — l'erreur est bornée, l'orbite ne se dégrade pas.
- [x] **3. Les astres s'attirent entre eux — pas de rails.** Fait. Le plan
      disait « position analytique, sinon ça dérive » ; la mesure a démenti la
      crainte, une orbite intégrée au pas fixe tient à ±0.02 % sans dériver.
      `CelestialBody_S` intègre donc la gravité mutuelle des astres et écrit
      leur transformée.
      - **`Kinematic` est ce qui rend ça possible** : Jolt ne laisse jamais un
        contact déplacer un corps cinématique, donc l'astre orbite pour de vrai
        sans que ce qui se pose dessus le bouscule. `Static` = astre épinglé
        (l'ancre du système), `Dynamic` refusé.
      - **`circularize_`** est une action d'inspecteur : elle remplit
        `velocity_` pour une orbite circulaire autour de l'astre qui tire le
        plus fort, puis se rabaisse. Éviter de calculer `sqrt(mu/r)` à la main
        pour chaque lune.
      - Mesuré : Lune à 1200 m d'une Terre épinglée, r = 1200 ± 0.26 m sur
        trois orbites sans dérive, et une caisse posée dessus reste à distance
        constante à 3 décimales pendant un tour complet — elle voyage avec la
        Lune au lieu d'être laissée sur place.
      - **Un pas de retard, par construction** : `MoveKinematic` fait parcourir
        le pas au corps, donc son collider suit sa transformée d'un pas
        (0.51 m à 30 m/s). C'est ce décalage qui emporte ce qui est posé
        dessus ; comparer une position à la transformée de l'astre plutôt qu'à
        son collider donne cet écart-là.
      - **Conséquence devenue bloquante** : un astre qui bouge emporte les corps
        **dynamiques** posés dessus, pas les meshes **statiques**. Comme on
        habite désormais une lune qui orbite, l'esquive d'avant (épingler l'astre
        habité) ne tient plus. C'est §5.1.

### 1.2 Échelle

**Le critère a changé.** Avant : « un transfert dure une minute ». Maintenant :
**le buggy doit suffire chez soi et échouer partout ailleurs.** C'est la vitesse
de libération de chaque corps qui dessine la progression, pas un tableau de
niveaux.

Valeurs de départ, à régler au ressenti :

| corps | rayon | g surface | mu = g·R² | orbite |
|---|---|---|---|---|
| Étoile | 400 m | — | 110 000 000 | centre |
| Planète A | 500 m | 10 | 2 500 000 | 6 000 m |
| **Notre planète** (B) | 600 m | 12 | 4 320 000 | 10 000 m |
| **Ta lune** | 300 m | 3 | 270 000 | 2 500 m autour de B |
| Petite lune | 100 m | 1.5 | 15 000 | 4 000 m autour de B |

L'échelle de difficulté qui en découle, en vitesse de libération :

- **ta lune : 42 m/s.** Hors de portée d'un buggy, atteignable par la première
  mini-fusée. C'est la barrière qui t'enferme chez toi au début.
- **petite lune : 17 m/s**, plus quelques dizaines de m/s pour changer d'orbite
  autour de B. La première destination « loin ».
- **planète B : 120 m/s.** Y descendre et en repartir demande une vraie fusée —
  c'est le deuxième palier.
- **planète A : transfert de Hohmann de 3 min 34** depuis B, plus les 141 m/s de
  sa propre libération. Le dernier palier.

Et les temps de trajet : ta lune fait le tour de B en 6 min 20, la petite lune
en 12 min 40, B fait le tour de l'étoile en 10 min, A en 4 min 40. **Les
destinations bougent pendant qu'on vole** — c'est ce qui crée les fenêtres de
tir, et c'est le principal bouton à tourner si ça paraît absurde à l'œil.

Pour le buggy : ta lune fait 1 885 m de tour, soit **95 secondes de conduite** à
20 m/s. Une livraison locale est un trajet, pas une formalité.

Les formules pour retoucher tout ça sans rien casser :

```
g = mu / R²             vitesse orbitale  v = sqrt(mu / r)
libération sqrt(2mu/r)  période           2π·sqrt(r³ / mu)
transfert de Hohmann    t = π·sqrt(a³/mu), a = (r1+r2)/2
```

**Rappel du piège d'échelle** : monter un rayon sans monter la gravité rallonge
les trajets en `sqrt(R/g)`, et au-delà de quelques minutes ça ramène
l'accélération du temps et l'origine flottante, deux systèmes dont §1.3 et §6.4
nous ont débarrassés.

### 1.3 Origine flottante — pas nécessaire, et c'est une conséquence de §1.2

Noté ici pour ne pas y revenir : à l'échelle réelle, le `float` (résolution
relative 6e-8) donne un pas de position de 23 m à la distance Terre-Lune, et
tout le projet aurait eu besoin d'une origine flottante. **À 1500 m de
l'origine, le pas vaut un dixième de millimètre.** Rien à faire, aucune brique
moteur à écrire, et les positions monde peuvent être mises en cache librement.

C'est le second gros dividende du modèle réduit, après la disparition de
l'accélération du temps (§6.4).

---

## 2. La fusée en pièces

**Acquis, et inchangé par le passage aux pièces** — toute la physique de vol
est faite et mesurée :

- [x] **1-4. `Rocket_C` + `Rocket_S`** — faits. Le composant porte la poussée
      max, le débit, le carburant, la masse à vide, la position locale du
      moteur, le couple RCS et l'assistance ; le jeu y écrit `throttle_` et
      `rcsInput_` à chaque tick.
      - **La poussée s'applique au point du moteur**, pas au centre de masse :
        c'est ce qui fera qu'une soute mal chargée donne un couple. Vérifié
        dans les deux sens — moteur dans l'axe, ω reste à 0.00000 ; décalé
        d'un mètre, α = couple/I au chiffre près.
      - **La masse baisse avec le carburant** via `EntityHandle::setMass`, qui
        écrit directement dans le corps Jolt. Passer par `registry.patch`
        lèverait `dirty_` et ferait reconstruire la forme à chaque tick.
        Validé par l'équation de Tsiolkovsky : `Δv = (F/ṁ)·ln(m₀/m₁)` donne
        12.048 m/s pour une seconde de poussée, mesuré 11.847.
      - **Panne sèche** : à carburant nul la poussée tombe, mesuré — vitesse
        figée sur 60 ticks manette à fond.
      - **L'assistance de rotation tire sur les mêmes propulseurs** : le couple
        correcteur est borné par `rcsTorque_`, donc elle ne peut pas arrêter la
        fusée plus vite que le pilote. C'est une décroissance exponentielle de
        constante `τ = I / assistGain_` — mesuré 1.417 s, et ω tombe de 1.026
        à 0.058 en 4.1 s.
      - **Piège de mesure, pas de code** : `Game::fixedUpdate` passe *avant*
        `Physics_S`, donc tout ce qu'on lit dans le jeu au tick N reflète le
        pas N-1. Un rapport instantané paraît toujours en retard d'un tick.

### 2.5 L'assemblage

**Le joueur choisit quoi et combien, jamais où.** Un vaisseau est donc une
**liste de modules** et rien d'autre ; l'ordre, les positions, les formes, la
masse et les capacités en sont dérivés. Une seule source de vérité, et l'éditeur
(plus tard) n'aura qu'à éditer cette liste.

**La fusée assemblée est UN corps rigide**, pas des corps reliés par des
contraintes : c'est de là que KSP tire son fléchissement et son coût en solveur.
`RigidBody_C` accepte déjà plusieurs formes — un module y dépose la ou les
siennes, et Jolt en tire le centre de masse seul.

**L'ordre de la pile est imposé**, de bas en haut :

```
moteur  →  structure  →  réservoirs  →  soute  →  cockpit
```

Les pattes sont sur la structure, donc en bas près du sol. Les réservoirs sous
la soute descendent le centre de masse. Le cockpit conique termine la pile.

**Règles de validité** : exactement un cockpit, exactement un moteur, autant de
réservoirs, soutes et structures qu'on veut. Le **cockpit fixe la classe** et
tous les modules suivent la sienne — jamais de mélange, jamais d'adaptateur.

- [ ] **1. La table des modules.** Un `ModuleType` (moteur, réservoir de gaz,
      réservoir d'oxygène, soute, structure, cockpit) × deux classes, et pour
      chacun : hauteur, diamètre, masse à vide, et sa contribution — poussée et
      débit pour le moteur, capacité pour les réservoirs, envergure des pattes
      pour la structure. En dur dans le jeu pour commencer ; ça migrera vers de
      la donnée quand le réglage deviendra pénible.
- [ ] **2. `RocketDesign_C`** — la liste des modules, plus la classe. Composant
      froid (il ne change qu'au garage), donc un `std::vector` y est admis comme
      dans `RigidBody_C`. C'est la seule chose que l'éditeur écrira.
- [ ] **3. `RocketBuild_S`** — reconstruit tout quand la liste change : trie les
      modules dans l'ordre canonique, empile les hauteurs pour obtenir chaque
      position locale, remplit `RigidBody_C::shapes_` et pose une entité enfant
      par module pour le mesh. **À valider sans une seule image** : une liste
      donnée produit la bonne hauteur totale, les bons offsets, et un centre de
      masse à la hauteur calculée à la main.
- [ ] **4. Agrégation dans `Rocket_C`** — poussée max, débit, capacité de gaz,
      capacité d'oxygène, masse à vide, point moteur. **`Rocket_S` ne change
      pas** : il lit les mêmes champs, ils sont calculés au lieu d'être saisis.
      À valider : un vaisseau assemblé équivalent à la fusée d'essai de §2 vole
      pareil, même Δv de Tsiolkovsky.
- [ ] **5. Les meshes**, une fois 1-4 mesurés. Puis l'éditeur, qui n'est qu'une
      UI par-dessus la liste de §2.5.2.
- [ ] **6. Casse** — un module détruit sort de la liste, `RocketBuild_S`
      reconstruit, la masse et le centre de masse suivent d'eux-mêmes. Voir §7.

**Deux pièges repérés d'avance :**

- **La soute est creuse, et une forme convexe ne l'est pas.** Elle contribue
      donc plusieurs formes — un plancher et des parois — pas une. C'est pour ça
      qu'un module doit pouvoir en déposer plusieurs.
- **Reconstruire coûte une forme Jolt neuve.** Ne le faire que sur changement
      réel de la liste, jamais par tick : c'est le même piège que `setMass` de
      §8.1, qui existe précisément pour éviter de passer par `patch`.

---

## 3. Le personnage à pied

- [x] **1.** `Character_C` + `Character_S` sur le `CharacterVirtual` de Jolt —
      c'est §8.3, avec ses quatre pièges.
- [x] **2. Le haut local vient de la gravité.** `Gravity_S` écrit
      `Character_C::gravity_` avec la même somme d'attractions que pour les
      corps rigides ; `Character_S` en déduit le vecteur *up*. Le personnage
      n'a donc aucune notion d'astre, et marcher sur une sphère ne demande pas
      une ligne de plus. Mesuré sur le **flanc** d'une planete de 250 m, là où
      le haut local est +X et pas +Y : debout à r = 250.0000, et après cinq
      secondes de marche tangentielle à 4 m/s, toujours r = 250.0000 pour un
      arc de 4.58° — exactement 20 m / 250 m. Aucune perte d'altitude ni de
      contact.
- [x] **3. Caméra FPS** — `FpsController_C` + `FpsController_S`. La souris
      oriente, WASD remplit `moveVelocity_`, espace lève `wantJump_`.
      - **Le cap est un vecteur du plan du sol, pas un angle de lacet.**
        Marcher autour d'une sphère fait tourner ce plan sous les pieds ; un
        lacet mesuré depuis un axe monde ne survivrait pas au passage sur le
        flanc, un vecteur reprojeté à chaque frame si.
      - Le tangage reste un scalaire borné, tourné autour du *right* tangent,
        donc l'horizon ne roule jamais. Mesuré : `fwd·up` et `right·up` à
        0.000000.
      - **L'entrée est lue dans `update`, l'œil placé dans `lateUpdate`** : la
        première parce que `pressed()` est un état de frame que le pas fixe
        verrait deux fois ou pas du tout (§8.3), le second parce que l'œil suit
        les pieds et doit attendre qu'ils aient bougé.
      - La caméra est une **entité enfant** portant `Camera_C` ; le système la
        place en espace monde à `pieds + up · eyeHeight`, parce qu'un décalage
        local suivrait +Y et pas la verticale locale.

---

## 4. Marchandises et soute

**Acte : rien ne se range tout seul.** Les caisses restent des corps libres, y
compris en vol ; c'est au joueur de les caler. Et l'objet porté n'est pas
accroché à la main, il pend au bout d'un ressort.

- [x] **1. `Carryable_C` + `Grabber_C` + `Grab_S`** — faits. *E* attrape et
      relâche. Le tir part de la caméra et passe par
      **`World::raycastPhysics`**, ajouté pour l'occasion : il touche les
      *colliders*, là où `SpatialIndex::raycast` touche les bornes de rendu. Pour
      attraper un objet physique c'est le collider qui fait foi, et ça marche
      sur un corps sans mesh. L'entité se retrouve par le user data du corps
      Jolt, celui-là même qui sert aux événements de contact (§8.2).
- [x] **2. L'objet porté reste `Dynamic`**, tiré par un ressort amorti vers un
      point devant la caméra. Il continue donc à cogner les murs, il traîne, il
      balance.
      **Le ressort est en N/m, pas en accélération** : l'écart au point de
      maintien vaut `m·g / stiffness`, donc il grandit avec la masse au lieu
      d'être le même pour tout le monde. Mesuré : 0.1481 m à 10 kg et 0.5947 m
      à 40 kg, contre 0.1482 et 0.5947 prédits — quatre fois plus lourd pend
      quatre fois plus bas.
      **`maxForce_` borne la prise** : à 300 kg le poids (5400 N) dépasse les
      4000 N du bras et la caisse ne quitte pas le sol. C'est le réglage qui
      décide de ce qu'un homme seul peut charger.
- [x] **3-5. Plus de rangement automatique** — les anciens points 3 à 5
      (`CargoBay_C`, caisse rendue `Kinematic` enfant de la fusée, masse
      sommée, centre de masse recalculé) sont **abandonnés**. Les caisses se
      baladent dans la soute comme n'importe quel corps, et la fusée sent leur
      masse **par les contacts** : elles s'appuient sur la cloison arrière
      pendant la poussée, ce qui transmet leur poids sans qu'on ait à l'ajouter
      à la main. Le centre de masse déplacé tombe du même coup.
      **À surveiller** : c'est exactement ce que l'ancien point 4 voulait
      éviter — une pile de corps libres dans une soute qui accélère à 3 g est
      le cas difficile du solveur. Si ça vibre, les sorties sont des sangles
      (contraintes) ou de l'amortissement, pas un retour au rangement
      automatique : le gameplay recherché est justement d'avoir à bien caler
      son chargement.

---

## 5. Le garage

Le point de départ et le seul lieu « chaud » du jeu : on y construit, on y
répare, on y prend ses contrats, on en repart chargé.

- [ ] **1. Blocage moteur : le contenu de surface doit suivre son astre.**
      C'est la conséquence directe de §1.1.3, et la nouvelle boucle la rend
      incontournable — **on habite une lune qui orbite**. Un astre `Kinematic`
      emporte les corps **dynamiques** posés dessus, mais pas les meshes
      **statiques** : le garage serait laissé sur place au premier tour
      d'orbite. Épingler sa lune tuerait justement ce qu'on veut voir.
      **La sortie** : `Physics_S` place les corps `Static` et `Kinematic` depuis
      `tc.pos()`, qui est la position **locale**. Les faire suivre la transformée
      **monde** rend le parentage utilisable, et le garage devient un enfant de
      sa lune. Correctif court et ciblé, mais à faire avant toute scène jouable.
- [ ] **2. La scène** — construite à l'éditeur : garage, aire de pose, buggy,
      caisses, plateforme de construction.
- [ ] **3. Ravitaillement et réparation** — à portée du garage, contre argent.
      Une simple distance suffit, pas besoin de capteur.
- [ ] **4. Bascule de vue** — à pied / buggy / fusée. `Camera_C::active_` fait
      déjà le travail ; ce qui compte est de désactiver le contrôleur qu'on
      quitte.
- [ ] **5. Le tableau des contrats** — où l'on choisit sa livraison. Voir §10.

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
- [ ] **4. Accélération du temps — supprimée par §1.2.** Un transfert dure une
      minute, il n'y a donc rien à accélérer : le vol se joue en temps réel d'un
      bout à l'autre. C'est un système entier en moins (multiplier le pas fixe
      casse l'intégration, et il aurait fallu forcer le retour à ×1 en
      approche), et surtout un vol qu'on pilote au lieu de le regarder passer.

---

## 7. Se poser, et se rater

Le moment du jeu. Tout le reste est calme, ici on retient son souffle.

**Et ça doit rester rattrapable.** Le plaisir vient de la tension, pas de
l'échec. Les leviers, du plus efficace au moins :

1. **`g` de l'astre visé** (§1.2) — le vrai bouton de difficulté : il donne du
   temps de réaction et réduit la vitesse d'arrivée. Le reste n'est qu'un
   ajustement à côté.
2. **Poussée/masse confortable** (TWR ≈ 3 à vide) — on doit toujours pouvoir
   annuler sa descente, même tard.
3. **Carburant généreux.** Le jeu porte sur le pilotage, pas sur l'optimisation
   du delta-v.
4. **Aire d'arrivée large**, seuils clairement indulgents au départ. On les
   resserre quand c'est trop facile, jamais l'inverse.
5. **Amortissement de rotation actif** (§2.3).

- [ ] **1. `LandingPad_C`** — l'aire et son rayon d'acceptation.
- [ ] **2. Conditions** évaluées au contact : vitesse verticale, vitesse
      horizontale, angle à la verticale locale. La donnée qui compte est
      `ContactEvent::closingSpeed_` de §8.2 — relevée **avant** le solveur,
      c'est la seule mesure d'impact que la réponse de collision n'a pas encore
      effacée.
- [ ] **3. La casse** — au-delà d'un seuil, la pièce touchée est détruite
      (§2.5.4). Les pattes cèdent avant les réservoirs : c'est le seuil par
      pièce qui raconte ça, pas une règle globale.
- [ ] **4. Le remorqueur** — épave irrécupérable : on paie, la fusée revient au
      garage amputée de ses pièces cassées, on rachète les pièces. Aucune perte
      de progression, seulement de l'argent. Le tarif monte avec la distance —
      se planter loin coûte cher, ce qui suffit à rendre les contrats lointains
      risqués sans inventer de pénalité.

---

## 8. Briques moteur à ajouter

Elles vivent dans `src/Engine`, pas dans `GameExemple`, et resservent à
n'importe quel jeu.

- [x] **1. Forces et impulsions depuis le gameplay.** Fait —
      `addForce`/`addTorque`/`addImpulse`/`addAngularImpulse` (avec les
      surcharges au point monde), `velocity`/`setVelocity`,
      `angularVelocity`/`setAngularVelocity` sur `EntityHandle`, implémentées
      dans `GameplayApi.cpp` à côté des transformées. Un appel sur une entité
      sans corps est sans effet : `Physics_S` crée les corps dans son propre
      `fixedUpdate`, qui passe après celui du jeu.

      **Les setters de transformée poussent la pose dans Jolt.** Il n'y a pas
      d'API de téléportation à part : `setPosition`, `setLocalPosition`,
      `translate` et les rotations déplacent aussi le corps ou le personnage,
      parce que sans ça le système qui détient la pose réécrit la transformée
      au tick suivant et le déplacement disparaît — silencieusement, ce qui
      était déjà le cas des corps **dynamiques** avant. Les vitesses ne sont
      pas touchées : le jeu les remet à zéro s'il le veut. Les systèmes
      moteur, eux, écrivent par `Transform_S` directement, donc leur écriture
      de retour ne reboucle pas.

- [x] **2. Capteurs et contacts.** Fait — `sensor_` dans `RigidBody_C` (volume
      qui signale les recouvrements sans réponse de collision), et
      `world.contacts()` qui rend les `ContactEvent` du dernier pas : les deux
      entités, entrée ou sortie, le point, la normale et `closingSpeed_`.
      Quatre choses à savoir avant de s'en servir :
      1. **Les événements ont un tick de retard.** `Physics_S` les résout après
         son `step`, qui passe après le `fixedUpdate` du jeu — donc le jeu lit
         au tick N les contacts du pas N-1.
      2. **`closingSpeed_` est la seule donnée non reconstructible.** Elle est
         relevée avant le solveur, positive quand les deux corps se
         rapprochaient ; après coup la réponse de collision l'a effacée. C'est
         la mesure du « posé à moins de 3 m/s » de §7.2.
      3. **`a_` et `b_` ne sont pas ordonnés par rôle** mais par `BodyID` de
         Jolt : le capteur peut être dans l'un ou l'autre, il faut tester les
         deux. La normale s'oriente en conséquence.
      4. **Un corps qui s'endort largue tous ses contacts**, donc une caisse au
         repos dans la soute émet une fausse sortie, puis une nouvelle entrée à
         son réveil. Les entrées sont idempotentes (ranger une caisse déjà
         rangée ne fait rien), les sorties demandent de vérifier que le corps a
         vraiment bougé.
- [x] **3. Contrôleur de personnage.** Fait — `Character_C` + `Character_S`
      au-dessus du `CharacterVirtual` de Jolt, appelé dans la boucle à pas fixe
      après le pas physique. Le jeu écrit `mode_`, `moveVelocity_`, `wantJump_`
      et `gravity_` ; il lit `onGround_` et `velocity_`. Cinq points :
      1. **`gravity_` pilote tout** : la chute, et le vecteur *up* qui en est
         déduit. Marcher sur une sphère (§3.2) ne demande donc qu'à écrire la
         gravité locale — mesuré, un basculement de la gravité en −X décolle le
         personnage d'un sol horizontal et le fait accélérer à 9.81 m/s².
      2. **`mode_` choisit la règle de vitesse**, et c'est la seule chose que
         le moteur impose. En `Walk`, `moveVelocity_` est projeté sur le sol,
         la gravité s'intègre, `wantJump_` saute, et le personnage hérite de la
         vitesse de son support. En `Fly`, `moveVelocity_` **est** la vitesse,
         en 3D, sans sol ni collage ni gravité — le jeu écrit alors sa propre
         règle entière. Tout le reste (glissement le long des murs) est de la
         géométrie, pas du game feel : ajouter un mode ne coûte qu'une branche
         dans `Character_S`.
      3. **La transformée est aux pieds**, pas au centre de la capsule, et
         `Character_S` n'écrit que la position : l'orientation reste au jeu,
         pour ne pas se battre avec la caméra.
      4. **Le personnage n'est pas dans le broadphase.** Un `CharacterVirtual`
         n'est pas un corps : aucun raycast ne le touche et rien ne rebondit
         dessus. Le jour où une caisse devra le heurter, c'est
         `mInnerBodyShape` dans les réglages Jolt, pas un corps à côté.
      5. Les vecteurs d'`ExtendedUpdateSettings` de Jolt sont câblés en +Y ;
         ils sont reconstruits depuis *up*, sinon monter une marche et coller
         au sol partent de travers dès que la gravité n'est plus verticale.
- [x] **4. HUD depuis la DLL du jeu.** Fait — le jeu appelle ImGui
      directement. La frame ImGui est ouverte par le moteur
      (`Renderer::beginFrame`), pas par l'éditeur, donc `Game::update` tombe
      dedans aussi bien en exe autonome qu'en mode Play. Mesuré sur un dump de
      frame : un bloc de 200×80 dessiné par la DLL sort à 16000 pixels exacts,
      à la bonne place et à la bonne couleur.
      - **En exe autonome il n'y avait rien à faire** : le jeu est compilé
        dedans, une seule copie d'ImGui.
      - **En DLL**, `Batap_Engine` est une lib **statique** : la DLL a son
        propre contexte ImGui *et* son propre allocateur. L'hôte passe les deux
        dans `GameModuleAPI` et `adoptHostImGui(*out)` les adopte au chargement
        — c'est le schéma que la doc d'ImGui prescrit aux DLL
        (`SetCurrentContext` + `SetAllocatorFunctions`). Oublier l'allocateur
        marche par accident tant que les deux binaires partagent le même CRT,
        et corrompt le tas dès qu'ils divergent.
      - **Le seul piège qui reste** : `game_module.cpp` doit appeler
        `adoptHostImGui`. Sans ça le jeu dessine dans un contexte que personne
        ne rend, sans erreur ni message. C'est noté dans la liste des pièges
        en tête de `GameModule.h`.

      Passer le moteur en lib partagée règlerait la famille entière de
      problèmes (ImGui, mais aussi les globales de réflexion décrites dans
      `GameModule.h`) ; ça reste la sortie de secours si ces adoptions se
      multiplient.

- [ ] **5. Animations de nœuds.** Les pieds d'atterrissage de la fusée
      bougent ; c'est la première animation du moteur, et il n'en a aucune.

      **Acté : animation rigide par nœud, pas de skinning.** Chaque pied est
      un objet Blender séparé, origine sur la charnière, parenté à la coque.
      `MeshDecomposer` fait déjà une entité par nœud avec `Hierarchy_C` et un
      `Transform_C` local : animer, c'est écrire la rotation locale de
      l'entité du pied, `Transform_S` propage. Zéro code rendu. Le skinning
      (armature ou shape keys) demanderait des joints et des poids dans le
      `.bmesh` et le vertex layout, des matrices d'os par instance dans
      `InstanceManager`, un vertex shader dédié et un upload par frame — pour
      trois pieds qui pivotent, ça ne paie pas. On y viendra le jour où un
      personnage doit se déformer, pas avant.

      1. **Import.** Lire les `aiAnimation` de la scène Assimp dans
         `MeshDecomposer` : un clip par animation, un canal par nœud (clés
         position / rotation / échelle, nom du nœud, durée, ticks par
         seconde). Écrire un fichier `.banim` à côté des `.bmesh`, et un
         `AnimationHandle` dans `AssetHandle.h` comme les trois autres.
      2. **Composant.** `Animator_C` sur la racine de l'objet : le clip, `t`,
         vitesse, boucle ou non. Plat et trivialement copiable comme les autres.
      3. **Système.** `Animator_S` avance `t` et, pour chaque canal, retrouve
         l'entité descendante qui porte le nom du nœud et écrit sa transformée
         locale par `Transform_S`. Interpolation linéaire des positions,
         `slerp` des rotations, rien de plus.
      4. **Gameplay.** Le jeu ne touche que `t` et la vitesse : pieds sortis =
         `t` va vers la fin, rentrés = vers zéro. Pas d'état machine ni de
         blend, une seule barre de temps par clip suffit ici.

      Trois pièges :
      - **Les canaux se lient par nom de nœud.** Renommer un pied dans Blender
        casse le lien silencieusement ; le système doit le signaler une fois
        au chargement plutôt que d'animer dans le vide.
      - **Ne jamais ajouter `aiProcess_PreTransformVertices`** aux flags
        d'import : il aplatit la hiérarchie et jette les animations avec.
        Même chose côté Blender : ne pas joindre les pieds ni appliquer leurs
        transforms, l'origine reviendrait au monde.
      - **Un pied animé ne collisionne pas tout seul.** Le collider de la
        fusée est celui de `RigidBody_C`, il ne suit pas les enfants. Pour
        v0 les pieds sont visuels, le collider reste une forme fixe sur la
        coque qui inclut les pieds sortis. Un collider par pied qui suit le
        mouvement, c'est un `MutableCompoundShape` Jolt et une mise à jour de
        la forme par tick — à ne faire que si l'atterrissage (§7) le réclame.

---

## 9. Le buggy

Le premier véhicule, et celui qui définit le rayon d'action du début.

- [ ] **1. Prendre le véhicule de Jolt.** `VehicleConstraint` +
      `WheeledVehicleController` sont dans la lib : roues, suspension, moteur,
      boîte, différentiels, barres anti-roulis. L'écrire à la main serait la
      même erreur que de réécrire `CharacterVirtual`.
- [ ] **2. `Buggy_C` + `Buggy_S`** sur le modèle de `Character_C` : le composant
      porte la géométrie et les réglages, le jeu y écrit gaz / frein / direction,
      le système tient l'objet Jolt. Même propriétaire que le pool de
      personnages, pour la même raison de durée de vie.
- [ ] **3. La gravité est déjà bonne** : le châssis est un corps dynamique, donc
      `Gravity_S` s'en occupe sans une ligne de plus. En revanche
      `VehicleConstraint` a un vecteur « haut » comme le personnage — même piège
      qu'en §8.3, il faut le réécrire depuis la gravité locale à chaque tick,
      sinon rouler sur le flanc d'une lune part de travers.
- [ ] **4. Une soute** — les caisses s'y baladent librement comme dans la fusée
      (§4). Freiner trop fort les envoie devant.

---

## 10. Contrats, argent, améliorations

Rien de physique ici, et c'est pourtant ce qui transforme la boucle en jeu.
Tout tient dans des composants plats et de l'UI.

- [ ] **1. `DeliveryPoint_C`** — posé à l'éditeur sur n'importe quel astre.
      Porte de quoi identifier le lieu ; le reste est calculé.
- [ ] **2. Contrats** — un colis à prendre, un point à atteindre, une prime.
      **La prime se dérive du delta-v, pas d'un chiffre écrit à la main** :
      c'est ce qui garantit que les destinations lointaines paient mieux sans
      qu'on ait à équilibrer un tableau.
- [ ] **3. Livraison validée** — le colis (un `Carryable_C`) repose dans la zone
      du point. On le porte à la main : c'est ce qui rend l'arrivée concrète
      plutôt qu'un écran de validation.
- [ ] **4. La boutique** — pièces de fusée, réparations, buggy, puis capacités
      du perso (porter plus lourd, réserve d'oxygène, jetpack — le mode `Fly` de
      `Character_C` existe déjà) et outillage du garage.
- [ ] **5. Sauvegarde** — argent, pièces possédées, contrats en cours. Le
      sérialiseur de scène existe ; ce qui manque est un état de partie qui ne
      soit pas une scène.

---

## Ordre

**v0 — piloter.** Un astre, une fusée d'une pièce, la caméra orbitale, le HUD
minimal. Décoller, tourner, revenir se poser. Pas de garage, pas de contrat, pas
de buggy. **Si piloter n'est pas bon, rien de ce qui suit ne le sauvera.** Il ne
manque pour ça que §6.1 et le branchement des touches sur `throttle_` et
`rcsInput_` : tout le reste est fait.

**v1 — le garage.** Le correctif de §5.1 (le contenu suit son astre), la scène
du garage sur la lune, la bascule de vue, et une livraison codée en dur d'un
bout à l'autre. C'est la première fois que la boucle moyenne tourne.

**v2 — le buggy.** §9 en entier, plus des points de livraison dispersés sur la
lune. Le jeu du début existe : on livre chez soi et on n'a pas les moyens de
partir.

**v3 — l'argent et les pièces.** §10 et §2.5. La progression s'ouvre : on
s'achète sa première fusée, puis de quoi aller plus loin.

**v4 — les crashs.** §7.3 et §7.4. L'échec devient une dépense.

---

## Pièges

- **Le contenu de surface ne suit pas son astre** (§5.1). Le plus gros piquet
  restant : tant que `Physics_S` place les corps statiques depuis leur position
  *locale*, rien ne peut être parenté à une lune qui bouge.
- **Les constantes se règlent ensemble** — `t ∝ sqrt(R/g)` (§1.2). Monter `g` en
  même temps que `R` annule l'effet sur les durées de trajet.
- **Le vecteur « haut » se réécrit depuis la gravité** — vrai pour le personnage
  (§8.3), vrai pour le véhicule (§9.3), et les réglages Jolt qui le câblent en
  +Y sont à reconstruire à chaque fois.
- **Composants du jeu** — un nouveau `Foo_C` doit être inclus depuis
  `GameComponents.h` pour que la DLL l'enregistre dans son registre.
- **Zéro état statique dans la DLL** — l'état du jeu (argent, contrat en cours)
  vit dans des composants, pas dans des globales : un hot reload le perdrait.
- **Le pas fixe est l'ami de la physique** — toute la logique de vol dans
  `fixedUpdate`, jamais dans `update`. Et ce qu'on y lit reflète le pas
  précédent (§2).
