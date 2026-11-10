# Changelog

All notable changes to Area Play. **1.0** is the first public release. Area Play was developed privately under the name *VitaFlow* (versions 1.x to 6.0); that history is summarised below.

## 1.0 — first public release (Vita Edition)

### New name and look
- **VitaFlow becomes Area Play.** New bubble ("Area Play Vita Edition"), new faceted "A" logo, new LiveArea banner and smoother LiveArea background.
- **Animated start-up screen**: the edges of the logo light up from the bottom to the top, then the facets fill and the title appears. The loading bar is visible from the start.
- **Settings > Appearance > Start-up wallpaper**: show the LiveArea artwork behind the start-up animation (Yes) or keep your wallpaper (No).
- Settings, collections and covers of VitaFlow are taken over automatically on first start (`ux0:data/VitaFlow` → `ux0:data/AreaPlay`). Area Play installs next to VitaFlow (new Title ID `ARPL00001`).

### Added
- **Catalogue view**: carousel with an information band, Down opens the grid with a short animation.
- **Christmas theme** (offered from 1 December to 6 January): winter backgrounds, falling snow, garland, holly, tree / gift / snowflake icons, gentle celesta sounds.
- **Settings > Network**: connection state, IP address, connection test; Wi-Fi bars show the real signal (touch them to open the Network page).
- **About page**: version, credits, licences, transparency note.
- **Battery**: orange at 20 %, red at 10 %, lightning bolt while charging.
- **PSP ISO / CSO without a bubble** launched through the RetroFlow Adrenaline Launcher bubble when it and AdrBubbleBooter are installed (optional).
- **English interface**, switchable at any time (Settings > Display > Language). More languages can be added with a simple text file, without recompiling.
- **New search screen**: on-screen keyboard on the left, results with cover and system on the right, updated at every letter.
- **Rename a game** (SELECT > Rename...). Covers are then searched with the new name.
- **Cover region** (Europe, France, USA or Japan first) and **other cover version** (choose among every matching cover on the server).
- **Remove a system**: START on its tile.
- Battery percentage, version in the bottom-right corner, optional FPS counter.

### Improved
- Navigation sounds without crackling.
- Left menu: full names (Collections, Homebrews), cleaner glossy reflections, Launch button with its shadow and glow.
- Game sheet: cover lower, hints on the right, a single "Close" hint.
- Covers found even with typos, roman numerals (II = 2), missing subtitles or dash-separated file names.
- Keyboard: Square deletes, Triangle adds a space, SELECT clears all; the right stick moves the cursor.
- Emu4Vita++ cores always listed, even with a very large number of RetroArch cores.

## VitaFlow 5.x — retro systems and emulators (private)

- **5.9** New keyboard (AZERTY page + numbers / punctuation / accents), cover region, other cover version, better title matching.
- **5.8** Covers found despite small typos in file names.
- **5.7** Standalone emulators detected and offered as cores: DaedalusX64 (N64), Flycast (Dreamcast), DSVita (DS), Azahar (3DS), EasyRPG. `ux0:roms` folder created with a README.
- **5.6** Visible covers are loaded before background preloading.
- **5.5** Disk cache for decoded covers (instant reload), twice as many covers kept in memory.
- **5.4** PS1 games recognised (bubble ID, disc ID, EBOOT category) and grouped in a PS1 system; PS1 EBOOTs without a bubble launched through RetroArch; **Emu4Vita++** support; START no longer quits.
- **5.3** System order (Square on a tile), dedicated image folders, replaceable icons folder, 26 accent colours, full-screen game backgrounds.
- **5.2** Choose a game's cover and background from your own images.
- **5.1** Large system images accepted, image picker across folders, cores filtered per system, 4 parallel downloads.
- **5.0** **Retro games through RetroArch**: ROMs listed per system, launched directly with the chosen core, covers from libretro-thumbnails.

## VitaFlow 4.x — interface and performance (private)

- **4.9** PSP and PS1 games launched through their LiveArea bubbles.
- **4.8** Search box in the top bar, large touch keyboard.
- **4.7** Search, navigation sounds (replaceable WAV files), Japanese titles, replaceable icons.
- **4.6** Smooth start-up screen, menu fades in already complete.
- **4.5 / 4.4** Stable display: start-up preloading, fade-in for late images, GPU-friendly backgrounds.
- **4.3** Large libraries (up to 2,000 apps), images kept in memory only near the selection.
- **4.1 / 4.2** Background image loading, memory fixes, FPS counter, anti-aliasing option.
- **4.0** **Wallpapers** (8 included + your own), **Collections**, rounded vector interface, glass menus.

## VitaFlow 1.x – 3.x — foundations (private)

- Native launcher (VitaSDK + vita2d) for PS Vita games, PSP bubbles and homebrews.
- Carousel, grid and list views, per menu.
- Favourites, sorting, custom systems, hidden games, game details (launch count, play time).
- Touch screen support.
- Automatic cover download (hexflow-covers, same source as RetroFlow).
- LiveArea bubble.
