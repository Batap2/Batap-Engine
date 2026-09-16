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

**La physique doit sembler réaliste, pas l'être.** Les *lois* sont les vraies —
gravité newtonienne en 1/r², orbites, transferts, atterrissage propulsif, tout
est intégré pour de bon et rien n'est scripté. Les *constantes* sont choisies
pour le jeu : la Terre fait 250 m de rayon, la gravité au sol vaut 18 m/s², un
transfert vers la Lune dure une minute. Personne ne peut voir qu'une constante
est fausse ; tout le monde sent qu'une trajectoire est vraie.

Ce choix est ce qui rend le jeu jouable, et il tire dans le bon sens : les
petites échelles rapprochent tout, et une gravité plus forte que la nôtre rend
le déplacement à pied plus nerveux (à 9.81 on flotte) **tout en raccourcissant
les trajets** (§1.2). Le joueur, les caisses et l'entrepôt gardent seuls leur
taille réelle — c'est à eux qu'on mesure le monde.

---

## 0. Inventaire

**Déjà là** : Jolt via `PhysicsWorld`, `RigidBody_C` avec formes multiples et
`gravityFactor_` par corps, pas de temps fixe (`Game::fixedUpdate`), raycast
scène (`SpatialIndex::raycast` → `entt::entity`), `DebugDraw` (lignes, sphères,
boîtes), caméras multiples via `Camera_C::active_`, `Time::scale_`, hot reload
du jeu.

**Manquait, côté moteur** — les quatre briques de §8 sont faites : forces et
impulsions sur `EntityHandle`, capteurs et événements de contact, contrôleur de
personnage, et ImGui accessible depuis la DLL du jeu. Le détail et les pièges
de chacune sont en §8.

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
objet dynamique ne coûtent rien. C'est plus juste *et* plus court à écrire.

- [x] **1.** `CelestialBody_C` + `CelestialBody_S` — fait. Le composant porte
      `mu_` (G·M directement, pour ne pas traîner de très grands nombres) et
      `radius_` ; le système **dérive le collider** : une sphère Jolt de
      `radius_`, analytique et exacte, aucune tessellation à faire. Le relief et
      l'entrepôt seront des meshes statiques posés dessus.
      **L'astre ne peut pas être `Dynamic`** : chaque corps qu'il attire lui
      repousse dessus par le contact, et un astre dynamique serait baladé par ce
      qui se pose sur lui. Le système le rétrograde en `Static` ; `Kinematic`
      reste permis pour les rails du point 3. Dériver le collider plutôt que de
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
      - **Conséquence à anticiper** : un astre qui bouge emporte les corps
        **dynamiques** posés dessus, pas les meshes **statiques**. Si l'entrepôt
        de §5 est posé sur une Terre qui orbite, il sera laissé sur place. D'où
        le choix simple : **épingler la Terre en `Static`** et ne faire orbiter
        que les lunes.

### 1.2 Échelle

**Contrainte de départ : un transfert Terre-Lune dure une minute réelle.** Un
transfert de Hohmann dure `t = π·sqrt(a³/mu)` avec `a = (r1+r2)/2` et
`mu = g·R²`. En posant les distances comme des multiples de `R`, il ne reste
que :

```
t ∝ sqrt(R / g)
```

C'est l'outil de réglage : il dit comment bouger une constante sans casser le
reste. Doubler le rayon de la planète rallonge le trajet de 40 % — sauf si on
double aussi sa gravité, auquel cas rien ne change. **Et comme une gravité plus
forte est aussi ce qu'on veut à pied, les deux réglages tirent ensemble : monter
`g` achète un monde plus grand à durée de trajet constante.**

Valeurs de départ, à régler au ressenti et pas au calcul :

| | rayon | g surface | mu = g·R² | orbite |
|---|---|---|---|---|
| Terre | 250 m | 18 | 1 125 000 | — |
| Lune | 80 m | 2.5 | 16 000 | 1200 m |
| Petite lune | 30 m | 3 | 2 700 | 600 m, inclinée |

Ce qu'elles donnent — tout est lisible à deux chiffres, ce qui est le vrai
critère :

- transfert depuis une orbite basse à 300 m vers 1200 m : **61 s** ;
- vitesse orbitale basse **61 m/s**, libération **95 m/s** ;
- injection vers la Lune : **16 m/s** de delta-v, deux ou trois secondes de
  poussée ;
- arrivée à 11 m/s de vitesse relative à la Lune, posé à moins de 3 m/s ;
- sphère d'influence de la Lune ≈ 220 m pour 80 m de rayon : largement de quoi
  se faire capturer sans viser au mètre près ;
- la Terre fait 1570 m de tour et l'horizon est à 30 m des yeux — un petit
  monde, mais un monde.

Les cinq boutons, et ce que chacun fait :

