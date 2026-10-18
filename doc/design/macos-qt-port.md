# TortoiseGit macOS / Qt 이식 설계

작성일: 2026-10-18. 조사 기준: `7338078f8`.

상태: **전체 기능 이식을 위한 설계와 초기 Qt 구현**.
Qt 빌드 대상과 macOS 앱의 첫 구현을 추가했다. 현재 지원 기능과 빌드 절차는
[Qt 구현 안내](../qt/README.md)를 참조한다. 전체 기능 이식은 진행 중이다.
사용자 결정은 Qt 기반 UI, 전체 기능의 이식 범위 설계, `qt` 브랜치에서의 작업이다.

### 초기 구현 현황

- Qt 6.11.3 / CMake / Ninja, C++20, 배포 대상 macOS 14.4로 구성했다.
- `src/portable/domain`의 상태 파서와 `src/portable/adapters`의 비동기 Git CLI 계층을
  `src/qt`의 Qt Widgets UI에서 사용한다. 원본 Windows 코드를 직접 링크하지 않는다.
- P1의 저장소 열기/생성, 상태/diff, 파일 staging/unstaging, commit 수직 기능을 구현했다.
  P2의 원격/branch/history와 P3–P4 일부 작업의 UI도 제공하지만, 전체 옵션 동등성은 미완료다.
- 임시 저장소와 로컬 bare remote의 Git 통합 테스트 및 Qt 위젯 조작 테스트를 제공한다.
- P0의 ARM 실기/Finder/기존 merge engine 추출 검증, P5 Finder 제품 통합,
  P6 배포 검증은 아직 완료되지 않았다. 초기 앱 실행을 전체 이식 완료로 취급하지 않는다.

## 1. 목표와 완료의 정의

목표는 기존 Git 작업과 보조 도구를 macOS에서 사용할 수 있도록 C++ / Qt로 이식하는 것이다.
초기 핵심 기능 출시는 중간 단계이며, 전체 이식 완료와 구분한다.
기능의 동작과 데이터 호환성을 유지하되 macOS 메뉴, 단축키, 파일 접근 방식에 맞춘다.

전체 이식 완료에는 다음 조건이 필요하다.

- 기존 명령, 각 대화상자의 옵션과 컨텍스트 메뉴, 문서에 있는 작업을 기능 목록과 연결한다.
- 각 항목에 구현 위치, 테스트, 지원 조건, macOS 대응 방식을 기록한다.
- Git 작업뿐 아니라 text diff / merge, unified diff, image diff, blame,
  revision graph, Finder 연동, 자동화, GitWCRev, 번역과 배포를 검증한다.
- Windows 전용 기능은 대체 기능을 제공하거나 별도의 범위 변경 결정을 기록한다.
  미해결 대체 기능이 있으면 전체 동등 기능 출시로 표시하지 않는다.
- Intel과 Apple Silicon에서 실제 실행하고, 지원 macOS 버전별로 배포본을 검증한다.

기존 Windows COM 플러그인 DLL, Explorer 확장 DLL, MSI의 바이너리 실행 호환성은
네이티브 이식으로 얻을 수 없다. 이들이 제공하는 사용자 기능의 macOS 대응은 범위에 포함한다.
기존 Windows 빌드는 이식 기간에도 유지한다.

## 2. 현재 소스에서 확인한 제약

