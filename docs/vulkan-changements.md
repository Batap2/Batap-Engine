# Branche vulkan — guide de relecture

Tout ce qui a changé depuis `main`, fichier par fichier, avec le pourquoi.
**L'étape C est faite : DX12 n'existe plus dans le repo** — le moteur est
Vulkan-only, mac et Windows partagent le même backend.

À lire avec les deux autres docs :

- **`src/Engine/VULKAN.md`** — le plan et son état (jalons, décisions actées) ;
- **`docs/vulkan.md`** — le doc pédagogique : les concepts Vulkan expliqués à
  travers le code du repo (à lire AVANT le code si Vulkan est encore flou) ;
- **ce fichier** — la carte du diff, pour la relecture.

Ordre conseillé : docs/vulkan.md (concepts) → le backend (§1, dans l'ordre
donné) → la couche plateforme (§2) → le reste en diagonale avec ce guide.

---

## 1. Le backend — `src/Engine/Renderer/` (Vulkan-only)

Ce qui reste dans `Renderer/` à la racine :

| Fichier | Rôle |
|---|---|
| `Renderer.h`, `ResourceManager.h` | Simples **forwarders** vers `Renderer/Vulkan/` — les 40+ appelants n'ont pas à connaître l'arborescence. |
| `SceneRenderer.h` | Header neutre du pont scène↔renderer (impl : `Vulkan/VulkanSceneRenderer.cpp`). |
| `EngineConfig.h` | `FramesInFlight = 3`, `BindlessTextureCapacity = 4096` (l'héritier du CBV_SRV_UAV heap — le SEUL descripteur qui se dimensionne encore), `StagingBytesPerFrame = 64 Mo`. (RTV/DSV/Sampler heaps et `CompositionSwapChain` sont morts avec DX12 : plus rien à dimensionner.) |
| `ResourceFormatWrapper.h` | L'enum `ResourceFormat` — il garde les **valeurs DXGI** exprès : les assets sérialisés y font référence, la traduction Vulkan est une table (`VulkanFormats.h`). |
| `SkyIrradiance.{h,cpp}` | Projection SH de l'HDRI/du gradient — **CPU, portable** (utilisé par le pipeline d'assets et l'instance skybox). |

`Renderer/Vulkan/`, dans l'ordre de lecture :

| Fichier | Rôle |
|---|---|
| `VulkanContext.{h,cpp}` | Init : volk (loader) + vk-bootstrap (instance/device) + VMA + validation layers. Toutes les features 1.3/1.2 exigées (dynamic rendering, sync2, bindless…) sont listées ici. |
| `VulkanSwapchain.{h,cpp}` | Surface (Metal sur mac, Win32 sur Windows — `#elif` sur `VK_USE_PLATFORM_*`) + swapchain + synchro de présentation. `recreate()` pour le resize, présentation sans vsync (IMMEDIATE > MAILBOX > FIFO). Transparence : compositeAlpha POST/PRE_MULTIPLIED si le driver l'expose, fallback OPAQUE loggé. |
| `VulkanResources.{h,cpp}` | La classe `ResourceManager`. Set 0 bindless (sampler immutable + 4096 textures, update-after-bind), staging rings mappés par frame, buffers par-frame, destructions différées par slot. |
| `VulkanPipelines.{h,cpp}` | SPIR-V + builder de pipeline (dynamic rendering). `setViewportYUp` : viewport à hauteur **négative** = convention Y-up de DX12, shaders/matrices inchangés. |
| `VulkanScenePasses.{h,cpp}` | Passes geometry + sky. Set 1 = 5 storage buffers par frame (caméras, instances, lights, matériaux, skybox). Un draw **par submesh** (l'index en push constant — pas de SV_PrimitiveID sur Metal). Le hot reload vit ici. |
| `VulkanShaderCompiler.{h,cpp}` | HLSL → SPIR-V à chaud via la lib DXC native (dlopen/LoadLibrary paresseux, non-fatal si absente). ComPtr RAII local (pas d'ATL). |
| `VulkanRenderer.{h,cpp}` | L'orchestrateur, classe `Renderer`. Frame complète : docs/vulkan.md §9. Init ImGui. Debug : `BATAP_DUMP_FRAME=N` → `frame_dump.png`. |
| `VulkanSceneRenderer.cpp` | Le pont trois-lignes vers le header neutre `SceneRenderer.h`. |
| `VulkanFormats.h`, `VulkanMemory.h` | Table `ResourceFormat`→`VkFormat` ; config VMA. |

**Pièges corrigés en route, à connaître pour la relecture :**

- `_frameIndex` est publié dans `beginImGuiFrame()` (début de frame CPU), pas
  dans `render()` : le dirty tracking par frame-in-flight doit voir le slot en
  cours de production. L'avoir en fin de frame donnait UNE FRAME SUR DEUX NOIRE.
- FIFO (vsync) plafonnait à 60 fps → IMMEDIATE quand dispo.
- `acquire()` rend `OutOfDate` (frame sautée proprement) au lieu de jeter.

## 2. La couche plateforme — `src/Engine/Platform/`

- `PlatformWindow.h` — le contrat neutre. Ajouts : `platformSurfaceHandle()`
  (ce sur quoi la VkSurface se crée : HWND sur Windows, CAMetalLayer* sur mac)
  et `platformImGui{Init,NewFrame,Shutdown}` (backend ImGui par OS).
- `MacOS/MacOSWindow.mm` — **neuf** : NSWindow + CAMetalLayer, delegate
  (close, resize → `Renderer::resize`), pompe NSEvent, **décodage input
  complet** (keycodes Carbon → `Key`, modificateurs via `flagsChanged`,
  souris en pixels physiques origine haut-gauche) → `InputManager::feed()`.
  Respecte la capture ImGui (WantCaptureMouse/Keyboard).
- `MacOS/MacOSFileDialog.mm` — **neuf** : NSOpenPanel/NSSavePanel pour
  `FileDialog.h`. Variantes Async modales (AppKit exige le main thread) mais
  même contrat de bus que les threads détachés de Win32.
- `Win32/Win32Window.cpp` — le décodage WM_*/RAWINPUT a déménagé ici depuis
  InputManager.cpp (miroir exact du .mm : table VkToKey + `feed()`), hooks
  `platformImGui*` (ImGui_ImplWin32), `WS_EX_NOREDIRECTIONBITMAP` retiré
  (mort avec DirectComposition).
- `Win32/Win32FileDialog.cpp` — l'ex `WindowsUtils/FileDialog.cpp` (COM),
  déplacé ici ; `WindowsUtils/` n'existe plus, le header neutre est
  `src/Engine/FileDialog.h`.
- `InputManager.{h,cpp}` — plus AUCUN code plateforme. `feed(KeyEvent)` /
  `feed(MouseEvent)` : la plateforme décode, l'InputManager accumule.

**⚠ Rien de la couche Win32 ni du chemin Vulkan-Windows n'a été compilé**
(pas de machine Windows pendant le port) — voir §7.

## 3. Modifs du code moteur existant

- `Engine.h` — `WindowDesc::transparent` (défaut `false`) : fenêtre composée
  avec alpha par pixel, pour les **outils/overlays uniquement** — une fenêtre
  transparente ne peut jamais avoir le scanout direct (le compositeur reste
  dans la boucle), donc jamais sur la fenêtre de jeu. mac : NSWindow
  non-opaque + POST_MULTIPLIED (testé, M3). Windows : BLACK_BRUSH +
  `DwmEnableBlurBehindWindow` + PRE_MULTIPLIED — remplace le DirectComposition
  du DX12, écrit non compilé. Le clear passe à alpha 0 quand le flag est mis.
- `Engine.cpp` — `_renderer->init(window_, …)` sans cast HWND, passe
  `desc.transparent`.
- `DebugUtils.cpp` — portable (`windows.h`/`OutputDebugString`/`__debugbreak`
  sous `#ifdef`, `FAILED(hr)` → `hr < 0`).
- `Utils/GPUArena.h`, `Instance/InstanceManager.h`, `Assets/AssetLoader.cpp` —
  types neutres partout (l'étape A : plus une trace de D3D12 hors backend).
- `Serialization/AssetFieldTypes.cpp` — warning stderr permanent sur handle
  non résolu (la leçon de la scène noire, cf. §5).
- `Editor/main.cpp` — `main()`/`wWinMain` par #ifdef ; `Editor/App.cpp` —
  `configPath()` portable (%APPDATA% / ~/Library/Application Support).

## 4. Shaders HLSL (`src/Engine/Shaders/`)

Il reste 4 shaders (les ComputeShader*.hlsl du DX12, orphelins, sont supprimés) :

- chaque ressource porte `[[vk::binding(n, set)]]` — le contrat avec l'enum
  `FrameSetBinding` de VulkanScenePasses ;
- root constants → `[[vk::push_constant]] struct DrawPush
  {_cameraIndex,_instanceIndex,_submeshIndex,_pointLightCount}` partagé VS/PS ;
- `PixelShader.hlsl` : une seule sortie SV_Target (le normalRT, écrit-jamais-lu,
  a disparu) ; matériau via `_materialIndices[_submeshIndex]` (plus de
  SV_PrimitiveID — capability Geometry absente sur Metal) ;
- `SkyPS.hlsl` : lit `SkyboxBuffer[0]` au lieu de 16 root constants dédiés.

## 5. Données (`GameExemple/`)

- **Migration `.btpl`** : anciens noms de champs (`mesh.path` → `mesh` ;
  `materials` → `slots[8]` + `count`) ignorés EN SILENCE par le chargement →
  scène noire au premier allumage. Migrées, originaux en `*.old-format.bak`
  (non suivis — à supprimer quand tu valides).
- **`cornelScene.btpl` : entité `Sky` ajoutée** (Skybox_C Gradient) pour
  valider la passe sky. **Ça change l'éclairage de la Cornel** (IBL du ciel
  active) — comportement correct, pas une régression.
- `GameExemple/main.cpp` : `ImGui::ShowDemoWindow()` et `transparent = true`
  laissés pour tester à la main — à virer quand tu n'en as plus besoin (et
  la transparence n'a rien à faire sur une fenêtre de jeu ; pour la VOIR,
  supprime l'entité Sky de la scène : le bureau apparaît derrière la boîte).

## 6. Build & outillage

- **Submodules** : + `volk`, `vk-bootstrap`, `VulkanMemoryAllocator` ;
  − `DirectX-Headers` (supprimé avec DX12).
- `CMakeLists.txt` racine — volk/vk-bootstrap/assimp pour les DEUX OS ;
  `VOLK_STATIC_DEFINES` = `VK_USE_PLATFORM_WIN32_KHR` / `METAL_EXT` selon
  l'OS ; l'éditeur n'est plus gated Windows.
- `src/Engine/CmakeLists.txt` — tri plateforme réduit à la couche Platform/
  (le backend est commun) ; imgui : `imgui_impl_vulkan` partout + win32/osx
  par OS (`-w` sur le code tiers) ; compilation des 4 shaders → `bin/shaders/`
  via le dxc du SDK Vulkan (découverte par OS) ; headers dxc + chemin de la
  lib DXC pour le hot reload ; sous Windows, copie de `dxcompiler.dll` à côté
  des exes (pas de dxil.dll : la sortie SPIR-V n'est pas signée).
- `tools/compile-check/` — **supprimé** : il servait à vérifier le code DX12
  depuis le mac ; le build mac est maintenant un vrai build du moteur entier.
- `tools/vk-smoke/` — **neuf** : harness des jalons 1-4 (offscreen PNG,
  triangle, texture bindless, fenêtre). Garde sa valeur de repro minimale.
- `CMakePresets.json` — preset `macos-debug` ; `.vscode/` — F5
  `TestGame/Editor (macos-debug)` (CodeLLDB,
  `DYLD_LIBRARY_PATH=/opt/homebrew/lib`), chemins Windows en dur retirés de
  settings.json.

## 7. Ce qui n'est PAS testé (à faire au retour Windows / à la main)

1. **Tout le chemin Windows** : la couche Win32 modifiée (input migré, hooks
   ImGui, FileDialog déplacé, fenêtre transparente via DwmEnableBlurBehindWindow
   + dwmapi.lib) et le backend Vulkan sous Windows (surface win32, CMake
   unifié, hot reload via LoadLibrary) — écrit proprement mais **jamais
   compilé**. Au retour : installer le SDK Vulkan LunarG, configurer, prévoir
   une passe de fix. NB transparence Windows : dépend du driver
   (supportedCompositeAlpha) ; si le driver n'expose que OPAQUE, fenêtre
   normale + message stderr, pas de casse.
2. **L'éditeur mac en interactif** : Browse (NSOpenPanel) → projet → scène,
   import assimp, sauvegarde. Il compile, se lance et rend son écran de
   démarrage sans erreur de validation — le parcours complet est à toi.
3. **Le resize en live** (drag de la fenêtre) : mécanisme validé par code,
   pas encore à l'œil.
4. La comparaison visuelle avec les anciennes captures DX12.
