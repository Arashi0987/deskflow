/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "common/MonitorLayout.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace deskflow::monitors {

namespace {

const std::vector<MonitorInfo> kNoMonitors;

std::string escape(const std::string &value)
{
  static const char *const hex = "0123456789ABCDEF";
  std::string out;
  out.reserve(value.size());
  for (const auto ch : value) {
    const auto c = static_cast<unsigned char>(ch);
    if (c == '%' || c == ';' || c == '=' || c < 0x20 || c == 0x7f) {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 0xf];
    } else {
      out += ch;
    }
  }
  return out;
}

int hexValue(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  return -1;
}

std::string unescape(const std::string &value)
{
  std::string out;
  out.reserve(value.size());
  for (size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '%' && i + 2 < value.size()) {
      const int hi = hexValue(value[i + 1]);
      const int lo = hexValue(value[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out += static_cast<char>((hi << 4) | lo);
        i += 2;
        continue;
      }
    }
    out += value[i];
  }
  return out;
}

std::map<std::string, std::string> splitRecord(const std::string &record)
{
  std::map<std::string, std::string> fields;
  size_t start = 0;
  while (start <= record.size()) {
    size_t end = record.find(';', start);
    if (end == std::string::npos)
      end = record.size();
    const auto pair = record.substr(start, end - start);
    if (const auto eq = pair.find('='); eq != std::string::npos && eq > 0) {
      fields[pair.substr(0, eq)] = unescape(pair.substr(eq + 1));
    }
    start = end + 1;
  }
  return fields;
}

std::optional<int32_t> toInt(const std::map<std::string, std::string> &fields, const char *key)
{
  const auto it = fields.find(key);
  if (it == fields.end() || it->second.empty())
    return std::nullopt;
  char *end = nullptr;
  const long value = std::strtol(it->second.c_str(), &end, 10);
  if (end == nullptr || *end != '\0')
    return std::nullopt;
  return static_cast<int32_t>(value);
}

void appendField(std::string &out, const char *key, const std::string &value)
{
  if (!out.empty())
    out += ';';
  out += key;
  out += '=';
  out += escape(value);
}

void appendField(std::string &out, const char *key, int32_t value)
{
  appendField(out, key, std::to_string(value));
}

std::optional<MonitorInfo> monitorFromFields(const std::map<std::string, std::string> &fields)
{
  const auto id = fields.find("id");
  const auto x = toInt(fields, "x");
  const auto y = toInt(fields, "y");
  const auto w = toInt(fields, "w");
  const auto h = toInt(fields, "h");
  if (id == fields.end() || id->second.empty() || !x || !y || !w || !h || *w <= 0 || *h <= 0)
    return std::nullopt;

  MonitorInfo info;
  info.id = id->second;
  if (const auto label = fields.find("label"); label != fields.end())
    info.label = label->second;
  info.local = {*x, *y, *w, *h};
  info.scalePercent = toInt(fields, "scale").value_or(100);
  if (info.scalePercent <= 0)
    info.scalePercent = 100;
  info.rotation = toInt(fields, "rot").value_or(0);
  info.primary = toInt(fields, "primary").value_or(0) != 0;
  return info;
}

std::string monitorFields(const MonitorInfo &info)
{
  std::string out;
  appendField(out, "id", info.id);
  if (!info.label.empty())
    appendField(out, "label", info.label);
  appendField(out, "x", info.local.x);
  appendField(out, "y", info.local.y);
  appendField(out, "w", info.local.w);
  appendField(out, "h", info.local.h);
  appendField(out, "scale", info.scalePercent);
  appendField(out, "rot", info.rotation);
  appendField(out, "primary", info.primary ? 1 : 0);
  return out;
}

int32_t clampInt(int32_t v, int32_t lo, int32_t hi)
{
  return std::max(lo, std::min(v, hi));
}

int32_t clampToSpan(double v, int32_t start, int32_t length)
{
  const auto i = static_cast<int64_t>(std::floor(v));
  return static_cast<int32_t>(std::clamp<int64_t>(i, start, static_cast<int64_t>(start) + length - 1));
}

// Shared length of two 1-D spans
int32_t overlapLength(int32_t a0, int32_t a1, int32_t b0, int32_t b1)
{
  return std::max(0, std::min(a1, b1) - std::max(a0, b0));
}

