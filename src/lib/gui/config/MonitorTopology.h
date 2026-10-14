/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "common/MonitorLayout.h"

#include <QList>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>

/**
 * @brief GUI side of the per-monitor layout: what the editor shows and what is persisted.
 *
 * Each computer's monitors are stored under `computer_<name>/monitors`, one
 * deskflow::monitors::serializeStored() record per monitor. A record keeps the last
 * reported monitor info (so offline monitors still show up in the editor) and, once
 * the user has placed it, its global position. The server reads the same records.
 *
 * Reports from the core (IPC `monitorTopology`) refresh the info of known monitors and
 * add new ones as unplaced; they never move or drop a placement, so a hotplug or a
 * reconnect cannot silently change the user's layout.
 */
class MonitorTopology
{
public:
  using StoredMonitor = deskflow::monitors::StoredMonitor;
  using MonitorInfo = deskflow::monitors::MonitorInfo;

  //! Merge a live report into the stored records of one computer.
  static QList<StoredMonitor> merge(const QList<StoredMonitor> &stored, const std::vector<MonitorInfo> &live);

  /**
   * @brief Give every unplaced monitor of \p computer a position.
   *
   * Monitors of a computer that has nothing placed yet keep the owning OS's local
   * arrangement (relative offsets, converted to logical units) and the group is put to the
   * right of everything already placed, so nothing overlaps. Monitors of a computer that is
   * partly placed are positioned relative to an already placed monitor of that computer.
   */
  void autoPlace(const QString &computer);
  void autoPlaceAll();

  //! @name Editing
  //@{
  QStringList computers() const;
  QList<StoredMonitor> monitors(const QString &computer) const;
  void setMonitors(const QString &computer, const QList<StoredMonitor> &monitors);
  bool setPosition(const QString &computer, const QString &id, int gx, int gy);
  bool unplace(const QString &computer, const QString &id);
  bool forget(const QString &computer, const QString &id);
  void report(const QString &computer, const std::vector<MonitorInfo> &live);
  //@}

  bool sameMonitors(const MonitorTopology &other) const
  {
    return m_monitors == other.m_monitors;
  }

  //! Monitors currently reported by a connected computer (as opposed to remembered ones).
  bool isOnline(const QString &computer, const QString &id) const;
  void setOnline(const QString &computer, const QSet<QString> &ids);
  //! Mark every computer not in \p connected as offline (an empty list clears all).
  void retainOnline(const QStringList &connected);

  //! Router-equivalent model of the placed monitors (offline ones count as live so the editor
  //! can validate the layout the user is building, not only the part that is connected now).
  deskflow::monitors::MonitorLayout layout() const;

  //! @name Persistence
  //@{
  void load(const QStringList &computers);
  void save(const QStringList &computers) const;
  static QList<StoredMonitor> loadComputer(const QString &computer);
  static void saveComputer(const QString &computer, const QList<StoredMonitor> &monitors);
  //@}

  //! Decode the IPC payload sent by the server: "<computer>\n<serializeMonitors()>" as base64url.
  static bool decodeReport(const QString &args, QString &computer, std::vector<MonitorInfo> &monitors);

  /**
   * @brief Handle an IPC `monitorTopology` report from the server.
   *
   * Merges the report into the persisted records (placements are kept) and marks the
   * reported monitors online in live(). Returns the computer name, or an empty string if
   * the payload was malformed.
   */
  static QString handleReport(const QString &args);

  //! Topology reported since the GUI started; shared by the IPC handler and the layout editor.
  static MonitorTopology &live();

private:
  QMap<QString, QList<StoredMonitor>> m_monitors;
  QMap<QString, QSet<QString>> m_online;
};
