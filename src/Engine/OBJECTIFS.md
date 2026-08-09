# Objectifs

Feuille de route issue de la revue d'architecture (août 2026).
Fil conducteur : réduire ce qu'un dev doit toucher pour ajouter un composant,
et faire du composant de jeu un citoyen de première classe partout
(éditeur, sérialisation, play mode, hot reload). Tout repose sur le même
investissement déjà en place : la réflexion (`BATAP_COMPONENT` + `ComponentRegistry`).

---

## 1. Hygiène immédiate (quelques heures, aucun risque)

- [ ] **Header interop C++/HLSL** — `InstanceData` existe en 3 copies synchronisées
      à la main (`StaticMeshGPUData` C++, `VertexShader.hlsl`, `PixelShader.hlsl`).
      Un seul header partagé (`#ifdef __cplusplus` pour `float4x4` ↔ `float[16]`),
      inclus par le C++ et les shaders. C'est le seul vrai risque de corruption
      silencieuse identifié. Règles à écrire dans le header : structs de données
      uniquement (jamais de bindings — `register()` reste dans les shaders),
      padding explicite, pas de `float3` nu, `static_assert` sizeof côté C++,
      convention matricielle documentée. Ce sous-ensemble a le même layout en
      D3D12 et en Vulkan → le header survivra tel quel à un port.
- [ ] **Migration FXC → DXC** — `Shaders.cpp` utilise `D3DCompileFromFile` (legacy :
      SM 5.x max, pas de HLSL 2021, pas de SPIR-V). Passer à `IDxcCompiler3` :
      mécanique, mêmes sources HLSL. Préalable au point suivant, au hot reload
      shaders (§5) et à tout port Vulkan (DXC = le chemin HLSL → SPIR-V).
