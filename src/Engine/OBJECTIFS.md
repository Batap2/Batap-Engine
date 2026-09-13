# Objectifs

**État (2026-09-11)** : Vulkan only. Pipeline composant réflexif
(`BATAP_COMPONENT`), éditeur-lib, Play/Stop, hot reload du jeu (DLL + snapshot
JSON), façade gameplay (`EntityHandle`/`World`, `batap.h`), timestep fixe —
tout ça est fait ; l'historique détaillé vit dans git. Fil conducteur
inchangé : réduire ce qu'un dev doit toucher, une seule source de vérité par
concept.

---

## 1. Physique — Jolt (chantier courant)

Décisions actées :
- **Jolt** (broadphase lock-free, requêtes complètes, adopté par Godot 4.4,
  shippé par Horizon FW) — pas de physique maison, pas de BVH gameplay maison.
- Les requêtes gameplay (raycast, overlap, sweep) passent par Jolt — toute
  entité à collider est requêtable, comme `Physics.Raycast`/PhysX chez Unity.
- Le `PhysicsSystem` Jolt vit dans **`World`** (pas `Engine` : deux Worlds ne
  partagent pas leurs corps), côté hôte (jamais dans la DLL jeu).
- **v1 = formes primitives** (box/sphère/capsule) : `Mesh` ne garde aucune
  donnée CPU, un `MeshShape` exigerait de relire le `.bmesh` — plus tard.

Étapes, chacune validable seule :

- [x] **1. Vendoring + build** — fait : submodule `include/JoltPhysics` épinglé
      **v5.6.0**, `add_subdirectory(Build)`, linké dans `Batap_Engine`. Les
      defines `JPH_*` et les flags ISA sont PUBLIC sur la cible → l'ODR est
      garanti par le link (conséquence : `/arch:AVX2` s'applique à tout le
      moteur). Jolt part en **DLL** (`JPH_BUILD_SHARED_LIBS ON`, explicite) →
      une seule copie partagée éditeur/DLL jeu, `Jolt.dll` copiée dans `bin/`.
      Validé par un smoke test temporaire (supprimé depuis) : sphère lâchée
      de y=4, au repos à y≈0.48 après 120 steps (0.5 − penetration slop 2 cm,
      normal).
- [x] **2. Monde physique dans `World`** — fait : `Physics/PhysicsWorld`
      (temp allocator, job pool, layers, `JPH::PhysicsSystem`) possédé par
      `World`, `clear()` appelé dans `resetScene()` — le registry meurt au
      Play/Stop et au hot reload, les corps meurent avec. `JoltRuntime`
      (Factory + `RegisterTypes`, process-wide) est refcompté et déclaré
      premier membre : `TempAllocatorImpl` alloue déjà via l'allocateur Jolt.
      Les includes de Jolt sont passés en SYSTEM côté racine, sinon
      `-Weverything -Werror` refuse ses headers. Validé par instrumentation
      temporaire du ctor de `World` : sphère au repos à y=0.48 après 120 steps,
      2 corps → 0 après `resetScene()`.
- [x] **3. Composants** — fait : **un seul** composant `RigidBody_C`, plat et
      trivial, `BATAP_COMPONENT`. Il porte la forme (Box/Sphere/Capsule +
      dimensions) *et* la dynamique (motion, masse, friction, restitution,
      damping, gravityFactor). Pas de `Collider_C` séparé : `motion_ = Static`
      **est** le « collider seul » — solide et requêtable, jamais bougé par la
      simulation. Un composant unique supprime les combinaisons muettes
      (un rigidbody sans forme ne faisait rien) et la question de qui possède
      le `bodyId_`.
      `bodyId_` (uint32) et `shapeScale_` sont de l'état runtime : sortis de
      la réflexion par `fieldSkip<>()`, ajouté à `ComponentRegistry` — ni
      `.btpl`, ni inspecteur, ni snapshot de hot reload.
      Reste rêche : un enum réfléchi s'édite au DragScalar (pas de combo) —
      un `FieldType` générique pour les enums via magic_enum réglerait ça
      pour tous les composants.
