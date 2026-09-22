# PLAN_WATER1_MAPVIEWER — 원본 세계지도 추출·메타데이터·Qt5 탐색기

> 상태: **P0~P5 및 설정 복원·원본 항구 sprite 추출 구현됨** (2026-09-22)
>
> 구현 증거: Qt5/CMake build 성공, `water1eng/` baseline profile 검증 성공, headless generator가
> `world_map.png` (24,192×6,048), `port_marker.png`, `world_map.json`, `manifest.json`을 실제 생성하고 package reader가
> image hash/dimension·70 active ports·13,459 treasure candidates를 검증했다. schema 6 package는
> 7단계·774개 tile pyramid와 per-tile hash manifest를 포함하며, viewer는 96 MiB bounded LRU cache로
> 현재 화면 tile만 비동기 decode한다. 창/splitter와 경로 상태는 별도 사용자 INI로 복원하며, 유효한
> package·SAVE.DAT만 시작 시 자동 로드한다. 항구는 `COLOR.CIM` 원본 24×12 sprite를 사용한다.
> CTest/QTest와 표준 라이브러리 Python package 분석·결정성/benchmark가 통과했다.
>
> 목표: 원본 Uncharted Waters / Water1 DOS 데이터 디렉터리를 사용자가 지정하면, 그 안의
> 원본 자원에서 완성된 세계지도를 하나의 독립 대형 이미지로 생성하고, 지도상의 위도·경도,
> 항구, 보물(고정 지점이 아니라 가능한 지점과 저장 파일의 활성 지점)을 별도 데이터 파일에
> 기록한다. `Water1_MapViewer` Qt5 프로그램은 큰 지도를 좌측에서 확대·축소·이동·클릭 탐색하고,
> 우측에서 선택 위치의 정보를 표시하며, 하단 탭에서 항구와 보물 목록을 grid로 제공한다.

> 이 문서의 구현 대상은 새 독립 프로젝트 `Water1_MapViewer/`이다. 기존 `Water1_Engine`과
> 원본 데이터는 **읽기 전용 입력**이며 수정·복사·덮어쓰지 않는다.

---

## 0. 완료 정의

다음이 모두 충족되어야 프로젝트를 완료로 판단한다.

1. Qt5 Widgets 기반 `Water1_MapViewer`가 원본 데이터 경로를 메뉴에서 지정·저장·변경·검증할 수 있다.
2. 선택된 경로의 `NEWGAME.DAT`, `MAP.PUT`, `SHINARIO.CIM`을 검증한 뒤 원본 전역 지형을
   단일 파일 `world_map.png`로 생성한다.
3. `world_map.json`에는 이미지 좌표, 게임 sector/sub 좌표, 위도·경도, 항구 위치·속성 및
   보물 데이터가 명확한 스키마로 저장된다. 이미지 파일만으로도 지형을 볼 수 있고, JSON이
   없거나 불일치하면 지도 위 정보 기능은 안전하게 비활성화된다.
4. 앱은 좌측 대형 지도, 우측 선택 정보 panel, 하단 `항구`/`보물` tab + grid를 제공한다.
5. 지도 위에서 Ctrl+mouse wheel로 mouse anchor 중심 확대·축소, mouse drag로 pan, click으로
   world cell 선택이 가능하다. 선택·table row·지도 marker는 상호 동기화된다.
6. 정적 원본 추출에 실제 좌표 보물 목록이 없다는 사실을 숨기지 않는다. 가능한 보물 지점과
   선택적 SAVE.DAT의 현재 활성 보물은 별도 종류로 구분한다.
7. 추출 실패, 잘못된 데이터 경로, 손상/다른 판본 파일, 이미지/JSON hash 불일치, 대형 이미지
   메모리 부족을 사용자에게 설명 가능한 오류로 처리한다.

---

## 1. 현재 엔진과 원본 자료 조사 결과

### 1.1 신뢰 가능한 입력 파일

| 파일 | 크기 | 뷰어의 용도 | 현 엔진/분석 근거 |
|---|---:|---|---|
| `NEWGAME.DAT` | 70,677 B | 전역 지형 계층 및 block metadata | `src/main_game/map_world.[ch]` |
| `MAP.PUT` | 9,072 B | 24×12 EGA 3-plane 지형 texture 4 latitude band | `src/main_game/map_terrain.[ch]` |
| `SHINARIO.CIM` | 7,595 B | 이름과 초기 항구 record | `src/main_game/game_data.[ch]`, `shin_mem.h` |
| `COLOR.CIM` | 2,568 B | 원본 24×12 항구 sprite (`0x0120`) | `src/main_game/sprite_color.c` |
| `MAIN.EXE` | 원본 판본별 | terrain→texture quadrant LUT의 profile provenance 확인 | `map_terrain.c` 주석: logical DS:0x2414 (packed raw file offset로 직접 읽지 않음) |
| `SAVE.DAT` | 61,756 B, 선택 | 현재 퀘스트의 동적 활성 보물 좌표 | `shin_mem.h`, `event.c`, `game_loop.c` |

기준 데이터 세트 `water1eng/`와 `Water1_Engine/data/`의 필수 3개 파일은 현재 SHA-256이 서로
일치한다.

