{
  description = "3Beans development shell";

  nixConfig.extra-experimental-features = [
    "nix-command"
    "flakes"
    "pipe-operators"
  ];

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs =
    { nixpkgs, ... }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in
    {
      devShells = forAllSystems (
        pkgs:
        let
          # Route PortAudio's ALSA output through the pulse plugin so it reaches
          # PipeWire's PulseAudio server instead of grabbing the hardware directly
          asoundConf = pkgs.writeText "asound.conf" ''
            <${pkgs.alsa-lib}/share/alsa/alsa.conf>
            pcm.!default { type pulse }
            ctl.!default { type pulse }
          '';
        in
        {
          default = pkgs.mkShell {
            packages = with pkgs; [
              gnumake
              pkg-config
              wxGTK33
              portaudio
              libepoxy
              libGL
              alsa-lib
              alsa-plugins
              libpulseaudio
              socat
              gdb
            ];

            shellHook = ''
              export ALSA_CONFIG_PATH=${asoundConf}
              export ALSA_PLUGIN_DIR=${pkgs.alsa-plugins}/lib/alsa-lib
              export PULSE_SERVER=unix:${"\${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"}/pulse/native
            '';
          };
        }
      );

      formatter = forAllSystems (pkgs: pkgs.nixfmt-rfc-style);
    };
}
