#!/usr/bin/bash

wget 'https://sourceforge.net/projects/qt5ct/files/latest/download'
tar xf download
cd qt5ct*
qmake
sudo make install
cd ..

if [ -n "$1" ]; then
    VERSION="$1"
else
    VERSION=$(grep -m1 '^CMAKE_PROJECT_VERSION:' build/CMakeCache.txt | cut -d= -f2)
fi

# echo VERSION was set to $VERSION
# if [[ $2 == *"-extra-plugins"* ]]; then
#         PLUGINS=$2
# fi

wget -c -nv "https://github.com/probonopd/linuxdeployqt/releases/download/continuous/linuxdeployqt-continuous-x86_64.AppImage"
chmod a+x linuxdeployqt-continuous-x86_64.AppImage

mkdir -p bin/appdir/usr
DESTDIR="$PWD/bin/appdir" cmake --install build --prefix /usr
cp dist/linux/hicolor/256x256/apps/io.github.apollo24k.qmedia.png bin/appdir/
cd bin
rm qmedia
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/lib/
../linuxdeployqt-continuous-x86_64.AppImage appdir/usr/share/applications/io.github.apollo24k.qmedia.desktop -appimage -updateinformation="gh-releases-zsync|Apollo24K|qMedia|latest|qMedia-*x86_64.AppImage.zsync" -extra-plugins=styles/libqt5ct-style.so,platformthemes/libqt5ct.so

if [ -n "$1" ]; then
    mv *.AppImage qMedia-nightly-$1-x86_64.AppImage
else
    mv *.AppImage qMedia-$VERSION-x86_64.AppImage
fi
rm -r appdir
