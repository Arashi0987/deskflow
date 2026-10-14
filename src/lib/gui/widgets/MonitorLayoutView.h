/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "gui/config/MonitorTopology.h"

#include <QPointF>
#include <QRectF>
#include <QWidget>

/**
 * @brief OS style display arrangement editor for the per-monitor layout.
 *
 * Every monitor of every computer is drawn as its own proportional rectangle (logical
 * units, so a 4K monitor at 200% is as big as a 1080p one at 100%). Monitors are dragged
 * freely and snap to the edges of other monitors; arrow keys nudge the selected monitor by
 * one unit (Shift: 10) for exact offsets. Monitors that are not placed yet are shown dashed
 * at the position "Arrange" would give them and become placed when dragged.
 */
class MonitorLayoutView : public QWidget
{
  Q_OBJECT

public:
  explicit MonitorLayoutView(QWidget *parent = nullptr);

  //! The view edits \p topology in place; it does not take ownership.
  void setTopology(MonitorTopology *topology);
  MonitorTopology *topology() const
  {
    return m_topology;
  }

  void select(const QString &computer, const QString &id);
  QString selectedComputer() const
  {
    return m_selComputer;
  }
  QString selectedMonitor() const
  {
    return m_selId;
  }

  //! Widget position of the centre of a monitor (for tests and accessibility).
  QPointF monitorCenter(const QString &computer, const QString &id) const;

  //! Snap distance in screen pixels.
  static constexpr int kSnapPixels = 12;

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

Q_SIGNALS:
  void layoutChanged();
  void selectionChanged(const QString &description);

protected:
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  void contextMenuEvent(QContextMenuEvent *event) override;

private:
  struct Item
  {
    QString computer;
    MonitorTopology::StoredMonitor monitor; // gx/gy are the preview position when not placed
    bool placed = false;
    bool online = false;
    bool overlapping = false;
    QRectF global;
  };

  struct Transform
  {
    double scale = 1.0;
    QPointF offset;
    QPointF toWidget(const QPointF &g) const
    {
      return g * scale + offset;
    }
    QPointF toGlobal(const QPointF &w) const
    {
      return (w - offset) / scale;
    }
  };

  QList<Item> items() const;
  Transform fitTransform(const QList<Item> &items) const;
  const Item *itemAt(const QList<Item> &items, const Transform &t, const QPointF &pos) const;
  void moveSelected(int gx, int gy, bool snap);
  QString describe(const Item &item) const;
  QColor computerColor(const QString &computer) const;

  MonitorTopology *m_topology = nullptr;
  QString m_selComputer;
  QString m_selId;
  bool m_dragging = false;
  QPointF m_grabOffset; // global units between the monitor origin and the cursor
  Transform m_dragTransform;
};
