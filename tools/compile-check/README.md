# compile-check — vérifier le moteur sans Windows

Le build officiel utilise clang-cl, dont le frontend est le même clang que
celui de macOS/Linux. Ce harnais fait donc un `clang++ -fsyntax-only` (parse +
typecheck complets, avec le jeu de warnings du projet en `-Werror`) sur chaque
TU compilable hors Windows. Ce qui passe ici compile là-bas, aux réserves
près listées plus bas.

## Lancer

```sh
# une fois : les submodules header-only
git submodule update --init --depth 1 \
  include/DirectX-Headers include/eigen include/emhash include/entt \
  include/imgui include/json include/magic_enum include/nano-signal-slot \
  include/stb

tools/compile-check/check.sh                 # tout
tools/compile-check/check.sh src/Engine/Serialization/EntitySerializer.cpp
```

Sortie : une ligne `ok`/`FAIL` par TU, code retour non nul si au moins un FAIL.

## Comment ça marche

- Les types D3D12 sont les vrais : DirectX-Headers est complet et supporte
  officiellement les hôtes non-Windows (`wsl/winadapter.h` + `wsl/stubs/`).
- `stubs/` fournit le peu que le SDK Windows apporte en plus : `HWND` en
  pointeur, les 3 fonctions d'événement que `FenceManager.h` appelle inline,
  des forward declarations DXGI/DirectComposition (uniquement portées par des
  `ComPtr<>` dans les headers), `_dupenv_s`. `stubs/prelude.h` est
  force-inclus devant chaque TU.
- Ces stubs ne sont **jamais** vus par le vrai build : rien dans le CMake ne
  référence ce dossier.

## Ce que ça ne valide pas

- Le **link** et le **runtime** (aller-retour save/load, rendu).
- Les TU exclus en tête de `check.sh` : code Win32/DXGI réel
  (`Renderer.cpp`, plateforme, entry points) et `Importers/` (submodule
  assimp non requis). Leurs *headers* sont couverts transitivement.
- Trois warnings coupés car spécifiques à l'hôte, pas au code :
  `-Wpadded` et `-Wweak-vtables` (le clang-cl/ABI Microsoft ne les émet pas —
  vérifié : ils flambent sur des structs qui compilent déjà sous Windows) et
  `-Wpoison-system-directories` (artefact de cross-compilation macOS).

## CI

Le script tourne tel quel sur un runner Linux GitHub Actions :
checkout + `submodule update --init --depth 1` des 9 submodules ci-dessus +
`tools/compile-check/check.sh`. Pas de SDK à installer.
