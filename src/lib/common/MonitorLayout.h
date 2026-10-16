/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "base/DirectionTypes.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

/**
 * @brief Per-monitor spatial layout and pointer routing.
 *
 * Every computer reports its monitors in its own local input coordinate space
 * (the space used for enter/mouse-move on that computer). The user places each
 * monitor independently in one shared global layout (logical units, i.e. pixels
 * divided by the OS display scale). The router maps pointer motion that leaves a
 * monitor to the monitor that touches that edge segment in the global layout,
 * on whatever computer owns it.
 *
 * Routing policy (see docs/user/monitor-layout.md):
 *  - Two monitors are neighbours across an edge only when their edges touch
 *    exactly in the global layout and the crossing point lies on the shared
 *    part of the edge. Gaps and non-shared parts of an edge are walls.
 *  - Overlapping monitors are a validation error; if present the router picks
 *    the candidate with the lowest (computer, id) so behaviour is deterministic.
 *  - Where the global layout has no neighbour across an edge, the owning OS's own
 *    display arrangement applies (the pointer may move into a locally adjacent
 *    monitor, otherwise it stops at the edge).
 *  - Only monitors that are both live (reported by a connected computer) and placed
 *    take part in routing. A computer with no placed live monitor is "unrouted" and
 *    the caller falls back to the legacy computer-level links.
 *
 * This module is pure C++ so it can be shared by the core and the GUI and unit
 * tested without a display.
 */
namespace deskflow::monitors {

//! Monitor id used for a computer that cannot enumerate monitors (legacy peers).
inline constexpr auto kWholeDesktopId = "desktop";

struct Rect
{
  int32_t x = 0;
  int32_t y = 0;
  int32_t w = 0;
  int32_t h = 0;

  int32_t right() const
  {
    return x + w;
  }
  int32_t bottom() const
  {
    return y + h;
  }
  bool isValid() const
  {
    return w > 0 && h > 0;
  }
  bool contains(int32_t px, int32_t py) const
  {
    return px >= x && px < x + w && py >= y && py < y + h;
  }
  bool intersects(const Rect &o) const
  {
    return x < o.right() && o.x < right() && y < o.bottom() && o.y < bottom();
  }
  bool operator==(const Rect &) const = default;
};

//! What a computer reports about one of its monitors.
struct MonitorInfo
{
  std::string id;         ///< stable identity (RandR output name, Windows device interface id, ...)
  std::string label;      ///< human readable name for the editor
  Rect local;             ///< bounds in the owning computer's input coordinate space
  int32_t pixelWidth = 0; ///< native resolution; 0 means same as local bounds
  int32_t pixelHeight = 0;
  int32_t scalePercent = 100; ///< OS display scale; 100 == 96 dpi
  int32_t rotation = 0;       ///< degrees (0/90/180/270), informational
  bool primary = false;

  bool operator==(const MonitorInfo &) const = default;
};

//! A monitor as persisted in settings: last known info plus its global placement.
struct StoredMonitor
{
  MonitorInfo info;
  bool placed = false;
  int32_t gx = 0;
  int32_t gy = 0;

  bool operator==(const StoredMonitor &) const = default;
};

//! A live monitor positioned in the global layout.
struct PlacedMonitor
{
  std::string computer;
  MonitorInfo info;
  Rect global;
};

//! Size of a monitor in the global layout (logical units): native resolution divided by OS scale.
//! Mapping between local and global coordinates is proportional, so this is correct whether the
//! local input space is physical pixels or DPI-virtualized coordinates.
int32_t logicalExtent(int32_t pixels, int32_t scalePercent);
Rect globalRect(const MonitorInfo &info, int32_t gx, int32_t gy);

//! A single monitor covering the whole desktop shape (used for legacy peers).
MonitorInfo wholeDesktop(int32_t x, int32_t y, int32_t w, int32_t h);

//! @name Serialization
//! Records are `key=value` pairs separated by ';' with %-escaping of values.
//! Unknown keys are ignored and malformed records are skipped, so newer peers
//! can add fields without breaking older ones.
//@{
std::string serializeMonitor(const MonitorInfo &info);
std::optional<MonitorInfo> parseMonitor(const std::string &record);
//! Newline separated list (protocol DMON payload and IPC payload)
std::string serializeMonitors(const std::vector<MonitorInfo> &monitors);
std::vector<MonitorInfo> parseMonitors(const std::string &text);
std::string serializeStored(const StoredMonitor &stored);
std::optional<StoredMonitor> parseStored(const std::string &record);
//@}

//! Snap a dragged rectangle to the edges of other rectangles within tolerance.
//! Edges are snapped so they touch (adjacency) and aligned (flush top/bottom or left/right).
Rect snapRect(const Rect &moving, const std::vector<Rect> &others, int32_t tolerance);

class MonitorLayout
{
public:
  enum class StepKind
  {
    Unrouted, ///< computer is not part of the layout; caller should use legacy routing
    Stay,     ///< pointer stays on its monitor (or OS-local move) at (x, y)
    Local,    ///< pointer moves to another monitor of the same computer at (x, y)
    Remote,   ///< pointer crosses to another computer at (x, y)
    Blocked   ///< pointer hit a wall; (x, y) is clamped onto the source monitor
  };

