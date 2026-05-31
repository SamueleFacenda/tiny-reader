<a id="readme-top"></a>



<!-- PROJECT SHIELDS -->
[![Contributors][contributors-shield]][contributors-url]
[![Forks][forks-shield]][forks-url]
[![Stargazers][stars-shield]][stars-url]
[![Issues][issues-shield]][issues-url]
[![project_license][license-shield]][license-url]


<!-- PROJECT LOGO -->
<div align="center">
    <a href="https://github.com/SamueleFacenda/tiny-reader">
        <img src="media/handheld.jpg" alt="Logo" width="256" height="172">
    </a>
    <h3 align="center">TinyReader</h3>
    <p align="center">
        TinyReader is a compact, offline-first e-book reader for the CrowPanel ESP32 2.13 E-Paper HMI display.
        <br />
        It reads plain text books from local storage, supports Wi-Fi upload, and is designed for simple low-power use.
        <br />
        <a href="https://github.com/SamueleFacenda/tiny-reader"><strong>Explore the docs »</strong></a>
        <br />
        <br />
        <a href="https://github.com/SamueleFacenda/tiny-reader">View Demo</a>
        &middot;
        <a href="https://github.com/SamueleFacenda/tiny-reader/issues">Report Bug</a>
        &middot;
        <a href="https://github.com/SamueleFacenda/tiny-reader/issues">Request Feature</a>
    </p>
</div>



<!-- TABLE OF CONTENTS -->
<details>
    <summary>Table of Contents</summary>
    <ol>
        <li>
            <a href="#about-the-project">About The Project</a>
            <ul>
                <li><a href="#built-with">Built With</a></li>
            </ul>
        </li>
        <li>
            <a href="#getting-started">Getting Started</a>
            <ul>
                <li><a href="#prerequisites">Prerequisites</a></li>
                <li><a href="#installation">Installation</a></li>
            </ul>
        </li>
        <li><a href="#docs">Docs</a></li>
        <li><a href="#usage">Usage</a></li>
        <li><a href="#roadmap">Roadmap</a></li>
        <li><a href="#contributing">Contributing</a></li>
        <li><a href="#license">License</a></li>
        <li><a href="#contact">Contact</a></li>
        <li><a href="#acknowledgments">Acknowledgments</a></li>
        <li><a href="#star-history">Star History</a></li>
    </ol>
</details>



<!-- ABOUT THE PROJECT -->
## About The Project

<div style="width:40%; margin: auto;">

![e reader ui](media/ui.jpg)
</div>

TinyReader is a small e-book reader built around a CrowPanel ESP32 2.13 E-Paper HMI display. It is intended for reading plain text files, storing books locally, and transferring content over a built-in Wi-Fi access point.

The project is experimental and practical rather than polished. Mechanical fit, battery choice, and power behavior depend on the exact hardware configuration and enclosure you build around it.

<p align="right">(<a href="#readme-top">back to top</a>)</p>



### Built With

* Arduino CLI
* ESP32 platform support through Nix
* LittleFS
* GxEPD2
* Adafruit GFX Library
* ArduinoJson
* OpenSCAD for enclosure work

<p align="right">(<a href="#readme-top">back to top</a>)</p>



<!-- GETTING STARTED -->
## Getting Started

The repository includes a Nix flake, so the Arduino toolchain and project dependencies can be gathered with `nix develop`.

### Prerequisites

* Nix with flakes enabled
* A compatible ESP32 development target
* The CrowPanel ESP32 2.13 E-Paper HMI display
* A suitable LiPo battery

### Installation

1. Clone the repo
     ```sh
     git clone https://github.com/SamueleFacenda/tiny-reader.git
     ```
2. Enter the development shell
     ```sh
     nix develop
     ```
3. Compile the sketch with Arduino CLI
     ```sh
     arduino-cli compile --fqbn esp32:esp32:esp32 tiny_reader.ino
     ```

The flake provides the Arduino CLI, ESP32 board packages, Python serial support, and OpenSCAD.

<p align="right">(<a href="#readme-top">back to top</a>)</p>



## Docs

