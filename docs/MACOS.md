# Pokémon Esmeralda español y compilador para macOS

Esta rama parte de **v0.1.2** y permite construir el port nativo de 3DS con los
textos y recursos de la ROM española limpia **BPES**. Incluye un builder nativo
para macOS Apple Silicon y un proceso reproducible para compilar el juego.
La distribución `0.1.2-es-dev` es experimental: falta probarla jugando en una
consola física. El payload original v0.1.2 sigue necesitando la ROM inglesa.

## Usar el paquete de Mac

Descomprime `Emerald3DS-v0.1.2-es-dev-macOS-arm64.zip` y abre
`Emerald3DS-Builder.command`. Selecciona tu ROM española y la tarjeta SD, o
crea primero la carpeta de instalación en el Mac.

El paquete contiene el ejecutable del compilador, el motor para 3DS, su receta
y los generadores de escenarios. No necesita instalar Python. Lee la ROM
localmente y no modifica el original ni lo envía a GitHub.

También se puede usar la consola, desde la carpeta descomprimida:

```sh
./emerald3ds-builder-cli build --rom "/ruta/Pokemon Esmeralda.gba" --output salida
./emerald3ds-builder-cli verify --pak salida/3ds/emerald3ds/emerald3ds.pak
```

La carpeta resultante `3ds/emerald3ds` se copia a la raíz de la SD. Ejecuta
`Emerald3DS.3dsx` desde el Homebrew Launcher. No es una ROM de GBA ni un CIA.

## Compilar desde el código en Mac

La opción probada usa Docker Desktop y mantiene las herramientas Linux en un
árbol separado de las herramientas nativas del Mac:

```sh
./tools/docker-build.sh --dir build/spanish-upstream \
  --spanish-rom "/ruta/Pokemon Esmeralda.gba" -j6
```

El script copia temporalmente el archivo de entrada a una carpeta ignorada y
lo elimina al finalizar. El contexto Docker excluye ROMs y archivos generados.
El resultado está en `build/spanish-upstream/3ds_port/emerald3ds.3dsx`.
Para generar la versión del motor destinada al builder:

```sh
docker run --rm --mount "type=bind,source=$PWD,target=/workspace" \
  emerald3ds-build:local make -C build/spanish-upstream/3ds_port release
```

También se puede compilar de forma nativa con Xcode, devkitPro/3ds-dev y las
dependencias de Homebrew:

```sh
brew install python python-tk make pkgconf libpng
./tools/macos.sh setup
./tools/macos.sh build --spanish-rom "/ruta/Pokemon Esmeralda.gba" -j6
```

El script usa Python 3.11 o posterior, GNU Make 4.3 o posterior, Clang para las
herramientas del Mac y devkitARM para la consola. El Make 3.81 de Apple y el
Python 3.9 del sistema no son compatibles. La compilación nativa de las
herramientas de host está comprobada; el motor 3DS se comprobó mediante Docker.

## Cómo se obtiene la versión española

`tools/localize_spanish.py` comprueba la ROM y las huellas de los archivos
fuente antes de escribir en el árbol generado. Los manifiestos de
`tools/locales/` contienen posiciones, tamaños, nombres de símbolos y
direcciones. No contienen textos, gráficos, sonidos ni secciones de ROM.

El proceso realiza 19.333 sustituciones de textos y campos, extrae 104 recursos
gráficos y reconstruye las 55 páginas de créditos y la Colina Desafío. Incluye
los nombres, diálogos, menús, orden del vocabulario, canciones del bardo,
frases de entrenadores, cartas de intercambio y preguntas de Ciudad Calagua.
La pantalla táctil usa etiquetas españolas y la Pokédex muestra metros y kilos.
Los parches de idioma adaptan los nombres de bayas, las descripciones de los
rivales, los títulos de concursos, la pantalla «FIN» y el ancho de una tabla
de textos de combate.

Hay 54 expresiones fuente sin correspondencia directa con BPES, principalmente
recursos no utilizados y eventos externos. La comprobación de símbolos del
motor solo conserva dos de esos nombres, cuyos textos se sustituyen mediante
el parche de bayas y rivales. No se distribuyen datos extraídos de estos
archivos ni las ROMs de referencia.

Cambiar de idioma elimina los objetos compilados y regenera los recursos
localizados. Esto evita reutilizar datos ingleses de una compilación anterior.

## Crear el paquete del builder

Instala las dependencias de investigación y PyInstaller en un entorno local.
Las búsquedas indexadas aceleran la generación de la receta y conservan las
mismas coincidencias que la búsqueda normal:

```sh
python3 -m pip install -r builder/requirements.txt -r tools/requirements-research.txt
```

Ensamblar `build/spanish-reference.s` dentro del árbol generado produce un ELF
con nombres, direcciones y tamaños, sin secciones de ROM. Se usa junto al ELF
del motor y al ELF de datos para crear la receta. `tools/build_release.py`
acepta `--tree build/spanish-upstream` y `--indexed-search`; empaqueta para
macOS, Windows o Linux según el sistema en el que se ejecute PyInstaller.

## Comprobaciones

- Compilaciones inglesa y española completadas con devkitARM en Docker.
- Versión española: 6.237 recursos verificados y 6.245 archivos de datos.
- Receta BPES: todos los archivos reconstruidos con su tamaño y CRC esperado;
  2.878 bytes sin correspondencia directa en objetos del juego, por debajo
  del límite original de 4.096. Los límites de seguridad no se han ampliado.
- 23 pruebas con datos sintéticos pasan, incluida la equivalencia de búsquedas.
- Paquete macOS arm64 compilado y auditado contra la ROM: sin ROM, paquetes de
  datos extraídos, rutas privadas ni secretos dentro del ZIP.
- El ejecutable congelado de Mac reconstruye y verifica el paquete BPES completo:
  6.245 archivos, 18,1 MiB, ABI `741db839`.
- Prueba de juego en consola física: pendiente.

ROMs limpias admitidas, siempre con el payload correspondiente:

| Idioma | Código | SHA-1 |
|---|---|---|
| Español | BPES | fe1558a3dcb0360ab558969e09b690888b846dd9 |
| Inglés | BPEE | f3ae088181bf583e55daf962a92bb46f4f1d07b7 |
