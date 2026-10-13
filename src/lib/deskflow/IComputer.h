/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2003 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "common/MonitorLayout.h"
#include "deskflow/ClipboardTypes.h"

#include <vector>

class IClipboard;

//! Computer interface
/*!
This interface defines the methods common to all computers.
*/
class IComputer
{
public:
  virtual ~IComputer() = default;
  struct ClipboardInfo
  {
  public:
    ClipboardID m_id;
    uint32_t m_sequenceNumber;
  };

  //! @name accessors
  //@{

  //! Get event target
  /*!
  Returns the target used for events created by this object.
  */
  virtual void *getEventTarget() const = 0;

  //! Get clipboard
  /*!
  Save the contents of the clipboard indicated by \c id and return
  true iff successful.
  */
  virtual bool getClipboard(ClipboardID id, IClipboard *) const = 0;

  //! Get computer shape
  /*!
  Return the position of the upper-left corner of the computer in \c x and
  \c y and the size of the computer in \c width and \c height.
  */
  virtual void getShape(int32_t &x, int32_t &y, int32_t &width, int32_t &height) const = 0;

  //! Get cursor position
  /*!
  Return the current position of the cursor in \c x and \c y.
  */
  virtual void getCursorPos(int32_t &x, int32_t &y) const = 0;

  //! Get monitors
  /*!
  Return the monitors that make up the desktop, in the same coordinate space
  as getShape(). The default reports the whole desktop as a single monitor,
  which is what computers that cannot enumerate their monitors look like.
  */
  virtual std::vector<deskflow::monitors::MonitorInfo> getMonitors() const
  {
    int32_t x = 0;
    int32_t y = 0;
    int32_t w = 0;
    int32_t h = 0;
    getShape(x, y, w, h);
    return {deskflow::monitors::wholeDesktop(x, y, w, h)};
  }

  //@}
};
