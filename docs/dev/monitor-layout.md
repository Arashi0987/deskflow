# Per-monitor layout: architecture, compatibility, build and deployment

Branch `feature/per-monitor-layout` of <https://github.com/Arashi0987/deskflow>, based on `v1.27.0`.
User documentation: [docs/user/monitor-layout.md](../user/monitor-layout.md).

## What upstream 1.27.0 can and cannot do

Upstream links *computers*: each computer is one rectangle, the bounding box of all of its monitors (`getShape()`).
A link connects one side of that rectangle to another computer, optionally over a fraction of the side
(`left(0,50) = other`). The pointer leaves a computer only at the outer edge of its bounding box, and the protocol
(1.8) carries one shape per computer (`DINF`). Nothing in the core, the protocol or the GUI knows about individual
monitors. So there is no native per-monitor routing. This branch adds it to all three layers rather than drawing an
editor on top of computer links.

## Architecture

| Layer | Files | Role |
|---|---|---|
| Model and router | `src/lib/common/MonitorLayout.{h,cpp}` | Pure C++: monitor info, global placements, edge-segment routing, validation, serialization, snapping. Shared by core and GUI. |
| Enumeration | `platform/MSWindowsComputer.cpp`, `platform/XWindowsComputer.cpp`, `deskflow/IComputer.h` | `getMonitors()`: Windows `EnumDisplayMonitors` + `EnumDisplayDevicesW(EDD_GET_DEVICE_INTERFACE_NAME)` id + `GetDpiForMonitor` scale; X11 RandR 1.5 `XRRGetMonitors` (output name as id, CRTC rotation). Other platforms report one whole-desktop monitor. |
| Protocol | `deskflow/ProtocolTypes.*`, `client/ServerProxy1_9.*`, `server/ClientProxy1_9.*`, `server/ClientProxyUnknown.cpp` | Protocol 1.9 `DMON` message, sent by a client immediately before every `DINF`. |
| Server | `server/Server.{h,cpp}`, `server/PrimaryClient.*` | Loads placements, tracks live monitors per computer, and routes primary and secondary pointer motion through the router when `server/monitorLayout` is on. Sends the topology to the GUI over IPC. |
| GUI | `gui/config/MonitorTopology.*`, `gui/widgets/MonitorLayoutView.*`, `gui/dialogs/ServerConfigDialog.*`, `gui/core/CoreProcess.*`, `gui/MainWindow.cpp` | Persists the topology, merges IPC reports, and adds the "Monitor layout" tab with the draggable canvas. |

### Coordinates

* **Local**: a computer's own input coordinate space (the one `DINF`, `enter` and `mouseMove` use). On Windows this is
  the per-monitor DPI-aware virtual screen (physical pixels); on X11 it is the root window.
* **Global**: the shared layout, in logical units = native pixels × 100 / scale%. A 3840×2160 monitor at 200% is
  1920×1080 units, the same size as a 1080p monitor at 100%. That is what makes a pointer crossing at the same physical
  height line up across mixed-DPI monitors.
* Local↔global mapping is proportional per monitor, so it is correct whether a platform reports physical or
  DPI-virtualized coordinates. Negative local coordinates (monitors left of or above the primary) are normal and
  covered by tests.

### Routing policy

Implemented in `MonitorLayout::route()`, covered by `MonitorLayoutTests`:

1. The source monitor is the live, placed monitor of the current computer that contains the last pointer position.
2. When motion leaves that monitor through an edge, the router looks in the global layout for a monitor whose opposite
   edge **touches exactly** and whose shared segment contains the crossing point (computed on the global edge).
   * A neighbour on another computer gives `Remote`: the server switches computers, and the entry point is mapped
     proportionally onto the destination monitor and placed the jump-zone inset away from its edge.
   * A neighbour on the same computer gives `Local`: the server warps if the OS arrangement differs from the layout.
   * No neighbour: the OS arrangement applies if it has a monitor there (`Stay`), otherwise it is a wall (`Blocked`,
     clamped to the source monitor).
