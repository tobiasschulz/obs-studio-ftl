sudo apt install build-essential pkg-config cmake git checkinstall \
        libx11-dev libgl1-mesa-dev libpulse-dev libxcomposite-dev \
        libxinerama-dev libv4l-dev libudev-dev libfreetype6-dev \
        libfontconfig-dev qtbase5-dev libqt5x11extras5-dev libx264-dev \
        libxcb-xinerama0-dev libxcb-shm0-dev libjack-jackd2-dev libcurl4-openssl-dev \
        libavcodec-dev libavfilter-dev libavdevice-dev libfdk-aac-dev

test -d ~/src || mkdir ~/src
cd ~/src

rm -rf obs-studio-ftl && \
git clone --recursive -b ftl https://github.com/tobiasschulz/obs-studio-ftl.git && \
cd obs-studio-ftl && \
mkdir build && cd build && \
cmake -DUNIX_STRUCTURE=1 -DCMAKE_INSTALL_PREFIX=/usr .. && \
make -j4 && \
sudo checkinstall --pkgname=obs-studio --fstrans=no --backup=no --pkgversion="$(date +%Y%m%d)-git" --deldoc=yes