```text
NEWGAME.DAT  da0e78e42fff86935a137d0b9b25296a262b3e4fd8fef6c965ee181cea77510f
MAP.PUT      7cc1be5a31a22941449a0c8cfa4ab02864452ed4cb923e5023f1336470ad638a
SHINARIO.CIM 9bef680aa42239275f3a073072a5f3e8f3b712c253496946a646dd8c6291bf20
```

이 hash는 **현재 영어 원본 기준 profile**의 식별자다. 다른 지역판/재발매판을 전부 거부할지,
구조 검사 통과 시 experimental profile로 허용할지는 P1에서 결정한다. 첫 구현은 hash가 맞는
profile을 완전 지원하고, 맞지 않는 경우 무음으로 진행하지 않는다.

### 1.2 전역 지도 원본 구조 — 확정된 해석

`NEWGAME.DAT`의 지도는 평면 `256×128` grid가 아니다. 현 엔진의 `map_world.c`가 구현한
block → descriptor → reusable chunk 계층을 사용해야 전체 지형을 정확히 복원할 수 있다.

```text
0x0000  4 B      LE: descriptor base=0x05A4, chunk base=0x1552
0x0004  1,440 B  24×12 block records, 5 B each
0x05A4  4,014 B  223 descriptors, 18 B = 3×3 sector의 9 u16 chunk indexes
0x1552 65,219 B  1,331 terrain chunks, 49 B = 7×7 terrain IDs
```

해상도 계층:

```text
24×12 block
  × 3×3 sector/block   = 72×36 sectors (X wrapping, Y non-wrapping)
  × 7×7 terrain/chunk
  × 2×2 quadrant/tile  = 14×14 screen sub-cells/sector
```

각 terrain byte `0..52`는 quadrant `(sub_y & 1) * 2 + (sub_x & 1)`와 함께 53×4 LUT를
조회해 texture `0..17`을 고른다. texture는 `MAP.PUT`의 latitude band 별 24×12px EGA 3-plane
bitmap이다.

```text
MAP.PUT 0x01B0..0x200F
  4 bands × 18 textures × 0x6C bytes
  texture 1개 = 24×12px × 3 EGA planes (plane order: mask 1 / 4 / 2)
```

현재 엔진의 `map_terrain_band(sector_y)`와 동치인 band 식:

```c
q = sector_y / 3;
t = (q < 6) ? (5 - q) : (q - 6);
band = (t < 4) ? (abs(t) / 2) : (t - 2);  // 0..3
```

### 1.3 생성 이미지의 정확한 크기와 비용

각 sector는 `14×14` screen sub-cell이고 sub-cell 하나는 `24×12px`이다.

```text
width  = 72 * 14 * 24 = 24,192 px
height = 36 * 14 * 12 =  6,048 px
pixels = 146,313,216 ≈ 139.5 Mi pixels
ARGB32 work buffer = 585,252,864 B ≈ 558 MiB
```

따라서 단일 `QImage::Format_ARGB32` 이미지를 생성·표시하는 것은 가능하나 저사양 환경에서
메모리 압박이 크다. **사용자 요구인 독립된 하나의 거대 이미지**는 `world_map.png`로 반드시
생성하되, GUI가 매번 원본 크기 전부를 복호화하도록 강제하지 않는다. §6의 viewer cache/pyramid를
함께 사용해 UI 메모리와 pan/zoom 반응성을 확보한다.

### 1.4 항구 데이터의 실제 상태

`SHINARIO.CIM` file offset `0x1786`에는 20-byte `PortRecord` 78개가 있다. 현 엔진의 실행
memory image `g_shin`은 그중 앞 70 record를 `SHIN_OFF_PORTS=0x1786`에서 항구 상태로 이용한다.
이전 분석에는 `PORT_COUNT=78`과 runtime 70의 차이가 있으므로, 뷰어는 이를 임의로 하나로
합치지 않는다.

| 필드 | file record offset | 의미 |
|---|---:|---|
| name | `0x00..0x0D` | DOS/ASCII null-terminated, 최대 14 bytes |
| raw byte `0x0E` | `0x0E` | unclassified raw field; sovereignty 판정에 사용하지 않음 |
| sector X | `0x0F` | 0..71, east-west world sector |
| sector Y | `0x10` | 0..35, north-south world sector |
| sub X | `0x11` | 0..13, sector 내부 cell |
| sub Y | `0x12` | 0..13, sector 내부 cell |
| attributes | `0x13` | low 2bit: independent/Portugal/Spain/Ottoman Turkey; `0x04`: Guild, `0x08`: initial discovery, `0x10`: Court ruler |

첫 구현에서 `active_port_records`는 engine runtime과 동치인 **0..69**로 한정한다. 70..77은
`unclassified_records`로 JSON에 보존하되 지도 marker/table 기본 목록에는 넣지 않는다. P2의
검증에서 원본 실행·engine fixture로 70..77의 의미를 확정한 뒤에만 활성 목록에 승격한다.
이 정책은 fake port marker나 identifier mismatch를 막는다.

### 1.5 위도·경도 규약

원본/엔진 sextant 표시식은 sector 좌표를 사용한다. sub-cell은 지도의 정확한 pixel marker에는
반영하지만 degree 표시는 sector 단위다.

