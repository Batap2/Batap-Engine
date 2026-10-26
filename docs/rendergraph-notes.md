# Render graph — notes par étape

Le détail que le plan (`rendergraph.md`) ne porte pas : mesures complètes,
itérations rejetées, pièges. Une section par étape, dans l'ordre du plan.

## 0.1 Banc de régression

*Fait (2026-10-25)* : `tools/render_bench.py`, 9 scènes dans `scenes/bench/`.
Le raster est identique au pixel entre deux runs sur tous les cas, le RT à
1–4 niveaux près (cache GI). Banc en ~40 s, référence en ~75 s.

*Écarts au texte du plan :*
- **Scènes.** Les scènes « buggy + dalle » du plan RT n'existent plus. À la
  place :
  - `moon_near` et `moon_far`, tirées de `test_buggy_fusee` : terrain proche
    et lointain, étoile à 52° au-dessus du buggy, phares ;
  - `room`, tirée de `test_auto_exposure` ;
  - le décor de `test_GI` sous chaque type de lumière : `gi_sun`,
    `gi_sun_low` (soleil à 15°), `gi_spot` (`test_GI_spot`), `gi_point`
    (800 cd, EV100 4), `gi_rect` (1 000 cd/m², EV100 8).
- **Pas de temps fixe** (`BATAP_FIXED_DT`), en plus du forçage du mode
  (`BATAP_RAY_TRACING`) : sans lui, le raster n'est pas reproductible (voir
  plus bas).
- **Run de l'éditeur** sur `gi_sun`, avec `Directional Light` sélectionnée.
- **Tolérance** : « dans le bruit » jusqu'à deux fois l'écart mesuré entre
  les deux runs de la référence, car le nombre de pixels bruités du RT varie
  d'un run à l'autre.

*Utilisation* (depuis la racine du jeu, avec le build `relwithdebinfo` à
jour) :
- `python tools/render_bench.py --baseline` **avant** de toucher au code : deux
  runs par cas, référence et tolérance dans `build/render_bench/baseline/`.
- `python tools/render_bench.py` après : un run par cas, comparé. Les images
  sont dans `build/render_bench/current/`, avec un `_diff.png` (pixels qui
  changent, en blanc) pour chaque cas qui bouge.
- `--only gi_spot,editor`, `--modes rt`, `--config debug` pour cibler.

*Moteur* : deux variables d'environnement, lues par le constructeur
d'`Engine` (`platformEnvVar`, nouveau dans `PlatformWindow.h`) :
- `BATAP_RAY_TRACING=0|1` remplace `render.rayTracing` de la config ;
- `BATAP_FIXED_DT=<s>` remplace le `deltaTime_` mesuré à chaque frame.

**Le pas fixe est ce qui rend le banc exact.** Avec le dt mesuré, la frame N
tombe à un temps de simulation différent à chaque run : les astres sur rails
n'y sont pas au même endroit (l'étoile éclaire sous un autre angle), et le
cache GI et l'auto-exposition, qui s'adaptent en secondes, n'ont pas
convergé pareil. C'est sans doute le bruit de ~210 px sur un bord d'ombre
que le plan RT mesurait en raster. Avec `BATAP_FIXED_DT=1/60`, la frame 600
est 10 s de simulation, quel que soit le nombre de fps : le jeu peut tourner
sans limite.

*Pièges trouvés :*
- **Terrain asynchrone.** Les chunks se construisent sur des threads, en
  temps réel. À la frame 1200 (~2 s), le premier lancement après un build
  n'avait pas fini : 430 000 px d'écart, des trous dans le relief. Deux runs
  « chauds » sont identiques dès la frame 1200 ; la lune est dumpée à la
  frame 2400 (~5 s), pour garder une marge.
- **Fenêtre « Time » du jeu.** Elle affiche un débit mesuré en temps réel
  (13,4 puis 13,1 s/s) : masquée dans tous les cas du jeu.
- **Caméra de l'éditeur.** Elle vient de sa config, par scène. Le banc y
  écrit une pose pour `scenes/bench/gi_sun.btpl` avant le run, puis remet
  les deux configs (jeu et éditeur) octet pour octet après chaque run.
- **`imgui.ini`.** Chaque run a son dossier de travail (cwd) vide : l'éditeur
  part de sa disposition par défaut, et le viewport est toujours à
  (272, 42)–(978, 728) en 1280 × 728.

