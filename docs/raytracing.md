# Ray tracing — plan

Décidé le 2026-09-30. Le raster actuel reste le fallback sur les machines sans
RT (MoltenVK, vieux GPU).

**Le rendu raster n'est jamais supprimé.** Un réglage utilisateur choisit entre
les deux (étape 0.2). Chaque étape RT ajoute une branche à côté du chemin
raster, qui reste complet et sélectionnable même sur un GPU RT : quand le plan
dit qu'une passe est « coupée » ou « disparaît », c'est en mode RT seulement.

## Décision

- **Ombres de l'étoile** : ray query dans `ShadowVisibility`.
- **GI** : cache hash en espace monde, clé en repère local de l'objet touché.
- **AO de contact** : SSILVB (`papers/`, `OBJECTIFS.md` §4).
- **Rebond des astres lointains** : planetshine analytique.
- **Écartés** : les sondes (DDGI), le path tracer temps réel, TAA/TSR/DLSS/FSR 2+.

## Règles

- **Temporel sur l'éclairage démodulé seulement**, jamais sur l'image finale.
  - On accumule visibilité, AO et indirect avant la multiplication par
    l'albedo.
  - Textures, arêtes et spéculaire sont recalculés à chaque frame en résolution
    native.
- **Vecteurs de mouvement exacts** : tout est rigide, donc
  `prevWorld × inverse(world) × P`. Rejet strict de l'historique (id
  d'instance, profondeur, normale), sans clamping de voisinage.
- **MSAA** en résolution native. Aucun upscaler temporel.
- **Chaque occulteur est compté une fois.**
  - Tout astre est dans la TLAS, par ses chunks de terrain ou son `Mesh_C` :
    en mode RT, la boucle `SphereOccluderBuffer` n'est plus atteinte (1.2 pour
    l'étoile, 1.5 pour le point et le spot, 5.6 pour les rect lights).
  - *(Option)* Garder les astres lointains en sphères analytiques, hors du
    masque du rayon d'ombre, pour des éclipses douces gratuites.
- **Une BLAS par mesh**, construite à sa création, jamais mise à jour (pas de
  refit). Aucun skinning : tout mouvement passe par la TLAS, reconstruite à
  chaque frame. Les futurs chunks de terrain ([OBJECTIF_TERRAIN.md](../../OBJECTIF_TERRAIN.md))
  sont des meshes comme les autres.

## Cache hash (GI)

- **Clé** : `(id du repère rigide, position locale quantifiée, octant de normale
  locale, niveau)`.
  - *Repère local, rotation comprise* : une salle ou un vaisseau qui bouge garde
    ses cellules. C'est la différence avec SHaRC, qui indexe en monde et ne
    converge pas quand la scène bouge.
  - *Octant de normale* : le dessus et le dessous d'une tôle ne partagent pas
    de cellule (pas de fuite sous le châssis).
  - *Niveau* : la taille croît avec la distance à la caméra, plancher 10–20 cm.
- **Mise à jour.**
  - Un PS basse résolution (¼ × ¼) lance ses rayons primaires en ray query
    depuis la caméra, avec 2 à 3 rebonds.
  - À chaque sommet : étoile × rayon d'ombre, plus le cache au dernier sommet.
  - Accumulation par atomiques dans un storage buffer, décroissance
    exponentielle avec hystérésis adaptative, éviction des cellules non
    touchées.
- **Lecture** : dans `ShadeSurface`, elle remplace `EvalSH9`.
  - Interpolation trilinéaire entre cellules.
  - Une cellule vide retombe sur `EvalSH9`.
- **Au point d'impact** :
  - Albedo constant du `Material` et normale géométrique, lus via un buffer
    « geometry info » par BLAS (adresse du buffer, `streamOffsets_`) avec
    `CommittedPrimitiveIndex()`.
  - Les textures viendront plus tard.
- **Limite** : l'éclairage est basse fréquence. Tout ce qui est plus petit
  qu'une cellule relève de l'AO.

## AO

**SSILVB, et non de l'AO ray tracée pure.**
- **Une seule implémentation pour les deux modes.** Les machines sans RT en ont
  besoin aussi, et le papier est déjà retenu dans `OBJECTIFS.md` §4.
- **Rayon de l'AO = taille de cellule du cache.** Le cache contient déjà
  l'occlusion au-delà de la cellule. L'AO ne couvre que ce qui est plus petit
  qu'une cellule, sinon l'indirect est assombri deux fois.
- **Application** : l'AO multiplie l'irradiance indirecte (le cache) et donne
  l'occlusion spéculaire. **Jamais l'éclairage direct**, que les rayons d'ombre
  traitent déjà.
- **En mode RT, version hybride.** Quand un pas de la marche tombe dans une zone
  ambiguë (échantillon hors écran, ou derrière un bord avec une épaisseur
  inconnue), on le termine par un ray query court, de longueur au plus une
  cellule. Ça corrige l'hypothèse d'épaisseur constante là où elle échoue.
- **RTAO pure (1–2 rayons courts + débruitage) seulement si les artefacts
  SSILVB restent visibles après l'hybride.** Elle est exacte, mais elle ne sert
  qu'aux machines RT et coûte un débruiteur de plus.

## Ombres

- **Rayon** : `RayQuery<ACCEPT_FIRST_HIT_AND_END_SEARCH | FORCE_OPAQUE>` vers `L`,
  `tMax = dL`.
- **Origine** : décalée le long de `Ngeom_` (Wächter & Binder, *RTG* ch. 6).
- **`NdotL > 0` testé avant `ShadowVisibility`.**
- **Cascades** : on saute l'atlas des cascades de l'étoile quand elle passe en RT.
- **Pénombre** (après la plomberie) : à partir de `sourceRadius_`, 1–2 rayons
  avec bruit bleu tournant, accumulation démodulée, puis filtre proportionnel à
  la largeur de pénombre (maison ou NRD SIGMA).

### Par type de lumière

| Type | Rayon dur (J1) | Source étendue (J5) |
|---|---|---|
| Point | vers `pos_`, `tMax = dL` | cône sous-tendu par la sphère `sourceRadius_` |
| Spot | idem point (cône déjà appliqué) | idem point |
| Directional | vers `-direction_`, `tMax` = borne de scène | cône d'angle `sourceRadius_` (en radians) |
| FarPoint (étoile) | vers `pos_`, `tMax = dL` | cône `asin(sourceRadius_ / dL)` |
| Rect | aucun : la carte d'ombre reste jusqu'à la source étendue (1.6 abandonnée) | tirage d'un point sur le rectangle (échantillonnage du rectangle sphérique, Ureña 2013) + estimateur ratio sur la LTC |