- [x] **4. Sync ECS ↔ Jolt** — fait : `Systems/Physics_S`, hôte, appelé dans
      la boucle fixe de `World::update(Game&)` (donc pas en mode édition).
      Un seul passage par step sur `view<RigidBody_C, Transform_C>` :
      création/destruction des corps pour coller aux composants, mise à jour
      de forme et de motion type, push des kinematic (`MoveKinematic`) et des
      statiques (`SetPositionAndRotationWhenChanged`), `step(fixedDt_)`, puis
      read-back des dynamic éveillés via `setLocalPosition/Rotation`.
      La création est **paresseuse** (corps créé au premier step où
      `RigidBody_C + Transform_C + active_` sont là) plutôt que sur
      `on_construct` : l'ordre d'ajout des composants ne compte pas, et le
      balayage est de toute façon nécessaire pour les kinematic. Seul
      `on_destroy<RigidBody_C>` est un hook — c'est le dernier moment où le
      `bodyId_` est lisible. `active_ = false` détruit le corps, `true` le
      recrée.
      Le scale du transform est pris en compte : forme enveloppée dans un
      `ScaledShape`, refaite (`SetShape`) quand le scale change en jeu.
      `MakeScaleValid` ramène au plus proche scale que la forme accepte —
      une sphère n'a qu'un scale uniforme, une capsule qu'un X/Z uniforme —
      au lieu de laisser Jolt asserter, scale nul compris.
      Piège Jolt : un corps créé Static n'a pas de `MotionProperties`, donc
      `SetMotionType` ne peut pas l'en sortir (crash). Traverser la frontière
      Static coûte un corps neuf ; Kinematic ↔ Dynamic passe par
      `SetMotionType` et garde la vitesse.
      Limites v1 assumées : la pose du corps est la pose **locale**, un corps
      sur une entité parentée dériverait ; et déplacer un corps Static ne
      réveille pas les corps endormis posés dessus (comportement Jolt — pour
      de la géométrie mobile, c'est Kinematic qu'il faut).
      Validé sur `GameExemple` par un harnais temporaire (retiré) pilotant des
      steps fixes : 4 corps, bille au repos sur un sol Static à 1.000, sphère
      ×2 à 1.480, corps `active_=false` jamais simulé (5.000 inchangé),
      kinematic suivi par Jolt à l'identique (3.995 / 3.995), `active_` off→on
      qui détruit puis recrée, Kinematic→Dynamic et Static→Dynamic→Static sans
      crash, `registry.destroy` qui détruit via le hook, `resetScene()` à 0.
- [x] **5. Formes multiples par corps** — fait : `RigidBody_C` porte un
      `std::vector<Shape>` à la place de la forme unique. Une `Shape` est
      plate (kind, dimensions, `localPos_`, `localRotDeg_`), le composant
      reste une entité = un objet physique.
      Écarté : des `Collider_C` sur les entités enfants à la Unity — le sens
      d'une entité se mettrait à dépendre de la présence d'un rigidbody chez
      un ancêtre. Écarté aussi : un `std::array` fixe façon `Materials_C` —
      son plafond n'y a de sens que parce qu'un slot matériau est **indexé**
      face à un sous-mesh, ce qu'une liste de formes n'est pas.
      Le `static_assert(is_trivially_destructible)` de `addComponentType` a
      donc sauté. Il venait du commit « game as a dll » et gardait une vraie
      propriété : `~App` détruisait `gameModule_` (donc `FreeLibrary`) avant
      que `~World` ne détruise le registry, si bien qu'un destructeur de
      composant tournait après l'unload. Plutôt que de contourner, l'ordre est
      réparé — `~App` appelle `resetScene()` — ce qui répare aussi le bug
      latent des composants **définis dans la DLL** : entt crée leur pool par
      `allocate_shared` dans le module qui l'instancie en premier, donc
      vtable et deleter appartenaient à la DLL indépendamment de l'assert.
      `RigidBody_C` n'était de toute façon pas concerné : `connectHooks` fait
      `on_destroy<RigidBody_C>()` → `assure<>()` dans le ctor de `World` et
      après chaque `reg = entt::registry{}`, donc son pool est toujours hôte.
      Côté Jolt, trois cas et non deux : forme nue si l'unique `Shape` est
      centrée, `RotatedTranslatedShape` si elle a un offset (un
      `StaticCompoundShape` exige au moins deux enfants), `StaticCompoundShape`
      au-delà. `MakeScaleValid` couvre un piège de plus : un compound refuse
      tout scale non uniforme dès qu'un de ses enfants est tourné.
      La forme n'est plus refaite sur le seul changement de scale. Première
      version : un hash des formes comparé à chaque step — ça ne rate jamais,
      mais ça travaille pour chaque corps à chaque step même quand rien ne
      bouge. Remplacé par le mécanisme prévu par entt, qui n'existait nulle
      part dans le moteur : `on_update<T>` + `registry.patch<T>(e)`.
      `ComponentType` gagne un `patch` type-effacé à côté des quatre autres
      opérations entt, l'inspecteur l'appelle là où il calculait déjà son
      `changed` — donc c'est valable pour **tous** les composants, pas que les
      colliders — et `Physics_S` lève un `dirty_` (fieldSkip) dessus. Coût :
      un `index()` de sparse set plus un `publish`, par édition et non par
      step. Contrepartie assumée : du code jeu qui écrit `shapes_` sans
      `patch` ne déclenche rien — le contrat entt, pas une bizarrerie maison.
      Le même chemin `dirty_` repousse friction, restitution, gravity factor,
      damping et masse vers le corps Jolt, ce qui répare deux bugs au
      passage : les éditer en Play ne faisait rien (ils n'étaient lus que dans
      `BodyCreationSettings`), et `SetShape(..., true, ...)` recalculait la
      masse depuis la densité de la forme, écrasant `mass_` à chaque
      changement de scale. Damping et masse ne sont pas sur `BodyInterface` :
      ils passent par un `BodyLockWrite` et `MotionProperties`.
      Le `drawUI` du field type `std::vector<Shape>` suffit à l'inspecteur —
      pas de `customEditor`, la boucle générique continue de dessiner masse et
      friction. `registerPhysicsFieldTypes()` est à appeler **dans les deux
      modules** (`Engine.cpp` et `game_module.cpp`), comme les asset types.
      Migration : les anciennes clés `shape`/`halfExtents`/`radius`/
      `halfHeight` sont ignorées au chargement (`cj.contains`) et
      `componentVersions` n'est jamais relu — `jolt.btpl` a été converti par
      script.
      Validé à l'image sur `GameExemple` par un harnais temporaire (retiré) :
      un compound sphère + boîte tournée ajouté au torus dynamique, corps
      construit en Play, torus au repos **sur sa boîte**, sphère décollée du
      plan — Jolt utilise bien le compound, et le fil de debug coïncide avec
      lui. Puis le chemin `patch` : torus au repos sur sa sphère, jambe
      ajoutée en cours de simulation via `registry.patch`, corps reconstruit
      et déséquilibré à l'image suivante. Sortie propre de l'éditeur avec la
      DLL jeu chargée.
