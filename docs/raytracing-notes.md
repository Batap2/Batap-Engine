# Ray tracing — notes par étape

Le détail que le plan (`raytracing.md`) ne porte pas : mesures complètes,
itérations rejetées, pièges. Une section par étape, dans l'ordre du plan.

## 2.6 Lecture dans `ShadeSurface`

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

## 2.7 Multi-rebonds

*Fait (2026-10-24)* : `GiUpdatePS` boucle sur `giBounces_` rebonds en
cosinus. À chaque sommet, `throughput × albedo × E_étoile` (rayon
d'ombre), le throughput étant le produit des albedos rencontrés ; au
dernier sommet s'ajoute `throughput × cache` de sa cellule (clé et
niveau du hit, comme une lecture d'écran), valeur résolue à la frame
précédente, qui tient lieu de tous les rebonds suivants. Miss : π × ciel
au throughput courant. Cellule de queue absente : rien d'ajouté, et le
compteur `RtCacheStatTailMiss` (log `[GI] cache:`). 1 + 2 B ray queries
par rayon de la passe.
- **Réglage utilisateur** `render.giBounces` (1 à 4, défaut 2) dans le
  fichier de config de chaque application, `EngineSettings::giBounces_`,
  `FrameGPUData::giBounces_` (l'ancien `pad0_`), et le champ « GI
  bounces » du menu Settings de l'éditeur pour l'éditeur et le jeu, à
  côté du ray tracing et du cap de fps.

*Mesuré* (copie statique, 60 fps, par rapport à l'état 2.6, mêmes
frames) :
- **Énergie** : B = 1 avec lecture de queue : fusée à l'ombre 0,577 →
  0,611 (+6 %), terminateur 0,668 → 0,699 ; B = 2 : 0,617 et 0,709 ;
  B = 3 : 0,617 et 0,707. **Convergé à B = 2**, d'où le défaut. Sol
  au soleil +2 à 3 %, collines lointaines à l'ombre 0,150 → 0,154.
  Fond de cratère à l'ombre (`slopes.btpl`) : 26,3 → 26,4 → 27,7 →
  27,9/255 pour B = 1 (2.6), 1, 2, 3 : **+6 %, « un peu plus »**. Sur
  la lune presque tout rayon de rebond part dans un ciel noir ; en
  intérieur la part des rebonds suivants sera bien plus grande.
- **Lectures de queue** : 75 à 115 par frame sur 58 000 rayons
  (0,2 %) sans cellule, sur les deux vues : le dernier sommet tombe
  presque toujours sur une surface que la caméra voit. Mettre à jour
  les cellules de tous les sommets (SHaRC) n'apporterait rien ici.
- **Scintillement, non résolu** : écart type de la luma entre les
  frames 5000 et 5010, surfaces hors HUD, 2.6 → B = 1, 2, 3 : 10,0 →
  10,3 → 10,7 → 10,7/255 (fusée 23 → 24,6, sol proche 15 → 17) ; 44
  à 47 % des pixels bougent de plus de 4/255 en 10 frames. Le bruit
  entre deux runs est de 0,04/255. Sous une étoile, la variance d'un
  échantillon vient du rayon d'ombre binaire au premier rebond, que la
  lecture de queue ne lisse pas, et chaque rebond ajoute le sien :
  **c'est 2.8b qui traite le bruit, pas 2.7.**
- Coût de la passe non mesuré (pas de chrono GPU), à faire en 2.8b.

Validation propre, RT actif : jeu debug sur la copie statique (300
frames). Pas d'éditeur debug (même raison qu'en 2.6).

## 2.8b Stabilité temporelle

*Pourquoi ça clignotait*, avec les mesures de 2.5 et 2.8 :
- La passe ¼ × ¼ lance 58 000 rayons par frame en 1280 × 728 pour 49 000
  cellules occupées caméra fixe : 1 à 2 échantillons par cellule et par frame
  (médiane 0,6 à 2 sur 5 000 frames en 2.5, et non les 10 à 80 estimés en 2.4,
  qui comptaient les pixels et non les rayons).
- Le plancher `α_min = 3 dt` (0,05 à 60 fps) bornait la mémoire d'une cellule à
  `2/α − 1 = 39` échantillons. Pour des échantillons qui valent 0 ou « ciel »
  (visibilité, p ≈ 0,7), l'écart type de la valeur est `√(α / (2 − α)) × σ`,
  soit ±10 % de la moyenne : ±14/255 à chaque frame, décorrélé d'une cellule à
  l'autre.
- L'hystérésis (≥ 16 échantillons et écart > 50 % + 0,02) se déclenchait sur du
  bruit : à 16 échantillons binaires l'estimation a un écart type de 0,115 E, le
  seuil est à 3 σ, soit 0,27 % des cellules par frame.
- Une cellule neuve était affichée dès son premier échantillon, à 0 ou à E
  plein : chaque mouvement de caméra créait un front de cellules à plein
  contraste.
- À la frange de ce que la passe touche, une cellule vue moins d'une fois par
  seconde était évincée puis réinsérée à 1 échantillon.

*Fait (2026-10-24), points 1 à 3* :
- Valeur à 48 octets (48 Mo) : `fastLuma_`, `variance_`, `zeroFraction_`,
  `fastCount_`. `history_` = échantillons repliés (10 bits, plafond 1 023) |
  série de frames vides (6 bits) | frame de la dernière visite (16).
  `FrameGPUData::giDt_` remplace `giAlphaMin_` ; constantes dans
  `GiCache.hlsli`.
- Fenêtre lente τ = 1 s (`α = max(n / (N + n), 1 − (1 − dt/τ)^âge)`),
  historique rapide en luma τ = 1/8 s, variance des estimations de frame autour
  de la lente, fraction de frames vides, nombre d'échantillons de la fenêtre
  rapide.
- Détection de changement, à partir de 16 échantillons :
  - extinction : une frame est « vide » si sa luma est sous 5 % de la valeur
    lente (relatif : avec la lecture de queue de 2.7 un échantillon n'est jamais
    exactement nul une fois la lumière éteinte). Une série de k frames vides
    remet la cellule à zéro quand `k ≥ ln(10⁻⁷) / ln(fraction de vides)`. La
    fraction ne bouge qu'à la fin d'une série, d'un bloc : mise à jour pendant
    la série, elle fuyait devant le compteur et la nuit ne venait jamais. Sur le
    sol lunaire (90 % de frames vides) le seuil dépasse les 63 frames du champ :
    pas de détection, la fenêtre lente seule.
  - saut : `|rapide − lente| > 5 σ + 0,02`, seulement si la fenêtre rapide
    contient 32 échantillons ou plus. Avant ce garde-fou, à 4,5 σ et 1 ou 2
    échantillons binaires par frame, 35 à 65 cellules par frame sautaient à
    lumière fixe : six rayons « touchés » d'affilée sont mille fois plus
    probables que la queue gaussienne ne le dit.
  - bande : hors saut, la lente est ramenée à 3 σ + 0,01 de la rapide, même
    garde-fou.
- Affichage : `lerp(repli, cache, min(N / 8, 1))` dans `DiffuseIrradiance`.
- Éviction à 3 s (`giMaxAge_ = 3 / dt`).

*Mesuré* (copie statique, 60 fps, 2.7 → 2.8b, frames 5000 et 5010) :
- Scintillement, écart type de la luma entre les deux frames, surfaces hors
  HUD : 10,7 → 4,3/255 ; fusée 24,5 → 9,5 ; sol proche 16,9 → 7,8 ; sol
  lointain 8,3 → 3,1 ; collines à l'ombre 2,8 → 1,1. Pixels bougeant de plus de
  4/255 : 47 % → 15 % ; de plus de 16/255 : 8,4 % → 1,0 %. Niveau moyen
  inchangé (0,611 → 0,612). Bruit entre deux runs : 0,04/255.
- Remises à zéro à lumière fixe, par tranche de 120 frames : 22, 16, 24, 6, 8
  pendant les 10 premières secondes, puis 0 à 3, puis 0 sur les 30 dernières
  secondes.
- Extinction (`orbit.btpl`, luma de la fusée après le coucher) : 2.8 : 163 →
  43 → 20 à +0, +0,25, +0,5 s ; 2.8b : 160 → 63 → 32. 80 % en 0,5 s contre
  88 % ; résidu nocturne 21/255 contre 9–14. Le prix de la fenêtre de 1 s.
- Panoramique (`pan.btpl`, frame 1500) : pixels clairs isolés 1,59 % → 1,27 %,
  contre 1,41 → 1,15 % sur la vue fixe ; mesure faible, dominée par le relief.
- Sur la lune la cible de 3/255 par cellule n'est pas atteinte (4,3 par
  pixel) : les échantillons valent 0 ou 2,1, pas 0 ou 0,76 comme sous un ciel.

*Fait (2026-10-24), chrono GPU, points 4 et 5* :
- Chrono GPU : `GiUpdatePass` écrit deux timestamps (`TOP_OF_PIPE` avant les
  passes, `BOTTOM_OF_PIPE` après la résolution) dans un query pool de 2 ×
  `FramesInFlight`, lus `FramesInFlight` frames plus tard, en ms via
  `VulkanContext::timestampPeriodNs_`. Dans le log `[GI] cache:` avec le nombre
  de passes.
- N passes par frame : `render.giPasses` (1 à 8, défaut 2), même plomberie que
  `giBounces`. Chaque passe reçoit son index dans `g_draw.instanceIndex_` et en
  dérive ses graines ; une barrière entre les passes, une seule résolution.
- Stratifié : suite R2 en virgule fixe 32 bits (`GiR2`), par pixel pour le
  jitter, par cellule pour la direction du premier rebond, indexée par les
  échantillons repliés plus la place réservée dans la frame (`GiReserve`).
  Interrupteur `GiStratified`, laissé à `false`.
- Protocole : `BATAP_DUMP_FRAME` accepte une liste (`1000,1010` →
  `frame_dump_<frame>.png`), la paire vient d'un seul run à la frame 1000 :
  20 s par réglage. Les valeurs à la frame 1000 ne se comparent qu'entre elles
  (7,4 contre 4,3 à la frame 5000 pour le même code).

*Mesuré* (frames 1000 et 1010, RTX 5070) :
- Coût : 1 passe 0,13–0,16 ms résolution comprise, 2 passes 0,23–0,26, 4 passes
  0,41–0,47 : 0,09 ms par passe, 0,05 ms de résolution. Sur une 3060, 3 fois
  plus environ : 2 passes ≈ 0,7 ms, dans la ligne du budget.
