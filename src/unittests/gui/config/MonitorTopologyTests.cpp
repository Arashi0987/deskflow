/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MonitorTopologyTests.h"

#include "common/Settings.h"
#include "gui/config/MonitorTopology.h"

using namespace deskflow::monitors;

namespace {

MonitorInfo monitor(const char *id, int x, int y, int w, int h, int scale = 100, bool primary = false)
{
  MonitorInfo info;
  info.id = id;
  info.label = id;
  info.local = {x, y, w, h};
  info.scalePercent = scale;
  info.primary = primary;
  return info;
}

// server: 2560x1440 at 150% plus 1920x1080 at 100% to its right (Windows, physical pixels)
std::vector<MonitorInfo> monaMonitors()
{
  return {monitor("M1", 0, 0, 2560, 1440, 150, true), monitor("M2", 2560, 0, 1920, 1080)};
}

// client: 1920x1080 plus a portrait 1080x1920 to its right
std::vector<MonitorInfo> queenMonitors()
{
  return {monitor("Q1", 0, 0, 1920, 1080, 100, true), monitor("Q2", 1920, 0, 1080, 1920)};
}

const StoredMonitor *find(const QList<StoredMonitor> &list, const char *id)
{
  for (const auto &m : list) {
    if (m.info.id == id)
      return &m;
  }
  return nullptr;
}

QString encode(const QString &computer, const std::vector<MonitorInfo> &monitors)
{
  const auto payload = QByteArray::fromStdString(computer.toStdString() + "\n" + serializeMonitors(monitors));
  return QString::fromLatin1(payload.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

} // namespace

void MonitorTopologyTests::initTestCase()
{
  QDir dir;
  QVERIFY(dir.mkpath(m_settingsPath));
  QFile::remove(m_settingsFile);
  QFile::remove(m_stateFile);
  Settings::setSettingsFile(m_settingsFile);
  Settings::setStateFile(m_stateFile);
}

void MonitorTopologyTests::mergeKeepsPlacements()
{
  StoredMonitor placed;
  placed.info = monitor("M1", 0, 0, 1920, 1080);
  placed.placed = true;
  placed.gx = -500;
  placed.gy = 40;
  StoredMonitor offline;
  offline.info = monitor("OLD", 1920, 0, 1280, 1024);
  offline.placed = true;
  offline.gx = 1420;

  // M1 changed resolution, M2 is new, OLD is unplugged
  const auto merged =
      MonitorTopology::merge({placed, offline}, {monitor("M1", 0, 0, 2560, 1440), monitor("M2", 2560, 0, 1920, 1080)});
  QCOMPARE(merged.size(), 3);
  const auto *m1 = find(merged, "M1");
  QVERIFY(m1);
  QVERIFY(m1->placed);
  QCOMPARE(m1->gx, -500);
  QCOMPARE(m1->gy, 40);
  QCOMPARE(m1->info.local.w, 2560);
  const auto *m2 = find(merged, "M2");
  QVERIFY(m2);
  QVERIFY(!m2->placed);
  const auto *old = find(merged, "OLD");
  QVERIFY(old);
  QVERIFY(old->placed);
  QCOMPARE(old->gx, 1420);
}

void MonitorTopologyTests::autoPlacePreservesLocalArrangementWithMixedDpi()
{
  MonitorTopology topology;
  topology.report("mona", monaMonitors());
  topology.report("queen", queenMonitors());
  topology.autoPlaceAll();

  const auto mona = topology.monitors("mona");
  const auto *m1 = find(mona, "M1");
  const auto *m2 = find(mona, "M2");
  QVERIFY(m1 && m1->placed && m2 && m2->placed);
  QCOMPARE(m1->gx, 0);
  QCOMPARE(m1->gy, 0);
  // M1 is 1707 logical units wide at 150%; M2 must touch it, not sit at the physical 2560
  QCOMPARE(m2->gx, 1707);
  QCOMPARE(m2->gy, 0);

  // queen goes to the right of everything already placed, keeping its own arrangement
  const auto queen = topology.monitors("queen");
  const auto *q1 = find(queen, "Q1");
  const auto *q2 = find(queen, "Q2");
  QVERIFY(q1 && q1->placed && q2 && q2->placed);
  QCOMPARE(q1->gx, 1707 + 1920);
  QCOMPARE(q2->gx, q1->gx + 1920);
  QCOMPARE(q2->gy, q1->gy);
  for (const auto &issue : topology.layout().validate())
    QVERIFY2(issue.kind != MonitorLayout::IssueKind::Overlap, "auto placement created an overlap");
}

void MonitorTopologyTests::autoPlaceRelativeToPlacedMonitor()
{
  MonitorTopology topology;
  topology.report("queen", queenMonitors());
  QVERIFY(topology.setPosition("queen", "Q1", 5000, -300));
  topology.autoPlace("queen");
  const auto *q2 = find(topology.monitors("queen"), "Q2");
  QVERIFY(q2 && q2->placed);
  QCOMPARE(q2->gx, 6920);
  QCOMPARE(q2->gy, -300);
}

void MonitorTopologyTests::autoPlaceNeverOverlaps()
{
  MonitorTopology topology;
  topology.report("mona", monaMonitors());
  topology.report("queen", queenMonitors());
  QVERIFY(topology.setPosition("mona", "M1", 0, 0));
  QVERIFY(topology.setPosition("mona", "M2", 1707, 0));
  // Q1 placed so that Q2's natural spot (right of Q1) is taken by M2
  QVERIFY(topology.setPosition("queen", "Q1", -213, 960));
  topology.autoPlace("queen");
  for (const auto &issue : topology.layout().validate())
    QVERIFY2(issue.kind != MonitorLayout::IssueKind::Overlap, "auto placement created an overlap");
  const auto *q2 = find(topology.monitors("queen"), "Q2");
  QVERIFY(q2 && q2->placed);
}

void MonitorTopologyTests::saveAndReload()
{
  MonitorTopology topology;
  topology.report("mona", monaMonitors());
  topology.report("queen", queenMonitors());
  topology.autoPlaceAll();
  QVERIFY(topology.setPosition("queen", "Q2", -1080, -200));
  QVERIFY(topology.unplace("mona", "M2"));
  topology.save({"mona", "queen"});

  MonitorTopology reloaded;
  reloaded.load({"mona", "queen"});
  QVERIFY(reloaded.sameMonitors(topology));
  const auto *q2 = find(reloaded.monitors("queen"), "Q2");
  QVERIFY(q2 && q2->placed);
  QCOMPARE(q2->gx, -1080);
  QCOMPARE(q2->gy, -200);
  QCOMPARE(q2->info.local.h, 1920);
  QVERIFY(!find(reloaded.monitors("mona"), "M2")->placed);

  // the server parses the very same records
  const auto records = Settings::value(Settings::Computer::Monitors.arg("queen")).toStringList();
  QCOMPARE(records.size(), 2);
  for (const auto &record : records)
    QVERIFY(parseStored(record.toStdString()).has_value());
}

void MonitorTopologyTests::reportKeepsPlacementsAndTracksOnline()
{
  MonitorTopology topology;
  topology.report("queen", queenMonitors());
  topology.autoPlaceAll();
  QVERIFY(topology.setPosition("queen", "Q1", 100, 200));
  topology.save({"queen"});

  // reconnect after Q2 was unplugged and Q1 changed mode
  auto replug = std::vector<MonitorInfo>{monitor("Q1", 0, 0, 2560, 1440, 125, true)};
  QCOMPARE(MonitorTopology::handleReport(encode("queen", replug)), QStringLiteral("queen"));

  const auto stored = MonitorTopology::loadComputer("queen");
  QCOMPARE(stored.size(), 2);
  const auto *q1 = find(stored, "Q1");
  QCOMPARE(q1->gx, 100);
  QCOMPARE(q1->gy, 200);
  QCOMPARE(q1->info.scalePercent, 125);
  QVERIFY(find(stored, "Q2")->placed);

  QVERIFY(MonitorTopology::live().isOnline("queen", "Q1"));
  QVERIFY(!MonitorTopology::live().isOnline("queen", "Q2"));

  MonitorTopology editor;
  editor.load({"queen"});
  QVERIFY(editor.isOnline("queen", "Q1"));

  MonitorTopology::live().retainOnline({"mona"});
  QVERIFY(!MonitorTopology::live().isOnline("queen", "Q1"));
}

void MonitorTopologyTests::malformedReportIgnored()
{
  QVERIFY(MonitorTopology::handleReport(QStringLiteral("***not base64***")).isEmpty());
  QVERIFY(MonitorTopology::handleReport(QString()).isEmpty());
  QString computer;
  std::vector<MonitorInfo> monitors;
  QVERIFY(MonitorTopology::decodeReport(encode("legacy", {}), computer, monitors));
  QCOMPARE(computer, QStringLiteral("legacy"));
  QVERIFY(monitors.empty());
}

void MonitorTopologyTests::editedLayoutRoutesInterleaved()
{
  // [M1][Q1][M2] with portrait Q2 under Q1, built through the editor model, then routed
  MonitorTopology topology;
  topology.report("mona", monaMonitors());
  topology.report("queen", queenMonitors());
  QVERIFY(topology.setPosition("mona", "M1", 0, 0));
  QVERIFY(topology.setPosition("queen", "Q1", 1707, 0));
  QVERIFY(topology.setPosition("mona", "M2", 3627, 0));
  QVERIFY(topology.setPosition("queen", "Q2", 1707, 1080));
  topology.save({"mona", "queen"});

  // what the server does at startup: placements from settings, live monitors from the computers
  MonitorLayout layout;
  for (const char *computer : {"mona", "queen"}) {
    std::vector<StoredMonitor> stored;
    for (const auto &record : Settings::value(Settings::Computer::Monitors.arg(computer)).toStringList())
      stored.push_back(*parseStored(record.toStdString()));
    layout.setStored(computer, stored);
  }
  layout.setLiveMonitors("mona", monaMonitors());
  layout.setLiveMonitors("queen", queenMonitors());

  // M1's right edge leads to queen even though mona's OS puts M2 there
  auto step = layout.route("mona", 2555, 720, 2565, 720);
  QCOMPARE(int(step.kind), int(MonitorLayout::StepKind::Remote));
  QCOMPARE(QString::fromStdString(step.computer), QStringLiteral("queen"));
  QCOMPARE(QString::fromStdString(step.monitorId), QStringLiteral("Q1"));

  // Q1's right edge leads back to mona's M2
  step = layout.route("queen", 1915, 500, 1925, 500);
  QCOMPARE(int(step.kind), int(MonitorLayout::StepKind::Remote));
  QCOMPARE(QString::fromStdString(step.computer), QStringLiteral("mona"));
  QCOMPARE(QString::fromStdString(step.monitorId), QStringLiteral("M2"));
  QVERIFY(step.x >= 2560);
}

QTEST_MAIN(MonitorTopologyTests)
