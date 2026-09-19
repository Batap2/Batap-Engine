# Livreur de l'espace — fiche du jeu

Ce qu'est le jeu. [OBJECTIFS.md](OBJECTIFS.md) dit quoi construire.

## Le pitch

Tu dois de l'argent à quelqu'un de patient mais pas gentil. Tu as un garage sur
une lune, un buggy pourri, et des gens qui ont besoin qu'on leur apporte des
choses. Tu livres jusqu'à ce que la dette soit éteinte — c'est la fin du jeu.

Tu es un petit délinquant, pas un héros. **La contrebande n'est pas une option,
c'est le jeu.**

## Le système

```
étoile
 ├─ planète A            la plus proche, rien en orbite
 └─ notre planète        la plus lointaine
     ├─ la grande lune   ← ton garage est là
     └─ la petite lune
```

Les deux lunes tournent autour de la planète, pas l'une autour de l'autre.

## Les trois boucles

| | durée | |
|---|---|---|
| **petite** | secondes | piloter — conduire, manœuvrer, se poser |
| **moyenne** | minutes | livrer — charger, voyager, passer les contrôles, décharger |
| **grande** | heures | s'équiper — fusée, capacités, outillage du garage (remplir l'essence plus vite, charger plus vite), et rembourser |

## Les mécaniques

**Piloter pour de vrai.** Gravité newtonienne, orbites, transferts, atterrissage
propulsif. Seule aide : la rotation s'arrête quand on lâche les commandes.

**Une vraie fusée.** Longue, droite, **un seul moteur principal** : pour freiner
il faut se retourner. L'attitude est le gameplay. L'engin est nerveux — TWR
élevé pour toujours pouvoir rattraper une descente, RCS assez fort pour que le
demi-tour prenne une seconde ou deux — et quelques **propulseurs de translation
latérale** permettent de se décaler de deux mètres à l'atterrissage sans
réorienter toute la fusée.

**La progression est en delta-v.** Ce qui bloque n'est jamais une porte, c'est
qu'on n'a pas de quoi repartir.

**L'arrivée est le moment du jeu.** Aucun point de livraison n'est une piste
plate : corniche, fond de cratère, plateforme en pente. Tout se joue dans les
cent derniers mètres.

**On sent la charge.** Les caisses se portent à la main et pendent d'autant plus
bas qu'elles sont lourdes. Rien ne se range tout seul : mal calées, elles se
baladent, déplacent le centre de masse et font partir la fusée de travers.

**Les douanes.** Un patrouilleur peut scanner en transit, un contrôle garde une
planète. Quatre réponses : contourner (coûte du delta-v), se faire discret
(moteur éteint, rasant), cacher (compartiment trop petit), payer. Ce qui se
décide au départ, c'est la méthode, pas la légalité : tout ce qui paie est
illicite. Les courses déclarées n'existent qu'en début de partie, quand on n'a
ni cachette ni delta-v.

**L'air et le carburant se paient.** Une chaîne de réserves emboîtées :
bouteille sur le perso, bouteille sur le buggy, gros réservoir dans le vaisseau,
recharge complète au garage. Des points de ravitaillement dispersés sur les
corps célestes prolongent la chaîne, plus cher que chez soi.
La bouteille se remplit seule dès qu'on entre dans le buggy ou le vaisseau : on
n'y pense que dehors. Au garage, un seul geste remplit tout et sort une facture.
**À zéro dehors, on ne meurt pas** : évanouissement, réveil au garage, contrat
perdu, facture de secours.
La bouteille est dimensionnée pour qu'une livraison normale soit tranquille et
qu'un atterrissage raté ne le soit pas — se poser de l'autre côté d'une crête
transforme la marche en problème. C'est la note de ton pilotage.

**Casser coûte de l'argent, pas la partie.** La pièce touchée est détruite, les
pattes avant les réservoirs. Le remorqueur ramène l'épave contre paiement, à
crédit — se planter loin coûte cher et fait grossir la dette. Aucune perte de
progression, jamais.

**Construire son vaisseau.** Moteurs, réservoir de gaz, réservoir d'oxygène,
soute, structure à pattes, cockpit. Les arbitrages : carburant contre air contre
soute, réservoirs en bas pour descendre le centre de masse, et l'**écartement
des pattes** — c'est lui qui tient une fusée haute sur un sol en pente. Des
pattes larges coûtent cher et prennent de la place.

