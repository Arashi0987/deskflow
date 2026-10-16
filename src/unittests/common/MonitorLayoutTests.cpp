/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MonitorLayoutTests.h"

#include "common/MonitorLayout.h"

#include <cstdlib>

using namespace deskflow::monitors;
using Kind = MonitorLayout::StepKind;

namespace {

MonitorInfo mon(const std::string &id, int32_t x, int32_t y, int32_t w, int32_t h, int32_t scale = 100)
{
  MonitorInfo info;
  info.id = id;
  info.local = {x, y, w, h};
  info.scalePercent = scale;
  return info;
}

// A: one 1000x1000 monitor at the origin. B: one 1000x500 monitor placed to its right, offset down by 250.
MonitorLayout partialPair()
{
  MonitorLayout layout;
  layout.setLiveMonitors("A", {mon("A1", 0, 0, 1000, 1000)});
  layout.setLiveMonitors("B", {mon("B1", 0, 0, 1000, 500)});
  layout.setPlacement("A", "A1", 0, 0);
  layout.setPlacement("B", "B1", 1000, 250);
  return layout;
}

// The demo topology used in the docs and the GUI demo:
//   mona  (Windows): M3 portrait left of M1 (150%), M2 right of M1 in the OS arrangement
//   queen (X11):     Q1 landscape, Q2 portrait to its right in the OS arrangement
// Global layout interleaves them: M3 | M1 | Q1 | M2, with Q2 placed under Q1.
MonitorLayout demoLayout()
{
  MonitorLayout layout;
  layout.setLiveMonitors(
      "mona", {mon("M1", 0, 0, 2560, 1440, 150), mon("M2", 2560, 0, 1920, 1080), mon("M3", -1080, -240, 1080, 1920)}
  );
  layout.setLiveMonitors("queen", {mon("Q1", 0, 0, 1920, 1080), mon("Q2", 1920, 0, 1080, 1920)});
  layout.setPlacement("mona", "M3", -1080, -240);
  layout.setPlacement("mona", "M1", 0, 0);
  layout.setPlacement("queen", "Q1", 1707, 0);
  layout.setPlacement("mona", "M2", 3627, 0);
  layout.setPlacement("queen", "Q2", 1707, 1080);
  return layout;
}

} // namespace

void MonitorLayoutTests::logicalExtent_scalesByDpi()
{
  QCOMPARE(logicalExtent(1000, 100), 1000);
  QCOMPARE(logicalExtent(2560, 150), 1707);
  QCOMPARE(logicalExtent(3840, 200), 1920);
  QCOMPARE(logicalExtent(1920, 0), 1920); // invalid scale treated as 100%
  QCOMPARE(globalRect(mon("x", -5, -5, 2560, 1440, 150), 10, 20), (Rect{10, 20, 1707, 960}));
}

void MonitorLayoutTests::snapRect_snapsToAdjacentEdge()
{
  const Rect snapped = snapRect({1006, 3, 500, 500}, {{0, 0, 1000, 1000}}, 10);
  QCOMPARE(snapped, (Rect{1000, 0, 500, 500}));

  // flush bottom edges
  const Rect bottom = snapRect({-498, 495, 500, 500}, {{0, 0, 1000, 1000}}, 10);
  QCOMPARE(bottom, (Rect{-500, 500, 500, 500}));
}

void MonitorLayoutTests::snapRect_leavesFarRectAlone()
{
  const Rect far{3000, 3000, 500, 500};
  QCOMPARE(snapRect(far, {{0, 0, 1000, 1000}}, 10), far);
}

void MonitorLayoutTests::route_unplacedComputer_isUnrouted()
{
  MonitorLayout layout;
  layout.setLiveMonitors("A", {mon("A1", 0, 0, 1000, 1000)});
  QCOMPARE(layout.route("A", 10, 10, 2000, 10).kind, Kind::Unrouted);
  QVERIFY(!layout.isRouted("A"));
  QCOMPARE(layout.route("nobody", 0, 0, 1, 1).kind, Kind::Unrouted);
}