```c
// latitude: sector_y 0..35
lat_hemisphere = (sector_y / 18 == 0) ? "N" : "S";
lat_degrees = (sector_y < 18) ? 90 - 5 * sector_y : 5 * sector_y - 90;

// longitude: sector_x 0..71, X wraps
lon_rem = (sector_x + 38) % 72;
lon_hemisphere = (lon_rem / 36 == 0) ? "E" : "W";
lon_degrees = (lon_rem / 36 == 0) ? 5 * lon_rem : 5 * (72 - lon_rem);
```

경도 0/360 경계와 180°는 hemisphere string만으로 표현하면 모호할 수 있다. JSON은 표기용
`latitude`/`longitude` 외에 정렬·계산용 `latitude_signed`(-90..90), `longitude_east_deg`
(`0 <= x < 360`)를 함께 저장한다. `longitude_east_deg`의 0 기준은 위 원본 `lon_rem` 보정과
동일함을 `coordinate_convention` 필드에 명시한다.

### 1.6 보물 데이터의 중요한 제한

원본에는 전 세계에 고정 배치된 "보물 위치 목록"이 없다. 탐험 퀘스트 완료 시 `event.c::quest_finalize`
로직이 다음 순서로 유효 terrain/quadrant에 **동적** target을 생성해 runtime/SAVE 상태
`DS:1DF5..1DF8`(엔진 `SHIN_OFF_ENCOUNTER`)에 쓴다.

```text
1. 24×12 macro 중 valid terrain이 하나 이상인 macro를 RNG로 선택
2. macro 내 3×3 sector, 7×7 terrain tile을 RNG로 선택하고 terrain class를 검사
3. 해당 tile의 2×2 quadrant 중 bitmask가 허용한 곳을 RNG로 선택
4. sector_x, sector_y, sub_x=tile_x*2+qx, sub_y=tile_y*2+qy 저장
```

따라서 viewer의 보물 tab/data는 다음을 분리해야 정직하다.

| 보물 종류 | 입력 | 개수 | 지도 표현 |
|---|---|---:|---|
| `eligible_cell` | NEWGAME terrain + original 22-byte quadrant mask | 다수 | 기본 숨김, filter로 표시; "possible" marker |
| `active_quest_target` | 사용자가 선택적으로 지정한 `SAVE.DAT` | 0 또는 1 | 강조 marker; quest-active bit가 있을 때만 active |
| `generated_name_catalog` | MAIN.EXE table 또는 engine의 검증된 12 prefix×13 suffix | 156 조합 | **좌표 없음**; 이름 catalog 표시 목적 |

정적 메타데이터가 모든 valid candidate cell을 넣으면 수만 record가 될 수 있으므로, 기본
`world_map.json`은 terrain mask 알고리즘의 version/bitset/index를 저장한다. 사용자에게 요구된
"보물 데이터파일"은 `treasure_model` + `eligible_terrain_rules` + `eligible_cells`의 compact
run-length/bitset representation을 포함한다. UI가 목록 grid에서 보여 줄 때만 index를 확장한다.
활성 보물은 `active_treasures`에 명시 record로 저장한다.

---

## 2. 제품 범위와 사용자 흐름

### 2.1 주 사용자 흐름

1. 앱 첫 실행 → empty canvas 및 “원본 데이터 경로를 지정하세요” 안내.
2. 메뉴 **파일 → 원본 데이터 경로 설정…** → directory picker.
3. 앱이 필수 파일, 길이, header base values, hash/profile을 검사하고 결과 dialog를 표시.
4. **파일 → 지도/데이터 생성…** → output directory를 고른다. 기본은 app config가 아닌 사용자가
   지정한 `Water1_MapViewer/output/<profile-id>/`; 원본 input 폴더에는 어떤 파일도 쓰지 않는다.
5. progress/cancel 가능 generator가 `world_map.png`, `world_map.json`, manifest를 원자적으로 생성.
6. generator가 완료되면 이미지와 JSON을 열어 지도/marker/table을 표시한다.
7. Ctrl+wheel로 zoom, left drag로 pan, left click으로 cell/port/treasure를 선택한다.
8. 클릭된 위치의 raw/game/geographic/image/terrain/nearby entity 정보를 오른쪽에서 확인한다.
9. 하단 port/treasure tab에서 row 선택·정렬·filter하면 map이 해당 marker를 center/flash하고
   right panel이 갱신된다.

### 2.2 UI wireframe

```text
+----------------------------------------------------------------------------------+
| File [원본 데이터 경로 설정…] [지도/데이터 생성…] [산출물 열기…] [Quit] | View |
+-----------------------------------------+----------------------------------------+
|                                         | Selection / 정보                       |
|   MapCanvas (QGraphicsView)             | - image pixel: (px, py)                |
|   giant map + transparent overlays      | - sector/sub: (sx, sy) / (ux, uy)      |
|   Ctrl+wheel = anchor zoom              | - latitude / longitude                 |
|   drag = pan; click = select            | - terrain ID, band, texture, valid?    |
|                                         | - port / treasure / nearest entity     |
|                                         | - source profile + output hash          |
+-----------------------------------------+----------------------------------------+
| [Ports (70)] [Treasures]                                                     | ^ |
| ID | Name | Lat | Lon | Sector X | Sector Y | Sub X | Sub Y | Owner | Attr  |   |
| ... QTableView / QSortFilterProxyModel ...                                    |   |
+----------------------------------------------------------------------------------+
| status: profile / image size / zoom / pointer map coordinate / generation state |
+----------------------------------------------------------------------------------+
```