- [ ] `fixedLateUpdate` seulement si un cas concret le réclame (Unity n'en a
      pas ; les contacts passent par les listeners Jolt).

## 1bis. Debug draw

Décision actée : **pas de lib externe**. debug-draw (glampert) couvre les
bonnes primitives mais aplatit tout en segments côté CPU, à chaque appel et à
chaque frame, dans des tableaux de taille fixe — quelques centaines de
colliders saturent `DEBUG_DRAW_MAX_LINES`. Et elle n'économise pas le travail
Vulkan : son `RenderInterface` est exactement la pass qu'il faut écrire de
toute façon. Le gain visé est CPU et bande passante, pas GPU : le vertex
shader traite autant de sommets dans les deux cas.

Le principe : **presque toute primitive de debug est une forme unitaire sous
une matrice**. Les wireframes unitaires sont construits une fois au démarrage,
chaque appel n'écrit qu'un `(mat4, couleur)` — O(1) par primitive au lieu de
O(segments). Un cube unitaire sert déjà de box, et servira d'AABB, d'OBB et de
frustum (le cube sous l'inverse de la view-projection).

- [x] **1. Pass de lignes + collecte** — fait : `Renderer/DebugDraw` ne
      connaît ni Vulkan ni l'ECS (v3f, m4f, col3 seulement), possédé par
      `Engine`, atteint par `world.debug()` — pas de singleton, la lib moteur
      est liée à la fois dans l'éditeur et dans `Game.dll`. Deux seaux : les
      formes instanciées et les segments bruts. Les deux pipelines n'ont
      **aucun vertex input** : géométrie et instances sont lues en
      `StructuredBuffer` via `SV_VertexID` / `SV_InstanceID`, ce qui a évité
      de toucher aux vertex input rates. Profondeur testée, jamais écrite, et
      tracé **après le ciel** (qui n'écrit pas de profondeur et repeindrait
      par-dessus les fils sur le fond). `GraphicsPipelineBuilder` gagne un
      `.topology()`. L'upload passe par `requestUpload` pendant `Engine::endFrame`, le
      `flushUploads` de `render()` fait la copie dans la même frame — même
      contrat que les pools d'instances. Validé à l'image : cube unitaire jaune,
      boîte (0.5, 2, 0.5) cyan décalée, et les trois axes RGB, correctement
      occultés par le sol.
- [x] **2. Le reste des formes** — fait : sphere, capsule, aabb, frustum,
      arrow, axes, durées et calque overlay.
      Trois points non évidents :
      **la capsule est en trois morceaux** (deux dômes + les arêtes du
      cylindre) parce qu'elle a deux dimensions indépendantes : un seul scale
      non uniforme écraserait ses calottes en ellipsoïdes. Le dôme du bas est
      le même, miroité en Y. Ça reste O(1) — trois matrices, aucune
      tessellation.
      **Le frustum est le cube unitaire sous une matrice projective**, pas
      affine : le VS divise donc par w. Pour toutes les autres formes w vaut 1
      et la division ne coûte rien. Le cube couvre [-1, 1] alors que le z de
      clip Vulkan va de 0 à 1, d'où un remap de demi-profondeur avant
      l'inverse de la view-projection.
      **Les durées tiennent dans une seule liste** : chaque entrée porte une
      date d'expiration, celles de la frame courante expirent immédiatement.
      `endFrame(dt)` avance l'horloge et purge — un seul balayage, pas de
      seconde liste.
      L'overlay est un second `DebugDraw` (`world.debugOverlay()`) plutôt
      qu'un drapeau par appel : rien de collant d'une feature à l'autre, et
      les sites d'appel restent courts. Ses pipelines ne diffèrent que par un
      `VK_COMPARE_OP_ALWAYS`.
      Validé à l'image : les six primitives dessinées ensemble, la sphère
      overlay visible **à travers** la boîte, le frustum correctement évasé
      (donc la division par w opère), et une ligne émise une seule fois à la
      frame 20 toujours vivante à la frame 60.
      **Puis unifié : une ligne est une forme.** Le seau « segments bruts »
      n'avait pas lieu d'être — je le croyais nécessaire parce qu'une ligne
      sous matrice semblait exiger une base orthonormée, or la ligne unitaire
      est sur +X avec y = z = 0, donc les deux colonnes du milieu ne sont
      jamais lues : `[b-a | 0 | 0 | a]` suffit, sans normalisation ni cas
      dégénéré. Coût : 32 octets de plus par ligne, sur le seau à faible
      volume. Gain : un shader, deux pipelines sur quatre, un buffer, un
      binding du frame set (8 → 7), la moitié de l'upload et une liste
      parallèle de moins pour les durées. Même rendu après unification.
- [x] **3. Hitboxes** — fait : `Physics_S::drawColliders`, appelé depuis
      `Systems::update` (donc dans les deux `World::update`), piloté par les
      composants et jamais par les corps Jolt — en mode édition la simulation
      ne tourne pas et aucun corps n'existe. Couleur par motion : vert
      statique, bleu kinematic, cyan dynamic, gris si `active_` est faux.
      Toggle **View > Colliders** dans l'éditeur (`showColliders_`).
      Deux points de fidélité qui ne vont pas de soi : le fil est construit
      sur la pose **locale**, comme le corps — dessiner la pose monde
      mettrait le collider là où Jolt ne l'a pas mis ; et le scale reproduit
      `MakeScaleValid` (sphère uniformisée par la moyenne des trois axes,
      capsule uniformisée en X/Z), sinon on afficherait un ellipsoïde là où
      Jolt simule une sphère.
      Validé à l'image sans `Game` donc sans `fixedUpdate` : les cinq
      colliders visibles avec leurs couleurs, la capsule et la sphère
      correctes, et la boîte scalée (2, 1, 2) visiblement plus large que
      haute.

## 1quater. Handles d'assets dans l'inspecteur générique

Constat parti d'un bug : les champs `MaterialHandle` et `TextureHandle` de
`Billboard_C` n'apparaissaient pas. Cause : la boucle de l'inspecteur ne
dessine un champ que si son `FieldType::drawUI` est rempli, et il ne l'était
pour aucun type de handle. `Mesh_C`, `Materials_C` et `Skybox_C` s'en sortaient
avec `customEditor = true` et un panneau écrit à la main — mais `customEditor`
saute le composant **entier**, donc tout nouveau composant portant un handle
devait redessiner ses autres champs pour rien.

- [x] **Fait** : les handles deviennent un type de champ de première classe.
      `drawUI` gagne un `FieldUIContext&` (l'`App`, le picker, l'entité et le
      `ComponentType` courants) — sans lui un widget ne peut ni résoudre un
      handle en nom, ni lister les assets, ni appeler `loadAsset`.
      `AssetPickerPopup` arrête de coder en dur sa cible : `openField` retient
      le **nom du composant et l'offset du champ**, et `applyToField` les
      résout au moment du clic. Pas de pointeur gardé entre l'ouverture et le
      choix, donc un hot reload entre les deux ne laisse rien qui pende. La
      réécriture passe par `ComponentType::patch` puis `markDirty`, donc les
      systèmes qui écoutent `on_update` voient le changement comme n'importe
      quelle édition.
      `AssetPickerPopup::draw` rend désormais un `bool` — vrai la frame où un
      choix ou un `Clear` a été appliqué — c'est ce que le widget remonte en
      `changed`.
      Piège ImGui : `OpenPopup` et `BeginPopup` doivent partager la même pile
      d'ID, donc le popup est dessiné **dans** le widget du champ et pas une
      fois par composant.
      Les trois panneaux sur mesure restent en place et fonctionnent — ils
      pourront tomber quand l'envie viendra, le chemin générique les couvre.
      Validé à l'image : `Billboard_C` montre Material et Texture en boutons de
      picker à côté de ses autres champs, et le torus garde son panneau Mesh et
      son Rigid Body intacts.

## 2. Structures d'accélération

Le partage est réglé par le §1 : Jolt possède la structure physique et ne voit
que les colliders — et seulement en Play, puisque les bodies naissent dans
`Physics_S::fixedUpdate`, que le chemin éditeur de `World::update` n'appelle
pas. L'éditeur n'a donc aujourd'hui aucun index spatial du tout.

Deux chantiers sortent de là, à ne pas confondre : ils ne répondent pas aux
mêmes questions et ne partagent que les AABB.

### 2a. Requêtes spatiales — TLAS/BLAS

Acté : un index CPU des entités **de rendu**, le même en édition et en Play,
en deux niveaux. TLAS sur les AABB monde des instances (« quel objet »), BLAS
sur les triangles d'un mesh (« quel point, quel triangle, quelle normale »).

Ça ne remplace pas Jolt et ça ne le double pas : les deux répondent à des
questions différentes et la bonne réponse n'est pas la même. Un arbre à
collider capsule doit être *touché* sur sa capsule (la physique doit rester
cohérente avec elle-même) et *cliqué* sur sa branche visible. Jolt garde le
tir, la ligne de vue, les triggers, le sol ; l'index de rendu prend le picking,
le drag-to-surface, le snap, la mesure, et tout ce qui n'a pas de collider —
lumières, billboards, décor. `MeshShape` ne change rien à ce partage : il est
réservé aux corps statiques, il garde sa propre copie des triangles, et donner
de l'exact à tout dégraderait la simulation. Il reste le bon outil pour la
collision du décor statique, c'est le « plus tard » du §1 et c'est indépendant.

**Le `.bmesh` ne bouge pas.** Le BLAS n'est jamais sérialisé : il se construit
paresseusement, à la première requête sur un mesh, en relisant son `.bmesh` —
qui contient déjà positions et indices. Conséquence qui vaut la contrainte : le
BVH n'est plus qu'un détail d'implémentation derrière l'API, donc **lib ou
maison devient réversible** et n'a pas à être tranché maintenant. Un build SAH
binné tourne autour de 1-3 M triangles/s, soit ~50 ms pour un mesh de 100k
triangles, une fois par mesh par session, et seulement pour les meshes
réellement interrogés (le TLAS a déjà réduit à quelques candidats). Si ça
hoquette un jour : build sur un thread de travail, précision AABB en attendant.
Si ça ne suffit toujours pas, la sortie est un cache de données dérivées à côté
(`<hash mesh + version builder>.bvh`), toujours pas le format d'asset.

Critère pour trancher lib/maison le jour venu : **bake d'éclairage → lib**
(débit et intersection watertight décident, tinybvh : header unique, MIT ;
pas Embree, TBB et des DLL de dizaines de Mo) ; **picking/snap/drag seulement →
BVH4+SIMD maison**, ~500 lignes, de l'ordre de 60-70 % du débit d'une lib.

- [x] **1. Plomberie AABB** — fait : `Aabb` dans `Bbox.h` (enfin sorti du
      placard), `Mesh::localBounds_` **dérivée au chargement** — un balayage
      des positions qu'on lit déjà, et le précédent existe, les tangentes ne
      sont pas stockées non plus. AABB monde dérivée à la demande
      (`transformed()`), **pas cachée** : le seul consommateur d'un cache est
      le TLAS, qui veut aussi la liste de ce qui a bougé pour son refit — les
      deux vont ensemble, donc c'est l'étape 3. S'en passer d'ici là ne coûte
      rien, une quinzaine de flops par instance.
      Validé : boîtes en place sur la scène Cornel (toggle **View > Bounds**),
      et l'identité `|linear| · halfSize` vérifiée contre la transformation
      brute des 8 coins sur 2000 matrices avec rotation, échelle négative et
      cisaillement — écart max 7e-15.
      Trouvé en chemin : `--project` n'était parsé nulle part dans l'éditeur,
      seul `--game` l'était, donc la vérification visuelle décrite dans
      CLAUDE.md ne pouvait pas marcher. Ajouté, avec `--scene`.
      **Piège pour l'étape 3** : `Transform_S` recalcule `world_` à deux
      endroits (`ensure_chain_up_to_date` et la boucle de `flushDirty`), et
      `World::update(Game&)` rappelle `transforms_->update` **après**
      `systems_->update` pour rattraper `lateUpdate`. Un cache rafraîchi depuis
      `Systems::update` serait donc en retard d'une frame sur tout ce que
      `lateUpdate` bouge : le point correct est à côté de
      `uploadRemainingFrameDirty`.
- [x] **2. L'API de requête avant la structure** — fait :
      `Spatial/SpatialIndex.h` expose `raycast(ray)`, `overlap(aabb)` et
      `overlap(centre, rayon)`, plus `rayFromScreen`. `RayHit` rend entité,
      `t`, point et normale. `World::spatialIndex()` reconstruit à la première
      requête après un changement, jamais autrement : invalidé par les hooks
      entt sur `Mesh_C` et par `Transform_S::update`, qui rend maintenant un
      `bool` — il savait déjà si quelque chose avait été flushé, il ne le
      disait pas.
- [x] **3. TLAS maison** — fait : `Spatial/Bvh.h` + `.cpp`, BVH binaire sur les
      AABB monde, build SAH binné 12 bins avec repli en feuille quand la coupe
      ne paie pas, traversée sur pile explicite avec l'enfant lointain empilé
      en premier pour que le proche puisse resserrer `tMax` avant. Pas de
      refit : reconstruction complète à l'invalidation — à quelques milliers de
      boîtes le build est sous la milliseconde, et le découpage
      statique/dynamique n'a d'intérêt qu'une fois qu'une scène le réclame.
      Validé contre un balayage brut : 1 à 5000 boîtes aléatoires, 20 000
      rayons chacun, **0 divergence** (2 égalités à 5000 boîtes — même distance,
      index différent, légitime).
      **Convention arrêtée à l'image** : pour une boîte qui contient l'origine
      du rayon, l'intersection est correcte à `t=0` — mais « la surface la plus
      proche » est alors sa face de **sortie**, pas son entrée. Sans ce choix
      l'AABB de la pièce gagne tous les clics faits depuis l'intérieur. L'autre
      convention possible (ignorer les boîtes contenant la caméra) rendrait la
      pièce impickable ; la face de sortie la garde sélectionnable en cliquant
      un mur tout en perdant contre n'importe quel objet devant.
      Il reste la limite attendue du niveau AABB : on clique la boîte, pas la
      géométrie — le trou du torus est cliquable. C'est l'étape 5.
- [x] **4. Rayon contre billboards** — fait, et le §1ter est fermé : une
      lumière se clique. `billboardQuad()` et `rayQuad()` vivent dans
      `Renderer/Billboards.h`, à côté du rendu, et reproduisent `BillboardVS`
      — les deux ne peuvent pas être partagés avec le shader, le `float3` de
      `ShaderInterop` étant un tableau nu sans arithmétique, donc c'est un
      miroir à tenir à jour.
      **Deux familles de billboards, pas une** : ceux qui sont des `Billboard_C`
      (le `SpatialIndex` les teste après le BVH, en linéaire) et **les icônes
      d'éditeur, qui n'en sont pas** — `EditorIcons` les pousse directement
      dans `Billboards` sans composant, donc l'index ne peut pas les voir.
      `EditorIcons::raycast` s'en charge, et l'éditeur prend le plus proche des
      deux ; à égalité l'icône gagne, elle est posée sur ce qu'elle marque.
      Reste non fait : lire l'alpha de la texture à l'UV touché, pour que le
      coin transparent d'une icône ne soit pas cliquable.
- [x] **Bbox de la sélection** — l'entité sélectionnée montre ses bornes en
      orange, `entityBounds()` pour un mesh ou un `Billboard_C`,
      `EditorIcons::boundsOf()` pour une icône. Validé à l'image : rayon tiré
      sur la lumière projetée à l'écran, la lumière est sélectionnée et sa
      boîte est dessinée sur son icône.
- [ ] **5. BLAS** — le jour où la précision triangle manque vraiment (cliquer
      à travers le trou d'un torus, drag-to-surface exact). Avant ça, une
      boucle brute sur les triangles des quelques candidats retenus par le
      TLAS suffit pour un clic.
- [ ] **Construction paresseuse** — `World::spatialIndex()` qui construit à la
      première requête. L'éditeur appelle dès l'ouverture d'une scène donc il
      l'a toujours ; un jeu qui n'interroge que Jolt ne paie rien. Dans un jeu
      shippé les vrais usages sont : entités sans collider (affiche, écran,
      interrupteur), impacts précis sur la surface visible, et les outils en
      jeu (construction, mode photo, modding).
- [ ] Au besoin : **grille de hash spatiale** pour du kNN sur des entités sans
      collider. ~100 lignes, le jour venu.

**Abandonné : le picking éditeur par id-buffer GPU.** Il se justifiait par
« pixel-perfect sur le mesh de rendu, les colliders Jolt sont simplifiés » — un
BLAS sur la géométrie de rendu retire exactement cette raison. Le rayon fait
mieux pour moins cher : ni target ni passe en plus, pas de readback GPU→CPU, et
il rend le point d'impact et la normale dont les autres outils ont besoin de
toute façon. Les objections habituelles ne mordent pas ici : ni skinning, ni
animation de sommets, et le seul `discard` du moteur est dans
`BillboardPS.hlsl`. **À ressortir le jour où il y aura du skinning** : un BLAS
en pose de repos est faux pour un personnage animé.

### 2b. Rendu — culling et binning

Aucun arbre ici, malgré le nom de la section : la HZB est une pyramide, les
froxels une grille régulière. Rappel : **le frustum culling ne demande aucune
structure** — en GPU-driven, un compute teste linéairement les AABB de toutes
les instances contre les 6 plans ; l'octree/BVH de culling est une optimisation
CPU d'une autre époque. Pas d'étape intermédiaire frustum CPU : elle serait
jetée au GPU-driven.

Les deux déclencheurs, pour arbitrer l'ordre le moment venu : le coût CPU des
draws n'explose vraiment qu'avec les **shadow maps** (chaque cascade re-parcourt
la scène), tandis que la boucle par pixel de `Lighting.hlsli` est en
O(pixels × lumières) et devient un mur dès la première scène sérieusement
éclairée.

- [ ] **1. GPU-driven culling two-phase Hi-Z** :
      1. **arena géométrique** : un draw indirect ne rebinde pas de buffers,
         or chaque mesh a le sien (`createStaticBuffer` par mesh) — tous les
         meshes dans un buffer partagé, offsets par mesh ; le `submeshIndex_`
         en push constant migre dans la donnée par-draw (`firstInstance`) ;
      2. frustum culling en compute + `vkCmdDrawIndexedIndirectCount` — le
         CPU passe de ~8000 commandes/frame à 2 ;
      3. pyramide de profondeur (HZB) min-depth depuis le depth buffer ;
      4. two-phase : dessiner les visibles de N-1 → construire la HZB →
         tester le reste → dessiner les faux-culls.
      Prérequis (cf. notes) : slots GPU stables — free-list au lieu de
      swap-remove.
- [ ] **2. Grille de clusters de lumières** (froxels) — chaque cellule de vue
      liste ses lumières. Prérequis de tout éclairage à N lumières ; ressert
      pour le brouillard volumétrique.

### Décision différée : RT hardware

Le TLAS/BLAS driver (`VK_KHR_acceleration_structure` + `ray_query`)
débloquerait ombres/AO/réflexions puis DDGI/ReSTIR, chaque étape « un shader
de plus ». Mais ~1/3 du parc Steam n'a pas de RT (RTX ≈ 60 %, GTX ≈ 12,5 % +
vieux AMD/iGPU) : les shadow maps devront exister de toute façon, donc le RT
n'économise rien — il s'ajoute. Décision au chantier éclairage ; rien du §2
ne l'engage — au contraire, le TLAS/BLAS software du §2a a exactement la même
forme à deux niveaux, donc la donnée serait déjà découpée comme le driver
l'attend. Pari actuel : shadow maps universelles,
RT en tier optionnel si `ray_query` présent. Alternative sans RT : SDF façon
Lumen software, beaucoup plus de code.

## 1ter. Billboards

Le besoin de départ est le picking éditeur : une lumière n'est pas seulement
impickable, elle est **invisible**. Et ce qu'on dessine est ce qu'on cliquera.
ImGui saurait poser une icône à une position projetée, mais il dessine après
la scène sans profondeur (une icône derrière un mur reste visible),
interpole ses quads de façon affine, et reconstruit ses sommets sur le CPU à
chaque frame — les trois murs qui comptent pour un usage en jeu.

- [x] **1. `shadingModel_` sur `Material`** — fait : enum (`Lit`/`Unlit`) et pas
      un booléen, pour que Subsurface ou Cloth s'ajoutent sans retoucher le
      layout ni les scènes déjà écrites. **Gratuit** : il prend la place du
      `pad_` de fin, `sizeof(Material) == 48` ne bouge pas.
      Occasion prise pour sortir l'éclairage dans `Shaders/Lighting.hlsli` —
      bindings partagés, helpers PBR et un `ShadeSurface(shadingModel, Surface)`
      qui porte l'early-out unlit. Les deux pixel shaders l'appellent : une
      seule définition de l'éclairage, même principe que `ShaderInterop.h` pour
      les structs.
- [x] **2. Rendu billboard** — fait : `Renderer/Billboards` (ni Vulkan ni ECS,
      possédé par `Engine`, atteint par `world.billboards()`), même contrat que
      `DebugDraw` — collecte immédiate, `requestUpload` dans `endFrame`, purge
      par durée de vie. Un `BillboardGPUData` de 48 octets par quad, aucun
      vertex input : six sommets par instance, les coins viennent de
      `SV_VertexID % 6` et la base caméra du `CameraGPUData`.
      Deux axes de généralité, parce que ce sont de vrais choix binaires :
      taille **monde** ou **fraction de hauteur d'écran** (l'icône garde sa
      taille à toute distance — le VS annule la division perspective avec
      `dist * tan(fov/2)`), orientation **sphérique** ou **cylindrique**
      verrouillée sur Y. Écartés tant qu'aucun besoin ne les réclame : atlas,
      frames d'animation, rotation par billboard.
      **Alpha-test, pas blending** : pas de tri arrière-vers-avant par frame,
      la profondeur est écrite comme pour une surface opaque, et le tri ne
      viendra pas gêner le GPU-driven du §2. Dessinés avant le ciel, dont le
      test `LESS_OR_EQUAL` laisse alors leurs pixels tranquilles.
      Le matériau est optionnel : un `textureIdx_` bindless surcharge la carte
      albedo, donc l'appelant peut dessiner un PNG sans écrire de `.bmat`.
      Un `__unlit_material` est créé avec le matériau par défaut.
      Piège rencontré : `FrameSetBindings` vérifie que chaque binding du frame
      set a un pool derrière lui — un nouveau buffer possédé par `ScenePasses`
      doit être déclaré dans `nonPool`, sinon le static_assert tombe.