bool touches(const Rect &a, const Rect &b, Direction dir)
{
  switch (dir) {
    using enum Direction;
  case Right:
    return b.x == a.right() && overlapLength(a.y, a.bottom(), b.y, b.bottom()) > 0;
  case Left:
    return b.right() == a.x && overlapLength(a.y, a.bottom(), b.y, b.bottom()) > 0;
  case Bottom:
    return b.y == a.bottom() && overlapLength(a.x, a.right(), b.x, b.right()) > 0;
  case Top:
    return b.bottom() == a.y && overlapLength(a.x, a.right(), b.x, b.right()) > 0;
  default:
    return false;
  }
}

constexpr Direction kDirections[] = {Direction::Left, Direction::Right, Direction::Top, Direction::Bottom};

} // namespace

int32_t logicalExtent(int32_t pixels, int32_t scalePercent)
{
  if (scalePercent <= 0)
    scalePercent = 100;
  const int64_t value = (static_cast<int64_t>(pixels) * 100 + scalePercent / 2) / scalePercent;
  return static_cast<int32_t>(std::max<int64_t>(1, value));
}

Rect globalRect(const MonitorInfo &info, int32_t gx, int32_t gy)
{
  return {gx, gy, logicalExtent(info.local.w, info.scalePercent), logicalExtent(info.local.h, info.scalePercent)};
}

MonitorInfo wholeDesktop(int32_t x, int32_t y, int32_t w, int32_t h)
{
  MonitorInfo info;
  info.id = kWholeDesktopId;
  info.label = "Desktop";
  info.local = {x, y, w, h};
  info.primary = true;
  return info;
}

std::string serializeMonitor(const MonitorInfo &info)
{
  return monitorFields(info);
}

std::optional<MonitorInfo> parseMonitor(const std::string &record)
{
  return monitorFromFields(splitRecord(record));
}

std::string serializeMonitors(const std::vector<MonitorInfo> &monitors)
{
  std::string out;
  for (const auto &monitor : monitors) {
    if (!out.empty())
      out += '\n';
    out += serializeMonitor(monitor);
  }
  return out;
}

std::vector<MonitorInfo> parseMonitors(const std::string &text)
{
  std::vector<MonitorInfo> monitors;
  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (line.empty())
      continue;
    if (auto monitor = parseMonitor(line); monitor) {
      // ids must be unique per computer; first one wins
      const bool duplicate = std::any_of(monitors.begin(), monitors.end(), [&](const MonitorInfo &m) {
        return m.id == monitor->id;
      });
      if (!duplicate)
        monitors.push_back(*monitor);
    }
  }
  return monitors;
}

std::string serializeStored(const StoredMonitor &stored)
{
  std::string out = monitorFields(stored.info);
  if (stored.placed) {
    appendField(out, "gx", stored.gx);
    appendField(out, "gy", stored.gy);
  }
  return out;
}

std::optional<StoredMonitor> parseStored(const std::string &record)
{
  const auto fields = splitRecord(record);
  auto info = monitorFromFields(fields);
  if (!info)
    return std::nullopt;
  StoredMonitor stored;
  stored.info = *info;
  const auto gx = toInt(fields, "gx");
  const auto gy = toInt(fields, "gy");
  stored.placed = gx.has_value() && gy.has_value();
  stored.gx = gx.value_or(0);
  stored.gy = gy.value_or(0);
  return stored;
}

Rect snapRect(const Rect &moving, const std::vector<Rect> &others, int32_t tolerance)
{
  // smallest correction (within tolerance) that makes an edge touch or align with another
  // rectangle that is near enough on the other axis to matter
  auto bestDelta = [tolerance](const std::vector<int32_t> &deltas) {
    int32_t chosen = 0;
    int32_t bestDistance = tolerance + 1;
    for (const auto d : deltas) {
      if (std::abs(d) < bestDistance) {
        bestDistance = std::abs(d);
        chosen = d;
      }
    }
    return chosen;
  };

  std::vector<int32_t> xDeltas;
  std::vector<int32_t> yDeltas;
  for (const auto &o : others) {
    const bool nearVertically = moving.y < o.bottom() + tolerance && o.y < moving.bottom() + tolerance;
    const bool nearHorizontally = moving.x < o.right() + tolerance && o.x < moving.right() + tolerance;
    if (nearVertically) {
      xDeltas.insert(xDeltas.end(), {o.right() - moving.x, o.x - moving.right(), o.x - moving.x, o.right() - moving.right()});
    }
    if (nearHorizontally) {
      yDeltas.insert(yDeltas.end(), {o.bottom() - moving.y, o.y - moving.bottom(), o.y - moving.y, o.bottom() - moving.bottom()});
    }
  }

  Rect result = moving;
  result.x += bestDelta(xDeltas);
  result.y += bestDelta(yDeltas);
  return result;
}

//
// MonitorLayout
//

void MonitorLayout::clear()
{
  m_placements.clear();
  m_live.clear();
}

