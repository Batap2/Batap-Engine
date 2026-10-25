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
