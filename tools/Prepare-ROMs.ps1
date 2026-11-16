param([Parameter(Mandatory=$true)][string]$ArchivesFolder)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$catalog=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'rom_catalog.json') -Raw | ConvertFrom-Json
$output=Join-Path $PSScriptRoot '../roms'
[IO.Directory]::CreateDirectory($output) | Out-Null
$found=@{}
function Accept-Rom([byte[]]$bytes) {
    $sha=[Security.Cryptography.SHA256]::Create()
    try { $hash=([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','').ToLowerInvariant() }
    finally { $sha.Dispose() }
    $item=$catalog | Where-Object { $_.sha256 -eq $hash } | Select-Object -First 1
    if ($null -eq $item) {return}
    $target=Join-Path $output $item.target
    if ((Test-Path -LiteralPath $target) -and ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hash)) {
        throw "Refusing to overwrite a different ROM: $target"
    }
    [IO.File]::WriteAllBytes($target,$bytes)
    $found[$item.target]=$true
    Write-Host "Recognized: $($item.target)"
}
Get-ChildItem -LiteralPath $ArchivesFolder -File | ForEach-Object {
    if ($_.Extension -ieq '.zip') {
        $zip=[IO.Compression.ZipFile]::OpenRead($_.FullName)
        try {
            foreach ($entry in $zip.Entries) {
                if ($entry.Length -notin 512,8192) {continue}
                $stream=$entry.Open();$memory=New-Object IO.MemoryStream
                try {$stream.CopyTo($memory);Accept-Rom ($memory.ToArray())}
                finally {$stream.Dispose();$memory.Dispose()}
            }
        } finally {$zip.Dispose()}
    } elseif ($_.Length -in 512,8192) {Accept-Rom ([IO.File]::ReadAllBytes($_.FullName))}
}
$missing=@('main.bin','scanner.bin','ic135.bin','ic137.bin') | Where-Object {-not $found.ContainsKey($_)}
if ($missing) {throw "Missing recognized ROMs: $($missing -join ', ')"}
Write-Host "Ready. Launch EII-Bringup.exe and select roms/main.bin."