**Deux classes de vaisseau, et c'est tout.** Le cockpit fixe la classe ; tout le
reste de la pile suit son diamètre. **On ne mélange jamais les deux** — pas
d'adaptateurs, pas de demi-gamme. Passer à la grande classe, c'est changer de
vaisseau, et c'est un palier de progression à part entière.
À l'intérieur d'une classe, la variation est la **longueur** : réservoirs et
soute existeront aussi en version deux fois plus haute. Deux axes propres, la
classe et la longueur.

**Le buggy.** Suffit pour livrer sur sa lune, n'en sortira jamais. Petite soute
où freiner fort envoie les caisses devant, et une bouteille d'air à bord — aller
en buggy et finir à pied devient une vraie décision.

## Ce que le jeu n'est pas

- **Pas un simulateur d'optimisation** — le carburant est généreux.
- **Pas un jeu de combat** — on n'est pas armé.
- **Pas de gestion de base** — le garage s'améliore, il ne se gère pas.
- **Pas d'exploration** — le système est connu dès le début ; ce qui change est
  ce qu'on peut atteindre.

## La direction artistique

**Low-poly à couleurs franches.** Facettes assumées, palette restreinte,
matériaux unis, pas de texture.

**Bricolé se lit dans la silhouette** : un cylindre, un cône, une caisse sanglée
sur le flanc, trois pattes qui ne sont visiblement pas de la même série.

**Une bande de couleur par fonction** : bleu pour l'oxygène, orange pour le
carburant, gris pour le reste. On lit la composition d'un vaisseau de
l'extérieur, d'un coup d'œil, sans UI — y compris la sienne en vol. La couleur
dit la fonction, jamais la marque.

**Chaleureux.** Le contraste entre le dehors hostile et le garage encombré et à
soi est la seule chose que la DA doit réussir. Il s'obtient par la lumière et la
couleur.

**Un monde-bille.** Quelques centaines de mètres de rayon : l'horizon se courbe
à vue, et depuis l'orbite on voit son propre garage.

**Haute et fine**, dans les deux classes : un rapport hauteur/diamètre de 3:1 à
4:1 — la grande fait 3 m de large pour 9 à 12 m de haut. Au-delà, le centre de
masse monte et la fusée se couche sur la moindre pente. Module lunaire, pas
Saturn V.

**Pattes à l'extérieur**, débordant jusqu'à 2.5 m de l'axe : 5 m d'envergure
pour 3 m de fuselage. L'écartement est une amélioration, il se voit de loin.

**Le vaisseau raconte le joueur** — pièces disparates, historique des
réparations. Neuf et assorti = signe de réussite.

**La cargaison est drôle** : faune non déclarée, pièces de réacteur sans
licence, alcool de lune, contrefaçons.

Références : **Outer Wilds** (petit système solaire, vraies orbites, vaisseau
bricolé à pattes externes, low-poly chaleureux), Astroneer (la propreté du
low-poly), Ratchet & Clank 1 (le garage), Euro Truck (la routine), Lunar Lander
(les cent derniers mètres).

## Production

**Modélisé** : réservoir d'oxygène, réservoir de gaz, soute, structure, cockpit,
moteurs — dans les deux classes. Low-poly, même nombre de facettes partout, tous
en solides de révolution donc ils s'emboîtent à n'importe quelle rotation.

**Reste à faire** : les pattes sur la structure, l'ouverture de la soute (sans
elle, ni chargement ni caisses visibles — prévoir 1,5 m utiles pour manipuler
une caisse d'un mètre), et les versions deux fois plus hautes des réservoirs et
de la soute. Puis l'intérieur du garage, les props et le personnage.

Pas de 3D générée par IA pour les pièces : vingt générations donnent vingt
styles, et la fusée est un assemblage.

## Questions ouvertes

**Contrebande générique ou drogue.** Mêmes mécaniques, mais on en transporte
toute la partie : la cargaison donne son ton au jeu entier. La drogue tire plus
sombre que le garage cosy. *Recommandation : générique.*

**Éditeur de pièces libre ou emplacements.** L'éditeur à la KSP peut manger le
projet et oblige à équilibrer contre n'importe quel vaisseau constructible.
*Recommandation : poser les pièces dans l'éditeur du moteur, jouer, décider
ensuite.* Même modèle de données dans les trois cas, rien ne se ferme.

**Combien de pression aux douanes.** Trop peu, le voyage est mort ; trop, c'est
de l'infiltration. À régler en jouant.

**La fin.** Dette éteinte : écran de fin, ou le jeu continue sans pression ?
