{ pkgs ? import <nixpkgs> { } }:

let
  runtimeLibs = with pkgs; [
    libGL
    libxkbcommon
    wayland
    xorg.libX11
    xorg.libXcursor
    xorg.libXi
    xorg.libXrandr
    xorg.libXinerama
    xorg.libXext
  ];
in
pkgs.mkShell {
  nativeBuildInputs = with pkgs; [
    cmake
    ninja
    pkg-config
    gcc
    gdb
  ];

  buildInputs = with pkgs; [
    glfw
    # Headers that ImGui's GLFW backend picks up for X11/Wayland support.
    xorg.libX11
    xorg.libXrandr
    wayland
  ] ++ runtimeLibs;

  shellHook = ''
    export LD_LIBRARY_PATH="/run/opengl-driver/lib:${pkgs.lib.makeLibraryPath runtimeLibs}''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  '';
}
