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

- [x] **3.1 Compute de culling.** `ScatterCullCS`, un thread par lot : cône de
  vue de la caméra, puis pour chaque cascade `castsInto`, cône de vue de
  l'ombre et `shadowStaysIn` portés en HLSL (le chemin CPU garde les mêmes
  tests). Il ajoute une `VkDrawIndexedIndirectCommand` par lot visible et par
  groupe de dessin (maillage × sous-maillage), avec un compteur atomique par
  (vue, groupe). Six vues : caméra, quatre cascades, lumières locales (toutes
  les ombres, sans culling, comme le CPU).
- [x] **3.2 Dessins indirects.** Un `vkCmdDrawIndexedIndirectCount` par
  groupe. `firstInstance` est le premier index du lot ; les shaders trouvent
  leur lot par `ScatterInstanceBatchBuffer[SV_InstanceID]`, sur les deux
  chemins.
- [x] **3.3 Ombres.** Une région de commandes par cascade (`shadowOffset_`,
  `shadowStride_`), lue par les passes d'atlas.
- [x] **3.4 Impostors.** Groupe 0 : un quad indexé (6 indices).
- [ ] **3.5 *(option)* Buffer de maillages partagé.** Tous les maillages lus
  par le vertex shader : un seul dessin indirect par pipeline.
