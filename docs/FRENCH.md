# Pokémon Émeraude en français

Pokémon Emerald 3Ds Dual Screen fonctionne avec la ROM française propre de
**Pokémon Émeraude (France, BPEF)**, avec ses textes et ses graphiques. La
traduction n'est pas distribuée : elle vient de ta propre ROM, sur ton
ordinateur ou dans ton navigateur.

La PR initiale d'andyst-dev rapporte des essais dans Azahar (émulateur). Une
compilation française locale a aussi été essayée sur une vraie 3DS : seuls le
lancement, l'introduction et les menus montrés ont été confirmés. Ce n'est pas
une validation de toute l'aventure, des combats, des sauvegardes ou de toutes
les astuces. La branche de contribution doit encore être retestée sur console.

## Installer

Comme la version anglaise : le builder web et celui pour Windows acceptent
toutes les ROM prises en charge et choisissent la langue tout seuls. Chaque
release contient les variantes, chacune avec sa recette. Pour la mise à jour
rapide (le 3DSX seul), la page web vérifie ton `emerald3ds.pak` et te propose
`Emerald3DS-fr.3dsx`, à enregistrer sur la carte SD sous le nom
`Emerald3DS.3dsx`.

| Langue | Code | SHA-1 de la ROM propre |
|---|---|---|
| Français | BPEF | `ca666651374d89ca439007bed54d839eb7bd14d0` |
| Anglais | BPEE | `f3ae088181bf583e55daf962a92bb46f4f1d07b7` |
| Espagnol | BPES | `fe1558a3dcb0360ab558969e09b690888b846dd9` |

## Compiler la version française depuis le code

Avec les prérequis du [guide de développement](DEVELOPMENT.md) :

```sh
python tools/bootstrap.py --make --french-rom "/chemin/Pokemon Emeraude.gba" -j8
```

Le bootstrap applique les patchs, compile les outils et les includes générés,
exécute `tools/localize_french.py` avec ta ROM et compile le 3DSX dans
`build/upstream/3ds_port/emerald3ds.3dsx`. Changer de langue (relancer sans
`--french-rom`) réinitialise l'arbre et efface les objets compilés et les
ressources localisées, pour ne pas réutiliser les données d'une autre langue.

Le moteur vérifie la langue avec laquelle il a été compilé : un exécutable
français n'accepte qu'un paquet de données généré depuis BPEF, et un anglais
que depuis BPEE.

## Comment on obtient la version française

`tools/localize_french.py` vérifie le SHA-1 de la ROM et les empreintes SHA-256
des fichiers source avant d'écrire dans l'arbre généré. Les manifestes de
`tools/locales/` contiennent des positions dans le code, des décalages et des
tailles dans la ROM, des noms de symboles et des adresses. Ils ne contiennent
aucun texte, graphique, son ni section de la ROM.

Le processus remplace les textes et les champs du code par ceux de ta ROM,
extrait 99 ressources graphiques et reconstruit les 55 pages de crédits ainsi
que les étages du Mont Dresseurs. Elle extrait aussi les cinq tables de largeur
des polices latines, les ordres alphabétiques du Pokédex et du vocabulaire,
ainsi que les identifiants des phrases prédéfinies et les positions des colonnes
du clavier. Ces valeurs numériques restent dans la ROM de l'utilisateur ; seuls
leurs emplacements sont versionnés.
Y passent les noms, les dialogues, les menus,
le vocabulaire, les chansons du barde, les phrases des dresseurs, les cartes,
les questions et les textes en braille des ruines. L'écran tactile utilise des
libellés français et le Pokédex affiche des mètres et des kilos avec une virgule
décimale et un alignement adapté au français.

Cinq ressources graphiques que la ROM française ne porte pas dans le découpage
plus récent du port restent en anglais : les boutons de l'écran de nommage. Le
manifeste les liste avec leur raison, pour que ce soit une décision et non un
oubli.

Les patchs de langue (`patches/pokeemerald/`, sous `PORT_BRIDGE`) adaptent les
unités du Pokédex, la tilemap de l'écran « FIN », l'ordre des mots des baies et
les fenêtres de l'étiquette de baie, les neuf colonnes du clavier français et
les lignes de noms de types. Le message du partage d'expérience ajouté par le
port a sa propre traduction française. Les branches anglaise et espagnole
gardent leur comportement précédent.

Si un changement du port modifie un fichier source du manifeste, la
localisation s'arrête (« Source differs from the pinned patched tree ») jusqu'à
ce que ses positions et ses empreintes soient mises à jour.

Les menus propres au port sont traduits séparément : RÉGLAGES, AMÉLIOR. et
ASTUCES, leurs options et leurs valeurs. Les libellés sont volontairement courts
pour tenir dans les cellules tactiles. Un adaptateur UTF-8 convertit les accents
et signes pris en charge vers l'encodage de la police du jeu, sans couper un
caractère au milieu ni dépasser le tampon. Il ne distribue aucune image de
caractère. Les libellés anglais et espagnols existants sont conservés.

## Tests sans ROM

```sh
python -m unittest discover -s builder/tests -v
```

Les tests ajoutés utilisent exclusivement des données synthétiques : sélection
et installation des trois variantes, refus d'une variante française absente,
tables numériques à deux colonnes et clavier à neuf colonnes, refus d'une ROM
ou d'un arbre source incorrect avant toute écriture, accents et limites des
tampons. Les tests C de
l'adaptateur nécessitent `cc` ou `gcc` sur le PATH.

## Auteur

Traduction française et outils de localisation d'andyst-dev.
