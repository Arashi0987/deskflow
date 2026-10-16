# Monitor layout

Deskflow normally treats each computer as one rectangle, and you link the rectangles side by side. With a monitor
layout, each monitor of each computer is placed on its own, like the display arrangement in your OS settings. The
pointer moves from monitor to monitor wherever their edges touch, even between monitors that belong to different
computers.

## Setting it up

1. Start the server and connect the clients. Each computer reports its monitors (Deskflow with protocol 1.9 or newer
   on Windows and X11; other systems show up as a single "Desktop" monitor).
2. On the server, open **Configure Server → Monitor layout**.
3. Click **Place unplaced monitors** to start from each computer's own arrangement, then drag the monitors where
   they physically are. Edges snap together. Use the arrow keys to nudge the selected monitor by one unit
   (Shift: 10).
4. Tick **Route the pointer between individual monitors using this layout** and save.

Sizes are in logical units (pixels divided by display scaling), so monitors with different resolutions or
scaling line up the way they look on the desk. Portrait monitors are shown upright.

## How the pointer moves

* It crosses an edge only where that edge touches another monitor. With a partly shared edge, only the touching
  part is a doorway.
* Gaps between monitors and edge parts that touch nothing are walls.
* Overlapping monitors are shown in red and should be moved apart.
* If no monitor touches an edge in the layout, the computer's own display arrangement still applies.
* Switch delay, double tap, corners and lock to computer work as before.

## Changes, unplugged monitors and older versions

* Placements are kept across reconnects and restarts. A monitor that is unplugged stays in the layout as *offline*.
  When it comes back, it takes its old place. Right-click → *Forget this monitor* removes it for good.
* Newly connected monitors appear dashed as *not placed* until you drag them or click **Place unplaced monitors**.
* A computer with no placed monitors, or one running an older Deskflow, uses the links from the **Computers** tab.
* Unticking the checkbox returns to computer links without losing the layout.

The editor never changes the computers' own display settings.
