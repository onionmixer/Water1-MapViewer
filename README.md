# Water1 Map Viewer

Qt5 Widgets 기반의 Water1 DOS 원본 세계지도 추출·탐색기입니다.

현재 구현은 원본 데이터 경로 설정/검증, 단일 세계지도 PNG 및 JSON/manifest 생성, 생성 package의
기본 지도·항구 marker·항구 grid·클릭 좌표 탐색과 원본 `SAVE.DAT`의 활성 보물 위치 overlay까지 포함합니다.
원본 파일은 항상 읽기 전용으로 검사합니다.

## Build

```sh
cmake -S . -B build
cmake --build build
./build/Water1_MapViewer
```

Qt5 Core, Gui, Widgets 개발 패키지가 필요합니다.

## Tests and verification

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

`Water1_MapViewerTests` uses temporary copies only to test missing/case-colliding/changed source inputs,
synthetic active `SAVE.DAT`, package path/hash rejection, Ctrl+wheel anchor zoom, UI state restoration,
automatic package/SAVE load, and cancelled generation. `Water1_PackageAnalysis` is a standard-library Python
test: it reconstructs source terrain/marker pixels and port coordinates independently of Qt output. The
large-memory benchmark is opt-in; see [docs/benchmark-baseline.md](docs/benchmark-baseline.md).

## Default configuration

[`water1_mapviewer.ini`](water1_mapviewer.ini)의 `paths/original_data_directory`와
`paths/output_directory`가 각각 기본 원본 데이터·생성 output 경로입니다. 상대 경로는 설정 파일이
있는 디렉터리를 기준으로 해석합니다. 기본값 `../water1eng`과 `output`은 각각 workspace의 원본
데이터와 `Water1_MapViewer/output/`을 가리킵니다. 메뉴에서 새 경로를 선택하면 사용자 설정에
저장되며, 그 값이 기본 설정 파일보다 우선합니다. 창 크기와 좌/우·상/하 splitter 크기, 마지막으로
연 package와 `SAVE.DAT` 경로는 기본 파일을 변경하지 않고 같은 디렉터리의
`water1_mapviewer.user.ini`에 저장되어 다음 실행 때 복원됩니다.

`[automation]`의 `open_generated_map_package=true`이면 마지막으로 성공한 `world_map.json`을,
없으면 현재 profile의 기본 output package를 시작 시 자동으로 엽니다. 이어서
`auto_load_optional_save_data=true` 및 유효한 `optional_save_data_path`/저장된 SAVE.DAT 경로가
있으면 읽기 전용으로 불러옵니다. 파일 존재, package hash/schema, SAVE.DAT 구조 검증 중 하나라도
실패하면 경고 대화상자 없이 해당 자동 작업만 건너뜁니다.

## Original data directory

메뉴 **File → Set Original Data Directory…**에서 원본 데이터가 들어 있는 디렉터리를 고릅니다.
현재 검증하는 필수 파일은 다음과 같습니다.

- `NEWGAME.DAT` (70,677 bytes)
- `MAP.PUT` (9,072 bytes)
- `SHINARIO.CIM` (7,595 bytes)
- `COLOR.CIM` (2,568 bytes; original 24×12 port marker sprite)
- `MAIN.EXE` (known English profile: 338,277 bytes)

파일 이름은 대소문자를 구분하지 않아 찾지만, 같은 이름의 대소문자 변형이 둘 이상 있으면
모호한 입력으로 거부합니다. 경로와 검증 결과만 사용자 설정에 저장하며 원본 파일은 수정하지
않습니다.

전체 설계와 후속 단계는 [PLAN_WATER1_MAPVIEWER.md](PLAN_WATER1_MAPVIEWER.md)를 참조하세요.

## Reproducible validation and generation

The same read-only validator and generator are available without opening the main window:

```sh
./build/Water1_MapViewer --validate-source ../water1eng
./build/Water1_MapViewer --generate ../water1eng --output ./output
```