*Mesuré (RTX 5070, 1280 × 728, 2026-10-25)* — écart entre deux runs du même
build, pixels différents / écart max (sur 255) :

| Cas | Raster | RT |
|---|---|---|
| `moon_near` | 0 | 12 566 / 4 |
| `moon_far` | 0 | 13 513 / 2 |
| `room` | 0 | 79 / 1 |
| `gi_sun` | 0 | 772 / 1 |
| `gi_sun_low` | 0 | 1 156 / 1 |
| `gi_spot` | 0 | 8 354 / 2 |
| `gi_point` | 0 | 785 / 1 |
| `gi_rect` | 0 | 1 126 / 1 |
| éditeur | 0 | 370 / 1 |

- Le bruit RT vient du cache GI : l'ordre d'insertion des clés (CAS, sondage
  linéaire) dépend du timing du GPU. Sur la lune, s'y ajoute l'index des
  chunks dans la clé, qui dépend de l'ordre de construction.
- Durées : référence 74 s (deux runs par cas), banc 37 à 42 s. Un run de la
  lune ~5 s, un run GI ~0,8 s, l'éditeur ~2,8 s.
- *Contre-épreuve* : `TonemapPS` multiplié par 1,01 (au plus 1 niveau
  d'écart) : les 18 cas en `DIFFÉRENT`, de 15 000 px (`gi_spot` raster) à
  640 000 px. Shader remis, banc vert.

## 0.2 Validation de synchronisation

*Fait (2026-10-25)* : `BATAP_SYNC_VALIDATION=1` (build debug seulement) active
la validation de synchronisation dans `VulkanContext`, par `VK_EXT_layer_settings`
(`add_layer_setting` de vk-bootstrap) ; log `[Vulkan] validation de
synchronisation : oui`. Le banc la lance avec `--validation`.

*Utilisation*, à chaque étape, en plus du banc d'images :
- avant : `python tools/render_bench.py --validation --baseline` (~28 s) ;
- après : `python tools/render_bench.py --validation` (~27 s). Un id de
  message absent de la référence pour ce cas fait échouer le banc.
- Build `debug`, frame 60 (assez pour les BLAS, leur compaction,
  l'effacement du cache et le readback de l'exposition), pas de comparaison
  d'images. Les messages de chaque cas, avec leur nombre et le premier texte,
  sont dans `build/render_bench/validation/<cas>.txt`.

*Écart au texte du plan* : en plus de `validate_sync`, le réglage
`syncval_shader_accesses_heuristic` est activé. Il est désactivé par défaut,
et **sans lui, une lecture par descripteur n'est jamais vérifiée** contre
l'écriture qui la précède. Contre-épreuve : la barrière `TransferDst →
ShaderRead` après l'écriture des constantes de frame (`ScenePasses::record`)
retirée.
- Sans l'heuristique : aucun message, ni en raster ni en RT.
- Avec : `SYNC-HAZARD-READ-AFTER-WRITE` en RT sur `gi_sun` et `moon_near`
  (« Shader stage FRAGMENT reads VkBuffer, previously written by
  vkCmdUpdateBuffer », la lecture de `GiUpdatePS`). En raster, rien, et
  c'est juste : la barrière de `ShadowPass` couvre l'écriture avant toute
  lecture.
- Sur le code intact, l'heuristique n'ajoute aucun message (les 18 cas
  identiques avec et sans) : pas de faux positif sur ce moteur.
- Les accès sans descripteur (attachments, copies) sont vus dans tous les
  cas : la barrière avant la copie du dump retirée donne
  `SYNC-HAZARD-READ-AFTER-WRITE` sur `vkCmdCopyImageToBuffer`, plus deux
  erreurs de layout.

*Erreurs d'aujourd'hui* (la référence) : une seule, dans `room` (raster et
RT), la seule scène en auto-exposition.
- `SYNC-HAZARD-WRITE-AFTER-WRITE` au `vkQueueSubmit` : la copie du
  readback de l'exposition (`ExposurePass::record`, `vkCmdCopyImageToBuffer`)
  écrit un buffer déjà écrit par la même copie, dans un command buffer
  soumis avant, sans barrière entre les deux.
- 10 messages = la limite de doublons de la couche, donc à chaque frame.
- Non analysée : le buffer est indexé par slot de frame et la fence du slot
  est attendue avant. Soit la couche ne voit pas cette attente, soit le
  readback n'est pas celui du slot. La phase 2 (dump et readback en passes)
  passe par là.

Les autres cas : aucun message. Référence stable sur trois runs.

## 0.3 Chrono de frame GPU

*Fait (2026-10-25)* :
- `Renderer` : un pool de `2 × FramesInFlight` timestamps. Un en tête du
  command buffer de la frame (`TOP_OF_PIPE`, après le reset des deux
  requêtes du slot), un à la fin (`BOTTOM_OF_PIPE`, juste avant
  `vkEndCommandBuffer`). Lus quand le slot revient, après l'attente de sa
  fence ; les 256 derniers sont gardés.
- Avec chaque dump : `[Vulkan] frame GPU : médiane X ms, min Y ms, max Z ms
  sur N frames`. Toujours actif, sans variable : deux requêtes par frame.
- Le banc lit la médiane. `--baseline` l'enregistre (moyenne des deux runs)
  dans `build/render_bench/baseline/gpu.json` ; le banc normal affiche
  « référence → actuel (écart %) ». Information seulement, jamais un échec.

*Écart au texte du plan* : le min et le max sont dans le log, en plus de la
médiane. Ils ont servi à vérifier que la mesure ne contient pas l'attente de
l'image de swapchain : la soumission attend l'acquire à l'étage
`COLOR_ATTACHMENT_OUTPUT`, donc entre les deux timestamps. Sur la lune en
raster, min 4,8–4,9 ms pour une médiane de 5,4 ms, identique d'un run à
l'autre : du travail, pas une attente.

*Référence (RTX 5070, 1280 × 728, 2026-10-25)* — médiane en ms, deux runs :

| Cas | Raster | RT |
|---|---|---|
| `moon_near` | 5,355 / 5,285 | 1,902 / 1,906 |
| `moon_far` | 3,010 / 3,024 | 2,207 / 2,210 |
| `room` | 0,133 / 0,133 | 0,270 / 0,271 |
| `gi_sun` | 0,083 / 0,083 | 0,233 / 0,234 |
| `gi_sun_low` | 0,081 / 0,081 | 0,231 / 0,231 |
| `gi_spot` | 0,060 / 0,060 | 0,217 / 0,217 |
| `gi_point` | 0,080 / 0,080 | 0,234 / 0,234 |
| `gi_rect` | 0,087 / 0,087 | 0,267 / 0,267 |
| éditeur | 0,095 / 0,096 | 0,333 / 0,333 |

- Bruit d'un run à l'autre : moins de 1 % sur les scènes GI et l'éditeur,
  jusqu'à 3 % sur la lune.
- **La lune en raster coûte 2,8 fois plus qu'en RT** (5,4 contre 1,9 ms).
  Non analysé. Piste : les ombres raster du terrain, que le RT remplace par
  des rayons. Il y a les 4 cascades de l'étoile, plus l'atlas local des deux
  phares du buggy, où chaque vue redessine tous les chunks. Le chrono par
  passe (4.1) le dira.
- Les scènes GI sont trop légères pour juger un coût de barrière : 0,06 à
  0,33 ms par frame. La lune est le seul cas où un écart de quelques pour
  cent se verrait.

## 1.1 `RenderGraph` sans client

*Fait (2026-10-25)* : `Renderer/Vulkan/RenderGraph.{h,cpp}` (~800 lignes),
`tools/RenderGraphCheck.cpp` (cible `SpaceFret_RenderGraphCheck`). Aucun code
du moteur ne s'en sert encore.

*L'API* :
- `importImage(nom, GraphImageInfo, in, out)` et `importBuffer(nom, VkBuffer,
  in, out)`. `GraphImageInfo` porte l'image, la vue, la taille, l'aspect et
  l'index bindless. Pour une ressource du `ResourceManager`, une surcharge
  prend son `GPUResourceHandle` et remplit tout (`extentFor` et `formatFor`
  ajoutés au `ResourceManager`, `isDepthFormat` sorti dans
  `VulkanFormats.h`). Swapchain, image de scène et profondeur passent par
  `GraphImageInfo`.
- `addPass(nom, file)` rend un `GraphPass&` qu'on enchaîne : `read`/`write`
  (image ou buffer, avec un `Usage`), `color(image, Load, clear)`,
  `depth(image, Load, clear)`, `execute(fonction)`. `Load` vaut `Keep`,
  `Clear` ou `DontCare`.
- `compile()`, puis `record(lot, cmd)` ; `record(cmd)` fait les deux pour un
  graphe à un seul lot, et refuse sinon.
- Dans `execute`, un `GraphContext` donne `cmd`, `image`, `view`, `extent`,
  `textureIndex` et `buffer` d'une ressource déclarée par la passe.

*Règles de la compilation* :
- **Image** : une barrière quand le layout change, quand le dernier accès
  écrivait, ou quand celui-ci écrit. Lecture après lecture du même usage :
  rien.
- **Buffer** : une écriture est rendue visible usage par usage. Une lecture
  dont l'usage n'a pas encore vu la dernière écriture reçoit une barrière
  depuis cette écriture. Une écriture attend toutes les lectures depuis la
  précédente (une barrière par usage lu). À l'enregistrement, toutes les
  barrières buffer d'une passe deviennent une seule barrière mémoire
  globale.
- **Contenu jeté** (`UNDEFINED`) : attachment en `Clear` ou `DontCare`, ou
  import en `None`.
- **storeOp** : `DONT_CARE` si aucune passe plus loin ne lit l'image et que
  sa sortie est `None`, `STORE` sinon.
- **Sorties** : la transition vers l'usage `out` se fait à la fin du lot du
  dernier accès. Rien si l'import n'est pas touché et que `in == out`.
- **Lots** : les passes consécutives d'une même file. Chaque ressource
  retient le lot de son dernier écrivain, ses lecteurs depuis, et les files
  qui ont vu la dernière écriture. Un accès d'une autre file attend le lot
  de l'écrivain ; une écriture attend en plus les lots lecteurs des autres
  files. Le producteur est marqué `signals_`, et la barrière du consommateur
  `afterWait_`, que `record` refuse tant qu'il n'y a qu'un command buffer.

*Écarts au texte du plan* :
- **Erreurs** : `std::logic_error` dans les deux configs, pas en debug
  seulement. Ça ne coûte que quelques comparaisons par passe, et le contrôle
  doit pouvoir les attraper. `ThrowRuntime` n'a pas été utilisé : il fait
  un `__debugbreak` en debug.
- **Erreurs en plus de celles du plan** :
  - une ressource deux fois dans la même passe ;
  - un usage sans layout (`AnyRead`, `AccelerationStructureBuild`) sur une
    image ;
  - des attachments de tailles différentes ;
  - une image de profondeur en attachment couleur, ou l'inverse ;
  - un contenu importé utilisé d'abord sur une autre file que la graphique,
    car on ne peut pas attendre du travail fait hors du graphe.
- **Viewport** : le graphe pose `setViewportYUp` à la taille des attachments
  après avoir ouvert la scope. Toutes les passes actuelles le font ; celles
  qui dessinent par tuiles (atlas d'ombre) le reposent.
- **`record(cmd)`** et non `execute(cmd)`, pour ne pas confondre avec le
  `execute` d'une passe.

*Détour du même jour, défait* : une première version mettait des types
Vulkan dans l'API du graphe, Batap a demandé un graphe indépendant de l'API.
Découpe faite : `Renderer/RenderGraph` neutre (handles, `Usage`, tailles,
`compile()`) + `Renderer/Vulkan/VulkanRenderGraph` (table parallèle
`VkImage`/`VkBuffer` par handle, callbacks, enregistrement), le test compilé
sans lier le moteur. Audit par un second Fable : découpe cohérente,
composition plutôt qu'héritage, quatre retouches. Puis Batap a jugé
l'indépendance inutile : tout le reste du renderer est Vulkan, aucun second
backend n'est prévu, et la découpe coûtait 770 → 1 031 lignes, deux tables
par ressource et sept types au lieu de trois, pour une portabilité qui
n'existe pas. Retour à la version Vulkan. Gardé du détour : le contrôle
couleur/profondeur, `record(cmd)`, l'import par `GPUResourceHandle`, et le
micro-banc. Leçon : chiffrer le coût d'une contrainte d'architecture avant
de la construire.

*Mesuré* :
- `RenderGraphCheck` : 39 contrôles verts, en relwithdebinfo et en debug,
  en moins d'une seconde.
  - Images : 6 contrôles. Profondeur : 5. Sorties : 4. Buffers : 6.
  - Refus : 10, dont les deux accès non déclarés à travers un
    `GraphContext` et les deux attachments du mauvais aspect.
  - Files : 8. Les lots et les attentes sont vérifiés avec trois files
    simulées (`{0, 1, 2}`), puis avec une seule (`{0, 0, 0}`), où un graphe
    compute + graphique + transfert donne un seul lot de trois passes.
- Build sans aucun avertissement, malgré `-Wconversion`, `-Wshadow-all` et
  `-Wunsafe-buffer-usage`.
- Coût CPU, micro-banc hors moteur (`cl /O2`, scratchpad du 2026-10-25,
  mesuré sur la version neutre, même algorithme) : une frame de 23 passes,
  15 images, 10 buffers, 50 barrières, déclaration + compilation,
  allocations comprises : **8,5 µs** (médiane sur 7 × 2000 frames, min 8,0,
  max 9,1). Référence pour 4.5.
- Banc d'images et validation non relancés : le graphe n'est appelé nulle
  part, le build suffit (règle du plan pour la plomberie sans client).

## 1.2 Exposition

*Fait (2026-10-26)* : `ExposurePass::record` devient `addPasses(graph, hdr,
pass, frame)`, et `draw()` disparaît avec ses barrières et sa scope.
`ScenePasses::record` construit un `RenderGraph` juste après la scène : il y
importe la HDR (`ShaderRead` → `ShaderRead`), et l'enregistre dans le command
buffer de la frame. Il y a quatre passes : mesure, réduction, total, readback.
- La grille, le bloc et le total sont importés en `ShaderRead` →
  `ShaderRead`. Le readback du slot est importé en `None` → `None` : aucune
  barrière, comme avant.
- On passe de 8 `vkCmdPipelineBarrier2` à 5 (une par passe, plus la sortie du
  total), avec les mêmes transitions. Le `ColorAttachment → ShaderRead` de la
  grille part avec le discard du bloc. Le total va de `ColorAttachment` à
  `TransferSrc` en une seule barrière, sans passer par `ShaderRead`.

*Écart* : la passe de mesure pousse l'index de la HDR dans `instanceIndex_`
(avant : 0), pour que les trois dessins partagent la même lambda.
`ExposureMeasurePS` ne lit pas cet index : il prend `frame.hdrTexture_`.

*Mesuré* (RTX 5070, 1280 × 728) :
- Banc : raster identique au pixel sur les 9 cas, RT dans le bruit.
  Validation : rien de nouveau. Le WAW du readback de 0.2 est toujours là
  (10 messages dans `room`), sans changement.
- `room` GPU : 0,133 → 0,133 ms en raster, 0,273 → 0,272 ms en RT.
- EV sur 600 frames, mesurée par un log `[EV]` temporaire (retiré depuis) :
  - en raster, l'EV finale vaut **16,0209503 au bit près** dans les 6 runs
    (2 avant, 4 après) ;
  - pendant la convergence (frames 7 à ~60), le même binaire n'est pas exact
    d'un run à l'autre. Avant/avant : 2e-6. Après/après : 6,7 à 8,6e-6.
    Avant/après : 2e-6 à 1,6e-5. C'est la même distribution ;
  - en RT : avant/avant 2,8e-4, avant/après 4,6e-5 à 2,7e-4.

*Pièges trouvés* :
- **L'EV raster n'est pas déterministe** (seuls les derniers bits du float
  bougent, pendant la convergence). Non analysé. Ce n'est pas le readback :
  il est lu après la fence de son slot. « Même EV » veut donc dire même EV
  finale, avec le même bruit.
- **Tolérance de `room_rt` trop serrée** : la référence a mesuré 40 px
  entre ses deux runs, mais les runs suivants donnent 59 à 220 px, avant
  comme après le changement. 2 runs sur 5 ont dépassé 2 × 40 px.
- Chrono de la lune bruité ce soir : la référence est à 11–12 ms en raster,
  contre 5,3 ms en 0.3. La lune n'est pas en auto-exposition, donc son
  graphe est vide : rien à voir avec l'étape.

## 1.3 Mise à jour GI

*Fait (2026-10-26)* : `GiUpdatePass::record` devient `addPasses(graph, cache,
pass, frame, passes)`, et `RtScene::copyCacheStats` devient
`addCacheStatsPass(graph, stats, frame)`. `RtScene` expose ses trois buffers
de cache (`cacheKeys`, `cacheValues`, `cacheStats`). Dans la branche RT,
`ScenePasses::record` construit un graphe `giGraph` :
- clés et valeurs importées en `ShaderRead` → `ShaderRead` (leur dernier
  lecteur est la scène de la frame précédente) ;
- compteurs importés en `ShaderWrite` → `TransferSrc`. Le remplissage à zéro
  de `buildTlas` les laisse en `ShaderWrite`, et la frame suivante attend
  `TransferSrc` avant de les remettre à zéro ;
- N passes `gi update i`, puis `gi resolve`. Chacune écrit les trois buffers
  en `ShaderWrite` et sa cible en `Load::DontCare` ;
- `gi cache stats` lit les compteurs en `TransferSrc` et écrit le readback
  du slot, importé en `None` → `None`.

Les deux barrières écrites à la main dans `ScenePasses` disparaissent :
- `ShaderRead → ShaderWrite` devient la barrière de la première mise à jour ;
- `ShaderWrite → ShaderRead` devient la sortie des clés et des valeurs.

On passe de 3N + 5 barrières à N + 3 : de 11 à 5 avec les 2 passes par
défaut. Entre deux mises à jour, le `ShaderWrite → ShaderWrite` global et le
`Color → Color` (avec discard) de la cible partent dans le même
`vkCmdPipelineBarrier2`.

*Écart* : le chrono GI garde son pool de requêtes, mais le reset et les deux
timestamps passent dans deux passes sans accès, `gi timer start` et `gi timer
end`. Une passe sans attachment n'ouvre pas de scope, or
`vkCmdResetQueryPool` y est interdit. Ces deux passes disparaissent en 4.1.

*Mesuré* :
- Banc : raster identique au pixel sur les 9 cas, RT dans le bruit
  (`gi_spot_rt` : 7 840 px / 2, contre 6 928 px / 1 entre les deux runs de la
  référence).
- Validation : rien de nouveau. Les accès des shaders aux buffers du cache
  sont vérifiés, grâce à l'heuristique activée en 0.2.
- GPU des cas GI en RT : −1 % (`gi_sun_rt` 0,235 → 0,234 ms), dans le bruit.
- Non vérifiée : la valeur du chrono GI, qui ne s'affiche qu'avec la vue
  debug du cache. La validation passe la séquence reset/timestamps sans
  erreur.

## 1.4 Ombres

*Fait (2026-10-26)* : `ShadowPass::record` devient `addPasses(graph, shadows,
pass, atlases, entryCount, terrain)`, et `recordAtlas` disparaît.
`ShadowAtlasViews` porte maintenant un nom et une `GraphImage`. L'index
bindless de l'atlas, écrit dans les entrées, se prend par
`graph.textureIndex`. Il y a trois passes :
- `shadow entries` écrit le buffer `shadows` en `TransferDst`, par
  `vkCmdUpdateBuffer` direct sur `c.buffer(shadows)`. Le découpage par
  64 Kio reste ;
- `local shadow atlas` et `cascade shadow atlas`, en `depth(atlas,
  Load::Clear, 1.f)` : chacune lit `shadows` en `ShaderRead`. Le graphe pose
  le viewport plein atlas, et la passe le repose par vue, comme avant.

Les imports, dans `ScenePasses` :
- les deux atlas en `ShaderRead` → `ShaderRead` ;
- `shadows` en `None` → `ShaderRead`. C'est un buffer par frame : celui du
  slot courant a été lu il y a `FramesInFlight` frames, et la fence de ce
  slot est attendue. Avant non plus, il n'y avait pas de barrière avant son
  écriture.

**La GI et les ombres partagent un graphe**, puisque rien d'écrit à la main
ne les sépare. La sortie des buffers du cache GI (`ShaderWrite →
ShaderRead`) part donc à la fin du graphe, juste avant la scène, dans la même
barrière que celle des atlas. Avant, elle tombait entre la GI et les ombres.
Le graphe de l'exposition, après la scène, devient `exposureGraph`.

En raster avec les deux atlas, on passe de 5 barrières à 3 :
- avant : l'écriture des entrées, plus 2 barrières par atlas ;
- après : 1 par atlas (la première porte aussi le `TransferDst →
  ShaderRead` de `shadows`), plus la sortie, partagée avec la GI en RT.

*Mesuré* :
- Banc : raster identique au pixel sur les 9 cas, dont la lune (cascades,
  et atlas local des phares) et `gi_spot` (atlas local). RT dans le bruit.
- `room_rt` est sorti en `DIFFÉRENT` (119 px), puis 73 et 102 px sur deux
  relances. C'est la tolérance trop serrée notée en 1.2.
- Validation : rien de nouveau.

## 1.5 Scène HDR

*Fait (2026-10-26, avec 1.6)* : `ScenePasses::record` construit un seul
graphe, enregistré une fois en fin de fonction. Il n'y reste plus aucune
barrière ni aucun `vkCmdBeginRendering` écrit à la main.
- **`frame constants`** : une passe en tête du graphe, qui fait
  `vkCmdUpdateBuffer` depuis une copie de `frameData_` capturée par valeur.
  Le buffer est par frame, importé en `None` → `None` (même raisonnement que
  `shadows` en 1.4).
- **Ses lecteurs le déclarent tous** : les passes GI de mise à jour et de
  résolution, la scène, la mesure de l'exposition et le tonemap.
  `GiUpdatePass::addPasses` et `ExposurePass::addPasses` prennent la
  `GraphBuffer` en plus. Le `TransferDst → ShaderRead` part donc avant le
  premier lecteur : la GI en RT, la scène en raster. Les ombres ne lisent pas
  les constantes de frame.
- **`scene`** : `color(hdr, Clear)` et `depth(depth, Clear, 0)`. Elle lit
  les constantes de frame et les deux atlas ; elle lit aussi `shadows` s'il y
  a des entrées, et les clés et les valeurs du cache en RT. Elle exécute
  `recordScene`, ou `rtDebug_` pour la vue du premier hit RT.
- `RenderTargets` porte maintenant `colorImage_` et `depthImage_`, que le
  `Renderer` remplit, car le graphe a besoin des `VkImage`. La profondeur et
  la sortie ont la taille `targets.extent_`, comme la HDR.
- L'exposition rejoint le graphe : `exposureGraph` disparaît.

*Écarts* :
- **La TLAS n'est pas déclarée.** Sa barrière reste écrite à la main dans
  `RtScene` jusqu'à 1.7, et une lecture en `AnyRead` ne produirait aucune
  barrière. 1.7 la déclare avec `AccelerationStructureRead`, pour la scène
  et pour la GI.
- **`shadows` n'est lu que s'il y a des entrées.** Sans lumière, la passe
  d'écriture n'existe pas : le graphe refuserait une lecture sans écrivain.

## 1.6 Tonemap et debug

*Fait (2026-10-26)* : une passe `tonemap` qui garde le tonemap et le debug
dans une seule scope : `color(output, DontCare)` et `depth(depth, Keep)`.
Elle lit la HDR et les constantes de frame. Les imports :
- **profondeur** en `DepthAttachment` → `None` : son `storeOp` devient
  `DONT_CARE` à la dernière passe, comme avant ;
- **sortie** (swapchain, ou image de scène de l'éditeur) en
  `ColorAttachment` → `ColorAttachment`.

**La frontière avec le `Renderer` coûte trois transitions de plus**, jusqu'à
2.2 :
- la profondeur et la sortie sont jetées deux fois : par le `Renderer`, puis
  par le graphe, avant la scène pour l'une et avant le tonemap pour l'autre ;
- une sortie `Color → Color` après le tonemap. Dans le chemin du jeu, elle
  ordonne pour la première fois le tonemap et le `LOAD` d'ImGui sur la
  swapchain, qui n'avaient aucune barrière entre eux.

En raster avec les deux atlas, sans auto-exposition, on passe de 6
`vkCmdPipelineBarrier2` à 5 dans `ScenePasses`, mais avec plus de
transitions d'image.

*Mesuré (1.5 et 1.6 ensemble)* :
- Banc : raster identique au pixel sur les 9 cas, éditeur compris
  (chemin de l'image de scène). RT dans le bruit.
- Validation : rien de nouveau.
- **Temps GPU non exploitables** : Batap jouait pendant le banc (GPU à 78 %,
  banc en 102 s au lieu de ~45 s). La lune double, les scènes GI prennent
  2 à 11 %. À remesurer machine libre, en particulier les petites scènes,
  où les trois transitions de la frontière pourraient se voir (quelques µs
  sur ~80 µs).
