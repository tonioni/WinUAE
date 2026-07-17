param(
    [string]$Destination = 'D:\Amiga',
    [string]$ArchivePath = '',
    [string]$Url = 'https://download.abime.net/winuae/stuff/drive_sounds.zip',
    [string]$Sha256 = '2efbe66d090c50f5b58e1248a1cab5c1886c7d203bd2f1d26d1ec382b3ffb088'
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$downloadedArchive = $false
$extractDir = Join-Path ([System.IO.Path]::GetTempPath()) `
    ("winuae-drive-sounds-extract-{0}" -f [System.Guid]::NewGuid())

try {
    if ([string]::IsNullOrWhiteSpace($ArchivePath)) {
        $ArchivePath = Join-Path ([System.IO.Path]::GetTempPath()) `
            ("winuae-drive-sounds-{0}.zip" -f [System.Guid]::NewGuid())
        $downloadedArchive = $true
        Invoke-WebRequest -Uri $Url -OutFile $ArchivePath
    } else {
        $ArchivePath = (Resolve-Path -LiteralPath $ArchivePath).Path
    }

    $actualSha256 = (Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualSha256 -ne $Sha256.ToLowerInvariant()) {
        throw "Drive sound archive checksum mismatch: expected $Sha256, got $actualSha256"
    }

    New-Item -ItemType Directory -Force -Path $extractDir | Out-Null
    Expand-Archive -LiteralPath $ArchivePath -DestinationPath $extractDir

    $extractedSampleDir = Join-Path $extractDir 'plugins\floppysounds'
    if (-not (Test-Path -LiteralPath $extractedSampleDir -PathType Container)) {
        throw "Drive sound archive did not provide plugins\floppysounds"
    }
    $clickSamples = @(Get-ChildItem -LiteralPath $extractedSampleDir -File -Filter 'drive_click_*.wav')
    if ($clickSamples.Count -eq 0) {
        throw "Drive sound archive did not provide any drive_click_*.wav sample sets"
    }
    foreach ($sample in @(Get-ChildItem -LiteralPath $extractedSampleDir -File -Filter '*.wav')) {
        if ($sample.Length -eq 0) {
            throw "Drive sound archive provided an empty sample: $($sample.FullName)"
        }
    }

    $pluginDir = Join-Path $Destination 'plugins'
    $sampleDir = Join-Path $pluginDir 'floppysounds'
    New-Item -ItemType Directory -Force -Path $pluginDir | Out-Null
    if (Test-Path -LiteralPath $sampleDir) {
        Remove-Item -LiteralPath $sampleDir -Recurse -Force
    }
    Move-Item -LiteralPath $extractedSampleDir -Destination $sampleDir
} finally {
    Remove-Item -LiteralPath $extractDir -Recurse -Force -ErrorAction SilentlyContinue
    if ($downloadedArchive) {
        Remove-Item -LiteralPath $ArchivePath -Force -ErrorAction SilentlyContinue
    }
}
