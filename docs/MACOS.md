# macOS y ROM española: estado de esta adaptación

Esta rama parte de la etiqueta **v0.1.2**. La adaptación no está terminada:
se reconoce la ROM española limpia y se prepara el builder para macOS, pero
**todavía no existe un payload español jugable**.

## Builder en macOS

Requisitos: Python 3.11 o posterior, Pillow y, para la ventana, Tk de la misma
versión de Python. El Python 3.9 incluido en algunos macOS no sirve para los
scripts del proyecto que importan `tomllib`.

Con Homebrew:

```sh
brew install python python-tk
./tools/macos.sh setup
```

Después abre `Emerald3DS-Builder.command`, o utiliza la consola:

```sh
./tools/macos.sh builder --payload /ruta/al/payload build \
  --rom "/ruta/Pokemon Esmeralda.gba" --output /ruta/salida
```

`--payload` va antes de `build` o `install`. El payload contiene
`emerald3ds.recipe`, el ejecutable 3DS y los generadores. La carpeta no se obtiene
compilando únicamente el builder. El payload de la descarga original v0.1.2
sirve exclusivamente para la ROM inglesa.

El script busca un Python compatible en el entorno local y en las ubicaciones
habituales de Homebrew (Apple Silicon e Intel). `PYTHON` permite escoger otro.
La ROM se lee localmente; no se modifica ni se sube a GitHub.

## Compilar el código para Nintendo 3DS

Instala las herramientas de Xcode y las dependencias de Homebrew:

```sh
xcode-select --install
brew install make pkgconf libpng
```