상단 `QMainWindow`, 중앙 `QSplitter(Qt::Horizontal)`, 하단 `QTabWidget`과 수직 splitter를 사용한다.
최초 splitter 비율은 좌 70%, 우 30%; 하단 30%로 한다. 배포 기본값은
`water1_mapviewer.ini`에 두고, geometry·splitter sizes·마지막 input/output/package/SAVE.DAT 경로는
기본 파일을 덮어쓰지 않는 `water1_mapviewer.user.ini`에 저장한다. 시작 자동 open은 설정상 enabled,
파일 존재, package/SAVE.DAT 검증 성공의 세 조건을 만족할 때만 실행한다.

### 2.3 메뉴 및 원본 데이터 경로 계약

필수 menu actions:

| 메뉴 | Action | 동작 |
|---|---|---|
| File | **Set Original Data Directory…** | `QFileDialog::getExistingDirectory`; validate 후 path 저장 |
| File | Validate Original Data Directory | 파일·size·header·profile/hash 검사 report |
| File | Generate Map Image and Metadata… | output directory 선택, generator 시작 |
| File | Open Generated Map Package… | `world_map.json` 선택; image+manifest 관계 검사 |
| File | Set Optional SAVE.DAT… | 동적 활성 treasure overlay/table source 지정 |
| File | Recent Original Data Directories | 성공한 path의 canonical path만 MRU 저장 |
| View | Show Ports / Show Eligible Treasure Cells / Show Grid / Reset View | overlays/view 제어 |
| Help | Data Format / Source Profile / About | input, hash, coordinate 규약과 제약 설명 |

경로 정책:

- accept는 directory 하나다. 대소문자 무관 filename 검색으로 필수 파일을 찾되, 충돌 filename이
  둘 이상이면 어느 file을 쓸지 임의 선택하지 않고 오류를 낸다.
- canonical path, profile ID, validation time만 `QSettings`에 저장한다. 원본 game 파일의 contents,
  save data, 개인정보를 config에 복사하지 않는다.
- input path를 바꾸면 이전 package를 즉시 폐기하지 않는다. current document와 source profile이
  다름을 status와 title에 보이고, user가 generate/open을 선택할 때만 전환한다.
- generator는 input file을 `QFile::ReadOnly`로 열고, output은 sibling temp file
  (`.partial`)+`QSaveFile`/atomic rename로 생성한다. cancel/failure 시 완성 파일처럼 보이는
  partial image/JSON/manifest가 남지 않게 한다.

---

## 3. 산출물과 데이터 스키마

### 3.1 package layout

사용자가 선택한 output directory 아래 한 profile snapshot을 만든다.

```text
<output>/uw1-eng-dos-<profile-id>/
  world_map.png             # 필수: 24,192×6,048, RGBA/ARGB rendered standalone image
  world_map.json            # 필수: image map metadata schema v1
  manifest.json             # 필수: generator version, input file hashes, output SHA-256
  cache/                     # 선택: viewer-generated thumbnail/tile pyramid; git/packaging 제외 가능
```

`world_map.png`는 marker/UI/text를 baked-in하지 않은 지형 base image다. 그러므로 독립 이미지로서
원본 세계지도를 보존하고, overlay 선택/필터가 바뀌어도 재생성할 필요가 없다. port/treasure marker,
grid/selection은 JSON 기반으로 viewer가 그린다.

PNG를 기본으로 정한 이유는 Qt5 `QImageWriter`/`QImageReader`가 별도 codec 없이 지원하고 단일
파일 공유가 쉽기 때문이다. image dimension 및 file size를 manifest에 쓰고 SHA-256을 검증한다.
PNG 저장 실패/codec limit은 명확히 보고하며, 구현 중 memory/size가 실측상 과도하면 **동일 pixel
content의 BigTIFF 같은 대체 포맷을 몰래 도입하지 않는다**. 별도 user decision과 Qt reader 지원
검토가 필요하다.

### 3.2 `manifest.json` 핵심

```json
{
  "format": "water1-map-package-manifest",
  "schema_version": 1,
  "profile_id": "uw1-eng-dos-2026-09",
  "generator_version": "0.1.0",
  "created_utc": "2026-09-21T00:00:00Z",
  "source_files": {
    "NEWGAME.DAT": {"bytes": 70677, "sha256": "..."},
    "MAP.PUT": {"bytes": 9072, "sha256": "..."},
    "SHINARIO.CIM": {"bytes": 7595, "sha256": "..."}
  },
  "outputs": {
    "image": {"path": "world_map.png", "width": 24192, "height": 6048, "sha256": "..."},
    "metadata": {"path": "world_map.json", "sha256": "..."}
  }
}
```

source path 자체는 manifest에 저장하지 않는다. 다른 사용자가 저작권이 있는 original data root를
노출하지 않고 package를 열 수 있게 하며, provenance는 hash/profile로만 남긴다.

### 3.3 `world_map.json` schema v1

필수 top-level 필드:

```json
{
  "format": "water1-world-map",
  "schema_version": 1,
  "profile": {"id": "...", "newgame_sha256": "...", "main_exe_sha256": "..."},
  "image": {"path": "world_map.png", "width": 24192, "height": 6048, "pixel_format": "RGBA8888"},
  "grid": {
    "sectors_x": 72, "sectors_y": 36, "sub_cells_per_sector": 14,
    "cell_pixel_width": 24, "cell_pixel_height": 12,
    "x_wraps": true, "y_wraps": false,
    "pixel_origin": "top-left; x grows east in sector space, y grows south"
  },
  "coordinate_convention": {"latitude": "...", "longitude": "..."},
  "ports": [],
  "unclassified_port_records": [],
  "treasure_model": {},
  "active_treasures": []
}
```