void MonitorLayoutTests::route_insideMonitor_stays()
{
  const auto layout = partialPair();
  const auto step = layout.route("A", 10, 10, 20, 30);
  QCOMPARE(step.kind, Kind::Stay);
  QCOMPARE(step.x, 20);
  QCOMPARE(step.y, 30);
  QCOMPARE(step.monitorId, std::string("A1"));
}

void MonitorLayoutTests::route_horizontal_crossesToRemoteMonitor()
{
  const auto layout = partialPair();
  const auto step = layout.route("A", 999, 500, 1003, 500);
  QCOMPARE(step.kind, Kind::Remote);
  QCOMPARE(step.computer, std::string("B"));
  QCOMPARE(step.monitorId, std::string("B1"));
  QCOMPARE(step.dir, Direction::Right);
  QCOMPARE(step.x, 0);
  QCOMPARE(step.y, 250); // global 500.5 - B's offset 250
}

void MonitorLayoutTests::route_partialEdge_blocksOutsideSharedSegment()
{
  const auto layout = partialPair();
  auto step = layout.route("A", 999, 100, 1003, 100);
  QCOMPARE(step.kind, Kind::Blocked);
  QCOMPARE(step.x, 999);
  QCOMPARE(step.y, 100);
  QCOMPARE(step.dir, Direction::Right);

  step = layout.route("A", 999, 800, 1003, 800);
  QCOMPARE(step.kind, Kind::Blocked);

  // first and last rows of the shared segment both cross
  QCOMPARE(layout.route("A", 999, 250, 1003, 250).kind, Kind::Remote);
  step = layout.route("A", 999, 749, 1003, 749);
  QCOMPARE(step.kind, Kind::Remote);
  QCOMPARE(step.y, 499);
}

void MonitorLayoutTests::route_gap_isAWall()
{
  auto layout = partialPair();
  layout.setPlacement("B", "B1", 1001, 250);
  QCOMPARE(layout.route("A", 999, 500, 1003, 500).kind, Kind::Blocked);
}

void MonitorLayoutTests::route_vertical_crossesDown()
{
  MonitorLayout layout;
  layout.setLiveMonitors("A", {mon("A1", 0, 0, 1000, 1000)});
  layout.setLiveMonitors("B", {mon("B1", 0, 0, 800, 600)});
  layout.setPlacement("A", "A1", 0, 0);
  layout.setPlacement("B", "B1", 100, 1000);

  const auto step = layout.route("A", 500, 999, 500, 1002);
  QCOMPARE(step.kind, Kind::Remote);
  QCOMPARE(step.computer, std::string("B"));
  QCOMPARE(step.dir, Direction::Bottom);
  QCOMPARE(step.x, 400);
  QCOMPARE(step.y, 0);

  // outside B's horizontal span is a wall
  QCOMPARE(layout.route("A", 50, 999, 50, 1002).kind, Kind::Blocked);
}

void MonitorLayoutTests::route_vertical_crossesUp()
{
  MonitorLayout layout;
  layout.setLiveMonitors("A", {mon("A1", 0, 0, 1000, 1000)});
  layout.setLiveMonitors("B", {mon("B1", 0, 0, 800, 600)});
  layout.setPlacement("A", "A1", 0, 0);
  layout.setPlacement("B", "B1", 100, 1000);

  const auto step = layout.route("B", 400, 0, 400, -3);
  QCOMPARE(step.kind, Kind::Remote);
  QCOMPARE(step.computer, std::string("A"));
  QCOMPARE(step.dir, Direction::Top);
  QCOMPARE(step.x, 500);
  QCOMPARE(step.y, 999);
}