Instala devkitPro pacman con su [instalador oficial](https://github.com/devkitPro/pacman/releases).
Después, desde Terminal:

```sh
sudo dkp-pacman -Syu
sudo dkp-pacman -S 3ds-dev
./tools/macos.sh setup
./tools/macos.sh build -j4
```

El script selecciona **GNU Make 4.3+** (`gmake`), porque el Make 3.81 de Apple
no entiende las reglas de objetivos agrupados de `3ds_port/full.mk`. Usa Clang
para las herramientas del Mac y devkitARM para el ejecutable de la consola.
Comprueba las dependencias antes de descargar la base o iniciar la compilación.
Se pueden cambiar `DEVKITPRO` y `DEVKITARM` para instalaciones alternativas.

Para compilar solo las herramientas del Mac, sin devkitARM:

```sh
./tools/macos.sh tools
./tools/macos.sh check
```

El empaquetador `tools/build_release.py` ya no etiqueta todas las salidas como
Windows. Ejecutado en el árbol preparado (`build/upstream/tools/build_release.py`),
produce una carpeta y ZIP macOS-arm64 o macOS-x86_64 según el equipo. PyInstaller
se ejecuta en ese mismo sistema; no convierte un .exe de Windows. Usa `--make gmake`
al preparar una release en macOS y un Python con Pillow/PyInstaller.

## Por qué reconocer BPES no basta

ROMs reconocidas por huella completa de 16 MiB (también ZIP y dumps recortados
con relleno final 0xFF):

| Región | Código | SHA-1 |
|---|---|---|
| Inglés | BPEE | f3ae088181bf583e55daf962a92bb46f4f1d07b7 |
| España | BPES | fe1558a3dcb0360ab558969e09b690888b846dd9 |

Las huellas se contrastaron con el [catálogo No-Intro de libretro](https://github.com/libretro/libretro-database/blob/master/metadat/no-intro/Nintendo%20-%20Game%20Boy%20Advance.dat).

El port no emula el cartucho: compila el código descompilado y extrae datos
mediante una receta. La base fijada en `upstream.lock` es inglesa. Los textos,
scripts, direcciones, símbolos y comprobaciones de los datos forman parte de
la compatibilidad entre el ejecutable y el paquete.

Para completar BPES queda pendiente:

1. Adaptar los datos y scripts a la ROM española, con un mapa de símbolos que
   corresponda a esa ROM y una construcción localizada del port.
2. Generar su receta contra los ELF de GBA, 3DS e imagen correspondientes.
   `gen_recipe.py` reconoce BPES y exige esos archivos; conserva los límites de
   literales y la validación de cada entrada.
3. Reconstruir el paquete completo desde la ROM real y comprobar todos sus CRC,
   el ABI, los generadores de escenarios y el resultado en consola.

El builder rechaza un payload de otro idioma antes de escribir el paquete. No
se han eliminado las comprobaciones para aparentar compatibilidad.

## Validación realizada

- Las ROMs española e inglesa facilitadas coinciden con sus SHA-1 de BPES y BPEE.
- Las herramientas de la base fijada compilan con Clang en macOS Apple Silicon.
- 16 pruebas Python: operaciones de receta, contenedor, integridad, instalación,
  reconocimiento de regiones y construcción regional con datos sintéticos.
- Pruebas nativas de entrada, vídeo y lectura de paquetes: correctas.
- La receta inglesa oficial se rechaza con la ROM española real.
- El builder nativo macOS-arm64 genera y verifica los 6.368 archivos del paquete
  inglés completo (18,1 MiB), incluidos los escenarios voxel, con la ROM BPEE.
- Compilación completa del código 3DS: **correcta desde macOS con Docker**,
  incluida la verificación de 6.236 recursos y la generación de `emerald3ds.3dsx`.
  Se corrigieron dependencias de imágenes/paletas en el arranque desde cero y dos
  expresiones incompatibles con Python 3.11. El flujo nativo con devkitPro
  instalado directamente en macOS no se ha validado.
- Juego completo en español y prueba física en consola: **pendientes**.

## Alternativa con Docker Desktop

Si no quieres instalar devkitPro en macOS, con Docker Desktop abierto:

```sh
./tools/docker-build.sh -j6
```

Utiliza la imagen oficial `devkitpro/devkitarm` y añade libpng/Pillow. Prepara
otro árbol (`build/upstream-docker`) para no intentar ejecutar en Linux las
herramientas compiladas para macOS. La compilación se inicia desde el Mac;
el builder de ventana/consola sigue siendo un ejecutable nativo de macOS.
No se monta la carpeta de tus ROMs en el contenedor.

## Comparación de ROMs

`tools/compare_roms.py` compara rangos largos de copia de la receta inglesa
contra ambas ROMs. Guarda únicamente rutas de datos, direcciones, longitudes y
contadores; no guarda bytes de las ROMs. Una coincidencia de bytes no identifica
por sí sola un símbolo traducido ni prueba que un script sea equivalente.

```sh
.venv/bin/python tools/compare_roms.py \
  --english /ruta/inglesa.gba --spanish /ruta/espanola.gba \
  --recipe /ruta/payload/emerald3ds.recipe --output build/comparison.json
```


La comparación de las ROMs reales encontró 753.599 bytes de operaciones de
copia largas de scripts que no aparecen tal cual en BPES; también faltan
193.765 bytes de esas operaciones de datos generales y 256.276 de gráficos.
Estos contadores incluyen repeticiones de rangos usados por la receta; no
son el tamaño de un parche ni una medida del porcentaje traducido. Las
coincidencias de mapas y sonidos ayudan a la investigación, pero no resuelven
la localización de símbolos y punteros del juego español.

Se revisaron dos repositorios de referencia españoles: uno no contiene código
y el otro amplía el motor/especies y no reproduce la ROM BPES oficial. No se
han incorporado como base del port ni se da su traducción por equivalente a la
ROM española. El objetivo solicitado sigue siendo el juego completo en español.

## Referencias del compilador para la localización

La investigación dispone de una compilación GBA de referencia que reproduce
exactamente la ROM BPEE. Al enlazarla con `--emit-relocs`, el compilador conserva
las posiciones de los punteros y las llamadas reales. Esto evita confundir
bytes de audio o gráficos con referencias a diálogos.

`tools/gba_references.py` comprueba que las secciones de la compilación coinciden
con la ROM inglesa y exporta únicamente direcciones y tipos de referencias.
Rechaza ejecutables distintos y archivos sin la información del enlazador.
La dependencia de desarrollo se instala con:

```sh
.venv/bin/python -m pip install -r tools/requirements-research.txt
.venv/bin/python tools/gba_references.py \
  --elf build/gba-reference/pokeemerald.elf \
  --english /ruta/inglesa.gba --output build/reference-graph.json
```

Este informe es una herramienta de investigación, no una receta BPES. La
versión española sigue pendiente de revisar los textos y recursos restantes,
generar su receta y comprobar el paquete resultante con el builder.

El parche `0006-trainer-hill-table-layout.patch` elimina otra dependencia del
compilador antiguo: copia las tablas de la Colina Desafío por sus nombres,
en lugar de asumir que el enlazador las coloca una detrás de otra. Así, el
compilador moderno conserva los datos de las plantas que necesita el juego.

La pantalla táctil selecciona sus etiquetas mediante `GAME_LANGUAGE`. En
español muestra los controles de combate, mapa, estadísticas y opciones en
español, y la ficha de la Pokédex usa metros y kilos con separador decimal
español. El parche `0007` adapta también el orden de los nombres de bayas y
separa las descripciones de los dos rivales.

Existe una compilación local de investigación con 17.458 textos relacionados
con BPES, gráficos localizados y las 55 páginas de créditos de esa edición.
Se han actualizado también la ordenación del vocabulario y las frases de los
entrenadores. Esto todavía no constituye una distribución española validada:
quedan recursos por revisar y falta completar la receta y su comprobación
contra una ROM BPES limpia. Los archivos extraídos permanecen en `build/`,
fuera del repositorio y de la distribución.