3. Gaps and the non-shared parts of a partially shared edge are walls. Nothing teleports across empty space.
4. Overlapping monitors are a validation error (logged by the server, red in the editor). If they are present
   anyway, the lowest (computer, id) wins, so behaviour is deterministic.
5. For the server's own cursor the OS clamps at the desktop edge, so a pointer within the jump zone of an outer edge
   counts as pushing through it. Inner OS edges, where Windows/X11 already moved the cursor onto the next local
   monitor, are recognised from the previous position and rerouted if the layout says so.
6. Switching goes through the existing `isSwitchOkay()` / `switchComputer()` path. Switch delay, double tap, dead
   corners, lock-to-computer, keyboard focus and modifier-state handling are unchanged.
7. A computer with no live placed monitor is *unrouted* and keeps its legacy computer links. That covers 1.8 peers,
   computers not placed yet, and a computer whose placed monitors are all unplugged.

### Persistence

* `server/monitorLayout` (bool, default `false`) switches routing on. When it is off, nothing changes from 1.27.0.
* `computer_<name>/monitors` holds a string list, one record per monitor:
  `id=…;label=…;x=…;y=…;w=…;h=…;pw=…;ph=…;scale=…;rot=…;primary=…[;gx=…;gy=…]`. `gx`/`gy` are present only when
  the monitor is placed. Values are %-escaped, unknown keys are ignored and malformed records are skipped with a
  warning.
* The GUI merges every IPC `monitorTopology` report into these records. Info is refreshed and new monitors are added
  unplaced; placements are never moved or removed by a report. That is what makes hotplug, reconnect and restart
  keep the layout. Unplugged monitors stay in the editor as "offline" until forgotten.
* Monitor ids are stable across reboots: RandR output names on X11, and the device interface path on Windows
  (fallback `\\.\DISPLAYn`).

## Protocol and version compatibility

`kProtocolMinorVersion` is now 9. The server greets with 1.9. A client on 1.8 or older answers with its own
version and gets a `ClientProxy1_8` (existing negotiation). A 1.9 client talking to a 1.8 server sees the 1.8 hello
and behaves as 1.8: no `DMON`.

| Server | Client | Result | Verified |
|---|---|---|---|
| this branch, layout on | this branch | per-monitor routing | sim `layout` |
| this branch, layout off | this branch | computer links, as in 1.27.0 | sim `legacy` |
| this branch, layout on | stock 1.27.0 | client is one whole-desktop monitor, reached via its computer link | sim `old-client` |
| stock 1.27.0 | this branch | negotiates 1.8, computer links | sim `old-server` |

Settings migration: none is needed, because existing computer-level configs load unchanged. Stock 1.27.0 deletes the
unknown `server/monitorLayout` key from the `server` group when it loads the settings
(`Settings::cleanSettings()`), but leaves `computer_<name>/monitors` alone. After a downgrade and re-upgrade the
placements are still there and only the checkbox needs ticking again (sim `old-server` checks both).

## Build and test (Docker Compose, source mounted)

```sh
# from the repository root, on any Docker host (queen.home was used)
docker compose -f dev/docker/compose.yml build            # deskflow-dev:per-monitor image (Debian, Qt 6.8)
docker compose -f dev/docker/compose.yml run --rm build   # cmake + ninja into ./build
docker compose -f dev/docker/compose.yml run --rm test    # ctest over src/unittests
```

Two-computer routing simulation. This is a real `deskflow-core` server and client on two Xvfb displays with RandR
monitors, with `xdotool` driving the pointer:

```sh
docker compose -f dev/docker/compose.yml run --rm shell bash dev/sim/monitor-layout-sim.sh layout   # or legacy
# old-client / old-server need a stock 1.27.0 core in build/legacy-build:
git archive --prefix=legacy-src/ v1.27.0 | tar -x -C build
docker compose -f dev/docker/compose.yml run --rm shell bash -c \
  "cmake -S /build/legacy-src -B /build/legacy-build -G Ninja -DBUILD_TESTS=OFF && cmake --build /build/legacy-build --target deskflow-core"
docker compose -f dev/docker/compose.yml run --rm shell bash dev/sim/monitor-layout-sim.sh old-client  # or old-server
```

Render the editor (the same widget the dialog uses) to a PNG:

```sh
docker compose -f dev/docker/compose.yml run --rm -e DESKFLOW_MONITOR_LAYOUT_PNG=/build/monitor-layout.png shell \
  bash -c "cd /build/src/lib/gui && /build/src/unittests/gui/widgets/MonitorLayoutViewTests"
```

Windows and macOS builds and packages come from the fork's CI (draft PR #1 against `base/v1.27.0`).

## What is simulated and what is live

* **Simulated (automated):** geometry and routing (unit tests), the protocol in both directions including 1.8
  peers, server routing with X11 on both ends, reconnect, settings round trip, and the editor's drag, snap and
  render.
* **Built but not run:** Windows monitor enumeration and the Windows server cursor path. They are compiled by CI
  only, with no Windows machine in the loop.
* **Not done:** a live mona (Windows server) ↔ queen (X11 client) session with the fork. That needs the deployment
  below.

## Deployment (handoff; not performed)

Do this when input sharing can be down for a few minutes. Keep a second keyboard and mouse on each machine.

1. Take the artifacts from the fork's CI run for the branch head: `deskflow-*-win-x64.msi` and the Flatpak bundle
   (or build a native Linux binary with the compose file above).
2. **Back up on mona:** `%APPDATA%\Deskflow\Deskflow.conf`, `C:\ProgramData\Deskflow\deskflow-server.conf`,
   `C:\ProgramData\Deskflow\tls\`.
   **Back up on queen:** `~/.var/app/org.deskflow.deskflow/config/Deskflow/` (includes `tls/`).
3. Upgrade the **client (queen)** first. A 1.9 client works with the 1.27.0 server (sim `old-server`), so this step
   alone changes nothing visible. Note the current commit first with
   `flatpak info --user org.deskflow.deskflow | grep Commit`, then run
   `flatpak install --user --reinstall ./deskflow-*.flatpak`.
4. Upgrade the **server (mona)**: run the MSI over the 1.27.0 install. The settings, TLS certificate and fingerprints
   are kept because the paths are unchanged.
5. On mona, open *Configure Server → Monitor layout*. Every monitor of mona and queen appears once queen is connected.
   Click *Place unplaced monitors* or drag them, tick *Route the pointer between individual monitors*, and save. The
   core restarts and the log shows `per-monitor layout enabled, N monitor placement(s) loaded`.
6. Check (under a minute): move the pointer across each touching edge, including the partial ones, and confirm it
   lands on the monitor the editor shows. Move it into a gap or an unshared edge part, where it should stop.

## Rollback

* **Soft:** untick *Route the pointer between individual monitors* and save. Routing is back to computer links
  immediately, with the 1.27.0 code path.
* **Full:** on mona, reinstall the official 1.27.0 MSI from the upstream release page over the fork. On queen, run
  `flatpak install --user --reinstall flathub org.deskflow.deskflow`, then
  `flatpak update --user --commit=<commit noted in step 3> org.deskflow.deskflow` to pin 1.27.0 again. Restore the
  backups from step 2 only if something is missing. 1.27.0 reads these settings as they are and ignores the monitor
  records.

## Known limitations

* macOS, Wayland/libei and other backends report one whole-desktop monitor, so they take part as one rectangle.
* The editor does not refresh while the dialog is open. Reopen it to see newly reported monitors.
* Renaming a computer drops its stored monitor records (they are keyed by computer name).
* A Windows server process that is not per-monitor DPI aware would report virtualized coordinates. The
  proportional mapping handles that, but it has not been checked live.
