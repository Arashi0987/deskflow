/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MonitorLayoutView.h"

#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

using namespace deskflow::monitors;

namespace {
constexpr double kMargin = 24.0;
}

MonitorLayoutView::MonitorLayoutView(QWidget *parent) : QWidget(parent)
{
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(false);
  setAccessibleName(tr("Monitor layout"));
}

void MonitorLayoutView::setTopology(MonitorTopology *topology)
{
  m_topology = topology;
  m_selComputer.clear();
  m_selId.clear();
  update();
}

void MonitorLayoutView::select(const QString &computer, const QString &id)
{
  m_selComputer = computer;
  m_selId = id;
  for (const auto &item : items()) {
    if (item.computer == computer && QString::fromStdString(item.monitor.info.id) == id) {
      Q_EMIT selectionChanged(describe(item));
      break;
    }
  }
  update();
}

QSize MonitorLayoutView::sizeHint() const
{
  return {640, 360};
}

QSize MonitorLayoutView::minimumSizeHint() const
{
  return {320, 180};
}

QList<MonitorLayoutView::Item> MonitorLayoutView::items() const
{
  QList<Item> result;
  if (!m_topology)
    return result;

  MonitorTopology preview = *m_topology;
  preview.autoPlaceAll();

  for (const auto &computer : m_topology->computers()) {
    const auto original = m_topology->monitors(computer);
    const auto placedPreview = preview.monitors(computer);
    for (int i = 0; i < original.size(); ++i) {
      Item item;
      item.computer = computer;
      item.placed = original.at(i).placed;
      item.monitor = i < placedPreview.size() ? placedPreview.at(i) : original.at(i);
      item.online = m_topology->isOnline(computer, QString::fromStdString(item.monitor.info.id));
      const auto r = globalRect(item.monitor.info, item.monitor.gx, item.monitor.gy);
      item.global = QRectF(r.x, r.y, r.w, r.h);
      result.append(item);
    }
  }

  for (const auto &issue : m_topology->layout().validate()) {
    if (issue.kind != MonitorLayout::IssueKind::Overlap)
      continue;
    for (auto &item : result) {
      const auto c = item.computer.toStdString();
      const auto &id = item.monitor.info.id;
      if ((c == issue.computerA && id == issue.monitorA) || (c == issue.computerB && id == issue.monitorB))
        item.overlapping = true;
    }
  }
  return result;
}

MonitorLayoutView::Transform MonitorLayoutView::fitTransform(const QList<Item> &items) const
{
  Transform t;
  if (items.isEmpty())
    return t;
  QRectF bounds = items.first().global;
  for (const auto &item : items)
    bounds = bounds.united(item.global);
  const double w = std::max(1.0, width() - 2 * kMargin);
  const double h = std::max(1.0, height() - 2 * kMargin);
  t.scale = std::min(w / std::max(1.0, bounds.width()), h / std::max(1.0, bounds.height()));
  const QPointF centre(width() / 2.0, height() / 2.0);
  t.offset = centre - bounds.center() * t.scale;
  return t;
}

const MonitorLayoutView::Item *
MonitorLayoutView::itemAt(const QList<Item> &items, const Transform &t, const QPointF &pos) const
{
  const auto g = t.toGlobal(pos);
  // last drawn is on top; the selected monitor is drawn last
  const Item *hit = nullptr;
  for (const auto &item : items) {
    if (item.global.contains(g)) {
      const bool selected = item.computer == m_selComputer && QString::fromStdString(item.monitor.info.id) == m_selId;
      if (!hit || selected)
        hit = &item;
    }
  }
  return hit;
}

QPointF MonitorLayoutView::monitorCenter(const QString &computer, const QString &id) const
{
  const auto list = items();
  const auto t = m_dragging ? m_dragTransform : fitTransform(list);
  for (const auto &item : list) {
    if (item.computer == computer && QString::fromStdString(item.monitor.info.id) == id)
      return t.toWidget(item.global.center());
  }
  return {};
}

QColor MonitorLayoutView::computerColor(const QString &computer) const
{
  static const QList<QColor> palette = {QColor(0x3d, 0x7e, 0xc9), QColor(0x2f, 0xa0, 0x6b), QColor(0xc9, 0x7a, 0x2b),
                                        QColor(0x8a, 0x5c, 0xc9), QColor(0xc4, 0x4f, 0x7a), QColor(0x3f, 0x9e, 0xa8)};
  const auto index = m_topology ? m_topology->computers().indexOf(computer) : 0;
  return palette.at(std::max<qsizetype>(0, index) % palette.size());
}

