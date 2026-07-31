# Batap Engine — suivi

**Statuts** : ` ` à traiter · `✅` fait · `❌` refusé · `⏸` plus tard · `?` à discuter

Réponds en éditant la colonne statut, ou en chat avec l'ID (`B6 go`, `DX7 ❌`, `F8 ⏸`).

---

## BUG — correctness

| ID | Statut | Sujet | Où |
|----|--------|-------|-----|
| B6 | | **Ring d'upload : corruption quand la frame dépasse 64 Mo** (détail ci-dessous) | `Renderer/ResourceManager.cpp:154-186` |
| B7 | | `requestUploadOwned` alloue un `std::vector` par requête → une alloc par patch/entité/frame | `Renderer/ResourceManager.cpp` |
| B8 | | Shutdown : `_resourceManager`/`_psoManager`/`_renderGraph` en `new` jamais détruits, device libéré avant ses ressources | `Renderer/Renderer.cpp:637` |
| B9 | ✅ | `Systems::freecam_` jamais construit — UB qui marche par accident (aucun membre, `this` jamais touché) | `Systems/Systems.cpp:22` |

### B6 — détail

Quand les 64 Mo de la frame sont dépassés :

```cpp
_fenceManager.signal(_fenceId, commandQueue);
_fenceManager.waitForLastSignal(_fenceId);
std::cout << "FIXME : remainingData -> wait : repenser l'upload...\n";
```

Le signal part sur la queue immédiatement, mais les `CopyBufferRegion` sont enregistrés dans un command list **pas encore soumis**. L'attente ne garantit rien → on écrase la zone que le GPU n'a pas encore lue. Stall CPU *et* corruption silencieuse.

Fix : allocateur de staging par blocs, recyclés derrière une fence. Design identique en DX12 et Vulkan → à faire une seule fois. Traite B7 dans le même mouvement.

---

## DX — ergonomie pour qui *utilise* le moteur