  struct Step
  {
    StepKind kind = StepKind::Unrouted;
    std::string computer;  ///< destination computer
    std::string monitorId; ///< destination monitor
    int32_t x = 0;
    int32_t y = 0;
    Direction dir = Direction::NoDirection; ///< edge that was crossed, if any
    int32_t clampX = 0;                     ///< position clamped onto the source monitor
    int32_t clampY = 0;
  };

  enum class IssueKind
  {
    Overlap,  ///< two placed monitors overlap
    Isolated, ///< a placed monitor shares no edge with any other placed monitor
    Unplaced, ///< a live monitor has no placement and is not routable
    Offline   ///< a placed monitor is not currently reported by its computer
  };

  struct Issue
  {
    IssueKind kind;
    std::string computerA;
    std::string monitorA;
    std::string computerB;
    std::string monitorB;
  };

  void clear();

  //! @name Placements (persisted user layout)
  //@{
  void setPlacement(const std::string &computer, const std::string &monitorId, int32_t gx, int32_t gy);
  void removePlacement(const std::string &computer, const std::string &monitorId);
  void clearPlacements(const std::string &computer);
  void clearPlacements();
  bool hasPlacement(const std::string &computer, const std::string &monitorId) const;
  std::optional<std::pair<int32_t, int32_t>> placement(const std::string &computer, const std::string &monitorId) const;
  //! Load placements from stored records (only records with placed == true are used).
  void setStored(const std::string &computer, const std::vector<StoredMonitor> &stored);
  //@}

  //! @name Live monitors (reported by connected computers)
  //@{
  void setLiveMonitors(const std::string &computer, const std::vector<MonitorInfo> &monitors);
  void removeLiveMonitors(const std::string &computer);
  const std::vector<MonitorInfo> &liveMonitors(const std::string &computer) const;
  //@}

  //! Live monitors that are placed, i.e. take part in routing.
  std::vector<PlacedMonitor> routableMonitors() const;
  std::vector<PlacedMonitor> routableMonitors(const std::string &computer) const;
  bool isRouted(const std::string &computer) const;

  //! Directions in which any routable monitor of \p computer has a neighbour on another computer.
  //! Bits are `1 << Direction`, matching DirectionMask.
  uint32_t remoteSides(const std::string &computer) const;

  /**
   * @brief Route one pointer motion step.
   *
   * @param computer computer the pointer is currently on
   * @param fromX,fromY last known pointer position (local coordinates)
   * @param toX,toY new pointer position (may be outside every monitor)
   * @param zone when > 0, a pointer within \p zone pixels of an edge counts as
   *        pushing against that edge (used for the server's own computer, where
   *        the OS clamps the cursor and it can never actually leave the edge)
   * @param entryInset distance from the entered edge at which the pointer is
   *        placed on the destination monitor (avoids an immediate jump back)
   */
  Step route(
      const std::string &computer, int32_t fromX, int32_t fromY, int32_t toX, int32_t toY, int32_t zone = 0,
      int32_t entryInset = 0
  ) const;

  //! Placement/geometry problems, for the editor and for logging at startup.
  std::vector<Issue> validate() const;

  static const char *dirName(Direction dir);

private:
  struct Key
  {
    std::string computer;
    std::string id;
    bool operator<(const Key &o) const
    {
      return computer != o.computer ? computer < o.computer : id < o.id;
    }
  };

  const PlacedMonitor *
  sourceMonitor(const std::vector<PlacedMonitor> &own, int32_t fromX, int32_t fromY, int32_t toX, int32_t toY) const;
  const PlacedMonitor *
  neighbour(const std::vector<PlacedMonitor> &all, const PlacedMonitor &src, Direction dir, double along) const;
  bool crossEdge(
      const std::vector<PlacedMonitor> &all, const PlacedMonitor &src, Direction dir, int32_t edgeX, int32_t edgeY,
      int32_t toX, int32_t toY, int32_t entryInset, Step &step
  ) const;

  std::map<Key, std::pair<int32_t, int32_t>> m_placements;
  std::map<std::string, std::vector<MonitorInfo>> m_live;
};

} // namespace deskflow::monitors