* [tiny_reader.ino](tiny_reader.ino) is the Arduino entrypoint. It wires the display, input, storage, and Wi-Fi flow together.
* [tiny_reader_2-13_case.scad](tiny_reader_2-13_case.scad) is the OpenSCAD enclosure model.
* The OpenSCAD model references a CrowPanel board STL for fit checking and conversion. The board asset can be converted from the vendor archive at [00-2-13_view_asm.rar](https://github.com/Elecrow-RD/CrowPanel-ESP32-2.13-E-paper-HMI-Display-with-122-250/blob/ca6f62e88c83c108be3904d36e00ded4f55bb68f/3D%20file/00-2-13_view_asm.rar).



<!-- USAGE EXAMPLES -->
## Usage

1. Flash the sketch to the board.
2. Store plain text books on the device or upload them through the Wi-Fi page.
3. Use the physical buttons to navigate the library, reading view, and status screens.
4. Let the device enter deep sleep when idle to preserve battery life.

See the source files for the screen flow and button mapping.

<p align="right">(<a href="#readme-top">back to top</a>)</p>



<!-- ROADMAP -->
## Roadmap

- Improve the library and book navigation flow.
- Refine the Wi-Fi user interface.
- Add a dedicated sleep-screen experience.
- Improve battery status reporting when a usable ADC input is available.
- Update only the necessary parts of the screen during Wi-Fi uptime refreshes.
- Evaluate compressed book storage to reduce flash usage.
- Reduce friction in navigation and return paths between screens.

See the [open issues](https://github.com/SamueleFacenda/tiny-reader/issues) for the current list of planned work.

<p align="right">(<a href="#readme-top">back to top</a>)</p>



<!-- CONTRIBUTING -->
## Contributing

Contributions are welcome. If you have an idea that would make TinyReader better, open an issue or submit a pull request with a clear description of the change.

1. Fork the Project
2. Create your Feature Branch (`git checkout -b feature/AmazingFeature`)
3. Commit your Changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the Branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

<p align="right">(<a href="#readme-top">back to top</a>)</p>



<!-- LICENSE -->
## License

Distributed under the EUPL. See [LICENSE.md](LICENSE.md) for more information.

<p align="right">(<a href="#readme-top">back to top</a>)</p>



<!-- CONTACT -->
## Contact

Project Link: [https://github.com/SamueleFacenda/tiny-reader](https://github.com/SamueleFacenda/tiny-reader)

Use GitHub issues for bugs, feature requests, and build questions.

<p align="right">(<a href="#readme-top">back to top</a>)</p>



<!-- ACKNOWLEDGMENTS -->
## Acknowledgments

* [CrowPanel ESP32 2.13 E-Paper HMI display](https://example.com/tinyreader-crowpanel-esp32-2-13)
* [Nix](https://nixos.org/)
* [Arduino CLI](https://arduino.github.io/arduino-cli/latest/)

<p align="right">(<a href="#readme-top">back to top</a>)</p>



## Star History

[![Star History Chart](https://api.star-history.com/svg?repos=SamueleFacenda/tiny-reader&type=Date)](https://www.star-history.com/#SamueleFacenda/tiny-reader&Date)



<!-- MARKDOWN LINKS & IMAGES -->
[contributors-shield]: https://img.shields.io/github/contributors/SamueleFacenda/tiny-reader.svg?style=for-the-badge
[contributors-url]: https://github.com/SamueleFacenda/tiny-reader/graphs/contributors
[forks-shield]: https://img.shields.io/github/forks/SamueleFacenda/tiny-reader.svg?style=for-the-badge
[forks-url]: https://github.com/SamueleFacenda/tiny-reader/network/members
[stars-shield]: https://img.shields.io/github/stars/SamueleFacenda/tiny-reader.svg?style=for-the-badge
[stars-url]: https://github.com/SamueleFacenda/tiny-reader/stargazers
[issues-shield]: https://img.shields.io/github/issues/SamueleFacenda/tiny-reader.svg?style=for-the-badge
[issues-url]: https://github.com/SamueleFacenda/tiny-reader/issues
[license-shield]: https://img.shields.io/github/license/SamueleFacenda/tiny-reader.svg?style=for-the-badge
[license-url]: https://github.com/SamueleFacenda/tiny-reader/blob/main/LICENSE.md