void MonitorLayout::setPlacement(const std::string &computer, const std::string &monitorId, int32_t gx, int32_t gy)
{
  m_placements[{computer, monitorId}] = {gx, gy};
}

void MonitorLayout::removePlacement(const std::string &computer, const std::string &monitorId)
{
  m_placements.erase({computer, monitorId});
}

void MonitorLayout::clearPlacements(const std::string &computer)
{
  std::erase_if(m_placements, [&computer](const auto &entry) { return entry.first.computer == computer; });
}

bool MonitorLayout::hasPlacement(const std::string &computer, const std::string &monitorId) const
{
  return m_placements.contains({computer, monitorId});
}

std::optional<std::pair<int32_t, int32_t>>
MonitorLayout::placement(const std::string &computer, const std::string &monitorId) const
{
  if (const auto it = m_placements.find({computer, monitorId}); it != m_placements.end())
    return it->second;
  return std::nullopt;
}

void MonitorLayout::setStored(const std::string &computer, const std::vector<StoredMonitor> &stored)
{
  clearPlacements(computer);
  for (const auto &monitor : stored) {
    if (monitor.placed)
      setPlacement(computer, monitor.info.id, monitor.gx, monitor.gy);
  }
}

void MonitorLayout::setLiveMonitors(const std::string &computer, const std::vector<MonitorInfo> &monitors)
{
  std::vector<MonitorInfo> valid;
  for (const auto &monitor : monitors) {
    if (!monitor.id.empty() && monitor.local.isValid())
      valid.push_back(monitor);
  }
  m_live[computer] = valid;
}

void MonitorLayout::removeLiveMonitors(const std::string &computer)
{
  m_live.erase(computer);
}

const std::vector<MonitorInfo> &MonitorLayout::liveMonitors(const std::string &computer) const
{
  if (const auto it = m_live.find(computer); it != m_live.end())
    return it->second;
  return kNoMonitors;
}

std::vector<PlacedMonitor> MonitorLayout::routableMonitors() const
{
  std::vector<PlacedMonitor> result;
  for (const auto &[computer, monitors] : m_live) {
    for (const auto &monitor : monitors) {
      if (const auto pos = placement(computer, monitor.id); pos) {
        result.push_back({computer, monitor, globalRect(monitor, pos->first, pos->second)});
      }
    }
  }
  return result;
}

std::vector<PlacedMonitor> MonitorLayout::routableMonitors(const std::string &computer) const
{
  std::vector<PlacedMonitor> result;
  for (const auto &monitor : liveMonitors(computer)) {
    if (const auto pos = placement(computer, monitor.id); pos) {
      result.push_back({computer, monitor, globalRect(monitor, pos->first, pos->second)});
    }
  }
  return result;
}

bool MonitorLayout::isRouted(const std::string &computer) const
{
  return !routableMonitors(computer).empty();
}

uint32_t MonitorLayout::remoteSides(const std::string &computer) const
{
  const auto all = routableMonitors();
  uint32_t sides = 0;
  for (const auto &own : all) {
    if (own.computer != computer)
      continue;
    for (const auto dir : kDirections) {
      for (const auto &other : all) {
        if (other.computer != computer && touches(own.global, other.global, dir)) {
          sides |= 1u << static_cast<int>(dir);
          break;
        }
      }
    }
  }
  return sides;
}

const PlacedMonitor *MonitorLayout::sourceMonitor(
    const std::vector<PlacedMonitor> &own, int32_t fromX, int32_t fromY, int32_t toX, int32_t toY
) const
{
  for (const auto &m : own) {
    if (m.info.local.contains(fromX, fromY))
      return &m;
  }
  for (const auto &m : own) {
    if (m.info.local.contains(toX, toY))
      return &m;
  }

  // the last position is in dead space (e.g. right after a hotplug); use the nearest monitor
  const PlacedMonitor *nearest = nullptr;
  int64_t nearestDistance = 0;
  for (const auto &m : own) {
    const auto &r = m.info.local;
    const int64_t dx = fromX < r.x ? r.x - fromX : (fromX >= r.right() ? fromX - r.right() + 1 : 0);
    const int64_t dy = fromY < r.y ? r.y - fromY : (fromY >= r.bottom() ? fromY - r.bottom() + 1 : 0);
    const int64_t distance = dx * dx + dy * dy;
    if (nearest == nullptr || distance < nearestDistance) {
      nearest = &m;
      nearestDistance = distance;
    }
  }
  return nearest;
}

