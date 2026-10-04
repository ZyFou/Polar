# Polar
A simple, powerful WT Performance Analyser for Dragon Ball Z : Dokkan Battle.

## Features
**_- :rocket: Fast & Easy to use_**

**_- :art: Highly customisable & User-friendly UI_**

**_- :bar_chart: Produces high-quality graphs_**

**_- :earth_africa: Multilingual support_**

**_- :arrows_counterclockwise: Auto-refresh with extra insights for the top leaderboard_**

## Gallery


<img width="1291" height="785" alt="image" src="https://github.com/user-attachments/assets/af67b7d2-3c4e-4813-8240-0af1b0065fc3" />
<img width="1251" height="744" alt="image" src="https://github.com/user-attachments/assets/6c12aae4-c82a-4025-acb5-45535609f60a" />
<img width="1295" height="781" alt="image" src="https://github.com/user-attachments/assets/cd0fcfd0-fb33-41a4-8de8-24de3a5e3cf3" />




## Installation

Check the releases.

## Build

If you want, you can build your own version of Polar.
To do so, you need to download it using [QT](https://qt.io).
I made this client using my own static QT build (v6.8.0), which allows me to compile it and to share it without you needing to install QT.
A dynamic Qt build can be distributed by bundling its required Qt libraries and plugins (for example with windeployqt on Windows). For a single executable, use a separately configured static Qt toolchain; CONFIG += static alone does not turn a dynamic Qt installation into a static one.
Feel free to contact us (check [contact](#Contact)) if you need any help to compile it.

### Build for (Arch) linux

Here are the steps i followed to compile Polar (Arch Linux).
First, download these packages :
```bash
sudo pacman -S qt6 qt6-base qt6-charts
```

Then, clone this repo :
```bash
git clone https://github.com/darkruss48/Polar
cd Polar
```
Create the build folder and cd into :
```bash
mkdir build
cd build
```

Translations are compiled and embedded by qmake; no source-tree `.qm` files are required.

Finally, use qmake and make to compile Polar :
```bash
/usr/lib/qt6/bin/qmake ..
make
```
To start the client :
```bash
./Polar
```
**Note**: If you use Nixos or Nix in general, simply do
```bash
nix build .#polar
```
**:warning: If you're using Arch, there are great chances that QT 5.x is already installed on your device. Polar use QT 6.x, so use `/usr/lib/qt6/bin/qmake ..` instead of `qmake ..` command.**

Polar can be built with Qt 5.x, but don’t expect the program to function as it should. Consider using QT 6.x.

## Contributors

This project was made possible thanks to the hard work and dedication of the following individuals:

- **Polo** : [GitHub Profile](https://github.com/polowiper)
- **Darkruss** : [GitHub Profile](https://github.com/darkruss48)

Thanks to Clєтυн26 for translating Polar in Spanish.
Thanks to LuCaPigeon for translating Polar in Italian.

## Contact
Contact me on [Twitter](https://twitter.com/darkruss47) or reach me on discord : darkruss (or polo as well)

## License
Polar is released under the [MIT License](https://choosealicense.com/licenses/mit/).

## Source layout

- `src/app`: application entry point
- `src/core`: settings and domain logic
- `src/network`: API and update transport
- `src/ui`: native widgets and chart rendering
- `ui`: Qt Designer forms
- `resources/images`: bundled artwork (resource aliases remain unchanged)
- `translations`: Linguist catalogs

## Leaderboard views

Use **Expanded view** on the Leaderboard page to see all 100 players across the
page, with scoring pace, active/idle time, finish scenarios, gaps and catch-up
estimates. Both views share the same refresh and scheduled quarter-hour updates.
Selection and scroll position survive refreshes; rank changes animate, with new
entrants and departures moving through the bottom of the list.

The optional background follows the selected player's wins/hour, from hour zero
to the tournament end. Choose a 1/2/6-hour scoring window and an additional future
pause for the selected player. Projections use actual sample timestamps, exclude
missing intervals and score resets, and stop when live data are stale. Finish
scenarios compare recent pace with observed pace; they are not probabilities.

Tournament `0` always means **Current**, even when metadata omits its edition
number. Archive numbers come from the public archive catalog. Current cannot be
mixed with numbered archives in one comparison range. Rank goals use only older,
completed tournaments. If the current edition has no number, forecasts use
verified archive dates instead of inventing an edition number.

## Settings and notifications

The new UI style applies to every page and to settings. The style and control
transparency options preview immediately; Cancel restores the previous appearance.
Mouse interactions do not leave a native focus outline, while keyboard navigation
keeps an explicit focus indicator. Notifications slide in and out at the bottom
left, with a global switch, per-category switches and durations from 2 to 120
seconds. The notification history shows concise messages without request URLs.

On Windows, Start menu settings distinguish a valid shortcut from one pointing to
another directory or executable, and provide a repair action. Update checks run
at startup and every hour and compare release tags numerically. A newer release
is offered through an in-app Yes/No notification. The verified executable is
staged beside the client; a separate helper waits for its process to exit, replaces
the file at the same path, then restarts it with its original arguments. Size,
PE header and release SHA-256 (when provided) are checked. Settings and renamed
executable paths are preserved; a failed restart restores the previous executable.
Other platforms open the release page only on request and keep their source-built
client unchanged.

The API transport is asynchronous, reuses one connection manager and coalesces
identical in-flight requests. Batch comparisons allow four concurrent requests
and at most 40 series. Successful responses have a short bounded cache; explicit
leaderboard refresh bypasses it. No Qt WebEngine, WebView or new runtime module
is required.

## Tests (Qt 6)

```sh
mkdir -p build/tests
cd build/tests
qmake6 ../../tests/tests.pro
make -j4
QT_QPA_PLATFORM=offscreen ./polar_tests
```

Tests cover metadata without an id, region catalogs, series validation, real-time
intervals, missing samples, large scores, projection boundaries, asynchronous
request coalescing and bounded batches, Current/rank-1 goals without an edition
number, translation, animated list reconciliation, notification actions and expiry,
live settings, release integrity, graph replacement and bundled resources. Windows
tests also exercise shortcut repair and executable replacement with rollback. Offscreen plugin size-hint warnings are
expected. Windows static packaging and live tournament accuracy still require
validation on the target platform.