항구 record 예시:

```json
{
  "id": 0,
  "name": "Lisbon",
  "source_record_index": 0,
  "position": {
    "sector_x": 32, "sector_y": 8, "sub_x": 3, "sub_y": 12,
    "cell_x": 451, "cell_y": 124,
    "pixel_x": 10824, "pixel_y": 1488, "pixel_center_x": 10836, "pixel_center_y": 1494
  },
  "coordinates": {
    "latitude": {"hemisphere": "N", "degrees": 50, "signed_degrees": 50},
    "longitude": {"hemisphere": "W", "degrees": 10, "east_degrees": 350}
  },
  "political_status": {
    "ownership_code": 1, "sovereignty": "Portugal", "independent": false,
    "court_available": true, "ruler_available": true, "ruler_title": "King", "guild_available": true
  },
  "attributes": {"raw": 29, "discovered_initially": true, "record_byte_0x0e": 0}
}
```

pixel conversion is one authoritative function:

```text
cell_x = sector_x * 14 + sub_x;      cell_y = sector_y * 14 + sub_y
pixel_x = cell_x * 24;                pixel_y = cell_y * 12
pixel_center_x = pixel_x + 12;        pixel_center_y = pixel_y + 6
```

The generated JSON must include raw values plus derived values. A consumer never has to infer whether
`x/y` means sector, sub-cell, cell or pixels.

`treasure_model` contains: original terrain ID acceptance ranges, quadrant mask table version/hash,
compact candidate index, and the exact coordinate conversion. A SAVE-derived `active_treasures` record has
`source: "SAVE.DAT"`, raw state offsets, active/found bits, target coordinate, terrain ID, pixel/lat/lon,
and `save_sha256`. If quest state does not prove an active map target, it is not plotted as an active treasure.

### 3.4 JSON schema validation

- Write a JSON Schema (draft compatible with tooling) at `schema/world_map.schema.json` and validate
  generator output in test code using a small in-process validator or a tested Qt field validator.
- `world_map.json` must reject unknown schema versions, impossible dimensions, cell/pixel mismatch,
  port coordinate bounds violations, duplicate active port ID, image path traversal, and image hash mismatch.
- JSON is UTF-8, arrays have deterministic source record order, objects use stable key ordering when written.
  This makes diff/reproducibility practical.

---

## 4. Renderer and extractor design

### 4.1 Core modules

```text
Water1_MapViewer/
  CMakeLists.txt
  README.md
  PLAN_WATER1_MAPVIEWER.md
  src/
    main.cpp
    ui/MainWindow.{h,cpp}
    ui/MapCanvas.{h,cpp}
    ui/SelectionPanel.{h,cpp}
    ui/EntityTableModel.{h,cpp}
    core/SourceDirectory.{h,cpp}       # path discovery and read-only validation
    core/SourceProfile.{h,cpp}         # known hash/layout profiles
    core/NewGameReader.{h,cpp}          # header/block/desc/chunk lookup
    core/MapPutDecoder.{h,cpp}          # 3-plane -> RGBA tiles
    core/TerrainLut.{h,cpp}             # versioned 53×4 LUT
    core/CoordinateTransform.{h,cpp}    # only authority for grid/pixel/geography
    core/PortReader.{h,cpp}
    core/TreasureModel.{h,cpp}
    core/SaveTreasureReader.{h,cpp}     # optional; no save writes
    core/MapPackage.{h,cpp}             # JSON/manifest load/write/validate
    core/WorldMapGenerator.{h,cpp}
    core/TileCache.{h,cpp}              # viewer performance cache, derived only
  tests/
  schema/world_map.schema.json
  docs/data-provenance.md
```

The new project copies neither Water1 engine source nor original resource binaries into its tree. If common
algorithms are later shared, move them only through a reviewed, dependency-free library; do not link the viewer
against `Water1_Engine` just to obtain map data.

### 4.2 `NewGameReader`

Responsibilities:

1. Open `NEWGAME.DAT` read-only, require 70,677 B for the known profile.
2. Decode header LE words and require `desc_base=0x05A4`, `chunk_base=0x1552`.
3. Validate each block descriptor index `<223` and every descriptor chunk index `<1331` before rendering.
4. Expose `terrainAt(sectorX, sectorY, terrainX7, terrainY7)` equivalent to the engine's
   `map_world_get_terrain`; X uses a positive modulo 72 and invalid Y returns error, not ocean fallback.
5. Expose `terrainAtSubCell(sectorX, sectorY, subX14, subY14)` that maps `sub/2` to the 7×7 byte plus
   `sub%2` quadrant. It must distinguish source read failure from terrain ID 0 (ocean).

### 4.3 `MapPutDecoder`

1. Read exactly `0x1E60` bytes from offset `0x01B0`.
2. For each band `0..3`, texture `0..17`, pixel row `0..11`, byte column `0..2`, use three bytes in
   source order plane0/mask1, plane2/mask4, plane1/mask2.
