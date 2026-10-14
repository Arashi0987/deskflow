/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MonitorTopology.h"

#include "common/Settings.h"

#include <QByteArray>

#include <algorithm>
#include <climits>
#include <cmath>

using namespace deskflow::monitors;

namespace {

int toLogical(int pixels, int scalePercent)
{
  if (scalePercent <= 0)
    scalePercent = 100;
  return static_cast<int>(std::lround(static_cast<double>(pixels) * 100.0 / scalePercent));
}

Rect rectOf(const StoredMonitor &m)
{
  return globalRect(m.info, m.gx, m.gy);
}

} // namespace

QList<StoredMonitor> MonitorTopology::merge(const QList<StoredMonitor> &stored, const std::vector<MonitorInfo> &live)
{
  QList<StoredMonitor> result = stored;
  for (const auto &info : live) {
    if (info.id.empty() || !info.local.isValid())
      continue;
    auto it = std::find_if(result.begin(), result.end(), [&info](const StoredMonitor &m) {
      return m.info.id == info.id;
    });
    if (it != result.end()) {
      it->info = info; // keep the placement, refresh resolution/scale/local position
    } else {
      StoredMonitor added;
      added.info = info;
      result.append(added);
    }
  }
  return result;
}

void MonitorTopology::autoPlace(const QString &computer)
{
  auto &own = m_monitors[computer];
  QList<int> pending;
  for (int i = 0; i < own.size(); ++i) {
    if (!own.at(i).placed)
      pending.append(i);
  }
  if (pending.isEmpty())
    return;

  // everything already placed, on any computer
  std::vector<Rect> occupied;
  for (auto it = m_monitors.cbegin(); it != m_monitors.cend(); ++it) {
    for (const auto &m : it.value()) {
      if (m.placed)
        occupied.push_back(rectOf(m));
    }
  }

  QList<int> anchors;
  for (int i = 0; i < own.size(); ++i) {
    if (own.at(i).placed)
      anchors.append(i);
  }

  bool fresh = anchors.isEmpty();
  if (fresh) {
    // seed: the primary monitor (or the top-left one) goes to the right of the existing layout
    int seed = pending.first();
    for (const int i : pending) {
      const auto &a = own.at(i).info;
      const auto &b = own.at(seed).info;
      if (a.primary != b.primary ? a.primary : (a.local.x != b.local.x ? a.local.x < b.local.x : a.local.y < b.local.y))
        seed = i;
    }
    int baseX = 0;
    int baseY = 0;
    if (!occupied.empty()) {
      baseX = INT_MIN;
      baseY = INT_MAX;
      for (const auto &r : occupied) {
        baseX = std::max(baseX, r.right());
        baseY = std::min(baseY, r.y);
      }
    }
    own[seed].placed = true;
    own[seed].gx = baseX;
    own[seed].gy = baseY;
    anchors.append(seed);
    pending.removeOne(seed);
  }

  // breadth first over the computer's local arrangement: a monitor that touches an already
  // placed one locally is placed touching it globally, so mixed DPI keeps edges adjacent
  QList<int> placedNow;
  while (!pending.isEmpty()) {
    bool progress = false;
    for (const int i : QList<int>(pending)) {
      auto &m = own[i];
      const auto &l = m.info.local;
      for (const int a : anchors) {
        const auto &n = own.at(a);
        const auto &nl = n.info.local;
        const auto ng = rectOf(n);
        const bool vOverlap = l.y < nl.bottom() && nl.y < l.bottom();
        const bool hOverlap = l.x < nl.right() && nl.x < l.right();
        bool touched = true;
        if (vOverlap && l.x == nl.right()) {
          m.gx = ng.right();
          m.gy = ng.y + toLogical(l.y - nl.y, n.info.scalePercent);
        } else if (vOverlap && l.right() == nl.x) {
          m.gx = ng.x - rectOf(m).w;
          m.gy = ng.y + toLogical(l.y - nl.y, n.info.scalePercent);
        } else if (hOverlap && l.y == nl.bottom()) {
          m.gy = ng.bottom();
          m.gx = ng.x + toLogical(l.x - nl.x, n.info.scalePercent);
        } else if (hOverlap && l.bottom() == nl.y) {
          m.gy = ng.y - rectOf(m).h;
          m.gx = ng.x + toLogical(l.x - nl.x, n.info.scalePercent);
        } else {
          touched = false;
        }
        if (touched) {
          m.placed = true;
          anchors.append(i);
          placedNow.append(i);
          pending.removeOne(i);
          progress = true;
          break;
        }
      }
    }
    if (!progress) {
      // not locally adjacent to anything placed: keep its offset from the first anchor
      const int i = pending.takeFirst();
      const auto &n = own.at(anchors.first());
      auto &m = own[i];
      m.gx = n.gx + toLogical(m.info.local.x - n.info.local.x, n.info.scalePercent);
      m.gy = n.gy + toLogical(m.info.local.y - n.info.local.y, n.info.scalePercent);
      m.placed = true;
      anchors.append(i);
      placedNow.append(i);
    }
  }

  // never create an overlap with monitors that were already there; move offenders to the far right
  for (const int i : placedNow) {
    auto &m = own[i];
    const auto r = rectOf(m);
    const bool overlaps = std::any_of(occupied.cbegin(), occupied.cend(), [&r](const Rect &o) {
      return o.intersects(r);
    });
    if (overlaps) {
      int right = INT_MIN;
      for (const auto &o : occupied)
        right = std::max(right, o.right());
      m.gx = right;
    }
    occupied.push_back(rectOf(m));
  }
}

