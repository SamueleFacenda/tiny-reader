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

         esp32Platform = pkgs.arduinoPackages.platforms.esp32.esp32."2.0.10";
         esptool = pkgs.arduinoPackages.tools.esp32.esptool_py."4.5.1";

         # The board carries 8MB of flash, but the bootloader the ESP32 core
         # builds declares 4MB and is SHA-256 protected, so esptool refuses to
         # correct that field while flashing. A partition table reaching past the
         # declared size makes the bootloader reset before it prints anything.
         # Regenerating it from the same ELF the core uses, with the real flash
         # size, is enough: platform.txt prefers a bootloader.bin found in the
         # sketch folder over anything it would build itself.
         # esptool.py imports pyserial even for offline image conversion.
         esptoolPython = pkgs.python3.withPackages (ps: [ ps.pyserial ]);

         bootloader = pkgs.runCommand "tinyreader-bootloader-8mb" { } ''
           ${esptoolPython}/bin/python3 \
             ${esptool}/packages/esp32/tools/esptool_py/4.5.1/esptool.py \
             --chip esp32s3 elf2image \
             --flash_mode dio --flash_freq 80m --flash_size 8MB \
             -o $out \
             ${esp32Platform}/packages/esp32/hardware/esp32/2.0.10/tools/sdk/esp32s3/bin/bootloader_qio_80m.elf
         '';
        in rec {
          legacyPackages = pkgs;
          packages.arduino-cli = arduinoCli;
          packages.bootloader = bootloader;

          devShells.default = pkgs.mkShell {
            packages = with pkgs; [
              arduinoCli
              python3Packages.pyserial
              openscad
            ];

            # Put the 8MB bootloader where the build looks for it. Guarded by the
            # sketch name so entering the shell from a subdirectory cannot
            # scatter symlinks around. Without this file the build quietly falls
            # back to a 4MB bootloader and the device boot-loops in silence.
            shellHook = ''
              if [ -f tiny-reader.ino ]; then
                ln -sf ${bootloader} bootloader.bin
              fi
            '';
          };
       }
     ));
}