void MonitorLayoutTests::route_interleaved_overridesOsNeighbour()
{
  // A's OS arrangement is A1|A2, but the global layout puts B1 between them
  MonitorLayout layout;
  layout.setLiveMonitors("A", {mon("A1", 0, 0, 1000, 1000), mon("A2", 1000, 0, 1000, 1000)});
  layout.setLiveMonitors("B", {mon("B1", 0, 0, 1000, 1000)});
  layout.setPlacement("A", "A1", 0, 0);
  layout.setPlacement("B", "B1", 1000, 0);
  layout.setPlacement("A", "A2", 2000, 0);

  // the OS already moved the pointer into A2; the layout says B1
  const auto step = layout.route("A", 999, 500, 1005, 500);
  QCOMPARE(step.kind, Kind::Remote);
  QCOMPARE(step.computer, std::string("B"));
  QCOMPARE(step.x, 0);
  QCOMPARE(step.y, 500);
}

void MonitorLayoutTests::route_interleaved_remoteToSecondLocalMonitor()
{
  MonitorLayout layout;
  layout.setLiveMonitors("A", {mon("A1", 0, 0, 1000, 1000), mon("A2", 1000, 0, 1000, 1000)});
  layout.setLiveMonitors("B", {mon("B1", 0, 0, 1000, 1000)});
  layout.setPlacement("A", "A1", 0, 0);
  layout.setPlacement("B", "B1", 1000, 0);
  layout.setPlacement("A", "A2", 2000, 0);

  const auto step = layout.route("B", 999, 500, 1004, 500);
  QCOMPARE(step.kind, Kind::Remote);
  QCOMPARE(step.computer, std::string("A"));
  QCOMPARE(step.monitorId, std::string("A2"));
  QCOMPARE(step.x, 1000);
  QCOMPARE(step.y, 500);
}

void MonitorLayoutTests::route_noLayoutNeighbour_fallsBackToOsArrangement()
{
  MonitorLayout layout;
  layout.setLiveMonitors("A", {mon("A1", 0, 0, 1000, 1000), mon("A2", 1000, 0, 1000, 1000)});
  layout.setLiveMonitors("B", {mon("B1", 0, 0, 1000, 1000)});
  layout.setPlacement("A", "A1", 0, 0);
  layout.setPlacement("B", "B1", -1000, 0);
  // A2 is live but not placed: the OS still lets the pointer move onto it

  const auto step = layout.route("A", 999, 500, 1005, 500);
  QCOMPARE(step.kind, Kind::Stay);
  QCOMPARE(step.monitorId, std::string("A2"));
  QCOMPARE(step.x, 1005);
  QCOMPARE(step.y, 500);
}

void MonitorLayoutTests::route_layoutSendsToDifferentLocalMonitor_warpsLocally()
{
  // OS: A1|A2. Layout: A2 placed to the *left* of A1.
  MonitorLayout layout;
  layout.setLiveMonitors("A", {mon("A1", 0, 0, 1000, 1000), mon("A2", 1000, 0, 1000, 1000)});
  layout.setPlacement("A", "A1", 0, 0);
  layout.setPlacement("A", "A2", -1000, 0);

  auto step = layout.route("A", 2, 500, -3, 500);
  QCOMPARE(step.kind, Kind::Local);
  QCOMPARE(step.monitorId, std::string("A2"));
  QCOMPARE(step.x, 1999);
  QCOMPARE(step.y, 500);

  // when the layout agrees with where the OS put the pointer, the OS position is kept
  layout.setPlacement("A", "A2", 1000, 0);
  step = layout.route("A", 999, 500, 1005, 500);
  QCOMPARE(step.kind, Kind::Local);
  QCOMPARE(step.x, 1005);
  QCOMPARE(step.y, 500);
}

