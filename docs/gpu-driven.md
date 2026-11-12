# Rendu piloté par le GPU — plan

Décidé le 2026-11-12. Objectif : le CPU ne parcourt plus les lots d'instances à
chaque frame. Le GPU garde leurs données, trie lui-même ce qui est visible et
écrit ses propres commandes de dessin.

## Point de départ

- Les instances dispersées (`ScatterDraws`, `ScatterDrawer`) : un lot = un
  maillage (ou un impostor) × un bloc. À chaque frame, le CPU :
  - place chaque lot (`ScatterDraws::place`) : position relative à la caméra,
    test du cône de vue ;
  - renvoie ses données (`ScatterDrawer::upload`, ~200 octets par
    `ScatterBatchGPUData`), parce que `placed_.world_` est relatif à la caméra
    et change dès qu'elle bouge ;
  - enregistre un dessin par lot visible et par passe.
- Mesuré le 2026-11-11 (planète de Compacteur, impostors jusqu'à 300 m,
  ~26 000 lots, 720p), par frame :

  | | Placement | Envoi | Enregistrement |
  |---|---|---|---|
  | Avant culling CPU | 1,92 ms | 1,90 ms | 0,70 ms |
  | Après culling CPU (26289f4) | 0,8-1,2 ms | 0,5-0,8 ms | ~0,6 ms |
  | Sans impostors | 0,13 ms | 0,12 ms | 0,16 ms |

- Le moteur n'a ni pipeline compute ni dessin indirect. Les bindings de frame
  et les push constants ne sont visibles que des stages vertex et fragment
  (`FrameBindings.cpp`). `GraphQueue::Compute` ne sert qu'aux constructions de
  BVH, et `Usage::ShaderWrite` ne couvre que les écritures en fragment.

## Règles

- Le chemin actuel reste jusqu'à ce que le nouveau donne une image identique
  (dump au SHA256, scène statique, RT coupé), comme dans le plan RT.
- Une étape = un commit. On mesure avant de passer à la suivante : chronos CPU
  des trois étapes ci-dessus, temps GPU.
- Les données sur le GPU restent dans le repère de leur propriétaire. Seule la
  table par propriétaire est relative à la caméra, pour la précision des float
  (même règle que le rendu relatif à la caméra, plan RT 1.0).

## Plan d'action

### Phase 1 — Données de lot statiques (sans compute)

- [x] **1.1 Table de propriétaires par frame.** `ScatterOwnerGPUData`
  (binding `ScatterOwnersBinding`) : partie linéaire et axes du propriétaire,
  caméra en mètres le long de ces axes découpée en cellules GI + reste, pour
  la frame et la précédente. `ScatterDraws::place` remplit `owners_`.
- [x] **1.2 Buffer persistant de lots.** `ScatterBatchGPUData` (80 octets au
  lieu de 208) : centre en cellules GI + reste, index de propriétaire,
  matériaux, fondu, rayon d'impostor. Index alloué à l'ajout, recyclé
  `kRetireFrames` après le retrait ; écrit à la première frame où le lot est
  placé.
- [x] **1.3 Shaders.** `ScatterPlaced(batch)` reconstruit le
  `TerrainWorldGPUData` d'avant (monde et monde précédent relatifs à la
  caméra) ; les appels de GI, vecteurs de mouvement et reflets n'ont pas changé.
- [x] **1.4 CPU allégé.** `Placement` ne porte plus de matrice ; `upload`
  n'écrit plus que la table et les lots nouveaux.

Vérifié le 2026-11-12 (planète de Compacteur, caméra qui avance et tourne,
impostors à 150 m, 720p) :
- raster : 800 à 3 900 pixels à peine différents, 6 à 8 avec un écart de plus
  de 30 (arrondi des positions) ; deux runs de l'ancienne version sont
  identiques ;
