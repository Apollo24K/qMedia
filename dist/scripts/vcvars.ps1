using namespace System.Runtime.InteropServices

if ([RuntimeInformation]::OSArchitecture -ne [Architecture]::X64) {
    throw 'Unsupported host architecture.'
}

$arch =
    $env:buildArch -eq 'X86' ? 'x64_x86' :
    $env:buildArch -eq 'Arm64' ? 'x64_arm64' :
    'x64'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path $vswhere) {
    $installationPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $path = Join-Path $installationPath 'VC\Auxiliary\Build'
} else {
    $path = Resolve-Path "${env:ProgramFiles}\Microsoft Visual Studio\*\*\VC\Auxiliary\Build" | Select-Object -First 1 -ExpandProperty Path
}

if (-not (Test-Path (Join-Path $path 'vcvarsall.bat'))) {
    throw 'Could not find a Visual Studio installation with the C++ build tools.'
}

cmd.exe /c "call `"$path\vcvarsall.bat`" $arch && set > %temp%\vcvars.txt"

$exclusions = @('VCPKG_ROOT') # Workaround for https://developercommunity.visualstudio.com/t/VCPKG_ROOT-is-being-overwritten-by-the-D/10430650
$seenVariables = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$pathValue = $null
Get-Content "$env:temp\vcvars.txt" | Foreach-Object {
    if ($_ -match "^(.*?)=(.*)$" -and $matches[1] -ieq 'PATH' -and -not $pathValue) {
        $pathValue = $matches[2]
    } elseif ($_ -match "^(.*?)=(.*)$" -and $matches[1] -notin $exclusions -and $seenVariables.Add($matches[1])) {
        [Environment]::SetEnvironmentVariable($matches[1], $matches[2])
    }
}

# Some parent processes can provide both PATH and Path. MSBuild treats those as duplicate keys.
[Environment]::SetEnvironmentVariable('PATH', $null)
[Environment]::SetEnvironmentVariable('Path', $null)
[Environment]::SetEnvironmentVariable('Path', $pathValue)