| 영역 | 조사 근거 | 이식에 미치는 영향 |
| --- | --- | --- |
| 빌드 | [build.txt](../../build.txt), [솔루션](../../src/TortoiseGit.sln) | Visual Studio, MFC/ATL, Windows SDK 기반이다. macOS 빌드 대상이 없다. |
| 주 UI | [TortoiseProc.vcxproj](../../src/TortoiseProc/TortoiseProc.vcxproj), [stdafx.h](../../src/TortoiseProc/stdafx.h) | MFC 대화상자와 Windows 라이브러리를 Qt로 교체해야 한다. |
| Git 계층 | [Git.h](../../src/Git/Git.h), [Git.cpp](../../src/Git/Git.cpp) | `CString`, ATL 잠금, 레지스트리, 프로세스 실행, Windows SSH transport가 섞여 있다. 그대로 링크할 수 없다. |
| 경로/상태 | [TGitPath.h](../../src/Git/TGitPath.h), [GitStatus.h](../../src/Git/GitStatus.h) | Windows 경로와 파일 API를 분리하고 POSIX 경로를 검증해야 한다. |
| Shell / cache | [architecture.txt](../../architecture.txt), `src/TortoiseShell`, `src/TGitCache` | Explorer DLL과 캐시 통신을 Finder 확장과 별도 상태 서비스로 대체한다. |
| Merge | [DiffData.h](../../src/TortoiseMerge/DiffData.h), [프로젝트](../../src/TortoiseMerge/TortoiseMerge.vcxproj) | 계산 코드에도 MFC/레지스트리 의존성이 있다. APR, Subversion 계열 diff 코드의 의존성도 추출해야 한다. |
| 외부 코드 | [.gitmodules](../../.gitmodules), `ext/build`, `ext/libgit2-*.patch` | macOS용 빌드와 패치 적용 가능성을 개별 확인해야 한다. |
| 테스트 | [테스트 precompiled header](../../test/UnitTests/stdafx.h) | 기존 GoogleTest도 MFC/WinSock을 포함한다. 테스트 대상 분리가 필요하다. |
| 플러그인 | [IBugTraqProvider.idl](../../src/IBugTraqProvider/IBugTraqProvider.idl) | COM, HWND, BSTR 계약을 플랫폼 독립적인 provider 계약으로 재설계한다. |

초기 정적 조사에서 `src/Git`의 `.cpp` / `.h` 31개 모두에 Windows/MFC 타입 등의
표식이 있었다. `src/TortoiseProc`는 442개 중 329개, `src/Utils`는 181개 중 138개였다.
이는 `CString*`, `HWND`, `HANDLE`, `DWORD`, `CWnd`, `CDialog*`, `CWinApp*`,
MFC/ATL/Windows include를 검색한 수치이며, 실제 이식 공수나 컴파일 가능 파일 수가 아니다.
UI 교체와 Git 계층의 의존성 분리를 함께 진행해야 한다는 근거로 사용한다.

조사 환경은 Intel macOS 26.7.1, Command Line Tools, Apple Swift 6.3.3이다.
전체 Xcode가 선택되어 있지 않으며, `cmake`, `ninja`, Qt 도구는 PATH에서 확인되지 않았다.
15개 submodule은 초기화되지 않은 상태다. 설계 작업에서는 설치나 submodule 변경을 하지 않았다.

## 3. 기술 선택

### UI와 빌드

- **Qt 6 Widgets + C++**를 사용한다. 기존 데스크톱 대화상자, 표, 트리,
  복잡한 편집 화면을 Qt Designer `.ui`와 model/view 구조로 옮긴다.
- 새 공용 코드는 C++20을 기준 후보로 두고, 추출할 기존 코드의 요구사항을 P0에서 확인한다.
  MFC 호환 클래스를 새로 만들어 전체 코드를 감싸는 방식은 채택하지 않는다.
- CMake + Ninja로 공용 코드와 Qt 앱을 빌드한다. Finder 확장은 별도 Xcode target으로
  구성하고, 상위 빌드 스크립트가 앱 번들에 포함하도록 한다.
- 필수 Qt 모듈 후보는 Core, Gui, Widgets, Network이며, Concurrent, Test,
  LinguistTools와 Svg는 실제 사용처를 확인한 뒤 추가한다.