| ID | Statut | Sujet | Où |
|----|--------|-------|-----|
| DX1 | | **Ajouter/modifier un composant ne marque pas dirty → échec silencieux.** Hooks entt `on_construct`/`on_update`/`on_destroy` → `markDirty`. Aucun hook n'existe aujourd'hui | `Scene.h`, `Instance/InstanceManager.cpp` |
| DX2 | | Façade jeu (`spawn`/`setPosition`/`input`/`load`) + header parapluie `batap.h`. Aujourd'hui : `world.systems_->_transforms->setLocalPosition(...)` et 9 includes | — |
| DX3 | | `loadAsset<T>()` typé au lieu de `optional<variant<...>>` + `std::get` | `Assets/AssetLoader.h` |
| DX4 | | Pas de `IsKeyPressed`/`IsKeyReleased`/`GetMouseWheel` — les sets existent mais sans accesseur | `InputManager.h:203` |
| DX5 | | `get<T>()` est `noexcept` **et** lève → `std::terminate`. `emplace<T>()` ne transmet pas d'arguments. Pas de surcharges `const` | `Components/EntityHandle.h:28,46` |
| DX6 | | `Transform_S::setParent` déclaré, jamais défini → erreur de link. C'est celui avec `keepWorld` | `Systems/Transform_S.h:39` |
| DX7 | | Trois conventions de nommage. `_meshInstancesPool` et `pointLightInstancePool_` à 7 lignes d'écart | partout |
| DX8 | | `v3f`/`m4f`/`quatf`/**`transform`** dans le namespace global | `EigenTypes.h` |
| DX9 | | Ni `#pragma once` ni `namespace batap` — seul composant dans ce cas | `Components/FreeCamController_C.h` |
| DX10 | | Jeu d'exemple avec du **code** — `GameExemple/` n'a que des assets | `GameExemple/` |
| DX11 | | **Couche plateforme SDL3** — fusionne ex-DX11 + ex-DX15 + ex-D6, voir détail plus bas | `Editor/WindowsApp.cpp`, `WindowsUtils/`, `InputManager.cpp`, `Renderer.h:73` |
| DX12 | | Pas d'enregistrement de systèmes utilisateur — `Systems::update` est en dur | `Systems/Systems.cpp:16` |
| DX13 | | Aucune requête : pas de raycast, pas de `findByName`, pas de query spatiale (`Bbox.hpp` existe, inutilisé) | — |
| DX14 | | Pas de timestep fixe, pas de pause, pas de timescale | `Context.cpp:69` |

### DX11 — détail (couche plateforme SDL3)

Deux problèmes qui se corrigent d'un seul geste :

- **Le moteur ne sait pas ouvrir de fenêtre ni boucler.** Toute la plomberie est dans `Editor/`, qui est un *exe* → un jeu doit recopier ~300 lignes de Win32, et devine l'ordre d'init (`renderer->init()` avant `ctx.init()`, sinon `AssetManager` reçoit un `ResourceManager*` nul). `IApp` est dans `Engine/` mais rien ne le consomme.
- **Et l'API du moteur parle Win32,** donc une couche plateforme étrangère ne peut pas se brancher : `Renderer::init(HWND)`, et `InputManager` décode `WM_*`/`RAWINPUT` en interne (`InputManager.cpp:150-210`).

Bonne nouvelle : le vocabulaire agnostique existe déjà (`Key`, `MouseButton`, `KeyEvent`, `MouseEvent`, `Nano::Signal`). Il manque `feed(KeyEvent)` au lieu de `ProcessWindowsEvent(msg,w,l)`, et un handle natif opaque au lieu de `HWND`.

**Ce que SDL3 remplace** : `WindowsApp.cpp` (311 l.), `WindowsUtils/FileDialog.cpp` (238 l.), le `switch(message)` (~60 l.), le DPI, l'icône, `_dupenv_s`/APPDATA → `SDL_GetPrefPath`. Et `Resize()`/`SetFullscreen()`, aujourd'hui entièrement commentés, deviennent gratuits. Gains nets : gamepad (mapping + hotplug) et un device audio.

**Fenêtre transparente conservée** : `SDL_PROP_WINDOW_CREATE_TRANSPARENT_BOOLEAN` pose `WS_EX_NOREDIRECTIONBITMAP`, et `..._EXTERNAL_GRAPHICS_CONTEXT_BOOLEAN` dit à SDL de ne pas toucher au swapchain. Tout le bloc DirectComposition reste inchangé. Repli si besoin : `SDL_PROP_WINDOW_CREATE_WIN32_HWND_POINTER` pour adopter une fenêtre créée à la main (~30 l. de Win32 conservées).

**Ne pas utiliser** : `SDL_Render`, et surtout **`SDL_GPU`** — plus petit dénominateur commun, pas de bindless, incompatible avec le système d'instances. SDL sert à abstraire l'OS, pas le GPU. L'audio SDL est un device + des streams, pas un moteur audio (mixage/spatialisation → miniaudio ou SoLoud par-dessus, plus tard).

**Quand** : le jour où tu écris un exe de jeu. Pas urgent — B6 et DX1 font plus de dégâts aujourd'hui.

---

## FEAT — ce qui manque pour être utilisable

| ID | Statut | Sujet | Où |
|----|--------|-------|-----|
| F1 | | Gizmo de manipulation (ImGuizmo) — aujourd'hui on place les objets en tapant des chiffres | `Editor/UI/` |
| F2 | | Picking souris dans la viewport (RT `R32_UINT` avec l'instance index) | `Renderer/SceneRenderer.cpp` |
| F3 | | « Open Scene » : `clearSceneAndLoad` existe et marche, branchée sur **aucun** menu | `Editor/UI/UIPanels.cpp:39` |
| F4 | | Supprimer / dupliquer une entité — `EntityFactory::destroy` existe, aucun bouton ne l'appelle | `Editor/UI/ScenePanel.cpp` |
| F5 | | Menu « Camera » vide (`// action`) | `Editor/UI/ScenePanel.cpp:111` |
| F6 | | **Aucun mipmap** → aliasing partout, et `mipCount` toujours 1 donc l'IBL spéculaire par roughness ne peut pas fonctionner | `Assets/AssetLoader.cpp` |
| F7 | | Lumière directionnelle + shadow map. `castShadows_` est sérialisé, affiché, envoyé au GPU… et lu par aucun shader | `Shaders/`, `PointLight_C.h` |
| F8 | | HDR : RT en `R8G8B8A8_UNORM` + `saturate()` en fin de PS → pas d'exposition, bloom impossible | `Renderer.cpp:210`, `PixelShader.hlsl:252` |
| F9 | | Frustum culling + tri des draws. Un draw par entité, 4 VBV rebindés à chaque fois, aucun culling | `Renderer/SceneRenderer.cpp:137` |
| F10 | | Hot-reload des shaders | `Renderer/Shaders.cpp` |
| F11 | | Play/Stop avec snapshot mémoire. `populateWorld` prend déjà un `json` ; il manque le `toJson` symétrique | `Serialization/EntitySerializer.cpp` |
| F12 | | Supprimer le code mort : `SVO`, `VoxelDataStructs`, `Bbox`, `VoxelMap_C`, 3 compute shaders de raymarch, `pass_render0` commentée | `Engine/` |
| F13 | | Passe de composition = `CopyResource` → aucun emplacement pour du post-process | `Renderer.cpp:460` |
| F14 | | `projectHDRIToSH` recalculée à **chaque lancement** (~8-10 s en debug sur un HDRI 4k, avant la première frame). À calculer à l'import et sérialiser dans le `.btex` — `Texture::irradianceSH_` existe déjà, il n'est pas persisté | `Renderer/SkyIrradiance.cpp`, `Serialization/BtexSerializer.cpp` |

---

## DECOUPLE — réduire la dépendance DX12

Objectif retenu : améliorer le moteur en minimisant le couplage, **sans** s'engager sur un port. Chaque item est utile en soi.

| ID | Statut | Sujet | Où |
|----|--------|-------|-----|
| D1 | | fxc/SM 5.1 → DXC/SM 6.6. Gain immédiat : `ResourceDescriptorHeap`, wave intrinsics. Et DXC sait sortir du SPIR-V | `Renderer/Shaders.cpp:36` |
| D2 | | API `ResourceManager` agnostique : `createStructuredBuffer(count, stride)`, `createTexture2D(w,h,ResourceFormat,mips)`, `bindlessIndex(handle)` | `Renderer/ResourceManager.h` |
| D3 | | Sortir D3D12 de `InstanceManager.h` (construit des `D3D12_SHADER_RESOURCE_VIEW_DESC` à la main) | `Instance/InstanceManager.h` |
| D4 | | Sortir D3D12 de `GPUArena.h` | `Utils/GPUArena.h` |
| D5 | | Sortir `DXGI_FORMAT` / `D3D12_RESOURCE_STATE` de `AssetLoader.cpp` | `Assets/AssetLoader.cpp` |
| D6 | → | Fusionné dans **DX11** | — |
| D7 | | CMake : séparer les flags clang des flags MSVC (`/O2`, `/EHsc` mélangés aux warnings clang) | `CMakeLists.txt:57` |

Après D2-D5 : `grep -rl D3D12 src/` ne devrait renvoyer que `Engine/Renderer/`.
