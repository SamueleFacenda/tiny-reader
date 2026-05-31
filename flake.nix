{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    flake-utils.url = "github:numtide/flake-utils";
    arduino-nix.url = "github:bouk/arduino-nix";
    arduino-index = {
      url = "github:bouk/arduino-indexes";
      flake = false;
    };
  };

  outputs = {
    self,
    nixpkgs,
    flake-utils,
    arduino-nix,
    arduino-index,
    ...
  }@attrs:
  let
    overlays = [
      (arduino-nix.overlay)
      (arduino-nix.mkArduinoPackageOverlay (arduino-index + "/index/package_index.json"))
      (arduino-nix.mkArduinoLibraryOverlay (arduino-index + "/index/library_index.json"))
    ];
  in
   (flake-utils.lib.eachDefaultSystem (system:
       let
         pkgs = (import nixpkgs) {
           inherit system overlays;
         };
         arduinoCli = pkgs.wrapArduinoCLI {
            libraries = with pkgs.arduinoLibraries; [
              (arduino-nix.latestVersion EPD)
              (arduino-nix.latestVersion CRC32)
              (arduino-nix.latestVersion pkgs.arduinoLibraries."Adafruit GFX Library")
              (arduino-nix.latestVersion pkgs.arduinoLibraries."Adafruit BusIO")
              (arduino-nix.latestVersion GxEPD2)
              (arduino-nix.latestVersion ArduinoJson)
            ];

           packages = with pkgs.arduinoPackages; [
             platforms.esp32.esp32."2.0.10"
           ];
         };
        in rec {
          legacyPackages = pkgs;
          packages.arduino-cli = arduinoCli;

          devShells.default = pkgs.mkShell {
            packages = with pkgs; [ 
              arduinoCli 
              python3Packages.pyserial 
              openscad 
            ];
          };
       }
     ));
}
