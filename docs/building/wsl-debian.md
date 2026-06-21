# Remere's Map Editor

## Supported OS

- Windows 11 + WSL2 with Debian

## 1. Install the required software

The following command will install Git, CMake, a compiler and the libraries used by Remere's Map Editor.

Git will be used to download the source code, and CMake will be used to generate the build files.

```bash
sudo apt update
sudo apt install git cmake build-essential autoconf autoconf-archive automake libtool ca-certificates curl zip unzip tar pkg-config ninja-build ccache bison linux-headers-amd64 \
libxi-dev libgl1-mesa-dev libegl1-mesa-dev libglu1-mesa-dev mesa-common-dev libxrandr-dev \
libxxf86vm-dev libx11-dev libxft-dev libxext-dev libwayland-dev libxkbcommon-dev libibus-1.0-dev libasound2-dev \
libxmu-dev libdbus-1-dev libxtst-dev linux-libc-dev libarchive-dev libwxgtk3.2-dev python3-distutils-extra python3-jinja2 libxrender-dev -y
```

## 2. Set up vcpkg

```bash
git clone https://github.com/microsoft/vcpkg
cd vcpkg
./bootstrap-vcpkg.sh
cd ..
```

### Configure environment for vcpkg (important)

To ensure **CMake presets and CLion detect vcpkg automatically without modifying the preset**, export `VCPKG_ROOT` globally for the user:

```bash
mkdir -p ~/.config/environment.d
echo 'VCPKG_ROOT=$HOME/vcpkg' > ~/.config/environment.d/10-vcpkg.conf
```

Note: This approach requires systemd. If `systemctl status` shows "unit not found", enable systemd by editing `/etc/wsl.conf` (e.g., `sudo nano /etc/wsl.conf`) and adding:

```ini
[boot]
systemd=true
```

Then run `wsl --shutdown` in _Powershell_ and restart WSL.

Fallback (for non-systemd setups):
```bash
echo 'export VCPKG_ROOT=$HOME/vcpkg' >> ~/.bashrc
source ~/.bashrc
```

Now **log out and log in again** (or reboot), then verify:

```bash
echo $VCPKG_ROOT
# Expected output:
# /home/<user>/vcpkg
```

Also validate the toolchain file exists:

```bash
test -f "$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" && echo "vcpkg OK" || echo "vcpkg not found"
```

## 3. Download the source code

```bash
git clone --depth 1 https://github.com/opentibiabr/remeres-map-editor.git
cd remeres-map-editor
```

## 4. Folder structure

```bash
.
├── remeres-map-editor
└── vcpkg
```

## 5. Configure and build

```bash
cmake --preset linux-release -DTOGGLE_BIN_FOLDER=ON
cmake --build --preset linux-release -j4
```

> -- Running vcpkg install 
**This step will take a long time on the first run, as it needs to download and install all the dependencies, so be patient!**

---