void MonitorLayoutTests::route_mixedDpi_mapsProportionally()
{
  // S: 2560x1440 at 150% (1707x960 logical). D: 1920x1080 at 100%, tops aligned.
  MonitorLayout layout;
  layout.setLiveMonitors("S", {mon("S1", 0, 0, 2560, 1440, 150)});
  layout.setLiveMonitors("D", {mon("D1", 0, 0, 1920, 1080)});
  layout.setPlacement("S", "S1", 0, 0);
  layout.setPlacement("D", "D1", 1707, 0);

  auto step = layout.route("S", 2559, 720, 2565, 720);
  QCOMPARE(step.kind, Kind::Remote);
  QCOMPARE(step.y, 480);
  step = layout.route("S", 2559, 1439, 2562, 1439);
  QCOMPARE(step.y, 959);

  step = layout.route("D", 0, 480, -2, 480);
  QCOMPARE(step.kind, Kind::Remote);
  QCOMPARE(step.computer, std::string("S"));
  QCOMPARE(step.x, 2559);
  QCOMPARE(step.y, 720);

  // D extends below S (1080 > 960 logical): that part of D's left edge is a wall
  QCOMPARE(layout.route("D", 0, 1000, -2, 1000).kind, Kind::Blocked);
}

void MonitorLayoutTests::route_dpiVirtualizedLocalSpace_matchesPhysical()
{
  // a DPI-unaware process on Windows sees the 150% monitor as 1707x960 even though it is 2560x1440;
  // the layout size and the routing outcome must not depend on which space the computer reports
  MonitorInfo virtualized = mon("S1", 0, 0, 1707, 960, 150);
  virtualized.pixelWidth = 2560;
  virtualized.pixelHeight = 1440;
  QCOMPARE(globalRect(virtualized, 0, 0), (Rect{0, 0, 1707, 960}));

  MonitorLayout layout;
  layout.setLiveMonitors("S", {virtualized});
  layout.setLiveMonitors("D", {mon("D1", 0, 0, 1920, 1080)});
  layout.setPlacement("S", "S1", 0, 0);
  layout.setPlacement("D", "D1", 1707, 0);

  auto step = layout.route("S", 1706, 480, 1709, 480);
  QCOMPARE(step.kind, Kind::Remote);
  QCOMPARE(step.y, 480);
  step = layout.route("D", 0, 959, -2, 959);
  QCOMPARE(step.kind, Kind::Remote);
  QCOMPARE(step.x, 1706);
  QCOMPARE(step.y, 959);
}

void MonitorLayoutTests::route_portrait_mapsAlongLongEdge()
{
  MonitorLayout layout;
  layout.setLiveMonitors("P", {mon("P1", 0, 0, 1080, 1920)});
  layout.setLiveMonitors("L", {mon("L1", 0, 0, 1920, 1080)});
  layout.setPlacement("P", "P1", 0, 0);
  layout.setPlacement("L", "L1", 1080, 420); // vertically centred on the portrait monitor

  auto step = layout.route("P", 1079, 1000, 1083, 1000);
  QCOMPARE(step.kind, Kind::Remote);
  QCOMPARE(step.x, 0);
  QCOMPARE(step.y, 580);
  QCOMPARE(layout.route("P", 1079, 300, 1083, 300).kind, Kind::Blocked);
  QCOMPARE(layout.route("P", 1079, 1600, 1083, 1600).kind, Kind::Blocked);

  step = layout.route("L", 0, 0, -1, 0);
  QCOMPARE(step.kind, Kind::Remote);
  QCOMPARE(step.x, 1079);
  QCOMPARE(step.y, 420);
}

void MonitorLayoutTests::route_negativeCoordinates()
{
  // Windows-style monitor left of and above the primary's origin
  MonitorLayout layout;
  layout.setLiveMonitors("W", {mon("W1", -1920, -200, 1920, 1080)});
  layout.setLiveMonitors("Q", {mon("Q1", 0, 0, 1920, 1080)});
  layout.setPlacement("W", "W1", 0, 0);
  layout.setPlacement("Q", "Q1", -1920, 0);

  auto step = layout.route("W", -1919, 340, -1925, 340);
  QCOMPARE(step.kind, Kind::Remote);
  QCOMPARE(step.computer, std::string("Q"));
  QCOMPARE(step.x, 1919);
  QCOMPARE(step.y, 540);

  step = layout.route("Q", 1919, 540, 1923, 540);
  QCOMPARE(step.kind, Kind::Remote);
  QCOMPARE(step.computer, std::string("W"));
  QCOMPARE(step.x, -1920);
  QCOMPARE(step.y, 340);
}

