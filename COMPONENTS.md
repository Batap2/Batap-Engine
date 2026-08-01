# Composants : les créer, et comment le système marche

Deux parties : d'abord **l'usage** (ce que tu fais au quotidien, 30 secondes),
ensuite **l'explication de tout ce qui a été construit**, pièce par pièce, avec
à chaque fois un verdict honnête : meilleur choix possible ou pas.

---

## Partie 1 — Ajouter un composant (l'usage)

### Cas normal

```cpp
// MonComposant_C.h
#pragma once
#include "Reflection/ComponentRegistry.h"

namespace batap
{
struct Health_C
{
    float current_ = 100.f;
    float max_ = 100.f;
    bool invulnerable_ = false;
};

BATAP_COMPONENT(Health_C, "health");
}
```

C'est tout. Tu obtiens gratuitement :

- **Sérialisation** : le composant s'écrit/se relit dans les `.btpl` sous
  `"type": "health"`, un champ = une clé json (`current_` → `"current"`,
  le `_` final est retiré).
- **Inspecteur** : le composant apparaît dans l'éditeur avec un widget par
  champ (float → drag, bool → checkbox, `col3` → color picker...).

### Règles

| Règle | Pourquoi |
|---|---|
| La struct doit être un **aggregate** : pas de constructeur, pas de `private`, pas de classe de base | c'est ce qui permet de découvrir les champs automatiquement |
| 16 champs max | limite mécanique, extensible dans `StructFields.h` si besoin |
| Pas de tableau C (`float x[3]`) | utilise les types du moteur (`v3f`...) |
| Une **couleur** = type `col3`, pas `v3f` | le widget et la sérialisation sont choisis par le *type* du champ |

### Types de champ gérés aujourd'hui

`float`, `bool`, `int32_t`, `uint32_t`, `std::string`, `v3f`, `quatf`, `col3`.

Un champ d'un type non géré → **erreur au lancement**, avec le nom exact :
`component 'health' field 'foo' has an unregistered field type`. Le système ne
peut pas se tromper silencieusement.

### Les exceptions (rare)

Réglage UI ponctuel — `fieldMeta<>` :

```cpp
BATAP_COMPONENT(Enemy_C, "enemy",
    fieldMeta<&Enemy_C::aggro_>({.min = 0.f, .max = 1.f}));
```

Composant avec un miroir GPU (le renderer doit être prévenu quand il change) —
`ComponentMeta{.flag}` :

```cpp
BATAP_COMPONENT(PointLight_C, "pointLight", ComponentMeta{.flag = ComponentFlag::PointLight});
```

Composant qui ne peut pas être un aggregate (`Transform_C` : champs private +
matrices à reconstruire) → il reste enregistré à la main dans l'ancien système
(`Serialization/ComponentSerializers.cpp`). C'est assumé.

---

## Partie 2 — Ce qu'on a construit, et si c'est le meilleur choix

Le problème de départ : ajouter un composant demandait d'écrire la liste de ses
champs **4 fois** (la struct, l'écriture json, la lecture json, les widgets
ImGui) dans **4 fichiers du moteur**. Un dev externe ne pouvait donc pas créer
de composant sans modifier le moteur.

L'idée : écrire la liste des champs **une fois**, sous forme de données. La
sérialisation et l'inspecteur deviennent des boucles génériques qui lisent ces
données. Trois étages :

```
StructFields.h        « quels champs a cette struct ? »   (la magie, 150 lignes)
        │
ComponentRegistry     « la table de tous les composants »  (les données)
        │
Consommateurs         serializer + inspecteur               (boucles bêtes)
```

---

### Étage 1 — `src/Engine/Reflection/StructFields.h`

Le C++ ne sait pas nativement énumérer les champs d'une struct (ça arrive avec
la réflexion C++26, pas encore dans les compilos). Ce fichier le fait pour les
aggregates avec trois astuces :

**1. Compter les champs.** Un aggregate accepte l'initialisation `T{a, b, c}`
avec *au plus* N arguments où N = nombre de champs. On teste en boucle à la
compilation : « est-ce que `T{16 args}` compile ? non. 15 ? non. ... 5 ? oui »
→ 5 champs. L'argument est un type factice (`AnyType`) convertible en
n'importe quoi.

**2. Accéder aux champs.** Un `if constexpr` par arité :
`auto& [a, b, c] = x;` puis `std::tie(a, b, c)`. Écrit mécaniquement jusqu'à
16. C'est moche à lire mais trivial : c'est juste le même motif 16 fois.