3. Map bits `(p0 | p1<<1 | p2<<2)` through the verified bright 8-color EGA attribute-controller mapping.
   `COLOR.CIM` is sprite data, not the terrain palette; the package records the palette profile in metadata.
4. The palette decision is a visual fidelity gate. Do not use Qt's default indexed palette or accidently map
   3-bit terrain colors as 4-bit ARGB values.

### 4.4 terrain LUT provenance

The 212-byte `terrain_id × quadrant → texture_id` LUT is a source-controlled baseline derivation from the
original's logical `MAIN.EXE` address `DS:0x2414`; the packed raw executable does not expose it at a direct
file offset. Package generation validates the supported `MAIN.EXE` hash, records the LUT SHA-256, and does not
apply the table to another profile. Python verification independently checks this LUT hash and representative
source-to-tile pixels.

### 4.5 full-image generation algorithm

```text
create QImage 24192×6048 RGBA8888/ARGB32 (or checked equivalent)
for sector_y in [0, 35]:
  band = latitudeBand(sector_y)
  for sector_x in [0, 71]:
    for sub_y in [0, 13]:
      for sub_x in [0, 13]:
        terrain = NewGameReader.terrainAt(..., sub_x/2, sub_y/2)
        quadrant = (sub_y&1)*2 + (sub_x&1)
        texture = lut[terrain][quadrant]
        decode/blit 24×12 MAP.PUT texture to image at
          ((sector_x*14 + sub_x)*24, (sector_y*14 + sub_y)*12)
write PNG through a temp output; SHA-256 produced file; atomically publish image
derive/write JSON + manifest through temporary files; atomically publish package
```

Progress units are sectors (2,592 total) or rows (36), never inner pixels only, so `QProgressDialog` remains
responsive. Run generation in worker `QThread`; all UI updates use queued signals. Cancellation is checked
at least once per sector and no partially named final output is published.

Before allocating the full image, calculate `width * height * 4` using 64-bit math, check Qt image dimension
limits and an application-configured safety budget. If allocation fails, report required memory and offer only
“cancel” or a future explicit tiled-output mode; do not pretend a half-rendered image fulfills the single-image
requirement.

### 4.6 reproducibility checks

- Given identical profile input and palette/LUT version, `world_map.png` and canonical JSON must have stable
  SHA-256. `created_utc` is only in manifest; if reproducible manifests are desired, isolate timestamp into a
  separate non-hashed report.
- Independently render sampled sectors with a small reference decoder and compare all pixels. Mandatory
  samples: northern/middle/southern bands, X=0/71 wrapping edge, coast/forest/territory terrain, Lisbon sector
  `(32,8)` and a terrain boundary.
- Image verifier reads PNG back and checks dimensions, selected cell pixels, alpha=255 everywhere, and that
  metadata pixel centers are in bounds.

---

## 5. Qt5 viewer design

### 5.1 map canvas

Use `QGraphicsView` with a `QGraphicsScene` whose scene rect is exact image dimensions. It has:

1. base map item (image/tile provider), z=0;
2. grid item (optional, generated per exposed rect/zoom), z=10;
3. port marker item collection, z=20;
4. eligible treasure overlay (aggregated/tiled, not one QGraphicsItem per cell), z=25;
5. active treasure markers, z=30;
6. selection crosshair/cell rectangle, z=40.

Input semantics:

| Gesture | Required behavior |
|---|---|
| Ctrl + wheel | pointer-under-mouse anchor zoom; clamp scale to `[0.02, 64]`; pixelated nearest transform |
| wheel without Ctrl | vertical/horizontal scroll (platform default), no surprise zoom |
| left drag | hand pan; drag threshold prevents accidental selection |
| left click | transform viewport→scene; clamp; resolve cell/entity; select |
| middle drag / Space+left drag | optional equivalent pan, if implemented document it |
| double-click port/table row | center marker and choose usable inspection scale |
| Home | reset fit-to-view; `1` = native 1:1 pixel scale |

`QGraphicsView::AnchorUnderMouse`, `Qt::SmoothTransformation` disabled, and nearest-neighbor pixmap
transform are required. At extreme zoom out, draw a thumbnail/pyramid level rather than minifying 146M pixels
each frame. At zoom in, source image pixels must remain crisp; no bilinear blur.

### 5.2 large image loading and cache

`world_map.png` must remain a single independent output. Qt5's normal `QPixmap`/`QImage` full decode can need
~558 MiB plus display copies, so the app uses this priority order.

1. Create a bounded derived cache after package open: thumbnail plus 256×256 or 512×512 PNG tiles at pyramid
   levels beneath `<output>/cache/`. Cache manifest records source image SHA-256 and viewer version.
2. At visible zoom, load only visible tiles with an LRU memory cap set in preferences; evict nonvisible tiles.
3. If the cache does not exist, create it with progress/cancel from the original `world_map.png`; preserve the
   original image and make cache failure nonfatal only if safe full image viewing fits the configured budget.
4. Never modify image pixels, never bake entities into the cache, and invalidate cache when image hash changes.

Implementation feasibility must be proven in P5 with Qt5's available image plugins. If Qt's PNG decoder requires
full image buffering before crop, use the one-time cache build and document its RAM requirement; do not claim
out-of-core loading without a tested decoder path.

### 5.3 selection resolver and right panel

On click at scene pixel `(px,py)`:

