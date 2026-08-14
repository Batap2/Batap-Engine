# Migration Vulkan — plan

> Relecture de la branche : **docs/vulkan-changements.md** (la carte du diff,
> fichier par fichier) + **docs/vulkan.md** (les concepts Vulkan via le code).

## ⚡ Reprise sur Windows — brief pour l'instance qui lit ceci à froid

Tu (Claude) reprends ce travail sur le PC Windows de Baptiste, avec un contexte
neuf. Tout ce qui suit a été appris/décidé pendant le port sur mac (août 2026) :

**Où on en est.** Le switch DX12→Vulkan est TERMINÉ et validé sur mac (Apple
M3, MoltenVK) : scène Cornel en PBR bindless, sky, ImGui, input, resize, hot
reload shaders, fenêtre transparente, éditeur qui se lance — zéro erreur de
validation. DX12 est supprimé du repo (étape C). Le chemin Windows est
**écrit mais n'a jamais été compilé** — c'est ta mission.

**Ta mission, dans l'ordre.**
1. SDK Vulkan LunarG installé ? (`C:/VulkanSDK/*` — le CMake y cherche
   `Bin/dxc.exe`). Configurer via `build_msvc.bat` / les presets msvc.
2. Passe de fix de compilation : couche Win32 (input `feed()`, hooks
   `platformImGui*`, `Win32FileDialog`, transparence DWM), backend commun
   (surface `VK_KHR_win32_surface` — windows.h AVANT volk), CMake unifié.
   Le code a été écrit à l'aveugle depuis le mac : attends-toi à des broutilles,
   pas à des problèmes de fond.
3. Valider le rendu : `BATAP_DUMP_FRAME=N` en variable d'env → `frame_dump.png`
   de la frame N (l'outil de validation utilisé pour tout le port — compare
   avec un run mac si doute).
4. Divergences driver connues à vérifier : composite alpha (transparence)
   dispo ou pas ; present mode IMMEDIATE dispo ou pas (sinon MAILBOX/FIFO,
   déjà géré) ; validation layers actives en debug via vk-bootstrap.

**Comment travailler avec Baptiste (important).**
- Réponses en **français**, tutoiement.
- Nommage : membres `var_` (suffixe), locaux/params nus — **jamais** `_var`.
- Minimum de libs, natif par OS — ne JAMAIS proposer SDL/GLFW.
- `docs/` (MkDocs) en anglais ; les .md perso du repo (comme celui-ci) en
  français.
- Pas d'optimisation ni de refactor non demandés : Baptiste relit TOUT le
  diff de la branche avant de commiter — chaque changement doit être
  justifiable et documenté dans docs/vulkan-changements.md.
