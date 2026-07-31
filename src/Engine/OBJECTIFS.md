# Objectifs — rendre le moteur utilisable pour faire un jeu

**Le test** : `testgame/`, un tir sur cibles. Bouger, tirer, toucher, score.

Cible CMake séparée (`Batap_TestGame`), à la racine et non dans `src/` : c'est un *consommateur* du moteur, pas une partie du produit. Il ne voit que les includes `PUBLIC` de `Batap_Engine`, jamais `src/Editor` — donc s'il lui manque quelque chose, il ne peut pas tricher. Pas de branche : le jeu doit compiler en permanence, sinon il ne détecte plus rien.

Arène construite en code. C'est lui qui valide chaque objectif — si le jeu se tape sans lire les internes du moteur, c'est gagné.

Les bugs du moteur (ring d'upload, shutdown, etc.) restent dans `TODO.md`. Piste séparée.

---

## Étape 1 — le jeu de test peut exister

Rien ne se fait avant. Un exe de jeu doit ouvrir une fenêtre, et ce code est dans `Editor/`, qui est un exe.

1. **Couche plateforme dans le moteur.** Déplacer `Editor/WindowsApp.cpp` → `Engine/Platform/Win32/`. Pas de SDL — c'est du déplacement de code qui marche déjà. Un `Window` + une boucle, et un constructeur qui impose l'ordre `renderer->init()` puis `ctx.init()` au lieu de l'espérer.
2. **`testgame/`** — cible exe, linke `Batap_Engine`, ouvre une fenêtre, affiche un cube. C'est le premier consommateur du moteur qui n'est pas l'éditeur.

## Étape 2 — l'API dont le tir a besoin

3. **Input** : `pressed()`, `released()`, `wheel()`. Les données existent (`KeysPressed`/`KeysReleased`), il manque les accesseurs.
4. **`Transform_S`** : `worldPos()`, `forward()`, `right()`, `up()`. Le calcul existe déjà dans `fillCamData`, il n'est pas exposé. `pos()` est locale.
5. **Services atteignables depuis `World`** : `w.transforms()`, `w.input()`, `w.assets()`. Fini `world.systems_->_transforms->`.
6. **`spawnMesh(path)`** + `loadAsset<T>()` typé. Un appel qui charge l'asset, crée l'entité et câble `Kind_C` + pool + `Materials_C`.
7. **Dirty automatique à la construction** (hooks entt `on_construct`/`on_destroy`) + `write<T>()` pour la mutation. Supprime le no-op silencieux — le bug qu'on a trouvé dans ton propre `TestScene`.

## Étape 3 — le gameplay

8. **`raycast(origin, dir, maxDist)`** → `RayHit{entity, position, normal, distance}`. `Bbox.hpp` existe et n'est pas utilisé.
9. **`debug().line(a, b, color)`** — une passe de lignes. Seul morceau renderer. Sans ça, écrire du gameplay se fait à l'aveugle.
10. **Le jeu** : déplacement, tir au clic, cibles touchées qui disparaissent et réapparaissent, score à l'écran.

---

## Décisions prises

| Décision | Pourquoi |
|---|---|
| `EntityHandle` ne change pas (hors bugs DX5) | c'est une clé de `emhash8::HashMap` dans les 3 maps des pools GPU — doit rester 16 o et hashable |
| Pas de transform sur toute entité | `createSkybox`/`createEmpty` n'en ont pas, et `Transform_S` no-op silencieusement. Une API qui prétend le contraire fabrique des bugs muets |
| Services sur `World`, pas de curseur `Entity` | 80 % du confort pour ~0 surface d'API. Un `Entity` fat peut s'ajouter par-dessus plus tard sans rien casser |
| Syntaxe cible : systèmes libres sur requêtes + `write<T>()` | c'est l'état de l'art ECS (Bevy, flecs), et `reg.view<A,B>().each()` t'y met déjà à 80 %. Ton `WriteProxy` est le `Mut<T>` de Bevy |
| Jeu de test dans le dépôt, template plus tard | le test garde le moteur honnête ; le template prouve le chemin « je télécharge et j'utilise ». `FETCHCONTENT_SOURCE_DIR_BATAP` permet d'itérer sur les deux à la fois |
| Arène en code | ne demande rien de neuf. Charger un `.btpl` quand tu voudras tester ce chemin |

## Hors périmètre

DX12/Vulkan, SDL, relogeabilité, dépôt template, éditeur (gizmo, picking, Play/Stop). Aucun effet sur le jeu de test.

## objectif de syntaxe pour lancer le moteur — ATTEINT (voir GameExemple/main.cpp)
int main()
{
    batap::Engine engine{{.title = "My Game", .width = 1280, .height = 720}};
    batap::World  world{engine};              // construit AVEC le moteur, possédé PAR le jeu
    world.loadScene("arena.btpl");

    while (batap::Frame frame = engine.nextFrame())
    {
        if (frame.input().pressed(Key::Space))
            shoot(world, frame.dt());

        world.update();   // LA ligne de simulation — la sauter = pause
    }
}

Décision : `Frame` ne connaît pas `World`. ~Frame ne garantit que l'invariant moteur
(present + clear input). La simulation est explicite dans la boucle : le moteur ne
touche jamais au contenu, et pause/timescale/timestep fixe se grefferont sur cette
ligne sans toucher Frame. Oublier world.update() = scène figée mais présentée
(défaillance visible), pas un hang.