const PlacedMonitor *MonitorLayout::neighbour(
    const std::vector<PlacedMonitor> &all, const PlacedMonitor &src, Direction dir, double along
) const
{
  const PlacedMonitor *found = nullptr;
  for (const auto &m : all) {
    if (m.computer == src.computer && m.info.id == src.info.id)
      continue;
    const auto &g = m.global;
    const auto &s = src.global;
    bool hit = false;
    switch (dir) {
      using enum Direction;
    case Right:
      hit = g.x == s.right() && along >= g.y && along < g.bottom();
      break;
    case Left:
      hit = g.right() == s.x && along >= g.y && along < g.bottom();
      break;
    case Bottom:
      hit = g.y == s.bottom() && along >= g.x && along < g.right();
      break;
    case Top:
      hit = g.bottom() == s.y && along >= g.x && along < g.right();
      break;
    default:
      break;
    }
    if (!hit)
      continue;
    if (found == nullptr || Key{m.computer, m.info.id} < Key{found->computer, found->info.id})
      found = &m;
  }
  return found;
}

bool MonitorLayout::crossEdge(
    const std::vector<PlacedMonitor> &all, const PlacedMonitor &src, Direction dir, int32_t edgeX, int32_t edgeY,
    int32_t toX, int32_t toY, int32_t entryInset, Step &step
) const
{
  const auto &sl = src.info.local;
  const auto &sg = src.global;
  const bool horizontal = dir == Direction::Left || dir == Direction::Right;

  // position along the edge in global units, using the pixel centre
  const double along = horizontal ? sg.y + (edgeY - sl.y + 0.5) * sg.h / static_cast<double>(sl.h)
                                  : sg.x + (edgeX - sl.x + 0.5) * sg.w / static_cast<double>(sl.w);

  const auto *dst = neighbour(all, src, dir, along);
  if (dst == nullptr)
    return false;

  const auto &dl = dst->info.local;
  const auto &dg = dst->global;
  int32_t x = 0;
  int32_t y = 0;
  if (horizontal) {
    const int32_t inset = clampInt(entryInset, 0, dl.w - 1);
    x = dir == Direction::Right ? dl.x + inset : dl.right() - 1 - inset;
    y = clampToSpan(dl.y + (along - dg.y) * dl.h / static_cast<double>(dg.h), dl.y, dl.h);
  } else {
    const int32_t inset = clampInt(entryInset, 0, dl.h - 1);
    y = dir == Direction::Bottom ? dl.y + inset : dl.bottom() - 1 - inset;
    x = clampToSpan(dl.x + (along - dg.x) * dl.w / static_cast<double>(dg.w), dl.x, dl.w);
  }

  step.computer = dst->computer;
  step.monitorId = dst->info.id;
  step.dir = dir;
  if (dst->computer == src.computer) {
    step.kind = StepKind::Local;
    // the OS already moved the pointer onto the monitor the layout agrees with; keep its position
    if (dl.contains(toX, toY)) {
      x = toX;
      y = toY;
    }
  } else {
    step.kind = StepKind::Remote;
  }
  step.x = x;
  step.y = y;
  return true;
}

