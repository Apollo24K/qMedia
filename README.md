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
brightness, contrast, and saturation filters.

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
