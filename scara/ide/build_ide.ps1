Set-Location $PSScriptRoot

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
  -o .\scara_ide.exe -lmingw32 -lSDL2main -lSDL2 -lcomdlg32

if ($LASTEXITCODE -ne 0) {
  Write-Output "Build failed."
  exit $LASTEXITCODE
}

Write-Output "Build OK: .\scara_ide.exe"
