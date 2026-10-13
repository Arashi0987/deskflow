/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include <QTest>

class MonitorLayoutTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  // geometry
  void logicalExtent_scalesByDpi();
  void snapRect_snapsToAdjacentEdge();
  void snapRect_leavesFarRectAlone();

  // routing
  void route_unplacedComputer_isUnrouted();
  void route_insideMonitor_stays();
  void route_horizontal_crossesToRemoteMonitor();
  void route_partialEdge_blocksOutsideSharedSegment();
  void route_gap_isAWall();
  void route_vertical_crossesDown();
  void route_vertical_crossesUp();
  void route_interleaved_overridesOsNeighbour();
  void route_interleaved_remoteToSecondLocalMonitor();
  void route_noLayoutNeighbour_fallsBackToOsArrangement();
  void route_layoutSendsToDifferentLocalMonitor_warpsLocally();
  void route_mixedDpi_mapsProportionally();
  void route_portrait_mapsAlongLongEdge();
  void route_negativeCoordinates();
  void route_roundTrip_doesNotTeleport();
  void route_zone_pushingOnServerEdgeTriggersRemote();
  void route_zone_localNeighbourDoesNotTrigger();
  void route_zone_entryInsetAvoidsJumpBack();
  void route_diagonalExit_usesFirstEdgeCrossed();
  void route_diagonalExit_fallsBackToSecondEdge();
  void route_offlineMonitor_isIgnored();
  void route_deadSpaceStart_usesNearestMonitor();
  void route_overlap_isDeterministic();
  void route_demoTopology_twoComputersFourMonitors();

  // policy helpers
  void remoteSides_reportsEdgesWithRemoteNeighbours();
  void validate_reportsProblems();

  // persistence
  void serialize_roundTrip();
  void serialize_escapesSeparators();
  void parse_skipsMalformedAndUnknownFields();
  void stored_roundTripAndPlacementLoad();
};