QString MonitorLayoutView::describe(const Item &item) const
{
  const auto &info = item.monitor.info;
  const int pw = info.pixelWidth > 0 ? info.pixelWidth : info.local.w;
  const int ph = info.pixelHeight > 0 ? info.pixelHeight : info.local.h;
  QStringList parts;
  parts << QStringLiteral("%1 / %2").arg(item.computer, QString::fromStdString(info.label.empty() ? info.id : info.label));
  parts << tr("%1x%2 at %3%").arg(pw).arg(ph).arg(info.scalePercent);
  if (ph > pw)
    parts << tr("portrait");
  if (info.primary)
    parts << tr("primary");
  parts << tr("local %1,%2").arg(info.local.x).arg(info.local.y);
  if (item.placed)
    parts << tr("layout %1,%2").arg(item.monitor.gx).arg(item.monitor.gy);
  else
    parts << tr("not placed");
  if (!item.online)
    parts << tr("offline");
  return parts.join(QStringLiteral(", "));
}

void MonitorLayoutView::paintEvent(QPaintEvent *)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.fillRect(rect(), palette().color(QPalette::Base));

  auto list = items();
  if (list.isEmpty()) {
    p.setPen(palette().color(QPalette::PlaceholderText));
    p.drawText(
        rect(), Qt::AlignCenter | Qt::TextWordWrap,
        tr("No monitors reported yet.\nStart the server and connect the clients; each computer reports its monitors.")
    );
    return;
  }

  const auto t = m_dragging ? m_dragTransform : fitTransform(list);
  std::stable_sort(list.begin(), list.end(), [this](const Item &a, const Item &b) {
    const bool aSel = a.computer == m_selComputer && QString::fromStdString(a.monitor.info.id) == m_selId;
    const bool bSel = b.computer == m_selComputer && QString::fromStdString(b.monitor.info.id) == m_selId;
    return a.placed != b.placed ? !a.placed : (!aSel && bSel);
  });

  QFont small = font();
  small.setPointSizeF(std::max(6.0, font().pointSizeF() * 0.85));
  QFont bold = font();
  bold.setBold(true);

  for (const auto &item : list) {
    const QRectF r(t.toWidget(item.global.topLeft()), t.toWidget(item.global.bottomRight()));
    const auto box = r.adjusted(1, 1, -1, -1);
    auto colour = computerColor(item.computer);
    if (!item.online)
      colour = colour.lighter(150);
    const bool selected = item.computer == m_selComputer && QString::fromStdString(item.monitor.info.id) == m_selId;

    p.setBrush(item.placed ? colour : QColor(colour.red(), colour.green(), colour.blue(), 70));
    QPen pen(item.overlapping ? QColor(Qt::red) : (selected ? palette().color(QPalette::Highlight) : colour.darker(160)));
    pen.setWidthF(selected || item.overlapping ? 3.0 : 1.5);
    if (!item.placed || !item.online)
      pen.setStyle(Qt::DashLine);
    p.setPen(pen);
    p.drawRoundedRect(box, 4, 4);

    const auto &info = item.monitor.info;
    const int pw = info.pixelWidth > 0 ? info.pixelWidth : info.local.w;
    const int ph = info.pixelHeight > 0 ? info.pixelHeight : info.local.h;
    QStringList lines;
    lines << QString::fromStdString(info.label.empty() ? info.id : info.label);
    lines << QStringLiteral("%1x%2").arg(pw).arg(ph) +
                 (info.scalePercent != 100 ? QStringLiteral(" @%1%").arg(info.scalePercent) : QString());
    if (!item.placed)
      lines << tr("not placed");
    else if (!item.online)
      lines << tr("offline");

    p.setPen(item.placed ? QColor(Qt::white) : palette().color(QPalette::Text));
    const auto textBox = box.adjusted(4, 4, -4, -4);
    p.setFont(bold);
    p.drawText(textBox, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, item.computer);
    p.setFont(small);
    p.drawText(textBox, Qt::AlignCenter | Qt::TextWordWrap, lines.join('\n'));
    if (info.primary) {
      p.drawText(textBox, Qt::AlignHCenter | Qt::AlignBottom, QStringLiteral("★"));
    }
  }
}

