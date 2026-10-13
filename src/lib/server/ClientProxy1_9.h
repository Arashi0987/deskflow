/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "server/ClientProxy1_8.h"

#include <vector>

//! Proxy for client implementing protocol version 1.9
/*!
Adds the DMON message: the client reports its individual monitors so the
server can route the pointer per monitor edge instead of per computer.
*/
class ClientProxy1_9 : public ClientProxy1_8
{
public:
  ClientProxy1_9(const std::string &name, deskflow::IStream *adoptedStream, Server *server, IEventQueue *events);
  ~ClientProxy1_9() override = default;

  std::vector<deskflow::monitors::MonitorInfo> getMonitors() const override;

protected:
  bool parseHandshakeMessage(const uint8_t *code) override;
  bool parseMessage(const uint8_t *code) override;

private:
  bool recvMonitors();

  std::vector<deskflow::monitors::MonitorInfo> m_monitors;
};
