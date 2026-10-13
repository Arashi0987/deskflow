# Development/build image for the per-monitor layout fork.
# Mirrors the "debian" dependency set used by upstream CI
# (.github/actions/install-dependencies) plus Xvfb/xrandr for headless tests.
FROM debian:stable-slim

RUN apt-get update -qq \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
      cmake build-essential ninja-build git ca-certificates pkg-config \
      xorg-dev libx11-dev libxtst-dev libxinerama-dev libxrandr-dev libssl-dev libxkbfile-dev \
      qt6-base-dev qt6-tools-dev qt6-l10n-tools libxkbregistry-dev libxkbcommon-dev \
      libei-dev libportal-dev help2man \
      xvfb x11-xserver-utils xdotool imagemagick \
 && rm -rf /var/lib/apt/lists/*

ARG UID=1000
ARG GID=1000
RUN groupadd -g ${GID} dev 2>/dev/null || true \
 && useradd -m -u ${UID} -g ${GID} -s /bin/bash dev 2>/dev/null || true
USER ${UID}:${GID}
WORKDIR /src