void MonitorLayoutTests::route_roundTrip_doesNotTeleport()
{
  MonitorLayout layout;
  layout.setLiveMonitors("S", {mon("S1", 0, 0, 2560, 1440, 150)});
  layout.setLiveMonitors("D", {mon("D1", 0, 0, 1920, 1080)});
  layout.setPlacement("S", "S1", 0, 0);
  layout.setPlacement("D", "D1", 1707, 100);

  for (int32_t y = 150; y < 1440; y += 37) {
    const auto there = layout.route("S", 2559, y, 2563, y);
    QCOMPARE(there.kind, Kind::Remote);
    const auto back = layout.route("D", there.x, there.y, there.x - 3, there.y);
    QCOMPARE(back.kind, Kind::Remote);
    QCOMPARE(back.computer, std::string("S"));
    QVERIFY2(std::abs(back.y - y) <= 1, qPrintable(QStringLiteral("y=%1 came back as %2").arg(y).arg(back.y)));
  }
}

void MonitorLayoutTests::route_zone_pushingOnServerEdgeTriggersRemote()
{
  MonitorLayout layout;
  layout.setLiveMonitors("A", {mon("A1", 0, 0, 1000, 1000)});
  layout.setLiveMonitors("B", {mon("B1", 0, 0, 1000, 1000)});
  layout.setPlacement("A", "A1", 0, 0);
  layout.setPlacement("B", "B1", 1000, 0);

  // the OS clamps the server's cursor at x=999, so it never actually leaves the monitor
  auto step = layout.route("A", 998, 500, 999, 500, 1);
  QCOMPARE(step.kind, Kind::Remote);
  QCOMPARE(step.computer, std::string("B"));
  QCOMPARE(step.y, 500);

  QCOMPARE(layout.route("A", 997, 500, 998, 500, 1).kind, Kind::Stay);
  // without a zone the same position is just a normal move
  QCOMPARE(layout.route("A", 998, 500, 999, 500, 0).kind, Kind::Stay);
  // pushing on an edge without a neighbour stays put
  QCOMPARE(layout.route("A", 1, 500, 0, 500, 1).kind, Kind::Stay);
}

void MonitorLayoutTests::route_zone_localNeighbourDoesNotTrigger()
{
  MonitorLayout layout;
  layout.setLiveMonitors("A", {mon("A1", 0, 0, 1000, 1000), mon("A2", 1000, 0, 1000, 1000)});
  layout.setPlacement("A", "A1", 0, 0);
  layout.setPlacement("A", "A2", 1000, 0);
  QCOMPARE(layout.route("A", 998, 500, 999, 500, 1).kind, Kind::Stay);
}

void MonitorLayoutTests::route_zone_entryInsetAvoidsJumpBack()
{
  MonitorLayout layout;
  layout.setLiveMonitors("A", {mon("A1", 0, 0, 1000, 1000)});
  layout.setLiveMonitors("B", {mon("B1", 0, 0, 1000, 1000)});
  layout.setPlacement("A", "A1", 0, 0);
  layout.setPlacement("B", "B1", 1000, 0);

  const auto in = layout.route("B", 1, 500, -2, 500, 0, 1);
  QCOMPARE(in.kind, Kind::Remote);
  QCOMPARE(in.computer, std::string("A"));
  QCOMPARE(in.x, 998);

  // next event on the server at the entry position must not bounce straight back
  QCOMPARE(layout.route("A", in.x, in.y, in.x, in.y, 1).kind, Kind::Stay);
}

