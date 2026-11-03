# Third-party dependencies

DonnerLive project source is licensed under AGPL-3.0-only. Dependency source retains its own notices and licenses.

| Dependency | Version | License used by this project | Acquisition |
| --- | --- | --- | --- |
| JUCE | 9.0.4 | GNU AGPL version 3 | CMake downloads the pinned upstream release archive |
| Steinberg ASIO SDK | 2.3.4 (2025-10-15 archive) | GNU GPL version 3 alternative | CMake downloads the pinned Steinberg SDK archive |

Download URLs and SHA256 checksums are defined in the root `CMakeLists.txt`. Dependency sources are cached
under `build/_deps` and are not vendored in this repository. The installer contains the compiled application,
ASIO DLL, and required license notices; it does not install dependency source trees.

JUCE 9 provides an AGPLv3 option alongside its commercial license. This project uses the AGPLv3 option.
JUCE's dependency inventory and additional notices are in its `LICENSE.md`, `JUCE.spdx.json`, and module source headers.
Keep these notices with source and binary distributions. The build copies the JUCE license, dependency
inventory, and license/readme/provenance files from the linked modules to `licenses/JUCE`. These include
the notices for zlib, HarfBuzz, SheenBidi, LunaSVG, PlutoVG, libpng, IJG JPEG, and libwebp. The original
dependency headers retain any additional notices when distributing the corresponding source.

The pinned ASIO SDK offers a GPL version 3 alternative in `LICENSE.txt`. Use that alternative for this AGPL
project. GPLv3 and AGPLv3 explicitly permit combined works under their respective section 13 provisions.
The build copies the SDK notice and the root `LICENSE-GPL-3.0` text to `licenses/ASIO`. An SDK without this GPLv3 alternative must be reviewed
before it replaces the dependency.

Official references:

- [JUCE licensing](https://juce.com/get-juce/)
- [GNU Affero GPL version 3](https://www.gnu.org/licenses/agpl-3.0.html)
- [GPL/AGPL compatibility](https://www.gnu.org/licenses/gpl-faq.html#v3Compatibility)
- [Steinberg developer resources](https://www.steinberg.net/developers/)

Donner and Elgato/Wave Link drivers are separately installed runtime prerequisites. They are not included
with the project, relicensed by it, or modified by its build. Their names identify
supported devices and drivers; they do not imply endorsement. Obtain drivers under their vendors' terms.
