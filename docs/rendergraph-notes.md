# Render graph — notes par étape

Le détail que le plan (`rendergraph.md`) ne porte pas : mesures complètes,
itérations rejetées, pièges. Une section par étape, dans l'ordre du plan.

## 0.1 Banc de régression

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
