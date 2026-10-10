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
4. Archive long-unused files instead of deleting them.
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
| Archiving | Files unused for 6 months / 1 year / 2 years / custom; already-compressed formats skipped; archive verified before originals go to trash |

**Feature 3: Duplicates across the system**

Several folders or whole drives scanned together. Before anything is removed, the user is warned that projects may reference the copies; the hard-link option exists exactly for that case (e.g. the same large video copied into two Premiere Pro project folders).

**Safety (applies everywhere)**

* Nothing is touched until the user sees the full list of proposed changes and types `A`.
* "Deleted" files go to `<folder>/.organice/trash`. Only *Empty OrgaNice trash* deletes permanently.
* Every run writes `<folder>/.organice/last_run.log`; *Undo* replays it backwards.
* Moves never overwrite; OS files (`desktop.ini`, `Thumbs.db`) and OrgaNice itself are never touched.

