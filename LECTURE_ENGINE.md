# Plan de lecture — comprendre la migration Vulkan

Objectif : lire tous les changements de la branche `vulkan` et comprendre tous les liens.
Coche `[x]` au fur et à mesure. Généré le 2026-08-14 depuis `git diff main...vulkan`
(126 fichiers, ~8900+/~5400−, dont ~3400 lignes de nouveau code Vulkan/MacOS).

## Pourquoi pas la recherche en profondeur depuis Engine

Ton instinct (partir de `Engine`, descendre chaque membre en DFS) a un vrai défaut pour un
cerveau humain : au 3e niveau de profondeur tu tombes sur `VulkanResources.cpp` (618 lignes)
sans savoir *pourquoi* il existe ni qui s'en sert, et tu dois garder toute la pile "je venais
d'où déjà ?" en tête — c'est exactement ce que la mémoire de travail fait mal. En plus, un
DFS sur le code actuel ne montre jamais ce qui a été **supprimé** (tout le DX12), donc tu
rates la moitié du diff.

Le plan ci-dessous inverse l'ordre : **la carte d'abord, les fondations ensuite, la trace
d'exécution à la fin**. Le DFS reste le bon réflexe, mais *à l'intérieur* d'un module, pas
entre modules. Chaque phase tient en une session de lecture.

---

## Phase 0 — La carte (≈30 min, à ne pas sauter)

Lire le *pourquoi* avant le *comment*. Ces docs existent déjà dans le commit :

- [ ] `src/Engine/VULKAN.md` — ton plan de migration : ce qui est fait, les jalons, et
      surtout la section **« Décisions actées »** (volk, vk-bootstrap, VMA…)
- [ ] `docs/vulkan.md` + `docs/vulkan-changements.md` — la version doc publique
- [ ] Lancer `git diff --stat main...vulkan -- src/` et survoler 2 min : repérer les trois
      familles — **nouveau** (`Renderer/Vulkan/`, `Platform/MacOS/`), **vidé** (l'ancien
      `Renderer/` DX12), **retouché** (le reste)

À la fin de cette phase tu dois pouvoir dire en une phrase : « on a remplacé X par Y, et
les modules Z n'ont pas bougé ».

## Phase 1 — Le squelette inchangé (≈20 min)

Vérifier que la façade publique est stable — c'est ce qui rend tout le reste lisible.

- [ ] `src/Engine/Engine.h` — les 4 membres (`_renderer`, `_inputManager`,
      `_sceneRenderer`, `_assetManager`) et le pattern `Frame` RAII
- [ ] `git diff main...vulkan -- src/Engine/Engine.cpp` — ce qui a changé dans
      l'orchestration (création fenêtre, beginFrame/endFrame)
- [ ] `git diff main...vulkan -- src/Editor/main.cpp` — le point d'entrée, 27 lignes

## Phase 2 — Le nouveau backend, de bas en haut (le gros morceau, ~2200 lignes)

Ordre des dépendances : chaque fichier n'utilise que des types déjà lus. Pour chaque
module : lire le `.h` en entier, puis le `.cpp` en DFS interne. Les tailles te donnent
le budget d'effort.

- [ ] 1. `VulkanContext.h/.cpp` (30+123) — instance, device, queues via vk-bootstrap
- [ ] 2. `VulkanMemory.h` (17) + `VulkanFormats.h` (62) — vocabulaire, lecture rapide
- [ ] 3. `VulkanResources.h/.cpp` (206+618) — **le cœur** : buffers, images, descriptors.
      Prends ton temps ici, tout le reste s'appuie dessus
- [ ] 4. `VulkanSwapchain.h/.cpp` (77+278) — acquire/present, resize
- [ ] 5. `VulkanShaderCompiler.h/.cpp` (32+197) — HLSL → SPIR-V
- [ ] 6. `VulkanPipelines.h/.cpp` (46+187) — assemblage shaders + états
- [ ] 7. `VulkanRenderer.h/.cpp` (99+430) — le chef d'orchestre qui tient 1→6
- [ ] 8. `VulkanScenePasses.h/.cpp` (62+350) + `VulkanSceneRenderer.cpp` (27) — les passes
      de rendu de la scène

## Phase 3 — Tracer une frame (c'est ici que les *liens* se comprennent)

La lecture statique donne la structure ; les liens, c'est le chemin d'exécution. Pars de
`Engine::nextFrame()` et suis **une frame complète** dans le code, en notant la chaîne
d'appels sur papier :