The generator writes `uw1-eng-dos-uw1-eng-dos-baseline/world_map.png`, `port_marker.png`, `world_map.json`,
`manifest.json`, and `tile_manifest.json` below the supplied output directory. It also writes a 512×512, 7-level tile pyramid
(`tiles/L0` through `tiles/L6`) for interactive viewing. Generation requires substantial memory because the
required standalone image is 24,192 × 6,048 pixels.

## Large-map viewing and overlays

The viewer does not decode the 558 MiB full-resolution RGBA image when opening a package. It selects tiles
for the current zoom level and keeps their decoded pixmaps in a bounded 96 MiB LRU cache. Tile files are
SHA-256 checked when they enter the cache; a corrupt or missing tile is reported without loading the entire map.

The map viewport has fixed top longitude and left latitude rulers. They follow pan and Ctrl+wheel zoom, selecting
an uncluttered multiple of the original 5° sector interval; once a sector occupies at least 72 screen pixels,
every 5° value is labelled.

The **View** menu controls port markers, quest-eligible treasure cells, and the sector grid. Ports and
Treasures tabs support column sorting and case-insensitive text filtering. Each package includes
`port_marker.png`, extracted from the original `COLOR.CIM` offset `0x0120` as its 24×12 three-plane sprite
(plane order 1,4,2); the viewer places it at the same cell top-left coordinate used by the original game.
Press **Ctrl+F** to open a modal port-name search; enter a partial name and press Enter to focus the first matching
port on the map, or press Esc to close without changing the selection. Press **F3** to select the next matching
port, wrapping to the first result after the last. A failed search is shown once inside the same modal, without
opening nested alerts. Clicking a port tile shows initial sovereignty, independent/national status, Court ruler availability
(king or sultan where applicable), guild availability, discovery state, and record ID in the right panel.
The selected port tile also receives a high-contrast rainbow outline that advances every 0.1 seconds.
Press **F2** to open a modal list of allocated slots in the loaded read-only `SAVE.DAT`; click a slot to switch
the active slot, or press Esc to cancel. Unallocated slots are not shown.
Press **F4** to open the selected slot's read-only **Fleet Status** modal. It shows active player ships, their
captains, crew, hull/sails/speed/guns/morale, cargo use, and a separate stock table for each ship. Empty player
ship slots are omitted; Food, Water, and Lumber are always shown first, while zero-quantity trade-goods are
omitted. Close and Esc close the modal.
When a port is selected, the right-side **Market Prices…** button opens a modal price table for the selected `SAVE.DAT`
slot. It places three horizontal `Item / Buy / Sell` sets side by side (nine rows, no table scroll); Buy means the
player buys from the port and Sell means the player sells to the port. Unavailable port stock is shown as `—`.
The dialog's **Close** button and Esc both close it. Original
`SAVE.DAT` market economy records exist only for port IDs 0–49, so the viewer explicitly reports that no
price data exists for the remaining map ports instead of estimating it.
Clicking a treasure tile shows either the active read-only `SAVE.DAT` slot/item/value or its static candidate status.

An existing package can be integrity-checked without opening the main window:

```sh
./build/Water1_MapViewer --verify-package ./output/uw1-eng-dos-uw1-eng-dos-baseline/world_map.json
./build/Water1_MapViewer --open-package ./output/uw1-eng-dos-uw1-eng-dos-baseline/world_map.json
./build/Water1_MapViewer --inspect-save ../water1eng/SAVE.DAT
```

뷰어에서 생성 package를 연 뒤 **File → Set Optional SAVE.DAT…**로 원본 저장 파일을 선택하면,
유효 슬롯 중 활성 상태인 보물 퀘스트의 위치와 이름·가치를 읽어 파란 marker와 보물 탭에 표시합니다.
이 동작은 저장 파일을 변경하지 않습니다.

The baseline profile and terrain LUT/palette provenance are documented in
[docs/data-provenance.md](docs/data-provenance.md); market-price reconstruction is documented in
[docs/market-prices.md](docs/market-prices.md), and fleet-status reconstruction is documented in
[docs/fleet-status.md](docs/fleet-status.md).
