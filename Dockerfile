# syntax=docker/dockerfile:1.7

FROM ubuntu:24.04 AS dependencies

ARG DEBIAN_FRONTEND=noninteractive
ENV DEBIAN_FRONTEND=${DEBIAN_FRONTEND}
ENV TZ=Etc/UTC
ARG VCPKG_FEED_URL
ARG VCPKG_FEED_USERNAME
ARG VCPKG_BINARY_CACHE_ACCESS=read
ARG VCPKG_BINARY_SOURCES
ENV VCPKG_BINARY_SOURCES=${VCPKG_BINARY_SOURCES}

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
	--mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
	apt-get update && apt-get install -y --no-install-recommends \
	autoconf \
	autoconf-archive \
	automake \
	bison \
	build-essential \
	ca-certificates \
	cmake \
	curl \
	git \
	libasound2-dev \
	libdbus-1-dev \
	libegl1-mesa-dev \
	libgl1-mesa-dev \
	libglu1-mesa-dev \
	libgtk-3-dev \
	libibus-1.0-dev \
	libltdl-dev \
	libtool \
	libwayland-dev \
	libx11-dev \
	libxext-dev \
	libxft-dev \
	libxi-dev \
	libxkbcommon-dev \
	libxmu-dev \
	libxrandr-dev \
	libxrender-dev \
	libxtst-dev \
	libxxf86vm-dev \
	linux-libc-dev \
	mesa-common-dev \
	mono-complete \
	ninja-build \
	pkg-config \
	python3 \
	python3-venv \
	tar \
	tzdata \
	unzip \
	zip \
	&& ln -snf "/usr/share/zoneinfo/${TZ}" /etc/localtime \
	&& echo "${TZ}" > /etc/timezone \
	&& apt-get clean \
	&& rm -rf /var/lib/apt/lists/*

WORKDIR /opt
COPY vcpkg.json /opt/vcpkg.json
RUN vcpkgCommitId="$(grep '.builtin-baseline' vcpkg.json | awk -F: '{print $2}' | tr -d ',\" ')" \
	&& echo "vcpkg commit ID: ${vcpkgCommitId}" \
	&& git clone https://github.com/microsoft/vcpkg.git \
	&& cd vcpkg \
	&& git checkout "${vcpkgCommitId}" \
	&& ./bootstrap-vcpkg.sh

WORKDIR /opt/vcpkg_manifest
COPY vcpkg.json /opt/vcpkg_manifest/

RUN --mount=type=secret,id=github_token \
	--mount=type=cache,target=/opt/vcpkg/downloads \
	--mount=type=cache,target=/opt/vcpkg/buildtrees \
	--mount=type=cache,target=/opt/vcpkg/packages \
	--mount=type=cache,target=/root/.cache/vcpkg \
	/bin/bash -euo pipefail -c '\
		nuget_config=""; \
		if [ -s /run/secrets/github_token ] && [ -n "${VCPKG_FEED_URL:-}" ] && [ -n "${VCPKG_FEED_USERNAME:-}" ]; then \
			nuget_auth_token="$(cat /run/secrets/github_token)"; \
			nuget_config="/tmp/nuget.config"; \
			printf "%s\n" \
				"<?xml version=\"1.0\" encoding=\"utf-8\"?>" \
				"<configuration>" \
				"  <packageSources>" \
				"    <add key=\"GitHubPackages\" value=\"${VCPKG_FEED_URL}\" />" \
				"  </packageSources>" \
				"  <packageSourceCredentials>" \
				"    <GitHubPackages>" \
				"      <add key=\"Username\" value=\"${VCPKG_FEED_USERNAME}\" />" \
				"      <add key=\"ClearTextPassword\" value=\"${nuget_auth_token}\" />" \
				"    </GitHubPackages>" \
				"  </packageSourceCredentials>" \
				"  <config>" \
				"    <add key=\"defaultPushSource\" value=\"GitHubPackages\" />" \
				"  </config>" \
				"</configuration>" \
				> "${nuget_config}"; \
			export VCPKG_NUGET_API_KEY="${nuget_auth_token}"; \
			export VCPKG_BINARY_SOURCES="clear;nugetconfig,${nuget_config},${VCPKG_BINARY_CACHE_ACCESS};nugettimeout,1200"; \
		elif [ -n "${VCPKG_BINARY_SOURCES:-}" ]; then \
			echo "Using provided VCPKG_BINARY_SOURCES."; \
		else \
			unset VCPKG_BINARY_SOURCES; \
		fi; \
		/opt/vcpkg/vcpkg install \
			--x-manifest-root=/opt/vcpkg_manifest \
			--x-install-root=/opt/vcpkg_installed \
			--triplet=x64-linux \
			--host-triplet=x64-linux; \
		if [ -n "${nuget_config}" ]; then rm -f "${nuget_config}"; fi'

FROM dependencies AS build

WORKDIR /srv
COPY CMakeLists.txt CMakePresets.json vcpkg.json /srv/
COPY cmake /srv/cmake
COPY source /srv/source
COPY brushes /srv/brushes
COPY icons /srv/icons
COPY --from=dependencies /opt/vcpkg_installed /srv/vcpkg_installed

RUN export VCPKG_ROOT=/opt/vcpkg \
	&& cmake --preset linux-release \
		-DTOGGLE_BIN_FOLDER=ON \
		-DOPTIONS_ENABLE_IPO=OFF \
		-DVCPKG_MANIFEST_INSTALL=OFF \
		-DVCPKG_INSTALLED_DIR=/srv/vcpkg_installed \
	&& cmake --build --preset linux-release

FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
ENV DEBIAN_FRONTEND=${DEBIAN_FRONTEND}
ENV TZ=Etc/UTC

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
	--mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
	apt-get update && apt-get install -y --no-install-recommends \
	ca-certificates \
	libasound2t64 \
	libdbus-1-3 \
	libegl1 \
	libgl1 \
	libglu1-mesa \
	libgtk-3-0 \
	libnotify4 \
	libsecret-1-0 \
	libwayland-client0 \
	libx11-6 \
	libxext6 \
	libxft2 \
	libxi6 \
	libxkbcommon0 \
	libxmu6 \
	libxrandr2 \
	libxrender1 \
	libxtst6 \
	libxxf86vm1 \
	tzdata \
	&& ln -snf "/usr/share/zoneinfo/${TZ}" /etc/localtime \
	&& echo "${TZ}" > /etc/timezone \
	&& apt-get clean \
	&& rm -rf /var/lib/apt/lists/* \
	&& groupadd --system rme \
	&& useradd --system --create-home --gid rme --home-dir /home/rme rme \
	&& install -d -o rme -g rme /rme

WORKDIR /rme
COPY --from=build --chown=rme:rme /srv/build/linux-release/bin/ /rme/
COPY --chown=rme:rme brushes /rme/brushes
COPY --chown=rme:rme data /rme/data
COPY --chown=rme:rme icons /rme/icons
COPY --chown=rme:rme rme_icon.ico remeres.exe.manifest LICENSE.rtf /rme/

USER rme
CMD ["./canary-map-editor"]