- [ ] `nextFrame()` → `beginFrame()` : qui acquiert l'image swapchain ? où est la
      synchro (fences/semaphores) ?
- [ ] Le chemin d'un draw : `SceneRenderer` → `VulkanScenePasses` → command buffer
- [ ] `~Frame()` → `endFrame()` : submit + present
- [ ] Le chemin d'un asset : comment un mesh chargé par `AssetManager` arrive dans un
      buffer GPU (`GPUArena` → `ResourceManager` → `VulkanResources`)
- [ ] Le resize de fenêtre : qui détecte, qui recrée quoi

Test de sortie : tu dois pouvoir dessiner le graphe d'appels d'une frame sans ouvrir les fichiers.

## Phase 4 — Les fichiers retouchés, en diff uniquement (`git diff main...vulkan -- <fichier>`)

Par importance décroissante. Ne relis pas les fichiers entiers, seulement le diff.

- [ ] `src/Engine/Renderer/Renderer.h` — la façade : ce qu'elle expose encore vs avant
- [ ] `src/Engine/Renderer/ResourceManager.h` (−480 lignes) — vidé de son impl DX12,
      comprendre ce qui reste et pourquoi
- [ ] `src/Engine/Utils/GPUArena.h` + `src/Engine/Instance/InstanceManager.h` +
      `instanceDeclaration.h` — les retouches côté données
- [ ] `src/Engine/Assets/AssetLoader.cpp` (90 lignes de diff)
- [ ] `src/Engine/Platform/PlatformWindow.h` + `MacOSWindow.mm` (482, nouveau) +
      `MacOSFileDialog.mm` (120) — la couche fenêtre native mac
- [ ] `src/Engine/InputManager.cpp/.h` — simplifié (−234)
- [ ] Shaders modifiés : `PixelShader.hlsl`, `SkyPS.hlsl`, `VertexShader.hlsl`
- [ ] `CmakeLists.txt` (Engine + Editor) — comment volk/VMA/vk-bootstrap sont branchés

## Phase 5 — Le supprimé (survol, 10 min max)

Juste pour savoir ce qui n'existe plus — ne pas lire le contenu :

- [ ] Survoler `git diff --name-status main...vulkan | grep '^D'` : tout l'ancien
      `Renderer/` (CommandQueue, FenceManager, DescriptorHeapAllocator, RenderGraph,
      Shaders.cpp, ResourceManager.cpp, Renderer.cpp…), les compute shaders DX12,
      `tools/compile-check/`
- [ ] Pour chaque fichier supprimé, savoir répondre : « son rôle est repris par ___ »
      (souvent : VMA, vk-bootstrap, ou un module Vulkan de la phase 2)

## Auto-test final

Si tu sais répondre à tout ça sans ouvrir le code, c'est gagné. Coche par bloc.

### Architecture & décisions

- [ ] 1. Pourquoi volk, vk-bootstrap et VMA plutôt que du Vulkan brut ? Que fait chacun exactement ?
- [ ] 2. Pourquoi avoir gardé les shaders en HLSL au lieu de passer à GLSL ? Quel outil fait HLSL → SPIR-V, et à quel moment (build ? runtime ?) ?
- [ ] 3. Pourquoi `ResourceDescriptorHeap` (le bindless SM 6.6 de DX12) n'était pas utilisable, et par quoi est-il remplacé ?
- [ ] 4. Qu'est-ce que « l'étape A — frontière scellée » a changé concrètement ? Cite deux fonctions de l'API neutre du `ResourceManager`.
- [ ] 5. Pourquoi `ResourceFormat` garde-t-il ses valeurs DXGI alors que DX12 est mort ? Comment la traduction vers Vulkan est-elle faite ?
- [ ] 6. Quelle version minimum de Vulkan est requise, et quelle feature de cette version supprime le besoin de render passes ?

### Contexte & initialisation

- [ ] 7. Dans quel ordre le contexte s'initialise-t-il (instance, surface, device, queues, allocateur) et qui crée quoi ?
- [ ] 8. Où les validation layers sont-elles activées, et dans quels builds ?
- [ ] 9. `Renderer::init` prend un `void*` — c'est quoi, et pourquoi ce type ?

### Ressources & bindless

