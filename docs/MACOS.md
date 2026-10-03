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

- La ROM española facilitada coincide con el SHA-1 de BPES.
- Las herramientas de la base fijada compilan con Clang en macOS Apple Silicon.
- 15 pruebas Python: operaciones de receta, contenedor, integridad, instalación,
  reconocimiento de regiones y construcción regional con datos sintéticos.
- Pruebas nativas de entrada, vídeo y lectura de paquetes: correctas.
- La receta inglesa oficial se rechaza con la ROM española real.
- Compilación completa 3DS y juego español: **pendientes**. devkitPro no está
  instalado en el equipo de validación; no se afirma que esos pasos hayan pasado.