**3. Le nom des champs.** La vraie magie noire. On prend l'adresse du champ
d'une instance statique comme paramètre template, et clang imprime alors le
*chemin complet du champ* dans `__PRETTY_FUNCTION__` :

```
"... [Ptr = &batap::refl::detail::Probe<batap::PointLight_C>::value.color_]"
```

On parse ce qui suit le dernier `.` → `"color_"` → on retire le `_` →
`"color"`. À la compilation, zéro coût au runtime. **C'est le même mécanisme
que `magic_enum`** (déjà dans le moteur) utilise pour les noms d'enum.

**Meilleur choix possible ?** Le *mécanisme*, oui : c'est exactement ce que
font Boost.PFR, glaze, reflect-cpp — les libs de réflexion C++ de référence.
Il n'existe pas mieux avant C++26, et le jour où C++26 arrive, seul ce fichier
est remplacé, le reste ne bouge pas.
Le fait de l'avoir **écrit nous-mêmes plutôt que vendoriser Boost.PFR** :
défendable, pas incontestable. PFR gère plus de compilos et est maintenu par
d'autres ; notre version fait 150 lignes, une seule cible (clang-cl), zéro
dépendance — c'était ta demande explicite. Le risque (clang change son format
de sortie) est couvert par des `static_assert` dans `ComponentRegistry.cpp` :
si ça casse un jour, c'est une **erreur de build**, jamais des clés json
silencieusement fausses. Si tu changes de compilo un jour, la porte de sortie
est de re-vendoriser PFR derrière la même API.

---

### Étage 2 — `src/Engine/Reflection/ComponentRegistry.h/.cpp`

Des **données**, pas de magie. Trois structs :

- **`FieldType`** — un par type de champ C++ (`float`, `col3`...). Trois
  pointeurs de fonction : `toJson`, `fromJson`, `drawUI`. Les deux premiers
  sont remplis par le moteur au démarrage (`registerBuiltinFieldTypes`), le
  troisième par l'éditeur (`installFieldUI`). Un build jeu sans éditeur
  laisse `drawUI` à null et ne le paie jamais.
- **`Field`** — un champ d'un composant : `{nom, FieldType*, offset}`.
  L'offset suffit : `adresse_du_composant + offset` = adresse du champ.
- **`ComponentType`** — un composant : son nom json, ses champs, et trois
  opérations entt effacées (`tryGet`, `getOrEmplace`, `remove`) générées à
  l'enregistrement.

`BATAP_COMPONENT(T, "name")` s'exécute **au démarrage du programme** (variable
globale `inline` dont l'initialisation appelle `registerComponent<T>`) : il
découvre les champs via l'étage 1 et remplit la table.

Au constructeur d'`Engine`, `validate()` vérifie que chaque champ de chaque
composant enregistré a un `FieldType` sérialisable — sinon erreur nommée.

**Meilleur choix possible ?** L'architecture (table de descripteurs + pointeurs
de fonction), oui — c'est celle de tous les moteurs à inspecteur générique, et
elle est simple : pas de virtuel, pas d'héritage, des données.
Deux choix discutables dedans :
- *L'enregistrement par variable globale* : standard, mais a un piège connu —
  un composant **interne au moteur** dont le header ne serait inclus par aucun
  `.cpp` réellement linké ne s'enregistrerait pas (lib statique → TU éliminé
  au link), sans erreur. Pour les composants du **jeu** (compilés dans l'exe),
  aucun risque. L'alternative (enregistrement manuel dans une fonction
  appelée au boot) est plus sûre mais réintroduit un fichier à modifier par
  composant — exactement ce qu'on voulait tuer. Compromis assumé.
- *`fieldMeta<>` matché par offset* : simple et suffisant. Une alternative par
  index serait plus fragile (réordonner les champs casserait les méta).

---

### Étage 3 — Les consommateurs

**Sérialisation** (`Serialization/EntitySerializer.cpp`) : à la sauvegarde,
pour chaque composant enregistré présent sur l'entité, une boucle écrit chaque
champ dans le json. Au chargement, l'inverse : `getOrEmplace` puis relecture
champ par champ (une clé absente du json = le champ garde sa valeur par
défaut). L'ancien système (`ComponentSerializers.cpp`) tourne **en parallèle**
pour les composants pas encore migrés — les deux écrivent dans le même
format `.btpl`, rien n'a changé sur disque.

