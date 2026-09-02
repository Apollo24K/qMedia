#!/usr/bin/env pwsh

# This script will download binary plugins from the kimageformats-binaries repository using Github's API.

param
(
    [string]$DestinationRoot = "bin",
    [switch]$ApngOnly
)

$pluginNames = $ApngOnly ? @("qtapng") : @("qtapng", "kimageformats")

$qtVersion = [version](qmake -query QT_VERSION)
Write-Host "Detected Qt Version $qtVersion"

# Qt version availability and runner names are assumed.
if ($IsWindows) {
    $imageName = "windows-2022"
} elseif ($IsMacOS) {
    $imageName = $qtVersion -lt [version]'6.5.3' ? "macos-13" : "macos-14"
} else {
    $imageName = "ubuntu-20.04"
}

$binaryBaseUrl = "https://github.com/jurplel/kimageformats-binaries/releases/download/cont"
$temporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("qmedia-plugins-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $temporaryRoot | Out-Null

if ($pluginNames.count -eq 0) {
    Write-Host "the pluginNames array is empty."
}

try {
    foreach ($pluginName in $pluginNames) {
        $qtArch = $env:qtArch ? "-$env:qtArch" : ''
        $artifactName = "$pluginName-$imageName-$qtVersion$qtArch.zip"
        $artifactPath = Join-Path $temporaryRoot $artifactName
        $pluginPath = Join-Path $temporaryRoot $pluginName
        $downloadUrl = "$binaryBaseUrl/$artifactName"

        Write-Host "Downloading $downloadUrl"
        Invoke-WebRequest -URI $downloadUrl -OutFile $artifactPath
        Expand-Archive $artifactPath -DestinationPath $pluginPath
    }

    if ($IsWindows) {
        $out_frm = $DestinationRoot
        $out_imf = Join-Path $DestinationRoot "imageformats"
    } elseif ($IsMacOS) {
        $out_frm = Join-Path $DestinationRoot "qMedia.app/Contents/Frameworks"
        $out_imf = Join-Path $DestinationRoot "qMedia.app/Contents/PlugIns/imageformats"
    } else {
        $out_frm = Join-Path $DestinationRoot "appdir/usr/lib"
        $out_imf = Join-Path $DestinationRoot "appdir/usr/plugins/imageformats"
    }

    New-Item -Type Directory -Path $out_frm -Force | Out-Null
    New-Item -Type Directory -Path $out_imf -Force | Out-Null

    function MoveLibraries($category, $destDir, $files) {
        foreach ($file in $files) {
            Write-Host "${category}: $($file.Name)"
            Move-Item -Path $file.FullName -Destination $destDir -Force
        }
    }

    # Deploy QtApng
    if ($pluginNames -contains 'qtapng') {
        Write-Host "`nDeploying QtApng:"
        MoveLibraries 'imf' $out_imf (Get-ChildItem (Join-Path $temporaryRoot "qtapng/QtApng/output"))
    }

    # Deploy KImageFormats
    if ($pluginNames -contains 'kimageformats') {
        Write-Host "`nDeploying KImageFormats:"
        $kimageformatsOutput = Join-Path $temporaryRoot "kimageformats/kimageformats/output"
        MoveLibraries 'imf' $out_imf (Get-ChildItem $kimageformatsOutput -Filter "kimg_*")
        MoveLibraries 'frm' $out_frm (Get-ChildItem $kimageformatsOutput)

        if ($IsWindows) {
            $qtTgaPlugin = Join-Path $out_imf "qtga.dll"
            if (Test-Path $qtTgaPlugin) {
                Write-Host "Removing duplicate Qt TGA plugin"
                Remove-Item -LiteralPath $qtTgaPlugin -Force
            }
        }
    }
} finally {
    Remove-Item -LiteralPath $temporaryRoot -Recurse -Force
}

Write-Host ''