- [ ] **Validation des layouts par réflexion shader** — au chargement d'un shader,
      comparer le layout reflété (`IDxcUtils::CreateReflection` : offset/taille de
      chaque champ d'`InstanceData`) avec `offsetof`/`sizeof` C++. Mismatch → erreur
      franche au démarrage nommant le champ. Complète le header interop : le header
      garantit les *sources*, la réflexion vérifie le *binaire chargé* (cache
      périmé, oubli de rebuild, futur hot reload). ~50 lignes. Design : logique de
      comparaison écrite contre une représentation neutre (`{name, offset, size}`
      par champ), remplie par un extracteur par backend — DXC/DXIL aujourd'hui,
      SPIRV-Reflect le jour de Vulkan (les offsets sont des décorations
      obligatoires du SPIR-V : la capacité est garantie, seul le lecteur change).
- [ ] **Round-trip des composants inconnus** dans `EntitySerializer` — aujourd'hui
      `if (!ct) continue;` au load + save reflété = un composant non enregistré
      dans le binaire est **silencieusement détruit** à la sauvegarde. Conserver
      le blob JSON tel quel et le réémettre. Protège aussi entre versions du moteur.
- [ ] **Supprimer `RenderInstance_C`** — écrit dans chaque factory, jamais lu,
      et de toute façon périmé dès qu'un swap-remove déplace l'entité dans le pool.
- [ ] **`GPUInstanceID` par défaut = invalide** — défaut actuel `value = 0` mais
      `valid()` teste contre `uint32_max` : un ID défaut pointe le slot 0. Initialiser à max.

## 2. Simplification du pipeline composant (1-2 jours)

Objectif final : ajouter un composant GPU-visible = le header du composant,
un bloc compact dans `instanceDeclaration.h` (struct interop + un `fill` + une
ligne de déclaration), un bit de `ComponentFlag`. Trois fichiers, zéro plomberie.

- [ ] **Patches partiels → un `fill()` unique par instance** — les structs GPU font
      40-224 octets : ré-uploader la struct entière coûte des miettes de bande
      passante et supprime `PatchDesc`/`PatchRange`/`byBit` + le risque de
      désynchronisation offset/layout. Moins de petits `CopyBufferRegion` en prime.
- [ ] **Pools peuplés par hooks entt** (`on_construct`/`on_destroy` par composant
      miroir) — la présence du composant *devient* l'appartenance au pool.
      Supprime le câblage manuel des factories (aujourd'hui un `emplace<Mesh_C>`
      hors factory = entité jamais rendue, silencieusement).
- [ ] **Kind dérivé des composants** — `markDirty` route par
      `(pool.usedFlags & flag) && pool.contains(handle)` au lieu de `Kind_C`;
      `destroy` fait un `forEach` (remove est déjà no-op si absent). `EntityKind`,
      `Kind_C`, `kindName()` et le switch `if (kind == "...")` du load disparaissent.
      Le load devient : créer l'entité, appliquer les composants reflétés, fin.
      (Le modèle single-aspect par entité est conservé — assemblage par hiérarchie ;
      un assert dans le hook « déjà dans un autre pool » garantit l'invariant.)
- [ ] **Factories → spawnables data-driven** — `createStaticMesh` etc. deviennent
      des listes de composants (nom affiché + composants à emplacer). Le menu de
      `ScenePanel` boucle dessus comme l'inspecteur boucle sur `ComponentRegistry::all()`.
- [ ] **Règle officielle : un composant est un agrégat trivially copyable** —
      `static_assert` à l'enregistrement. C'est la contrainte qui rend possibles
      à la fois les pools GPU simples et le hot reload memcpy (§5). Payée une fois.
- [ ] `ComponentFlag` reste manuel (une ligne d'enum) — l'auto-dériver coûterait
      la constexpr-ness (`UsedComposents`, tables) pour un gain nul.

## 3. Éditeur en bibliothèque (le modèle Unity/UE, version statique)

Problème résolu : le `ComponentRegistry` vit par binaire ; l'éditeur ne connaît
pas les composants du jeu → perte de données (mitigée par le round-trip du §1,
réglée pour de bon ici).

- [ ] **`src/Editor` → lib `Batap_Editor`** avec un point d'entrée `runEditor(cfg)`.
- [ ] **L'éditeur standalone** redevient un exe trivial (main de 3 lignes).
- [ ] **Chaque jeu déclare une cible `MyGame_Editor`** : `editor_main.cpp` qui
      inclut `GameComponents.h` (header agrégateur, par convention) + `runEditor()`.
      L'init statique enregistre les composants du jeu dans le binaire éditeur :
      inspecteur, menu add-component et sérialisation marchent nativement,
      zéro schéma, zéro manifeste.

## 4. Play / Stop (modèle snapshot, à la Unity)

- [ ] **Interface `Game { init(World&); update(World&, Frame&); }`** — sortir la
      logique du `main()` du jeu pour que l'éditeur puisse la piloter. Le `main()`
      du jeu devient : boucle moteur + `game.update()`. C'est le seul vrai chantier.
- [ ] **Play** : `save(world)` → buffer mémoire ; les systèmes du jeu tickent.
      **Stop** : clear registry + pools GPU → `load(buffer)`.
- [ ] Vider/remapper la sélection éditeur au Stop (les `EntityHandle` meurent).
- [ ] **Bouton « Run » en process séparé** (~20 lignes : scène temp + spawn du jeu) —
      un jeu qui crashe n'emporte pas l'éditeur. Complémentaire, pas concurrent.

## 5. Hot reload

Ordre : shaders d'abord (indépendant, petit), code ensuite (réutilise §4).

- [ ] **Shaders** : watch mtime sur le dossier shaders + recompilation/rebind.
      Pattern hôte du template Odin directement transposable. S'appuie sur DXC +
      la validation par réflexion (§1) : un shader rechargé avec un layout
      incompatible est rejeté avec une erreur claire au lieu de corrompre le rendu.
- [ ] **Jeu en DLL** + boucle hôte : copie de la DLL avant chargement (verrou
      fichier Windows), watch mtime, versionnage `game_{n}.dll`, swap de la table
      de fonctions. Ré-enregistrement propre du `ComponentRegistry` au reload
      (ses function pointers pointent dans la DLL).
- [ ] **Préservation d'état par snapshot binaire + hash de layout** — PAS de JSON,
      PAS de handoff de pointeur brut (pattern Odin : garde des pointeurs vers la
      vieille DLL, interdit de la décharger, casse si une struct change) :
      - snapshot binaire par pool (memcpy — composants trivially copyable, cf. §2) ;
      - au reload, hash du layout par type (champs : nom+type+offset, tout est
        dans le registry) ;
      - type inchangé → restore memcpy ; type modifié → migration champ-par-champ
        (le mécanisme de désérialisation existant), payée seulement par ce type.
      Coût dominé par le link de la DLL (~50-100 ms), indépendant de la taille de
      la scène. Les assets/GPU vivent côté hôte : jamais rechargés.
- [ ] entt à travers la frontière DLL : type ids stables (`ENTT_STANDARD_CPP`).
- [ ] Règle côté jeu : pas d'état statique dans la DLL (tout état vit dans le World).

---

## Notes / vigilance (pas des tâches)

- **IDs GPU instables** (swap-remove dans les pools) : correct aujourd'hui, mais
  dès que quelque chose côté GPU persiste un index entre frames (culling GPU-driven,
  historique TAA, picking différé), il faudra des slots stables + free-list.
- Le triple-buffering des instance buffers est assumé (choix simplicité/sécurité).
- Composants avec `std::string`/`std::vector` : basculent dans le chemin migration
  du hot reload même à layout constant — à éviter par design (handles + valeurs plates).
- La réflexion shader ouvre deux chantiers pour plus tard : générer les root
  signatures / descriptor layouts depuis les shaders au lieu de les câbler à la
  main (quasi obligatoire pour Vulkan), et l'UI matériaux auto-générée depuis les
  cbuffers (même philosophie que `BATAP_COMPONENT` : source de vérité unique).