- Scintillement selon N (stratifié) : surfaces 7,1 → 5,7 → 4,7 pour 1, 2, 4
  passes ; sol proche 13,1 → 8,9 → 6,4 ; collines 2,6 → 1,7 → 1,1 ; fusée 14,9
  → 11,7 → 11,1 (son plancher vient d'ailleurs, à voir avec 2.9).
- Stratifié contre aléatoire, 1 passe, mêmes frames : surfaces 7,1 contre 7,4,
  fusée 14,9 contre 16,1, sol proche 13,1 contre 10,3, collines 2,6 contre
  2,0 : pas de gain mesurable, une seule paire de frames ne départage pas
  ±10 %.

Validation propre (jeu debug, copie statique, 300 frames) après les points 1 à
3 et après les points 4 et 5. Pas d'éditeur debug.

## 2.9 Interpolation trilinéaire

*Fait (2026-10-24)* :
- **Facteur π d'abord** (décidé par Batap) : `kD × E × albedo / π` dans
  `ShadeSurface`, pour le ciel SH comme pour le cache, raster compris. Sur la
  fusée (vue de référence, frame 5000), l'apport du cache passe de 76,9 à
  24,7/255 de luma (rapport 3,1) ; lecture du cache coupée, la zone est à
  65,6.
- `GiCellKey(repère, cellule, côté, niveau)`, sorti de `GiCacheKey`.
- `GiIrradianceAt` (`GiCache.hlsli`), appelée par `DiffuseIrradiance` : les 8
  cellules dont les centres entourent le point, même repère, côté et niveau.
  Chaque cellule trouvée apporte `GiShownIrradiance` (fondu des cellules
  neuves vers le repli, 2.8b) avec son poids trilinéaire, et la somme est
  divisée par les poids trouvés : les coins hors de la surface ne sont jamais
  écrits, la renormalisation les écarte. Aucune cellule : repli `EvalSH9`.
- **Fondu entre niveaux** : niveau continu `log2(distance / 7,5 m)`, bande de
  ±0,25 (`GiLevelBlend`) autour de chaque frontière, où l'on lit deux niveaux
  de 8 cellules avec un poids linéaire. La mise à jour tire le niveau de son
  premier hit au hasard dans la même bande (`GiRandomLevel`), avec les
  probabilités du fondu : au niveau entier, les cellules du niveau grossier
  sous sa frontière ne sont jamais écrites et le fondu n'a rien à lire. Chaque
  niveau garde au moins la moitié des écritures de son domaine. La lecture de
  queue (2.7) et les vues debug restent au niveau entier, une cellule ; la
  vue « GI cache » montre toujours la cellule brute, magenta si absente.

*Mesuré* (copie statique, 60 fps, RT, 2 passes, 2 rebonds ; référence = HEAD +
`/ π`, mêmes frames ; zones : fusée 600,280–670,430, sol proche y 500–700, sol
lointain 300,320–1280,480, collines 330,90–620,320) :
- **Scintillement**, écart type de la luma entre les frames 5000 et 5010 :
  surfaces 2,66 → 1,39/255 ; fusée 4,88 → 2,41 ; sol proche 2,28 → 1,15 ; sol
  lointain 3,37 → 1,82 ; collines 1,65 → 0,94. Pixels bougeant de plus de
  4/255 : 6,2 % → 1,8 %.
- **Plancher hors cache** (lecture coupée, mêmes frames) : 0,49 sur les
  surfaces, 0,78 au loin, 0,11 à 0,16 ailleurs. Part du cache : 2,61 → 1,30,
  **÷ 2,0** ; fusée ÷ 2,0, sol proche ÷ 2,0, sol lointain ÷ 2,0, collines
  ÷ 1,8.
- Frames 1000 et 1010 : 2,32 → 1,57 (÷ 1,5) ; fusée 4,37 → 2,56.
- Bande de ±0,5 (fondu sur tout le niveau, 16 lectures partout) : 1,42 à la
  frame 5000, 1,51 à la frame 1000. Pas de gain : ±0,25 gardé.
- Niveau moyen inchangé : écart moyen −0,05/255 avec la référence, 1,1 % des
  pixels à plus de 8/255.
- **Blocs** : le damier de cellules sur le flanc de la fusée disparaît ;
  restent les changements de facette, où le côté de la clé change avec la
  normale. Les losanges sombres du sol au premier plan sont identiques
  lecture du cache coupée : des ombres, pas le cache.
- Coût non mesuré : le chrono GPU ne couvre que la mise à jour et la
  résolution, la lecture est dans le PS principal. À mesurer en 2.11.

Validation propre, RT actif : jeu debug sur la copie statique (300 frames). Pas
d'éditeur debug.

## 2.10 Lumières locales dans la mise à jour

*Fait (2026-10-24)* :
- `GiDirectIrradiance` (`GiUpdatePS.hlsl`, ex-`GiStarIrradiance`) boucle sur
  toutes les lumières, positions relatives à la caméra comme dans
  `ShadeSurface`. Point, spot, directionnelle, étoile :
  `couleur × intensité × atténuation × N·L × visibilité`, un rayon d'ombre
  vers la lumière (`tMax` = distance, ou `ShadowRayFar` pour une
  directionnelle).
- `LightIncidence` (`Lighting.hlsli`) : direction, distance, fenêtre de
  portée et cône du spot ; `ShadeSurface` l'appelle aussi, à la place de son
  code en ligne. `RectLightFace` : la face vue depuis P (fenêtre sur la
  distance au rectangle, volets), sortie de `RectLightContribution`, qui
  l'appelle.
- Rect : `E = π × radiance × fenêtre × facteur de forme`, le facteur de forme
  par `LtcEvaluate` avec la matrice identité (le diffus de
  `RectLightContribution`, sans dépendre d'un V). Visibilité : un rayon vers
  un point tiré uniformément sur la face. Le cache moyenne ces tirages : la
  pénombre de la rect y apparaît, alors que l'éclairage direct garde la carte
  d'ombre dure. Un point tiré sous l'horizon de la surface compte visible.
- `castShadows` respecté (`shadowIndex_` invalide : pas de rayon), étoile
  comprise : avant, l'étoile tirait son rayon même sans ombres.

*Mesuré* (copies de `test_GI` où la directionnelle est remplacée par un
`pointLight` à 14 m au-dessus du centre, un spot ou une rect de 8 × 8 m à 16
m, tournés vers le sol ; frame 300, 60 fps, avant et après) :
- **Raster identique au pixel près** pour les trois lampes (0 pixel
  d'écart) : la mise en commun de `LightIncidence` et `RectLightFace` ne
  change rien.
- **RT** : pixels noirs (< 8/255) 568 873 → 504 079 sous le `pointLight`
  (luma moyenne 46,7 → 54,5) ; 731 436 → 655 754 sous le spot (les murs hors
  du disque apparaissent) ; 588 058 → 559 892 sous la rect. Sol au pied du
  mur rouge sous la rect : RGB 129 129 129 → 150 134 134. Sous le
  `pointLight` le sol éclairé sature, la teinte ne s'y mesure pas.
- `test_GI` sous sa directionnelle : écart de 1/255 au plus avec 2.9.
- Coût non mesuré (le chrono n'est logué qu'avec la vue « GI cache ») : un
  rayon d'ombre par lumière à portée et par sommet, 2.11.

Validation propre, RT actif : jeu debug sous le `pointLight`, le spot et la
rect (300 frames chacun). Pas d'éditeur debug.

## 2.10b Bruit et traînées

Scène : `scenes/test_GI_spot.btpl` (Batap), un spot étroit (11° de demi-angle)
qui entre par le côté caméra, ciel noir, murs et sol à 0,9 d'albedo : tout ce
qui n'est pas dans le faisceau ne vit que du rebond.

*Banc* (RT, 60 fps, 2 passes, 2 rebonds) :
- **Statique** : dumps aux frames 600, 660 et 1200. Bruit = écart type de la
  luma entre 600 et 660, relatif à la moyenne, sur trois zones sans éclairage
  direct (mur gauche, sol à gauche et à droite) ; plus la distance RMS de 600
  à 1200 sur toute la vue.
- **Lampe déplacée** : copie avec un `freeCamController` sur le spot ; D
  envoyé 400 ms à 6 s, la lampe glisse de 4 m. Dumps toutes les 3 frames
  autour de l'arrêt, puis jusqu'à la frame 1500 qui sert de référence
  convergée. Écart RMS à la référence en fonction du temps depuis l'arrêt
  (horodaté par le lanceur, les dumps ralentissent le jeu).
- **Caméra déplacée** : pareil avec le contrôleur sur la caméra.

*Avant* : bruit 31 / 33 / 36 % ; RMS 600 → 1200 : 2,93. Lampe : 6,0 à
l'arrêt, encore 5,8 au bout de 75 frames, plancher (3,0) vers 4 s. Caméra :
5,4 à l'arrêt, 6,2 trente frames plus tard. Pendant ces secondes, les pixels
sombres sont plus clairs que la référence (+1,1 et +1,3/255).

*Expériences*. D = détection 2.8b coupée, C2 = cellules ×2, P = plafond
d'échantillons sur 16 bits, F = filtre spatial (26 voisines) ; lampe : écart
RMS à la référence 30 et 135 frames après l'arrêt (~0,8 s et ~2,7 s) :

| | Bruit (mur, sol G, sol D) | RMS statique | Lampe +30 / +135 |
|---|---|---|---|
| Avant | 31 / 33 / 36 % | 2,93 | 5,9 / 3,7 |
| D | 29 / 30 / 32 % | 2,83 | 4,3 / 3,2 |
| D, sans lecture de queue | 32 / 36 / 34 % (énergie −15 à −22 %) | 2,68 | 3,9 / 3,1 |
| D, C2 | 13 / 15 / 17 % | 1,56 | 2,5 / 1,6 |
| D, cellules ×4 | 15 / 13 / 17 % | 1,38 | 1,9 / 1,4 |
| D, C2, P | 13 / 13 / 14 % | 1,36 | 2,3 / 1,5 |
| D, C2, P, 8 passes | 6 / 7 / 9 % | 0,67 | — |
| D, C2, P, τ = 0,5 s | 20 / 21 / 23 % | 1,94 | 2,7 / 2,0 |
| D, C2, P, F | 7 / 7 / 7 % | 0,77 | 1,9 / 0,87 |
| D, C2, P, F, τ = 0,5 s | 11 / 11 / 10 % | 1,08 | 1,6 / 1,1 |
| D, C2, P, F, τ = 0,5 s, 4 passes | 7 / 9 / 9 % | 0,78 | 1,2 / 0,8 |
| **Retenu** : D, C2, P, filtre 18 voisines, τ = 1 s | 7 / 7 / 7 % | 0,77 | 2,0 / 0,86 |

- **La détection de changement** (2.8b) faisait plus de mal que de bien. Un
  échantillon vaut presque toujours ~0 ou très fort (le rayon de rebond touche
  la tache du spot ou non) : la moyenne rapide saute vers le haut bien plus
  souvent que vers le bas, et chaque saut ou « bande » tire la moyenne lente
  avec elle. D'où l'indirect plus clair après tout mouvement, caméra
  comprise, et une convergence retardée. Coupée : dérive nulle, et la
  traînée caméra disparaît (écart à l'arrêt 1,04 pour un plancher de 0,75).
- **La lecture de queue** n'explique pas la lenteur : sans elle, le retour
  ne gagne que 10 % et l'indirect perd 15 à 22 % de son énergie. Gardée.
- **Cellules** : ×2 divise le bruit par 2 ; ×4 n'apporte presque rien de
  plus. Le plafond de 1 023 échantillons repliés (10 bits) bornait la
  fenêtre à ~1 000 échantillons dès qu'une cellule en reçoit plus de ~17 par
  frame : la fenêtre n'était plus d'une seconde. Passé à 16 bits (les bits
  de la série de frames vides sont libres), gain modeste à ×2.
- **Le filtre spatial** fait autant que 8 passes sans un rayon de plus.
  Dans la résolution, chaque cellule mise à jour fait la moyenne de son
  voisinage 3 × 3 × 3 (même repère, côté et niveau ; poids 8 / 4 / 2 au
  centre, aux faces, aux arêtes ; chaque voisine pondérée par sa confiance
  `N / 8`), stockée dans `filtered_`, que lit `GiShownIrradiance`. Les 8
  coins (1/8 du poids) sont sautés : même bruit, recherche 27 → 19. La
  queue et la vue « GI cache » lisent toujours la valeur brute. Les voisines
  sont lues pendant leur propre mise à jour : valeur de cette frame ou de la
  précédente, les deux valides.
- **τ** : 0,5 s rend la lampe déplacée deux fois plus vite au plancher mais
  le bruit monte de 50 % ; avec 4 passes on retrouve le bruit de τ = 1 s.
  Gardé à 1 s et 2 passes par défaut, voir le coût.

*Coût* (chrono GPU, mise à jour + résolution, RTX 5070, 1280 × 728) :
`test_GI_spot` 0,09 ms à 2 passes, 0,14 ms à 4. Lune (copie statique, 31 000
cellules) : 0,23 ms sans filtre, 0,34 ms avec 26 voisines, 0,29 ms avec 18 ;
0,55 ms à 4 passes. En 1440p les rayons sont 2,8 fois plus nombreux : 2 passes
font déjà ~0,8 ms sur la 5070, au-dessus du budget d'une 3060 (0,5–1 ms) ;
d'où 2 passes par défaut.

*Lune* (copie statique, frames 5000 / 5010, contre 2.9) : scintillement des
surfaces 1,39 → 0,90/255, fusée 2,41 → 1,41, collines 0,94 → 0,53 ; à la frame
1000, 1,57 → 1,01. Niveau moyen inchangé, plus de blocs sur la fusée.

*Pour aller plus vite après un changement* sans payer le bruit en
permanence : un signal du moteur quand une lumière (ou un mesh) bouge, qui
raccourcit τ quelques dixièmes de seconde. Une détection par cellule
robuste aux queues lourdes (sur la moyenne filtrée plutôt que brute) est
l'autre piste.

Validation propre, RT actif : jeu debug sur `test_GI_spot` et sur la copie
statique de la lune (300 frames chacun). Pas d'éditeur debug.

## 3.1 HDR + tonemap

*Avant* : `ShadeSurface` sortait de la radiance linéaire, coupée par
`saturate` puis écrite telle quelle dans une swapchain `B8G8R8A8_UNORM`
(espace `SRGB_NONLINEAR`). Aucun encodage sRGB : l'écran prenait des valeurs
linéaires pour des valeurs encodées, d'où des tons moyens trop sombres, alors
que les textures, elles, sont décodées (formats SRGB). Et toute valeur au-delà
de 1 était coupée canal par canal : blanc plat, et les couleurs vives
changeaient de teinte (le mur bleu, albedo 0 ; 0,43 ; 1, virait au cyan).

*Fait (2026-10-24)* :
- **Cible HDR** : `ScenePasses` possède une cible `RGBA16F` à la taille de la
  vue (`hdrTarget_`, recréée quand la vue change), dont l'index bindless
  passe par `FrameGPUData::hdrTexture_` (l'ancien `pad0_`). Géométrie,
  terrain, billboards, ciel, vue RT de debug, ombres et mise à jour du cache
  sont construits pour ce format ; `saturate` retiré de `PixelShader`,
  `TerrainPS`, `BillboardPS`.
- **`TonemapPass`** + `TonemapPS.hlsl` : triangle plein écran (`SkyVS`), lit
  la cible HDR par `Load` au pixel et écrit la sortie (format de la
  swapchain : la swapchain dans le jeu, l'image de scène dans l'éditeur).
  L'alpha passe tel quel (fenêtre transparente).
- **AgX**, approximation analytique de Wrensch (2023, celle de Blender 4 en
  polynôme) : matrice d'entrée, log2 borné de −12,47 à +4,03 EV, polynôme de
  contraste de degré 6, matrice de sortie. La sortie est déjà encodée pour
  un écran de gamma ~2,2 : écrite telle quelle dans la cible UNORM. Le gris
  moyen 0,18 sort à ~0,50. Au-delà de ~16 en linéaire, blanc.
- **Debug après le tonemap** : la passe se fait en deux rendus. Le premier
  dessine la scène dans la cible HDR, avec la profondeur maintenant gardée
  (`STORE`). Le second, dans la sortie, lance le tonemap puis les fils de
  debug (gizmos, lignes, overlay), qui gardent leurs couleurs exactes et
  restent testés contre la profondeur de la scène. Les vues de debug qui
  passent par `ShadeSurface` (cellules et cache GI, cascades) et la vue RT
  sont tonemappées : couleurs un peu adoucies.
- ImGui n'est pas touché : il dessine après, dans la swapchain UNORM.

*Mesuré* (jeu, 60 fps, frame 600, comparé aux dumps de 2.10b) :
- **`test_GI_spot`, RT** : pixels blanc pur 25 995 → 0 ; pixels ayant un
  canal à 255 : 35 985 → 3 894 (le cœur du faisceau, au-delà de +4 EV).
  Disque du spot : luma de 3–255 (écart type 104) à 18–221 (65), il garde
  un dégradé. Le rouge et le bleu tirent vers le blanc au cœur du faisceau
  au lieu de saturer ou de virer au cyan.
- **Lune** (copie statique) : aucun pixel écrêté ni avant ni après, mais
  l'image s'éclaircit et se désature. Sol proche : luma médiane 164 → 183,
  90ᵉ centile 198 → 191 (les clairs sont comprimés) ; RGB moyen 160 151 136
  → 179 177 172, le beige du régolithe disparaît presque. Normal : les
  intensités ont été réglées à l'œil sur une sortie sans encodage, et un
  régolithe d'albedo 0,8 sous le soleil est dans l'épaule d'AgX. Réglage en
  3.2, avec l'exposition.
- **Indirect** : l'encodage relève les tons sombres, donc la GI se voit
  bien plus, et ses taches basse fréquence aussi (sol de `test_GI_spot`).
- Raster et RT passent par la même passe.

Validation propre : jeu debug en RT et en raster sur `test_GI_spot` et la
lune (300 frames chacun) ; éditeur debug sur `test_GI`, lumière
sélectionnée (gizmo dessiné après le tonemap), 120 frames.

*Choix du tonemap (2026-10-25)* :
- `TonemapOperator` dans `ShaderInterop.h`, `FrameGPUData::tonemap_`
  (l'ancien `pad1_`). Le réglage est la clé de config, une chaîne dans
  `EngineSettings::tonemap_` ; `Renderer/Tonemap.h` relie clé, libellé du
  menu et opérateur (clé inconnue : AgX). Ainsi `Engine.h` n'inclut pas
  `ShaderInterop.h`.
- `userConfig::writeRender` et `editorConfig::writeRender` prennent un
  `EngineSettings` au lieu de quatre arguments positionnels.
- `TonemapPS.hlsl` : AgX (sortie déjà encodée) ; les autres sortent du
  linéaire, encodé en sRGB à la fin (`SrgbEncode`, courbe exacte). PBR
  Neutral : la formule de Khronos (2024). ACES : l'approximation de Stephen
  Hill (matrices d'entrée et de sortie + RRT/ODT), celle de BakingLab.
  Reinhard : `x / (1 + x)` par canal. Uchimura : la courbe de Gran Turismo
  (CEDEC 2017) avec ses paramètres par défaut (pied jusqu'à 0,22, partie
  linéaire, épaule exponentielle vers 1). `none` : coupe à 1 puis encode,
  donc pas l'ancien rendu non encodé.

*Comparaison* (`test_GI_spot` et la lune, RT, frame 600) :
- AgX : le plus doux, les fortes lumières tirent vers le blanc (rouge →
  rose), régolithe presque gris.
- PBR Neutral : le rouge du mur reste rouge, seul le cœur du faisceau
  blanchit ; le régolithe garde son beige.
- ACES : contrasté, le rouge vire à l'orange sous le spot.
- Reinhard : terne, les couleurs saturées restent sombres.
- Uchimura et `none` : disque du spot plat et mur bleu cyan, comme avant
  la 3.1, mais encodés.

Dans l'éditeur, `render.tonemap: "aces"` dans sa config donne bien ACES. La
liste du menu Settings n'a pas été cliquée (l'éditeur s'ouvre derrière les
autres fenêtres) : elle écrit le même champ que le fichier.

## 3.2 Unités physiques

*Choix de Batap* : candela pour les lampes point et spot, lux pour l'étoile
et la directionnelle ; exposition sur la caméra ; matériaux Unlit affichés
tels quels jusqu'à l'émissif (B.6).

*Fait (2026-10-25)* :
- **Lumières** (`LightIncidence`, partagée par `ShadeSurface` et la mise à
  jour du cache) :
  - `Directional` et `FarPoint` : `intensity_` = éclairement reçu, en lux.
    L'étoile garde sa fenêtre de portée, sans inverse du carré : le plan
    demande des lux reçus, pas une intensité à distance astronomique.
  - `Point` et `Spot` : `intensity_` en candela, `E = I × fenêtre / d²`.
    Distance plancher : `max(sourceRadius_, 1 cm)`. Fenêtre de portée
    `(1 − (d/r)⁴)²`, celle des rect lights (Frostbite) ; `falloff_` > 0
    multiplie en plus par `(1 − d/r)^falloff_`, 0 = physique seul.
  - `Rect` : luminance de la face en cd/m², comme avant.
  - Ciel : `intensity_` en cd/m². `SkyPS` ne l'appliquait pas : le ciel
    visible ignorait l'intensité que l'éclairage, lui, prenait.
  - Défauts des composants : point 100 cd, spot 1 000 cd, directionnelle et
    étoile 100 000 lux, rect 1 000 cd/m², `falloff_` 0. Vitesse de glisse
    des intensités dans l'inspecteur à l'échelle (1, 10, 100).
- **Exposition** : `Camera_C::ev100_` (défaut 15), `Camera_C::exposure()` =
  `1 / (1,2 × 2^EV100)` (Frostbite, Lagarde 2014). `ScenePasses` la prend
  sur la caméra rendue (`FrameGPUData::exposure_`). Appliquée **avant**
  l'écriture dans la cible HDR (pré-exposition) : `ShadeSurface` multiplie
  l'éclairage, `SkyPS` le ciel ; le tonemap ne la refait pas. Les valeurs de
  la cible restent autour de 0–16, loin des 65 504 du FP16, alors qu'un sol
  au soleil fait ~25 000 cd/m². Non exposés : Unlit, vues de debug en
  couleurs (cellules, faces, cascades) et vue RT ; la vue « GI cache »
  affiche `E × exposition / π`, la luminance d'un blanc lambertien.
- **Éditeur** : EV100 dans les options de caméra, gardée par scène dans la
  config de l'éditeur (`ev100` à côté de la pose). Une scène sans valeur
  enregistrée reprend celle de la caméra active de la scène dès qu'elle
  existe ; une édition de l'EV100 annule cette reprise.
- **Cache GI** : en lux. Les sommes de la frame (32 bits, virgule fixe)
  débordaient en unités physiques : ~1,3·10⁵ lux × 256 par échantillon, plus
  que 2³² en ~160 échantillons. Elles sont comptées en unités exposées
  (`RtCacheScale` 65 536 × exposition), échantillon plafonné à 2²²
  (64 fois le blanc à l'écran) ; la résolution divise par la même échelle,
  dans la même frame. Pas de 0,6 lux à EV 15.

*Scènes converties* (`intensity` en unités physiques, EV100 sur la caméra
active, choisis pour garder l'image de la 3.1) :

| Scène | Lumières | EV100 |
|---|---|---|
| `test_buggy_fusee` | étoile 128 000 lux (soleil à 1 UA), phares 20 000 cd, `falloff` 0 | 15 |
| `test_GI` | directionnelle 100 000 lux | 15 |
| `test_GI_spot` | spot 500 000 cd | 3,1 |
| `bug_GI` | point 800 cd, `falloff` 0 ; ciel 19,2 cd/m² | 4 |
| `bug_rocket` | — (HDRI) | −0,26 |

*Mesuré* (frame 600, 60 fps, contre les dumps de la 3.1) : luminosité
moyenne de la lune 138,4 → 136,9 (RT) et 135,3 → 133,8 (raster), `test_GI`
59,0 → 58,7, spot 33,9 → 33,3. Sur le spot, l'inverse du carré éclaire un
peu plus le sol proche et un peu moins le mur du fond. Au soleil, les phares
du buggy ne se voient plus (réaliste : 20 000 cd donnent ~20 lux à 20 m,
fenêtre de portée comprise, contre ~10⁵ lux de soleil) ; à l'ombre, l'auto-exposition (3.2b) les
révélera.

## 3.2b Auto-exposition

*Fait (2026-10-25)* :
- **Mesure** (`ExposurePass`, trois passes PS, pas de compute) :
  `ExposureMeasurePS` lit la cible HDR en 16 points par case d'une grille
  64 × 64 et écrit la somme des `log2` de la luminance **de scène** (valeur
  exposée divisée par l'exposition de la frame) et le nombre de pixels
  retenus ; `ExposureReducePS` somme des blocs 8 × 8, deux fois (64 → 8 →
  1), cibles `R32G32F`. Les pixels sous 10⁻³ cd/m² sont exclus : le ciel
  noir de l'espace tirerait sinon la moyenne vers le bas et brûlerait le sol
  éclairé. Le 1 × 1 est copié dans un buffer mappé par slot de frame (les
  cibles du `ResourceManager` ont maintenant `TRANSFER_SRC`), relu
  `FramesInFlight` frames plus tard, comme les compteurs du cache GI : pas
  d'écriture de stockage depuis un PS, donc valable sans RT.
- **Adaptation** (CPU, `ExposurePass::update`) : moyenne géométrique `L`,
  cible `EV100 = ev100ForExposure(0,18 / L) − compensation`, bornée par
  `minEv100` et `maxEv100`, puis `EV += (cible − EV) × (1 − e^(−dt/τ))`
  avec τ = 0,4 s quand la cible monte (l'image s'éclaircit) et 1,5 s quand
  elle descend. Le premier essai visait `log2(L × 100 / 12,5)` (Lagarde
  2014), soit une moyenne exposée à ~0,10 : le régolithe au soleil sortait
  gris foncé, 2,1 EV sous l'EV 15 manuelle de 3.2 ; le gris moyen 0,18 en
  rend 0,8.
- **Réglages** : `Camera_C::autoExposure_` (désactivé par défaut : les
  scènes de test GI gardent leur EV manuelle), `exposureCompensation_`,
  `minEv100_` (−6), `maxEv100_` (20). En auto, `ev100_` sert de point de
  départ, et l'adaptation repart de lui quand la caméra rendue change. Dans
  l'éditeur, les options de caméra ont « Auto exposure », compensation et
  bornes ; ils sont gardés par scène et repris de la caméra de la scène à
  la première ouverture, comme l'EV100.
- **Cache GI** : `RtCacheScale` 65 536 → 1 024. Avec l'exposition d'un
  intérieur, le plafond par échantillon (64 fois le blanc à l'écran)
  écrêtait les rebonds sur le sol au soleil vus par la porte : moins
  d'indirect, l'exposition montait encore, et la boucle s'entretenait. Le
  plafond est maintenant 4 096 fois le blanc, pas de ~40 lux à EV 15.

*Banc* : `room_auto.btpl` (scratchpad), sol de 200 m sous un soleil de
100 000 lux, ciel noir, pièce fermée de 8 × 3 × 8 m avec une porte et une
lampe de 150 cd sous le toit ; caméra en auto-exposition à 25 m dehors,
`freeCamController`, W pendant 2,6 s à 6 s (elle entre d'un mètre), S
pendant 2,6 s à 18 s ; un dump toutes les 12 frames, horodaté.

*Mesuré* :
- **Vue fixe dehors** : médiane 124/255, stable à ±0,1 de luma sur 4 s ;
  cible stable à ±0,002 EV (log temporaire). Pas de pompage.
- **Entrée** : l'image tombe à 36–38 de luma (exposition encore pour le
  soleil), revient à 74 en 0,9 s, 95 en 1,9 s, puis monte doucement jusqu'à
  ~125 en ~8 s.
- **Sortie** : pic de médiane à 150, retour à 125 en ~1,5 s après l'arrêt
  de la caméra.
- Dedans, la cible descend de ~1 EV en 6 s, caméra immobile. Ce n'est pas
  l'exposition : en EV 10 fixe, l'intérieur s'assombrit tout seul de 129 à
  101 de luma en ~5 s. C'est le cache GI qui se stabilise après un grand
  changement de vue. Point ouvert.
- **Lune** (copie statique, auto) : EV 17,07 avec la formule de Lagarde,
  donc ~16,3 au gris moyen, contre 15 en manuel : `exposureCompensation`
  ≈ +1,3 redonne l'image de 3.2.

Validation propre : jeu debug en RT et en raster sur `room_auto` et
`test_buggy_fusee` (300 frames chacun).

*Mesure par percentile (2026-10-30)* : la moyenne géométrique de toute
l'image sur-exposait dès qu'une zone sombre dominait (80 % d'espace, 20 %
de sol : le sol brûlait). Le seuil à 10⁻³ n'écartait que le noir exact ; un
fond à peine lumineux (lueur d'atmosphère au crépuscule) pesait autant
qu'un rocher au soleil. `ExposureReducePS` disparaît : la grille 64 × 64
(somme des log2 et nombre d'échantillons par case) est relue entière
(32 Ko) et le CPU trie les cases par luminance moyenne, pondérées par leur
nombre d'échantillons. Il prend la luminance au percentile
`Camera_C::meteringPercentile_` (0,98) et moyenne (en log) les cases
comprises entre elle et `meteringRange_` EV (6) en dessous : l'œil et les
posemètres matriciels s'accrochent aux zones claires, une grande zone
sombre ne fait plus monter l'exposition, et le soleil ou les spéculaires
(au-dessus du percentile) ne la font pas descendre. Réglables dans
l'inspecteur et les options de caméra de l'éditeur.

## 3.2c Bloom

*Fait (2026-11-04)* : chaîne de Jimenez 2014 (Call of Duty: Advanced
Warfare), sans seuil, dans `Renderer/Features/Bloom/`, entre la mesure
d'exposition et le tonemap.
- **Descente** (`BloomDownPS`) : de la cible HDR vers une image `RGBA16F` de
  demi-résolution, puis chaque niveau vers sa moitié, jusqu'à 6 niveaux
  (1/64) ou un côté sous 8 px. Une image du graphe par niveau, chacune à sa
  taille (pas d'accès par mip, rendergraph 5.3). Filtre 13 points : 5 boîtes
  2 × 2, pondérées 0,5 (centre) et 4 × 0,125. Au premier niveau, moyenne de
  Karis : chaque boîte pondérée par 1 / (1 + luma), ce qui éteint les
  lucioles. Les lectures sont bornées à 60 000 : un pixel HDR débordé vaut
  inf, et les poids de Karis en feraient un NaN dans toute la chaîne.
- **Remontée** (`BloomUpPS`), à la Unity :
  `up(i) = lerp(down(i), tente(up(i + 1)), 0,7)`. La tente 3 × 3 sort
  multipliée par 0,7 avec un alpha de 0,7, et le blend `ONE,
  ONE_MINUS_SRC_ALPHA` garde 0,3 du niveau en place : pas d'image de plus.
  Le niveau 0 est une moyenne pondérée des niveaux (0,3 ; 0,21 ; 0,15 ;
  0,10 ; 0,07 ; 0,17 pour le dernier), de somme 1 : une image uniforme reste
  uniforme.
- **Composition** : `TonemapPS` ajoute `bloom × FrameGPUData::bloomIntensity_`
  à la couleur HDR avant le tonemap (lecture bilinéaire de la
  demi-résolution).
- Le seul sampler répète : les UV sont bornés à un demi-texel du bord, sinon
  le soleil d'un bord fleurit sur le bord opposé.
- **Réglages** (`Camera_C`, donc aussi la caméra de l'éditeur) :
  `bloomIntensity_` (0,1 ; 0 retire les passes), `bloomRadius_` (écart des
  points de la tente, en texels du niveau lu ; 1). Une zone uniforme gagne
  exactement `bloomIntensity` de sa valeur ; l'exposition auto mesure avant
  le bloom.
- Défaut 0,1 et non 0,04 : à 0,04, le halo du soleil ne se distingue pas du
  halo de Mie de l'atmosphère ; il se lit à 0,1.

*Mesuré* (1280 × 720, soleil dans le champ, intensité 0,1 contre 0) : ciel
uniforme +4,4/255 en vert (121,8 → 126,1 après AgX ; 0,2 donne +8,3, le
gain suit l'intensité), près du soleil +6,9/255 ; écart moyen 5,4/255,
max 61. La première version, qui sommait les niveaux, donnait un écart
moyen de 11,9/255 dès 0,04 : un voile sur tout le ciel. GPU : ~0,04 ms.

## 3.2d Paillettes (glints)

*Fait (2026-11-04)* : `Shaders/Glints.hlsli`, version simplifiée de Deliot
& Belcour 2023 (lois binomiales sur grille) : grille hachée en UV, comptes
binomiaux de facettes par case et par direction, sans dictionnaire ni
grille anisotrope.
- **Modèle** : `glintDensity` cases par unité d'UV, une facette par case
  fine. Le niveau de grille suit l'empreinte du pixel en UV (`ddx`/`ddy`,
  axe mineur borné au quart du majeur) : une case de niveau `l` couvre à
  peu près le pixel et compte `4^l` facettes. Leurs pentes suivent la GGX
  du matériau ; l'espace des pentes est découpé en cases de 0,25 α. Pour
  chaque case de grille et case de pente, le nombre de facettes alignées
  est tiré (hash, puis binomiale : Poisson par inversion sous une moyenne
  de 6, gaussienne au-dessus) et divisé par son espérance. Interpolation
  bilinéaire sur 4 cases de grille et 4 cases de pente, linéaire entre 2
  niveaux : 32 tirages par lumière et par pixel.
- **Effet** : le `D` de la GGX est multiplié par
  `(1 − min(intensité, 1)) + intensité × rapport`. Le rapport vaut 1 en
  moyenne. De près, peu de facettes par pixel : éclats rares et forts. De
  loin, des milliers : le rapport tend vers 1 et la GGX revient (sortie
  directe quand l'espérance au pic dépasse 10⁴). Il dépend de H, donc de la
  vue, de la lumière et de l'orientation de l'objet. Repère des pentes : la
  tangente du mesh, ou un repère arbitraire autour de N si le mesh n'en a
  pas.
- **Où** : lumières ponctuelles et directionnelles de `ShadeSurface`, sur
  les surfaces de `MeshSurface` (passe opaque, translucide, variante RT).
  Terrain, scatter et billboards mettent la densité à 0. Ni l'IBL, ni les
  rect lights, ni le chemin GI.
- **Matériau** : `Material::glintDensity` (0 = GGX seule) et
  `glintIntensity` (1 par défaut ; 0,5 garde la moitié du reflet lisse ;
  au-delà de 1, éclats renforcés sans reflet lisse). La struct passe à 80
  octets. Le `.bmat` n'écrit les deux clés que si la densité est non nulle.
  Material Editor : « Glint density », « Glint intensity ».
- **Réglage** : `3d/resources/metal.bmat`, `3d/trash/can_blue.bmat` et
  `can_red.bmat` à 1 500 / 0,5 (cube propre de 0,2 m, 1,25 UV/m, vu à
  ~1 m : ~15 facettes par pixel).

*Mesuré* (1280 × 720, faces de cube au soleil sur ~40 % de l'écran) :
~0,15 ms (médianes 2,31 ms sans, 2,43 et 2,50 ms avec, bruitées par
d'autres charges GPU).

## 3.3 Prépasse profondeur + normales

*Fait (2026-10-30)* :
- **Passe `prepass`** (`SceneFrame`), juste avant `scene` : les meshes
  (`GeometryVS`) et le terrain (`TerrainVS`) avec `PrepassPS`, qui écrit la
  normale géométrique (interpolée, sans normal map) en espace vue dans
  `view normals` (`A2B10G10R10_UNORM`, `n × 0,5 + 0,5`, 0 hors géométrie).
  La profondeur y est effacée à 0 et écrite en `GREATER`. C'est une
  transitoire du graphe, qui ajoute `SAMPLED` et `STORE` dès qu'une passe la
  lit : rien à faire ici, la première lecture viendra avec l'AO.
- **Passe `scene`** : profondeur en `Load::Keep`, meshes et terrain (raster
  et RT) en `EQUAL` sans écriture. Billboards (alpha test, `discard`) et
  ciel inchangés, après.
- **Mêmes triangles** : en prépasse, le terrain fait un draw par chunk (la
  liste des ombres, renommée `chunkDraws_`) ; la passe éclairée garde un
  draw par strate. Les strates partitionnent les indices du chunk
  (`TerrainChunk.cpp`), et `TerrainVS` ne lit pas la strate pour la
  position.
- **`Invariant` sur `SV_Position`** de `VertexShader` et `TerrainVS` : la
  spec Vulkan ne promet la même position entre deux pipelines qu'avec cette
  décoration. DXC n'en émet pas pour `precise` (seulement `NoContraction`) :
  `[[vk::ext_decorate(18)]]` sur le membre, vérifié dans le SPIR-V.
- La vue `rtFirstHit` garde la prépasse : ses fils de debug sont maintenant
  cachés par la géométrie.

*Banc* (`render_bench.py`), en deux temps pour séparer les deux effets :
- **`Invariant` seul** change l'image une fois pour toutes (le code de
  position change) : raster lune 47 et 95 px (bascules d'arêtes, jusqu'à 98
  niveaux), quelques px à ±1 ailleurs. Sans la décoration, la prépasse
  tombait déjà juste sur ce GPU ; avec, c'est garanti sur tout pilote.
- **Prépasse contre une référence prise avec `Invariant`** : raster
  identique au pixel sur les deux lunes, room, spot, point et rect ; 1 à
  2 px à ±1 sur gi_sun, gi_sun_low et l'éditeur ; RT dans le bruit partout
  sauf `room_rt` (4 630 px, max 11). Ce sont les égalités de profondeur :
  en `GREATER` le premier fragment dessiné gagnait, en `EQUAL` tous passent
  et le dernier gagne. Dans `room`, les bouts des murs latéraux sont
  coplanaires avec la façade : deux bandes de ~5 px aux bords de la façade,
  invisibles en raster (même éclairage), visibles en RT, où le cache GI est
  indexé dans le repère de l'objet touché.
- **Normales** (dump temporaire de la cible à la place du tonemap) : sol de
  `room` à (0, 1, 0), façade à (0, 0, 1), longueur 1 à la quantification
  près, terrain de la lune continu.
- Validation de synchronisation : aucun nouveau message.

*Coût GPU* (temps de frame du banc, 1280 × 720, quatre passages) : lune
proche +0,6 à +1,1 ms en raster (5,0 → 5,6–6,1) et +0,8 à +1,0 ms en RT
(1,9 → 2,7–2,9) ; lune lointaine +0,4 à +0,8 ms ; pièces de −5 % à +8 %.
Le terrain est dessiné une fois de plus et n'a presque pas d'overdraw à
regagner : le gain attendu ne se voit que sur les petites scènes. Le détail
par passe attend le chrono par passe (render graph 4.1).

## 3.4 Slots d'instance stables

*Fait (2026-10-30)* :
- **`InstanceSlots<Packed>`** (`InstanceManager.h`) : les tables entité ↔ id
  et la liste des ids libres, sortis de `FrameInstancePool`, qui garde le
  buffer GPU et les drapeaux de frame. Sans GPU, donc testable seul.
- **Le mode suit `CountField`.** Un pool dont les shaders bouclent de 0 à
  son compte (lumières, skybox, sphères d'ombre) ne peut pas avoir de trou :
  il reste compact, et un retrait y déplace toujours la dernière instance.
  Meshes et caméras ne sont lus que par id (push constant, instance de la
  TLAS) : un retrait y libère l'id, qu'une insertion reprend (pile), et
  `size()` reste la fin de la plage occupée. Le slot libre garde ses données
  périmées, que personne ne lit : les draws et la TLAS partent du registre.
- **Reprise immédiate** : sans risque pour les frames en vol (un buffer par
  slot de frame, la nouvelle entité est marquée sale sur tous), mais
  l'historique de 3.5 devra être remis à zéro à l'insertion (note ajoutée à
  3.5).

*Vérif* : `SpaceFret_InstanceSlotsCheck` (`tools/InstanceSlotsCheck.cpp`),
12 contrôles verts : en mode stable, retirer une entité ne change l'id
d'aucune autre et l'id libéré est repris ; en mode compact, la dernière
instance comble le trou.

*Banc* : raster identique sur les 9 cas, RT dans le bruit, aucun nouveau
message de validation, temps GPU inchangés (±4 %).

## 3.5 Matrices précédentes

*Fait (2026-10-30)* :
- **Instances avec historique** : une instance qui déclare `History` reçoit
  dans `fill(in, out, previous)` le `GPUData` écrit pour cette entité à la
  frame d'avant (null à l'insertion, donc aussi pour un id repris en 3.4).
  `StaticMeshInstance` en tire `prevWorld_`, `CameraInstance` `prevPos_` et
  `prevViewProj_` (proj × rotation de la vue, comme les shaders les
  appliquent). Le pool garde ce `GPUData` par slot côté CPU.
- **Au repos sur les trois buffers** : une instance dont le `GPUData` a
  changé est re-marquée sale sur toutes les frames, jusqu'à ce qu'un `fill`
  rende la même chose. Sans ça, le buffer de frame repris trois frames après
  l'arrêt garde « précédent ≠ courant » pour toujours (contrôle positif
  ci-dessous). Un objet qui bouge est donc réécrit 5 frames au lieu de 3.
- **Terrain** : sa matrice est déjà relative à la caméra et chaque chunk
  affiché est placé à chaque frame ; le `Slot` garde la matrice de la
  dernière pose et sa frame, `TerrainWorldGPUData::prevWorld_` la reprend si
  la pose date de la frame d'avant, sinon la courante. Le morph des LOD est
  ignoré.
- **Recentrage** : rien à décaler, contrairement à ce que prévoyait le plan.
  Les valeurs précédentes sont celles de la frame d'avant dans son repère, la
  position précédente de la caméra aussi ; le mouvement ne dépend que de leur
  différence (`prevWorld − prevPos`), que le décalage ne touche pas.
- **Vue debug** « Motion vectors » (menu View de l'éditeur,
  `DebugMotionVectors`) : dans le PS des meshes et du terrain, la position
  précédente est `prevWorld × inverse(world) × P` (règle du plan, `Motion.hlsli`),
  P étant `posRel_`, chaque matrice relative à la caméra de sa frame. Couleur :
  gris = immobile, R et G = décalage NDC vers la position précédente × 8.
  Rien dans les VS : un premier essai y calculait la position précédente pour
  tous les sommets et coûtait +0,2 à +0,3 ms en RT sur la lune, pour une vue
  de debug.
- Limites connues : un changement de caméra de rendu compare la nouvelle à
  sa propre frame d'avant (une frame fausse) ; le ciel et les billboards
  n'ont pas de vecteur.

*Vérif* (réglages temporaires retirés depuis : dérive d'une entité par nom,
`shiftFrame` à chaque frame, vue forcée avec tonemap `none`) :
- `gi_sun` immobile, puis recentré de ±(1500, 0, −700) m à **chaque**
  frame : tous les pixels de géométrie au gris neutre (|v| ≤ 0,28 px, la
  quantification 8 bits du gris), frames 600 et 601.
- Lune, caméra qui avance et tourne de 0,4°/frame : 630 000 px en
  mouvement (2,9 px en moyenne) ; écart à la frame d'avant 5,1 niveaux sans
  les vecteurs, 1,7 en suivant les vecteurs.
- Lune, buggy qui roule et tourne de 1,5°/frame, caméra fixe : 6 900 px en
  mouvement, tout le reste immobile ; écart 45 → 9,4 niveaux (bords et
  éclairage qui tourne avec le buggy). Mêmes chiffres en recentrant à chaque
  frame.
- Caméra et buggy qui avancent ensemble : le buggy entier au gris neutre,
  le sol autour en mouvement.
- Buggy arrêté après 10 frames : zéro dès la frame suivante, et sur les 8
  d'après. **Contrôle positif** : sans le re-marquage, le dernier mouvement
  revient une frame sur trois (2391, 2394, 2397…).

*Banc* : raster identique sur les 9 cas, RT dans le bruit (`room_rt` et
`editor_rt` ont dépassé une fois une référence prise basse : 94–169 px et
71–218 px à 1 niveau, relancés dans le bruit), aucun nouveau message de
validation, temps GPU dans le bruit.

## Terrain : le raster lit le BLAS

*Fait (2026-10-30)*, suite de la BLAS dynamique qui suit le morph (commit
`5f04582`).

*Problème* : en RT, caméra en mouvement, environ 80 fps sur la lune et des
taches noires sur le sol quand elle va vite. Mesuré (`moon_near`, caméra à
0,36 m par frame) : frame 2,9 → 13,6 ms. `Terrain_S` marquait tous les chunks
`moved_` dès que la caméra bougeait par rapport à l'astre : ~5 200 chunks à
remorpher par frame, plafonnés à ~3 270 par `kMaxMorphRows`, donc 3 270
refits de BLAS par frame (26 000 géométries, 257 Mo de scratch alloués et
rendus à chaque frame) : 4,4 ms de CPU dans `vkCmdBuildAccelerationStructuresKHR`,
1,6 ms à préparer les entrées, 3,3 ms de refit GPU. Les chunks au-delà du
plafond prenaient du retard : leur BLAS ne collait plus au morph du raster
et les rayons d'ombre partis de la surface raster la recoupaient, d'où les
taches (elles disparaissent en levant le plafond). Les chunks hors de la
bande de morph ne changent pas, mais ~3 250 y sont : les sauter ne suffit pas.

*Correction* :
- **Le raster lit le BLAS** : `TerrainWorldGPUData::tracedBase_` (l'ancien
  `pad0_`) donne la place du chunk dans le pool `traced` quand ses
  positions sont à jour, `TerrainNoTraced` sinon (chunk pas encore morphé,
  paramètres changés, RT éteint). `TerrainVS` y lit la position ; la normale
  reste morphée dans le VS. Raster et BLAS sont identiques par
  construction, quel que soit le retard d'un chunk : plus de taches.
- **Budget de morph** : `Placement::cameraInOwner_` remplace `moved_`. Par
  slot, le drawer garde la caméra du dernier morph et la largeur de la bande ;
  le retard d'un chunk est le déplacement de la caméra depuis, divisé par
  cette largeur. Chaque frame, les `kMorphBudget` (256) chunks les plus en
  retard sont remorphés et refittés, en plus des morphs forcés (ajout,
  paramètres, ré-affichage), toujours sous `kMaxMorphRows`. Le morph d'un
  chunk avance donc par pas quand vient son tour ; à l'arrêt tout rattrape
  en une vingtaine de frames.
- Un échec mesuré avant : un seuil de retard (ε de la bande) sans budget
  baissait la moyenne mais laissait des pics (p95 1,5 × la médiane : les
  chunks d'un même niveau franchissent le seuil ensemble), et tout retard du
  BLAS sur le raster donnait les taches.

*Mesuré (jeu, RT, 1280 × 728, caméra à 0,36 m et 0,3° par frame)* :

| | à l'arrêt | en mouvement : moyenne | p95 | max |
|---|---|---|---|---|
| `moon_near`, avant | 3,0 ms | 13,6 ms | 16,3 ms | 21 ms |
| `moon_near`, après | 3,4 ms | 4,7 ms | 6,1 ms | 8,5–9,8 ms |
| `test_GI_Moon`, avant | 1,5 ms | 4,5 ms | 6,1 ms | 7,9 ms |
| `test_GI_Moon`, après | 1,6 ms | 1,9 ms | 2,6 ms | 3,3–4,1 ms |

À 48 m/s, le retard d'un chunk vaut 9 % de sa bande en moyenne, 16 % au
plus. À l'arrêt, +0,3 à +0,4 ms de GPU sur la lune : le VS lit les positions
mais calcule encore tout le morph pour la normale. Caméra rapide : plus de
taches. Banc : raster identique, RT dans le bruit, validation propre.


## 4.1 Passe bitmask

*Fait (2026-11-06)* :
- **Passe `ao`** (`Features/Ao`), entre `prepass` et `scene` : triangle plein
  écran (`SkyVS` + `AoPS`), sortie `R8_UNORM` pleine résolution. Elle lit la
  profondeur (Z inversé, `L = znear / z`) et les normales de vue par leur
  index bindless (`FrameGPUData::depthTexture_`, `viewNormalTexture_`) ; les
  PS de la géométrie lisent `aoTexture_` à `SV_Position` (`ScreenAo`). Le
  graphe ajoute `SAMPLED` à la profondeur, que `scene` reprend en
  attachement.
- **Réglages sur `Camera_C`**, comme le bloom (pas de `Ssgi_C`) :
  `aoStrength` (0 coupe la passe), `aoRadius` (raster seulement),
  `aoThickness` (épaisseur plancher, m). En RT, rayon = cellule du cache à la
  distance du pixel, `GiCellBase × max(d / GiLevelDistance, 1)` : 15 cm sous
  3,75 m, puis 4 % de la distance, sans saut aux changements de niveau.
  Rayon écran borné à 25 % de la hauteur ; sous 2 px, AO = 1. Caméra
  ortho : pas de passe.
- **Marche** : 1 tranche par pixel, 8 pas de chaque côté, répartis en carré
  (`t = 1 + s²(R − 1)` px), IGN pour l'angle et le décalage des pas (FAST en
  4.3). Plan de tranche exact : celui de l'œil, de P et du pixel voisin sur
  la ligne.
- **Épaisseur** : chaque pas est un segment le long du rayon caméra, de
  l'échantillon à `max(|ΔL| si connecté, aoThickness + 2 px)`. Connecté =
  la pente du Z inversé (linéaire sur un plan) reste à ±50 % des deux
  précédentes. Le segment est coupé exactement par la sphère de rayon R
  autour de P (EEVEE l'approche dans l'axe de vue). Les angles sont
  multipliés par 1,05 autour de la normale : un pas à moins de ~4° du plan
  tangent ne masque rien.
- **Pondération** : visibilité en cosinus exacte. Intégrale de
  `cos φ · |sin(φ + n)|` (l'intégrande de GTAO), en forme fermée, sur chaque
  plage de secteurs occultés ; divisée par l'hémisphère entier, un pixel
  dégagé vaut exactement 1.
- **Vue debug** « Ambient occlusion » (menu View). `BATAP_DEBUG_FLAGS=<bits>`
  force les bits `Debug*` sans éditeur (64 = AO).

*Scène* : `scenes/test_ao.btpl` (`tools/make_ao_scene.py`) : coin de pièce,
cube, sphère, plaque de 3 cm à 30 cm du sol, ailette de 3 cm, ciel seul ;
`test_ao_sun.btpl` y ajoute un soleil bas.

*Mesuré* (1280 × 720, RTX 5070, médiane GPU de la frame) :

| | raster | RT |
|---|---|---|
| AO coupée | 0,25 ms | 0,54 ms |
| 2 tranches, poids par secteur | 0,59 ms | 0,88 ms |
| 2 tranches, intégrale par plage | 0,56 ms | 0,84 ms |
| 1 tranche (retenu) | 0,42 ms | 0,70 ms |

- Le coût suit le nombre d'échantillons : 4 pas au lieu de 8, ou 1 tranche
  au lieu de 2, le divise par deux. Un bruit constant (cache cohérent) ne
  gagne que 13 % : une pyramide de profondeur n'est pas la priorité.
- ≈ 0,65 ms en 1440p sur la 5070, environ le double sur la cible 3060 : la
  demi-résolution sera probablement nécessaire (4.3).
- Vue debug, après AgX (sol dégagé = 200) : coin du fond 162 en raster,
  175 en RT ; pied du cube 175 / 180 ; sous la plaque 182 / 200 (en RT la
  plaque est au-delà de la cellule, c'est le cache qui l'assombrit) ;
  milieu d'un mur 201.
- Reste : des points sombres isolés aux silhouettes (haut de la sphère et
  de l'ailette), en raster comme en RT. À revoir avec le débruitage.
- Validation (build debug, synchronisation comprise, raster et RT) : rien
  de la passe. Trois erreurs préexistantes, `independentBlend` non activé
  pour les pipelines translucides (accum et reveal n'ont pas le même
  blend, 585044a), et une assertion VMA « allocations non libérées » à la
  fermeture, présente aussi AO coupée.

## 4.2 Application

*Fait (2026-11-06)* :
- **`ShadeSurface`** : l'AO multiplie `DiffuseIrradiance` (le cache en RT, le
  ciel SH9 en raster) et donne l'occlusion spéculaire du ciel préfiltré,
  `SpecularOcclusion` de Lagarde 2014 sur α = r² (comme Filament). Le direct
  n'y passe jamais. AO = 1 sur les translucides et les billboards (pas dans
  la prépasse).
- **Fausses occlusions de la passe 4.1** : le critère de l'étape (« une face
  dégagée ne bouge pas ») a montré que le sol plat s'occultait lui-même, en
  pointillés suivant l'IGN. Trois causes, corrigées dans `AoPS` :
  - le centre de texel d'un pas d'un pixel est jusqu'à 0,7 px hors de la
    ligne de tranche. Mesuré en 3D, un point du plan tangent montait
    au-dessus de l'horizon ; projeté perpendiculairement à la tranche, aussi
    (il quitte le plan tangent). L'angle se lit maintenant en coordonnées de
    tranche, (hauteur au-dessus du plan tangent / |N projetée|, écart à la
    normale projetée) : un point du plan tombe exactement sur l'horizon ;
  - le bout arrière du segment, sous le plan, pouvait dépasser ±π et
    remplir tout le masque : le segment est coupé au plan tangent avant la
    mesure ;
  - une tranche presque couchée dans le plan tangent (|N projetée| petit,
    vues rasantes) amplifie l'erreur de la normale 10 bits de la prépasse :
    sous 0,4, la tranche est tournée d'un quart de tour, où la normale est
    presque dans la tranche (tourner plutôt que sauter : avec une tranche
    par pixel, sauter donne AO = 1).
  Les points sombres aux silhouettes notés en 4.1 avaient la même origine.

*Mesuré* (`test_ao`, 1280 × 720, luminance de la frame, avant → après) :

| zone | raster | RT |
|---|---|---|
| sol dégagé | 119,6 → 119,6 | 117,4 → 117,4 |
| milieu d'un mur | 110,8 → 110,8 | 104,9 → 104,9 |
| coin du fond | 116,9 → 78,3 | 93,2 → 70,6 |
| pied du cube | 117,4 → 90,3 | 106,3 → 86,5 |
| sous la plaque | 119,6 → 99,7 | 93,2 → 93,2 |

- Vue debug : 0 pixel occulté sur le sol dégagé (1 555 avant les
  corrections), 0 point isolé sur le sol rasant au loin (612).
- En RT, le dessous de la plaque (30 cm, au-delà de la cellule) ne change
  pas : le cache l'assombrissait déjà. Le coin perd 24 % de plus que le
  cache seul ; sans référence, le double compte du résidu n'est pas
  chiffrable, mais le coin reste lisible.
- Coût, passe et lecture : +0,21 ms en 720p dans les deux modes (+0,04 ms
  sur 4.1 : `atan2`, tranche tournée, lecture dans les PS).
- `planet.btpl` en RT : 0,36 % des pixels à plus de 8 niveaux (intérieur et
  arêtes du traîneau, pied des blocs, robot) ; le reste à ±1, l'exposition
  auto.
- Validation (debug, synchronisation) : rien de nouveau.

## 4.3 Bruit et filtre

*Fait (2026-11-06)* :
- **Estimateur linéaire.** La passe écrivait par pixel le ratio
  visible / total de sa tranche : exact (1) sans occultant, mais moyenner
  des ratios ne donne pas l'intégrale, et la tranche rasante tournée de 90°
  biaisait l'échantillonnage. Sur des angles de tranche uniformes,
  |N projetée| × (intégrale de la tranche entière) vaut 1 en moyenne
  (∫ N·ω sur l'hémisphère / π) : `1 − |N projetée| × occulté` est sans
  biais, linéaire pour le filtre, et vaut exactement 1 sans occultant. Il
  descend sous 0, d'où une cible brute `R16_SFLOAT` (`ao raw`). La tranche
  rasante (|N projetée| < 0,4) est laissée de côté au lieu d'être tournée :
  elle ne pèse que |N projetée|.
- **Passe `ao filter`** (`AoFilterPS`) : 5 × 5 binomial, coupé par la
  distance au plan tangent du centre (tolérance 2 % de la distance) et par
  `(N·Nq)^8`. Sortie `R8_UNORM` (`aoTexture_`), `aoStrength` appliqué ici.
  `Ao.hlsli` partage les bindings et la reconstruction de position.
- **Bruit : IGN, pas FAST.** Mesuré contre une référence (32 tranches
  stratifiées × 32 pas, sans filtre), vue debug, raster, erreur RMS sur les
  pixels occultés :

| | brut | filtré 5 × 5 |
|---|---|---|
| ratio (4.2), IGN | 9,7 | 5,4 |
| ratio, FAST | 9,7 | 5,5 |
| linéaire, IGN (retenu) | 14,6 | 4,9 |
| linéaire, FAST | 14,6 | 5,1 |

  FAST (`vector2_uniform_binomial5x5`) ne gagne rien : l'occlusion dépend de
  l'angle de tranche de façon très non linéaire, l'optimisation spectrale du
  bruit ne passe pas. Retiré (pas de données EA dans le moteur). À
  réessayer avec un filtre temporel (5.3), son vrai terrain. Noyau : 3 × 3
  binomial, 5 × 5 binomial et 5 × 5 boîte se valent (4,91 / 4,87 / 4,88).
- **Ce qui reste est la marche, pas le bruit.** Contre une référence à
  32 tranches mais 8 pas, le filtré a un biais de −0,03 et une erreur de
  0,85. Contre 32 pas, le biais est de +2,3 (plus clair) : 8 pas ratent des
  occultants. Levier qualité / coût séparé.
- Image : le grain d'IGN a disparu, contacts nets. L'AO est plus légère
  qu'en 4.2 : le ratio d'avant assombrissait.
- Coût : 0,28 ms en 720p dans les deux modes (+0,07 ms de filtre), soit
  ≈ 1,1 ms en 1440p sur la 5070, environ le double sur une 3060 : au-dessus
  du budget de 0,5–1 ms. La demi-résolution est le levier suivant.
- Validation (debug, synchronisation) : rien de nouveau.
- **Réglage utilisateur** `render.ambientOcclusion` (défaut vrai), case
  « Ambient occlusion » des Settings de l'éditeur, pour l'éditeur et pour
  le jeu. Éteint, la passe n'existe pas : image identique à `aoStrength` 0,
  temps GPU revenu à la base.

## 4.3b Demi-résolution

*Fait (2026-11-06)* :
- **`ao` et `ao filter` en ½ × ½** (`R16_SFLOAT`). Un texel demi-résolution
  représente le pixel en haut à gauche de son bloc 2 × 2 (`AoFullTexel`) :
  position, normale et marche viennent de ce pixel, la marche lit toujours
  la profondeur pleine résolution.
- **Passe `ao upsample`** (`AoUpsamplePS`, pleine résolution, `R8_UNORM`,
  `aoTexture_`) : les quatre texels demi-résolution voisins, pondérés en
  bilinéaire et par `AoSurfaceWeight` (plan tangent + normales, partagé avec
  le filtre dans `Ao.hlsli`). Un pixel qu'aucun texel ne représente (objet
  plus fin que le bloc) n'a pas d'AO. `aoStrength` s'applique ici.
- Pas d'option pleine résolution : même erreur, voir ci-dessous.

*Mesuré* (`test_ao`, 1280 × 720) :

| | erreur RMS (pixels occultés) | AO, temps GPU |
|---|---|---|
| pleine résolution (4.3) | 4,87 | 0,28 ms |
| demi-résolution | 4,93 | 0,12–0,13 ms |

- ≈ 0,5 ms en 1440p sur la 5070, ≈ 1 ms sur une 3060 : dans le budget.
- Image un peu plus douce aux contacts, sans escalier aux arêtes.
- Validation (debug, synchronisation) : rien de nouveau.

## 4.4 Occlusion directionnelle

*Fait (2026-11-06)* :
- **Le moment d'ordre 1 au lieu d'une SH L1 de visibilité.** La passe sort
  `(ao, m)` en `RGBA16F` : `m = (1/π) ∫ O(ω) ω (N·ω) dω`, la direction
  moyenne pondérée en cosinus de ce qui est occulté, en espace monde
  (`(2/3) N` si tout l'est). C'est la bande 1 de l'occlusion pondérée en
  cosinus : même information qu'une SH L1, et le même estimateur linéaire
  que l'AO (intégrales fermées de `(cos t, sin t) cos(t − n) |sin t|` par
  plage de secteurs, `AoMoment`). Filtre et remontée le moyennent sans
  biais.
- **Ciel directionnel (raster)** : `DiffuseIrradiance` retire du ciel SH9
  `π (L0 (1 − ao) + L1 · m)`, exact pour une radiance linéaire en direction
  (bandes 0 et 1 du ciel ; L0 = 0,282095 c₀, L1 = 0,488603 (c₃, c₁, c₂)).
  Un occultant de côté retire l'horizon, pas le zénith. Ciel uniforme :
  identique à `ao × ciel`. En RT, le cache n'est pas directionnel : `× ao`.
- **Occlusion spéculaire** (remplace Lagarde) : un cap des directions
  occultées autour de `m`, taillé pour que sa part pondérée en cosinus,
  `(N·D) sin²α`, vaille `1 − ao`, coupé par le cap du reflet
  (`cos = 2^(−3,32 α²)`, intersection d'Oat et Sander lissée par Jimenez
  2016). Deux échecs avant :
  - un cap des directions *visibles* : il coupait les reflets rasants dès
    que l'AO passait sous 1 (bords noirs sur les sphères métal du furnace) ;
  - sans fondu : les miroirs montraient un trou noir là où l'occultant
    aurait dû se refléter (cuivre de `test_brdf` devant la sphère rouge).
    Estompé sur α ∈ [0,01 ; 0,09], comme Filament : les surfaces lisses
    auront les réflexions RT (phase 7).
- **Le furnace n'est plus un furnace avec l'AO** : en raster (rayon 1 m),
  les sphères voisines à 30 cm s'occultent, diffus et spéculaire. Pour
  valider une BRDF, couper l'AO (réglage `render.ambientOcclusion`). En RT
  (rayon = cellule), le furnace ne bouge pas (182 px à ±4).

*Mesuré* (luminance de la frame, 4.3b → 4.4) :

| zone de `test_ao` | raster | RT |
|---|---|---|
| sol dégagé, milieu d'un mur | identiques | identiques |
| coin du fond | 83,4 → 84,1 | 75,0 → 73,8 |
| pied du cube | 94,3 → 94,5 | 90,6 → 89,4 |
| sous la plaque | 98,5 → 97,0 | 93,2 → 93,2 |

- `test_brdf` : 0,02–0,04 % des pixels à plus de 8 niveaux (contacts du
  cuivre rugueux).
- Coût : 0,14–0,15 ms en 720p (+0,02 ms sur 4.3b).
- Validation (debug, synchronisation) : rien de nouveau.

## 4.5 Hybride RT : écarté

*Décidé (2026-11-06)*, sur le critère du plan (« seulement si des artefacts
restent visibles ») :
- **Hors écran.** En RT, le rayon de l'AO est une cellule du cache : 49 px
  à 2 m, 26 px à 5 m en 720p (borné à 25 % de la hauteur de près). Au-delà,
  le cache, tracé en 3D, voit déjà les occultants hors écran. Ce qui manque
  se limite à une bande de cette largeur au bord de l'écran, quand un
  occultant est juste hors champ.
- **Épaisseur ambiguë.** L'épaisseur adaptative (4.1) et la coupe à la
  sphère de rayon R couvrent l'ailette de 3 cm et la plaque de `test_ao`
  sans halo ni fuite visibles.
- Rien de visible sur `test_ao`, `test_ao_sun`, `test_brdf` ni la lune.
- À reprendre si l'AO « apparaît » au bord de l'écran en jeu quand la
  caméra tourne près d'un mur : un ray query de longueur R dans la tranche,
  au milieu des secteurs encore libres, à la sortie de l'écran (passe en
  variante `BATAP_RT`, layout RT).

## AO sortie de la caméra

*2026-11-07* : `aoStrength`, `aoRadius` et `aoThickness` quittent `Camera_C`
(l'AO décrit l'éclairage de la scène, pas l'objectif). Constantes :
intensité 1, rayon raster 1 m (`AoRasterRadius`), épaisseur plancher 10 cm
(`AoMinThickness`). Seul réglage : `render.ambientOcclusion`. Les scènes
qui gardent ces clés se chargent (champs inconnus ignorés). Raster
identique à 4.4.

## Phase 6 — Planetshine

*Fait (2026-11-07)* :
- **`Planetshine_C`** (6.1), sur l'entité d'un astre : `radius` en unités
  locales, multiplié par sa plus grande échelle monde (un mesh sphère
  unité mis à l'échelle n'a rien à régler), `albedo` en couleur.
- **`Planetshine_S`** (6.2) : pour chaque astre, une lumière ponctuelle
  cachée (`EditorOnly_C`, ni sauvegardée ni listée, sans icône) au centre de
  l'astre. Ponctuelle et non directionnelle : une scène n'a qu'une
  directionnelle ou `FarPointLight` (`CascadeLight_S`), et le point donne de
  lui-même la direction et le 1/d² en tout point. Intensité
  `E_étoile (2/3) A R² Φ(α)` candelas, phase prise depuis la caméra qui
  rend, étoile = la lumière des cascades. Rayon de source = rayon de
  l'astre (pénombres de la phase 5). Ombres par rayon en RT, aucune en
  raster (une cube map sur toute la scène).
- **`ShadowRayLength`** : le rayon d'ombre d'une source sphérique s'arrête
  à sa sphère (`dL − sourceRadius_`), sinon un astre qui projette des
  ombres masquerait sa propre lumière (piège noté dans « Ombres »).
- **Option « GI bounce »** de `Mesh_C` (vrai par défaut), case dans
  l'inspecteur : un rayon GI qui touche l'objet s'arrête sans rien ramener
  (drapeau `RtInstanceNoGiBounce` dans `RtInstanceGPUData`). L'objet reste
  visible et éclairé. Pour un astre éclairé par planetshine, sinon sa
  lumière compte deux fois.

*Mesuré* (`planet.btpl` copié, RT, nuit : soleil sous la lune, orbites
figées, EV100 5 ; Terre de 900 m à ~12 km, 20 × la Terre vue de la Lune) :

| | luminance moyenne |
|---|---|
| rebond GI de la Terre (avant) | 82,0 — taches du cache, bords noirs en dents de scie |
| Terre sans rebond GI | 2,0 |
| sans rebond + planetshine | 57,0 — lumière nette, ombres des structures |
| idem en raster (sans ombres) | 58,3 |
| plein jour, avant / avec planetshine | 108,75 / 108,74 |

Validation (debug, synchronisation, raster et RT) : rien de nouveau.
- **2026-11-07, les deux sens** : l'option ne coupait que les rayons qui
  touchent l'objet ; ceux qui en partent ramenaient encore la lumière des
  autres (la lune éclairait la Terre). Maintenant un chemin GI qui part
  d'un objet exclu ne compte que le ciel : `GiPathIrradiance` s'arrête au
  premier objet touché, et en mode par cellule le candidat porte le
  drapeau (7e mot, `RtCacheCandidateWords`). Vue debug « GI cache » sur la
  Terre de nuit : cellules grises (lumière de la lune) avec le rebond,
  noires sans. Sol de nuit, vue d'origine : 82 avec le rebond, 2 sans ;
  avec planetshine 97,7 contre 57,1.

## 7.1 Attributs au hit

*Fait (2026-11-08)* :
- **`RtGeometryGPUData` passe à 48 octets** : décalages des normales, UV et
  tangentes depuis les positions, pris dans `Mesh::streamOffsets_`
  (`meshGeometryRecord`). Les chunks n'en ont pas besoin.
- **`RtResolveSurface(record, dir, coneWidth)`** rend un `Surface` comme
  `MeshSurface` et `TerrainPS` le font :
  - normales par sommet tournées puis normalisées comme dans le VS,
    interpolées par les barycentriques ; tangente et signe interpolés,
    normal map comme `MeshSurface` ;
  - terrain : normale morphée par `terrainVertex` (la fonction de
    `TerrainVS`), normale de face si `TerrainFlat`, `cell_` (mètres par
    pas de grille) pour l'échappement des facettes, textures au coin
    (0, 0) au niveau 0, comme `TerrainPS` ;
  - `RtHitRecord` + `RtCommitted`, en deux surcharges (requête opaque ou
    non opaque) : un hit filtré candidat par candidat (mesh coupé) se
    résout pareil. `RtResolveHit` (GI) ne change pas.
- **LOD par ray cone** (Akenine-Möller 2021) : λ = ½ log₂(aire UV × w × h
  / aire monde) + log₂(largeur du cône / |n·d|), par texture
  (`GetDimensions`). Indices bindless en `NonUniformResourceIndex` : en
  passe plein écran, le matériau change d'un pixel à l'autre.
- **`ShadeSurface` → `ShadeSurfaceToward(model, s, V, coverage, seen)`** :
  même calcul, V en paramètre. `seen = false` (hit d'un rayon) s'arrête
  avant la perspective aérienne, l'exposition et les vues de debug.
  `RtShadeHit` ramène Unlit en radiance (`albedo / exposition`), puisque
  la passe éclairée l'affiche tel quel après exposition.
- **Vue `rtFirstHit`** : moitié gauche, le hit du rayon caméra ombré
  comme la passe éclairée ; moitié droite, la normale d'ombrage.

*Mesuré* (`scenes/test_reflection.btpl`, `tools/make_reflection_scene.py`,
RT, AO et bloom coupés : le bloom de la moitié droite déborde) :
- moitié gauche de `rtFirstHit` contre le rendu RT : 4,3 % des pixels de
  géométrie à ±1–8, 0,26 % au-delà de 8 (max 69), tous sur le tuyau
  texturé : LOD du cône isotrope contre dérivées matérielles, un peu plus
  flou. Sphères, sol, miroirs : identiques ;
- raster `test_brdf` identique au pixel, RT dans le bruit (246 px à 1) ;
- GPU de la frame inchangé (`test_reflection`, RT : 0,689 → 0,686 ms).

*Vu en passant*, non corrigé : sur une copie de `planet.btpl` (caméra de
poursuite), le rayon caméra touche un chunk de terrain *devant* le sol
raster dans une bande en bas de l'écran (profondeur du rayon plus courte
que la prépasse). Les rayons d'ombre, de GI et de reflet voient ce
chunk-là. Rochers et cailloux (scatter) ne sont pas dans la TLAS.

## 7.2 Miroirs

*Architecture, choisie pour 7.2 à 7.5* : prépasse étendue + passes plein
écran, pas de rayon dans le PS des matériaux.
- **Pourquoi pas dans le PS** : il faudrait des MRT supplémentaires à
  toutes les pipelines de la passe `scene` (meshes, terrain, scatter,
  billboards, ciel), et un composite après coup devrait refaire la
  perspective aérienne et l'exposition. Surtout, la réutilisation des
  rayons voisins (7.4) et la reprojection (7.5) ont besoin des rayons de
  tous les voisins *avant* d'ombrer un pixel.
- **Prépasse RT** : une deuxième cible `reflection surface`
  (`R32G32_UINT`) : x = normale d'ombrage monde en octaèdre 2 × 16 bits,
  y = rugosité (10 bits) | genre (2 bits : mesh, chunk, scatter) |
  instance (20 bits, pour le rejet d'historique et les matrices de 7.5).
  Pipelines à part (`PrepassMeshRtPS`, `PrepassTerrainRtPS`,
  `PrepassScatterRtPS`, sans `BATAP_RT` : elles ne tracent rien), choisies
  par `DrawContext::reflections_`. La prépasse raster ne change pas. En RT,
  le terrain y est dessiné par strate (comme la passe éclairée) pour
  connaître le matériau : mêmes triangles, même profondeur. Unlit et
  régolithe s'écrivent rugosité 1 (pas de lobe spéculaire).
- **Passe `reflection trace`** (`ReflectTracePS`, layout RT) : P depuis la
  profondeur (`znear / z` le long du rayon caméra), normale géométrique
  depuis `view normals` (pour décaler l'origine seulement, 1 mm + 0,1 mm
  par mètre), N et rugosité depuis la cible ci-dessus. Sorties
  `reflection raw` (`RGBA16F` : radiance × exposition, bornée à 256 ;
  alpha = distance du hit, 65 000 pour le ciel, 0 = non tracé) et
  `reflection ray` (direction, pdf ; 0 pour un miroir).
- **Au hit** : `RtResolveSurface` (7.1), cône = angle d'un pixel ×
  (distance caméra + t), ombré par `ShadeSurfaceToward` (lumières et rayons
  d'ombre, cache GI pour le diffus, IBL pour le spéculaire du hit : un
  rebond puis l'IBL). Raté : `SampleSky`. Meshes coupés : requête non
  opaque sur `TlasMaskCut`, candidat gardé du bon côté du plan (comme
  `CutShadowHit`). Translucides : hors masque, pas reflétés.
- **Passe éclairée** (variantes RT de `PixelShader`, `TerrainPS`,
  `ScatterPS`) : `ScreenReflection` lit `reflectionTexture_` au pixel,
  divise par l'exposition ; `ShadeSurfaceToward` remplace
  `SampleSkyPrefiltered × SpecularOcclusion` par `lerp(IBL, reflet, poids)`
  multiplié par le même `lobe` (DFG + énergie). Le poids vient de la
  rugosité du PS lui-même. Pas d'occlusion spéculaire sur le reflet tracé :
  le rayon voit l'occultant. Translucides et billboards gardent l'IBL.
- **Miroir = rugosité ≤ 0,05** : toute rugosité est bornée à 0,05
  (`MeshSurface`), « < 0,05 » n'arrive jamais. Un miroir à normal map dont
  le rayon réfléchi passe sous la surface géométrique garde l'IBL.

*Mesuré* (720p, RT ; images dans le scratchpad de la session) :
- `test_reflection` : le sol miroir montre la sphère de cuivre, le pilier,
  les sphères, les deux déchets texturés et le cube rouge au-dessus du
  champ ; la sphère de cuivre montre le cube vert derrière la caméra, le
  cube rouge, le pilier. 6,9 % des pixels à plus de 8 niveaux.
- `test_brdf` : la sphère de cuivre à 0,02 montre le sol et les sphères
  rouges voisines, la rouge à 0,02 un reflet rasant du sol ; 0,15 % des
  pixels à plus de 8 niveaux, tous sur ces deux sphères.
- Raster `test_brdf` identique au pixel. Lune (copie de `planet.btpl`) :
  dans le bruit (aucun pixel à plus de 2 niveaux).
- GPU de la frame : `test_reflection` 0,686 → 0,818 ms, `test_brdf`
  0,715 → 0,771 ms ; lune, temps minimal 0,939 → 1,003 ms (prépasse avec
  sa cible de plus, terrain par strate, passe qui sort tout de suite).

## 7.3 Rugosité intermédiaire

*Fait (2026-11-08)* :
- **Un rayon VNDF** au-dessus de 0,05 : repère tangent quelconque autour de
  N (GGX isotrope), `BrdfSampleVndf` puis `BrdfPdfVndf` (densité en L),
  hachage du pixel et de `frameIndex_` (bruit blanc, change à chaque frame).
  Deux tirages au plus ; une direction sous la surface (géométrique ou
  d'ombrage) est retirée, et après deux échecs le rayon part dans la
  direction miroir avec une pdf nulle.
- **Échantillon démodulé = la radiance du hit, seule.** Avec la VNDF, f
  cos / pdf vaut F G₂/G₁ : presque constant dans le lobe, donc l'image
  moyenne d'un pixel est la moyenne pondérée par la BRDF à un facteur
  près, que la passe éclairée remet (`lobe`). L'estimateur ratio de 7.4
  pèse les échantillons exactement.
- **Cône** : angle d'un pixel × (distance caméra + t) + α t (le lobe
  élargit l'empreinte au hit).
- **Coupure 0,6, fondu de 0,5 à 0,6** (`ReflectWeight`, la même fonction
  dans la passe de trace et la passe éclairée). Choisie sur les images et
  les matériaux : 36 `.bmat` lit du jeu sur 48 non texturés sont sous 0,6,
  dont 28 à 0,45–0,5 (structures, drones, rovers, conteneur, traîneau,
  machines). L'IBL y montre le ciel noir de la lune là où le reflet tracé
  montre le régolithe éclairé ; dans `test_reflection`, la moitié basse
  des sphères à 0,45 passe du brun du nadir au gris du sol.

*Mesuré* (720p, RT, contre 7.2) :
- `test_reflection` : 0,63 % des pixels à plus de 8 niveaux (sphères à
  0,1–0,45, tuyau texturé), `test_brdf` : 0,46 % (cuivre et blanc à
  0,15–0,45). D'une frame à l'autre, 0,31 % des pixels bougent de plus de
  8 niveaux : le bruit blanc d'un rayon, que 7.4 et 7.5 retirent.
- GPU de la frame : `test_reflection` 0,818 → 0,852 ms, `test_brdf`
  0,771 → 0,917 ms (le sol à 0,5 et 14 sphères tracés, rayons
  incohérents).

## 7.4 Démodulation et réutilisation des rayons voisins

*Fait (2026-11-08)* :
- **Passe `reflection resolve`** (`ReflectResolvePS`, layout de base, pas
  de rayon), plein écran, `RGBA16F` : la radiance démodulée filtrée, lue
  par la passe éclairée à la place de la cible brute. Miroirs (pdf nulle) :
  recopiés tels quels.
- **Estimateur ratio** (Stachowiak 2015) : chaque échantillon j (le rayon
  du pixel et 4 voisins) pèse `D · Vis · (N·L) / pdf_j`, la BRDF *du pixel*
  sans Fresnel évaluée vers le point touché par le voisin (`P_j + L_j t_j`,
  ou `L_j` pour le ciel), divisée par la pdf du voisin. Résultat
  `Σ w L / Σ w` : la moyenne de la radiance pondérée par le lobe du pixel,
  que la passe éclairée multiplie par `lobe` (∫ f cos préintégré). Pas de
  jacobien (rapport des distances au point touché), négligeable sous le
  rayon choisi.
- **Rayon** : `α × t × |N·V|`, en pixels à la profondeur du pixel, borné à
  [3 px ; 2 % de la hauteur]. α t est l'empreinte du lobe au point touché,
  donc la parallaxe d'un voisin reste dans le lobe ; |N·V| ramène le disque
  monde à l'axe court de son ellipse à l'écran (sol rasant). Près d'un
  contact, t → 0 : rayon minimal, le reflet reste net (durcissement au
  contact). Disque de Vogel tourné par pixel et par frame.
- **Rejet** : distance au plan tangent du pixel au-delà de 2 % de la
  profondeur. Le poids BRDF écarte le reste. Par voisin, trois lectures
  (brut, rayon, profondeur).
- **Échantillons sans direction** : après quatre tirages VNDF sous la
  surface, le pixel est marqué `ReflectNoSample` (alpha −1) ; ses voisins
  le remplissent, et sans voisin la passe éclairée garde l'IBL. Un miroir
  dont le rayon passe sous la surface reste non tracé (alpha 0).
- **Régularisation au hit** : rugosité du point touché portée au moins à
  celle du pixel (Kaplanyan et Dachsbacher 2013), sinon un rayon rugueux qui
  tombe sur le reflet du soleil d'une sphère miroir donne une luciole
  ≥ 1000 fois le fond. Plafond de 16 (unités exposées) pour un échantillon
  stochastique, 256 pour un miroir.
- **Vue debug « Reflections »** (`DebugReflections` = 128, menu View de
  l'éditeur) : le reflet tel que la passe éclairée le lit, × exposition.

*Pièges* :
- **Y de l'écran** : le viewport a une hauteur négative, donc
  `ndc.y = 1 − 2 (y + 0,5) / h` (comme `AoViewPosition`). Avec le signe
  inverse, la passe recalculait P au pixel symétrique : rejets au hasard,
  poids faux, et le filtre ne filtrait presque rien.
- **Karis** (poids `1 / (1 + luminance)`) essayé contre les lucioles :
  écarté, il assombrit les reflets clairs d'un facteur ~4.

*Mesuré* (720p, RT) :
- Référence : la passe de trace temporairement à 128 rayons VNDF par pixel
  (`make_ref_spec.py` dans le scratchpad), sur une copie de
  `test_reflection` dont le miroir est à 0,3. Erreur RMS (niveaux 8 bits)
  du sol contre elle : un rayon 25,6, avec 7.4 20,9 ; zone du reflet du
  cube rouge 22,7 → 19,6. Le bruit restant (grains clairs du reflet des
  sphères) part en 7.5. 4 voisins plutôt que 8 pour le coût : 8 coûtaient
  +0,1 ms de plus sur `test_brdf`.
- Contre 7.3 : `test_reflection` 0,83 % des pixels à plus de 8 niveaux,
  `test_brdf` 0,24 %. Raster `test_brdf` et `test_reflection` identiques
  au pixel.
- GPU de la frame : `test_brdf` 0,917 → 1,039 ms (sol à 0,5 tracé, 70 %
  de l'écran), `test_reflection` 0,852 → 0,913 ms ; lune, temps minimal
  1,003 → 1,039 ms.

## 7.5 Reprojection par la distance de hit

*Fait (2026-11-08)* :
- **Historique persistant** (`Reflections`, deux paires en ping-pong,
  `createTarget` sans nom, refaites au changement de taille, oubliées par
  le graphe avant destruction) : `RGBA16F` (radiance démodulée exposée,
  alpha = longueur d'historique) et `R32G32B32A32_UINT` (normale et mot
  d'instance de la prépasse, profondeur de vue, inverse de la distance de
  hit accumulée). La passe éclairée lit la sortie de la frame.
- **Passe `reflection temporal`** (layout de base) :
  - point virtuel `X = P + dir × t`, t = moyenne harmonique des distances
    de hit de la passe de réutilisation (le ciel compte pour 1/65 000) ;
  - mouvement : `PreviousPosition` avec les matrices du réflecteur (mesh :
    `world_`/`prevWorld_` de l'instance ; chunk et scatter : leurs
    matrices déjà relatives à la caméra), appliqué à X et à P. Exact pour
    un miroir plan et un contenu reflété immobile ; le ciel (X très loin)
    suit la seule rotation de la caméra ;
  - 4 texels bilinéaires autour de X reprojeté, chacun gardé si même
    instance (genre + id), normale > 0,9, et sur le plan de P à 1 % de la
    profondeur près. La position précédente d'un texel vient de sa
    profondeur et des lignes de `prevViewProj_` (projection de
    `Camera_C::make_proj`). Aucun clamping de voisinage ;
  - longueur max : 1 pour un miroir (aucun historique, rien à lisser),
    rampe jusqu'à 32 frames à 0,3 ;
  - **anti-traînée** : l'inverse de la distance de hit accumulé dans
    l'historique contre sa moyenne 3 × 3 dans la frame courante ; écart
    relatif `|a − b| / (a + b + 0,05)` au-delà de 0,15 réduit la longueur
    d'historique, à 0 au-delà de 0,4. Un objet reflété qui arrive ou part
    change la distance ; seul, l'écart d'une frame (5 échantillons)
    rejetait l'historique partout sur un sol rugueux immobile ;
  - changement d'exposition : l'historique × `exposure_ / prevExposure_`
    (`FrameGPUData::prevExposure_`, ex-`pad3_`).
- `FrameGPUData` passe à 320 octets (textures résolue, historique, surface
  d'historique).

*Pièges* :
- **Store op** : une image importée lue seulement à la frame suivante sort
  en `STORE_OP_DONT_CARE` si son `out` est `Usage::None` (rien ne la lit
  plus tard dans la frame) : l'historique était vide une frame sur deux,
  sans erreur. Import avec `out = Usage::ShaderRead`.
- **Première frame** : une image importée sans usage connu ne peut pas
  être lue (« reads ... before anything writes it ») : la passe ne déclare
  la lecture de l'historique que s'il est valide (`historyValid_`).
- **Comparer deux exécutions** : la frame N de deux lancements n'a pas
  toujours le cube au même endroit (chargement de la scène) ; vérifier
  l'alignement sur une zone sans reflet (`match.py` du scratchpad) avant de
  comparer les reflets.

*Mesuré* (720p, RT) :
- Caméra fixe, sol à 0,3 (copie de `test_reflection`) contre la référence
  à 128 rayons : erreur RMS 25,6 (un rayon) → 20,9 (7.4) → 9,6 ; d'une
  frame à l'autre 25,5 → 7,9. La référence elle-même bouge de 10,7 d'une
  frame à l'autre.
- Cube jaune qui tourne (`test_reflection_moving`, sol à 0,15), frames
  alignées, contre le rendu sans historique : écart moyen dans le reflet du
  cube +0,1 niveau (+1,7 avec l'anti-traînée plus lâche, où une traînée
  se voyait au bord du reflet du pilier). Pas de traînée visible.
- Caméra en orbite (`Orbit_C` sur la caméra, 12° par seconde) : reflets
  du déchet et des sphères suivis, sans dédoublement.
- `test_brdf` : bruit d'une frame à l'autre 7,3 → 4,6 sur les sphères.
  Raster identique au pixel (`test_brdf`, `test_reflection`).
- GPU de la frame : `test_reflection` 0,913 → 0,960 ms ; `test_brdf`
  temps minimal 1,02 → 1,11 ms ; lune, temps minimal 1,039 → 1,100 ms.

## 7.6 SSR d'abord : écarté

*Décidé (2026-11-08)*, sur le critère du plan (seulement si le coût mesuré
dépasse le budget) :
- Mesure en 1440p (fenêtre 2560 × 1440 dans la config de test), réflexions
  coupées par une version temporaire de `Reflections::createImages` qui
  rend `nullopt` (prépasse ordinaire, aucune passe) contre la version
  normale ; temps minimal de la frame, deux lancements par cas. Lune :
  2,45 → 2,90 ms ; `test_reflection` : 1,89 → 2,69 ms ; `test_brdf` :
  1,98 → 3,46 ms. Tableau dans la section Budget du plan.
- Lune ≈ 1,1 ms sur une 3060 : sous le budget de 1–3 ms (réflexions) +
  0,5–1,5 ms (débruitage). Le SSR n'économise des rayons que sur les
  surfaces lisses, rares sur la lune (structures et machines à 0,45–0,5,
  petites à l'écran).
- Leviers si un jeu y trouve un grand sol lisse : passes de réutilisation
  et temporelle en ½ × ½ (comme l'AO), SSR d'abord, ou coupure plus basse.