```text
cell_x = clamp(floor(px / 24), 0, 1007)
cell_y = clamp(floor(py / 12), 0, 503)
sector_x = cell_x / 14;  sub_x = cell_x % 14
sector_y = cell_y / 14;  sub_y = cell_y % 14
terrain = NewGameReader or JSON terrain index at (sector, sub/2)
lat/lon = CoordinateTransform(sector_x, sector_y)
```

Right information panel fields:

- selected pixel, world cell, sector/sub, block/descriptor/chunk indexes, terrain ID, quadrant, latitude band,
  texture ID;
- latitude/longitude (human and numeric representations);
- port exactly at the cell, all nearby ports in configurable radius, raw port record fields;
- eligible treasure status, active target(s) and raw/save provenance;
- package profile, image/metadata source hashes.

If more than one entity is at a cell, show a compact selectable list; never overwrite one record with another.
If map package was opened without original data root, terrain provenance derives from JSON; detailed raw block
fields may be unavailable and are shown as `not packaged`, not guessed.

### 5.4 port/treasure bottom grid

Use `QTableView` plus `QAbstractTableModel` and `QSortFilterProxyModel`, not 70+ hand-built widgets.

| Tab | Rows | Columns | Default behavior |
|---|---|---|---|
| Ports | active 70 records | ID, name, lat, lon, sector/sub, sovereignty, Court/Guild/discovery attrs | sort by ID; F3 name search |
| Treasures: active | 0/1 from optional SAVE | name if known, state, lat/lon, sector/sub, terrain, source/save hash | selected target focus |
| Treasures: candidates | compact index expanded lazily | lat/lon, sector/sub, terrain, quadrant, candidate reason | disabled by default; filter/range required |
| Unclassified records | 8 by known profile | source index, raw name/bytes, coordinates if valid | diagnostic only, not map marker by default |

The user requested port and treasure lists divided by tab in grid form. The primary tabs shall therefore be
`Ports` and `Treasures`; nested treasure tabs or a mode selector inside `Treasures` is allowed so active and
candidate data are not conflated. Selecting a table row centers the map and updates the right panel; map clicks
select the matching table row where a unique entity exists.

---

## 6. implementation phases and gates

### P0 — repo boundary, project skeleton, and test fixtures

1. Create only `Water1_MapViewer/`; do not alter Water1 engine files/data.
2. Add CMake project targeting Qt5 Core/Gui/Widgets/Test and a documented Qt 5.15.3 development baseline
   (current environment: `qmake` and `Qt5Widgets` 5.15.3 are available).
3. Define source profiles, fixture policy, output package names, and CTest test layout.
4. Fixtures contain minimal synthetic binary blobs or hashes/offset fixtures; do not commit full copyrighted
   original data unless project policy explicitly permits it.

**Gate:** configure/build/test executes a blank `QMainWindow`; project contains no link/dependency on
`Water1_Engine` executable or SDL.

### P1 — input path dialog and read-only validator

1. Implement File menu and QSettings/MRU behavior from §2.3.
2. Resolve filenames case-insensitively and reject ambiguity/missing/unreadable files.
3. Validate sizes, NEWGAME header bases, profile hashes, and MAIN.EXE LUT provenance.
4. Provide a validation report with per-file path, expected/actual sizes/hash, profile status, and next action.

**Gate:** valid `water1eng/` is accepted; a missing file, wrong size, changed header, duplicate case-collision,
and hash mismatch are all rejected or explicitly marked unsupported with no writes to source directory.

### P2 — deterministic data decoders

1. Implement `NewGameReader`, terrain LUT provider, `MapPutDecoder`, and `CoordinateTransform`.
2. Implement `PortReader` including active 70/unclassified 8 split.
3. Implement `TreasureModel` and optional SAVE reader without assuming static treasure locations.
4. Add fixture and property tests for bounds, hierarchy values, plane order, coordinate and port conversions.

**Gate:** all sampled source terrain and texture pixels match independent expected fixtures; Lisbon record gives
sector `(32,8)`, sub `(3,12)` and exact derived pixel center `(10836,1494)`; latitude/longitude round-trip
tests cover 0, 17, 18, 35 and longitude wrap boundaries.

### P3 — generator and package integrity

1. Implement worker-thread giant image render and cancellation/atomic package publication.
2. Generate canonical JSON/manifest; validate JSON before publication.
3. Add SHA-256, PNG dimension/pixel sampling, and JSON/image relationship checks.
4. Benchmark memory, render time, PNG size on baseline hardware; document measured numbers rather than estimates.

**Gate:** generator creates 24,192×6,048 standalone PNG and valid package from a profile input; cancellation
does not leave published partial outputs; repeated runs produce matching image/metadata hashes when timestamp
is excluded.

### P4 — Qt viewer interaction and inspector

1. Load generated package, render map/pyramid, and add overlays.
2. Implement exact Ctrl+wheel anchor zoom, pan, click resolver and selection panel.
3. Add port/treasure table models, sorting/filtering, bidirectional selection and map focus.
4. Add no-source-root package opening and clear provenance-degraded states.

**Gate:** every required gesture works under QTest/event simulation plus manual test; selected cell at all map
edges has correct coordinates, and table↔map synchronization has no feedback loop.

### P5 — robustness, large-image performance, documentation