- [ ] 10. Pourquoi les buffers n'ont-ils pas d'objets « view » dans le design Vulkan, alors que DX12 en avait ?
- [ ] 11. Décris le set bindless : combien de sets, quel type de descripteurs, quels flags le rendent « partially bound » et « update-after-bind » ?
- [ ] 12. Comment une texture obtient-elle son `bindlessIndex`, et comment un shader s'en sert-il (par quel canal l'index arrive-t-il au shader) ?
- [ ] 13. C'est quoi le staging ring ? Pourquoi « mappé zéro-copie » est-il mieux que ce que faisait la version DX12 ?
- [ ] 14. Comment marche la destruction différée ? Pourquoi ne peut-on pas détruire un buffer immédiatement, et qu'est-ce qui décide qu'un slot est libérable ?
- [ ] 15. Quelle est la différence entre `createStaticBuffer` et `createFrameStructuredBuffer` ? Lequel est multiplié par le nombre de frames in-flight, et pourquoi ?
- [ ] 16. Quel trajet fait une texture du PNG sur disque jusqu'à être samplée dans un shader ? (fichiers + fonctions traversés)

### Swapchain & synchronisation

- [ ] 17. Combien d'images dans la swapchain, quel mode de présentation, et pourquoi ce choix ?
- [ ] 18. Sémaphores par image mais fences par frame : pourquoi cette asymétrie ? Que protège chacun ?
- [ ] 19. Que se passe-t-il exactement quand la fenêtre est redimensionnée ? Qui détecte, qu'est-ce qui est recréé, qu'est-ce qui survit ?
- [ ] 20. Où vivent les frames in-flight, combien y en a-t-il, et quelles données sont dupliquées par frame ?
- [ ] 21. Pourquoi le viewport est-il à hauteur négative ? Quelle convention ça préserve ?

### Shaders & pipelines

- [ ] 22. Décris la chaîne complète d'un `.hlsl` jusqu'au `VkPipeline` prêt à dessiner.
- [ ] 23. « Bindings explicites » dans les shaders : ça veut dire quoi concrètement dans le HLSL, par opposition à quoi ?
- [ ] 24. Que contient un pipeline layout ici, et pourquoi les push constants y jouent un rôle central ?
- [ ] 25. Pourquoi les compute shaders (`ComputeShader*.hlsl`) ont-ils été supprimés plutôt que portés ?

### Frame & passes

- [ ] 26. Déroule tout ce qui se passe entre `Engine::nextFrame()` et `~Frame()`, dans l'ordre, avec les objets de synchro aux bons endroits.
- [ ] 27. Qui enregistre les command buffers, et quand sont-ils reset ?
- [ ] 28. Quelles passes `VulkanScenePasses` contient-il, et dans quel ordre s'exécutent-elles ?
- [ ] 29. Comment le clear color / le skybox arrive-t-il à l'écran — quelle passe, quel pipeline ?
- [ ] 30. Le pattern `Frame` RAII : pourquoi le present est-il dans `~Frame()` plutôt que dans une méthode `endFrame()` publique ? Qu'est-ce que ça rend impossible ?

### Assets & données

- [ ] 31. Quel trajet fait un mesh du `.btpl` sur disque jusqu'au draw call ? (AssetLoader → AssetManager → GPUArena → ?)
- [ ] 32. Quel rôle joue `GPUArena` maintenant, et qu'est-ce qui a changé dedans avec la migration ?
- [ ] 33. Que fait `InstanceManager` avec les dirty flags à chaque frame côté GPU ?

### Plateforme

- [ ] 34. Que doit fournir une implémentation de `PlatformWindow.h` ? Cite les responsabilités principales.
- [ ] 35. Comment `MacOSWindow.mm` fournit-il une surface à Vulkan alors que macOS ne parle que Metal ?
- [ ] 36. Comment les events clavier/souris Cocoa arrivent-ils jusqu'à `InputManager` ?
- [ ] 37. Pourquoi le chemin Win32 est-il écrit mais non compilable actuellement, et que faudra-t-il vérifier au retour sur PC ?

### Le supprimé (une réponse par fichier mort)

- [ ] 38. `CommandQueue` / `FenceManager` : repris par quoi ?
- [ ] 39. `DescriptorHeapAllocator` / `Descriptorhandle.h` : repris par quoi ?
- [ ] 40. `RenderGraph.h` : repris par quoi — ou pourquoi plus besoin ?
- [ ] 41. `Shaders.cpp` (DXC → DXIL) : repris par quoi ?
- [ ] 42. `ResourceManager.cpp` (969 lignes) : où est passée chaque responsabilité majeure ?
- [ ] 43. `tools/compile-check/` et ses stubs Windows : pourquoi n'a-t-il plus de raison d'exister ?