| bouton | effet direct | effet de bord |
|---|---|---|
| `g` Terre | ressenti à pied (saut, chute) | raccourcit le trajet en `1/sqrt(g)` |
| `R` Terre | taille du monde jouable | rallonge le trajet en `sqrt(R)` |
| distance Lune | durée du voyage (≈ `d^1.5`) | et rien d'autre |
| `g` Lune | **difficulté de l'atterrissage** | taille de sa sphère d'influence |
| poussée / masse | marge de rattrapage en vol | consommation |

La petite lune est inclinée pour ne pas venir perturber la trajectoire
Terre-Lune par hasard. Elle sert d'escale et de second terrain.

**L'alternative, si le monde-bille ne plaît pas visuellement** : garder une
grosse planète et faire le trajet **en poussée continue** au lieu d'une orbite
de transfert. `t = 2·sqrt(d/a)` — à 2 g de poussée, une minute couvre 18 km.
Tout aussi cohérent, mais ça coûte une dizaine de fois plus de carburant et ça
supprime la mécanique orbitale du jeu. À trancher en regardant la première
planète à l'écran, pas avant.

### 1.3 Origine flottante — pas nécessaire, et c'est une conséquence de §1.2

Noté ici pour ne pas y revenir : à l'échelle réelle, le `float` (résolution
relative 6e-8) donne un pas de position de 23 m à la distance Terre-Lune, et
tout le projet aurait eu besoin d'une origine flottante. **À 1500 m de
l'origine, le pas vaut un dixième de millimètre.** Rien à faire, aucune brique
moteur à écrire, et les positions monde peuvent être mises en cache librement.

C'est le second gros dividende du modèle réduit, après la disparition de
l'accélération du temps (§6.4).

---

## 2. La fusée

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
- [ ] **4. Accélération du temps — supprimée par §1.2.** Un transfert dure une
      minute, il n'y a donc rien à accélérer : le vol se joue en temps réel d'un
      bout à l'autre. C'est un système entier en moins (multiplier le pas fixe
      casse l'intégration, et il aurait fallu forcer le retour à ×1 en
      approche), et surtout un vol qu'on pilote au lieu de le regarder passer.

---

## 7. L'atterrissage

C'est le moment du jeu, l'équivalent de la marche arrière au quai dans Euro
Truck. Tout le reste est calme, ici on retient son souffle.

**Et ça doit rester rattrapable.** Le plaisir vient de la tension, pas de
l'échec — une approche ratée doit pouvoir être reprise, pas punie. Les leviers,
du plus efficace au moins :

1. **`g` de la Lune bas** (§1.2). C'est le vrai bouton de difficulté : il donne
   du temps de réaction et réduit la vitesse d'arrivée au sol. Tout le reste
   n'est qu'un ajustement à côté.
2. **Poussée/masse confortable** (TWR ≈ 3 à vide) — on doit toujours pouvoir
   annuler sa descente, même tard.
3. **Carburant généreux.** Le jeu porte sur le pilotage, pas sur l'optimisation
   du delta-v ; tomber en panne sèche en approche n'apprend rien.
4. **Piste large** devant la fusée, seuils d'atterrissage clairement indulgents
   au départ. On les resserre quand c'est trop facile, jamais l'inverse.
5. **Amortissement de rotation actif** (§2.3), pour que l'assiette ne soit pas
   un problème en plus de la vitesse.

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

**v2 — le voyage.** La Lune, la trajectoire prédite, le HUD, la piste et ses
conditions d'atterrissage. Rien d'autre : ni origine flottante ni accélération
du temps, le modèle réduit de §1.2 a supprimé les deux.

**v3 — le FPS.** Le personnage, l'entrepôt, la pompe, l'échelle, la bascule de
vue. C'est ce qui donne le ton du jeu, mais c'est de l'habillage autour d'une
boucle qui doit déjà tourner.

---

## Pièges

- **Les constantes se règlent ensemble** — `t ∝ sqrt(R/g)` (§1.2). Grossir la
  planète seule rallonge le trajet, et au-delà de quelques minutes ça ramène
  l'accélération du temps et l'origine flottante, deux systèmes que le modèle
  réduit avait supprimés. Monter `g` en même temps annule l'effet.
- **Gravité globale de Jolt** — `gravityFactor_ = 0` sur chaque corps dynamique,
  sinon deux gravités s'additionnent.
- **Composants du jeu** — un nouveau `Foo_C` doit être inclus depuis
  `GameComponents.h` pour que la DLL l'enregistre dans son registre.
- **Zéro état statique dans la DLL** — la règle du hot reload vaut ici aussi :
  l'état du jeu (phase courante, fusée pilotée) vit dans des composants, pas
  dans des globales.
- **Le pas fixe est l'ami de la physique** — toute la logique de vol dans
  `fixedUpdate`, jamais dans `update`.