1. Build/cache pyramid behavior, bounded memory, corrupt cache invalidation and loading progress.
2. Test path changes, output package relocation, missing image/metadata, mismatched manifest, source removal after
generation, resize/high-DPI UI, and optional SAVE change.
3. Write README with build/run, original-data legal responsibility, path menu workflow, package schema, coordinate
convention, active-versus-possible treasure distinction, and known profile limitations.

**Gate:** §0 completion definition and §7 matrix all pass; no claim of exact original fidelity remains without
source hash/LUT/palette verification evidence.

---

## 7. verification matrix

| ID | Test | Proof |
|---|---|---|
| D1 | NEWGAME hierarchy | 24×12 block bounds; all desc `<223`, chunks `<1331`; independent sampled terrain equality |
| D2 | MAP.PUT plane decode | 24×12 texture fixture proves mask 1/4/2 plane order and bit ordering |
| D3 | LUT provenance | exact 212 bytes/hash/profile; every ID/quadrant texture in range 0..17 |
| D4 | coordinate transform | all valid sector/sub coordinates yield in-bounds cell/pixel; wrap and hemisphere boundaries correct |
| D5 | port records | parse 78×20B; active 70 and unclassified 8 retained distinctly; no duplicate active IDs |
| D6 | treasure model | candidate masks agree with terrain rules; inactive SAVE produces no active marker; active fixture produces one |
| G1 | giant image | exact 24,192×6,048, opaque pixels, deterministic sample hashes in 4 latitude bands and edges |
| G2 | package integrity | manifest image/JSON hash mismatch, path traversal and schema mismatch reject safely |
| U1 | input path menu | accept valid root, errors for missing/malformed/case collision; no input directory write |
| U2 | interaction | Ctrl+wheel anchor invariance, pan, click cell resolver, Home/1 behavior |
| U3 | UI requirements | left map/right info/bottom port+treasure grids visible after package open |
| U4 | sync | click→right/table; table→map/right; overlapping entities list correctly |
| P1 | memory/performance | cache LRU cap respected; 1×/fit/pan representative timing recorded |
| R1 | source isolation | opening/generation never modifies originals; cancel leaves no final partial package |

Manual visual reference should compare engine rendering of representative sectors with generated pixels, but an
engine screenshot is not enough alone because its 14×14 viewport has UI and viewport positioning. Compare
the extracted texture/cell pipeline at source pixels first, then compare UI appearance second.

---

## 8. risks and decisions requiring evidence

| Risk / ambiguity | Impact | Planned response |
|---|---|---|
| 558MiB base ARGB image plus Qt copies | OOM/UI stall | preflight budget, worker generation, tile pyramid/LRU; measure before declaring supported |
| Qt5 PNG reading may fully decode image | cache build may still peak high RAM | prove with P5; document minimum memory or adopt separately approved streaming codec strategy |
| terrain palette/profile mismatch | colors can differ despite correct geometry | bright EGA mapping, raw MAP.PUT pixel reconstruction, package metadata and baseline profile hash are verified |
| 70 vs 78 ports | false markers/data loss | preserve both; only 70 active until original/runtime evidence resolves remaining 8 |
| treasure positions are dynamic | misleading fixed "treasure list" | distinct candidates/active/save-derived models and UI labels |
| MAIN.EXE regional differences | incorrect LUT | profile hash + extracted LUT offset; no silent cross-profile use |
| corrupted/malicious external files | parser crash/large allocation | all lengths/indexes validated before seek/allocation; fuzz decoders with malformed fixtures |
| original game asset redistribution | licensing concern | user supplies path; no source assets committed by default; generated content ownership documented |

### Decisions deliberately deferred

1. Whether to support Korean/other original profiles; require input hashes and LUT/palette validation first.
2. Whether to bake a latitude/longitude grid into a second export; current requirement is a single clean base
   image and separate image-map data, so grid stays overlay.
3. Whether port labels are drawn permanently; current plan keeps labels interactive overlays to preserve the
   standalone map image as original terrain.
4. Whether optional SAVE.DAT uses only active target coordinates or also inventory/name parsing; P2 defines
   exact state offsets/flags before implementation.

---

## 9. acceptance checklist

- [x] New `Water1_MapViewer` project only; Water1 engine unchanged.
- [x] Qt5 build instructions, CMake target and CTest suite are reproducible.
- [x] File menu sets, validates and persists original data directory.
- [x] Generator reads only `NEWGAME.DAT`, `MAP.PUT`, `SHINARIO.CIM`, `COLOR.CIM`, profile `MAIN.EXE`, optional `SAVE.DAT`.
- [x] Generator creates one standalone `world_map.png` of 24,192×6,048 through staging publish.
- [x] Separate metadata JSON/manifest describe image map, coordinates, ports, treasure model and provenance.
- [x] Left map/right inspector/bottom Ports and Treasures grid tabs are implemented.
- [x] Ctrl+wheel anchor zoom/pan/click selection and table/map synchronization are QTest-covered.
- [x] Static candidates and dynamic active treasures are distinctly labeled.
- [x] Source directory remains unmodified; cancellation/error paths are tested in temporary output.
- [x] Hash/profile/schema/image consistency, source pixel/port/marker verification, UI persistence/automation and deterministic re-generation pass.

This plan intentionally starts with verified binary structures already implemented in the engine, but keeps the
new viewer independent: it parses original files itself, packages its own derived image/data, and does not
depend on SDL or on the running game engine.
