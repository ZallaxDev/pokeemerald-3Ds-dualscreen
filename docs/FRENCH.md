# Pokémon Émeraude en français

Pokémon Emerald 3Ds Dual Screen fonctionne avec la ROM française propre de
**Pokémon Émeraude (France, BPEF)**, avec ses textes et ses graphiques. La
traduction n'est pas distribuée : elle vient de ta propre ROM, sur ton
ordinateur ou dans ton navigateur.

La variante française a été jouée dans Azahar (émulateur) ; elle n'a pas encore
été essayée sur console, contrairement aux autres langues.

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
que les étages du Mont Dresseurs. Y passent les noms, les dialogues, les menus,
le vocabulaire, les chansons du barde, les phrases des dresseurs, les cartes,
les questions et les textes en braille des ruines. L'écran tactile utilise des
libellés français et le Pokédex affiche des mètres et des kilos.

Cinq ressources graphiques que la ROM française ne porte pas dans le découpage
plus récent du port restent en anglais : les boutons de l'écran de nommage. Le
manifeste les liste avec leur raison, pour que ce soit une décision et non un
oubli.

Les patchs de langue (`patches/pokeemerald/`, sous `PORT_BRIDGE`) adaptent les
unités du Pokédex, la tilemap de l'écran « FIN », l'ordre des mots des baies et
les fenêtres de l'étiquette de baie. Dans la compilation anglaise, ils ne
changent rien.

Si un changement du port modifie un fichier source du manifeste, la
localisation s'arrête (« Source differs from the pinned patched tree ») jusqu'à
ce que ses positions et ses empreintes soient mises à jour.

Les écrans ajoutés après la traduction gardent des libellés anglais : les
onglets d'OPTIONS (SETTINGS, ENHANCEMENTS, CHEATS) et les valeurs des astuces
(OFF, FEW, SOME, MANY, NORMAL, 1/4, 2X...). Le reste de l'écran tactile est en
français.

## Auteur

Traduction française et outils de localisation d'andyst-dev.