void MonitorLayoutView::mousePressEvent(QMouseEvent *event)
{
  setFocus(Qt::MouseFocusReason);
  const auto list = items();
  const auto t = fitTransform(list);
  const auto *hit = itemAt(list, t, event->position());
  if (!hit) {
    m_selComputer.clear();
    m_selId.clear();
    Q_EMIT selectionChanged(QString());
    update();
    return;
  }
  select(hit->computer, QString::fromStdString(hit->monitor.info.id));
  if (event->button() == Qt::LeftButton) {
    m_dragging = true;
    m_dragTransform = t;
    m_grabOffset = t.toGlobal(event->position()) - hit->global.topLeft();
  }
}

void MonitorLayoutView::mouseMoveEvent(QMouseEvent *event)
{
  if (!m_dragging)
    return;
  const auto origin = m_dragTransform.toGlobal(event->position()) - m_grabOffset;
  moveSelected(static_cast<int>(std::lround(origin.x())), static_cast<int>(std::lround(origin.y())), true);
}

void MonitorLayoutView::mouseReleaseEvent(QMouseEvent *event)
{
  if (m_dragging && event->button() == Qt::LeftButton) {
    m_dragging = false;
    update();
  }
}

void MonitorLayoutView::keyPressEvent(QKeyEvent *event)
{
  if (m_selId.isEmpty() || !m_topology) {
    QWidget::keyPressEvent(event);
    return;
  }
  const int step = event->modifiers().testFlag(Qt::ShiftModifier) ? 10 : 1;
  int dx = 0;
  int dy = 0;
  switch (event->key()) {
  case Qt::Key_Left:
    dx = -step;
    break;
  case Qt::Key_Right:
    dx = step;
    break;
  case Qt::Key_Up:
    dy = -step;
    break;
  case Qt::Key_Down:
    dy = step;
    break;
  default:
    QWidget::keyPressEvent(event);
    return;
  }
  for (const auto &item : items()) {
    if (item.computer == m_selComputer && QString::fromStdString(item.monitor.info.id) == m_selId) {
      moveSelected(item.monitor.gx + dx, item.monitor.gy + dy, false);
      break;
    }
  }
}

void MonitorLayoutView::contextMenuEvent(QContextMenuEvent *event)
{
  const auto list = items();
  const auto *hit = itemAt(list, fitTransform(list), event->pos());
  if (!hit || !m_topology)
    return;
  const auto computer = hit->computer;
  const auto id = QString::fromStdString(hit->monitor.info.id);
  const bool placed = hit->placed;
  const bool online = hit->online;

  QMenu menu(this);
  auto *unplace = menu.addAction(tr("Remove from layout"));
  unplace->setEnabled(placed);
  auto *forget = menu.addAction(tr("Forget this monitor"));
  forget->setEnabled(!online);
  forget->setToolTip(tr("Only offline monitors can be forgotten; connected ones are reported again."));
  const auto *chosen = menu.exec(event->globalPos());
  if (chosen == unplace && m_topology->unplace(computer, id)) {
    Q_EMIT layoutChanged();
  } else if (chosen == forget && m_topology->forget(computer, id)) {
    m_selId.clear();
    Q_EMIT layoutChanged();
  }
  update();
}

void MonitorLayoutView::moveSelected(int gx, int gy, bool snap)
{
  if (!m_topology || m_selId.isEmpty())
    return;
  const auto list = items();
  const Item *self = nullptr;
  std::vector<Rect> others;
  for (const auto &item : list) {
    const bool isSelf = item.computer == m_selComputer && QString::fromStdString(item.monitor.info.id) == m_selId;
    if (isSelf)
      self = &item;
    else if (item.placed)
      others.push_back({static_cast<int32_t>(item.global.x()), static_cast<int32_t>(item.global.y()),
                        static_cast<int32_t>(item.global.width()), static_cast<int32_t>(item.global.height())});
  }
  if (!self)
    return;

  Rect moved = globalRect(self->monitor.info, gx, gy);
  if (snap) {
    const auto scale = m_dragging ? m_dragTransform.scale : fitTransform(list).scale;
    const auto tolerance = static_cast<int32_t>(std::ceil(kSnapPixels / std::max(scale, 1e-6)));
    moved = snapRect(moved, others, tolerance);
  }
  if (m_topology->setPosition(m_selComputer, m_selId, moved.x, moved.y)) {
    for (const auto &item : items()) {
      if (item.computer == m_selComputer && QString::fromStdString(item.monitor.info.id) == m_selId) {
        Q_EMIT selectionChanged(describe(item));
        break;
      }
    }
    Q_EMIT layoutChanged();
  }
  update();
}
