# TortoiseGit Qt preview

This is the first native Qt Widgets implementation of the
[macOS port design](../design/macos-qt-port.md). It uses the installed Git executable;
the existing Windows/MFC applications remain available through their Visual Studio solution.

## Build and run on macOS

The initial tested toolchain is Qt **6.11.3**, CMake **4.4.4**, Ninja **1.13.2**,
AppleClang **21**, and the macOS 26 SDK. The deployment target is macOS 14.4.
Runtime testing on older macOS releases and Apple Silicon remains required before release.
Install Apple's Command Line Tools with `xcode-select --install` if needed.

From the repository root:

```sh
bash scripts/qt/bootstrap-macos.sh
bash scripts/qt/build-macos.sh --test --package --run
```

Bootstrap downloads the official Qt binary archives using `aqtinstall` and installs
the build tools in `.tools`; it does not install Homebrew or modify system packages.
These dependencies, build outputs, and local bundles are ignored by Git.
Alternatively, use your own Qt 6.8 or newer installation and set `QT_ROOT` to its
macOS prefix, with CMake and Ninja on `PATH`.

The build executable is `build-qt/TortoiseGitQt.app`. `--package` installs a local
bundle, including Qt frameworks and the askpass helper, into `dist-qt/TortoiseGitQt.app`.
The package step audits Mach-O dependencies/runtime paths and verifies the local
code signature, including the askpass helper.
`--run` opens the built/packaged app on this repository. The bundle is a development
preview; Developer ID signing, notarization, and a public release installer are pending.
An installed Git and its external tools are still required on the destination Mac.

```sh
open dist-qt/TortoiseGitQt.app --args --repository /path/to/working-tree
build-qt/TortoiseGitQt.app/Contents/MacOS/TortoiseGitQt --help
```

`TGIT_BUILD_TYPE`, `TGIT_BUILD_JOBS`, `TGIT_BUILD_DIR`, and `TGIT_DIST_DIR` override
the default Release build, four compilation jobs, and output directories.
For a universal build, configure `CMAKE_OSX_ARCHITECTURES="x86_64;arm64"` directly
with CMake and use universal dependencies. A universal binary alone does not verify
runtime behavior on both architectures.

## Implemented workflows

- Open an existing working tree, initialize a repository, or clone a URL/local repository.
- View porcelain v2 status, tracking branch/ahead/behind counts, conflicts, and literal paths.
- Filter and select changed files; view working-tree and staged diffs, including untracked files.
- Stage selected files or all changes; unstage files in both existing and unborn repositories.
- Discard tracked unstaged edits after confirmation; untracked files are retained.
- Commit the current index with a message; amend with confirmation. Existing hooks, signing
  configuration, and Git credential helpers continue to run through the installed Git.
- View the latest 200 commits and commit patches, refs/tags, reflog, and stash list.
- Create branches/lightweight tags, switch local branches, merge/rebase/cherry-pick/revert,
  and continue/abort interrupted operations using Git's repository state.
- Configure remotes, fetch, fast-forward pull, and push HEAD to an explicitly selected
  remote branch with upstream tracking. Force push is not exposed.
- Save binary-capable staged patches, check/apply patches, stash/apply/pop/drop,
  update submodules recursively, and list/create linked worktrees.
- Blame a selected changed file; edit a conflicted UTF-8 text file and stage the saved result;
  compare two images side by side with zoom and pan.
- Cancel queued/running operations, retain command failures, watch repository metadata,
  and periodically refresh while the app is active. Cancellation is not rollback.

The app discovers Git on `PATH` (or `/usr/bin/git` on macOS). Select another executable
using **Repository → Git executable**. Git's terminal prompt is disabled; the bundled
askpass dialog is available for interactive credentials. SSH agents, HTTPS helpers,
signing tools, LFS filters, and hooks depend on the local Git environment.

Only filenames that round-trip through UTF-8 may be sent back to Git by this UI.
Other path bytes are retained by the parser, displayed as unsupported, and blocked
from file mutation. Bare repositories are outside this working-tree preview.
Diff/output previews are limited to 32 MiB and the conflict editor to 8 MiB.
The conflict editor handles UTF-8 text and preserves CRLF when present; binary,
non-UTF-8, symlink, and delete/type conflicts require an external tool.

## Validation

`ctest --test-dir build-qt --output-on-failure` covers the status parser, real Git
operations, a local bare remote, rejected pushes/hooks, queue recovery, timeouts,
cancellation, and the actual Qt stage/unstage/commit controls using disposable repositories.
The widget tests use Qt's offscreen platform and do not modify user repositories or settings.

Local verification on 2026-10-18: Release build and all three CTest suites passed
on Intel macOS 26.7.1 with Apple Git 2.50.1. The deployed bundle passed the 11-file
Mach-O/signature audit and a native window smoke test while the development Qt
SDK directory was temporarily unavailable. This does not replace testing on a clean
Mac or on Apple Silicon.

For a native window smoke test (requires a graphical macOS session):

```sh
build-qt/TortoiseGitQt.app/Contents/MacOS/TortoiseGitQt \
  --repository /path/to/working-tree --smoke-test /tmp/tgit-window.png
```

The app waits for status/diff loading, saves its window, and exits. Exit codes 2 and 3
mean screenshot failure and load timeout. `--no-settings` disables saved preferences.

## Remaining work

This preview is a usable first implementation, not full Windows feature parity.
The following design work remains open:

- Finder Sync badges/context menus, native IPC/status service, and extension signing.
- Hunk/line staging, interactive rebase, complete revision graph/repository browser,
  three-pane merge tooling, full image/blame/UDiff options and standalone tool automation.
- Complete settings, issue providers/COM replacement, GitWCRev, LFS/git-svn/bisect UI,
  mail/daemon workflows, migration, translations, and accessibility audits.
- Full command/dialog option inventory and Windows regression integration.
- Apple Silicon/older macOS validation, release dependency/license inventory,
  signed/notarized distribution, updates, and diagnostics.

Keep implementation status in the design's milestone table; do not infer parity
from the existence of a menu entry. Dependency information is in
[DEPENDENCIES.md](DEPENDENCIES.md).
