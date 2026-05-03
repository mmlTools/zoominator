[CmdletBinding()]
param(
    [ValidateSet('x64')]
    [string] $Target = 'x64',
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'RelWithDebInfo'
)

$ErrorActionPreference = 'Stop'

if ( $DebugPreference -eq 'Continue' ) {
    $VerbosePreference = 'Continue'
    $InformationPreference = 'Continue'
}

if ( $env:CI -eq $null ) {
    throw "Package-Windows.ps1 requires CI environment"
}

if ( ! ( [System.Environment]::Is64BitOperatingSystem ) ) {
    throw "Packaging script requires a 64-bit system to build and run."
}

if ( $PSVersionTable.PSVersion -lt '7.2.0' ) {
    Write-Warning 'The packaging script requires PowerShell Core 7. Install or upgrade your PowerShell version: https://aka.ms/pscore6'
    exit 2
}

function Copy-DirectoryContents {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Source,
        [Parameter(Mandatory = $true)]
        [string] $Destination
    )

    if ( ! ( Test-Path -Path $Source -PathType Container ) ) {
        return
    }

    New-Item -ItemType Directory -Force -Path $Destination | Out-Null

    Get-ChildItem -Path $Source -Force | ForEach-Object {
        Copy-Item -Path $_.FullName -Destination $Destination -Recurse -Force
    }
}

function Package {
    trap {
        Write-Error $_
        exit 2
    }

    $ProjectRoot = Resolve-Path -Path "$PSScriptRoot/../.."
    $BuildSpecFile = "${ProjectRoot}/buildspec.json"

    $UtilityFunctions = Get-ChildItem -Path $PSScriptRoot/utils.pwsh/*.ps1 -Recurse

    foreach( $Utility in $UtilityFunctions ) {
        Write-Debug "Loading $($Utility.FullName)"
        . $Utility.FullName
    }

    $BuildSpec = Get-Content -Path ${BuildSpecFile} -Raw | ConvertFrom-Json
    $ProductName = $BuildSpec.name
    $ProductVersion = $BuildSpec.version
    $OutputName = "${ProductName}-${ProductVersion}-windows-${Target}"
    $InstallRoot = Resolve-Path -Path "${ProjectRoot}/release/${Configuration}"
    $PackageRoot = "${ProjectRoot}/release/windows-package/${OutputName}"
    $PluginRoot = "${PackageRoot}/obs-plugins/64bit"
    $DataRoot = "${PackageRoot}/data/obs-plugins/${ProductName}"

    Remove-Item -ErrorAction SilentlyContinue -Recurse -Force -Path @(
        "${ProjectRoot}/release/${ProductName}-*-windows-*.zip",
        "${ProjectRoot}/release/windows-package"
    )

    New-Item -ItemType Directory -Force -Path $PluginRoot | Out-Null
    New-Item -ItemType Directory -Force -Path $DataRoot | Out-Null

    $ExistingPluginRoot = Join-Path -Path $InstallRoot -ChildPath 'obs-plugins/64bit'
    if ( Test-Path -Path $ExistingPluginRoot -PathType Container ) {
        Get-ChildItem -Path $ExistingPluginRoot -Filter '*.dll' -File | ForEach-Object {
            Copy-Item -Path $_.FullName -Destination $PluginRoot -Force
        }
    }

    $DirectDllCandidates = Get-ChildItem -Path $InstallRoot -Filter '*.dll' -File -Recurse | Where-Object {
        $_.FullName -notlike "*\data\obs-plugins\*" -and
        $_.FullName -notlike "*\windows-package\*" -and
        $_.DirectoryName -ne $PluginRoot
    }

    foreach ( $Dll in $DirectDllCandidates ) {
        Copy-Item -Path $Dll.FullName -Destination $PluginRoot -Force
    }

    if ( ! ( Get-ChildItem -Path $PluginRoot -Filter '*.dll' -File -ErrorAction SilentlyContinue ) ) {
        throw "No Windows plugin DLLs were found under ${InstallRoot}."
    }

    $ExpectedDataRoot = Join-Path -Path $InstallRoot -ChildPath "data/obs-plugins/${ProductName}"
    if ( Test-Path -Path $ExpectedDataRoot -PathType Container ) {
        Copy-DirectoryContents -Source $ExpectedDataRoot -Destination $DataRoot
    } else {
        $AnyDataRoot = Join-Path -Path $InstallRoot -ChildPath 'data/obs-plugins'
        if ( Test-Path -Path $AnyDataRoot -PathType Container ) {
            $DataCandidates = @(Get-ChildItem -Path $AnyDataRoot -Directory)
            if ( $DataCandidates.Count -eq 1 ) {
                Copy-DirectoryContents -Source $DataCandidates[0].FullName -Destination $DataRoot
            } elseif ( $DataCandidates.Count -gt 1 ) {
                foreach ( $DataCandidate in $DataCandidates ) {
                    Copy-DirectoryContents -Source $DataCandidate.FullName -Destination "${PackageRoot}/data/obs-plugins/$($DataCandidate.Name)"
                }
            }
        }
    }

    if ( ! ( Get-ChildItem -Path $DataRoot -Force -ErrorAction SilentlyContinue ) ) {
        Remove-Item -Path $DataRoot -Force -Recurse -ErrorAction SilentlyContinue
    }

    Log-Group "Archiving ${ProductName}..."
    $ArchivePaths = @(
        "${PackageRoot}/obs-plugins",
        "${PackageRoot}/data"
    ) | Where-Object { Test-Path -Path $_ }

    $CompressArgs = @{
        Path = $ArchivePaths
        CompressionLevel = 'Optimal'
        DestinationPath = "${ProjectRoot}/release/${OutputName}.zip"
        Verbose = ($Env:CI -ne $null)
    }
    Compress-Archive -Force @CompressArgs
    Log-Group
}

Package