- [x] **3. Les deux alimentations** — fait, et c'est la décision structurante :
      **immédiat pour l'éditeur, composant pour le jeu**, un seul buffer et un
      seul draw derrière.
      Une icône d'éditeur n'est pas de la donnée de scène : ni sauvée, ni
      sélectionnable, ni snapshotée au Play, ni hot-reloadée. La mettre dans
      l'ECS aurait obligé à l'exclure d'`EntitySerializer`, de `ScenePanel`,
      de l'inspecteur et de `resetScene` — cinq exceptions « sauf celui-là »
      dans des boucles génériques, le signe que la donnée est au mauvais
      endroit. Un sprite de jeu, lui, est authoré et sauvé : `Billboard_C` +
      `Billboard_S` qui le repousse en immédiat chaque frame.
      Limite assumée : `Billboard_S` refait le travail à chaque frame comme
      `DebugDraw`. Un vrai pool d'instances serait meilleur à des milliers de
      sprites persistants ; il remplira le même buffer, donc rien à jeter.
- [x] **4. Icônes éditeur** — fait : `Editor/EditorIcons`, taille écran.
      Aucun fichier d'icône : les glyphes sont **rasterisés au lancement**
      depuis `assets/MaterialIcons-Regular.ttf` avec `stb_truetype` (déjà
      vendoré, `STBTT_STATIC` pour ne pas croiser la copie d'ImGui), aux
      codepoints d'`IconsMaterialDesign.h`. L'icône du viewport est donc le
      glyphe que l'arbre de scène affiche déjà pour cette entité, et la police
      étant déjà livrée, ni dépendance ni obligation nouvelle — une banque
      d'icônes tierce a été écartée pour ça : attribution visible exigée, et
      redistribution des fichiers dans un dépôt public non tranchée.
      Les textures ne passent pas par l'`AssetManager` : ce ne sont pas des
      assets de projet, juste `createImage2D` + `requestTextureUpload` +
      `textureIndex`, détruites dans `~EditorIcons` (l'`Engine` qui possède le
      ResourceManager survit à l'`App`, donc l'ordre tient).
      Pixels blancs, couverture du glyphe dans l'alpha : une seule texture
      sert toutes les couleurs de lumière, la teinte est par instance.
      Ce que ça coûte : on ne peut plus remplacer une icône en déposant un
      PNG, le jeu d'icônes est celui de la police. L'icône de lumière est teintée par la couleur de la
      lumière. La caméra **active** est sautée : elle est à l'œil, son icône
      remplirait l'écran ou passerait derrière le near plane. Toggle
      **View > Icons**.
      Validé à l'image sur `GameExemple` par un harnais temporaire (retiré) :
      icône lumière orange et icône caméra à taille constante, caméra de
      l'éditeur sans icône, et un `Billboard_C` vert en taille monde — découpe
      alpha nette sur les rayons du soleil, couleur non assombrie donc unlit
      effectif.
- [ ] **Reste à faire** : rendre les billboards cliquables, qui est la raison
      d'être de tout ça. Plus d'id-buffer (cf. §2a) : un test rayon/quad
      analytique, la même construction que le VS, à lire depuis
      `Renderer/Billboards.h` pour que les deux ne divergent pas.

## 3. Restes

- [ ] **Hot reload : snapshot binaire + hash de layout** (remplace le JSON) —
      snapshot memcpy par pool (composants trivially copyable), hash de layout
      par type (nom+type+offset, tout est dans le registry) ; type inchangé →
      restore memcpy, type modifié → migration champ-par-champ payée par ce
      type seul. Coût dominé par le link, indépendant de la taille de scène.
- [ ] **`findByName`** — la seule requête qui reste côté moteur.
- [ ] **Budget de staging par frame** — un débordement lève au lieu de
      corrompre, mais une frame lourde (gros import) tue le process.
      Allocateur de staging par blocs recyclés derrière une fence.

---

## Notes / vigilance (pas des tâches)

- **Règle DLL jeu : zéro état statique** — tout état durable vit dans le World
  (composants plats). Un static dans la DLL meurt au reload.
- **IDs GPU instables** (swap-remove dans les pools) : correct aujourd'hui,
  mais le culling GPU-driven, un historique TAA ou un picking différé
  persistent des index entre frames → slots stables + free-list (§2.2).
- Le triple-buffering des instance buffers est assumé (`FramesInFlight = 3`,
  simplicité/sécurité).
- Composants avec `std::string`/`std::vector` : permis depuis les formes
  multiples (§1.5), mais à réserver aux composants froids — les composants
  lus en boucle par un système ou un pool GPU restent des valeurs plates.
- **Descriptor layouts câblés à la main** (`FrameSetBindingCount`, set
  bindless) : la réflexion SPIR-V les rendrait dérivables des shaders, comme
  une UI matériaux auto-générée. Même philosophie que `BATAP_COMPONENT`. À
  faire quand le nombre de bindings fera mal.
