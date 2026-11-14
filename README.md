# mkMangaBrowser

Qt6 app that displays a manga/comic collection as a grouped cover overview (successor of the old AHK script; grid view and thumbnail cache taken from mkGameBrowser).
For now only unpacked books (image files in a folder) are supported; CBZ may follow later.

## Data structure

```
<Manga>/<Author>/<Author (Date) Title>/
    000.jpg … NNN.jpg   pages (any image format Qt can read), naturally sorted
    metadata.conf       book metadata (UTF-8 without BOM)
```

The metadata format is specified in [doc/MetadataFormat.md](doc/MetadataFormat.md).
A book folder without `metadata.conf` gets a new one, generated from the folder names. After setting `chapters=N`, F5 adds empty entries for chapters 1..N and removes entries beyond N.
Folders whose name starts with `_` are ignored.

## Usage

- **Right-click on empty space:** Group (author, type, title, year, year/month), sort, label.
- **Right-click on a cover:** Open, Chapters, Edit meta file, Open folder, Google.
- **Double-click:** open the first page in the system image viewer (mkImageViewer acts as the reader). **Hover:** info window with chapters.
- **Ctrl+F:** filter bar (every word must occur in names, type, date or chapter titles). Esc clears and closes it.
- **Keys:** A–Z/0–9 jump to the group; Page Up/Down, Home/End, arrow keys (horizontal with Shift);
  Ctrl+E/G/L for the book under the mouse; F1 help; F5 reload; Esc closes the filter bar or quits.

## Thumbnail cache

`~/.cache/mkMangaBrowser/ThumbsCache<PixelHeight>/` – per book `<AuthorFolder>_<BookFolder>.png` and `.grey.png`, plus `_List.txt`. Same format and behavior as in mkGameBrowser.

## Configuration

`~/.config/mkProjects/mkMangaBrowser.conf` holds only `[Paths] MangaDir`. On first start you are asked for the folder (or use `mkMangaBrowser --manga <folder>`).
View, window geometry and scroll position are stored in `~/.local/state/mkProjects/mkMangaBrowser_state.conf`.

## Building

```
./build.sh            # builds into build/bin/ and copies to ~/.local/bin/
./build_lupdate.sh    # updates i18n/*.ts
```
