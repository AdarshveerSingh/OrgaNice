# OrgaNice

**Organize nicely. Clean safely. Undo anything.**

OrgaNice is a file organizer and cleanup tool written in C. It sorts cluttered folders, finds duplicate files by their *content*, recognises ZIP files you already extracted and installers you already installed, and archives files you haven't touched in months. Every change is previewed first and can be undone.

---

## 1. Project proposal

### 1.1 Problem statement

The Downloads folder slowly turns into a digital junk drawer: installers, ZIPs, PDFs, screenshots, videos, duplicate downloads and files downloaded once and forgotten. This project started from my own Downloads folder, which filled up with builds and assets while I worked on a procedural game project.

Looking at how people describe this problem online (Reddit, Microsoft Q&A, Apple Support Communities), the same pain points come up again and again:

| Problem | What people actually experience |
|---|---|
| Clutter | Thousands of mixed files in Downloads/Desktop, no structure |
| Duplicates | `photo.jpg`, `photo (1).jpg`, `IMG_2938.jpg` are the same picture but no tool tells you |
| Leftovers | `setup.exe` and `project.zip` stay forever, because nobody remembers if they were installed / extracted |
| Fear of breaking things | Projects (Premiere Pro, Blender...) reference files by path; deleting a "duplicate" causes *missing media* |
| Mistakes while reorganizing | Bulk moves put files in the wrong place with no way back |

Most existing tools either only sort by extension, or delete things outright. **Duplicate ≠ same filename**, and **a ZIP being "old" doesn't mean it was extracted**: you need to look inside.

### 1.2 Goals

1. Organize folders by type, extension, name rules, size or date (`Week1`, `Week2`... / months / years), with filters and conflict handling.
2. Detect duplicates by **content**, across folders and drives, and offer a non-destructive fix: **hard links** keep every path working while storing the data once.
3. Clean up installers and ZIPs **intelligently**: verify that a ZIP's contents really exist on disk (CRC-32 check) and that an installer's program is installed.
4. Archive files that have not been modified for a long time instead of deleting them.
5. Be **safe by design**: Scan → Analyze → Propose → Review → Confirm → Execute, with a trash folder and undo log.
6. Be approachable: Basic and Advanced modes, a "What do you want to do?" search, and a sandbox to try features on demo files.
7. Pure C, no external libraries, portable between Windows and Linux.

### 1.3 Specifications

**Feature 1: File organisation**

| Option | Details |
|---|---|
| Group by type | Images, Videos, Audio, Documents, Archives, Installers, Code, Fonts, Design_3D, Other |
| Group by extension | `PDF/`, `JPG/`, `ZIP/`... |
| Group by name rules | `keyword=Folder` rules (case-insensitive *contains*), plus presets: screenshot→Screenshots, IMG_→Camera, invoice→Invoices... |
| Group by size | Small (<1 MB), Medium (1–100 MB), Large (100 MB–1 GB), Huge (>1 GB) |
| Group by date | Year, Month, Day, Week of year, or **Week1, Week2, ...** counted from a chosen start date; optional second-level grouping inside each date folder (type / extension / name / size) |
| Filters | Modified-date range (last 7 / 30 days / custom), extension selection ("tick by number" checkbox style or type the extensions) |
| Folder names | Predefined, or renamed by the user (e.g. Images → Photos) |
| Destination | Same folder or another one; optionally include subfolders |
| Conflicts | Per file: Rename (keep both) / Skip / Overwrite (old file → trash); `!` applies the choice to all; identical files are flagged |

**Feature 2: Cleanup and duplicates**

| Option | Details |
|---|---|
| Duplicate detection | Size → 64 KB FNV-1a fingerprint → byte-by-byte comparison (100% certain); existing hard links recognised by file ID and ignored |
| Keeper policy | Smart (no "(1)"/"copy" in name, not in Downloads/Temp, oldest), Oldest, Newest, Shortest path, or manual per group |
| Actions | Move copies to trash, replace copies with **hard links**, or review group by group |
| ZIP leftovers | Central directory parsed directly; every entry searched on disk by name + size and **verified with CRC-32** → SAFE (≥95%), PARTIAL (≥50%), KEEP |
| Installer leftovers | File name → product key (`ChromeSetup.exe` → `chrome`), matched against Windows *Uninstall* registry entries; older versions of the same installer flagged as superseded |
| Unfinished downloads | `.crdownload`, `.part`... untouched for a day or more |
| Archiving | Files not modified for 6 months / 1 year / 2 years / custom; already-compressed formats skipped; archive verified before originals go to trash |