- ⚠ Tant que la branche n'est pas commitée : ne JAMAIS `git checkout`/`restore`
  un fichier pour annuler une modif temporaire (l'index contient les versions
  d'avant-port — un checkout a déjà détruit PixelShader.hlsl une fois).
  Sauvegarde par `cp`, restauration par `cp`.

**Les trois docs.** Ce fichier = plan et état. `docs/vulkan.md` = les concepts
Vulkan expliqués via le code (à lire avant de toucher au backend).
`docs/vulkan-changements.md` = la carte du diff pour la relecture, à tenir à
jour si tu changes quelque chose.

Décision (août 2026) : **remplacement sec**, et **bring-up sur mac d'abord**.
FAIT : le backend DX12 a servi de spécification de lecture pendant le port,
sans jamais être recompilé, puis a été **supprimé** (étape C, 14 août).
Le moteur est Vulkan-only ; Windows (de retour ~16 août) est re-servi par le
même backend + la couche fenêtre Win32 existante (chemin écrit, à compiler).
Pas de RHI à deux backends, rien de polymorphe : les types publics sont
neutres, un seul backend derrière.

**Couche plateforme : native par OS, pas de SDL/GLFW** (décision août 2026 —
« pur, minimum de libs »). `PlatformWindow.h` est déjà l'interface opaque ;
l'impl Win32 existe, l'impl Cocoa est à écrire (`Platform/Mac/MacWindow.mm` :
NSWindow + CAMetalLayer + pompe NSEvent). Input : décodage natif par OS
alimentant `InputManager` via une API neutre `feed(KeyEvent/MouseEvent)`
(le vocabulaire `Key`/`MouseButton` existe déjà). Dialogs : NSOpenPanel / COM
par OS. ImGui : `imgui_impl_osx` / `imgui_impl_win32` selon la plateforme.

**Cadrage** : chemin critique = faire tourner le moteur sur le mac. Optimisation,
clarté, hygiène → « Reporté ».

**Environnement mac** (en place) : brew — molten-vk, vulkan-loader (dans
`/opt/homebrew/lib`, hors dlopen par défaut → `DYLD_LIBRARY_PATH`, géré par
`tools/vk-smoke/run.sh`), vulkan-headers, vulkan-validationlayers. SDK LunarG
`~/VulkanSDK/1.4.357.0` : `dxc` CLI dans `macOS/bin`, `libdxcompiler.dylib`
dans `macOS/lib` (même API `IDxcCompiler3` que sous Windows).
Acquis vérifiés sur l'Apple M3 : MoltenVK expose Vulkan 1.4 et accepte
**toutes** nos features 1.3/1.2 (dynamic rendering, synchronization2, timeline
semaphores, descriptor indexing complet — le bindless passe) ; les 5 shaders
HLSL du moteur compilent en SPIR-V 1.3 sans warning.

---

## Fait

- [x] **FXC → DXC** (`Shaders.cpp`, `IDxcCompiler3`, SM 6.6, bytecode neutre
      `vector<uint8_t>`). Chemin SPIR-V prouvé sur mac ; le chemin DXIL ne sera
      jamais exercé.
- [x] **Étape A — frontière scellée** : hors `Renderer/`, plus aucun code ne
      nomme D3D12. API neutre du `ResourceManager` (`createStaticBuffer`,
      `createStatic/FrameStructuredBuffer` → `{resource, srv}`,
      `createTexture2D` → `{resource, srv, bindlessIndex}`, `bindlessIndex(view)`,
      `requestTextureUploadOwned(ResourceFormat)`) ; fuites colmatées
      (`InstanceManager.h`, `GPUArena.h`, `AssetLoader.cpp`) ;
      `Renderer::init(void*)`.
- [x] **Jalon B1** — `Renderer/Vulkan/VulkanContext.{h,cpp}` : volk +
      vk-bootstrap + VMA + validation layers, clear color offscreen relu en PNG.
      Harness autonome `tools/vk-smoke/` (projet CMake indépendant).

## Étape B — le backend, jalon par jalon (tout se passe sur mac)

Les jalons 2-3 vivent dans le harness `vk-smoke` (headless, PNG). À partir du
jalon 4, c'est le moteur lui-même qui compile et tourne sur mac.

2. [x] **Triangle offscreen** : HLSL → SPIR-V (dxc au build, CMake du harness),
       `VkPipeline` en dynamic rendering, pipeline layout, viewport à hauteur
       négative (convention Y-up actée), draw, readback PNG — validé sur M3.
       Guide de lecture : `docs/vulkan.md`.
3. [x] **ResourceManager Vulkan** — `Renderer/Vulkan/VulkanResources.{h,cpp}` +
       `VulkanFormats.h`, validé sur M3 (texture uploadée via staging ring,
       samplée bindless par index en push constant). Design épuré vs DX12 :
       pas d'objets « view » pour les buffers, un seul set bindless (sampler
       immutable + tableau variable partially-bound update-after-bind),
       staging ring mappé zéro-copie (le design que B6 aurait dû avoir),
       destruction différée par slot de frame. Reste pour l'intégration
       moteur (jalon 5) : `createFrameStructuredBuffer` et le branchement
       des appelants de l'étape A.
4. [x] **Fenêtre + swapchain** — validé sur M3, à l'écran, via la vraie couche
       plateforme :
       - `Renderer/Vulkan/VulkanSwapchain.{h,cpp}` : surface Metal, swapchain
         FIFO 3 images, acquire/present, sémaphores par image + fences par frame ;
       - `Platform/MacOS/MacOSWindow.mm` : impl Cocoa de `PlatformWindow.h`
         (NSWindow + CAMetalLayer, delegate close/resize/retina, pompe,
         exeDir/args) — miroir de la version Win32 ;
       - `PlatformWindow.h` gagne `platformSurfaceHandle()` (HWND sur Windows,
         CAMetalLayer* sur mac — ce sur quoi la VkSurfaceKHR se crée) ;
       - le harness `vk_window` (`window_main.cpp`, zéro Objective-C) consomme
         tout ça — c'est le test de l'abstraction.
       Reste au jalon 5 : décodage NSEvent → `InputManager::feed()` (API
       neutre à créer) et resize → `Renderer::resize` (TODO posés dans la .mm).