void MonitorTopology::autoPlaceAll()
{
  for (const auto &computer : computers())
    autoPlace(computer);
}

QStringList MonitorTopology::computers() const
{
  return m_monitors.keys();
}

QList<StoredMonitor> MonitorTopology::monitors(const QString &computer) const
{
  return m_monitors.value(computer);
}

void MonitorTopology::setMonitors(const QString &computer, const QList<StoredMonitor> &monitors)
{
  m_monitors[computer] = monitors;
}

bool MonitorTopology::setPosition(const QString &computer, const QString &id, int gx, int gy)
{
  for (auto &m : m_monitors[computer]) {
    if (m.info.id == id.toStdString()) {
      m.placed = true;
      m.gx = gx;
      m.gy = gy;
      return true;
    }
  }
  return false;
}

bool MonitorTopology::unplace(const QString &computer, const QString &id)
{
  for (auto &m : m_monitors[computer]) {
    if (m.info.id == id.toStdString()) {
      // an unplaced record has no position when persisted
      m.placed = false;
      m.gx = 0;
      m.gy = 0;
      return true;
    }
  }
  return false;
}

bool MonitorTopology::forget(const QString &computer, const QString &id)
{
  auto &list = m_monitors[computer];
  const auto before = list.size();
  list.removeIf([&id](const StoredMonitor &m) { return m.info.id == id.toStdString(); });
  return list.size() != before;
}

void MonitorTopology::report(const QString &computer, const std::vector<MonitorInfo> &live)
{
  m_monitors[computer] = merge(m_monitors.value(computer), live);
  QSet<QString> ids;
  for (const auto &info : live)
    ids.insert(QString::fromStdString(info.id));
  m_online[computer] = ids;
}

bool MonitorTopology::isOnline(const QString &computer, const QString &id) const
{
  return m_online.value(computer).contains(id);
}

void MonitorTopology::setOnline(const QString &computer, const QSet<QString> &ids)
{
  m_online[computer] = ids;
}

void MonitorTopology::retainOnline(const QStringList &connected)
{
  for (auto it = m_online.begin(); it != m_online.end();) {
    if (connected.contains(it.key()))
      ++it;
    else
      it = m_online.erase(it);
  }
}

MonitorLayout MonitorTopology::layout() const
{
  MonitorLayout layout;
  for (auto it = m_monitors.cbegin(); it != m_monitors.cend(); ++it) {
    const auto name = it.key().toStdString();
    std::vector<StoredMonitor> stored(it.value().cbegin(), it.value().cend());
    std::vector<MonitorInfo> infos;
    for (const auto &m : stored)
      infos.push_back(m.info);
    layout.setStored(name, stored);
    layout.setLiveMonitors(name, infos);
  }
  return layout;
}

void MonitorTopology::load(const QStringList &computers)
{
  m_monitors.clear();
  for (const auto &computer : computers) {
    if (!computer.isEmpty())
      m_monitors[computer] = loadComputer(computer);
  }
  // the editor is a copy of the persisted topology; online state comes from what the core reported
  if (this != &live())
    m_online = live().m_online;
}

void MonitorTopology::save(const QStringList &computers) const
{
  for (const auto &computer : computers) {
    if (!computer.isEmpty())
      saveComputer(computer, m_monitors.value(computer));
  }
}

QList<StoredMonitor> MonitorTopology::loadComputer(const QString &computer)
{
  QList<StoredMonitor> result;
  const auto records = Settings::value(Settings::Computer::Monitors.arg(computer)).toStringList();
  for (const auto &record : records) {
    if (const auto stored = parseStored(record.toStdString()); stored)
      result.append(*stored);
  }
  return result;
}

void MonitorTopology::saveComputer(const QString &computer, const QList<StoredMonitor> &monitors)
{
  QStringList records;
  for (const auto &m : monitors)
    records.append(QString::fromStdString(serializeStored(m)));
  Settings::setValue(Settings::Computer::Monitors.arg(computer), records.isEmpty() ? QVariant() : QVariant(records));
}

bool MonitorTopology::decodeReport(const QString &args, QString &computer, std::vector<MonitorInfo> &monitors)
{
  const auto decoded = QByteArray::fromBase64Encoding(
      args.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals |
                           QByteArray::AbortOnBase64DecodingErrors
  );
  if (!decoded)
    return false;
  const auto text = decoded.decoded.toStdString();
  const auto newline = text.find('\n');
  computer = QString::fromStdString(text.substr(0, newline));
  if (computer.isEmpty())
    return false;
  monitors = newline == std::string::npos ? std::vector<MonitorInfo>{} : parseMonitors(text.substr(newline + 1));
  return true;
}

QString MonitorTopology::handleReport(const QString &args)
{
  QString computer;
  std::vector<MonitorInfo> monitors;
  if (!decodeReport(args, computer, monitors))
    return {};
  saveComputer(computer, merge(loadComputer(computer), monitors));
  QSet<QString> ids;
  for (const auto &info : monitors)
    ids.insert(QString::fromStdString(info.id));
  live().setOnline(computer, ids);
  return computer;
}

MonitorTopology &MonitorTopology::live()
{
  static MonitorTopology instance;
  return instance;
}
