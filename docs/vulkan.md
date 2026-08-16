# Vulkan — comprendre le backend

Guide de lecture du code Vulkan du moteur, écrit pour quelqu'un qui connaît
déjà DX12 (toi). Chaque concept est expliqué à travers le code réel du repo :
`src/Engine/Renderer/Vulkan/VulkanContext.{h,cpp}` et le harness
`tools/vk-smoke/main.cpp` (jalons 1-2 : triangle offscreen → PNG).

Pour voir tourner tout ce qui est décrit ici :

```bash
./tools/vk-smoke/run.sh      # build + run, produit vk_smoke.png
```

---

## 1. La pile, de ton code au GPU

```
ton code ──> loader Vulkan (libvulkan) ──> driver / ICD ──> GPU
             │                             │
             │ dispatche les appels        │ sur mac : MoltenVK,
             │ + injecte les layers        │ qui traduit vers Metal
             └─ validation layers ici ─────┘
```

Trois différences de philosophie avec DX12 à intégrer d'entrée :

1. **Le loader est une indirection explicite.** En DX12 tu linkes `d3d12.lib`
   et c'est fini. En Vulkan, un *loader* (libvulkan) trouve les drivers (ICD)
   et te donne des pointeurs de fonctions. C'est le rôle de **volk**
   (`include/volk`, un seul `.c`) : il `dlopen` le loader et remplit tous les
   pointeurs — `volkInitialize()`, puis `volkLoadInstance()`/`volkLoadDevice()`
   pour court-circuiter le dispatch et gagner un chouïa de perf.
   Sur mac, le loader brew vit dans `/opt/homebrew/lib`, hors des chemins
   `dlopen` par défaut → `run.sh` pose `DYLD_LIBRARY_PATH`.

2. **La validation est une couche optionnelle, pas un mode du driver.**
   L'équivalent du debug layer DX12 (`ID3D12Debug`) est la *validation layer*
   Khronos, interposée par le loader entre ton code et le driver. On l'active
   à la création de l'instance + un *debug messenger* qui reçoit les messages
   (l'équivalent de l'info queue). C'est ton premier réflexe de debug, comme
   sous DX12.

3. **Sur mac il n'y a pas de driver Vulkan natif** : MoltenVK est un ICD qui
   traduit Vulkan vers Metal. Il expose Vulkan 1.4 sur l'Apple M3 et accepte
   toutes les features qu'on exige (vérifié au jalon 1) — pour nous c'est un
   driver comme un autre.

## 2. Dictionnaire DX12 → Vulkan

| Tu connais (DX12) | Équivalent Vulkan | Nuance qui compte |
|---|---|---|
| `IDXGIFactory` + debug layer | `VkInstance` | + validation layers et extensions déclarées à la création |
| `IDXGIAdapter` | `VkPhysicalDevice` | énumération + *feature query* très fine |
| `ID3D12Device` | `VkDevice` | créé en déclarant les queues ET les features qu'on utilisera |
| `ID3D12CommandQueue` (DIRECT/COMPUTE/COPY) | `VkQueue` d'une *queue family* | les familles sont découvertes, pas choisies : chaque GPU expose les siennes |
| `ID3D12CommandAllocator` + `GraphicsCommandList` | `VkCommandPool` + `VkCommandBuffer` | reset par *pool* entier de préférence (1 pool/frame) |
| `ID3D12Fence` (+ valeur monotone) | `VkSemaphore` **timeline** | mapping 1:1 avec `FenceManager` ; le `VkFence` binaire ne sert qu'à attendre côté CPU |
| `D3D12_RESOURCE_STATES` + `transitionTo` | `VkImageLayout` + `vkCmdPipelineBarrier2` | voir §4, c'est LE morceau à comprendre |
| Root signature | `VkPipelineLayout` | même rôle : la « signature » des ressources du pipeline |
| Root constants | Push constants | idem, ~128 octets garantis |
| Descriptor heap + tables | `VkDescriptorPool` / `VkDescriptorSetLayout` / `VkDescriptorSet` | le plus gros écart conceptuel — jalon 3 |
| PSO (`D3D12_GRAPHICS_PIPELINE_STATE_DESC`) | `VkPipeline` | quasi identique champ à champ |
| RTV + `OMSetRenderTargets` + `ClearRenderTargetView` | `VkImageView` + `vkCmdBeginRendering` (dynamic rendering) | le clear est un `loadOp`, pas une commande |
| `CreateCommittedResource` | `VkBuffer`/`VkImage` **sans mémoire** + allocation séparée | d'où VMA, voir §3 |
| DXIL | SPIR-V | mêmes sources HLSL, `dxc -spirv` |