5. [x] **Le moteur compile et tourne sur mac** — scène Cornel rendue (geometry
       pass complète : PBR, point lights, matériaux, bindless — vérifiée par
       readback `BATAP_DUMP_FRAME`, zéro erreur de validation). Architecture
       des passes : docs/vulkan.md §9. En place :
       - headers-aiguillage `Renderer/Renderer.h` + `ResourceManager.h`
         (`#if _WIN32` → DX12, sinon → `Renderer/Vulkan/`) ; la classe Vulkan
         s'appelle `ResourceManager`/`Renderer`, appelants inchangés ;
       - API `ResourceManager` complétée : `createFrameStructuredBuffer`
         (FramesInFlight buffers routés au flush), mesh views IBV/VBV
         (métadonnées de bind), uploads avec sous-région (InstanceManager) ;
       - **input** : API neutre `InputManager::feed(KeyEvent/MouseEvent)` ;
         le décodage natif vit dans la couche plateforme des DEUX OS
         (NSEvent dans `MacOSWindow.mm`, WM_*/RAWINPUT dans
         `Win32Window.cpp`) — `InputManager.cpp` est 100 % portable ;
       - **resize** : `VulkanSwapchain::recreate()` (oldSwapchain, sémaphores
         de rendu par image recréés), depth recréée, `acquire()` rend
         `OutOfDate` au lieu de jeter → frame sautée proprement ; branché au
         delegate Cocoa (`syncLayerSize`) ;
       - **ImGui** : `imgui_impl_osx` + `imgui_impl_vulkan` (dynamic
         rendering, volk) ; le backend plateforme passe par les hooks neutres
         `platformImGui{Init,NewFrame,Shutdown}` (impls Cocoa ET Win32) ; la
         capture ImGui (WantCaptureMouse/Keyboard) filtre l'input du jeu ;
       - **sky** validée (Skybox_C gradient ajouté à la scène Cornel, pixels
         du ciel vérifiés par dump caméra hors de la boîte) ;
       - **hot reload shaders** : `VulkanShaderCompiler` (libdxcompiler.dylib
         en dlopen, même IDxcCompiler3 que Windows) ; ScenePasses surveille
         les mtime des .hlsl du dossier source et reconstruit les pipelines —
         erreur HLSL = log + pipelines conservées ; validé en éditant
         PixelShader.hlsl pendant que le jeu tournait ;
       - **présentation sans vsync** (IMMEDIATE si dispo, sinon MAILBOX,
         sinon FIFO) — le FIFO du bring-up plafonnait à 60 fps ;
       - fix synchro frames-in-flight : `_frameIndex` publié dans
         beginImGuiFrame (le dirty tracking visait le slot de la frame
         précédente → une frame sur deux noire) ;
       - CMake : tri plateforme dans `src/Engine/CmakeLists.txt`, preset
         `macos-debug` ; F5 VS Code : configs `TestGame/Editor (macos-debug)`.
       NOTE données : les `.btpl` étaient dans un ancien format de champs
       (`path`/`materials`) que le load ignorait EN SILENCE — migrés
       (backups `.old-format.bak`), warning permanent ajouté dans
       `AssetFieldTypes.cpp`. Le round-trip d'OBJECTIFS §1 réglera le fond.
6. [~] **Éditeur sur mac** — `Batap_Editor` compile, link et affiche son écran
       de démarrage sur mac (zéro erreur de validation) :
       - `main.cpp` portable (`main` / `wWinMain` par #ifdef) ;
       - FileDialog : le header `WindowsUtils/FileDialog.h` était déjà neutre ;
         impl Cocoa `Platform/MacOS/MacOSFileDialog.mm` (NSOpenPanel /
         NSSavePanel, filtres via UTType). Les variantes Async sont modales
         (AppKit exige le main thread) mais honorent le même contrat de bus ;
       - assimp + `Importers/` compilés sur mac (submodule, plus de gate) ;
       - `configPath()` portable (%APPDATA% / ~/Library/Application Support).
       RESTE à valider à la main : parcours complet (Browse → projet → scène,
       import assimp, sauvegarde), et la comparaison visuelle avec les
       captures DX12.

**Correspondances** (pour la réécriture des .cpp de `Renderer/`) :

| DX12 | Vulkan |
|---|---|
| `ID3D12Device` | `VkDevice` (+ `VkPhysicalDevice`, queue families) |
| `IDXGISwapChain4` + waitable object | `VkSwapchainKHR` + acquire semaphores |
| Command list + allocator | `VkCommandBuffer` + `VkCommandPool` (1 pool/frame/queue) |
| `FenceManager` (fence + valeur) | `VkSemaphore` timeline — mapping 1:1 |
| Root signature | `VkPipelineLayout` |
| Root constants | Push constants |
| Descriptor tables / heaps | `VkDescriptorSetLayout` + un gros set bindless par frame |
| Static samplers | Immutable samplers |
| `D3D12_RESOURCE_STATES` | `VkImageLayout` + `vkCmdPipelineBarrier2` |
| PSO | `VkPipeline` (formats RT via `VkPipelineRenderingCreateInfo`) |
| DXC → DXIL | DXC → `-spirv -fspv-target-env=vulkan1.3` (même API, un flag) |

**Pièges à traiter au premier triangle, pas après :**
- **Y inversé** : viewport à hauteur négative → shaders et matrices inchangés.
  NDC z ∈ [0,1] : identique à DX12, rien à faire.
- **Bindings HLSL → SPIR-V** : `register(bN/tN/uN, spaceM)` → `binding/set` via
  `-fvk-{b,t,u,s}-shift`. Décider la convention une fois.
- **Validation layers + `VK_EXT_debug_utils`** dès le jour 1 (fait au jalon 1).
- **Upload/staging** : ring simple par frame, correct d'entrée (pas le design
  par blocs complet de B6 — voir Reporté).

