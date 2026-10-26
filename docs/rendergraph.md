# Render graph — plan

Décidé le 2026-10-25. Se place entre l'étape 3.2b et l'étape 3.3 du plan RT
([raytracing.md](raytracing.md)) : la prépasse (3.3), les matrices précédentes
(3.5), le MSAA (3.6), l'AO (phase 4) et les historiques temporels (5.3, 7.5)
se déclarent alors dans le graphe au lieu d'ajouter leurs barrières, leurs
cibles et leur resize à la main.

Ce qui a été fait à chaque étape, et ses mesures, va dans
[rendergraph-notes.md](rendergraph-notes.md), jamais ici.

## Pourquoi maintenant

**Aujourd'hui** (2026-10-25) :
- 34 appels à `BarrierBatch` dans 7 fichiers, chacun avec son `from` deviné :
  `ShaderRead → ColorAttachment` avec `Discard::Yes` dans chaque passe plein
  écran, `sceneLastUsage_` tenu à la main dans le `Renderer`.
- 6 `vkCmdBeginRendering`, chacun avec son `VkRenderingAttachmentInfo`
  recopié (`ExposurePass`, `GiUpdatePass`, `ShadowPass`, deux dans
  `ScenePasses::record`, l'UI dans `Renderer::render`).
- Chaque cible gère sa taille : `hdrTarget_` recréé dans `record`,
  `GiUpdatePass::resize`, profondeur et image de scène dans le `Renderer`
  (`createDepthBuffer`, `retireTargets`).
- Pas de chrono GPU permanent : 1.8 et 2.8b ont posé des timestamps
  temporaires, `GiUpdatePass` a son propre pool de requêtes.

**Ce qui arrive** : profondeur lisible + normales, 4 à 5 passes plein écran
pour l'AO, des historiques en ping-pong, des resolves MSAA, la LUT DFG, le
préfiltrage de l'IBL par mip. Chacun ajouterait ses barrières et ses cibles
au même endroit.

## Décisions

- **Graphe reconstruit à chaque frame**, déclaratif. Une vingtaine de passes :
  construction et compilation coûtent quelques dizaines de µs. Les bascules
  (RT/raster, vues debug, auto-exposition) ne demandent aucune invalidation :
  une passe qui n'a rien à faire n'est pas ajoutée.
- **Ordre d'exécution = ordre de déclaration.** Pas de tri topologique, pas de
  réordonnancement. Le graphe calcule les dépendances, il ne choisit pas
  l'ordre. Avec plusieurs files, c'est l'ordre de déclaration dans chaque file.
- **Le vocabulaire est `Usage`** (`VulkanBarrier.h`). Une passe déclare
  `read(ressource, Usage::X)` et `write(ressource, Usage::Y)`. Le graphe
  connaît le dernier usage de chaque ressource et émet un seul
  `vkCmdPipelineBarrier2` avant chaque passe. C'est le modèle des états D3D12
  que `VulkanBarrier.h` a déjà reconstruit, rendu automatique.
- **Buffers : une barrière mémoire globale par point de synchro**, comme
  `BarrierBatch::memory` le justifie déjà. Le graphe trace quand même les
  accès buffer par buffer : ce sont eux qui donnent les dépendances, et plus
  tard les sémaphores entre files.
- **Le graphe ouvre la scope de rendu** d'une passe raster d'après ses
  attachments déclarés (vue, load op, valeur d'effacement). Une passe
  n'appelle plus jamais `vkCmdBeginRendering`.
  - Le `storeOp` se déduit : `DONT_CARE` pour une transitoire que personne
    ne lit plus dans la frame (la profondeur après le tonemap), `STORE`
    sinon.
  - La première écriture d'une transitoire part d'`UNDEFINED` (l'actuel
    `Discard::Yes`), sans qu'on l'écrive.
- **Deux sortes de ressources.**
  - *Créées par le graphe* (transitoires) : une taille (absolue, ou fraction
    de la sortie) et un format. Recyclées d'une frame à l'autre par
    description, rendues par `requestDestroy` après `FramesInFlight` frames
    sans demande. Le resize est automatique. Les bits d'usage Vulkan se
    déduisent des accès déclarés (`SAMPLED` dès qu'une passe la lit). Elles
    gardent leur slot bindless, comme `createTarget`.
  - *Importées* : swapchain, image de scène de l'éditeur, atlas d'ombre,
    cache GI, AS, buffers par frame. Le graphe garde leur état d'une frame à
    l'autre, indexé par handle : plus de `sceneLastUsage_`.
- **Accès déclarés, sinon erreur.** Dans `execute`, une passe obtient image,
  vue, index bindless ou buffer par le contexte du graphe, qui vérifie en
  debug qu'elle les a déclarés. Un accès non déclaré est une barrière
  manquante : c'est là qu'on l'attrape. De même, lire une transitoire que
  personne n'a écrite dans la frame est une erreur.
- **Compilation sans GPU.** Passes, accès et files entrent ; barrières et
  lots sortent, en handles et en `Usage`. Elle se teste sans GPU
  (`RenderGraphCheck`, comme `TerrainCheck`), y compris les cas à plusieurs
  files que la machine n'a pas.
  - Règle d'image : barrière si le layout change, si le dernier accès
    écrivait, ou si celui-ci écrit. Lecture après lecture : rien.
  - **Le graphe est Vulkan**, comme le reste du renderer. Une découpe noyau
    neutre / backend Vulkan a été faite et défaite le 2026-10-25 : sans
    second backend elle n'achetait aucune portabilité, et coûtait deux
    tables et quatre types de plus (notes de 1.1).
- **Écartés :**
  - *Culling automatique* : il demande de versionner les ressources (chaque
    écriture crée une version) pour un gain nul, puisque le code décide déjà
    par des `if`. La vérification utile qu'il apporterait est gardée (lecture
    sans écrivain).
  - *Aliasing mémoire* des transitoires : *(option)* en 5.4, sur une mesure.
  - *Passes compute* : le moteur n'en a pas (`OBJECTIFS.md` §4). Le type de
    passe existe pour qu'elles s'ajoutent sans toucher au graphe.
  - *Sous-passes et attachments `LAZILY_ALLOCATED`* : utiles sur mobile
    seulement.

### API visée

```cpp
const GraphImage hdr = graph.createImage("hdr", {OutputSize{}, HdrFormat});
const GraphImage depth = graph.importImage("depth", depth_, Usage::DepthAttachment);
const GraphBuffer cache = graph.importBuffer("gi cache", rtScene_->cacheValues());

graph.addPass("scene")
    .color(hdr, Load::Clear, clearColor)
    .depth(depth, Load::Clear, 0.f)
    .read(cache, Usage::ShaderRead)
    .execute([this, push](GraphContext& c) { recordScene(c.pass(push)); });

graph.addPass("exposure readback", GraphQueue::Graphics)
    .read(total, Usage::TransferSrc)
    .write(readback, Usage::TransferDst)
    .execute([=](GraphContext& c) { c.copyImageToBuffer(total, readback); });
```

`execute` tourne dans la même frame, après la compilation, quand toutes les
ressources ont leur image. Capturer par valeur ce qui est local à la
construction.

## Préparer plusieurs files

Rien de ce plan ne crée une seconde file, mais tout ce qui suit est en place
dès les phases 1 à 3, pour que la phase 6 ne touche que le graphe, le contexte
et la soumission :

1. **Chaque passe déclare sa file** : `GraphQueue::Graphics` (défaut),
   `Compute` ou `Transfer`. Tant qu'une seule existe, `VulkanContext` donne
   la file graphique pour les trois.
   - `Transfer` est réservé aux copies qui gagnent à tourner à côté : les
     uploads de streaming. Une petite copie au milieu de la frame (readback
     de l'exposition, compteurs du cache GI) reste `Graphics` : sur une autre
     file elle coûterait deux sémaphores pour rien.
   - Les builds de BLAS et de TLAS sont déclarés `Compute`, leur place
     d'arrivée.
2. **L'état d'une ressource retient qui l'a touchée** : la passe (donc la
   file) de son dernier écrivain, et ses lecteurs depuis. Même file :
   barrière. Autre file : sémaphore entre lots, plus la barrière côté
   consommateur. La seconde branche est écrite et testée par
   `RenderGraphCheck` dès 1.1, jamais prise à l'exécution avant la phase 6.
3. **La compilation produit des lots**, pas un command buffer : `Batch`
   = file, passes, attentes, signaux. Un seul lot aujourd'hui. Le `Renderer`
   soumet les lots dans l'ordre ; l'attente de l'acquire et le signal du
   present s'accrochent au lot qui touche l'image de swapchain en premier et
   en dernier. Rien hors du graphe ne suppose « un command buffer par frame ».
4. **Soumission par `vkQueueSubmit2`**, avec un sémaphore timeline par file
   (`timelineSemaphore` est déjà demandé dans `VulkanContext`) : un lot
   signale sa file à la valeur de son numéro, un autre lot l'attend.
5. **Command pools par slot de frame et par famille de file**, dès que le
   `Renderer` passe au graphe (2.3).
6. **Les uploads sont une passe** (2.1). Ils écrivent une ressource
   symbolique `uploads`, que lisent les passes qui lisent des données montées
   (instances, matériaux, sommets du terrain). Aujourd'hui, une barrière ; sur
   la file de transfert, cette dépendance devient le sémaphore.
7. **Partage entre familles, plus tard** : une ressource qui passe d'une
   file à l'autre est créée en `VK_SHARING_MODE_CONCURRENT`, et il n'y a
   aucun transfert de propriété. Le graphe le sait à la compilation pour ses
   transitoires. À mesurer : sur AMD, une image `CONCURRENT` peut perdre sa
   compression.
8. **Un pool de timestamps par file** dans le profileur (4.1).

Sur MoltenVK, ou sur un GPU à une seule famille, tout retombe sur la file
graphique sans autre code.

## Règles

- **Le graphe ne change pas l'image.** Des phases 0 à 4, chaque étape rend
  une image identique (banc de 0.1). Les phases 5 et 6 ajoutent des
  capacités ; leur premier client vient du plan RT.
- **Pas de double chemin.** Contrairement au RT, aucun réglage ne garde
  l'ancien code : une passe migrée perd ses barrières et sa scope dans le même
  commit. Le banc garantit l'équivalence.
- **Migration passe par passe** (phase 1). Le graphe s'exécute dans le
  command buffer existant, entre le code écrit à la main. À la frontière,
  une ressource est importée avec son usage d'entrée et rendue dans un usage
  de sortie déclaré (`importImage(h, in, out)`). La frontière recule à chaque
  étape et disparaît en 2.2.
- **Synchro vérifiée par la validation**, pas par l'image : une course
  peut ne rien montrer sur une carte et casser sur une autre. Chaque étape
  passe la validation de synchronisation (0.2) sans nouvelle erreur.
- **Le plan ne porte que la case cochée.** Ce qui a été fait, les mesures et
  les écarts au texte vont dans `rendergraph-notes.md`.

## Pièges

- **Lecture de la TLAS par ray query** : il faut
  `VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR` côté consommateur, que
  `Usage::ShaderRead` ne donne pas. Aujourd'hui `AnyRead` (`MEMORY_READ`) le
  couvre. Ajouter `Usage::AccelerationStructureRead` (étages des shaders,
  accès AS) en 1.7.
- **`Usage::ShaderWrite` est limité au fragment shader.** Une passe compute
  demandera `ComputeRead` et `ComputeWrite`. Sur une file compute ou
  transfert, un étage que la file ne supporte pas est une erreur de
  validation.
- **Un index bindless écrit dans un buffer n'est pas un accès.**
  `FrameGPUData::hdrTexture_` est écrit par la passe des constantes de frame,
  mais c'est la passe qui *échantillonne* qui déclare la lecture. L'index se
  prend par `graph.textureIndex(h)`, valable après la compilation, hors de la
  vérification des accès.
- **Buffers par frame** (`createPerFrameBuffer`) : un `VkBuffer` par slot,
  on importe celui du slot courant.
- **Image de swapchain** : son index n'est connu qu'après l'acquire, on
  l'importe après. Son état est par image, remis à zéro à la recréation.
- **`BarrierBatch::Capacity` vaut 8.** La passe de scène lira plus d'images
  que ça (deux atlas, HDR, profondeur, puis AO et normales) : capacité
  dynamique dans le graphe.

## Plan d'action

Une étape = un commit. Claude vérifie la *Vérif*, coche, puis Batap commite.
Rien ne démarre sans un « vas-y » explicite par étape.

**Vérification commune à chaque étape :**
- build vert en `msvc-debug` et en `msvc-relwithdebinfo` ;
- banc de 0.1 vert, en raster et en RT ;
- validation, synchronisation comprise (0.2), sans nouvelle erreur.

Pour la plomberie sans effet à l'exécution (types, API sans client), le build
suffit.

Les étapes marquées *(option)* peuvent sauter sans bloquer la suite.

### Phase 0 — Banc d'essai (rien ne change à l'écran)

- [x] **0.1 Banc de régression.** Un script dans `tools/` rend une série de
      scènes et compare chaque dump à une référence.
      - Scènes statiques versionnées dans `scenes/bench/` (physique retirée,
        caméra fixe) : buggy + dalle sous l'étoile à 46° et à 15°, point,
        spot, rect, terrain proche et lointain, `test_GI`, `test_GI_spot`,
        `test_auto_exposure`. Elles remplacent les copies faites à la main
        dans le plan RT.
      - Chaque scène en raster et en RT, par `SpaceFret.exe --scene` et
        `BATAP_DUMP_FRAME`, à une frame fixe par scène. Le mode RT est forcé
        par une variable d'environnement, sans toucher la config de
        l'utilisateur. Lancement sans prendre le focus.
      - Plus un run de l'éditeur (`--select`, recadrage du viewport) : c'est
        le seul chemin qui passe par l'image de scène et ImGui, et 2.2 le
        réécrit.
      - `--baseline` rend deux fois chaque scène : l'écart entre les deux
        runs est la tolérance de la scène (le plan RT a mesuré 4 px en RT,
        ~210 px en raster sur un bord d'ombre).
      - Sortie : un tableau scène × mode, pixels différents et écart max,
        vert dans la tolérance. Cible : moins de 2 minutes.

      *Vérif* : deux runs sur le même commit sont verts ; un shader modifié
      exprès passe au rouge.
- [x] **0.2 Validation de synchronisation.** `BATAP_SYNC_VALIDATION=1` active
      `VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT` en debug
      (lente, d'où la variable). Le banc compte les messages par scène.
      *Vérif* : les erreurs d'aujourd'hui sont comptées et notées ; c'est la
      référence des étapes suivantes. Les corriger n'est pas dans l'étape.
- [x] **0.3 Chrono de frame GPU.** Deux timestamps autour du command buffer
      de la frame, médiane loggée par le banc. Sert à montrer que les
      barrières du graphe ne coûtent pas plus que celles écrites à la main.
      *Vérif* : chiffres de référence notés par scène.

### Phase 1 — Le noyau, passe par passe

- [x] **1.1 `RenderGraph` sans client.** `Renderer/Vulkan/RenderGraph.{h,cpp}` :
      - import d'images et de buffers (usage d'entrée, usage de sortie), par
        objets Vulkan ou par `GPUResourceHandle` ;
      - passes : nom, file, accès, attachments, `execute` ;
      - compilation en barrières par passe et en lots, sans GPU ;
      - enregistrement d'un lot dans un command buffer donné, scope de rendu
        comprise ;
      - contexte d'exécution qui vérifie les accès.

      `RenderGraphCheck` (sans fenêtre) : lecture après écriture, lecture
      après lecture sans barrière, écriture après lecture, profondeur écrite
      puis échantillonnée, première écriture en `UNDEFINED`, `storeOp`
      déduit, lecture sans écrivain refusée, accès non déclaré refusé,
      attachment du mauvais aspect refusé ; et, files simulées, une
      dépendance entre files qui donne deux lots et un sémaphore.
      *Vérif* : `RenderGraphCheck` vert.
- [x] **1.2 Exposition.** `ExposurePass` : trois passes raster plus le
      readback, la HDR importée (`ShaderRead` en entrée et en sortie). Ses
      barrières et sa scope disparaissent.
      *Vérif* : `test_auto_exposure` identique, readback compris (même EV
      sur 600 frames).
- [x] **1.3 Mise à jour GI.** `GiUpdatePass` : N passes de mise à jour et la
      résolution ; les buffers du cache (`RtScene`) importés ; la copie des
      compteurs en passe. Les barrières `ShaderRead → ShaderWrite` de
      `ScenePasses::record` disparaissent. Le pool de requêtes reste
      jusqu'à 4.1.
- [x] **1.4 Ombres.** Atlas local et atlas des cascades en deux passes raster
      (profondeur, effacement à 1), importés ; l'écriture du buffer
      `shadows` en passe.
- [x] **1.5 Scène HDR.** Géométrie, terrain, billboards, ciel (ou le
      premier hit RT) en une passe raster : HDR et profondeur effacées, lit
      les atlas, le cache GI, la TLAS. L'écriture des constantes de frame en
      passe.
- [x] **1.6 Tonemap et debug.** La sortie (`RenderTargets::color_`) importée,
      la profondeur en `Load`.
- [ ] **1.7 Structures d'accélération.** BLAS, compaction et TLAS en passes
      `Compute` (servies par la file graphique), l'effacement du cache en
      passe `Graphics` ; AS et scratch importés comme buffers.
      `Usage::AccelerationStructureRead` ajouté (voir Pièges). Les barrières
      de `RtScene` disparaissent.
- [ ] **1.8 `ScenePasses` déclare, n'enregistre plus.** `record(cmd, …)`
      devient `addPasses(graph, …)`. Il reste une frontière : la sortie et la
      profondeur, au `Renderer`.
      *Vérif* : `BarrierBatch` n'apparaît plus que dans `RenderGraph.cpp`,
      `VulkanRenderer.cpp` et les uploads de `VulkanResources.cpp`.

### Phase 2 — La frame entière

- [ ] **2.1 Uploads.** `flushUploads` devient une passe en tête du graphe,
      qui écrit la ressource symbolique `uploads` (voir « Préparer plusieurs
      files », point 6). Elle reste `Graphics` jusqu'à 6.3.
- [ ] **2.2 Swapchain, image de scène, ImGui, dump.** `Renderer::render`
      construit le graphe :
      - swapchain importée après l'acquire, état final `Present` ;
      - image de scène de l'éditeur importée, son état gardé par le graphe :
        `sceneLastUsage_` disparaît ;
      - ImGui en passe raster, qui lit l'image de scène quand l'UI la montre ;
      - l'effacement sans caméra et le dump de frame en passes.

      *Vérif* : le run éditeur du banc identique ; redimensionner le viewport
      ne donne aucune erreur de validation.
- [ ] **2.3 Lots et soumission.** La compilation rend ses lots (un seul), le
      `Renderer` les soumet par `vkQueueSubmit2`, avec l'acquire et le
      present accrochés au lot de la swapchain. Command pools par slot de
      frame et par famille.
      *Vérif* : plus aucun `vkQueueSubmit` hors de la soumission des lots.

### Phase 3 — Ressources créées par le graphe

- [ ] **3.1 Transitoires.** `createImage(nom, desc)`, recyclage par
      description, libération différée, bits d'usage déduits, slot bindless.
      Premiers clients : les cibles de l'exposition et de la mise à jour GI.
      `GiUpdatePass::resize` disparaît.
- [ ] **3.2 HDR et profondeur.** `hdrTarget_` et son resize disparaissent de
      `ScenePasses`, `createDepthBuffer` et la profondeur de `retireTargets`
      du `Renderer`.
      *Vérif* : redimensionner la fenêtre et le viewport de l'éditeur ; le
      nombre d'images du graphe revient au même après 3 frames.

### Phase 4 — Outils

- [ ] **4.1 Chrono GPU par passe.** Deux timestamps par passe, pool par slot
      de frame et par file, lus `FramesInFlight` frames plus tard, médiane
      sur 1 s. Remplace le pool de `GiUpdatePass` et les patchs temporaires
      de 1.8 et 2.8b du plan RT. Toujours actif : quelques dizaines de
      requêtes par frame.
- [ ] **4.2 Fenêtre « Render graph »** dans l'éditeur (menu View) : passes
      dans l'ordre avec leur file, leur temps, leurs attachments et les
      barrières émises avant elles ; ressources avec taille, format et
      mémoire.
- [ ] **4.3 Aperçu d'une ressource.** Un clic sur une image l'affiche dans la
      fenêtre, par une passe d'aperçu déclarée comme les autres (donc
      synchronisée) : HDR exposée, profondeur en Z inversé ramenée en
      linéaire, un canal au choix.
      *Vérif* : HDR, profondeur, atlas d'ombre, cible de mise à jour GI
      lisibles.
- [ ] **4.4 Dump d'une ressource** *(option)*. `BATAP_DUMP_RESOURCE=<nom>`
      avec `BATAP_DUMP_FRAME` : l'aperçu de 4.3 écrit en PNG. Permet un
      « image identique » sur la HDR avant le tonemap.
- [ ] **4.5 Mesures.** Temps CPU de construction et de compilation du graphe ;
      temps de frame GPU contre 0.3. Reporter dans « Chiffres ».

### Phase 5 — Ce que le plan RT demande ensuite

Chaque étape attend son premier client.

- [ ] **5.1 Historiques.** `graph.history(nom, desc)` rend l'image courante et
      celle de la frame précédente (deux images physiques échangées à chaque
      frame). La précédente est invalide à la première frame, après un
      resize, ou sur demande (coupure de caméra) : le shader le sait et rejette
      l'historique.
      Client : 3.5 (vecteurs de mouvement), puis 5.3 et 7.5.
- [ ] **5.2 MSAA.** Nombre d'échantillons dans la description, resolve
      déclaré avec l'attachment (`resolveImageView`, moyenne pour la
      couleur). Client : 3.6.
- [ ] **5.3 Accès par mip.** L'état suivi par sous-ressource, pour qu'une passe
      écrive le mip *i* en lisant le mip *i − 1*. Client : B.7 (préfiltrage
      de l'IBL), plus tard la HZB.
- [ ] **5.4 Aliasing mémoire** *(option)*. Deux transitoires dont les vies ne
      se croisent pas partagent leur mémoire. Seulement si la mesure de 4.5
      le justifie.

### Phase 6 — Plusieurs files (sur un besoin mesuré)

Déclencheur : le chrono de 4.1 montre la file graphique occupée par du
travail qui pourrait tourner à côté (uploads des chunks de terrain, build de
la TLAS, AO).

- [ ] **6.1 Familles dédiées.** `VulkanContext` cherche une famille compute
      et une famille transfert dédiées (vk-bootstrap
      `get_dedicated_queue`), sinon retombe sur la graphique. Log
      `[Vulkan] files : …`.
      *Vérif* : le log ; banc identique (rien n'est encore déplacé).
- [ ] **6.2 Plusieurs lots.** Sémaphores timeline entre lots, partage
      `CONCURRENT` des ressources qui changent de file, timestamps par file.
      *Vérif* : `RenderGraphCheck` (déjà vert depuis 1.1) ; validation de
      synchronisation propre avec une passe de test forcée sur la file
      compute.
- [ ] **6.3 Uploads sur la file de transfert.** Premier client : les chunks
      de terrain. Textures et buffers montés en `CONCURRENT`.
      *Vérif* : temps de frame en survolant la lune, contre 4.5.
- [ ] **6.4 BLAS et TLAS sur la file compute.**
      *Vérif* : la file graphique ne les attend plus qu'au premier ray query.
- [ ] **6.5 Passes compute.** Hors de ce plan : il faut d'abord des
      pipelines compute dans le moteur (`ComputeRead`, `ComputeWrite`, hot
      reload). Le graphe n'a alors rien à changer.

## Chiffres

À remplir par 0.3 et 4.5 : temps de frame GPU par scène avant et après,
temps CPU du graphe, nombre de passes, de barrières et de transitoires par
frame.