- [x] **3.6 Compaction par instance.** Le compute écrit les index des instances
  visibles dans une liste par (vue, groupe) ; une commande par (vue, groupe),
  dont il incrémente `instanceCount_` par lot visible. Un
  `vkCmdDrawIndexedIndirect` par groupe, `firstInstance` à 0 : le vertex
  shader lit `ScatterVisibleBuffer[instanceIndex_ + SV_InstanceID]`
  (`instanceIndex_` = `InvalidGPUIndex` sur le chemin CPU). Un groupe de 64
  threads par lot : le thread 0 teste et réserve, tous écrivent les index.
  Ne demande plus `drawIndirectCount`, `multiDrawIndirect` ni
  `drawIndirectFirstInstance` (que la phase 3 utilisait sans l'activer).

Actif par défaut ; `BATAP_GPU_DRIVEN=0` garde le chemin CPU. En mode GPU, `place` ne calcule plus
que les propriétaires et les lots jamais placés, gardés dans `placed_` jusqu'à
ce que le renderer les prenne. Le drawer écrit chaque lot une fois et ne
construit plus de liste par frame.

Vérifié le 2026-11-12 (même caméra qu'en phase 1, raster, impostors à 150 m) :
- image identique au chemin GPU sans culling d'ombre ; vecteurs de mouvement
  identiques à la référence ; chemin CPU (`BATAP_GPU_DRIVEN=0`) identique au
  pixel à avant la phase ;
- compteurs relus : autant de lanceurs par cascade que le CPU, frame par
  frame. Les écarts d'image entre les deux chemins (≤ 3 000 pixels) viennent
  du contenu : les tas se remplissent sur un thread et le terrain travaille au
  budget de temps, donc à frame égale le contenu dépend du temps CPU ;
- RT : ~25 000 pixels écartés de 1/765 d'un run à l'autre ; l'ordre des
  dessins dans un groupe suit les atomiques ;
- debug avec `BATAP_SYNC_VALIDATION=1` (raster, RT avec GI et reflets, chemin
  CPU) : seulement les `independentBlend` déjà là. La validation a trouvé deux
  copies vers le même lot dans une frame (placeholder vide puis lot) : les
  écritures de lots passent par une table, une copie par lot et par frame.

Mesuré (impostors à 300 m, ~23 800 lots), par frame :

| | Chemin CPU | Chemin GPU |
|---|---|---|
| `place` | 0,70 ms | 0,001 ms |
| `upload` | 0,25 ms | 0,004 ms |
| Enregistrement (prépass, éclairée, 4 cascades) | 1,0 ms | 0,07 ms |
| Compute de culling (GPU) | — | 11 µs |
| Dessins de déchets (GPU) | ~2,4 ms | ~3,0 ms |

Constat : le GPU dessine le même ensemble, mais plus lentement (+0,5 ms par
frame). Un sous-dessin indirect coûte ~0,35 µs contre ~0,19 µs pour un dessin
direct, et il y en a ~8 500 par frame : un par lot et par passe, et beaucoup
de lots n'ont que quelques instances. Des atomiques agrégés par wave n'y
changent rien (et le compute passe à 45 µs) : ce n'est pas la contention.
Piste 3.6 : le compute écrit les index des instances visibles dans une liste
par (vue, groupe), avec un seul dessin par groupe, soit ~150 dessins par passe
au lieu de milliers. Le vertex shader lit son instance dans cette liste.

Mesuré le 2026-11-13 après 3.6 (impostors à 300 m, 1,22 M instances, raster
avec cascades et ombres écran, caméra `BATAP_VIEW=3`, frame 1500). Chronos
GPU par passe et statistiques de pipeline posés temporairement dans le render
graph (retirés) ; « déchets seuls » = timestamps autour des dessins scatter :

| | Chemin CPU | Chemin GPU |
|---|---|---|
| Frame | 3,48 ms | 3,95 ms (phase 3 : 4,05) |
| Atlas des cascades | 0,73-0,76 ms | 0,91-0,95 ms |
| Prépass | 0,82 ms | 0,95-0,97 ms |
| Éclairée | 1,43 ms | 1,50-1,54 ms |
| Déchets seuls : ombres / prépass / éclairée | 0,29 / 0,69 / 1,15 ms | 0,50 / 0,84 / 1,26 ms |
| Compute de culling | — | 19 µs |
| Invocations de vertex shader (ombres) | 15 795 090 | 15 795 114 |

Image identique au pixel à la phase 3 (raster, vecteurs de mouvement), et
entre les deux chemins à la frame 2000. Le GPU traite les mêmes sommets et les
mêmes primitives : la dépense en plus vient de la façon de les dessiner, pas
du nombre de dessins. Écarté par mesure :
- coût par dessin : 8 dessins indirects vides de plus par groupe coûtent
  ~17 ns chacun ;
- indirect : le chemin CPU en `vkCmdDrawIndexedIndirect` (commandes écrites
  par le CPU) va aussi vite qu'en direct ;
- taille des dessins : les comptes du compute rejoués en dessins directs, un
  par groupe ou par tranches de 16 instances, sont aussi lents ;
- lecture de la liste : le chemin CPU qui lit ses instances dans une liste
  identité écrite par le compute va aussi vite que sans ; liste copiée par un
  transfert, profondeur des lectures dépendantes (lot rangé avec l'instance) :
  sans effet ;
- ordre : ordre inversé, lots non recyclés côté GPU, dessins mélangés côté CPU :
  sans effet ;
- cache : le compute lancé sur le chemin CPU, ou 48 Mo écrits avant les
  ombres, ne le ralentissent pas ;
- ordre spatial : une liste rangée lot par lot dans l'ordre de Morton des
  centres (deux computes, sans atomiques) donne la même image et les mêmes
  temps de dessin, et sa passe de compaction coûte 0,3-0,8 ms ;
- lectures divergentes du lot : une boucle `WaveReadLaneFirst` qui charge
  chaque lot d'un warp de façon uniforme (image juste) ne change rien.

Il reste une seule différence : un dessin du chemin CPU ne contient qu'un lot,
un dessin du chemin GPU enchaîne des instances de lots différents. C'est la
seule configuration lente mesurée ; la phase 3 (multi-draw indirect, un
sous-dessin par lot) l'était aussi, sans doute parce que la carte fusionne les
sous-dessins. Ni l'ordre spatial ni les lectures du lot n'expliquent la
différence : il faut des compteurs matériels (Nsight Perf SDK) pour savoir
quelle unité sature.

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