## Étape C — suppression [FAITE]

- [x] Supprimé : `includeDX12.h`, tous les .cpp/.h DX12 de `Renderer/`
      (Renderer, ResourceManager, SceneRenderer, Shaders, CommandQueue,
      DescriptorHeapAllocator, Descriptorhandle, FenceManager, RenderGraph,
      ResourceName), le bloc DComp (`CompositionSwapChain` retiré
      d'EngineConfig et de Win32Window), les libs DX du CMake, le submodule
      `DirectX-Headers`, `imgui_impl_dx12` du build, les ComputeShader*.hlsl
      orphelins, et `tools/compile-check/` (le build mac est un vrai build).
      `Renderer.h`/`ResourceManager.h` sont devenus de simples forwarders vers
      `Renderer/Vulkan/`.
- [x] La couche Win32 (fenêtre, input, dialogs) **reste** — c'est l'impl
      plateforme de Windows, au même titre que Cocoa sur mac. Le décodage
      d'input vit dans `Win32Window.cpp` derrière `InputManager::feed()`
      (même API que le mac), les hooks `platformImGui*` Win32 sont écrits,
      et `FileDialog.h` (neutre, à la racine Engine) a ses deux impls dans
      `Platform/Win32/Win32FileDialog.cpp` et `Platform/MacOS/`.
- [x] Windows re-ciblé par le backend Vulkan — écrit mais **jamais compilé**
      (pas de machine) : surface `VK_KHR_win32_surface` dans VulkanSwapchain
      (windows.h avant volk), `VulkanShaderCompiler` portable
      (LoadLibrary/dlopen), CMake unifié (volk + vk-bootstrap + VMA + assimp
      des deux côtés, dxc du SDK Vulkan, `VOLK_STATIC_DEFINES
      VK_USE_PLATFORM_WIN32_KHR`, copie de dxcompiler.dll pour le hot reload).
      **Au retour sur Windows : installer le SDK Vulkan LunarG, configurer,
      et prévoir une passe de fix de compilation.**

---

## Reporté (utile, mais ne fait pas tourner le moteur sur mac)

- **Header interop C++/HLSL** + **validation des layouts par réflexion**
  (OBJECTIFS §1) — la synchro manuelle marche ; SPIRV-Reflect le jour venu.
- **B6/B7 — staging allocator par blocs recyclés** — le ring Vulkan sera écrit
  correctement d'entrée ; le design par blocs viendra après.
- **B8 — shutdown propre** — les validation layers râleront à la sortie du
  process ; accepté tant que ça ne touche pas le rendu.
- **Génération des descriptor layouts par réflexion** — on câble à la main.
- ~~**Fenêtre transparente**~~ FAITE (14 août) : `WindowDesc::transparent`,
  pour les outils/overlays (jamais la fenêtre de jeu — interdit le scanout
  direct). mac : NSWindow non-opaque + compositeAlpha POST_MULTIPLIED (testé).
  Windows : BLACK_BRUSH + `DwmEnableBlurBehindWindow` + PRE_MULTIPLIED si le
  driver l'expose (écrit, non compilé). Décisions actées en route :
  `WS_EX_NOREDIRECTIONBITMAP` est INTERDIT avec Vulkan (sans bitmap de
  redirection le driver n'a nulle part où présenter) ; l'interop
  Vulkan→DXGI/DComp est écartée définitivement (une copie GPU par frame de
  toute façon, pour un module entier de synchro cross-API en plus).
- **Dé-vendorer vk-bootstrap** — il ne sert qu'à l'init ; remplaçable par ~400
  lignes maison quand l'envie de pureté l'emportera. volk et VMA, on garde.
- Hot reload shaders, mipmaps, HDR… — après la première frame.

## Décisions actées

- Remplacement sec ; DX12 jamais revalidé, supprimé après parité.
- **Bring-up sur mac** ; Windows re-servie ensuite par le même backend Vulkan.
- **Couche plateforme native par OS** (Win32 existante + Cocoa à écrire),
  pas de SDL/GLFW — « pur, minimum de libs ».
- HLSL conservé (DXC → SPIR-V), bindings explicites, pas de
  `ResourceDescriptorHeap` (non supporté par la sortie SPIR-V de DXC).
- Vulkan 1.3 minimum, bindless via descriptor indexing.
- `ResourceFormat` garde ses valeurs DXGI ; la traduction Vulkan est une table.
- Chemin critique only : optimisation et clarté attendent la première frame.
