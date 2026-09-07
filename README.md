<h1 align=center>qMedia</h1>

<p align=center>qMedia is a minimal image and video viewer built from qView.</p>

<h3 align=center>
    <a href="https://github.com/Apollo24K/qMedia">View on GitHub</a>
</h3>

<h4 align=center>
    <a href="https://github.com/Apollo24K/qMedia/releases">Downloads</a> |
    <a href="https://github.com/jurplel/qView">Original qView project</a>
</h4>

<p align=center>
    <a href="https://github.com/Apollo24K/qMedia/releases">
        <img alt="Downloads shield" src="https://img.shields.io/github/downloads/Apollo24K/qMedia/total?color=blue&style=flat-square">
    </a>
</p>

qMedia retains qView's lightweight image-viewing workflow while adding mixed-media
navigation, video playback, synchronized audio, shared canvas controls, and live
layered color, hue, transparency, and gradient filters. Press **H** for the floating
Layers HUD: reorder, rename, hide, duplicate, and blend source and filter layers
without changing canvas pan or zoom.

## Home and folders

An empty window opens on Home, with one **Open** button and recent items. The Open
picker accepts files and folders; double-click a folder inside the picker to browse it.
Open, paste, or drop a folder to browse a simple gallery of its subfolders, images,
and videos. Tiles resize and change column count to fill the available width.
Click to select, double-click or press **Enter** to open. Click empty space to clear
the selection, drag a selection rectangle, or use **Ctrl/Shift** to select multiple
items. **Delete** moves the selection to the trash, using the existing confirmation
preference. Selected folders include their contents; failed operations leave their
items in place. **Ctrl+Z** can restore trashed items one at a time where supported.

All four **arrow keys** navigate gallery selection. The mouse **Back** thumb button
opens the parent folder; **Forward** retraces the branch you just left, in both the
gallery and viewer. **Alt+Up/Alt+Down** are keyboard alternatives. Opening a different
branch clears that return history. The default
rotation keys are **R** (left) and **T** (right); existing custom bindings are preserved.
**Ctrl+H** (or **Alt+Home**) opens Home from either mode. **Ctrl+R** refreshes the folder.
The gallery contains only items, with wheel scrolling and a small selection-count
overlay at the bottom. A scrollable gap gives the first row breathing room. Refreshing
or deleting items preserves the scroll position, clamping at the end when needed.
Returning from media preserves the current window size.
Double-click empty space to toggle fullscreen in Home, the gallery, or the viewer.
Escape clears gallery selections first, otherwise exits fullscreen or dismisses an active tool without navigating away from
the current media. Home and the gallery follow the viewer's background color settings,
including the alternate-background quick action.

Folder scans and image thumbnails run in the background. Only visible tiles
request previews, with one thumbnail decode at a time and a 24 MiB memory cache.
Videos use play tiles without starting playback for previews. Browsing is one
folder at a time, with no recursive indexing or persistent thumbnail database.
Use **Ctrl+R** to pick up changes made outside qMedia. Very large or unreadable
images use a placeholder; opening them still uses the regular viewer.

## Clipboard and URLs

Paste accepts copied image/video files, local file paths, clipboard images,
encoded image/video data, direct HTTP(S) media links, and media data URLs.
Copied HTML can supply an original image or video source. Original encoded bytes
are preserved so supported animations and videos play normally; bitmap-only
copies remain still images. If an HTML media download fails, an available
clipboard bitmap is used instead.

Open URL also supports direct image and video links. Downloads are saved to
temporary files and removed when qMedia exits normally. Links to web pages,
browser-private `blob:` URLs, and media requiring browser authentication are not
supported as direct media links.

## Development

See [DEVELOPING.md](DEVELOPING.md) for the local Windows toolchain, build, test,
and deployment setup.
