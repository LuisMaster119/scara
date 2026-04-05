# SCARA IDE (SDL2 + Dear ImGui)

Este modulo crea un ejecutable IDE separado del compilador principal.
No modifica el flujo actual de `programasCOMPI/scara.exe`.

## Objetivo del MVP

- Barra superior con menus (Archivo, Tema, Fuente, Ayuda)
- Barra breadcrumb de ruta de archivo
- Panel lateral de proyectos (colapsable)
- Tabs de archivos abiertos
- Editor principal del archivo seleccionado
- Consola inferior para salida del compilador/VM
- Panel de visualizacion integrado (placeholder inicial)

## Dependencias

1. SDL2 (ya disponible en tu entorno MSYS2)
2. Dear ImGui (fuentes y backends SDL2 + SDLRenderer2)

Se espera este arbol dentro del repo:

- `third_party/imgui/imgui.h`
- `third_party/imgui/imgui.cpp`
- `third_party/imgui/imgui_draw.cpp`
- `third_party/imgui/imgui_widgets.cpp`
- `third_party/imgui/imgui_tables.cpp`
- `third_party/imgui/backends/imgui_impl_sdl2.h`
- `third_party/imgui/backends/imgui_impl_sdlrenderer2.h`
- y sus `.cpp` equivalentes.

## Compilar en PowerShell (MSYS2/MinGW)

Desde `scara/ide`:

```powershell
g++ .\src\ide_main.cpp `
  ..\third_party\imgui\imgui.cpp `
  ..\third_party\imgui\imgui_draw.cpp `
  ..\third_party\imgui\imgui_tables.cpp `
  ..\third_party\imgui\imgui_widgets.cpp `
  ..\third_party\imgui\misc\cpp\imgui_stdlib.cpp `
  ..\third_party\imgui\backends\imgui_impl_sdl2.cpp `
  ..\third_party\imgui\backends\imgui_impl_sdlrenderer2.cpp `
  -I..\third_party\imgui `
  -I..\third_party\imgui\backends `
  -IC:\msys64\mingw64\include\SDL2 `
  -LC:\msys64\mingw64\lib `
   -o scara_ide.exe -lmingw32 -lSDL2main -lSDL2 -lcomdlg32
```

## Ejecutar

```powershell
.\scara_ide.exe
```

## Estado

Cascaron inicial de interfaz listo para iterar.

## Funciones ya conectadas

1. Archivo -> Abrir
- Abre un modal para ingresar la ruta completa del archivo `.scara`.
- Crea una nueva pestana en el editor o activa la existente si ya estaba abierta.

2. Ejecutar archivo seleccionado
- Desde barra superior: `Ejecutar archivo`.
- Desde panel visual: boton `Ejecutar archivo seleccionado`.
- Ejecuta `..\\programasCOMPI\\scara.exe <archivo>` y captura toda la salida.
- La salida se muestra en la consola inferior integrada.

3. Breadcrumb dinamico
- La barra de ruta se actualiza con la ruta real del archivo activo.

4. Estado de archivo modificado
- Las pestanas muestran `*` cuando hay cambios sin guardar.
- Guardar limpia el indicador de cambios.

5. Explorador lateral con filtros y busqueda
- Busqueda por nombre de archivo.
- Filtros por extension (.scara, .txt, codigo C/C++, .md).

6. Vista rapida SCARA
- Panel auxiliar con resaltado basico de keywords/comentarios para lectura rapida.

7. Diagnosticos desde salida de compilador
- La consola parsea errores reportados con numero de linea.
- Panel de diagnosticos clicable para abrir archivo y enfocar linea en la vista rapida.
- Util para navegar rapido cuando hay errores lexico/sintactico/semantico.

8. Busqueda y estado del editor
- `Ctrl+F` abre barra de busqueda en archivo activo.
- `F3` navega a la siguiente coincidencia.
- Botones `Prev/Next` para recorrer coincidencias.
- Barra de estado con nombre de archivo, linea/columna y estado guardado/sin guardar.

9. Navegacion y productividad extra
- `Ctrl+G` abre barra "Ir a linea" y enfoca la linea indicada en la vista rapida.
- Autocompletado basico de keywords SCARA (sugerencias contextuales segun prefijo).
- El explorador lateral marca con `*` los archivos con cambios sin guardar.

10. Navegacion semantica local
- `F12`: ir a definicion local para simbolos `VAR`/`POINT`.
- `Shift+F12`: buscar referencias locales del simbolo.
- Barra de referencias con `Prev/Next` y contador.

11. Panel de simbolos locales
- Panel con lista de `VAR`/`POINT` del archivo activo.
- Filtro por texto y salto por clic a la linea del simbolo.
- `Ctrl+Shift+O` para mostrar/ocultar panel.

12. Panel de Problemas enriquecido
- Filtros por severidad (`E/W/I`).
- Filtro de texto por archivo/severidad/mensaje/linea.
- Orden por `Archivo/Linea`, `Severidad` o `Mensaje`.

13. Info semantica en cursor
- Barra contextual bajo el editor con:
  simbolo actual, tipo, linea de definicion y cantidad de usos.
- Boton directo para saltar a definicion.

14. Persistencia de estado UI
- El IDE guarda/restaura estado en `scara_ide_ui.cfg` junto al ejecutable.
- Persistencia actual:
  - visibilidad de paneles principales,
  - filtros de explorador,
  - filtros/orden de Problemas,
  - estado de panel/filtro de simbolos,
  - visibilidad de barra semantica.

## Historial Por Bloques

### IDE-0 - Base
- Shell principal SDL2 + ImGui con layout tipo IDE.

### IDE-1 - Flujo archivo/ejecucion
- Abrir, guardar y ejecutar archivo activo contra `scara.exe`.

### IDE-2 - Explorador
- Arbol de proyecto con filtros y dirty markers.

### IDE-3 - Productividad
- Buscar (`Ctrl+F`), siguiente (`F3`), ir a linea (`Ctrl+G`), status bar.

### IDE-4 - Diagnosticos
- Parseo de errores por linea y navegacion desde panel.

### IDE-5 - Navegacion semantica
- Definicion/referencias locales (`F12` / `Shift+F12`).

### IDE-6 - Simbolos
- Panel de simbolos locales con filtro y salto.

### IDE-7 - Problemas avanzado
- Filtro de texto, severidad y orden de lista.

### IDE-8 - Persistencia UI
- Restauracion de paneles y filtros al reabrir el IDE.

## Estado Actual

- Build del IDE en verde.
- Arranque estable en smoke tests recientes.
- Flujo end-to-end operativo:
  abrir -> editar -> guardar -> ejecutar -> diagnosticar -> navegar.

## Roadmap (Bloques Futuros)

1. Resaltado sintactico real en editor principal
- Keywords, comentarios, strings y numeros en el editor editable.

2. Navegacion avanzada por referencias/problemas
- Doble click/Enter para saltos rapidos y navegacion secuencial de usos.

3. Integracion visual embebida real
- Sustituir placeholder del panel visual por reproduccion integrada.

4. Restauracion de sesion de trabajo
- Reabrir tabs de la sesion previa y recordar tab activo.

5. Persistencia avanzada de posicion
- Cursor y scroll por archivo (MVP).

6. QA automatizado de regresion IDE
- Script de smoke tests para build/arranque/flujo minimo.

## Criterio de Cierre del IDE (MVP+)

- Flujo diario estable para editar y ejecutar SCARA.
- Navegacion semantica local funcional y rapida.
- Panel de Problemas util para depuracion iterativa.
- Integracion visual embebida operativa y sin regresiones de compilador.