**Feature 3: Duplicates across the system**

Several folders or whole drives scanned together. Before anything is removed, the user is warned that projects may reference the copies; the hard-link option exists exactly for that case (e.g. the same large video copied into two Premiere Pro project folders).

**Safety (applies everywhere)**

* Nothing is touched until the user sees the full list of proposed changes and types `A`.
* "Deleted" files go to `<folder>/.organice/trash`. Only *Empty OrgaNice trash* deletes permanently.
* Every run writes `<folder>/.organice/last_run.log`; *Undo* replays it backwards.
* Moves never overwrite; OS files (`desktop.ini`, `Thumbs.db`) and OrgaNice itself are never touched.

### 1.4 Design

#### Architecture

```
                 main.c
                    |
                  ui.c ---------------- sandbox.c ---- zipfile.c (writer)
      (Basic / Advanced / search / sandbox)
        |           |             |              |
  organize.c  duplicates.c  leftovers.c      archive.c
        \           |        /    |              /
         \          |       / zipfile.c (reader) /
          \         |      /  hash.c (CRC-32)   /
           +--------+-----+---------------------+
           |   plan.c  (preview, execute, trash, undo log)
           |   scanner.c (folder walk -> FileList)
           |   hash.c  (FNV-1a, CRC-32)
           +--------------------------------------
                 utils.c (strings, paths, prompts)
                 platform.c (ONLY file with OS-specific code)
```

| Module | Responsibility |
|---|---|
| `platform.c/h` | Win32 vs POSIX: directory walking, stat, move, hard links, file IDs, registry |
| `utils.c/h` | Strings, paths, sizes, dates, colored output, input prompts |
| `scanner.c/h` | Walks folders into a `FileList` (path, name, size, dates, root) |
| `hash.c/h` | FNV-1a 64-bit and CRC-32, implemented from scratch |
| `plan.c/h` | The safety core: a `Plan` of MOVE/TRASH/HARDLINK operations, preview, execution, undo, trash |
| `organize.c/h` | Feature 1 |
| `duplicates.c/h` | Features 2 & 3: duplicate detection and actions |
| `zipfile.c/h` | ZIP central-directory reader and a minimal stored-ZIP writer |
| `leftovers.c/h` | Already-extracted ZIPs, installed / superseded installers, unfinished downloads |
| `archive.c/h` | Archiving of files not modified for a long time |
| `sandbox.c/h` | Generates a demo folder full of realistic clutter |
| `ui.c/h` | Menus, generated from one feature table |

#### The pipeline every feature follows

```
SCAN -> ANALYZE -> PROPOSE (Plan) -> USER REVIEWS -> CONFIRM -> EXECUTE (+ undo log)
```

No feature module writes to the disk itself; it only adds operations to a `Plan`. That makes previewing, logging and undoing uniform and keeps each feature small.

#### Key algorithms

**Duplicate detection** (cheapest filter first):
1. Sort files by size; only same-size files can be duplicates.
2. Drop paths with the same `(volume, file index)`: they are already hard links.
3. Hash the first 64 KB with FNV-1a; only equal fingerprints continue.
4. Compare byte-by-byte. No hash collision can produce a false positive.

**"Was this ZIP extracted?"**
1. Find the End-Of-Central-Directory record (`PK\5\6`) in the last 64 KB of the file.
2. Read the central directory (`PK\1\2` entries): name, CRC-32, uncompressed size of every file.
3. Look every entry up in a hash index of all scanned files keyed by (lower-case name, size).
4. Confirm candidates by computing their CRC-32 and comparing it with the ZIP's.
5. Report the percentage of data found and where it was extracted to.

**"Was this installer installed?"**
1. Reduce the file name to a key: drop versions, architectures and words like *setup/installer* (`VSCodeUserSetup-x64-1.85.exe` → `vscodeuser`, `python-3.12.1-amd64.exe` → `python`).
2. Read `DisplayName` / `DisplayVersion` from the three Windows *Uninstall* registry locations.
3. Match the key against the normalized names; otherwise flag older versions of the same key as superseded.

**Undo log format** (tab-separated, one line per completed operation, flushed after each one):
```
MOVE   <original path>  <new path>
TRASH  <original path>  <path in trash>
LINK   <original path>  <path in trash>     (original is now a hard link)
```

#### Design decisions and limitations

