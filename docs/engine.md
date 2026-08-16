# Engine

## Architecture

```mermaid
graph TD
    Engine --> World
    Engine --> Input[InputManager]
    Engine --> AM[AssetManager]
    Engine --> IM[InstanceManager]
    Engine --> R[VulkanRenderer]
    Engine --> P[PlatformWindow]

    World --> Scene
    Scene --> ECS["ECS (entt registry)"]
    ECS --> C[Components]
    ECS --> S[Systems]

    AM --> AL[AssetLoader]
    AM --> AD["Mesh / Texture / Material"]

    IM --> Pool["FrameInstancePool (one per instance type)"]

    R --> Ctx[VulkanContext]
    R --> Swap[VulkanSwapchain]
    R --> Pipe[VulkanPipelines]
    R --> Res[VulkanResources]
    R --> Pass[VulkanScenePasses]

    P --> Mac[MacOS]
    P --> Win[Win32]
```

---
## **Frame Life Cycle**

The game loop is `while (Frame frame = engine.nextFrame()) { ...; world.update(); }` — the `Frame` destructor triggers `endFrame()`.

```mermaid
flowchart TD
    NF["Engine::nextFrame()"] --> PM["platformPumpMessages()<br/>OS events → input queue, resize"]
    PM --> BIF["Renderer::beginFrame()<br/>wait frame fence · ResourceManager::beginFrame()<br/>ImGui::NewFrame()"]
    BIF --> DE["InputManager::DispatchEvents()"]
    DE --> GAME["Game / editor code<br/>game logic, ImGui UI<br/>editor: pump async messages (file dialog → asset import)"]
    GAME --> WU

    subgraph WU["World::update()"]
        direction TB
        SU["Scene::update()<br/>game logic hook"] --> SY["Systems::update()<br/>write components + mark dirty"]
        SY --> UD["SceneRenderer::uploadDirty()<br/>dirty components → InstancePatch → staging buffer"]
    end

    WU --> EF["~Frame → Engine::endFrame()"]
    EF --> CS["InputManager::ClearFrameState()"]
    CS --> RR

    subgraph RR["Renderer::render()"]
        direction TB
        AC["acquire swapchain image<br/>out of date → recreate + skip frame"] --> FU["flushUploads()<br/>staged uploads recorded into the command buffer"]
        FU --> REC["ScenePasses::record()<br/>scene draw (dynamic rendering)"]
        REC --> IMG["ImGui::Render()"]
        IMG --> SP["submit + present"]
    end
```
---
## Asset Manager

**Purpose**  

- Manage asset lifetime on CPU  
- Bridge imported data and GPU resources  

**Properties**

- Assets are **data-only**
- Assets are **referenced**, never owned, by instances

**Examples**

- Mesh
- Texture
- Material data

**Import pipeline**

```mermaid
graph LR
    F[File on disk] --> Imp[Importers]
    Imp --> AL[AssetLoader]
    AL --> AM[AssetManager]
    AM --> CPU["CPU asset (data-only)"]
    CPU --> GPU[AssetGPUArena]
    CPU -. referenced by .-> MC[Mesh_C / Materials_C]
```

---

## ECS

**Overview**

- ECS stores **authoritative CPU-side data**
- Components represent state
- Systems implement logic

**Components**

- Stored in `src/Components`
- Ideally **data-only** (small helper logic allowed)
- Source of truth
- **No direct GPU access**
- Mutations must occur inside a **System**
- GPU-relevant writes outside systems must use `Scene::write<Component>()`
- Direct `entt::registry::get / try_get` **bypasses dirty tracking**

**Systems**

- Stored in `src/Systems`
- Ideally **pure logic**
- Read/write components
- Mark components dirty when GPU sync is required

**Rules**

- Systems do **not** access GPU resources
- Systems do **not** own data
- Systems operate only on ECS state

---

## GPU Instances / Instance Manager / Declaration

**Instance Manager**

- Owns multiple `FrameInstancePool<T>`
- One pool per instance type (Camera, Mesh, ...)

**FrameInstancePool**

- One structured GPU buffer + SRV per pool
- Contiguous CPU-side pool
- Entity → Instance mapping
- Supports resize and recycling

### GPU Instance

**Purpose**

- Represent ECS entities in a GPU-friendly format
- Synchronize ECS data to GPU buffers
- Support multi-frame-in-flight rendering

**Defines**

- **Used ECS components**: `ComponentFlag UsedComposents`
- **GPU memory layout**: `GPUData`
- **CPU → GPU fill rules**: `InstancePatch`

**Constraints**

- `GPUData` must be **trivially copyable**
- GPU-compatible alignment
- No pointers or ownership
- No direct GPU access

**ECS → GPU sync**

```mermaid
graph LR
    S[Systems] -- "write" --> C[Components]
    C -- "mark dirty" --> D[DirtyFlag]
    D --> IM[InstanceManager]
    IM -- "InstancePatch" --> GD[GPUData]
    GD --> Pool[FrameInstancePool]
    Pool -- "structured buffer" --> GPU[(GPU)]
```

