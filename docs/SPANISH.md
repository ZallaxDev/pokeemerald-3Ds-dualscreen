# Pokémon Esmeralda en español

Pokémon Emerald 3Ds Dual Screen funciona con la ROM española limpia de
**Pokémon Esmeralda (España, BPES)**, con sus textos y gráficos. La traducción
no se distribuye: sale de tu propia ROM, en tu ordenador o en tu navegador.

Probado en una 3DS física con las funciones actuales (6 de octubre de 2026).

## Instalar

Igual que la versión inglesa: el builder web y el de Windows aceptan las dos
ROM y eligen solos el idioma. Cada release incluye las dos variantes: el
ejecutable inglés y el español, cada uno con su receta. Para la actualización
rápida (solo el 3DSX), la web comprueba tu `emerald3ds.pak` y te ofrece
`Emerald3DS-es.3dsx`, que se guarda en la SD como `Emerald3DS.3dsx`.

| Idioma | Código | SHA-1 de la ROM limpia |
|---|---|---|
| Español | BPES | `fe1558a3dcb0360ab558969e09b690888b846dd9` |
| Inglés | BPEE | `f3ae088181bf583e55daf962a92bb46f4f1d07b7` |

## Compilar la versión española desde el código

Con los requisitos de la [guía de desarrollo](DEVELOPMENT.md):

```sh
python tools/bootstrap.py --make --spanish-rom "/ruta/Pokemon Esmeralda.gba" -j8
```

El bootstrap aplica los parches, compila las herramientas y los includes
generados, ejecuta `tools/localize_spanish.py` con tu ROM y compila el 3DSX en
`build/upstream/3ds_port/emerald3ds.3dsx`. Cambiar de idioma (volver a
ejecutar sin `--spanish-rom`) reinicia el árbol y borra los objetos compilados
y los recursos localizados, para no reutilizar datos del otro idioma.

El motor comprueba el idioma con el que se compiló: un ejecutable español solo
acepta un paquete de datos generado desde BPES, y uno inglés solo desde BPEE.

## Cómo se obtiene la versión española

`tools/localize_spanish.py` comprueba el SHA-1 de la ROM y las huellas SHA-256
de los archivos fuente antes de escribir en el árbol generado. Los manifiestos
de `tools/locales/` contienen posiciones en el código, desplazamientos y
tamaños en la ROM, nombres de símbolos y direcciones. No contienen textos,
gráficos, sonidos ni secciones de la ROM.

El proceso sustituye los textos y campos del código por los de tu ROM, extrae
104 recursos gráficos y reconstruye las 55 páginas de créditos y los pisos de
la Colina Desafío. Incluye nombres, diálogos, menús, vocabulario, canciones
del bardo, frases de entrenadores, cartas, preguntas y los textos en braille de
las ruinas. La pantalla táctil usa etiquetas españolas y la Pokédex muestra
metros y kilos.

Los parches de idioma (`patches/pokeemerald/0030-…` a `0036-…`, todos bajo
`PORT_BRIDGE`) copian por nombre las tablas de la Colina Desafío y adaptan los
nombres de bayas, las descripciones de los rivales, los títulos de concursos,
la pantalla «FIN», el ancho de una tabla de textos de combate, las unidades
de la Pokédex y las ventanas de la etiqueta de bayas. En la compilación inglesa no cambian nada salvo la copia de la
Colina Desafío, que ya no depende del orden en que el enlazador coloca las
tablas.

Si un cambio del port modifica alguno de los archivos fuente del manifiesto,
la localización se detiene («Source differs from the pinned patched tree»)
hasta que se actualicen sus posiciones y huellas. `tools/repin_locale.py`
las actualiza a partir del árbol anterior y del nuevo: mueve cada edición con
las líneas que la rodean y se detiene si el cambio toca la línea de una
edición (esa necesita una correspondencia nueva a mano).

Las funciones añadidas después de la traducción (ajustes y trucos nuevos de
la pantalla táctil, por ejemplo) pueden mostrar todavía textos en inglés.
Los mensajes de los Pokémon que te siguen (FOLLOWERS) tienen su propia
traducción en `src/data/text/follower_messages_es.h` y
`follower_helper_es.h` (parche `0038`); las correcciones son bienvenidas.

## Autoría

Traducción y herramientas de localización de Jesus Oliva
([pull request #6](https://github.com/ZallaxDev/pokeemerald-3Ds-dualscreen/pull/6)).