void MonitorLayoutTests::route_diagonalExit_usesFirstEdgeCrossed()
{
  MonitorLayout layout;
  layout.setLiveMonitors("A", {mon("A1", 0, 0, 1000, 1000)});
  layout.setLiveMonitors("B", {mon("B1", 0, 0, 1000, 1000)});
  layout.setLiveMonitors("C", {mon("C1", 0, 0, 1000, 1000)});
  layout.setPlacement("A", "A1", 0, 0);
  layout.setPlacement("B", "B1", 1000, 0);
  layout.setPlacement("C", "C1", 0, 1000);

  const auto step = layout.route("A", 998, 990, 1004, 1001);
  QCOMPARE(step.kind, Kind::Remote);
  QCOMPARE(step.computer, std::string("B"));
  QCOMPARE(step.dir, Direction::Right);
  QCOMPARE(step.y, 992);
}

void MonitorLayoutTests::route_diagonalExit_fallsBackToSecondEdge()
{
  MonitorLayout layout;
  layout.setLiveMonitors("A", {mon("A1", 0, 0, 1000, 1000)});
  layout.setLiveMonitors("B", {mon("B1", 0, 0, 1000, 400)});
  layout.setLiveMonitors("C", {mon("C1", 0, 0, 1000, 1000)});
  layout.setPlacement("A", "A1", 0, 0);
  layout.setPlacement("B", "B1", 1000, 0);
  layout.setPlacement("C", "C1", 0, 1000);

  const auto step = layout.route("A", 998, 990, 1004, 1001);
  QCOMPARE(step.kind, Kind::Remote);
  QCOMPARE(step.computer, std::string("C"));
  QCOMPARE(step.dir, Direction::Bottom);
  QCOMPARE(step.x, 999);
  QCOMPARE(step.y, 0);
}

void MonitorLayoutTests::route_offlineMonitor_isIgnored()
{
  MonitorLayout layout;
  layout.setLiveMonitors("A", {mon("A1", 0, 0, 1000, 1000)});
  layout.setLiveMonitors("B", {mon("B1", 0, 0, 1000, 1000)});
  layout.setPlacement("A", "A1", 0, 0);
  layout.setPlacement("B", "B1", -1000, 0);
  layout.setPlacement("B", "B2", 1000, 0); // unplugged

  QCOMPARE(layout.route("A", 999, 500, 1003, 500).kind, Kind::Blocked);
  QCOMPARE(layout.route("A", 1, 500, -3, 500).kind, Kind::Remote);

  // reconnect with B2 present: routing resumes with the same placement
  layout.setLiveMonitors("B", {mon("B1", 0, 0, 1000, 1000), mon("B2", 1000, 0, 1000, 1000)});
  const auto step = layout.route("A", 999, 500, 1003, 500);
  QCOMPARE(step.kind, Kind::Remote);
  QCOMPARE(step.monitorId, std::string("B2"));
  QCOMPARE(step.x, 1000);

  // whole computer disconnects: no route to it
  layout.removeLiveMonitors("B");
  QCOMPARE(layout.route("A", 999, 500, 1003, 500).kind, Kind::Blocked);
}

void MonitorLayoutTests::route_deadSpaceStart_usesNearestMonitor()
{
  MonitorLayout layout;
  layout.setLiveMonitors("A", {mon("A1", 0, 0, 1000, 1000), mon("A2", 1000, 0, 1000, 500)});
  layout.setPlacement("A", "A1", 0, 0);
  layout.setPlacement("A", "A2", 1000, 0);

  const auto step = layout.route("A", 1500, 700, 1500, 710);
  QCOMPARE(step.kind, Kind::Blocked);
  QCOMPARE(step.monitorId, std::string("A2"));
  QCOMPARE(step.x, 1500);
  QCOMPARE(step.y, 499);
}

