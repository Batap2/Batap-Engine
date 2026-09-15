# Objectifs

**État (2026-09-11)** : Vulkan only. Pipeline composant réflexif
(`BATAP_COMPONENT`), éditeur-lib, Play/Stop, hot reload du jeu (DLL + snapshot
JSON), façade gameplay (`EntityHandle`/`World`, `batap.h`), timestep fixe —
tout ça est fait ; l'historique détaillé vit dans git. Fil conducteur
inchangé : réduire ce qu'un dev doit toucher, une seule source de vérité par
concept.

---

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


## 4. Occlusion et indirect diffus en espace écran — visibility bitmask

Le papier est dans `papers/` : *Screen Space Indirect Lighting with Visibility
Bitmask* (Therrien, Levesque, Gilet — The Visual Computer, 2023). Il remplace
les deux angles d'horizon de GTAO par un champ de 32 bits qui décrit l'état
occulté/libre de 32 secteurs de la tranche d'hémisphère. Même coût
d'échantillonnage qu'une recherche d'horizon, mais la lumière passe derrière
les surfaces fines, et le même parcours donne l'AO, l'ambiant directionnel et
un rebond diffus.

Acté : on prend ce papier et pas GTAO. L'écart est d'une quinzaine
d'instructions par échantillon (0.01-0.02 ms mesuré par les auteurs) et ce
qu'il corrige — halos, sur-occlusion derrière une grille ou du feuillage — est
exactement ce qui se voit. XeGTAO reste la référence à lire pour le squelette
de la passe (distribution des directions, jitter, dénoiseur), pas pour
l'intégration.

Le moteur n'a rien de ce qu'il faut, et c'est l'essentiel du travail : **une
seule scope de rendu** (`Renderer::render` ouvre un `vkCmdBeginRendering`
swapchain+profondeur où tout se record, ImGui compris), **profondeur illisible**
(`usage = DEPTH_STENCIL_ATTACHMENT` seul, `storeOp = DONT_CARE`), **pas de
buffer de normales**, **pas de HDR** (`PixelShader.hlsl` termine par `saturate`
dans une swapchain UNORM). Les étapes 1 à 3 sont de la plomberie qui resservira
bien au-delà de l'AO ; le papier ne commence qu'à l'étape 4.

Découpage : **l'AO d'abord (1-6), l'ambiant directionnel ensuite (7), l'indirect
diffus en dernier (8)** — le rebond réclame un buffer de lumière HDR, un
tonemapping et de la réprojection temporelle, donc un chantier à part qui n'a
pas à bloquer l'AO.

Tout en pixel shader plein écran, pas en compute : l'algorithme est parallèle
par pixel, sans mémoire partagée ni coopération entre threads, et une pipeline
compute serait une deuxième famille à porter (layout, barrières, hot reload)
pour zéro gain. Le jour où un dénoiseur voudra de la LDS, ce sera un vrai
argument — pas avant.

- [ ] **1. Plusieurs scopes de rendu** — une passe plein écran qui *lit* la
      profondeur ne peut pas vivre dans la scope qui l'écrit. Sortir le
      begin/end de `Renderer::render` : le `Renderer` passe la vue swapchain et
      la vue profondeur à `ScenePasses::record`, qui ouvre les siennes, et garde
      pour lui la scope ImGui, après la scène. Validable seul : l'image ne doit
      pas bouger d'un pixel.
- [ ] **2. Cibles de rendu dans `ResourceManager`** — `createImage2D` est taillé
      pour les textures uploadées : chaîne de mips complète, slot bindless,
      `SAMPLED|TRANSFER_*` et rien d'autre. Ajouter un `createRenderTarget` — un
      seul mip, `COLOR_ATTACHMENT|SAMPLED`, aucun chemin d'upload, recréé dans
      le `onResize`. Il garde le slot bindless, et c'est structurant : le frame
      set est **storage buffers uniquement** par construction
      (`createFrameSetLayout` câble `STORAGE_BUFFER` sur ses
      `FrameSetBindingCount` bindings), donc une texture produite par une passe
      se lit par son index bindless, passé en push constant — pas par le frame
      set.
