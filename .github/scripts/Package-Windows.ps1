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
    $InstallRoot = Resolve-Path -Path "${ProjectRoot}/release/${Configuration}/${ProductName}"
    $StageRoot = "${ProjectRoot}/release/${OutputName}"
    $PluginStageRoot = "${StageRoot}/${ProductName}"

    $RemoveArgs = @{
        ErrorAction = 'SilentlyContinue'
        Path = @(
            "${ProjectRoot}/release/${ProductName}-*-windows-*.zip",
            $StageRoot
        )
    }

    Remove-Item @RemoveArgs -Recurse -Force

    # OBS 33 plugin layout. The archive holds a single <plugin> folder that is
    # extracted into %ProgramData%\obs-studio\plugins:
    #   <plugin>\<plugin>.dll
    #   <plugin>\data\locale\*.ini
    $PluginDll = "${InstallRoot}/${ProductName}.dll"
    $PluginData = "${InstallRoot}/data"

    foreach ( $Required in @($PluginDll, "${PluginData}/locale/en-US.ini") ) {
        if ( ! ( Test-Path -LiteralPath $Required ) ) {
            throw "Missing required plugin file: ${Required}"
        }
    }

    New-Item -ItemType Directory -Force -Path $PluginStageRoot | Out-Null
    Copy-Item -LiteralPath $PluginDll -Destination $PluginStageRoot -Force
    Copy-Item -LiteralPath $PluginData -Destination $PluginStageRoot -Recurse -Force

    Log-Group "Archiving ${ProductName}..."
    $CompressArgs = @{
        Path = $PluginStageRoot
        CompressionLevel = 'Optimal'
        DestinationPath = "${ProjectRoot}/release/${OutputName}.zip"
        Verbose = ($Env:CI -ne $null)
    }
    Compress-Archive -Force @CompressArgs
    Log-Group
}

Package
