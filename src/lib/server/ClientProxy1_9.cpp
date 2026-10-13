/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/ClientProxy1_9.h"

#include "base/Log.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"

#include <cstring>

ClientProxy1_9::ClientProxy1_9(
    const std::string &name, deskflow::IStream *adoptedStream, Server *server, IEventQueue *events
)
    : ClientProxy1_8(name, adoptedStream, server, events)
{
  // do nothing
}

std::vector<deskflow::monitors::MonitorInfo> ClientProxy1_9::getMonitors() const
{
  if (m_monitors.empty()) {
    return ClientProxy1_8::getMonitors();
  }
  return m_monitors;
}

bool ClientProxy1_9::parseHandshakeMessage(const uint8_t *code)
{
  if (memcmp(code, kMsgDMonitors, 4) == 0) {
    return recvMonitors();
  }
  return ClientProxy1_8::parseHandshakeMessage(code);
}

bool ClientProxy1_9::parseMessage(const uint8_t *code)
{
  // the client follows DMON with DINF, which raises the shape changed event
  if (memcmp(code, kMsgDMonitors, 4) == 0) {
    return recvMonitors();
  }
  return ClientProxy1_8::parseMessage(code);
}

bool ClientProxy1_9::recvMonitors()
{
  std::string text;
  if (!ProtocolUtil::readf(getStream(), kMsgDMonitors + 4, &text)) {
    return false;
  }

  // malformed records are dropped; an empty list means "use the whole shape"
  m_monitors = deskflow::monitors::parseMonitors(text);
  LOG_DEBUG("received %zu monitor(s) from client \"%s\"", m_monitors.size(), getName().c_str());
  for (const auto &m : m_monitors) {
    LOG_DEBUG(
        "client \"%s\" monitor \"%s\" at %d,%d %dx%d scale %d%%", getName().c_str(), m.id.c_str(), m.local.x, m.local.y,
        m.local.w, m.local.h, m.scalePercent
    );
  }
  return true;
}
