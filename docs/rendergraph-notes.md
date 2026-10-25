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