- vecteurs de mouvement : 25 à 150 pixels à peine différents ;
- RT sans reflets ni GI : comme le raster ; avec GI : ~100 000 pixels à peine
  différents, 12 à 17 de plus de 30 ;
- RT avec reflets : ~400 000 pixels différents. L'historique des reflets n'est
  repris que si l'id de surface est le même d'une frame à l'autre ; l'id d'un
  lot était son rang dans la frame et changeait quand la caméra tournait, il
  est maintenant fixe et l'historique tient sur les pièces.

CPU à 300 m (~26 000 lots) : placement 0,8-1,2 → 0,64-0,67 ms, envoi
0,5-0,8 → 0,18-0,21 ms (il reste une recherche de lot par placement),
enregistrement inchangé (~0,58 ms).

### Phase 2 — Compute dans le moteur

- [x] **2.1 Pipelines compute.** `buildComputePipeline` dans `Pipelines` (une
  fonction suffit : un seul stage). Un shader `cs_6_6` s'ajoute comme les
  autres dans `ShaderLibrary` et CMake.
- [x] **2.2 Bindings.** `DrawPushStages` (vertex, fragment, compute) sur les
  bindings de frame et la plage de push constants ; `pushDraw` le passe en
  entier, comme l'exige `vkCmdPushConstants`. `bindCompute` lie les sets sur
  le point de liaison compute.
- [x] **2.3 Render graph.** Une passe sans attachement n'ouvre pas de rendu :
  rien à ajouter pour une passe compute. Usages `ComputeWrite` et
  `IndirectRead` ; une lecture en compute reste `ShaderRead`. Les buffers par
  défaut ont `INDIRECT_BUFFER`.

Vérifié le 2026-11-12 avec une passe de test temporaire (retirée) : un compute
écrit `i × 3 + 7 + slot` dans un buffer mappé de 4 096 entiers, relu à la frame
suivante sur le même slot. Juste sur les trois slots en raster et en RT ; en
debug avec `BATAP_SYNC_VALIDATION=1`, aucune erreur de validation hors des
`independentBlend` des pipelines translucides, déjà là avant. `IndirectRead`
n'est pas encore exercé (phase 3).

### Phase 3 — Culling et dessins sur le GPU

- [ ] **3.1 Compute de culling.** Un thread par lot : cône de vue, puis
  frustum exact. Il écrit des `VkDrawIndexedIndirectCommand` et un compteur
  par (pipeline, maillage).
- [ ] **3.2 Dessins indirects.** `vkCmdDrawIndexedIndirectCount` ; les shaders
  trouvent leur lot par l'index du dessin au lieu du push constant
  `instanceIndex_`.
- [ ] **3.3 Ombres.** Le même compute par cascade, avec `castsInto` et
  `shadowStaysIn` portés en HLSL.
- [ ] **3.4 Impostors.** Même chemin avec `vkCmdDrawIndirectCount`
  (6 sommets par instance).
- [ ] **3.5 *(option)* Buffer de maillages partagé.** Tous les maillages lus
  par le vertex shader : un seul dessin indirect par pipeline.

Vérifier : image identique ; chronos CPU des instances proches de zéro ; coût
du compute (cible ~0,1 ms) ; compteurs relus en debug.

### Phase 4 — Options

- Choix 3D / impostor et LOD par instance sur le GPU : plus de saut au
  changement de LOD d'un bloc.
- Occlusion par pyramide de profondeur (Hi-Z, deux phases).
- Meshes (`MeshDrawer`) et terrain sur le même chemin.
- Autres usages du compute : ombres écran avec le partage d'échantillons de
  Bend, passes de GI qui tournent aujourd'hui en pixel shaders.

## Risques

- Vecteurs de mouvement et historique des reflets (phase 1) : une erreur se
  voit en traînage, pas en plantage.
- Commandes écrites par le GPU, difficiles à déboguer : compteurs relus et vue
  debug dès 3.1.
- Le RT n'est pas concerné : les instances dispersées ne sont pas dans la TLAS.