void MonitorLayoutTests::route_overlap_isDeterministic()
{
  MonitorLayout layout;
  layout.setLiveMonitors("A", {mon("A1", 0, 0, 1000, 1000)});
  layout.setLiveMonitors("C", {mon("C1", 0, 0, 1000, 1000)});
  layout.setLiveMonitors("B", {mon("B1", 0, 0, 1000, 1000)});
  layout.setPlacement("A", "A1", 0, 0);
  layout.setPlacement("C", "C1", 1000, 0);
  layout.setPlacement("B", "B1", 1000, 0);

  QCOMPARE(layout.route("A", 999, 500, 1003, 500).computer, std::string("B"));
  const auto issues = layout.validate();
  QCOMPARE(issues.size(), size_t(1));
  QCOMPARE(issues[0].kind, MonitorLayout::IssueKind::Overlap);
}

void MonitorLayoutTests::route_demoTopology_twoComputersFourMonitors()
{
  const auto layout = demoLayout();
  QVERIFY(layout.validate().empty());
  QCOMPARE(layout.routableMonitors().size(), size_t(5));

  // mona M1 (150%) -> right -> queen Q1
  auto step = layout.route("mona", 2559, 720, 2563, 720);
  QCOMPARE(step.kind, Kind::Remote);
  QCOMPARE(step.computer, std::string("queen"));
  QCOMPARE(step.monitorId, std::string("Q1"));
  QCOMPARE(step.x, 0);
  QCOMPARE(step.y, 480);

  // queen Q1 -> right -> mona M2, although queen's OS has Q2 to the right of Q1
  step = layout.route("queen", 1919, 500, 1925, 500);
  QCOMPARE(step.kind, Kind::Remote);
  QCOMPARE(step.computer, std::string("mona"));
  QCOMPARE(step.monitorId, std::string("M2"));
  QCOMPARE(step.x, 2560);
  QCOMPARE(step.y, 500);

  // mona M2 -> left -> queen Q1, although mona's OS has M1 to the left of M2
  step = layout.route("mona", 2560, 500, 2557, 500);
  QCOMPARE(step.kind, Kind::Remote);
  QCOMPARE(step.computer, std::string("queen"));
  QCOMPARE(step.x, 1919);
  QCOMPARE(step.y, 500);

  // queen Q1 -> down -> queen Q2 (portrait, partially aligned): local warp on queen
  step = layout.route("queen", 500, 1079, 500, 1083);
  QCOMPARE(step.kind, Kind::Local);
  QCOMPARE(step.monitorId, std::string("Q2"));
  QCOMPARE(step.x, 2420);
  QCOMPARE(step.y, 0);
  QCOMPARE(layout.route("queen", 1500, 1079, 1500, 1083).kind, Kind::Blocked);

  // and back up
  step = layout.route("queen", 2020, 0, 2020, -2);
  QCOMPARE(step.kind, Kind::Local);
  QCOMPARE(step.monitorId, std::string("Q1"));
  QCOMPARE(step.x, 100);
  QCOMPARE(step.y, 1079);

  // mona M3 (portrait) -> right -> mona M1 follows the OS position
  step = layout.route("mona", -1, 500, 2, 500);
  QCOMPARE(step.kind, Kind::Local);
  QCOMPARE(step.monitorId, std::string("M1"));
  QCOMPARE(step.x, 2);
  QCOMPARE(step.y, 500);

  // M2's right edge has nothing next to it
  QCOMPARE(layout.route("mona", 4479, 500, 4483, 500).kind, Kind::Blocked);
}

void MonitorLayoutTests::remoteSides_reportsEdgesWithRemoteNeighbours()
{
  const auto layout = demoLayout();
  const uint32_t leftRight = (1u << static_cast<int>(Direction::Left)) | (1u << static_cast<int>(Direction::Right));
  QCOMPARE(layout.remoteSides("mona"), leftRight);
  QCOMPARE(layout.remoteSides("queen"), leftRight);
  QCOMPARE(layout.remoteSides("nobody"), 0u);
}

