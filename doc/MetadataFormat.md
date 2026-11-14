# Book metadata format

Reference definition of the metadata text file used by mkMangaBrowser (inherited from the old AHK script).
Code that reads or writes book metadata is built and reviewed against this document.

**Status:** incomplete. The old app knew more fields than listed here; they are added as they are remembered.
Fields marked *not yet read* are defined here but ignored by the current code.

## Location and file

- One file per book, inside the book folder: `<Manga>/<Author>/<Author (Date) Title>/metadata.conf`.
- The file name is exactly `metadata.conf`. No other file is read as metadata (other formats such as `*.txt`,
  ComicInfo.xml or MetronInfo.xml are ignored).
- Encoding: UTF-8 without BOM.
- Line endings: LF (CR before LF is tolerated when reading).

## Syntax

- One entry per line: `key=value`. Everything after the first `=` is the value.
- Empty lines are ignored and are used to group entries visually.
- The order of entries does not matter.
- Values are not quoted; quotation marks are part of the value (`Fight 1 "Rusty Angel"`).

Parser tolerance (not part of the original format, only what the reader accepts):
leading/trailing whitespace around key and value is ignored, keys are case-insensitive,
lines without `=` and lines starting with `;` or `#` are skipped.

## Page references

Several fields refer to a page image. A page reference is the **file name without extension**, e.g. `000a` or `029-030`
(a double page stored as a single image). It is matched case-insensitively against the image files in the book folder,
so it does not depend on the image format.

Page images are all files in the book folder whose extension is a readable image format. They are numbered from `000`
to the last page; suffixes like `000a` and double pages like `029-030` occur.

## Book fields

| Key           | Value                        | Meaning |
|---------------|------------------------------|---------|
| `author_orig` | text                         | Author name in the original language/script, e.g. `木城 ゆきと` |
| `author_en`   | text                         | Author name in English/romanized, e.g. `Kishiro Yukito` |
| `name_orig`   | text                         | Book title in the original language, e.g. `銃夢Vol.01 錆びた天使` |
| `name_en`     | text                         | Book title in English, e.g. `Battle Angel Alita v01 - Rusty Angel` |
| `type`        | word                         | Kind of book, e.g. `manga` (further values to be defined) |
| `date`        | `YYYY-MM-DD`                 | Publication date (whether shorter forms like `YYYY` occur is open) |
| `cover`       | page reference               | Cover image of the book, e.g. `000a` |
| `chapters`    | number or empty              | Number of chapters; only chapters `1..chapters` are valid. Empty: not yet set, no limit |

## Chapter fields

`N` is the chapter number, starting at 1.

| Key              | Value          | Meaning |
|------------------|----------------|---------|
| `chN_start`      | page reference | First page of the chapter. Opening the chapter starts here. |
| `chN_cover`      | page reference | Title image of the chapter, shown as its thumbnail. May differ from `chN_start`: a chapter can start on page `028` but use the double page `029-030` as its title image. |
| `chN_title_orig` | text           | Chapter title in the original language |
| `chN_title_en`   | text           | Chapter title in English |

## Creating a missing file

If a book has no `metadata.conf`, a new one is created, but only if

- the book is an unpacked folder (not a CBZ), and
- the folder is on the book level: `<Manga>/<Author>/<Book>`.

The new file is filled from the folder names:

| Field       | Source |
|-------------|--------|
| `author_en` | name of the author folder (`<Author>`) |
| `name_en`   | title from the book folder name, see below |
| `date`      | date from the book folder name, see below; empty if none |
| `cover`     | first image file of the folder (natural sort order), as page reference; empty if there is none |

The book folder name is matched against these forms, in this order:

1. `Text (Date) Text`, e.g. `Kishiro Yukito (1991-09-19) Battle Angel Alita v01`: title = the text after the date
2. `(Date) Text`, e.g. `(1991-09-19) Battle Angel Alita v01`: title = the text after the date
3. otherwise the whole folder name is the title, and the date stays empty.

`Date` is `YYYY`, `YYYY-MM` or `YYYY-MM-DD`; other text in parentheses (e.g. `Foo (Remastered) Bar`) is not a date.

The new file also contains the empty fields `author_orig`, `name_orig`, `type` and `chapters` for manual editing:

```
author_orig=
author_en=Kishiro Yukito

name_orig=
name_en=Battle Angel Alita v01

type=
date=1991-09-19

cover=000
chapters=
```

## Updating chapter entries

Workflow: the file is created (see above), the user sets `chapters=N` and presses F5. On every load (start and F5)
an existing `metadata.conf` is adjusted to `chapters`:

- If `chapters` is empty, missing or not a number ≥ 0, the file is not changed.
- All `chN_*` entries with `N > chapters` are removed without asking (also unknown chapter keys).
- For every chapter `1..chapters`, missing `chN_start`, `chN_cover`, `chN_title_orig` and `chN_title_en` are added
  with empty values:
  - within an existing chapter, each missing key is inserted after the preceding key of this order
    (or before the first line of the chapter);
  - a chapter without any entry is added as a block, separated by blank lines, after the previous chapter
    (chapter 1: after the `chapters=` line).
- All other lines (other fields, unknown keys, comments, blank lines) stay where they are. CRLF becomes LF.
- When the file is written, consecutive blank lines are collapsed into one.
- The file is only written if something changed, so a second F5 does not modify it again.

Example: `chapters=2` in the new file above results in

```
cover=000
chapters=2

ch1_start=
ch1_cover=
ch1_title_orig=
ch1_title_en=

ch2_start=
ch2_cover=
ch2_title_orig=
ch2_title_en=
```

No other field of an existing file is ever changed.

## Fallbacks

Every field may be missing (and the file itself, if it could not be created).

| Value              | Order of sources |
|--------------------|------------------|
| Displayed author   | `author_en`, `author_orig`, author folder name |
| Displayed title    | `name_en`, `name_orig`, title from the book folder name (rules above) |
| Date               | `date`, date from the book folder name (rules above) |
| Book cover         | page given by `cover`, first page |
| Chapter thumbnail  | page given by `chN_cover`, page given by `chN_start` |
| Chapter title      | `chN_title_en`, `chN_title_orig`, "Chapter N" |

## Example

Chapter 2 is modified from the real file to show a title image that differs from the start page.

```
author_orig=木城 ゆきと
author_en=Kishiro Yukito

name_orig=銃夢Vol.01 錆びた天使
name_en=Battle Angel Alita v01 - Rusty Angel

type=manga
date=1991-09-19

cover=000a
chapters=6

ch1_start=003
ch1_cover=003
ch1_title_orig=錆びた天使
ch1_title_en=Fight 1 "Rusty Angel"

ch2_start=028
ch2_cover=029-030
ch2_title_orig=戦いの血
ch2_title_en=Fight 2 "Fight on Instinct"
```

## Open points

- Further fields of the old app (to be added).
- Allowed values of `type` besides `manga`.
- Whether `date` also occurs in shorter forms (`YYYY`, `YYYY-MM`) in the meta file.
- Where the metadata of a CBZ book will be stored.