- [ ] **3. Prepass profondeur + normales** — SSILVB lit la profondeur complète
      de la scène *avant* l'éclairage ; un forward ne l'a pas. Une passe
      depth-only (le VS de la géométrie, pas de PS) avec une cible normales en
      MRT, normale géométrique en view space. Trois conséquences : la profondeur
      passe en `SAMPLED` avec `storeOp = STORE`, la passe géométrie passe en
      `EQUAL` sans écriture de profondeur (et gagne l'overdraw au passage), et
      le prepass doit dessiner **exactement** les mêmes triangles que la
      géométrie, sinon z-fighting. La normale de normal map est écartée : elle
      obligerait le prepass à lire matériau, UV et tangentes, et la normale
      géométrique suffit à de l'occlusion. Dériver la normale de la profondeur
      (ddx/ddy) éviterait la cible mais casse sur les silhouettes — c'est la
      sortie de secours si le MRT coûte trop.
- [ ] **4. La passe bitmask** — triangle plein écran, `FullscreenVS` + `SsgiPS`
      à déclarer dans `ShaderCatalog.h` (le hot reload recompile tout
      `kShaders` : un shader non déclaré ne se recharge pas). Sortie `R8_UNORM`.
      L'algorithme 1 du papier tel quel : Nd directions, Ns pas de chaque côté,
      position view space reconstruite depuis la profondeur, bitmask centré sur
      la normale projetée, `a, b` de la ligne 18 puis
      `b_j = ((1 << b) - 1) << a`, `AO = 1 - countbits(b_i) / 32`.
      **32 secteurs**, valeur du papier : ça tient dans un `uint`, et 128 coûte
      5-10 % pour un gain invisible. Épaisseur constante t ≈ 0.2 en unités
      monde, avec l'option de la faire croître linéairement avec la distance.
      Les paramètres (rayon, t, Ns, Nd) vivent dans un `Ssgi_C` posé sur une
      entité, comme `Skybox_C` porte le ciel.
      **Pièges** : `setViewportYUp` pose un viewport de hauteur négative, donc
      le signe de Y entre clip space, UV et gradient d'écran se vérifie sur une
      image et pas au raisonnement ; et `CameraGPUData` n'a aucune matrice
      inverse — reconstruire le rayon depuis `right_/up_/fwd_/fov_/znear_`
      plutôt que d'en ajouter une.
- [ ] **5. Application** — dans `Lighting.hlsli`, l'AO multiplie le terme
      ambiant et lui seul (`kD * EvalSH9(N) * albedo`, et le spéculaire IBL avec
      un AO spéculaire dérivé ou rien). **Jamais les lumières directes** : la
      boucle sur `PointLightBuffer` a déjà son `NdotL`, l'assombrir une seconde
      fois est l'erreur classique. Le PS lit la texture à `SV_Position.xy`, donc
      aucun UV à interpoler. Validation à la mesure sur un dump de frame : la
      luminance chute dans les angles rentrants d'une pièce et ne bouge pas au
      centre d'une face dégagée.
- [ ] **6. Bruit** — le papier tient à une slice par pixel et compte sur le
      jitter : un décalage d'angle sur les directions et un jitter le long de la
      direction, par pixel (Bayer 4×4 ou R2) et tournant par frame, puis une
      passe de flou bilatéral guidé par la profondeur (0.3 ms en 1080p chez les
      auteurs). Distribuer les pas exponentiellement autour du pixel : le proche
      compte plus, et ça récupère le détail des petits objets. Mesuré, pas jugé
      à l'œil : écart-type d'une zone plate du dump avant/après. **Sans
      historique temporel, une rotation par frame scintille** — la figer sur N
      frames en attendant, ou l'assumer jusqu'à ce que l'étape 8 amène la
      réprojection.
- [ ] **7. Ambiant directionnel** (§3.2 du papier) — c'est ici que le rapport
      travail/gain est le meilleur, parce que l'ambiant est déjà de la SH L2
      évaluable dans n'importe quelle direction : il n'y a rien à construire.
      Découper l'hémisphère en autant de sous-régions que d'échantillons
      d'ambiant, évaluer `EvalSH9` au centre de chacune et la pondérer par ses
      secteurs libres. La passe sort alors l'irradiance ambiante occultée en RGB
      (`R16G16B16A16_FLOAT`, le `.a` gardant l'AO scalaire pour le spéculaire)
      et le PS de géométrie remplace son `EvalSH9(s.N_)` par une lecture. Bent
      normal écarté explicitement (figure 12 du papier) : une seule direction ne
      décrit pas une surface entourée de barreaux.
- [ ] **8. Indirect diffus** (§3.3) — le gros morceau, et il commence par du
      HDR : rendre la scène dans une cible `R16G16B16A16_FLOAT` au lieu de la
      swapchain, plus une passe de tonemapping/blit, et le `saturate` de
      `PixelShader.hlsl` disparaît. La passe échantillonne alors ce buffer de
      lumière et les normales à chaque pas, pondère par
      `countbits(b_j & ~b_i) / 32 · (n_p · l) · (n_j · -l)`, puis `b_i |= b_j`.
      Multi-bounce = réinjecter le résultat dans le buffer de lumière pour la
      frame suivante : il faut un historique et un équilibrage d'intensité,
      sinon l'accumulation diverge. Compter 1 à 4 ms en 1080p selon les
      paramètres ; le levier est la demi-résolution avec upsampling bilatéral
      (≈ 4×). Limite à assumer et à écrire quelque part de visible : une source
      directe qui sort de l'écran emporte son rebond avec elle.

## 5. Matériaux — viewer d'assets, éditeur de textures, material = shader

Discuté le 13/09 : « l'éditeur de material et de textures, une fenêtre à part
avec un inspecteur d'objet comme Unreal ». Fait depuis : `MaterialEditorPanel`
(fenêtre ImGui flottante, un `.bmat` à la fois : albedo, roughness, metallic,
reflectivity, shading model, 4 slots texture), l'inspecteur objet réduit à
vignette + nom + *Edit* par slot, bouton *New* dans le picker. Le reste est ici.

**Ce qu'est un matériau aujourd'hui** : 48 octets de données pures
(`Material` dans `ShaderInterop.h`), uploadés tels quels dans une arène GPU et
lus par l'unique `PixelShader.hlsl` via `MaterialBuffer[idx]`. `GeometryPass`
a **une** pipeline ; `shadingModel_` est un `if` dans `ShadeSurface`. Chez
Unreal un matériau est un shader compilé et les *Material Instances* sont des
jeux de valeurs sur un parent. Acté : on va vers ce modèle (§5.3), mais dans
l'ordre — le viewer d'abord, parce qu'il sert de banc d'essai à tout le reste.

**Contrainte fenêtre** : ImGui est la branche master (1.92.4), pas de docking
ni de multi-viewports. « Fenêtre à part » = fenêtre ImGui flottante dans la
fenêtre principale. Une vraie seconde fenêtre OS = second swapchain
(`VulkanSwapchain`, `Win32Window`) + second contexte ImGui : on ne le fait que
si la flottante gêne à l'usage, pas avant.

### 5.1 Viewer d'assets

- [ ] **1. Preview offscreen** — le matériau sur une sphère, rendue dans une
      cible dédiée et affichée en image ImGui. Dépend de §4.2
      (`createRenderTarget`, un mip, `COLOR_ATTACHMENT|SAMPLED`) : c'est la
      même brique. Caméra orbitale à la souris, lumière fixe, fond = skybox de
      la scène courante ou gris neutre. Le backend Vulkan d'ImGui veut un
      `VkDescriptorSet` par image affichée (`ImGui_ImplVulkan_AddTexture`),
      un par cible, recréé si elle change de taille.
- [ ] **2. Un viewer, trois assets** — même panneau pour un `StaticMesh`
      (le mesh lui-même, avec ses matériaux), une `Texture` (quad plein,
      canaux R/G/B/A isolables, choix du mip) et un `Material`
      (sphère/cube/plan). Ouverture par double-clic dans le picker ou *Edit*
      dans l'inspecteur. C'est l'« inspecteur d'objet » d'Unreal : viewport à
      gauche, propriétés à droite, dans la même fenêtre.
- [ ] **3. Par-instance** (reste du 13/09) — deux besoins distincts :
      *flash de coup* = `Tint_C { col3 color_ }` optionnel, `float4 tint_`
      dans `StaticMeshGPUData` (112 octets), `albedo *= tint` dans le PS ;
      *tout éditer sur cet objet* = **Make unique** : clone du matériau dans un
      slot d'arène sans chemin (`AssetGPUArena::insertUnnamed`, qui saute
      `pathToKey_` — donc `saveAllAssets` l'ignore d'office), et les valeurs
      écrites en clair dans le JSON de scène puisqu'il n'y a pas de chemin
      (un type de champ dans `AssetFieldTypes.cpp`). Dans le modèle §5.3,
      *Make unique* = une instance locale du même shader : zéro pipeline en
      plus.

### 5.2 Éditeur de textures

`Texture` porte déjà `colorSpace_`, `filter_`, `wrapU_`, `wrapV_`, mais ils
sont **inertes** : seul `BtexSerializer` les lit, le shader échantillonne tout
avec le `g_sampler` global de `Lighting.hlsli`. Un éditeur de textures qui les
modifie ne change rien à l'image — d'où le prérequis.

- [ ] **1. Samplers par texture** — table de samplers bindless
      (`SamplerState g_samplers[]`, une entrée par combinaison filter × wrapU
      × wrapV, 18 au plus, créées au démarrage), index choisi à l'upload
      depuis les champs de `Texture` et rangé dans les bits hauts de l'index
      bindless de la texture — `Material` ne grossit pas, le PS décode
      `idx & 0xFFFFFF` / `idx >> 24`. `colorSpace_` reste ce qu'il est : un
      choix de `VkFormat` (`_SRGB` ou pas) à la création de l'image.
- [ ] **2. Le panneau** — colorSpace, filter, wrap, régénération des mips,
      et **réimport** depuis le fichier source (`.png` → `.btex`), le chemin
      source étant gardé dans le `.btex`. Aperçu par canal via le viewer §5.1.2.

### 5.3 Material = shader

Le découpage d'Unreal, transposé : le **graphe de matériau** produit une
`Surface` (albedo, normale, roughness, metallic, emissive…), le **shading
model** l'éclaire. Chez nous la seconde moitié existe déjà — `Surface` et
`ShadeSurface` dans `Lighting.hlsli` — et le `PixelShader.hlsl` actuel est
exactement « un matériau » : celui qui remplit `Surface` depuis 4 textures et
4 scalaires. Le rendre remplaçable, c'est ça le chantier.

Ce que ça coûte chez Unreal, et qu'on refuse d'importer : l'explosion de
permutations (un shader × N features × M passes), et un éditeur de nœuds avant
tout le reste. Ici : **un shader de matériau = un fichier `.hlsl` écrit à la
main**, un `.bmat` = ce shader + ses valeurs de paramètres, une pipeline par
shader (pas par `.bmat`), et l'éditeur de nœuds est le dernier étage, s'il
vient un jour.

- [ ] **1. Le contrat** — un shader de matériau fournit
      `void MaterialMain(MaterialInputs i, MaterialParams p, inout Surface s)`
      et déclare son `struct MaterialParams`. Le PS de géométrie devient un
      squelette : interpolants → `MaterialInputs`, `#include` du matériau,
      `MaterialMain`, `ShadeSurface`. Étape validable seule : `PixelShader.hlsl`
      redécoupé en `Default.material.hlsl` + squelette, image identique au
      pixel, une seule pipeline encore.
- [ ] **2. Paramètres à taille variable** — `MaterialBuffer` typé
      `Material[]` devient un blob (`ByteAddressBuffer`), chaque `.bmat`
      occupe `offset + sizeof(MaterialParams)` ; `materialIndices_` de
      `StaticMeshGPUData` devient un offset. Le `.bmat` sur disque = chemin du
      shader + valeurs par nom. **L'UI de l'éditeur de matériaux se dérive du
      struct** : réflexion SPIR-V du `MaterialParams` (dxc sort les membres,
      offsets et types — c'est la note « descriptor layouts câblés à la main »
      des vigilances, qui trouve ici sa première vraie raison), ou, en
      attendant, un `BATAP_PARAMS(...)` dans un en-tête partagé façon
      `ShaderInterop.h`. Les textures sont des `uint` bindless comme
      aujourd'hui.
- [ ] **3. Une pipeline par shader de matériau** — `GeometryPass::pipeline_`
      devient une map shader → pipeline, les draws groupés par pipeline
      (c'est le *binning* de §2b : en GPU-driven, le compute de culling range
      chaque instance dans la liste indirecte de son shader, un
      `vkCmdDrawIndexedIndirectCount` par pipeline). Deux `.bmat` sur le même
      shader = même pipeline, aucune recompilation : ce sont les *Material
      Instances* d'Unreal, gratuites.
- [ ] **4. Compilation et hot reload** — `VulkanShaderCompiler` (dxc chargé
      au runtime) compile déjà à chaud ; `kShaders` est un `constexpr array`
      pour les shaders moteur, les shaders de matériau vivent dans une liste
      dynamique découverte depuis les `.bmat` chargés. Un `.material.hlsl`
      sauvé dans VS Code se recompile, la pipeline est reconstruite, la sphère
      du viewer (§5.1.1) se met à jour : c'est ça l'éditeur, avant tout
      graphe. Erreur de compilation → on garde l'ancienne pipeline et on
      affiche le log de dxc dans le panneau.
- [ ] **5. Éditeur de nœuds** — un générateur de `.material.hlsl` depuis un
      graphe. Ne se justifie que si écrire le HLSL à la main s'avère être le
      frein ; à décider avec l'usage, pas avant que 1-4 tournent.

Ordre : 5.1.1 (preview) → 5.3.1-2 (contrat + params, une pipeline) → 5.3.3-4
(pipelines multiples + hot reload) → 5.2 (samplers, textures) → 5.1.2-3 → 5.3.5.
Le prepass de §4.3 dessine la géométrie sans PS : il n'est pas concerné par
les shaders de matériau, sauf le jour où un matériau voudra de l'alpha test
ou du déplacement de vertex — à ce moment-là le contrat gagne un
`MaterialVertex`, pas avant.

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