**Pièges :**
- **Le luminaire masque sa propre lumière.** Une ampoule ou un abat-jour qui
  englobe la source arrête tous ses rayons. Il faut un bit de masque « ne
  projette pas d'ombre » sur ces meshes, ou un `tMax = dL − sourceRadius_`.
- **Ce qui n'est pas dans la TLAS ne projette pas d'ombre.** C'est le cas des
  billboards CPU et du debug.
- **`castShadows_ = false` : pas de rayon.**
- **Verre et transmission** (plus tard) : il faudra un any-hit, ou un rayon
  qui traverse le verre pour une ombre colorée. Aujourd'hui tout est
  `FORCE_OPAQUE`.
- **Matériaux émissifs** : ce ne sont pas des lumières. Ils éclairent par le
  cache GI, sans ombre directe nette. Pour les rendre nets, il faudra les
  promouvoir en lumières.

### Plusieurs lumières (intérieur, ~6 lumières)

- **1 rayon d'ombre par lumière qui contribue.** On teste d'abord la portée
  (`radius_`), le cône et `NdotL`, et on ne trace qu'ensuite.
  - Les rayons intérieurs sont courts (`tMax = dL`), donc peu chers.
  - Ordre de grandeur, à mesurer : 0,2–0,5 ms par lumière couvrant tout l'écran
    en 1440p. Dans le pire cas (6 lumières qui se recouvrent), environ 1,5–3 ms
    sur une RTX 20, moins sur les cartes récentes.
- **Pas de ReSTIR à 6 lumières.** L'échantillonnage stochastique ne devient
  utile qu'au-delà de ~15–20 lumières qui se recouvrent. La grille de clusters
  (`OBJECTIFS.md` §3.2) suffit à couper ce qui est hors portée.
- **Gros gain côté raster** : les shadow maps cube de chaque point light
  (6 vues par lumière) disparaissent. Pour 6 lumières, ça fait 36 re-rendus de
  la scène en moins.
- **Ombres douces des lumières locales : estimateur ratio** de Heitz, Hill &
  McGuire 2018, *Combining Analytic Direct Illumination and Stochastic Shadows*.
  - L'éclairage non ombré reste analytique et net (GGX, et LTC pour les rect
    lights, déjà dans le moteur).
  - Seul le ratio « ombré / non ombré » est stochastique : 1 rayon par lumière
    vers un point tiré sur la source (sphère `sourceRadius_` ou rectangle).
  - Ce ratio est accumulé dans le temps puis filtré. Les textures et le
    spéculaire ne passent jamais par le filtre, ce qui respecte la règle de
    netteté.
  - Le papier est écrit précisément pour les lumières LTC.
- **Cache hash.** À chaque sommet de mise à jour, on boucle sur les lumières à
  portée, avec 1 rayon d'ombre chacune. C'est acceptable en ¼ × ¼.
  - Au-delà de quelques lumières : une seule lumière tirée en proportion de sa
    contribution non ombrée estimée, avec du RIS sur les candidates.
- **Fuites entre pièces.** Les deux faces d'un mur ont des octants de normale
  opposés, donc des cellules distinctes. Les murs doivent avoir une épaisseur
  réelle, ou être modélisés à double face.

## BRDF physique

État actuel (`Lighting.hlsli`) :
- GGX + Schlick-GGX (`k = (r+1)²/8`) + Fresnel de Schlick + Lambert pondéré
  par `(1−F)(1−metallic)`.
- Le spéculaire IBL lit les mips de l'HDRI produites par blit (box filter),
  sans DFG.
- `reflectivity` multiplie ce spéculaire IBL : un paramètre non physique, qui
  casse la conservation d'énergie.
- Tout est clampé par `saturate` dans une swapchain UNORM.

Ce qu'il faut, dans l'ordre :
1. **HDR + tonemap + unités physiques** (J3) : lux pour l'étoile, lumens ou
   candela pour les lampes, exposition. Sans ça, aucune amélioration de BRDF ne
   se voit.
2. **Spéculaire**
   - Smith à hauteur corrélée (Heitz 2014) à la place de Schlick-GGX.
   - Compensation multi-diffusion (Kulla-Conty 2017 / Turquin 2019) via une LUT
     DFG 2D. Sans elle, les métaux rugueux perdent jusqu'à ~60 % d'énergie.
