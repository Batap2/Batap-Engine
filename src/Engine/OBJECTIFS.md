# Objectifs

**État (2026-09-02)** : le port Vulkan est fini et mergé sur `main` — un seul
backend (`Renderer/Vulkan/`), DX12 supprimé. Ce plan reprend la revue
d'architecture d'août 2026 et l'ancien suivi `TODO.md`, revérifiés ligne à ligne
contre le code d'aujourd'hui.

Fil conducteur inchangé : réduire ce qu'un dev doit toucher pour ajouter un
composant, et faire du composant de jeu un citoyen de première classe partout
(éditeur, sérialisation, play mode, hot reload). Tout repose sur le même
investissement déjà en place : la réflexion (`BATAP_COMPONENT` +
`ComponentRegistry`).

---

## 0. Déjà fait (ne pas re-proposer)

- **FXC → DXC** — fait et dépassé : `dxc` compile les HLSL en SPIR-V au build
  (`batap_compile_shader` dans `src/Engine/CMakeLists.txt`) et au runtime via
  `libdxcompiler` chargée en dlopen (`VulkanShaderCompiler`).
- **Hot reload shaders** — `ScenePasses::checkHotReload()` : mtime sur les
  sources HLSL de l'arbre, recompilation runtime, rebuild des pipelines hors
  enregistrement de commandes (`vkDeviceWaitIdle`).
- **Corruption du ring d'upload** — `requestUpload`/`requestPartialUpload`
  rendent un span dans le staging de la frame ; un débordement lève une
  exception franche au lieu d'écraser une zone en vol. Reste le budget (§6).
- **Shutdown** — `resources_`/`scenePasses_` en `unique_ptr`, `vkDeviceWaitIdle`
  dans `Renderer::~Renderer`.
