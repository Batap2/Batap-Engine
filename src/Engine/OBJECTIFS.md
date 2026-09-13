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

## 2. Structures d'accélération (rendu)

Le partage est réglé par le §1 : Jolt possède la seule structure CPU et ne
voit que les colliders. À nous le côté rendu — trois structures, dans l'ordre.
Rappel : **le frustum culling ne demande aucune structure** — en GPU-driven,
un compute teste linéairement les AABB de toutes les instances contre les 6
plans ; l'octree/BVH de culling est une optimisation CPU d'une autre époque.
Pas d'étape intermédiaire frustum CPU : elle serait jetée au GPU-driven.

- [ ] **1. Plomberie AABB** — le socle. AABB locale par mesh calculée à
      l'import (stockée dans le `.bmesh`), AABB monde par instance recalculée
      quand le transform change (le dirty-marking sait déjà quand).
      `Bbox.hpp` sort enfin du placard.
- [ ] **2. GPU-driven culling two-phase Hi-Z** :
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
- [ ] **3. Grille de clusters de lumières** (froxels) — chaque cellule de vue
      liste ses lumières. Prérequis de tout éclairage à N lumières ; ressert
      pour le brouillard volumétrique.
- [ ] **Picking éditeur par id-buffer GPU** — les ids d'entité rendus dans une
      petite target, lecture du pixel sous la souris. Pixel-perfect sur le
      mesh de rendu (les colliders Jolt sont simplifiés).
- [ ] Au besoin : **grille de hash spatiale** pour du kNN sur des entités sans
      collider. ~100 lignes, le jour venu.

### Décision différée : RT hardware

Le TLAS/BLAS driver (`VK_KHR_acceleration_structure` + `ray_query`)
débloquerait ombres/AO/réflexions puis DDGI/ReSTIR, chaque étape « un shader
de plus ». Mais ~1/3 du parc Steam n'a pas de RT (RTX ≈ 60 %, GTX ≈ 12,5 % +
vieux AMD/iGPU) : les shadow maps devront exister de toute façon, donc le RT
n'économise rien — il s'ajoute. Décision au chantier éclairage ; les trois
structures ci-dessus n'engagent rien. Pari actuel : shadow maps universelles,
RT en tier optionnel si `ray_query` présent. Alternative sans RT : SDF façon
Lumen software, beaucoup plus de code.

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
- Composants avec `std::string`/`std::vector` : interdits par design (handles
  + valeurs plates) — le `static_assert` de l'enregistrement les refuse.
- **Descriptor layouts câblés à la main** (`FrameSetBindingCount`, set
  bindless) : la réflexion SPIR-V les rendrait dérivables des shaders, comme
  une UI matériaux auto-générée. Même philosophie que `BATAP_COMPONENT`. À
  faire quand le nombre de bindings fera mal.