* **Terminal UI**: a GUI toolkit in C would have taken most of the time budget; the terminal UI uses ANSI colors (enabled on Windows 10+).
* **Archiving uses the system `tar`** (built into Windows 10+, writes `.zip`; `.tar.gz` on Linux). Writing a DEFLATE compressor was out of scope; the ZIP *reader* is fully custom.
* ZIP64 archives (> 4 GB) are reported as unsupported.
* The installed-program check is Windows-only; on Linux installers are only compared with each other.
* Paths are limited to 1024 characters and use the ANSI (non-Unicode) Win32 API.
* Undo covers the **last** run per folder.

---

## 2. Building and running

**Windows** (MSYS2 / MinGW-w64):
```
make
organice.exe
```
or without make:
```
gcc -std=gnu11 -O2 -D__USE_MINGW_ANSI_STDIO=1 src/*.c -o organice.exe -ladvapi32
```

**Linux / macOS**:
```
make
./organice
```

### Quick tour

```
Main menu
  1) Basic mode                simple, safe one-step cleanups
  2) Advanced mode             filters, parameters, several folders
  3) What do you want to do?   describe a goal, find the right feature
  4) Sandbox                   try any feature on demo files first
  5) Undo last operation       put everything back
```

Start with **4) Sandbox**: it creates `./organice_sandbox` with duplicates under different names, an extracted ZIP, a half-extracted ZIP, an untouched ZIP, two versions of an installer, an unfinished download, old files and the same video in two "Premiere" project folders. Example output of the leftovers check there:

```
ZIP archives
  [ 1] SAFE    project_assets.zip       84.4 KB
         3 of 3 files found & CRC-verified (100% of data) in organice_sandbox
  [ 2] PARTIAL half_extracted.zip       16.2 KB
         1 of 2 files found & CRC-verified (50% of data) in organice_sandbox/half_extracted
  [ 3] KEEP    never_opened.zip         40.1 KB
         none of its 1 files are on disk - probably never extracted
Installers
  [ 4] SAFE    python-3.11.4-amd64.exe  200.0 KB
         older version - newer installer python-3.12.1-amd64.exe is also here
```

---

## 3. Roadmap

| Phase | Status |
|---|---|
| Proposal, platform layer, scanner | Done |
| Plan engine: preview, trash, undo | Done |
| Organizer: type / extension / name / size / date, filters, conflicts | Done |
| Duplicates: content-based, keeper policy, hard links | Done |
| ZIP reader + extraction check, installer check, unfinished downloads | Done |
| Archiving of files not modified for a long time | Done |
| Basic / Advanced UI, feature search, sandbox | Done |
| Storage analyzer (what is using my disk, drill-down) | Planned |
| Fast whole-drive scan by reading the NTFS Master File Table | Planned |
| File-name analyzer (`final_final_REAL.pdf` → rule-based suggestions) | Planned |
| Unicode paths (wide-char Win32 API), ZIP64, multi-level undo history | Planned |

---

## 4. Testing

OrgaNice was tested on Windows 11 (MinGW-w64 gcc) and Linux, first on the built-in sandbox and then on real folders.

| Area | What was checked | Result |
|---|---|---|
| Leftovers | Fully extracted ZIP, half-extracted ZIP and never-opened ZIP | SAFE / PARTIAL / KEEP as expected |
| Leftovers | Older installer next to a newer one; stale `.crdownload` | Both flagged SAFE |
| Duplicates | Same picture under three names; same video in two project folders | Found by content, not by name |
| Hard links | `fsutil hardlink list` after linking | All paths listed, both files still open |
| Hard links | Re-running the search on linked files | Reported as "already hard links", ignored |
| Undo | Quick tidy followed by Undo | All files back in place, empty folders removed |
| Archive | Files older than one year | Verified archive created, originals moved to trash |

**Measured disk saving** (a 2 GB file, copied and then hard-linked with OrgaNice; free space read with `Get-Volume`):

| Step | Free space (MB) | Change |
|---|---|---|
| After creating `a.bin` | 55009 | |
| After copying it to `b.bin` | 52955 | -2054 |
| After linking and emptying the OrgaNice trash | 54939 | +1984 |

Free space only increases after the OrgaNice trash is emptied, because the old copy is kept there until then so that Undo works.

**Issue found during testing:** the archive feature originally treated a file as "recently used" if its last-accessed date was recent. Other features read the files, which can refresh that date on Windows, so nothing looked old enough to archive. Archiving now uses the last-modified date only.