## 3. `VulkanContext` pas à pas (jalon 1)

`VulkanContext::init()` fait, dans l'ordre :

1. **`volkInitialize()`** — trouve le loader, remplit les pointeurs globaux.

2. **`VkInstance`** via vk-bootstrap (`include/vk-bootstrap` — uniquement du
   boilerplate d'init, remplaçable par du code maison plus tard) :
   validation layers + debug messenger + version d'API exigée (1.3).
   L'instance est l'objet « monde Vulkan » : c'est à travers elle qu'on
   énumère les GPUs.

3. **Sélection du `VkPhysicalDevice`** — c'est ici que Vulkan est plus strict
   (et plus honnête) que DX12 : chaque feature qu'on compte utiliser doit être
   *déclarée et supportée*, sinon la création échoue. On exige :
   - `dynamicRendering` (1.3) — rendre sans `VkRenderPass`, cf. §6 ;
   - `synchronization2` (1.3) — l'API moderne des barrières, cf. §4 ;
   - `timelineSemaphore` (1.2) — le modèle exact de ton `FenceManager` ;
   - `bufferDeviceAddress` (1.2) — pointeurs GPU (utile plus tard) ;
   - `descriptorIndexing` + `runtimeDescriptorArray` + `partiallyBound` +
     `variableDescriptorCount` (1.2) — **le bindless** : l'équivalent de tes
     descriptor heaps + `g_textures[]` unbounded.

4. **`VkDevice` + queue** — on récupère une queue *graphics* et l'index de sa
   famille. Note : en Vulkan on ne crée pas les queues, on les *demande* à la
   création du device parmi les familles que le GPU expose (graphics,
   compute-only, transfer-only…). L'équivalent de tes trois
   `D3D12_COMMAND_LIST_TYPE` existe donc, mais découvert à l'exécution.

5. **VMA** (`include/VulkanMemoryAllocator`) — en Vulkan, créer un
   `VkBuffer`/`VkImage` n'alloue *pas* de mémoire : tu dois choisir un *memory
   type* (device-local, host-visible, cached…), allouer, et binder — et le
   nombre d'allocations est limité, donc il faut sous-allouer. VMA est
   l'allocateur qui fait ça bien (c'est l'équivalent du travail que
   `CreateCommittedResource` fait pour toi en DX12, en mieux contrôlable).
   Config dans `VulkanMemory.h` : fonctions récupérées dynamiquement via volk.

`shutdown()` détruit dans l'ordre inverse — Vulkan exige que le device soit
détruit après tout ce qu'il a créé (les validation layers hurlent sinon, c'est
le pendant du B8 de TODO.md).

## 4. Layouts d'image et barrières — LE concept à intégrer

En DX12 tu penses « états » : `transitionTo(cmdList, D3D12_RESOURCE_STATE_X)`.
En Vulkan, une barrière dit **trois** choses, explicitement :

```cpp
VkImageMemoryBarrier2 b{};
b.srcStageMask  = ...;  // 1. j'attends que CES étages du pipe aient fini
b.srcAccessMask = ...;  //    ... et que CES types d'écriture soient flushés
b.dstStageMask  = ...;  // 2. avant que CES étages ne démarrent
b.dstAccessMask = ...;  //    ... avec CES types d'accès visibles
b.oldLayout     = ...;  // 3. et au passage, réarrange la mémoire de l'image
b.newLayout     = ...;  //    de CE layout vers CE layout
```

