/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/widgets/MonitorLayoutView.h"

#include <QApplication>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTest>

using namespace deskflow::monitors;

namespace {

MonitorInfo
monitor(const char *id, const char *label, int x, int y, int w, int h, int scale = 100, bool primary = false)
{
  MonitorInfo info;
  info.id = id;
  info.label = label;
  info.local = {x, y, w, h};
  info.scalePercent = scale;
  info.primary = primary;
  return info;
}

const StoredMonitor *find(const QList<StoredMonitor> &list, const char *id)
{
  for (const auto &m : list) {
    if (m.info.id == id)
      return &m;
  }
  return nullptr;
}

void send(QWidget *w, QEvent::Type type, const QPointF &pos, Qt::MouseButtons buttons)
{
  QMouseEvent event(type, pos, w->mapToGlobal(pos), Qt::LeftButton, buttons, Qt::NoModifier);
  QApplication::sendEvent(w, &event);
}

void drag(QWidget *w, const QPointF &from, const QPointF &to)
{
  send(w, QEvent::MouseButtonPress, from, Qt::LeftButton);
  const int steps = 8;
  for (int i = 1; i <= steps; ++i)
    send(w, QEvent::MouseMove, from + (to - from) * i / steps, Qt::LeftButton);
  send(w, QEvent::MouseButtonRelease, to, Qt::NoButton);
}

} // namespace

class MonitorLayoutViewTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void init();
  void dragSnapsToNeighbourEdge();
  void draggingUnplacedMonitorPlacesIt();
  void arrowKeysNudge();
  void renderDemo();

private:
  // global units per screen pixel
  double unitsPerPixel() const;

  MonitorTopology m_topology;
  std::unique_ptr<MonitorLayoutView> m_view;
};

void MonitorLayoutViewTests::init()
{
  // Interleaved demo layout:        [mona M1][queen Q1][mona M2]
  //                                           [queen Q2]  (portrait)
  // plus an unplaced queen Q3 that was just hot-plugged.
  m_topology = MonitorTopology();
  m_topology.report(
      "mona", {monitor("\\\\.\\DISPLAY1", "DELL U2720Q", 0, 0, 2560, 1440, 150, true),
               monitor("\\\\.\\DISPLAY2", "LG 24MP", 2560, 0, 1920, 1080)}
  );
  m_topology.report(
      "queen",
      {monitor("DP-1", "DP-1", 0, 0, 1920, 1080, 100, true),
       monitor("HDMI-1", "HDMI-1 (portrait)", 1920, 0, 1080, 1920), monitor("DP-2", "DP-2", 3000, 0, 1280, 1024)}
  );
  QVERIFY(m_topology.setPosition("mona", "\\\\.\\DISPLAY1", 0, 0));
  QVERIFY(m_topology.setPosition("queen", "DP-1", 1707, 0));
  QVERIFY(m_topology.setPosition("mona", "\\\\.\\DISPLAY2", 3627, 0));
  QVERIFY(m_topology.setPosition("queen", "HDMI-1", 1707, 1080));

  m_view = std::make_unique<MonitorLayoutView>();
  m_view->resize(960, 540);
  m_view->setTopology(&m_topology);
}

double MonitorLayoutViewTests::unitsPerPixel() const
{
  const auto a = m_view->monitorCenter("mona", "\\\\.\\DISPLAY1");
  const auto b = m_view->monitorCenter("queen", "DP-1");
  // centres are (853.5) and (1707 + 960) apart in global units
  return (2667.0 - 853.5) / (b.x() - a.x());
}

void MonitorLayoutViewTests::dragSnapsToNeighbourEdge()
{
  QSignalSpy changed(m_view.get(), &MonitorLayoutView::layoutChanged);
  const auto upp = unitsPerPixel();
  const auto start = m_view->monitorCenter("queen", "HDMI-1");

  // a few pixels off to the right: snaps back flush with Q1's left edge
  drag(m_view.get(), start, start + QPointF(5, 3));
  auto q2 = find(m_topology.monitors("queen"), "HDMI-1");
  QCOMPARE(q2->gx, 1707);
  QCOMPARE(q2->gy, 1080);
  QVERIFY(changed.count() > 0);

  // move it under mona's M2, landing slightly off: snaps to touch M2's bottom and align with its left edge
  const auto again = m_view->monitorCenter("queen", "HDMI-1");
  drag(m_view.get(), again, again + QPointF(1920 / upp + 4, -4));
  q2 = find(m_topology.monitors("queen"), "HDMI-1");
  QCOMPARE(q2->gx, 3627);
  QCOMPARE(q2->gy, 1080);
  QCOMPARE(m_view->selectedComputer(), QStringLiteral("queen"));
}

void MonitorLayoutViewTests::draggingUnplacedMonitorPlacesIt()
{
  QVERIFY(!find(m_topology.monitors("queen"), "DP-2")->placed);
  const auto start = m_view->monitorCenter("queen", "DP-2");
  drag(m_view.get(), start, start + QPointF(30, 40));
  const auto *dp2 = find(m_topology.monitors("queen"), "DP-2");
  QVERIFY(dp2->placed);
  for (const auto &issue : m_topology.layout().validate())
    QVERIFY(issue.kind != MonitorLayout::IssueKind::Overlap);
}

void MonitorLayoutViewTests::arrowKeysNudge()
{
  m_view->select("mona", "\\\\.\\DISPLAY2");
  QTest::keyClick(m_view.get(), Qt::Key_Down);
  QTest::keyClick(m_view.get(), Qt::Key_Right, Qt::ShiftModifier);
  const auto *m2 = find(m_topology.monitors("mona"), "\\\\.\\DISPLAY2");
  QCOMPARE(m2->gx, 3637);
  QCOMPARE(m2->gy, 1);
}

void MonitorLayoutViewTests::renderDemo()
{
  // DESKFLOW_MONITOR_LAYOUT_PNG=/path/out.png renders the editor for the documentation/demo
  m_view->select("queen", "HDMI-1");
  const auto image = m_view->grab();
  QVERIFY(!image.isNull());
  if (const auto path = qEnvironmentVariable("DESKFLOW_MONITOR_LAYOUT_PNG"); !path.isEmpty())
    QVERIFY(image.save(path));
}

QTEST_MAIN(MonitorLayoutViewTests)
#include "MonitorLayoutViewTests.moc"