3. **Diffus**
   - Couplage énergétique `diffus × (1 − E_spec(μ))` avec la même LUT, à la
     place de `(1−F)(1−metallic)`.
   - Diffus rugueux EON, l'Oren-Nayar à conservation d'énergie (Portsmouth et
     al. 2024, celui d'OpenPBR).
4. **Paramètres matériau**
   - Remplacer `reflectivity` par l'IOR / `specular` des diélectriques
     (F0 = ((n−1)/(n+1))², 0,04 pour n = 1,5).
   - Ajouter `emissive`, qui sert aussi au cache GI.
   - Plus tard : transmission (hublots), clearcoat.
   - Cible : un sous-ensemble d'OpenPBR, ou glTF PBR + `KHR_materials_*`.
5. **IBL**
   - Environnement préfiltré GGX par mip (importance sampling) + split-sum avec
     la LUT DFG.
   - Occlusion spéculaire à partir de l'AO.
6. **Régolithe** (niche, spécifique à l'espace) :
   - Un `ShadingModel` Lommel-Seeliger / Hapke pour le sol lunaire.
   - Il donne un disque éclairé uniformément sans assombrissement au bord, et
     l'effet d'opposition (surbrillance quand l'étoile est derrière la caméra).
   - Lambert ne sait faire ni l'un ni l'autre.
7. **Une seule BRDF partagée raster/RT** (`.hlsli`) avec `eval`, `sample` et
   `pdf`.
   - Échantillonnage VNDF : Heitz 2018, ou les calottes sphériques de Dupuy &
     Benyoub 2023, plus simples et plus rapides.
   - Indispensable pour les rebonds spéculaires du cache et pour un futur mode
     photo.
8. **Validation**
   - White furnace : matériau blanc dans un environnement uniforme blanc,
     l'image doit être uniforme à 1, quelle que soit la rugosité.
   - Comparaison avec une référence : mode photo progressif, ou Cycles /
     Mitsuba sur la même scène.

Les LTC des rect lights sont déjà ajustées sur GGX. Il suffit de leur appliquer
la même compensation multi-diffusion.

## Réflexions RT

Le paramètre `reflectivity` disparaît. La force du reflet vient du Fresnel (F0
issu de l'IOR ou de `metallic`), et sa netteté de la rugosité, par construction.

- **Le rayon.** Une direction tirée sur la VNDF GGX de la BRDF partagée, avec
  `RayQuery` dans le PS ou en compute. Pas besoin de pipeline RT ni de SBT tant
  que le matériau reste un uber-shader. On passera au pipeline RT + SER si les
  hits divergent trop.
- **Rugosité ≈ 0 : le reflet est déterministe.** Un seul rayon exact suffit,
  l'image est **nette et sans bruit**, et aucun filtre n'est nécessaire.
- **Rugosité intermédiaire : 1 rayon stochastique.** Le bruit est retiré en
  respectant la règle de netteté :
  - **Démodulation.** On divise par le terme spéculaire préintégré (la LUT DFG)
    et on remultiplie après filtrage. Les normal maps et le Fresnel restent nets
    au pixel.
  - **Réutilisation des rayons voisins** (Stachowiak 2015, *Stochastic SSR*).
    Chaque pixel réévalue sa propre BRDF sur les hits de ses voisins (ratio
    BRDF/pdf). Le gain vaut celui d'un flou, sans flouter la géométrie.
  - **Reprojection par la distance de hit.** Le reflet est reprojeté selon son
    mouvement virtuel (la parallaxe du reflet), pas selon celui de la surface.
    C'est la méthode de ReBLUR/ReLAX, et c'est ce qui évite le ghosting typique
    des reflets.
  - **Rayon de filtre proportionnel à rugosité × distance de hit.** On obtient
    le durcissement au contact : le reflet est net près de l'objet et flou loin,
    ce qui est physique.
- **Rugosité > ~0,5–0,6 :** on ne trace plus. On prend l'IBL préfiltrée,
  multipliée par l'occlusion spéculaire. À ce niveau de rugosité, la différence
  ne se voit pas (Bevy Solari place la coupure à 0,4).
- **Ombrage au point touché**
  - Lumières : boucle à portée, rayons d'ombre en ray query.
  - Diffus indirect : **lecture du cache hash**, d'où des multi-rebonds
    gratuits dans les reflets.
  - Miss : le ciel, au mip qui correspond à la rugosité.
  - Reflet dans le reflet : 1 rebond, puis l'IBL.
- **Données au hit**
  - UV, normale et tangente via le buffer geometry info.
  - Textures bindless avec un LOD calculé par **ray cones** (Akenine-Möller
    2021).
- **SSR d'abord, RT ensuite (option).** On tente le hit en espace écran ; on ne
  trace un rayon que si le SSR échoue ou sort de l'écran. Ça économise des
  rayons sur les grands sols.

## Budget (cible : 60 fps en 1440p natif sur une RTX 3060 / 2070)

Ce n'est pas du path tracing. La visibilité primaire reste en raster, et les
rayons ne servent que là où ils changent l'image. Estimations à remplacer par
des mesures (timestamps GPU / tracy) :

| Poste | Estimation |
|---|---|
| Ombres de l'étoile | 0,3–0,6 ms |
| Ombres de 6 lampes en intérieur (pire cas, recouvrement total) | 1–3 ms |
| Mise à jour du cache GI (¼ × ¼) | 0,5–1 ms |
| Réflexions (pixels avec rugosité < 0,5 seulement, SSR d'abord) | 1–3 ms |
| Débruitage des réflexions et de la pénombre | 0,5–1,5 ms |
| AO SSILVB | 0,5–1 ms |
| **Total RT + AO** | **~4–10 ms**, dont une partie compensée par la suppression des cascades et des cube maps d'ombre |

**Mesuré en 1.8 (2026-10-15, RTX 5070, 2560×1448, médiane de 3 séries)** :
GPU par frame, raster → RT, sur les scènes de test (un buggy, une dalle, le
terrain de la lune) :

| Scène | Raster | RT |
|---|---|---|
| Buggy, étoile + point light | 0,383 ms | 0,415 ms |
| Buggy, point light seul | 0,223 ms | 0,256 ms |
| Buggy, spot seul | 0,149 ms | 0,218 ms |
| Terrain proche | 0,355 ms | 0,323 ms |
| Terrain lointain | 0,258 ms | 0,293 ms |

- La passe d'ombre passe de 0,015–0,042 ms à ~0,005 ms ; la construction de
  la TLAS ajoute 0,04–0,06 ms.
- L'ombrage coûte à peu près pareil : +0,03 ms sur le buggy, −0,07 ms sur le
  terrain proche (le rayon s'arrête au premier obstacle, la carte lit 16
  texels et choisit sa cascade).
- Scènes trop légères pour juger les estimations ci-dessus : le GPU n'y est
  pas chargé et les coûts fixes dominent.

**Leviers si ça déborde :**
- un preset par effet ;
- une coupure de rugosité plus basse ;
- les réflexions rugueuses en demi-résolution, avec upsampling bilatéral ;
- une fraction du cache mise à jour par frame ;
- des rayons d'ombre au point d'impact remplacés par la lecture du cache.

**Repère** : Doom TDA impose RTGI + réflexions RT et vise 60 fps dès la
RTX 2060 S, mais avec upscaling.

## Planetshine

L'astre est traité comme une sphère lambertienne :
`E = E_étoile · (2/3)·A · (R/d)² · Φ(α)`, avec
`Φ(α) = (sin α + (π − α)·cos α) / π`.

On l'ajoute comme une lumière directionnelle faible venant du centre de l'astre.
Il faut ajouter l'albedo des astres.

## Plan d'action

Une étape = un commit. Je coche l'étape quand elle est faite et vérifiée, puis
tu commits.

**Vérification commune à chaque étape :**
- build vert en `msvc-debug` et en `msvc-relwithdebinfo` ;
- éditeur lancé sur `scenes/test_buggy.btpl` **sans erreur des couches de
  validation** ;
- dump de frame (`BATAP_DUMP_FRAME=N`) quand l'étape touche l'image.

« Image identique » signifie un dump identique au SHA256, avec le RT coupé et
sur une scène statique.

Les étapes marquées *(option)* peuvent sauter sans bloquer la suite.

### Phase 0 — Socle RT (rien ne change à l'écran)

- [x] **0.1 Détection du RT.** Dans `VulkanContext.cpp`, demander *si présentes*
      les extensions `VK_KHR_acceleration_structure`, `VK_KHR_ray_query`,
      `VK_KHR_deferred_host_operations`, ainsi que les features
      `accelerationStructure` et `rayQuery`. Exposer `rtSupported_` et logger
      `[Vulkan] RT : oui/non`.
      *Vérif* : le log ; image identique.
- [x] **0.2 Réglage RT / raster.**
      - `rayTracing_` dans `EngineSettings` ; le mode actif vaut
        `rayTracing_ && rtSupported_`.
      - Persisté dans la config utilisateur (`UserConfig`), section `render`,
        sur le modèle de la section `window` : un fichier pour l'éditeur, un
        pour le jeu.
      - Une case dans le menu Settings, pour l'éditeur et pour le jeu, grisée
        si `!rtSupported_`. Côté éditeur, elle s'applique tout de suite.
      - Pour l'instant, elle n'a aucun effet sur l'image.

      *Vérif* : la case existe, survit à un redémarrage ; image identique.
- [x] **0.3 Flags des buffers.** Dans `createBufferInternal`
      (`VulkanResources.cpp:179`), ajouter `SHADER_DEVICE_ADDRESS` et, si
      `rtSupported_`, `ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY`. Ajouter
      `ResourceManager::deviceAddress(handle)`.
      *Vérif* : image identique.
- [x] **0.4 Buffers d'AS et de scratch.** Deux créations dédiées :
      - le stockage d'AS (`ACCELERATION_STRUCTURE_STORAGE` + `SHADER_DEVICE_ADDRESS`) ;
      - le scratch (`STORAGE` + `SHADER_DEVICE_ADDRESS`), aligné sur
        `minAccelerationStructureScratchOffsetAlignment`.

      Destruction différée comme les autres.
      *Vérif* : build.
- [x] **0.5 `RtScene` vide.** `Renderer/Vulkan/RtScene.{h,cpp}`, possédée par
      `ScenePasses`, créée seulement si `rtSupported_`.
      *Vérif* : création et destruction propres à la fermeture.
- [x] **0.6 BLAS d'un mesh.**
      - Construire une BLAS par `Mesh` à sa première apparition dans `record`,
        après `flushUploads` ;
      - une géométrie par submesh, en `OPAQUE`, positions `R32G32B32_SFLOAT` à
        `streamOffsets_[Position]`, index `R32_UINT` ;
      - flags `PREFER_FAST_TRACE | ALLOW_COMPACTION` ;
      - un cache `Mesh* → BLAS`, libéré avec le mesh.

      *Vérif* : un log par BLAS (triangles, octets) ; validation propre.
- [x] **0.6b BLAS d'un chunk de terrain.** Les chunks ne sont pas des `Mesh_C` :
      ils arrivent par `TerrainDraws` dans le pool de la `TerrainPass`.
      - Une BLAS par chunk, construite la première fois qu'il est montré,
        libérée à son `remove` ; une géométrie par strate ;
      - positions `position_` lues dans le pool (vertex de 48 octets), index
        `R16_UINT`.
      - **Morph** : le VS déplace les vertices vers leurs cibles, la BLAS reste
        sur `position_`. Dans les zones de transition, la surface tracée
        diffère un peu de la surface dessinée : écart à mesurer en 1.2 (acné,
        fuite d'ombre).

      *Vérif* : un log par BLAS de chunk ; validation propre en survolant la
      lune.
- [x] **0.7 Compaction.** Requête de taille compactée, copie, puis libération de
      l'original quelques frames plus tard. Logger avant/après.
      *Vérif* : le log ; noter le gain de compaction sur les BLAS du buggy et
      des chunks de terrain.
      *Mesuré (RTX 5070)* : meshes du buggy 186 → 76 Ko (−59 %), chunk de
      2 048 triangles 133 → 47 Ko (−65 %).
- [x] **0.8 TLAS par frame.**
      - Reconstruite en tête de `record`, avec la même boucle que
        `recordMeshDraws` sur `view<Mesh_C>`, plus les chunks de terrain
        montrés (`TerrainPass::worlds()`) ;
      - une TLAS par slot de frame, agrandie au besoin, instances écrites dans
        un buffer mappé ;
      - transforms 3×4 **relatifs à la caméra de rendu**, comme le terrain est
        dessiné : en coordonnées monde, un float vaut 1 mm à 10 km et deux
        chunks voisins ne se raccorderaient plus. Les rayons devront partir de
        la position relative à la caméra (à passer du VS au PS en 1.2), pas de
        `posWS_` ;
      - `instanceCustomIndex` = index GPU de l'instance (`Slot::instance_`
        pour un chunk), masque `0xFF` ;
      - barrière entre le build et la lecture.

      *Vérif* : validation propre ; insensible au recentrage de l'origine par
      construction. Le coût du build sera mesuré en 1.8 : Tracy n'est
      instrumenté nulle part dans le moteur.
- [x] **0.9 Set 2.** Layout et pool d'un descripteur
      `ACCELERATION_STRUCTURE_KHR`, écrit à chaque frame. Un second pipeline
      layout `{textures, frame, rt}` n'existe que si `rtSupported_`.
      *Vérif* : validation propre.
- [x] **0.10 Variante de shader RT.** Compiler `PixelShader.hlsl` **et
      `TerrainPS.hlsl`** une seconde fois avec `-D BATAP_RT=1`, et déclarer ces
      variantes dans `ShaderCatalog.h` pour le hot reload. Les passes géométrie
      et terrain les choisissent quand `rayTracing_` est actif. La TLAS est
      déclarée dans `Shadows.hlsli` mais rien ne trace encore. Le SPIR-V
      `RayQuery` ne se charge pas sur un GPU sans RT, d'où la variante.
      *Vérif* : RT coché ou non, image identique (SHA du viewport, frame 300).
      dxc retire la TLAS tant qu'elle n'est pas lue : la variante est encore
      identique au shader raster. Avec `-fspv-preserve-bindings`, elle sort au
      set 2, binding 0, et `spirv-val` passe.

### Phase 1 — Ombres en ray query

- [x] **1.0 Rendu relatif à la caméra.** Aujourd'hui le VS des meshes calcule
      `world × position` en coordonnées monde : à 11 km de l'origine (la
      caméra de `test_buggy`), chaque sommet est arrondi au millimètre
      séparément, et la view retranche deux grands nombres. Le terrain fait
      déjà autrement.
      - VS des meshes : `translation − cam.pos_` d'abord (exact quand l'objet
        est proche), puis la position locale tournée ; view sans translation,
        comme `TerrainVS`. Aucun upload de plus : les instances gardent leur
        matrice absolue.
      - Interpolant `posRel_` (relatif à la caméra) à la place de `posWS_` ;
        ombrage (V, lumières, cascades) en relatif. C'est l'espace de la TLAS :
        les rayons de 1.2 en partiront.
      - Idem `ShadowVS` (matrices de lumière relatives à la caméra), billboards,
        debug.

      *Vérif* : dump quasi identique près de l'origine ; à 11 km, plus de
      tremblement du buggy en mouvement.
      *Fait* : les matrices des vues d'ombre (cascades et atlas local) sont
      construites relatives à la caméra côté CPU, en double pour les cascades
      (`cascadeViewProj(s, origin)`), et `sphere_` aussi ; `ShadowVS` lit la
      caméra. Les lumières et les occulteurs sphériques restent absolus dans
      leurs buffers, le PS retranche `cam.pos_`.
      *Mesuré (SpaceFret.exe, frame 300, 1280×728)* : scène statique buggy +
      dalle + far point light (cascades) + point light (atlas local), rendue
      à l'origine puis à 11 km avec des décalages exactement représentables
      en float (même disposition relative au bit près). Avant : 7 732 px
      différents entre les deux rendus (0,83 %), l'arrondi par sommet. Après :
      475 px (0,05 %), tous sur des égalités de profondeur dans
      l'auto-ombrage du buggy. Près de l'origine, avant/après : 44 px, delta
      max 4. Reste le pas de 1 mm des `Transform_C` en float à 11 km (la
      caméra et le buggy s'arrondissent chacun de leur côté) : décalage
      rigide, pas de déformation, invisible à 4 mm/px. Validation propre
      (éditeur debug, `test_buggy.btpl`, 60 frames).
- [x] **1.1 `NdotL` avant l'ombre.** Dans `ShadeSurface`, ne pas appeler
      `ShadowVisibility` quand `NdotL <= 0`.
      *Vérif* : image identique (gain de perf seulement).
      *Mesuré* : 4 scènes statiques (buggy + dalle, à l'origine et à 11 km),
      raster et RT : SHA identiques avant/après. La vue debug des cascades
      garde le même `debugLit`.
- [x] **1.2 Rayon vers l'étoile.** Sous `BATAP_RT`, pour FarPoint et
      Directional :
      `RayQuery<ACCEPT_FIRST_HIT_AND_END_SEARCH | FORCE_OPAQUE>`, origine
      décalée le long de `Ngeom_`, `tMax = dL` (borne de scène pour
      Directional).
      *Vérif* : dump RT contre dump cascades. Mêmes ombres, bords plus nets, pas
      d'acné.
      *Fait, avec trois écarts au texte* :
      - `CULL_BACK_FACING_TRIANGLES` en plus. Les cartes gardent les faces
        dos à la lumière, celles qu'un rayon vers la lumière rencontre de
        face ; une face traversée par-derrière appartient à l'objet que le
        rayon quitte. Contre-essai : sans culling, ou en cullant les faces
        avant (mêmes résultats), la zone de morph de la vue terrain lointaine
        a 26 197 px en désaccord sur les bords et 154 en pleine zone, contre
        2 773 et 0. L'enroulement des meshes et du terrain suit donc bien la
        convention des AS.
      - Origine poussée le long de la **normale de la face**, du côté de la
        lumière (`FaceNormal`, dérivées écran de `posRel_` en tête de PS) :
        aucune face ne s'ombre elle-même. Le long de `Ngeom_`, une face passé
        le terminateur (face dos au soleil, normale de sommet vers lui) se
        recoupait : 880 px de plus ombrés sur le terrain à 15°, la calandre
        du buggy assombrie. Poussée 0,1 mm + 1e-5 × distance à la caméra.
      - Pour l'étoile, le rayon remplace les cartes **et** les sphères : la
        boucle `SphereOccluderBuffer` n'est plus atteinte pour FarPoint et
        Directional en RT (voir 1.5).

      `tMax` du Directional : 1e30.
      *Mesuré (SpaceFret.exe, 1280×728)* : terme d'ombre comparé pixel à
      pixel, vue debug des cascades forcée, hors bords (2 px) :
      - buggy + dalle, soleil à 46° et à 15° ; terrain de la lune, soleil
        d'origine (80°) : **0 px** en désaccord en pleine zone.
      - terrain à 15° : 611 px où la carte éclaire et le rayon ombre, tous à
        moins de 8 m, sans morph : la carte y étale une traînée floue (texel
        allongé ×3,9 à 15°), le rayon une ombre nette.
      - au-delà de `CascadeRange` (200 m), le raster n'a plus d'ombre
        d'étoile ; le rayon ombre tout le terrain chargé.
      - RT à l'origine contre RT à 11 km, même disposition : 400 px (475 en
        raster). Raster identique au bit près à 1.1.

      *Écart du morph (demandé en 0.6b)*, surface dessinée contre BLAS :

      | Distance | Médiane | p99 | Max |
      |---|---|---|---|
      | < 16 m | 0 | 0 | 0 |
      | 16–100 m | < 1 cm | 2,4–3,8 cm | 8,8 cm |
      | 100–400 m | 1–4 cm | 6,5–19 cm | 30 cm |
      | 400–1 100 m | 5–8 cm | 34–40 cm | 80 cm |

      Moitié au-dessus de la BLAS, moitié en dessous. Pas d'acné grâce au
      culling. Il décale le bord d'une ombre de écart / tan(hauteur du
      soleil) : sous le pixel en médiane, 1 à 2 px au p99 avec un soleil à
      15°.
      Validation propre, RT actif : éditeur debug sur `test_buggy.btpl`
      (60 frames), jeu debug sur la vue terrain (150 frames).
- **1.3 Fusionnée dans 1.5 et 5.6.** Elle sautait la boucle
      `SphereOccluderBuffer` en RT, pour ne pas compter deux fois un astre
      déjà dans la TLAS. Pour l'étoile, c'est fait en 1.2 ; pour les lumières
      locales, la boucle disparaît d'elle-même quand elles tracent leurs
      rayons (1.5 pour le point et le spot, 5.6 pour les rect lights). Les astres entrent dans la TLAS par leurs chunks de terrain :
      le `Mesh_C` de la lune est vide et caché, et aucune scène n'utilise
      `shadowSphere`.
- [x] **1.4 Cascades coupées.** En mode RT, ne plus enregistrer l'atlas des
      cascades de l'étoile.
      *Vérif* : la passe disparaît, image inchangée par rapport à 1.2.
      *Fait* : en RT, `ScenePasses::record` saute le fit des cascades. Leurs
      entrées restent à `strength_` 0, et `ShadowPass` saute l'atlas vide.
      *Mesuré (SpaceFret.exe, 1280×728)* :
      - Tracy n'est instrumenté nulle part, donc la passe est vérifiée par un
        log temporaire dans `ShadowPass::record`, à la frame 200. Buggy +
        dalle : atlas des cascades à 4 vues en raster, 0 en RT ; l'atlas
        local garde ses 6 vues (le point light, jusqu'à 1.5). Terrain en RT :
        plus aucune passe d'ombre.
      - Buggy + dalle, soleil à 46° et à 15° ; terrain proche et lointain ;
        raster et RT : les 8 dumps sont identiques au SHA avant/après.
      - Conséquence : en RT, la vue debug des cascades n'a plus rien à
        teinter pour l'étoile et montre l'image normale.

      Validation propre, RT actif : éditeur debug sur `test_buggy.btpl`
      (120 frames), jeu debug sur le terrain proche (150 frames).
- [x] **1.5 Point et spot.** Rayon vers `pos_`, `tMax = dL`. On ne trace que si
      la lumière contribue (portée, cône, `NdotL`, `castShadows_`). L'atlas
      local n'est plus enregistré en RT, et la boucle `SphereOccluderBuffer`
      n'est plus atteinte.
      *Vérif* : scène de test avec un point light ombré ; plus aucune vue
      d'atlas local pour le point et le spot.
      *Fait* :
      - En RT, `ShadowVisibility` trace pour tous les types sauf Rect.
      - Le tri des lumières qui contribuent existait déjà dans
        `ShadeSurface` : portée, cône et `NdotL` sont testés avant, et
        `shadowIndex_` est invalide sans `castShadows_`.
      - En RT, `LocalShadowAllocator::allocate` saute le point et le spot.
        Les rect lights gardent l'atlas jusqu'à 5.6.

      *Mesuré (SpaceFret.exe, 1280×728)* : scènes sans étoile, buggy + dalle
      sous un point light ombré (P) ou un spot ombré (S), à l'origine et à
      11 km, plus une rect light ombrée (R).
      - Log temporaire à la frame 200 : en RT, P et S n'ont plus aucune vue
        d'atlas local (6 et 1 en raster) ; R garde ses 6 vues.
      - Raster identique au SHA avant/après sur P, S, leurs versions à 11 km,
        E, G, T1 et T3. En RT, G, T1 et T3 sont identiques (ils n'ont que
        l'étoile), et R est identique au raster.
      - RT contre carte : 300 px (P) et 1 170 px (S) avec un écart de
        luminance > 48, tous à moins de 2 px d'un bord d'ombre : **0 px** en
        pleine zone. Les bords sont nets là où la carte filtre. Les rayures
        du capot éclairé par le spot en raster disparaissent.
      - Origine contre 11 km, même disposition : 247 px en RT contre 314 en
        raster (P), 351 contre 508 (S).

      Validation propre, RT actif : jeu debug sur P, S et R (150 frames),
      éditeur debug sur `test_buggy.btpl` (120 frames).
- **1.6 Abandonnée (2026-10-15).** Un rayon vers le centre du rectangle donnait
      des ombres dures, fausses pour une grande source, et 5.6 les remplaçait.
      En RT, les rect lights gardent donc leur carte d'ombre et la boucle
      `SphereOccluderBuffer` jusqu'à 5.6.
- [x] **1.7 Meshes sans ombre.** Un `Mesh_C` à `castShadows_ = false` (champ
      ajouté par l'agrandissement, pour la boule du soleil) reçoit le bit de
      masque 0x2, que le rayon d'ombre ignore. Sert aussi aux luminaires qui
      englobent leur source.
      *Vérif* : le soleil éclaire à travers sa boule ; une ampoule autour d'un
      point light n'éteint plus la lumière.
      *Fait* : le champ n'existait pas encore. Il est ajouté ici, tel que
      l'étape 18 de l'agrandissement le prévoit, des deux côtés :
      - `Mesh_C::castShadows_` (`true` par défaut, absent des scènes = `true`),
        avec sa case dans l'inspecteur.
      - RT : masque d'instance `TlasMaskShadowCaster` (0x1) ou
        `TlasMaskNoShadow` (0x2), et le rayon d'ombre ne cherche que 0x1. Les
        chunks de terrain sont à 0x1. La BLAS reste construite, pour les
        rayons du cache GI.
      - Raster : `recordMeshDraws` saute le mesh dans les passes d'ombre, pour
        que les deux modes donnent la même image.

      *Mesuré (SpaceFret.exe, 1280×728), raster et RT* :
      - Étoile enfermée dans un cube de 300 m ; point light enfermé dans un
        cube de 30 cm. Avant : 45 % des pixels changent, la lumière est
        éteinte (la scène de la boule est identique au SHA à la même scène
        sans étoile). Après, à `false` : les deux scènes sont identiques au
        SHA à leur version sans cube. À `true` : toujours éteintes, comme
        avant.
      - Série de régression (buggy, étoile à 46° et 15°, point, spot, rect,
        terrain proche et lointain) identique au SHA avant/après.

      Validation propre, RT actif : jeu debug sur les trois scènes de test
      (150 frames), éditeur debug sur `test_buggy.btpl` (120 frames).
- [x] **1.8 Mesures.** Timestamps GPU (build TLAS, passe géométrie), RT
      désactivé puis activé, en 1440p. Reporter les chiffres dans « Budget ».
      *Fait* : timestamps posés par un patch temporaire autour de chaque passe
      de `ScenePasses::record`, puis retirés : le moteur n'a pas de profileur
      GPU permanent. Chiffres dans « Budget ».

### Phase 2 — Cache GI

- [x] **2.1 Buffer geometry info.**
      - Par BLAS : adresse du buffer, `streamOffsets_` et submeshes ;
      - par instance de TLAS : l'index dans ce buffer et l'index GPU de
        l'instance ;
      - exposés dans le set 2.

      *Vérif* : build, validation propre.
      *Fait* : deux tables au set 2, dans des buffers mappés par slot de
      frame, recalculées à chaque frame dans `RtScene::buildTlas` :
      - `RtGeometryGPUData` (binding 1), une par géométrie de BLAS (submesh,
        ou strate d'un chunk), dans l'ordre de la BLAS. Elle contient
        l'adresse du premier index de la géométrie, l'adresse des sommets, le
        pas des sommets et la taille d'un index. Écart au texte : les adresses
        sont résolues par géométrie au lieu d'adresse + `streamOffsets_` ;
        les UV, normales et tangentes ajouteront les leurs en 7.1.
      - `RtInstanceGPUData` (binding 2), à l'index de l'instance dans la
        TLAS : première géométrie, index GPU (`StaticMeshGPUData` pour un
        mesh, `TerrainChunkGPUData` pour un chunk) et type.
      - Au hit : `RtGeometries[RtInstances[CommittedInstanceIndex()]
        .firstGeometry_ + CommittedGeometryIndex()]`.
      - Le même parcours des submeshes (`forEachSubmesh`) construit la BLAS et
        la table, pour que l'ordre des géométries ne puisse pas diverger.
      - Les tables sont recalculées et non stockées avec la BLAS, parce que
        le pool de terrain change d'adresse quand il grandit, alors que la
        BLAS reste valable.
      - Les adresses sont écrites en deux `uint`, que le shader recompose en
        64 bits : `shaderInt64` rejoint les conditions de `rtSupported_`.
      - Rien ne lit encore les tables : 2.2 les branche côté shader.

      *Mesuré* :
      - Log temporaire à la frame 200. Buggy : 8 instances, 8 géométries
        (index 32 bits, un sommet tous les 12 octets). Terrain proche :
        37 instances, 247 géométries, soit 7 meshes + 30 chunks × 8 strates
        (index 16 bits, un sommet tous les 48 octets) ; les strates vides ont
        0 triangle, comme dans la BLAS. L'index GPU de chaque instance est
        celui de la TLAS.
      - Raster et RT identiques au SHA sur la série de régression.

      Validation propre, RT actif : jeu debug sur le buggy et le terrain
      proche, éditeur debug sur `test_buggy.btpl`.
- [ ] **2.2 Vue debug « premier hit ».**
      - Un PS plein écran (variante RT) lance un rayon caméra reconstruit
        depuis `right_/up_/fwd_/fov_` ;
      - au hit, il lit le triangle via `CommittedPrimitiveIndex()` et affiche
        albedo et normale géométrique.

      Valide toute la chaîne au hit, sans encore de cache.
      *Vérif* : le dump ressemble à la scène, sans ombrage.
- [ ] **2.3 Table de hachage.**
      - Storage buffer de clés et de valeurs (capacité 2²⁰ pour commencer),
        avec une passe d'effacement ;
      - activer `fragmentStoresAndAtomics`, et les atomiques 64 bits si les
        clés sont en 64 bits.

      *Vérif* : build, validation propre.
- [ ] **2.4 Vue debug « clé de cellule ».** Chaque pixel calcule sa clé
      (repère rigide, position locale, octant de normale, niveau) et affiche une
      couleur dérivée de cette clé.
      *Vérif* : les cellules sont fixes sur les objets quand ils bougent,
      grossissent avec la distance, et diffèrent entre les deux faces d'une
      tôle.
- [ ] **2.5 Mise à jour, un rebond.** Un PS en ¼ × ¼ :
      - rayon caméra, puis rebond diffus ;
      - au second hit, l'étoile multipliée par un rayon d'ombre ;
      - accumulation atomique dans la cellule du premier hit.

      Ajouter une vue debug du cache.
      *Vérif* : la vue debug montre le sol éclairé qui teinte ce qui l'entoure.
- [ ] **2.6 Lecture dans `ShadeSurface`.** En variante RT, la lecture du cache
      remplace `EvalSH9`. Une cellule vide retombe sur `EvalSH9`.
      *Vérif* : le dessous du buggy et les cratères reçoivent de la lumière
      rebondie.
- [ ] **2.7 Multi-rebonds.** Lire le cache au dernier sommet, puis passer à 2–3
      rebonds.
      *Vérif* : l'intérieur des cratères s'éclaircit un peu plus.
- [ ] **2.8 Vieillissement.** Décroissance exponentielle, hystérésis
      adaptative, éviction des cellules qui ne sont plus touchées.
      *Vérif* : le taux d'occupation de la table reste stable ; une lumière
      qu'on éteint fait disparaître son rebond en moins d'une seconde.
- [ ] **2.9 Interpolation trilinéaire.** 8 lectures, et un fondu entre niveaux.
      *Vérif* : plus d'aspect en blocs.
- [ ] **2.10 Lumières locales dans la mise à jour.** Boucle sur les lumières à
      portée, avec un rayon d'ombre pour chacune.
      *Vérif* : une pièce éclairée par une lampe reçoit du rebond.
- [ ] **2.11 Mesures.** Reporter les chiffres dans « Budget ».

### Phase 3 — Plomberie commune

- [ ] **3.1 HDR + tonemap.** Rendu dans une cible `RGBA16F`, puis une passe
      tonemap vers la swapchain (choix du tonemapper à faire dans l'étape). Le
      `saturate` de `PixelShader.hlsl` disparaît.
      *Vérif* : le dump ne clippe plus les hautes lumières.
- [ ] **3.2 Unités physiques.** Lux pour l'étoile, lumens ou candela pour les
      lampes, et une exposition.
      *Vérif* : réglages de `test_buggy.btpl` mis à jour, image cohérente.
- [ ] **3.3 Prépasse profondeur + normales.** C'est `OBJECTIFS.md` §4 étape 3.
- [ ] **3.4 Slots d'instance stables.** Remplacer le swap-remove des instances
      de mesh, ou ajouter une indirection stable.
      *Vérif* : un id ne change pas quand une autre entité est supprimée.
- [ ] **3.5 Matrices précédentes.** Garder la matrice monde de la frame
      précédente par instance, et le view-proj précédent dans `CameraGPUData`.
      Ajouter une vue debug des vecteurs de mouvement.
      *Vérif* : vecteurs nuls sur ce qui voyage avec la caméra, et justes
      sur le buggy.
- [ ] **3.6 MSAA.** MSAA sur la passe géométrie, avec le resolve avant le
      tonemap.
      *Vérif* : arêtes lissées ; coût mesuré dans tracy.

### Phase 3b — BRDF physique

- [ ] **B.1 Smith à hauteur corrélée** (Heitz 2014), à la place de
      Schlick-GGX.
      *Vérif* : le dump change peu, surtout aux angles rasants.
- [ ] **B.2 LUT DFG.** Générée au démarrage par un PS, sur une cible 2D.
- [ ] **B.3 Compensation multi-diffusion** (Kulla-Conty) via la LUT.
      *Vérif* : les métaux rugueux ne s'assombrissent plus.
- [ ] **B.4 Couplage diffus/spéculaire** par la LUT, à la place de
      `(1−F)(1−metallic)`.
- [ ] **B.5 `reflectivity` remplacé par l'IOR.** Changement du `Material`, du
      sérialiseur `.bmat` et de l'import assimp.
- [ ] **B.6 `emissive`** dans le `Material`, et pris en compte par le cache.
- [ ] **B.7 IBL préfiltrée GGX.** Une passe de préfiltrage par mip, plus le
      split-sum avec la LUT.
- [ ] **B.8 Scène white furnace.**
      *Vérif* : l'image est uniforme pour toutes les rugosités.
- [ ] **B.9 Échantillonnage VNDF** (calottes sphériques, Dupuy & Benyoub 2023),
      `eval`/`sample`/`pdf` dans un `.hlsli` partagé.
- [ ] **B.10 Diffus EON** *(option)*.
- [ ] **B.11 `ShadingModel` régolithe** (Lommel-Seeliger / Hapke) *(option)*.

### Phase 4 — AO

- [ ] **4.1–4.4** = `OBJECTIFS.md` §4, étapes 4 à 7. Le rayon d'action de l'AO
      vaut la taille de cellule du cache, et l'AO multiplie l'irradiance du
      cache.
- [ ] **4.5 Hybride RT** *(option)*. Un ray query court sur les pas ambigus.

### Phase 5 — Pénombres

- [ ] **5.1 Texture de bruit bleu**, et le pixel passé à `ShadowVisibility`.
- [ ] **5.2 Étoile, N rayons dans le cône**, sans temporel.
      *Vérif* : pénombre visible et grain limité à la bande de pénombre.
- [ ] **5.3 Accumulation temporelle de la visibilité**, démodulée, avec rejet
      strict.
- [ ] **5.4 Filtre spatial proportionnel à la largeur de pénombre.** Repasser
      à 1–2 rayons.
- [ ] **5.5 Point et spot à sphère `sourceRadius_`** par l'estimateur ratio
      (Heitz 2018).
- [ ] **5.6 Rect par tirage sur le rectangle** et l'estimateur ratio sur la
      LTC. Remplace la carte d'ombre des rect lights en RT (1.6 abandonnée) :
      l'atlas local n'est plus enregistré, et la boucle
      `SphereOccluderBuffer` n'est plus atteinte.

### Phase 6 — Planetshine

- [ ] **6.1 Albedo des astres** (composant moteur générique).
- [ ] **6.2 Lumière directionnelle de rebond** par astre éclairé, avec la
      formule de la section Planetshine.
      *Vérif* : la face nocturne de la lune est éclairée par la planète.

### Phase 7 — Réflexions

- [ ] **7.1 Attributs au hit** (UV, normale, tangente) et LOD par ray cones.
- [ ] **7.2 Miroirs** : rugosité < 0,05, un rayon déterministe, sans filtre.
      *Vérif* : un reflet net d'un objet hors écran.
- [ ] **7.3 Rugosité intermédiaire** : VNDF, et coupure à 0,5–0,6 vers l'IBL.
- [ ] **7.4 Démodulation + réutilisation des rayons voisins** (Stachowiak).
- [ ] **7.5 Reprojection par la distance de hit.**
- [ ] **7.6 SSR d'abord** *(option)*.

## Portée

**Moteur, réutilisable tel quel** : le socle RT et le fallback, les ombres en ray
query (tout type de lumière), le cache hash à clé locale, l'AO SSILVB, les
règles temporelles et le MSAA.

**Hypothèses de Space Fret, à lever pour un autre jeu :**

| Hypothèse | Ce qu'il faut ajouter sinon |
|---|---|
| Moins de ~15–20 lumières qui se recouvrent | Au-delà : échantillonnage stochastique des lumières (ReSTIR DI, façon MegaLights) dans le PS et aux sommets du cache. |
| Tout est rigide | Mesh skinné : skinning en compute (le moteur n'a pas de compute), refit de BLAS par frame, positions précédentes par vertex pour les vecteurs de mouvement. Pour la clé du cache, indexer dans l'espace de la pose de repos (position de bind en attribut). |
| Pas d'alpha test | Feuillage, grilles : any-hit ou opacity micromaps, à la place de `FORCE_OPAQUE` partout. |
| Astres sphériques | Le planetshine et le partage sphères / TLAS servent à tout jeu spatial. Ailleurs, ils ne servent à rien, sans rien casser. |
| Origine flottante dans le repère de l'astre (OBJECTIF_AGRANDISSEMENT) | Un jeu sans origine flottante garde un monde de quelques km au plus. Les clés locales du cache n'en dépendent pas. |

## Points ouverts

- **Support de `VK_KHR_ray_tracing_position_fetch` dans dxc/HLSL.** S'il existe,
  il remplace le buffer geometry info.
- **Photons de l'étoile** pour alimenter le cache : une expérience plus tard.

## Références

- SHaRC — https://github.com/NVIDIA-RTX/SHARC
- NRD (SIGMA) — https://github.com/NVIDIA-RTX/NRD
- vk_raytracing_tutorial_KHR — https://github.com/nvpro-samples/vk_raytracing_tutorial_KHR
- NVIDIA RT best practices — https://developer.nvidia.com/blog/best-practices-for-using-nvidia-rtx-ray-tracing-updated/
- Bevy Solari (world cache) — https://jms55.github.io/posts/2025-12-27-solari-bevy-0-18/
- AMD GI-1.0 (cache hash) — https://arxiv.org/html/2310.19855
- Heitz, Hill, McGuire 2018, ombres stochastiques + éclairage analytique — https://eheitzresearch.wordpress.com/705-2/
- Stachowiak 2015, Stochastic Screen-Space Reflections (réutilisation des rayons) — https://www.ea.com/frostbite/news/stochastic-screen-space-reflections
- Ray cones (LOD texture en RT) — https://research.nvidia.com/publication/2021-04_improved-shader-and-texture-level-detail-using-ray-cones
- Ray Tracing Gems ch. 6 (offset) — https://link.springer.com/chapter/10.1007/978-1-4842-4427-2_6