- Le **layout** (`VK_IMAGE_LAYOUT_*`) est la partie qui ressemble aux resource
  states : `COLOR_ATTACHMENT_OPTIMAL` ≈ `RENDER_TARGET`, `TRANSFER_SRC_OPTIMAL`
  ≈ `COPY_SOURCE`, etc. C'est un vrai réarrangement mémoire potentiel
  (compression, tiling), pas juste un flag de bookkeeping.
- Les **stage/access masks** sont la partie que DX12 déduisait pour toi : la
  synchronisation. `synchronization2` (qu'on utilise partout) permet de les
  donner par paires src/dst dans un seul appel `vkCmdPipelineBarrier2`.

Exemple concret du smoke test — « l'image devient une render target » :

```cpp
imageBarrier(VK_IMAGE_LAYOUT_UNDEFINED,                    // je me fiche du contenu
             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,     // deviens une RT
             VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,       // rien à attendre
             VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
             VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);      // avant les écritures de RT
```

Le futur `ResourceManager` Vulkan encapsulera ça exactement comme
`GPUResource::transitionTo` le fait en DX12 — l'appelant continuera de penser
en usages, la table usage → (layout, stage, access) vivra dans le backend.

## 5. Shaders : HLSL → SPIR-V

Rien ne change dans tes sources : **les mêmes `.hlsl`**, compilés par **le même
dxc** (l'API `IDxcCompiler3` de `Shaders.cpp`), avec un flag en plus :
`-spirv -fspv-target-env=vulkan1.3`. Les 5 shaders du moteur passent déjà.

Côté C++, le bytecode SPIR-V s'enveloppe dans un `VkShaderModule` (trois
lignes, cf. `createShaderModule` dans le smoke test) et se branche au pipeline
avec son *entry point* — d'où les `mainVS`/`mainPS` de `triangle.hlsl` : un
seul fichier peut contenir plusieurs entry points, chacun compilé séparément.

Deux conventions à connaître (fixées au jalon 2) :

- **Bindings** : `register(t0, space1)` HLSL devient `binding/set` SPIR-V. Tant
  qu'on n'a pas de descriptors (jalon 3), pas de décision à prendre ; ce sera
  les flags `-fvk-{b,t,u,s}-shift` ou `[[vk::binding]]`, décidé une fois.
- **Y inversé** : le NDC Vulkan a Y vers le bas. Plutôt que toucher shaders ou
  matrices, on passe un **viewport à hauteur négative** (core depuis 1.1) —
  le triangle du smoke test pointe vers le haut précisément grâce à ça.
  NDC z ∈ [0,1] : identique à DX12, rien à faire.

## 6. Le pipeline et le rendu (jalon 2)

`VkGraphicsPipelineCreateInfo` est ton `D3D12_GRAPHICS_PIPELINE_STATE_DESC`
éclaté en sous-structs — la correspondance est presque mécanique :

| DX12 (`desc.`) | Vulkan (`pipelineInfo.p*State`) |
|---|---|
| `VS`, `PS` | `pStages[]` (module + entry point) |
| `InputLayout` | `pVertexInputState` |
| `PrimitiveTopologyType` | `pInputAssemblyState` |
| `RasterizerState` | `pRasterizationState` |
| `BlendState` | `pColorBlendState` |
| `DepthStencilState` | `pDepthStencilState` |
| `RTVFormats` / `DSVFormat` | `VkPipelineRenderingCreateInfo` (chaîné en `pNext`) |
| `pRootSignature` | `layout` (`VkPipelineLayout`) |

La ligne « RTVFormats » mérite un mot : historiquement Vulkan exigeait un
`VkRenderPass` + `VkFramebuffer` décrivant tout le graphe d'attachments à
l'avance. **Dynamic rendering** (core 1.3) supprime tout ça : le pipeline
déclare juste les formats, et au moment de dessiner :

```cpp
vkCmdBeginRendering(cmd, &renderingInfo);   // ≈ OMSetRenderTargets + Clear
vkCmdBindPipeline(...);                     // ≈ SetPipelineState + SetRootSignature
vkCmdDraw(cmd, 3, 1, 0, 0);                 // ≈ DrawInstanced
vkCmdEndRendering(cmd);
```

`VkRenderingAttachmentInfo` fusionne le bind de RT et le clear : `loadOp =
CLEAR` + `clearValue` remplacent `ClearRenderTargetView` (et `storeOp = STORE`
dit de garder le résultat). C'est le modèle que le `RenderGraph` portera.

## 7. Exécution et synchronisation CPU↔GPU

Le flux du smoke test, qui est aussi le squelette d'une frame :

```
CommandPool ── alloc ──> CommandBuffer
                             │  begin (ONE_TIME_SUBMIT)
                             │  barrier  UNDEFINED → COLOR_ATTACHMENT
                             │  beginRendering (clear bleu) / bind / draw / end
                             │  barrier  COLOR_ATTACHMENT → TRANSFER_SRC
                             │  copyImageToBuffer (readback host-visible, mappé)
                             │  end
vkQueueSubmit(queue, ..., fence)
vkWaitForFences(...)         ← équivalent de ton signal() + waitForLastSignal()
```

- Le `VkFence` binaire suffit ici (attente CPU ponctuelle). Pour le moteur, ce
  seront des **timeline semaphores** : un compteur 64 bits monotone qu'on
  signale/attend par valeur — ton `FenceManager` (id + `_currentValue`) mappe
  dessus sans changer d'API.
- Le buffer de readback est créé par VMA en mémoire host-visible et **mappé en
  permanence** (`VMA_ALLOCATION_CREATE_MAPPED_BIT`) — même principe que ton
  ring d'upload DX12 qui reste mappé.

## 8. Les descriptors et `VulkanResources` (jalon 3)

Le plus gros écart conceptuel avec DX12. Là-bas : un grand descriptor heap
shader-visible, des handles GPU, `g_textures[]` unbounded indexé par
`heapIdx`. En Vulkan, trois objets :

- `VkDescriptorSetLayout` — la *forme* d'un set (≈ une entrée de root signature) ;
- `VkDescriptorPool` — l'arène d'allocation (≈ le heap) ;
- `VkDescriptorSet` — un paquet de descriptors qu'on binde d'un coup.

`Renderer/Vulkan/VulkanResources.{h,cpp}` reproduit le modèle du moteur avec
**un seul set bindless global**, construit dans `init()` :

- binding 0 : le sampler par défaut, *immutable* (gravé dans le layout —
  l'équivalent exact de ton static sampler `s0`) ;
- binding 1 : un tableau de sampled images à taille **variable** (4096),
  `PARTIALLY_BOUND` (pas besoin que tous les slots soient remplis) et
  `UPDATE_AFTER_BIND` (on peut écrire des slots alors que le set est déjà
  utilisé par des frames en vol — indispensable pour charger des textures en
  cours de jeu). Contrainte de spec : le binding à taille variable doit être
  le dernier du set.

`createTexture2D()` alloue un slot (free-list + compteur), écrit le descriptor
via `vkUpdateDescriptorSets`, et renvoie ce `bindlessIndex` — le même rôle que
`heapIdx` en DX12 : tes matériaux le stockent, le shader indexe avec. Côté
HLSL (cf. `tools/vk-smoke/texture.hlsl`), l'index arrive en **push constant**
(`[[vk::push_constant]]`, l'équivalent des root constants) :

```hlsl
[[vk::binding(0, 0)]] SamplerState g_sampler   : register(s0);
[[vk::binding(1, 0)]] Texture2D    g_textures[] : register(t0);
[[vk::push_constant]] PushConstants g_push;
...
g_textures[g_push.texIdx].Sample(g_sampler, i.uv);
```

Les autres différences de design vs le ResourceManager DX12 :

- **Pas d'objets « view » pour les buffers** : un descriptor référence
  `{buffer, offset, range}` directement, et les vertex/index buffers se
  bindent par `vkCmdBindVertexBuffers`/`BindIndexBuffer` sans view du tout.
  `GPUMeshView` et le zoo CBV/SRV/UAV/RTV/DSV n'ont pas d'équivalent — les
  handles de view restent des alias stables, pour la parité du contrat.
- **Staging ring zéro-copie** : `requestUploadOwned` rend un span *directement
  dans le ring mappé* (en DX12 il rendait un `std::vector` recopié ensuite —
  le bug B7). `flushUploads(cmd)` enregistre les `vkCmdCopyBuffer`/
  `CopyBufferToImage` + les barrières de layout. Un ring par frame-in-flight,
  recyclé par `beginFrame(i)` quand le GPU a fini ce slot.
- **Pas d'alignement de row pitch** pour les textures : le 256 était une règle
  DX12 ; en Vulkan les rangées sont serrées (`bufferRowLength = 0`).
- **Destruction différée par slot de frame** : `requestDestroy` met en file,
  `beginFrame(i)` détruit ce qui avait été mis en file au cycle précédent —
  plus simple que la fence par flush du DX12, même garantie.

## 9. L'architecture de frame du moteur (jalon 5)

Le moteur entier tourne sur mac : `cmake --preset macos-debug`, puis

```bash
DYLD_LIBRARY_PATH=/opt/homebrew/lib ./build/macos-debug/bin/Batap_TestGame --project GameExemple
# BATAP_DUMP_FRAME=60 en plus → écrit frame_dump.png (readback de la frame 60)
```

Le rendu n'est PAS un portage 1:1 du RenderGraph DX12 — c'est le design que
Vulkan 1.3 permet, en plus simple. Les fichiers, dans l'ordre de lecture :

- `Renderer/Vulkan/VulkanRenderer.{h,cpp}` — l'orchestrateur. Une frame :
  `beginFrame()` (début de frame CPU : attend la fence du slot, recycle
  staging + destructions, publie `frameIndex_`, ouvre la frame ImGui) → la
  simulation écrit ses uploads → `render()` : check hot-reload → `flushUploads`
  → barrières → **un** rendering scope (swapchain + depth, loadOp CLEAR) dans
  lequel la scène s'enregistre puis ImGui se dessine → barrière → present.
  `frameIndex_` est publié en DÉBUT de frame CPU exprès : tout ce qui est
  « par frame en vol » (dirty tracking, staging) doit voir le slot en cours
  de production, pas celui de la frame passée.
- `Renderer/Vulkan/VulkanScenePasses.{h,cpp}` — les passes geometry + sky,
  et le hot reload (mtime des .hlsl du dossier source → recompile → rebuild
  des pipelines ; une erreur HLSL garde les pipelines courantes).
- `Renderer/Vulkan/VulkanPipelines.{h,cpp}` — chargement SPIR-V + builder de
  pipeline (dynamic rendering, viewport dynamique Y-up).
- `Renderer/Vulkan/VulkanShaderCompiler.{h,cpp}` — HLSL → SPIR-V à chaud via
  `libdxcompiler.dylib` (dlopen paresseux ; même `IDxcCompiler3` que Windows).
- `Renderer/Vulkan/VulkanSceneRenderer.cpp` — le pont trois-lignes entre le
  header neutre `SceneRenderer.h` et le backend.

**Autour du rendu :**

- *Présentation* : IMMEDIATE si disponible (pas de vsync, comme le DX12),
  sinon MAILBOX, sinon FIFO. Le mode se choisit dans
  `VulkanSwapchain::createSwapchain()`, réutilisé tel quel au resize.
- *Resize* : le delegate Cocoa (`syncLayerSize`) ajuste la CAMetalLayer puis
  appelle `Renderer::resize` → `VulkanSwapchain::recreate()` (oldSwapchain
  dans la create-info, sémaphores de rendu par image recréés) + depth. Si la
  swapchain périme entre deux événements, `acquire()` rend `OutOfDate` et la
  frame est sautée — les uploads en attente partent à la suivante.
- *ImGui* : `imgui_impl_vulkan` (dynamic rendering, fonctions via volk) dessine
  dans le même rendering scope que la scène ; le backend plateforme
  (`imgui_impl_osx` / `imgui_impl_win32`) est derrière les hooks neutres
  `platformImGui{Init,NewFrame,Shutdown}` de `PlatformWindow.h`.
- *Input* : chaque OS décode ses événements natifs dans sa couche plateforme
  (NSEvent dans `MacOSWindow.mm`, WM_*/RAWINPUT dans `Win32Window.cpp`) et
  alimente l'API neutre `InputManager::feed()` ; quand ImGui capture
  (WantCaptureMouse/Keyboard), le jeu ne voit pas l'événement.

**Le modèle de binding — deux sets, un layout, zéro écriture par draw :**

| Quoi | Où | Fréquence |
|---|---|---|
| sampler + textures bindless | set 0 (ResourceManager) | écrit à la création des textures, bindé 1×/frame |
| caméras, instances, lights, matériaux, skybox (5 storage buffers) | set 1 (ScenePasses, un par frame en vol) | réécrit 1×/frame (les pools grandissent) |
| `{camIdx, instanceIdx, submeshIdx, nLights}` | push constants (16 octets, VS+PS) | par draw |

Côté HLSL, chaque ressource porte son `[[vk::binding(n, set)]]` — c'est LE
contrat entre `VulkanScenePasses.cpp` (enum `FrameSetBinding`) et les shaders.

**Ce qui a disparu par rapport au DX12, et pourquoi :**

- *La passe composition* — c'était un `CopyResource` render3D → backbuffer sans
  post-process. On rend directement dans la swapchain.
- *Le normalRT* — écrit par le PS, lu par personne. Le PS a une seule sortie.
- *`SV_PrimitiveID` + le scan `triangleOffsets`* — la capability Geometry
  n'existe pas sur Metal. À la place : **un draw par submesh**, l'index de
  submesh en push constant — plus simple ET portable.
- *Les 16 root constants du sky* — le SkyPS lit le storage buffer skybox, où
  ces données étaient déjà (le PixelShader s'en sert pour l'IBL). Une source
  de vérité.
- *Les 7 slots de root signature* → un pipeline layout partagé par les passes.

**Leçon du premier debug** (la scène noire) : le chargement ignorait des champs
au nom périmé dans les `.btpl` (`path` → `mesh`, `materials` → `slots`/`count`)
— *silencieusement*. Les scènes ont été migrées (backups `.old-format.bak`), et
`AssetFieldTypes` signale désormais tout handle non résolu sur stderr. Le
round-trip des composants inconnus (OBJECTIFS §1) réglera le fond du problème.

## 10. Gotchas mac / MoltenVK

- **Loader introuvable au run** (`volkInitialize` échoue) → il manque
  `DYLD_LIBRARY_PATH=/opt/homebrew/lib` ; `run.sh` s'en charge.
- **dxc** vient du SDK LunarG (`~/VulkanSDK/1.4.357.0/macOS/bin`) — pas de
  binaire Microsoft pour mac, pas de formule brew.
- MoltenVK traduit vers Metal : quelques limites du *portability subset*
  existent, mais rien de ce qu'on utilise (vérifié au jalon 1 : toutes nos
  features 1.3/1.2 passent, bindless compris).
- La fenêtre mac passe par `VK_EXT_metal_surface` sur la `CAMetalLayer` posée
  par `Platform/MacOS/MacOSWindow.mm` (impl Cocoa de `PlatformWindow.h`).
- Le hot reload cherche `libdxcompiler.dylib` via dlopen (DYLD_LIBRARY_PATH,
  puis le SDK repéré au configure) — absente, il se désactive sans casser.
