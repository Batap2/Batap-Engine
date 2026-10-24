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
  - *Demi-axe dominant de la normale locale* (±X, ±Y ou ±Z, 6 valeurs) : le
    dessus et le dessous d'une tôle ne partagent pas de cellule (pas de fuite
    sous le châssis). L'octant, prévu d'abord, a été écarté en 2.5 : sur une
    face alignée sur un axe de l'objet, une composante de la normale vaut
    zéro et son signe est du bruit, différent entre dérivées d'écran et
    triangle du rayon.
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
- éditeur lancé sur `scenes/test_buggy_fusee.btpl` **sans erreur des couches
  de validation** ;
- dump de frame (`BATAP_DUMP_FRAME=N`) quand l'étape touche l'image.

« Image identique » signifie un dump identique au SHA256, avec le RT coupé et
sur une scène statique. Depuis l'origine mobile (agrandissement, étapes 12 à
16) : sur la copie statique de `test_buggy_fusee` (`make_static_scene.py`,
physique retirée, caméra fixe), deux runs de la même scène peuvent différer
de quelques pixels (mesuré : 4 px en RT, ~210 px en raster sur le bord de
l'ombre portée, à plus de 8 niveaux). Mesurer ce bruit avant de juger un
avant/après, et compter « identique » ce qui reste dedans.

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
- [x] **2.2 Vue debug « premier hit ».**
      - Un PS plein écran (variante RT) lance un rayon caméra reconstruit
        depuis `right_/up_/fwd_/fov_` ;
      - au hit, il lit le triangle via `CommittedPrimitiveIndex()` et affiche
        albedo et normale géométrique.

      Valide toute la chaîne au hit, sans encore de cache.
      *Vérif* : le dump ressemble à la scène, sans ombrage.
      *Fait* : `RtDebugPS.hlsl` (variante RT, triangle plein écran de
      `SkyVS`), dans `RtDebugPass`, choisie par le menu View → « RT first
      hit » de l'éditeur (`World::showRtFirstHit_`, bit `DebugRtFirstHit`).
      En RT avec ce bit, elle remplace géométrie, terrain, billboards et ciel ;
      le debug filaire reste dessiné par-dessus.
      - Rayon de `znear_` à `zfar_`, masque `ShadowCaster | NoShadow` : les
        meshes sans ombre (boule du soleil, ampoules) restent visibles.
      - Au hit : `RtInstances[CommittedInstanceIndex()]`, puis
        `RtGeometries[firstGeometry_ + CommittedGeometryIndex()]`, puis les
        3 index et les 3 positions lus à l'adresse (`vk::RawBufferLoad`, les
        index 16 bits extraits d'un mot de 32). Normale géométrique = produit
        vectoriel des arêtes après `CommittedObjectToWorld3x4()`. Matériau par
        `StaticMeshGPUData` ou `TerrainChunkGPUData` selon le type.
      - Moitié gauche : albedo constant du matériau. Moitié droite :
        `n × 0,5 + 0,5`. Miss en noir.

      *Mesuré (SpaceFret.exe, 1280×728, RT)* : buggy + dalle, terrain proche
      et lointain, ampoule. Albedo : dalle grise, buggy blanc (son matériau
      constant, la couleur est dans les textures), strates de la lune avec la
      roche plus sombre. Normales : dalle à +Y sur 59 % de la moitié droite
      (0 pixel inversé), capot vers le haut, relief lunaire continu, ciel en
      miss. Flag coupé : RT et raster identiques au SHA à 2.1.

      Validation propre, RT actif, vue active : jeu debug sur le buggy et le
      terrain proche (150 frames) ; vue coupée : éditeur debug sur
      `test_buggy.btpl` (120 frames).
- [x] **2.3 Table de hachage.**
      - Storage buffer de clés et de valeurs (capacité 2²⁰ pour commencer),
        avec une passe d'effacement ;
      - activer `fragmentStoresAndAtomics`, et les atomiques 64 bits si les
        clés sont en 64 bits.

      *Vérif* : build, validation propre.
      *Fait (2026-10-23)* :
      - Deux buffers statiques dans `RtScene`, au set 2 : clés
        (`uint64` par case, 0 = vide, binding 3, 8 Mo) et valeurs
        (`RtCacheValueGPUData` : somme RGB en virgule fixe + compteur,
        16 octets, binding 4, 16 Mo). `RtCacheCapacity = 2²⁰` dans
        `ShaderInterop.h`.
      - Effacement par `vkCmdFillBuffer` à la première frame
        (`RtScene::clearCache`, hors rendu, barrière vers lecture). Pas de
        compute : inutile pour un remplissage à zéro. L'éviction continue
        viendra en 2.8.
      - Features : `fragmentStoresAndAtomics` et `shaderInt64` demandés
        ensemble « si présents » ; `shaderBufferInt64Atomics` par un second
        `VkPhysicalDeviceVulkan12Features` que vk-bootstrap fusionne avec
        celui des features requises (`FeaturesChain::add_structure`). Une
        carte sans l'un des trois passe en raster, Vulkan 1.2 requis
        inchangé. 
      - Rien ne lit ni n'écrit la table : 2.4 calcule les clés, 2.5 écrit.

      Build vert ; `RT : oui` sur la RTX 5070 ; validation propre, RT actif :
      jeu debug sur la copie statique de `test_buggy_fusee` (300 frames),
      éditeur debug sur `test_buggy_fusee` (120 frames).
- [x] **2.4 Vue debug « clé de cellule ».** Chaque pixel calcule sa clé
      (repère rigide, position locale, octant de normale, niveau) et affiche une
      couleur dérivée de cette clé.
      *Vérif* : les cellules sont fixes sur les objets quand ils bougent,
      grossissent avec la distance, et diffèrent entre les deux faces d'une
      tôle.
      *Fait (2026-10-23)* : `GiCache.hlsli`, inclus par `Lighting.hlsli`.
      - **Taille des cellules** : `15 cm × 2^niveau`, `niveau =
        ⌊log2(distance / 7,5 m)⌋` borné à 0..15. Donc 15 cm jusqu'à 15 m,
        30 cm jusqu'à 30 m, 1,2 m à 100 m, ~10 m à 500 m : environ 1/50 à
        1/100 de la distance, soit 0,6 à 1,2° de champ, et 10 à 80 rayons de
        la passe ¼ × ¼ par cellule et par frame.
      - **Repère** : les axes de la matrice monde de l'instance, normalisés
        (l'échelle n'entre pas dans les positions locales) ; translation
        relative à la caméra. Mesh : `StaticMeshGPUData` et son index GPU.
        Chunk : `TerrainWorldGPUData` et l'index du chunk. Le PS remplit
        `Surface::gi_` (repère, position locale, normale locale) ; la clé est
        calculée dans `ShadeSurface`, le niveau depuis `length(posRel_)`.
      - **Clé** : deux `uint`, pas de 64 bits, donc disponible aussi en
        raster. Haut : bit valide, repère 20 bits (type 1 + index 19), niveau
        4, octant 3, 4 bits hauts de x. Bas : 8 bits bas de x, y 12, z 12.
        Cellules de −2048 à +2047 par axe dans le repère de l'objet.
      - Vue : menu View → « GI cells », bit `DebugGiCells`, couleur = hash de
        la clé, dans `ShadeSurface` comme la vue des cascades. Et « GI
        sides » (`DebugGiSides`, d'abord « GI octants ») : rouge, vert, bleu
        = axe dominant de la normale locale, clair pour +, sombre pour −.

      *Mesuré* sur la copie statique de `test_buggy_fusee`, frame 5000, et
      deux copies où buggy, fusée et caméra sont déplacés ensemble (levés de
      30 cm le long de la verticale locale ; tournés de 30° autour d'elle) :
      - **Fixes sur les objets** : les 6 552 pixels du buggy et de la fusée
        sont identiques au bit près dans les trois rendus ; le terrain, qui
        n'a pas bougé avec la caméra, change partout.
      - **Même clé en RT et en raster** : identiques au bit près sur ces
        pixels.
      - **Grossissent avec la distance** : 12 280 couleurs distinctes dans la
        bande de l'image la plus proche, 2 588 dans la plus lointaine.
      - **Deux faces d'une tôle** : par construction (octant dans la clé),
        visible sur les facettes de la fusée ; pas de tôle vue des deux côtés
        dans la scène.
      - **Terrain entre deux runs** : les frontières de cellules sont
        identiques à 100 % (317 684 pixels d'arête ; 75 % si l'on décale d'un
        pixel), mais les couleurs changent : l'index de slot d'un chunk
        dépend de l'ordre de construction. Dans un run, c'est cohérent. Voir
        « Points ouverts ».

      Validation propre, vue active, RT actif : jeu debug (300 frames) et
      éditeur debug sur `test_buggy_fusee` (120 frames).
- [x] **2.5 Mise à jour, un rebond.** Un PS en ¼ × ¼ :
      - rayon caméra (`TMin = znear_`, `TMax` constant de 1e8 m comme
        `RtDebugPS` : la caméra n'a plus de `zfar_`), puis rebond diffus ;
      - au second hit, l'étoile multipliée par un rayon d'ombre ;
      - accumulation atomique dans la cellule du premier hit.

      Ajouter une vue debug du cache.
      *Vérif* : la vue debug montre le sol éclairé qui teinte ce qui l'entoure.
      *Fait (2026-10-23)* :
      - `GiUpdatePass` + `GiUpdatePS.hlsl` (variante RT), triangle plein
        écran sur une cible RGBA8 d'un quart de la frame, avant les ombres.
        Par pixel : rayon caméra décalé au hasard dans le pixel (graine =
        pixel + numéro de frame), insertion de la clé du premier hit (CAS 64
        bits, 16 sondes linéaires), rebond en cosinus autour de la normale
        géométrique, puis au second hit `albedo × Σ étoile (couleur ×
        intensité × atténuation × N·L × rayon d'ombre)` ; miss = π × ciel ;
        matériau `Unlit` = π × albedo. Somme en virgule fixe (×256) plus
        compteur, par atomiques 32 bits.
      - `RtHit.hlsli` : la résolution du hit (triangle, normale orientée vers
        le rayon, matériau, `GiPoint`), partagée avec `RtDebugPS`.
      - `FrameGPUData` devient par frame (numéro de frame, taille de la
        passe), écrit à chaque frame. Barrières `ShaderWrite` ajoutées à
        `VulkanBarrier`.
      - Vue View → « GI cache » (`DebugGiCache`, RT seulement) : irradiance
        de la cellule du pixel, magenta si la cellule n'existe pas.
      - **Clé** : l'octant est remplacé par le demi-axe dominant (voir « Cache
        hash »), et la normale de l'écran est la normale de face orientée
        vers la caméra (`GiFacing`), comme celle du rayon.

      *Mesuré* (copie statique de `test_buggy_fusee`, RT, frame 5000, ciel
      noir : le seul indirect est le rebond du régolithe) :
      - Flancs de la fusée à E ≈ 0,65 (saturé par endroits), buggy ≈ 0,45,
        contre 0,1 à 0,2 pour le sol lui-même, qui ne voit que ses pentes
        voisines : **le sol éclairé teinte ce qui l'entoure**. Le rendu
        normal est identique à celui de l'étape 15 de l'agrandissement : la
        passe tourne sans toucher l'image tant que 2.6 ne lit pas le cache.
      - Cellules vides vues par la caméra : 0,17 % de l'image avec l'octant,
        dont 1 075 px sur la fusée et le buggy ; un test clé contre clé
        (rayon lancé depuis le PS vers son propre point) donnait 2 632 px sur
        5 969 en désaccord d'octant, 0 de repère ou de niveau, 7 de position.
        Avec le demi-axe : 0,08 %, 371 px sur les objets, en mouchetures sur
        les segments très tessellés de la fusée (dérivées d'écran aux bords
        de triangles). Repli `EvalSH9` en 2.6.
      - Échantillons par cellule à la frame 5000 : médiane 3 000 sur la
        fusée, 5 000 à 10 000 sur le sol ; minimum 78.
      - Jamais lu par la passe : `samples_` déborde après 2³² échantillons
        et la somme après ~2³²/256 : 2.8 (vieillissement) borne les deux.

      Validation propre, RT actif : jeu debug sur la copie statique (vue
      active puis coupée), éditeur debug sur `test_buggy_fusee`.
- [x] **2.6 Lecture dans `ShadeSurface`.** En variante RT, la lecture du cache
      remplace `EvalSH9`. Une cellule vide retombe sur `EvalSH9`.
      *Vérif* : le dessous du buggy et les cratères reçoivent de la lumière
      rebondie.
      *Fait (2026-10-24)* : `DiffuseIrradiance` dans `Lighting.hlsli`. En RT,
      la cellule du pixel (`GiSlotFind` sur la clé de `Surface::gi_`, même
      niveau que la vue « GI cache ») ; sinon, et quand elle manque, `EvalSH9`
      s'il y a un ciel, 0 sans ciel. Le diffus indirect `kD × E × albedo` est
      calculé quel que soit le ciel : avec le ciel noir de la lune, l'ambiant
      n'était jamais évalué. Le spéculaire du ciel ne bouge pas. En raster,
      même expression qu'avant : vue de référence à la frame 5000, 177 px
      d'écart (> 8) entre les shaders de HEAD et les nouveaux, tous sur le
      bord de l'ombre en cascade de la fusée, où deux runs des mêmes shaders
      diffèrent de 300 px.

      *Mesuré* (copie statique de `test_buggy_fusee`, 60 fps, RT, avant et
      après au même numéro de frame ; deux runs « après » diffèrent de 2/255
      au plus, sur 5 006 px, aucun de plus de 8) :
      - **Vue de référence, frame 5000** : 66 % des pixels de la scène
        éclaircis, aucun assombri, +0,064 de luma en médiane (le sol au soleil
        reçoit ses pentes voisines). 10 533 px noirs (< 8/255) sont éclairés,
        à 0,28 de luma en moyenne. Versants à l'ombre des collines du fond :
        0,10 → 0,15 ; leurs 5 160 px noirs → 0,07.
      - **Fusée, flanc à l'ombre** (la caméra voit le côté opposé à l'étoile,
        à 50° au-dessus de l'horizon) : 0,05 → 0,58 de luma, teinté par le
        régolithe (RGB 169 144 111) ; côté terminateur 0,20 → 0,67. Le sol au
        soleil est à 0,70 : **le flanc à l'ombre est aussi clair que le sol
        éclairé**, voir « facteur π » ci-dessous.
      - **Dessous du buggy** (`under.btpl` : caméra à 4 m du côté opposé à
        l'étoile, 0,5 m au-dessus du sol, buggy en `strate_2.bmat`) : 14 127
        px noirs éclairés, luma 0,20 en moyenne, 0,34 au 90ᵉ centile : flancs
        intérieurs des roues et dessous du châssis. Le vrai matériau du buggy
        (`mat_0.bmat`) est `metallic 1` sans texture, donc `kD = 0` : il ne
        reçoit aucun diffus, ni du ciel ni du cache, et reste noir à l'ombre
        (1 123 px noirs → 0,001). À corriger dans l'asset (metallic 0), pas
        dans le rendu.
      - **Cratères et pentes** (`slopes.btpl` : 3 m au-dessus et 5 m à côté du
        buggy, regard vers l'azimut de l'étoile) : 37 % des pixels éclaircis,
        2 850 px noirs → 0,10 ; le fond du cratère du centre et les versants
        opposés au soleil sortent du noir.
      - Aspect en blocs, et quelques cellules isolées plus claires que leurs
        voisines (mouchetures sur le sol) : attendu avant 2.9.

      **Facteur π.** Le terme ambiant est `kD × E × albedo`, E en irradiance
      (les SH sont projetées sans le facteur lambertien, `EvalSH9` l'applique ;
      le cache accumule π × radiance par rayon en cosinus). Le terme direct
      fait `albedo / π × E_étoile × N·L`. Four blanc (ciel uniforme à 1,
      albedo 1) : direct à 1, ambiant à π, saturé. Sur la lune : flanc de
      fusée à l'ombre, E ≈ 0,65 dans le cache → rendu 0,58 (albedo ≈ 0,9) au
      lieu de 0,19 ; sol éclairé 0,8/π × 3,4 × sin 50° = 0,67, ce qu'on mesure.
      Correctif : `/ PI` sur le diffus indirect, une ligne, mais il assombrit
      d'un facteur 3 l'ambiant de tout ciel, en raster aussi (le raster ne
      bouge pas sans décision), et le four blanc est prévu en B.8. À trancher
      avant 2.9 : les mesures de 2.9 et 2.7 dépendent de l'échelle.

      Validation propre, RT actif : jeu debug sur la copie statique (300
      frames). Pas d'éditeur debug : Batap utilisait la machine, et il vole le
      focus ; le shader est le même des deux côtés.
- [ ] **2.7 Multi-rebonds.** Lire le cache au dernier sommet, puis passer à 2–3
      rebonds.
      *Vérif* : l'intérieur des cratères s'éclaircit un peu plus.
- [x] **2.8 Vieillissement.** Décroissance exponentielle, hystérésis
      adaptative, éviction des cellules qui ne sont plus touchées.
      *Vérif* : le taux d'occupation de la table reste stable ; une lumière
      qu'on éteint fait disparaître son rebond en moins d'une seconde.
      *Fait (2026-10-23), avant 2.6 et 2.7* : sans éviction, une caméra en
      mouvement remplissait le million de cases en 5 à 20 s, après quoi
      toute insertion échouait et le cache se « peignait » cellule par
      cellule.
      - **Valeur à 32 octets** (`RtCacheValueGPUData`, le cache passe à
        40 Mo) : la valeur résolue (3 flottants), un accumulateur de la
        frame (somme ×256 + compteur, par atomiques) et `history_` =
        échantillons repliés (16 bits) | frame de la dernière visite (16).
      - **Passe de résolution** (`GiResolvePS`, triangle plein écran sur une
        cible R8 de 1024 × 1024, une case par pixel) juste après la mise à
        jour, avant les lecteurs. Par case occupée :
        - visitée cette frame : `valeur = lerp(valeur, somme/compteur, α)`
          avec `α = max(compteur / (échantillons + compteur), 1 − (1 −
          α_min)^âge)` ; le plancher compte les frames écoulées depuis la
          dernière visite, sinon une cellule touchée par un rayon toutes
          les cinq frames oubliait cinq fois plus lentement (mesuré : résidu
          nocturne 0,09 et descente en 2,5 s). `α_min = 3 dt`, soit une
          constante de temps de 0,33 s quel que soit le nombre de fps ;
        - **hystérésis adaptative** : une frame à ≥ 16 échantillons dont
          l'estimation diffère de la valeur de plus de 50 % (+ 0,02)
          ramène les échantillons repliés à ceux de la frame (α = ½) ;
        - non visitée depuis plus de `giMaxAge_` frames (= 1 s au dt
          courant) : clé et valeur remises à zéro. Un trou dans une chaîne
          de sondage linéaire rend orpheline une cellule derrière lui : elle
          s'évince à son tour et sa surface est réinsérée dès le rayon
          suivant ; à 5 % d'occupation les chaînes sont presque toujours de
          longueur 1.
      - `FrameGPUData` porte `giMaxAge_` et `giAlphaMin_`, calculés du dt.
      - **Compteurs** (binding 5 du set 2, 256 seaux par compteur pour ne pas
        sérialiser les atomiques) : occupées, insérées, échecs, évincées ;
        remis à zéro à chaque frame, copiés dans un buffer mappé par slot
        de frame, lus trois frames plus tard. Log `[GI] cache:` toutes les
        120 frames quand la vue « GI cache » est active.

      *Mesuré* (copie statique de `test_buggy_fusee`, 60 fps imposés par la
      config, vue « GI cache ») :
      - **Occupation** avec un `Rotator_C` sur la caméra (0,5 rad/s), relevée
        toutes les 2 s sur 25 s : 19 664 → 35 117 → 45 221 → 58 911 →
        71 250 → 44 629 → 18 964 → 29 730 → 44 017 → 52 712 cellules, de 100
        à 500 insertions et autant d'évictions par frame, **0 échec**. Elle
        suit ce que la caméra balaie au lieu de monter jusqu'au million.
        Caméra fixe : 49 300 cellules, ±50 par frame.
      - **Extinction** : l'étoile remplacée par une `FarPointLight_C` à
        500 m qui tourne autour du buggy (π rad/s, axe horizontal) et passe
        sous l'horizon une seconde sur deux. Luminance du cache sur la
        fusée à huit instants espacés de 0,25 s : 0,04 0,02 0,16 0,44 0,56
        0,44 0,22 0,10. Après le coucher, 0,44 → 0,10 en 0,5 s, 0,04 en
        0,75 s, 0,02 en 1 s : **95 % du rebond disparu en une seconde**.
      - Vue statique à la frame 5000 : mêmes valeurs qu'en 2.5 à 5 % près
        (fusée 0,66 contre 0,70, buggy 0,42 contre 0,43, sol identique),
        680 px de cellules vides contre 561.
      - **Côté de la clé, égalités à 45°** (trouvé par Batap sur
        `bug_GI.btpl`, la fusée vue à 45° de ses axes) : sur la bande qui
        fait face à la caméra, |x| = |z| pour la normale locale, et le bruit
        entre la normale des dérivées d'écran et celle du triangle choisit
        l'axe dominant au hasard : 7 159 px en désaccord de côté (test clé
        contre clé, 0 de repère, de niveau ou de position), soit 6 559 px de
        cellules introuvables. `GiSide` tranche les égalités à 1e-3 près,
        X puis Y : 0 px sur `bug_GI`, et 340 au lieu de 680 sur la scène
        statique (le reste est aux bords de triangles).
      - Rendu normal (qui ne lit pas le cache) : 383 px d'écart en RT et
        630 en raster avec la référence de l'étape 15, tous sur la fusée,
        dont le mesh a été réimporté le jour même ; identiques hors fusée.

      Validation propre, RT actif : jeu debug sur la copie statique,
      éditeur debug sur `test_buggy_fusee`.
- [ ] **2.8b Stabilité temporelle.** Avant 2.9, pour mesurer 2.9 sur une base
      propre. Constat de Batap sur `test_GI` après 2.6 : ça clignote de
      partout, inutilisable en l'état. Les réglages de 2.8 ont été choisis
      pour la réactivité (extinction rattrapée en 1 s), pas pour le bruit.

      *Pourquoi ça clignote*, avec les mesures de 2.5 et 2.8 :
      - La passe ¼ × ¼ lance 58 000 rayons par frame en 1280 × 728 pour
        49 000 cellules occupées caméra fixe : **1 à 2 échantillons par
        cellule et par frame** (médiane 0,6 à 2 sur 5 000 frames en 2.5, et
        non les 10 à 80 estimés en 2.4, qui comptaient les pixels et non les
        rayons).
      - Le plancher `α_min = 3 dt` (0,05 à 60 fps) borne la mémoire d'une
        cellule à `2/α − 1 = 39` échantillons. Pour des échantillons qui
        valent 0 ou « ciel » (visibilité, p ≈ 0,7), l'écart type de la valeur
        est `√(α / (2 − α)) × σ = 0,16 × 0,46 E = 7 % de E`, soit ±10 % de la
        moyenne : **±14/255 à chaque frame** sur un sol à E = 0,76, décorrélé
        d'une cellule à l'autre.
      - L'hystérésis (≥ 16 échantillons et écart > 50 % + 0,02) se déclenche
        sur du bruit : à 16 échantillons binaires l'estimation a un écart type
        de 0,115 E, le seuil est à 3 σ, soit 0,27 % des cellules par frame,
        quelques dizaines de remises à zéro par frame près de la caméra.
      - Une cellule neuve est affichée dès son premier échantillon, à 0 ou à E
        plein : chaque mouvement de caméra, et chaque changement de niveau avec
        la distance, crée un front de cellules à plein contraste qui converge
        en 20 à 40 frames.
      - À la frange de ce que la passe ¼ × ¼ touche, une cellule vue moins
        d'une fois par seconde est évincée puis réinsérée à 1 échantillon.

      À faire, dans l'ordre, chacun mesuré seul :
      1. **Fenêtre longue + historique rapide.** Deux moyennes par cellule :
         lente, `α = 1 − (1 − 1/64)^âge` (64 frames, soit 64 à 128
         échantillons à 1 ou 2 par frame : bruit ÷ 1,5 à 1,8 par rapport à
         39), et rapide, `α = 1/4`. Variance estimée en ligne (moyenne des
         carrés). Changement réel quand `|rapide − lente| > 3 σ_lente + 0,02`
         : la lente prend la rapide. Extinction : la rapide tombe à 10 % en
         8 frames, la détection suit, < 0,2 s au lieu de 1 s. La valeur passe
         de 32 à 48 octets (48 Mo), ou on recycle `history_`.
      2. **Cellule affichée à partir de 8 échantillons**, avec un fondu depuis
         le repli (`EvalSH9`, 0 sans ciel) : `lerp(E_SH, E_cache,
         min(n / 8, 1))`. Un front de cellules neuves devient un fondu de 8
         frames au lieu d'un damier. 2.9 pourra lire le niveau parent, plus
         ancien, à la place.
      3. **Âge d'éviction 3 s** au lieu de 1 : l'occupation mesurée est de
         5 %, la table a 20 fois la place.
      4. **Échantillonnage stratifié** par pixel (R2 ou Halton avec rotation
         par pixel) pour le jitter et la direction du rebond : gain ≤ 2 sur
         l'écart type pour l'intégrale sur l'hémisphère, peu sur la
         visibilité binaire. Option.
      5. **Passe en ½ × ½** : 233 000 rayons, 4 fois plus d'échantillons par
         cellule, bruit ÷ 2. Mesurer d'abord le coût de la passe ¼ × ¼ (jamais
         chronométrée) ; à 3 ray queries par rayon, 0,7 M de requêtes par
         frame, de l'ordre de 1 ms sur une 3060. Option, selon le budget.

      *Cible* : après 1 à 3, caméra fixe sur `test_GI`, écart type par cellule
      3/255 (64 échantillons binaires) ; après 2.9 (8 cellules, ÷ 2,8) et la
      passe ½ × ½ (÷ 2), **moins de 1/255 par pixel**. 2.7 baisse en plus la
      variance de chaque échantillon : au second hit on lit le cache au lieu
      d'un rayon d'ombre binaire.

      *Vérif* : caméra fixe sur `test_GI` et sur la copie statique, 8 dumps à
      10 frames d'écart (le bruit entre deux runs est de 2/255) : écart type
      par pixel sous 3/255 hors arêtes ; compteur de remises à zéro
      (`RtCacheStatReset`) nul lumière fixe ; `orbit.btpl` : extinction
      rattrapée en moins de 0,5 s ; `pan.btpl` : aucune cellule à plein
      contraste, le front des cellules neuves est un fondu.
- [ ] **2.9 Interpolation trilinéaire.** 8 lectures, et un fondu entre niveaux.
      *Vérif* : plus d'aspect en blocs, et l'écart type par pixel de 2.8b
      divisé par 2 au moins.
- [ ] **2.10 Lumières locales dans la mise à jour.** Boucle sur les lumières à
      portée, avec un rayon d'ombre pour chacune. Aujourd'hui
      `GiStarIrradiance` ne voit que l'étoile : sur `test_GI` (un
      `pointLight`, murs rouge et bleu, sol blanc) le mur rouge n'est pas
      éclairé pour le cache et le sol ne reçoit que le ciel gris.
      *Vérif* : une pièce éclairée par une lampe reçoit du rebond ; sur
      `test_GI`, le sol blanc prend le rouge du mur. Tant que le facteur π
      n'est pas tranché, le sol blanc sature (ciel 0,76 + lampe 0,25) et la
      teinte s'écrête : mesurer sur le sol à l'ombre, ou après la décision.
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
      Le depth est en Z inversé sans plan lointain (agrandissement, 15 et
      16) : profondeur linéaire = `znear_ / z`, 0 = infini. Vaut pour tout ce
      qui lira ce buffer : l'AO (phase 4), le SSR (7.6), le rejet
      d'historique (3.5).
- [ ] **3.4 Slots d'instance stables.** Remplacer le swap-remove des instances
      de mesh, ou ajouter une indirection stable.
      *Vérif* : un id ne change pas quand une autre entité est supprimée.
- [ ] **3.5 Matrices précédentes.** Garder la matrice monde de la frame
      précédente par instance, et le view-proj précédent dans `CameraGPUData`.
      Ajouter une vue debug des vecteurs de mouvement.
      - Origine mobile : `World::shiftFrame` décale tous les `Transform_C`,
        donc il doit décaler aussi les matrices précédentes, comme
        `RigidBody_C::prevPos_` ; sinon chaque vecteur de mouvement saute de
        2 km à la frame du recentrage, et tout l'historique temporel (5.3,
        7.5, le cache) est rejeté d'un coup.
      - Un gel ou un dégel (agrandissement, étape 10) téléporte un corps dans
        la hiérarchie : son historique est rejeté une frame, c'est accepté.

      *Vérif* : vecteurs nuls sur ce qui voyage avec la caméra, et justes
      sur le buggy ; nuls aussi sur la frame d'un recentrage.
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
| Origine flottante dans le repère de l'astre (OBJECTIF_AGRANDISSEMENT) | Un jeu sans origine flottante garde un monde de quelques km au plus. Les clés locales du cache n'en dépendent pas ; seules les matrices précédentes (3.5) doivent suivre le recentrage. |

## Points ouverts

- **Repère des cellules du terrain.** Un chunk est le repère de ses cellules
  (2.4). Quand il est reconstruit à un autre niveau de détail, son index
  change et ses cellules sont perdues : le cache y reconverge en quelques
  frames. Si ça se voit (scintillement de l'indirect au passage d'un LOD),
  indexer par l'identifiant stable du chunk (face, niveau, x, y) et, pour
  partager les cellules entre niveaux, par des positions dans le repère de
  l'astre à un plancher plus grossier.

- **Facteur π du diffus indirect.** `kD × E × albedo` sans `/ π`, pour les SH
  du ciel comme pour le cache : l'ambiant est π fois trop clair par rapport au
  direct (2.6, mesuré sur la fusée). Une ligne à changer, qui assombrit tout
  ciel en raster aussi : décision à prendre avant 2.9.

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
