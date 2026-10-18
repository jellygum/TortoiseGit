# Qt preview dependencies

The new code carries `SPDX-License-Identifier: GPL-2.0-or-later` and preserves the
repository's existing license. This change does not relicense the Windows code.

| Dependency | Development version | Use |
| --- | --- | --- |
| Qt Core/Gui/Widgets | 6.11.3 | Process/files/settings, native desktop widgets, image handling |
| Qt Test | 6.11.3 | Build-time integration/widget tests |
| Git | User-installed; `/usr/bin/git` in local verification | Repository operations, credentials/hooks/filters/signing |
| aqtinstall | 3.3.0 | Downloads the official Qt archives during bootstrap |
| CMake | 4.4.4 | Build/install/deployment generation |
| Ninja | 1.13.2 | Compilation |

Qt is dynamically linked. Its module and bundled third-party license conditions,
copyright notices, corresponding sources, and deployment requirements must be
included in the release inventory before publishing a redistributable release.
The local development bundle is not presented as a release-ready distribution.

- [Qt licensing and third-party code](https://doc.qt.io/qt-6/licensing.html)
- [Qt source archives](https://download.qt.io/official_releases/qt/)
- [Qt macOS deployment](https://doc.qt.io/qt-6/macos-deployment.html)
- [aqtinstall source](https://github.com/miurahr/aqtinstall)

The development SDK's `sbom` directory contains dependency metadata. Bootstrap
does not download the existing Windows-only submodules, and the Qt preview does
not link the original MFC, Detours, gitdll, or COM libraries.