**Inspecteur** (`Editor/UI/InspectorPanel.cpp`, `drawReflected`) : pour chaque
composant enregistré présent sur l'entité sélectionnée, un groupe repliable,
une ligne par champ, le widget venant de `FieldType::drawUI`. Si un champ a
changé et que le composant a un `flag` GPU → `markDirty`.

**Meilleur choix possible ?** Oui, sans réserve — c'est la partie triviale, et
c'est le but de tout le système : que la complexité soit concentrée dans les
étages du bas (écrits une fois) pour que tout le reste soit des boucles bêtes.

---

### `col3` — la couleur comme type

`struct col3 : v3f` dans `EigenTypes.h`. Hérite de `v3f` donc tout le code
existant (upload GPU...) marche sans changement. Mais c'est un *type distinct*,
donc la réflexion lui associe son propre widget (color picker) et sa propre
sérialisation — sans métadonnée par champ.

**Meilleur choix possible ? Oui.** C'est le `Color` vs `Vector3` de Unity, le
`FLinearColor` d'Unreal. La sémantique appartient au type, pas à une annotation
— et la struct devient auto-documentée. Si tu vois un `fieldMeta` revenir
partout pour la même raison, c'est le signe qu'un type sémantique manque.

---

### Ce qui n'est PAS géré (choix délibéré)

- **`std::vector`, maps, structs imbriquées** dans les composants → erreur
  nommée au boot. Le point d'extension existe (`FieldType`), on l'implémentera
  au premier composant qui en a réellement besoin. Verdict : bon choix —
  construire ça sans cas d'usage réel, c'est du code spéculatif intestable.
- **`Transform_C`, `Mesh_C` et `Materials_C`** restent dans l'ancien système.
  Forcer `Transform_C` dans le moule (champs private, matrices à reconstruire
  via `Transform_S`) aurait demandé plus de mécanisme que ça n'en économise ;
  `Mesh_C` et `Materials_C` sont produits par l'importeur sans ECS vivant.
  Verdict : bon choix.
- **Portabilité compilateur** : clang-cl uniquement pour les noms de champs.
  Verdict : bon choix *tant que* le projet est mono-compilo — c'est le cas.

---

### État de la migration

| Composant | État |
|---|---|
| `PointLight_C` | ✅ migré, vérifié en runtime (valeurs de scène chargées) |
| `Camera_C` | ✅ migré — gagne au passage un panneau d'inspecteur qu'il n'avait pas |
| `Skybox_C` | ✅ sérialisation migrée ; l'inspecteur garde son panneau (`customEditor`) |
| `Mesh_C`, `Materials_C` | restent à la main — l'importeur les construit **sans ECS** |
| `Transform_C` | reste à la main (champs private, passe par `Transform_S`) |

Les trois handlers restants ne sont pas de la dette : `MeshDecomposer` fabrique
des `EntityDesc` hors de tout registry entt, et la réflexion a besoin d'un
composant vivant (`tryGet`). Les supprimer suppose d'abord de faire écrire
l'importeur dans un monde temporaire — un autre chantier, pas un oubli.

Ce que la migration a ajouté au passage :

- `fieldName` retire aussi un `_` **en tête** (`_znear` → `"znear"`), la
  convention du projet étant mixte. Les clés json sur disque sont inchangées,
  et des `static_assert` dans `Camera_C.h` le figent.
- Les **enums** n'ont rien à enregistrer : `fieldTypeFor<M>()` leur rend le slot
  de leur type sous-jacent (mêmes octets, même json).
- `Serialization/AssetFieldTypes.cpp` enregistre `AssetHandle<T>` pour Mesh,
  Texture et Material : le handle est écrit comme le chemin que l'AssetManager
  lui connaît, et rechargé à la lecture. C'est le seul type de champ qui se sert
  du `const Engine&` de la signature.
- `ComponentMeta::customEditor` : la sérialisation est réfléchie, mais
  l'inspecteur laisse le panneau écrit à la main dessiner le composant. C'est
  ce qui permet de migrer un composant dont l'UI a besoin d'un asset picker,
  sans attendre que `drawUI` sache en faire un.

**Limite connue de `drawUI`** : sa signature est `(void* field, const Field&)`
— aucun contexte. Un champ `AssetHandle<T>` se sérialise donc tout seul mais ne
peut pas s'éditer (il faudrait l'AssetManager pour peupler un picker). C'est
exactement pourquoi `Skybox_C` garde son panneau. Le jour où on veut supprimer
les derniers `drawX`, c'est cette signature qu'il faut élargir.
