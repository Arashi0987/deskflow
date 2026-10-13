/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "client/ServerProxy1_9.h"

#include "base/Log.h"
#include "client/Client.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"

ServerProxy1_9::ServerProxy1_9(Client *client, deskflow::IStream *stream, IEventQueue *events)
    : ServerProxy1_8(client, stream, events)
{
  // do nothing
}

void ServerProxy1_9::sendInfo(const ClientInfo &info)
{
  const auto monitors = getClient()->getMonitors();
  const auto text = deskflow::monitors::serializeMonitors(monitors);
  LOG_DEBUG("sending %zu monitor(s)", monitors.size());
  ProtocolUtil::writef(getStream(), kMsgDMonitors, &text);
  ServerProxy1_8::sendInfo(info);
}
