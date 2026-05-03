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

    if ( ! ( Test-Path -LiteralPath $Source ) ) {
        return
    }

    New-Item -ItemType Directory -Force -Path $Destination | Out-Null

    Get-ChildItem -LiteralPath $Source -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $Destination -Recurse -Force
    }
}

function Package {
    trap {
        Write-Error $_
        exit 2
    }

    $ScriptHome = $PSScriptRoot
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
    $StageRoot = "${ProjectRoot}/release/${OutputName}"
    $PluginBinaryRoot = "${StageRoot}/obs-plugins/64bit"
    $PluginDataRoot = "${StageRoot}/data/obs-plugins/${ProductName}"

    $RemoveArgs = @{
        ErrorAction = 'SilentlyContinue'
        Path = @(
            "${ProjectRoot}/release/${ProductName}-*-windows-*.zip",
            $StageRoot
        )
    }

    Remove-Item @RemoveArgs -Recurse -Force

    New-Item -ItemType Directory -Force -Path $PluginBinaryRoot | Out-Null
    New-Item -ItemType Directory -Force -Path $PluginDataRoot | Out-Null

    $DllCandidates = @(
        "${InstallRoot}/obs-plugins/64bit/${ProductName}.dll",
        "${InstallRoot}/obs-plugins/64bit/${ProductName}-plugin.dll",
        "${InstallRoot}/bin/64bit/${ProductName}.dll",
        "${InstallRoot}/bin/64bit/${ProductName}-plugin.dll",
        "${InstallRoot}/${ProductName}.dll",
        "${InstallRoot}/${ProductName}-plugin.dll"
    )

    $CopiedDll = $false

    foreach ( $Candidate in $DllCandidates ) {
        if ( Test-Path -LiteralPath $Candidate ) {
            Copy-Item -LiteralPath $Candidate -Destination $PluginBinaryRoot -Force
            $CopiedDll = $true
        }
    }

    if ( ! $CopiedDll ) {
        $FoundDlls = Get-ChildItem -LiteralPath $InstallRoot -Recurse -File -Filter '*.dll' |
            Where-Object {
                $_.Name -eq "${ProductName}.dll" -or
                $_.Name -eq "${ProductName}-plugin.dll" -or
                $_.DirectoryName -match 'obs-plugins|bin\\64bit|bin/64bit'
            }

        foreach ( $Dll in $FoundDlls ) {
            Copy-Item -LiteralPath $Dll.FullName -Destination $PluginBinaryRoot -Force
            $CopiedDll = $true
        }
    }

    if ( ! $CopiedDll ) {
        throw "Could not find the plugin DLL under ${InstallRoot}."
    }

    $CopiedData = $false

    $ExactDataCandidates = @(
        "${InstallRoot}/data/obs-plugins/${ProductName}",
        "${InstallRoot}/data/${ProductName}"
    )

    foreach ( $Candidate in $ExactDataCandidates ) {
        if ( Test-Path -LiteralPath $Candidate ) {
            Copy-DirectoryContents -Source $Candidate -Destination $PluginDataRoot
            $CopiedData = $true
            break
        }
    }

    if ( ! $CopiedData ) {
        $ObsPluginsDataRoot = "${InstallRoot}/data/obs-plugins"
        if ( Test-Path -LiteralPath $ObsPluginsDataRoot ) {
            $DataFolders = Get-ChildItem -LiteralPath $ObsPluginsDataRoot -Directory -Force
            if ( $DataFolders.Count -eq 1 ) {
                Copy-DirectoryContents -Source $DataFolders[0].FullName -Destination $PluginDataRoot
                $CopiedData = $true
            } elseif ( Test-Path -LiteralPath "${ObsPluginsDataRoot}/${ProductName}" ) {
                Copy-DirectoryContents -Source "${ObsPluginsDataRoot}/${ProductName}" -Destination $PluginDataRoot
                $CopiedData = $true
            }
        }
    }

    if ( ! $CopiedData -and ( Test-Path -LiteralPath "${InstallRoot}/data" ) ) {
        $TopLevelDataItems = Get-ChildItem -LiteralPath "${InstallRoot}/data" -Force |
            Where-Object { $_.Name -ne 'obs-plugins' }

        if ( $TopLevelDataItems.Count -gt 0 ) {
            foreach ( $Item in $TopLevelDataItems ) {
                Copy-Item -LiteralPath $Item.FullName -Destination $PluginDataRoot -Recurse -Force
            }
            $CopiedData = $true
        }
    }

    if ( ! $CopiedData ) {
        $KnownDataFolders = @('locale', 'locales', 'locale.ini', 'locales.ini', 'images', 'sounds')
        foreach ( $Name in $KnownDataFolders ) {
            $Matches = Get-ChildItem -LiteralPath $InstallRoot -Recurse -Force -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -eq $Name }

            foreach ( $Match in $Matches ) {
                if ( $Match.PSIsContainer ) {
                    Copy-Item -LiteralPath $Match.FullName -Destination $PluginDataRoot -Recurse -Force
                } else {
                    Copy-Item -LiteralPath $Match.FullName -Destination $PluginDataRoot -Force
                }
                $CopiedData = $true
            }
        }
    }

    Log-Group "Archiving ${ProductName}..."
    $CompressArgs = @{
        Path = (Get-ChildItem -LiteralPath $StageRoot)
        CompressionLevel = 'Optimal'
        DestinationPath = "${ProjectRoot}/release/${OutputName}.zip"
        Verbose = ($Env:CI -ne $null)
    }
    Compress-Archive -Force @CompressArgs
    Log-Group
}

Package