- Qt Widgets 앱의 CMake 구성과 `.app` 생성은
  [Qt CMake 문서](https://doc.qt.io/qt-6/cmake-get-started.html)를 따른다.

Qt의 정확한 minor/patch 버전, 최소 macOS, Xcode/SDK 조합은 P0 산출물에서 고정한다.
조사 시점의 [Qt macOS 지원 문서](https://doc.qt.io/qt-6/macos.html)는
Intel과 ARM, universal 빌드, 앱 번들 배포를 설명한다. 선택한 버전의 지원표를 기준으로
최소 OS를 결정하고, 앱·Qt·모든 native 라이브러리에 같은 아키텍처 정책을 적용한다.

### 대안에 대한 결정

| 선택지 | 결정 이유 |
| --- | --- |
| Qt Widgets | 사용자가 지정한 Qt 방향을 반영하며, C++ 재사용과 기존 표/대화상자 중심 UI에 맞는다. |
| Qt Quick | 초기 UI에 혼합하지 않는다. 화면별로 두 UI 체계를 유지하는 비용을 피한다. |
| SwiftUI/AppKit 전체 UI | 앱 UI는 Qt로 통일한다. Apple API가 필요한 작은 플랫폼 bridge만 native로 작성한다. |
| Windows 실행 파일을 Wine으로 실행 | macOS 네이티브 전체 이식의 완료 기준에 해당하지 않는다. |

## 4. 모듈 구조와 실행 경계

아래 구조가 목표다. 초기 구현은 `tgit_domain`, `tgit_git`, `tgit_ui`,
`TortoiseGitQt`, `TortoiseGitAskPass` target으로 구성하며, native 서비스/확장은 후속 작업이다.

```text
Qt Widgets 앱 / CLI 자동화
        |
Application: 작업 요청, 진행 상태, 취소, 사용자 확인
        |
Domain: Repository, Revision, Status, Diff, OperationResult
        |
Ports: GitBackend, ProcessRunner, SettingsStore, CredentialBroker,
       FileWatcher, IssueProvider, ShellIntegration
        |
Adapters: Qt process / filesystem, Git CLI / libgit2, macOS bridge

Finder Sync .appex --> native IPC bridge --> 상태 서비스
        |                                   |
        +--> 앱에서 작업 열기               +--> 읽기 전용 상태 조회/캐시
```

- `src/portable/domain`: STL 중심의 값 타입과 계산. UI, MFC, Apple framework 의존성을 금지한다.
- `src/portable/application`: UI와 독립된 작업 요청/결과, 작업별 state machine과 취소 계약.
- `src/portable/adapters`: Qt 기반 프로세스, 파일, 설정 및 Git backend 구현.
- `src/qt`: main window, dialogs, view models, diff / merge / blame 화면, resources.
- `src/platform/macos`: Objective-C++ bridge, Keychain/앱 열기/파일 감시와 Finder 확장.
- `test/portable`, `test/qt`, `test/macos`: 계산, Git 통합, UI와 확장 검증을 나눈다.

기존 `src/Git`, `src/Utils`, `src/TortoiseMerge`에서 추출할 때 원래 테스트와 출처를 유지한다.
공용 헤더에서 `windows.h`, `afx*`, `atl*`, `HWND`, `HANDLE`, `CString`을 제거한다.
Windows UI에는 adapter를 붙여 점진적으로 공용 코드를 사용하게 하고, 기존 솔루션은 유지한다.
새 Qt Windows UI의 제품 전환은 macOS 이식과 별개의 결정이다.

### Git 실행과 데이터 모델

- Git CLI를 쓰기 작업의 우선 backend로 사용한다. 읽기 성능이 필요한 status/history/object
  접근은 공용 인터페이스 뒤에 libgit2 adapter를 추가하고, 결과 동등성과 성능을 비교한다.
  backend 선택이 `.gitconfig`, filters, hooks, credential helper 동작을 바꾸지 않게 한다.
- 기존 `ext/gitdll`과 Git for Windows 관련 코드는 그대로 가져오지 않는다.
  호출처를 조사해 Git CLI/libgit2 대체 가능 여부를 하나씩 기록한다.
- [QProcess](https://doc.qt.io/qt-6/qprocess.html)의 executable과 argument list를 분리해 사용한다.
  executable 절대 경로와 작업 디렉터리를 지정하고, shell 명령 문자열을 조합하지 않는다.
- 기계 판독용 출력은 해당 Git 명령이 제공하는 NUL 구분/porcelain 형식을 사용한다.
  filename의 원시 bytes와 화면 표시용 문자열을 분리하고, 손실 있는 변환으로 다른 파일을
  수정하지 않도록 경로 round trip을 검증한다. UTF-8 외 이름의 지원 정책은 P0에서 확정한다.
- 저장소별 쓰기 작업을 직렬화한다. linked worktree의 공통 git directory도 잠금 범위에 포함한다.
  외부 Git의 lock은 존중하며, 임의 삭제하지 않는다. read 결과에는 generation을 붙여
  이전 조회 결과가 최신 화면을 덮어쓰지 못하게 한다.
- stdout/stderr를 비동기로 소비하고 큰 결과는 제한/스트리밍한다. UI thread에서
  `waitForFinished()`를 호출하지 않는다. progress와 오류, 종료 코드, 취소를 구분한다.
- 취소는 child process와 hook/filter/SSH 자식까지 고려한다. 중단 시 저장소 상태를 다시 읽고
  merge/rebase 등의 continue/abort 복구를 제공한다. 취소를 자동 rollback으로 표시하지 않는다.
- path 옵션에는 `--` 또는 지원되는 pathspec 입력을 사용한다. leading dash, 개행, 공백,
  glob 문자가 파일 선택을 바꾸지 않도록 literal pathspec 정책을 테스트한다.

## 5. 화면 설계

주 창은 저장소 선택, 작업 영역, 진행/결과 패널로 구성한다. Finder 명령이나 CLI에서
시작하면 해당 저장소와 작업 화면을 직접 열 수 있게 한다.

| 기존 UI/도구 | Qt 구성과 유지할 동작 |
| --- | --- |
| 상태/커밋 | `QTreeView`/`QTableView` + model, staged/unstaged 구분, 파일·hunk·line 선택, 메시지/버그 ID, amend, hooks, signing |
| Log / refs / reflog | 페이지 단위 조회, 필터/검색, 경로 이력, refs 트리, log 컨텍스트 작업, commit 비교, mailmap |
| Progress / sync | 작업별 진행/출력, 인증 요청, 실패 원인, 재시도와 취소. push의 원격/브랜치와 변경 범위를 명시 |
| Text diff / merge | 스크롤/선택 동기화, 2-way/3-way, 충돌 탐색/선택/편집/저장, whitespace/EOL/encoding 옵션, moved blocks |
| Unified diff / blame | 큰 파일용 가상화 뷰, 문법 강조, 검색/이동, blame 색상·작성자·이전 revision 탐색 |
| Image diff | Qt image decode와 custom painter, zoom/pan, side-by-side/overlay, alpha/색상 비교. 지원 이미지 형식을 목록화 |
| Revision graph | 별도 그래프 model/layout와 Qt scene/view, 필터·zoom·ref 표시·내보내기. 대규모 이력의 메모리 예산 설정 |
| Settings | `QSettings` 기반 설정 저장, Git config scope 구분, remotes/credentials/proxy/SMTP/hooks/tool associations/색상/배지/저장 데이터 관리 |

`GitStatusListCtrl`처럼 UI와 Git 데이터가 섞인 클래스는 model과 service로 나눈다.
diff 편집기는 custom `QAbstractScrollArea` 기반 후보와 Scintilla Qt 연동 후보를 P0에서
비교한다. 텍스트 표시만 되는 위젯으로 merge 편집 기능을 대체 완료 처리하지 않는다.
외부 편집기/diff/merge 도구 호출과 종료 코드 계약도 유지한다.
Scintilla/lexilla, OGDF, Hunspell의 재사용 여부는 각각의 빌드·기능 검증으로 결정한다.

번역은 기존 PO 문자열과 resource ID를 Qt TS/QM context에 연결하는 변환표를 만든다.
한글 경로/입력기, 키보드 탐색, VoiceOver, Retina, light/dark mode, 시스템 메뉴와
Cmd 단축키를 검증한다. 기존 번역 파일을 직접 덮어쓰지 않는다.

## 6. Windows 기능의 macOS 대응

| 기존 구성 | 제안하는 macOS 대응 | 필수 검증 |
| --- | --- | --- |
| Explorer shell DLL / overlays | 작은 native Finder Sync `.appex`, 등록된 저장소의 메뉴와 badge | 사용자 활성화, 다중 선택, nested repos, extension 재시작, 다른 Finder 확장과 공존 |
| TGitCache | 사용자 세션 상태 서비스 + debounce/invalidation cache | FSEvents 누락/재스캔, rename, 외부 Git 변경, 앱 종료/로그인, worktree/submodule 경계 |
| Registry / saved data | `QSettings`, Git config, 명시적 설정 import/export | global/local/worktree scope, 이전 설정의 이식 가능 항목과 기기별 경로 구분 |
| Win32 process / named pipe | QProcess, native IPC adapter | 종료/취소, stdout/stderr 처리, 인증 응답, 버전 불일치, 서비스 부재 |
| Windows credentials / PuTTY / Pageant | macOS credential helper/Keychain, OpenSSH/agent, askpass helper | HTTPS/SSH, 취소, host key 실패, passphrase, GUI launch 환경, GPG/SSH signing |
| IBugTraqProvider COM | versioned out-of-process provider protocol | 설정 검증, ticket 선택, commit 메시지 검사/수정, 오류와 취소; 기존 provider는 별도 이식 필요 |
| GitWCRev / COM automation | portable `gitwcrev` CLI 및 문서화한 호출 계약 | keyword substitution, dirty/untracked/submodule 상태; COM 호출처를 CLI로 바꾸는 안내 |
| Shell drag/drop / clipboard | Qt 파일 작업 + Finder에서 앱으로 전달 | copy/move/add/worktree 동작, overwrite 확인, 다중 파일, 취소와 symlink |
| SMTP / Windows mail integration | 이식 가능한 SMTP adapter 및 native mail compose | patch 첨부, encoding, TLS/auth, 여러 patch의 순서; `mailto`만으로 대체하지 않음 |
| CHM / MSI / update / crash report | HTML 도움말, `.app`/DMG, macOS update/진단 경로 | offline help, 업데이트 검증/실패 복구, 사용자 동의 후 진단 전송 |

Finder Sync는 등록 폴더의 badge와 메뉴를 제공하는 API이며, 일반적인 Finder UI 확장을
무제한 제공하는 API는 아니다. 이 설계에서 사용할 수 있는 범위를 P0에서 실기 검증한다.
공유 설정은 App Group, 상태 조회는 native XPC bridge를 우선 후보로 둔다.
이는 [Apple의 Finder Sync 안내](https://developer.apple.com/library/archive/documentation/General/Conceptual/ExtensibilityPG/Finder.html)를
기반으로 한 설계 제안이며, entitlement/서명/IPC 접근은 아직 검증하지 않았다.
확장 안에 Qt UI나 Git write 작업을 넣지 않고 앱에서 실행한다.
`directoryURLs`는 시작 시 설정한다([API 계약](https://developer.apple.com/documentation/findersync/fifindersynccontroller/directoryurls)).
Finder의 drop menu를 똑같이 재현할 수 있는지는 별도 spike로 판단하고, 앱 내 대응과 차이를 기록한다.

상태 서비스와 확장 사이의 요청에는 protocol version, repository ID, 제한된 action,
경로 목록, request ID를 둔다. 실행 가능한 임의 명령 문자열은 전달하지 않는다.
App Group/IPC의 접근 주체와 요청 경로를 검증하고, 캐시에는 credential을 저장하지 않는다.
확장 재시작이나 프로토콜 불일치가 Git 작업의 중복 실행으로 이어지지 않도록 한다.

## 7. 기능 추적표

다음은 [Command.cpp](../../src/TortoiseProc/Commands/Command.cpp)의 `commandInfo`에
등록된 **74개 이름** 전체의 초기 분류다. 등록되어도 비활성화되거나 미구현된 항목이 있다.
행의 P 번호는 구현 목표 단계이며, 현재 구현 완료를 뜻하지 않는다.

| 명령 이름 | macOS 대응 / 목표 단계 |
| --- | --- |
| `about`, `help`, `rtfm` | 정보, offline/online 도움말 / P1, P6 |
| `repocreate`, `clone`, `firststart`, `inaccessible` | 생성/복제/설정 wizard/접근 오류 / P1–P2 |
| `repostatus`, `add`, `commit`, `diff`, `prevdiff` | 상태, staging, commit, 비교 / P1–P3 |
| `rename`, `remove`, `revert`, `ignore`, `cleanup` | 파일 작업, 되돌리기, ignore, 정리 / P2 |
| `fetch`, `pull`, `push`, `sync` | 원격 작업과 통합 상태 / P2 |
| `branch`, `tag`, `switch` | ref 생성/삭제/전환 및 관련 옵션 / P2 |
| `log`, `cat`, `refbrowse`, `reflog`, `commitisonrefs` | 이력/object/refs/reflog/ref 포함 여부 / P2–P4 |
| `conflicteditor`, `resolve`, `merge`, `rebase` | 충돌 편집, 해결, merge/rebase 복구 상태 / P3 |
| `formatpatch`, `importpatch`, `export`, `showcompare` | patch 생성/적용, archive, revision 비교 / P3 |
| `blame`, `repobrowser`, `revisiongraph` | 전용 viewer와 graph / P4 |
| `stashsave`, `stashapply`, `stashpop` | stash 및 충돌/미추적 파일 옵션 / P3 |
| `subadd`, `subupdate`, `subsync` | submodule 및 재귀/인증/충돌 / P4 |
| `newworktree`, `worktreelist`, `dropnewworktree` | worktree 생성/관리와 앱의 drop 대응 / P4–P5 |
| `bisect` | 시작/good/bad/skip/reset 상태와 결과 / P4 |
| `lfslocks`, `lfslock`, `lfsunlock` | Git LFS backend와 인증/locking / P4 |
| `svndcommit`, `svnrebase`, `svnfetch`, `svnignore` | git-svn 설치 탐지와 동등한 workflow / P4 |
| `sendmail`, `requestpull`, `daemon` | patch mail, request-pull, Git daemon 수명/옵션 / P4 |
| `settings`, `updatecheck`, `pgpfp` | 설정, macOS 배포 업데이트, 검증할 릴리스 키 정보 / P2, P6 |
| `dropcopyadd`, `dropmove` | 앱 내 drop/copy/move/add 및 Finder 대체 / P5 |
| `registerwin11contextmenu` | Windows 전용 등록을 Finder 설정 안내로 대응 / P5 |
| `autotexttest` | 내부 진단으로 분류; 사용자 기능과 테스트 도구를 분리 / P6 |
| `stashlist` | 현재 factory case가 없는 등록 이름. stash 목록의 실제 UI 진입점을 추적하고 P3에 구현 |
| `crash`, `dropcopy`, `pastecopy`, `pastemove`, `rebuildiconcache`, `unignore` | factory의 `#if 0` 항목. 활성 기능과 구별해 보관하고 macOS 재활성화는 별도 결정 |

명령 이름만으로 전체 기능을 셀 수 없다. 다음 항목은 별도 추적이 필요하다.

- Log/refs 컨텍스트 메뉴: cherry-pick, revert commit, reset 모드, squash,
  notes, branch/tag 관리, compare, patch export 등 실제 메뉴의 모든 action.
- commit의 부분 staging/unstaging, amend, sign-off, signing, changelist,
  프로젝트 속성, 자동 완성/맞춤법 검사, bugtraq와 client/server hooks.
- remotes/refspec, tracking, force-with-lease, push tags, pull 전략,
  stash 목록/삭제/충돌, submodule/worktree의 세부 작업.
- sparse/partial clone, bare repo, shallow repo, alternates 등은 현재 Windows
  구현/문서에서의 지원 여부부터 확인하고, 기존 범위와 Git backend의 기능을 혼동하지 않는다.
- TortoiseMerge/UDiff/IDiff/Blame의 메뉴/설정/파일 형식과 자동화 인자,
  GitWCRev placeholder, external tools, hooks, issue provider의 세부 계약.
- 문서의 [작업 가이드](../source/en/TortoiseGit/tgit_dug/dug.xml),
  [자동화](../source/en/TortoiseGit/tgit_app_automation.xml),
  [GitWCRev](../source/en/TortoiseGit/tgit_gitwcrev.xml),
  [설정 코드](../../src/TortoiseProc/Settings)를 행 단위 backlog로 확장한다.

각 backlog 항목은 `기존 위치 → macOS 화면/service → 테스트 → 상태/차이`를 가진다.
P0에서 메뉴/resource/문서까지 교차 조사하기 전에는 이 표를 최종 기능 명세로 취급하지 않는다.

## 8. 외부 의존성 처리

| 의존성 | 계획과 확인 항목 |
| --- | --- |
| Git CLI / Git LFS / git-svn / signing tools | 초기에는 사용자 설치 실행 파일을 명시적으로 선택. 버전/필요 기능 탐지, GUI PATH 문제, 미설치 안내, helper/hook 동작을 검증. 번들 배포 여부는 P0 결정 |
| libgit2 / zlib / PCRE2 | 정확한 commit과 패치를 고정하고 macOS native 빌드. Windows 전용 transport/filter는 분리. Git CLI와 설정/권한 처리 차이를 비교 |
| `ext/gitdll`, `ext/tgit`, APR/APR-util | Git wrapper와 Subversion diff 코드의 실제 사용 범위 조사. 재사용 계산 코드만 별도 library로 추출하고 결과 fixture로 검증 |
| Scintilla / lexilla | 현재 Win32 wrapper 교체 필요. Qt 연동판의 유지 상태/라이선스/기능 또는 custom editor 비용을 P0에서 비교 |
| OGDF | 그래프 계산 코드 재사용 후보. macOS CMake/성능/배포 조건 확인 |
| Hunspell / dictionaries / EditorConfig | 공용 빌드 후보. resource 배치, encoding, 설정 우선순위와 license notices 확인 |
| SimpleIni / JSON | 사용하는 데이터 호환성에 따라 유지 또는 Qt adapter로 교체. 저장 형식 변경은 명시적 migration 필요 |
| ResizableLib / Detours / TortoiseOverlays / PuTTY / CrashServer | Windows 구현은 macOS 대상에서 제외. 대응 기능은 Qt layout/native integration/OpenSSH/진단 경로로 각각 추적 |
| GoogleTest | UI 독립 테스트를 CMake target으로 추출. Qt 화면은 Qt Test, native 확장은 별도 실기 테스트 |

Qt와 추가 editor 라이브러리를 포함한 배포 의존성의 라이선스/notice/source 제공 조건을
버전별로 기록한다. 기존 파일의 저작권과 라이선스 문구를 유지하며, 이 설계에서는
프로젝트의 라이선스를 변경하지 않는다. Qt 모듈마다 조건이 같다고 가정하지 않는다
([Qt Licensing](https://doc.qt.io/qt-6/licensing.html)).

## 9. 구현 순서와 통과 기준

| 단계 | 작업/산출물 | 통과 기준 |
| --- | --- | --- |
| P0: feasibility | Qt/Xcode/CMake 버전 고정, 전체 기능 backlog, Git status parser/실행 spike, 작은 diff/merge engine 추출, native Finder/IPC/서명 spike, editor 결정 | Intel에서 Qt 창 실행과 한글 경로 status/diff 확인. ARM 장비에서 같은 spike 실행. Finder badge/menu/앱 전달 실기 확인. 실패한 API 대안과 공수 재산정 기록 |
| P1: 실행 가능한 수직 기능 | Qt `.app`, 저장소 열기/생성, 상태, diff, 기본 staging/commit, 작업 결과 패널, portable 테스트 | 임시 저장소에서 선택한 변경만 commit되고 staged/unstaged 파일 및 index가 기대와 일치. 실패/취소 후 UI 사용 가능. 이 단계가 최초 로컬 빌드·실행 산출물 |
| P2: 일상 Git 작업 | clone/fetch/pull/push, branch/tag/checkout, 파일 작업, history/refs, remotes/settings, HTTPS/SSH/signing | 로컬 bare remote 및 테스트 remote에서 동작. 인증 취소, hook 실패, non-fast-forward, dirty checkout 검증. 부분 완료를 성공으로 표시하지 않음 |
| P3: 변경/복구 도구 | 완전한 부분 staging, stash, 2/3-way merge, interactive rebase/cherry-pick/reset/revert, patch/encoding | 충돌 fixture에서 편집→저장→해결→commit. rebase continue/skip/abort, binary/EOL/encoding/rename 사례 검증 |
| P4: 고급 기능/보조 도구 | blame, UDiff/IDiff, graph, repo browser, submodule/worktree/LFS/git-svn/bisect, mail/daemon, GitWCRev, issue provider | 각 도구의 기존 메뉴/옵션/자동화 계약을 기능별 fixture와 연결. 추가 도구 미설치 상태도 동작을 설명. provider 연동 예제와 GitWCRev 결과 비교 |
| P5: Finder 통합 | 제품용 확장/상태 서비스, 메뉴/badge, drop/clipboard 대응, 자동화 CLI | 실제 Finder에서 다중 선택과 nested repo/worktree 표시. 외부 commit 후 상태 갱신. 확장 비활성/종료/재시작 시 앱 작업 유지. OS별 차이 해결 또는 범위 결정 기록 |
| P6: 전체 동등 기능 검증/배포 | 번역, 접근성, 성능, HTML docs, signed/notarized DMG, 업데이트/진단, Windows 회귀 검증 | 추적표의 모든 항목에 테스트 또는 명시적인 범위 결정. 깨끗한 Intel/ARM Mac에서 설치→실행→Git 작업→업데이트 확인. 남은 기능 차이를 릴리스 문서에 공개 |

의존 순서는 P0 → P1 → P2 → P3/P4 → P5 → P6이다. Finder의 기술 가능성은 P0에서
먼저 확인하고 제품 통합은 P5에서 한다. P1–P4 진행 중에도 Windows 회귀 테스트를 유지한다.
팀 규모, 재사용 성공률, native 확장 결과가 없으므로 완료 날짜를 확정하지 않는다.
P0 이후 화면별 이식량과 dependency spike 결과를 근거로 단계별 인력/일정을 산정한다.

### 검증할 저장소와 데이터

- 정상/empty/unborn/bare/detached/shallow 저장소, linked worktree와 submodule.
- 한글/emoji/조합형 이름, 공백/개행/leading dash, symlink, executable bit,
  case-sensitive/insensitive APFS, case-only rename와 Unicode normalization 충돌.
- LF/CRLF, BOM/legacy encoding, binary/large files, filter/LFS, 부분 staging과 rename.
- 외부 Git과 동시 사용, index lock, 권한/디스크 부족, sleep/wake, 서비스 재시작.
- HTTPS credential helper, SSH agent/askpass, 잘못된 host key, 서명/commit hook 실패.
- destructive 작업의 대상/결과 안내와 복구 경로. 테스트는 폐기 가능한 fixture에서 수행한다.

성능 fixture는 소형/대형 작업 트리와 긴 이력을 포함한다. P0에서 장비와 데이터 크기,
첫 화면 응답, 상태 조회 시간, peak memory를 측정해 예산을 정하고 이후 회귀 기준으로 쓴다.
실제 사용자 저장소를 수정하거나 실제 remote로 테스트 push하지 않는다.

## 10. 빌드와 배포 설계

개발 빌드는 `scripts/qt/build-macos.sh`로 CMake configure/build/test/install을 실행하고,
`build-qt` / `dist-qt` 디렉터리에 `.app`을 만든다. 실제 명령과 사전 설치 항목,
앱 경로는 [Qt 구현 안내](../qt/README.md)에 있다.
CI는 Intel/ARM 실행 job, portable/Qt 테스트, Windows 기존 빌드를 포함하도록 확장한다.

배포는 우선 Mac App Store 외부의 `.app`/DMG로 설계한다. Qt frameworks/plugins와 native
helpers/extension을 번들에 포함하고, 개발용 경로가 남지 않는지 깨끗한 Mac에서 확인한다.
Qt 의존성 배치는 [macOS deployment 문서](https://doc.qt.io/qt-6/macos-deployment.html)를 따른다.
사용자 설치 Git을 쓰는 개발판과 재배포 가능한 release dependency 정책을 구분한다.

외부 배포 후보는 Developer ID 서명, hardened runtime, notarization 및 ticket stapling을
거친다([Apple 배포 지침](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)).
확장의 sandbox/entitlement와 containing app의 정책을 따로 검증한다.
App Store sandbox는 외부 Git/hooks 접근을 추가 검증해야 하므로 별도 배포 과제다
([Apple의 sandbox 및 hardened runtime 안내](https://developer.apple.com/documentation/xcode/preparing-your-app-for-distribution)).
개발용 로컬 실행, 양 아키텍처 실행 검증, 서명된 외부 배포 성공을 서로 다른 완료 상태로 기록한다.

## 11. P0에서 해결할 결정과 첫 구현 작업

| 미확정 항목 | 결정에 필요한 증거 |
| --- | --- |
| 정확한 Qt/SDK와 최소 OS | 선택 버전 지원표, Intel/ARM CI 가용성, extension 실기 결과 |
| Git 설치/번들 정책과 버전 | hooks/filters/credentials/LFS/git-svn 동작, 배포 크기와 dependency 목록 |
| 기존 Git/diff 코드 재사용 범위 | MFC 제거 후 작은 library의 macOS 컴파일과 기존 fixture 동등성 |
| Editor/graph 구현 | 큰 파일/이력의 편집·선택·렌더 성능, 추가 dependency 조건 |
| Finder 기능 동등성 | badge/menu/다중 선택/drop, App Group/XPC/서명과 OS별 제한 |
| 설정/언어/provider migration | 기존 형식 샘플, 번역 ID 대응, provider protocol 예제 |

첫 구현 변경은 새 CMake target과 Qt main window, `GitBackend`/비동기 process adapter,
NUL 구분 status parser, 저장소 열기→상태→diff의 수직 기능으로 제한한다.
동시에 Finder와 merge engine의 작은 feasibility spike를 수행해 전체 이식의 핵심 위험을 확인한다.
그 결과를 이 문서와 기능 backlog에 반영한 뒤 P1–P6 구현을 이어간다.
