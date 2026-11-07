# Objectifs

Idées pas encore faites, une par ligne. Le détail de l'ancienne version est dans git (`5e5cb46`).

## Rendu

- Culling GPU two-phase Hi-Z : meshes dans un buffer partagé, frustum en compute, `vkCmdDrawIndexedIndirectCount`, HZB.
- Grille de clusters de lumières (froxels) : scènes à N lumières, puis brouillard volumétrique.
- AO, bruit : jitter par pixel et flou bilatéral guidé par la profondeur (plan RT 4.3).
- AO, ambiant directionnel : SH du ciel pondérée par les secteurs libres du bitmask (plan RT 4.4).
- Indirect diffus en espace écran (SSILVB §3.3) pour le raster : rebond lu dans la lumière HDR de la frame précédente.
- Ombres de contact en espace écran : 8 à 16 pas dans la profondeur sur ~1 m.
- Occulteurs d'ombre en capsule, en plus des sphères.
- Shadow map par objet, pour l'auto-ombrage d'un gros objet lointain.

## Matériaux

- Viewer d'assets : mesh, texture ou matériau dans une fenêtre, caméra orbitale, propriétés à côté.
- Teinte par instance (`Tint_C`) pour les flashs de coup.
- Make unique : copie locale d'un matériau sur un objet, valeurs écrites dans la scène.
- Samplers par texture : `filter_` et `wrap_` de `Texture` lus par le shader (aujourd'hui un seul `g_sampler`).
- Panneau texture : espace couleur, filtre, wrap, mips, réimport depuis le fichier source.
- Matériau = shader : `MaterialMain` remplit `Surface`, le PS de géométrie devient un squelette.
- Paramètres de matériau à taille variable, UI de l'éditeur dérivée du struct.
- Une pipeline par shader de matériau, draws groupés par pipeline.
- Hot reload des `.material.hlsl`, erreurs de dxc affichées dans le panneau.
- Éditeur de nœuds qui génère le `.material.hlsl`, seulement si le HLSL à la main freine.
- Seconde fenêtre OS pour les éditeurs, seulement si la fenêtre ImGui flottante gêne.

## Moteur

- Hot reload : snapshot binaire par pool et hash de layout, à la place du JSON.
- Requête `findByName` côté moteur.
- Descriptor layouts dérivés des shaders par réflexion SPIR-V.