void MonitorLayoutTests::validate_reportsProblems()
{
  MonitorLayout layout;
  layout.setLiveMonitors("A", {mon("A1", 0, 0, 1000, 1000), mon("A2", 1000, 0, 1000, 1000)});
  layout.setLiveMonitors("B", {mon("B1", 0, 0, 1000, 1000)});
  layout.setPlacement("A", "A1", 0, 0);
  layout.setPlacement("B", "B1", 5000, 5000); // isolated
  layout.setPlacement("A", "A9", 0, 0);       // offline on a live computer
  layout.setPlacement("Z", "Z1", 0, 0);       // whole computer offline: not an issue
  // A2 is unplaced

  int isolated = 0;
  int unplaced = 0;
  int offline = 0;
  for (const auto &issue : layout.validate()) {
    switch (issue.kind) {
      using enum MonitorLayout::IssueKind;
    case Isolated:
      ++isolated;
      break;
    case Unplaced:
      QCOMPARE(issue.monitorA, std::string("A2"));
      ++unplaced;
      break;
    case Offline:
      QCOMPARE(issue.monitorA, std::string("A9"));
      ++offline;
      break;
    case Overlap:
      QFAIL("unexpected overlap");
    }
  }
  QCOMPARE(isolated, 2);
  QCOMPARE(unplaced, 1);
  QCOMPARE(offline, 1);
}

void MonitorLayoutTests::serialize_roundTrip()
{
  MonitorInfo a = mon("\\\\.\\DISPLAY1", -1920, -200, 1920, 1080, 125);
  a.label = "DELL U2720Q";
  a.rotation = 90;
  a.pixelWidth = 2400;
  a.pixelHeight = 1350;
  a.primary = true;
  const MonitorInfo b = mon("HDMI-1", 0, 0, 2560, 1440);

  const auto text = serializeMonitors({a, b});
  const auto parsed = parseMonitors(text);
  QCOMPARE(parsed.size(), size_t(2));
  QVERIFY(parsed[0] == a);
  QVERIFY(parsed[1] == b);
}

void MonitorLayoutTests::serialize_escapesSeparators()
{
  MonitorInfo a = mon("odd;id=50%\nx", 0, 0, 10, 10);
  a.label = "Label; with = signs";
  const auto record = serializeMonitor(a);
  QVERIFY(record.find('\n') == std::string::npos);
  const auto parsed = parseMonitor(record);
  QVERIFY(parsed.has_value());
  QVERIFY(*parsed == a);
}

void MonitorLayoutTests::parse_skipsMalformedAndUnknownFields()
{
  const std::string text = "garbage\n"
                           "id=;x=1;y=1;w=1;h=1\n"
                           "id=zero;x=1;y=2;w=0;h=5\n"
                           "id=bad;x=one;y=2;w=3;h=5\n"
                           "id=ok;x=1;y=2;w=3;h=4;future=whatever\r\n"
                           "id=ok;x=9;y=9;w=9;h=9\n";
  const auto parsed = parseMonitors(text);
  QCOMPARE(parsed.size(), size_t(1));
  QCOMPARE(parsed[0].id, std::string("ok"));
  QCOMPARE(parsed[0].local, (Rect{1, 2, 3, 4}));
  QCOMPARE(parsed[0].scalePercent, 100);
}

void MonitorLayoutTests::stored_roundTripAndPlacementLoad()
{
  StoredMonitor placed{mon("P", 0, 0, 100, 100), true, -300, 40};
  StoredMonitor unplaced{mon("U", 100, 0, 100, 100), false, 0, 0};

  const auto p = parseStored(serializeStored(placed));
  const auto u = parseStored(serializeStored(unplaced));
  QVERIFY(p && u);
  QVERIFY(*p == placed);
  QVERIFY(*u == unplaced);

  MonitorLayout layout;
  layout.setPlacement("A", "stale", 1, 1);
  layout.setStored("A", {*p, *u});
  QVERIFY(layout.hasPlacement("A", "P"));
  QVERIFY(!layout.hasPlacement("A", "U"));
  QVERIFY(!layout.hasPlacement("A", "stale"));
  QCOMPARE(layout.placement("A", "P")->first, -300);
}

QTEST_MAIN(MonitorLayoutTests)