MonitorLayout::Step MonitorLayout::route(
    const std::string &computer, int32_t fromX, int32_t fromY, int32_t toX, int32_t toY, int32_t zone,
    int32_t entryInset
) const
{
  Step step;
  const auto all = routableMonitors();
  std::vector<PlacedMonitor> own;
  for (const auto &m : all) {
    if (m.computer == computer)
      own.push_back(m);
  }
  if (own.empty()) {
    step.kind = StepKind::Unrouted;
    return step;
  }

  const auto *src = sourceMonitor(own, fromX, fromY, toX, toY);
  const auto &sl = src->info.local;
  step.clampX = clampInt(toX, sl.x, sl.right() - 1);
  step.clampY = clampInt(toY, sl.y, sl.bottom() - 1);

  auto stay = [&](const std::string &monitorId) {
    step.kind = StepKind::Stay;
    step.computer = computer;
    step.monitorId = monitorId;
    step.x = toX;
    step.y = toY;
    step.dir = Direction::NoDirection;
    return step;
  };

  if (sl.contains(toX, toY)) {
    if (zone <= 0)
      return stay(src->info.id);

    // pushing against an edge of the server's own monitor. only a remote neighbour
    // triggers here; moves between local monitors are left to the OS.
    std::vector<Direction> pushing;
    if (toX < sl.x + zone)
      pushing.push_back(Direction::Left);
    else if (toX >= sl.right() - zone)
      pushing.push_back(Direction::Right);
    if (toY < sl.y + zone)
      pushing.push_back(Direction::Top);
    else if (toY >= sl.bottom() - zone)
      pushing.push_back(Direction::Bottom);

    for (const auto dir : pushing) {
      const int32_t edgeX = dir == Direction::Left ? sl.x : (dir == Direction::Right ? sl.right() - 1 : toX);
      const int32_t edgeY = dir == Direction::Top ? sl.y : (dir == Direction::Bottom ? sl.bottom() - 1 : toY);
      Step candidate = step;
      if (crossEdge(all, *src, dir, edgeX, edgeY, toX, toY, entryInset, candidate) &&
          candidate.kind == StepKind::Remote) {
        return candidate;
      }
    }
    return stay(src->info.id);
  }

  // the pointer left the source monitor; find which edge(s) it crossed and in which order
  const int32_t fx = clampInt(fromX, sl.x, sl.right() - 1);
  const int32_t fy = clampInt(fromY, sl.y, sl.bottom() - 1);
  struct Exit
  {
    double t;
    Direction dir;
  };
  std::vector<Exit> exits;
  if (toX < sl.x)
    exits.push_back({(fx - (sl.x - 0.5)) / static_cast<double>(fx - toX), Direction::Left});
  else if (toX >= sl.right())
    exits.push_back({((sl.right() - 0.5) - fx) / static_cast<double>(toX - fx), Direction::Right});
  if (toY < sl.y)
    exits.push_back({(fy - (sl.y - 0.5)) / static_cast<double>(fy - toY), Direction::Top});
  else if (toY >= sl.bottom())
    exits.push_back({((sl.bottom() - 0.5) - fy) / static_cast<double>(toY - fy), Direction::Bottom});
  std::stable_sort(exits.begin(), exits.end(), [](const Exit &a, const Exit &b) { return a.t < b.t; });

  for (const auto &exit : exits) {
    const double cx = fx + exit.t * (toX - fx);
    const double cy = fy + exit.t * (toY - fy);
    const int32_t edgeX = clampToSpan(cx, sl.x, sl.w);
    const int32_t edgeY = clampToSpan(cy, sl.y, sl.h);
    Step candidate = step;
    if (crossEdge(all, *src, exit.dir, edgeX, edgeY, toX, toY, entryInset, candidate)) {
      return candidate;
    }
  }

  // no neighbour in the global layout: the OS arrangement applies
  for (const auto &monitor : liveMonitors(computer)) {
    if (monitor.local.contains(toX, toY))
      return stay(monitor.id);
  }

  step.kind = StepKind::Blocked;
  step.computer = computer;
  step.monitorId = src->info.id;
  step.x = step.clampX;
  step.y = step.clampY;
  step.dir = exits.empty() ? Direction::NoDirection : exits.front().dir;
  return step;
}

std::vector<MonitorLayout::Issue> MonitorLayout::validate() const
{
  std::vector<Issue> issues;
  const auto all = routableMonitors();

  for (size_t i = 0; i < all.size(); ++i) {
    for (size_t j = i + 1; j < all.size(); ++j) {
      if (all[i].global.intersects(all[j].global)) {
        issues.push_back(
            {IssueKind::Overlap, all[i].computer, all[i].info.id, all[j].computer, all[j].info.id}
        );
      }
    }
  }

  if (all.size() > 1) {
    for (const auto &m : all) {
      bool connected = false;
      for (const auto &other : all) {
        if (&other == &m)
          continue;
        for (const auto dir : kDirections) {
          if (touches(m.global, other.global, dir)) {
            connected = true;
            break;
          }
        }
        if (connected)
          break;
      }
      if (!connected)
        issues.push_back({IssueKind::Isolated, m.computer, m.info.id, {}, {}});
    }
  }

  for (const auto &[computer, monitors] : m_live) {
    for (const auto &monitor : monitors) {
      if (!hasPlacement(computer, monitor.id))
        issues.push_back({IssueKind::Unplaced, computer, monitor.id, {}, {}});
    }
  }

  for (const auto &[key, pos] : m_placements) {
    const auto live = m_live.find(key.computer);
    if (live == m_live.end())
      continue; // whole computer is offline; that's expected
    const bool present = std::any_of(live->second.begin(), live->second.end(), [&key](const MonitorInfo &m) {
      return m.id == key.id;
    });
    if (!present)
      issues.push_back({IssueKind::Offline, key.computer, key.id, {}, {}});
  }

  return issues;
}

const char *MonitorLayout::dirName(Direction dir)
{
  switch (dir) {
    using enum Direction;
  case Left:
    return "left";
  case Right:
    return "right";
  case Top:
    return "up";
  case Bottom:
    return "down";
  default:
    return "none";
  }
}

} // namespace deskflow::monitors