- **Header interop C++/HLSL** — `Shaders/ShaderInterop.h` : `CameraGPUData`,
  `StaticMeshGPUData`, `PointLightGPUData`, `Material`, `SkyboxGPUData`,
  `DrawPush` et les numéros de set/binding y sont déclarés une fois, compilés
  en HLSL par dxc et en C++ par le moteur (`static_assert` de taille et
  d'offset côté C++). Les copies dans les `.hlsl` ont disparu.
- Divers : accesseurs d'input (`down`/`pressed`/`released`/`wheel`),
  `EntityHandle::get()` n'est plus `noexcept`-et-throw, `GameExemple/` a
  enfin du code (`main.cpp`), convention de nommage `var_` passée partout,
  `Hierarchy_S::setParent` implémenté.

## 1. Hygiène immédiate (quelques heures, aucun risque)

- [ ] **Validation des layouts par réflexion SPIR-V** — au chargement d'un
      shader, comparer le layout reflété (offset/taille de chaque champ) avec
      `offsetof`/`sizeof` C++. Mismatch → erreur franche au démarrage nommant le
      champ. Le header garantit les *sources*, la réflexion vérifie le *binaire
      chargé*, ce que `ShaderInterop.h` ne couvre pas : `.spv` périmé, oubli de
      rebuild, shader rechargé à chaud contre un C++ non recompilé. Les offsets
      sont des décorations obligatoires du SPIR-V : soit SPIRV-Reflect (deux fichiers),
      soit un lecteur maison des décorations `Offset`/`ArrayStride` (~150 lignes,
      cohérent avec « minimum de libs »). Logique de comparaison écrite contre
      une représentation neutre `{name, offset, size}`.
- [ ] **Round-trip des composants inconnus** dans `EntitySerializer`
      (`EntitySerializer.cpp:205`) — aujourd'hui `if (!ct) continue;` au load +
      save reflété = un composant non enregistré dans le binaire est
      **silencieusement détruit** à la sauvegarde. Conserver le blob JSON tel
      quel et le réémettre. Protège aussi entre versions du moteur.
- [ ] **Supprimer `RenderInstance_C`** — encore `emplace` dans les quatre
      factories (`EntityFactory.cpp:50,67,85,100`), jamais lu, et de toute façon
      périmé dès qu'un swap-remove déplace l'entité dans le pool.
- [ ] **`GPUInstanceID` par défaut = invalide** — `InstanceManager.h:28` :
      défaut `value = 0` mais `valid()` teste contre `uint32_max`, donc un ID
      défaut pointe le slot 0. Initialiser à max.
- [ ] **`Transform_S::setParent` déclaré, jamais défini** (`Transform_S.h:39`) —
      erreur de link à la première utilisation. `Hierarchy_S::setParent` existe :
      soit y porter le `keepWorld`, soit supprimer la déclaration morte.
- [ ] **`FreeCamController_C.h`** : ni `#pragma once` ni `namespace batap` —
      seul composant dans ce cas.
- [ ] **Supprimer `include/DirectX-Headers`** (3.8 Mo) — reste du backend DX12,
      plus référencé par aucun CMake.

## 2. Simplification du pipeline composant (1-2 jours)

Objectif final : ajouter un composant GPU-visible = le header du composant,
un bloc compact dans `instanceDeclaration.h` (struct interop + un `fill` + une
ligne de déclaration), un bit de `ComponentFlag`. Trois fichiers, zéro plomberie.

- [ ] **Patches partiels → un `fill()` unique par instance** — les structs GPU
      font 40-224 octets : ré-uploader la struct entière coûte des miettes de
      bande passante et supprime `PatchDesc`/`PatchRange`/`byBit` + le risque de
      désynchronisation offset/layout. Moins de petits `vkCmdCopyBuffer` en prime.
- [ ] **Pools peuplés par hooks entt** (`on_construct`/`on_update`/`on_destroy`
      par composant miroir) — la présence du composant *devient* l'appartenance
      au pool, et une modification hors factory marque dirty. Aujourd'hui aucun
      hook n'existe : un `emplace<Mesh_C>` hors factory = entité jamais rendue,
      silencieusement.
- [ ] **Kind dérivé des composants** — `markDirty` route par
      `(pool.usedFlags & flag) && pool.contains(handle)` au lieu de `Kind_C`;
      `destroy` fait un `forEach` (remove est déjà no-op si absent). `EntityKind`,
      `Kind_C`, `kindName()`, `GPUKinds` et le switch `if (kind == "...")` du load
      disparaissent. Le load devient : créer l'entité, appliquer les composants
      reflétés, fin. (Le modèle single-aspect par entité est conservé —
      assemblage par hiérarchie ; un assert dans le hook « déjà dans un autre
      pool » garantit l'invariant.)
- [ ] **Factories → spawnables data-driven** — `createStaticMesh` etc. deviennent
      des listes de composants (nom affiché + composants à emplacer). Le menu de
      `ScenePanel` boucle dessus comme l'inspecteur boucle sur
      `ComponentRegistry::all()`.
- [ ] **Règle officielle : un composant est un agrégat trivially copyable** —
      `static_assert` à l'enregistrement (il n'existe aujourd'hui que sur les
      `GPUData`, `instanceDeclaration.h:89`). C'est la contrainte qui rend
      possibles à la fois les pools GPU simples et le hot reload memcpy (§5).
- [ ] `ComponentFlag` reste manuel (une ligne d'enum) — l'auto-dériver coûterait
      la constexpr-ness (`UsedComposents`, tables) pour un gain nul.

## 3. Éditeur en bibliothèque (le modèle Unity/UE, version statique)

Problème résolu : le `ComponentRegistry` vit par binaire ; l'éditeur ne connaît
pas les composants du jeu → perte de données (mitigée par le round-trip du §1,
réglée pour de bon ici). État actuel : `Batap_Editor` est un `add_executable`
qui glob `src/Editor/*` — il n'y a rien à lier pour un jeu.

- [ ] **`src/Editor` → lib `Batap_Editor`** avec un point d'entrée `runEditor(cfg)`.
- [ ] **L'éditeur standalone** redevient un exe trivial (le `main.cpp` actuel,
      wWinMain/main + try/catch, devient trois lignes).
- [ ] **Chaque jeu déclare une cible `MyGame_Editor`** : `editor_main.cpp` qui
      inclut `GameComponents.h` (header agrégateur, par convention) + `runEditor()`.
      L'init statique enregistre les composants du jeu dans le binaire éditeur :
      inspecteur, menu add-component et sérialisation marchent nativement,
      zéro schéma, zéro manifeste.

## 4. Play / Stop (modèle snapshot, à la Unity)

- [ ] **Interface `Game { init(World&); update(World&, Frame&); }`** — sortir la
      logique du `main()` du jeu pour que l'éditeur puisse la piloter. Le
      `main()` de `GameExemple` (boucle `nextFrame()` + `world.update()`) devient
      boucle moteur + `game.update()`. C'est le seul vrai chantier.
- [ ] **Play** : `save(world)` → buffer mémoire ; les systèmes du jeu tickent.
      **Stop** : clear registry + pools GPU → `load(buffer)`.
- [ ] Vider/remapper la sélection éditeur au Stop (les `EntityHandle` meurent).
- [ ] **Bouton « Run » en process séparé** (~20 lignes : scène temp + spawn du jeu) —
      un jeu qui crashe n'emporte pas l'éditeur. Complémentaire, pas concurrent.

## 5. Hot reload du code (les shaders sont faits, cf. §0)

- [ ] **Jeu en bibliothèque dynamique** + boucle hôte : watch mtime, swap de la
      table de fonctions, ré-enregistrement propre du `ComponentRegistry` au
      reload (ses function pointers pointent dans la lib). Sur mac : `dlopen`
      d'un `.dylib`, pas de verrou fichier — la copie avant chargement et le
      versionnage `game_{n}` sont des contournements Windows, à garder derrière
      un `#ifdef` le jour du portage.
- [ ] **Préservation d'état par snapshot binaire + hash de layout** — PAS de JSON,
      PAS de handoff de pointeur brut (pattern Odin : garde des pointeurs vers la
      vieille lib, interdit de la décharger, casse si une struct change) :
      - snapshot binaire par pool (memcpy — composants trivially copyable, cf. §2) ;
      - au reload, hash du layout par type (champs : nom+type+offset, tout est
        dans le registry) ;
      - type inchangé → restore memcpy ; type modifié → migration champ-par-champ
        (le mécanisme de désérialisation existant), payée seulement par ce type.
      Coût dominé par le link (~50-100 ms), indépendant de la taille de la scène.
      Les assets/GPU vivent côté hôte : jamais rechargés.
- [ ] entt à travers la frontière dynamique : type ids stables (`ENTT_STANDARD_CPP`).
- [ ] Règle côté jeu : pas d'état statique dans la lib (tout état vit dans le World).

## 6. Ergonomie moteur (repris de l'ancien `TODO.md`)

Indépendant des chantiers ci-dessus, à prendre à la pièce.

- [ ] **Façade jeu + header parapluie `batap.h`** — aujourd'hui `World` n'expose
      que `update`/`loadScene`/`renderArgs` et ses `unique_ptr` publics : écrire
      du gameplay c'est `world.systems_->transforms_->setLocalPosition(...)` et
      neuf includes. Viser `spawn`/`setPosition`/`input`/`load`.
- [ ] **`loadAsset<T>()` typé** au lieu de `optional<AssetHandleAny>` + `std::get`
      (`Assets/AssetLoader.h:20`).
- [ ] **Enregistrement de systèmes utilisateur** — `Systems::update` est en dur
      (deux `unique_ptr` membres), un jeu ne peut pas ajouter son système.
- [ ] **Requêtes** : pas de raycast, pas de `findByName`, pas de query spatiale
      (`Bbox.hpp` existe, inutilisé).
- [ ] **Timestep fixe, pause, timescale** — il n'y a que `Engine::deltaTime_`.
- [ ] **`v3f`/`m4f`/`quatf`/`transform` dans le namespace global** (`EigenTypes.h`).
- [ ] **`EntityHandle::emplace<T>()` ne transmet pas d'arguments**, et pas de
      surcharges `const` sur `get`/`try_get`.
- [ ] **Budget de staging par frame** — un débordement lève désormais au lieu de
      corrompre, mais une frame lourde (import d'un gros mesh) tue le process.
      Allocateur de staging par blocs recyclés derrière une fence.

---

## Notes / vigilance (pas des tâches)

- **IDs GPU instables** (swap-remove dans les pools) : correct aujourd'hui, mais
  dès que quelque chose côté GPU persiste un index entre frames (culling GPU-driven,
  historique TAA, picking différé), il faudra des slots stables + free-list.
- Le triple-buffering des instance buffers est assumé (`FramesInFlight = 3`,
  choix simplicité/sécurité).
- Composants avec `std::string`/`std::vector` : basculent dans le chemin migration
  du hot reload même à layout constant — à éviter par design (handles + valeurs plates).
- **Descriptor layouts câblés à la main** (`FrameSetBindingCount` côté scène, set
  bindless côté `ResourceManager`) : la réflexion SPIR-V du §1 les rendrait
  dérivables des shaders. Même chose pour une UI matériaux auto-générée depuis
  les structs de matériau. Même philosophie que `BATAP_COMPONENT` : source de
  vérité unique. À faire quand le nombre de bindings commencera à faire mal.
- Le chemin Windows (Win32Window, surface Win32, `dxcompiler.dll`) est écrit mais
  n'a jamais été compilé : prévoir une passe de fix, pas une réécriture.
