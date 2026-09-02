#!/usr/bin/bash

if [[ -n "$1" ]]; then
    VERSION=$1
else
    VERSION=$(LC_ALL=C sed -nE 's/^project\(qMedia VERSION ([0-9.]+).*/\1/p' CMakeLists.txt)
fi

cd bin

echo "Running macdeployqt"
macdeployqt qMedia.app

IMF_DIR=qMedia.app/Contents/PlugIns/imageformats
if [[ (-f "$IMF_DIR/kimg_heif.dylib" || -f "$IMF_DIR/kimg_heif.so") && -f "$IMF_DIR/libqmacheif.dylib" ]]; then
    # Prefer kimageformats HEIF plugin for proper color space handling
    echo "Removing duplicate HEIF plugin"
    rm "$IMF_DIR/libqmacheif.dylib"
fi
if [[ (-f "$IMF_DIR/kimg_tga.dylib" || -f "$IMF_DIR/kimg_tga.so") && -f "$IMF_DIR/libqtga.dylib" ]]; then
    # Prefer kimageformats TGA plugin which supports more formats
    echo "Removing duplicate TGA plugin"
    rm "$IMF_DIR/libqtga.dylib"
fi

echo "Running codesign"
if [[ "$APPLE_NOTARIZE_REQUESTED" == "true" ]]; then
    APP_IDENTIFIER=$(/usr/libexec/PlistBuddy -c "Print CFBundleIdentifier" "qMedia.app/Contents/Info.plist")
    codesign --sign "$CODESIGN_CERT_NAME" --deep --force --options runtime --timestamp "qMedia.app"
else
    codesign --sign "$CODESIGN_CERT_NAME" --deep --force "qMedia.app"
fi

echo "Creating disk image"
if [[ -n "$1" ]]; then
    BUILD_NAME=qMedia-nightly-$1
    DMG_FILENAME=$BUILD_NAME.dmg
    mv qMedia.app "$BUILD_NAME.app"
    hdiutil create -volname "$BUILD_NAME" -srcfolder "$BUILD_NAME.app" -fs HFS+ "$DMG_FILENAME"
else
    DMG_FILENAME=qMedia-$VERSION.dmg
    brew install create-dmg
    create-dmg --volname "qMedia $VERSION" --window-size 660 400 --icon-size 160 --icon "qMedia.app" 180 170 --hide-extension qMedia.app --app-drop-link 480 170 "$DMG_FILENAME" "qMedia.app"
fi
if [[ "$APPLE_NOTARIZE_REQUESTED" == "true" ]]; then
    codesign --sign "$CODESIGN_CERT_NAME" --timestamp --identifier "$APP_IDENTIFIER.dmg" "$DMG_FILENAME"
    xcrun notarytool submit "$DMG_FILENAME" --apple-id "$APPLE_ID_USER" --password "$APPLE_ID_PASS" --team-id "${CODESIGN_CERT_NAME: -11:10}" --wait
    xcrun stapler staple "$DMG_FILENAME"
    xcrun stapler validate "$DMG_FILENAME"
fi

rm -r *.app
