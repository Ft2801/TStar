# set_version.ps1
param(
    [Parameter(Mandatory=$true)]
    [string]$NewVersion,

    [Parameter(Mandatory=$false)]
    [string]$ChangelogMessage
)

$ChangelogFile = Join-Path $PSScriptRoot "changelog.txt"

if (!(Test-Path $ChangelogFile)) {
    Write-Host "[ERROR] $ChangelogFile not found!"
    exit 1
}

$Content = Get-Content $ChangelogFile -Raw
$Date = Get-Date -Format "yyyy-MM-dd"

if ($ChangelogMessage) {
    # 1. PREPEND a new version entry if a message is provided
    Write-Host "Prepending new entry for version $NewVersion..."
    
    $NewEntry = @"

Version $NewVersion
-------------
($Date)
- $ChangelogMessage
"@
    
    # Prepend after the title header (assuming "===============")
    $SplitIndex = $Content.IndexOf("===============") + 15
    if ($SplitIndex -lt 15) { $SplitIndex = 0 }
    
    $Pre = $Content.Substring(0, $SplitIndex)
    $Post = $Content.Substring($SplitIndex)
    
    $NewContent = $Pre + "`n" + $NewEntry + "`n" + $Post
    Set-Content -Path $ChangelogFile -Value $NewContent
} else {
    # 2. UPDATE the latest version number if no message is provided
    Write-Host "Updating latest version to $NewVersion..."
    # Replace the FIRST occurrence of "Version x.x.x"
    $NewContent = $Content -replace "(?m)^Version [0-9.]+", "Version $NewVersion"
    Set-Content -Path $ChangelogFile -Value $NewContent
}

Write-Host "Done! TStar version is now $NewVersion (Source: changelog.txt)"
