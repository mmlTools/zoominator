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

if ( -not [System.Environment]::Is64BitOperatingSystem ) {
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

    $UtilityFunctions = Get-ChildItem -Path "$ScriptHome/utils.pwsh/*.ps1" -Recurse
    foreach ($Utility in $UtilityFunctions) {
        Write-Debug "Loading $($Utility.FullName)"
        . $Utility.FullName
    }

    $BuildSpecFile = Join-Path $ProjectRoot "buildspec.json"
    if (-not (Test-Path $BuildSpecFile)) {
        throw "Buildspec not found at ${BuildSpecFile}"
    }

    $BuildSpec = Get-Content -Path $BuildSpecFile -Raw | ConvertFrom-Json
    $ProductName = $BuildSpec.name
    $ProductVersion = $BuildSpec.version

    if (-not $ProductName -or -not $ProductVersion) {
        throw "buildspec.json must contain 'name' and 'version'."
    }

    $OutputName = "${ProductName}-${ProductVersion}-windows-${Target}"

    # Clean old zips
    $RemoveArgs = @{
        ErrorAction = 'SilentlyContinue'
        Path        = @(
            "${ProjectRoot}/release/${ProductName}-*-windows-*.zip"
        )
    }
    Remove-Item @RemoveArgs

    $ReleaseConfigDir = Join-Path $ProjectRoot "release/${Configuration}"
    if (-not (Test-Path $ReleaseConfigDir)) {
        throw "Release directory not found: ${ReleaseConfigDir}. Did you run Build-Windows.ps1 first?"
    }

    Log-Group "Archiving ${ProductName} (ZIP)..."

    $ZipPath = "${ProjectRoot}/release/${OutputName}.zip"

    $CompressArgs = @{
        Path             = (Get-ChildItem -Path $ReleaseConfigDir)
        CompressionLevel = 'Optimal'
        DestinationPath  = $ZipPath
        Verbose          = ($Env:CI -ne $null)
    }

    Compress-Archive -Force @CompressArgs

    Log-Group

    Write-Host "Packaging complete:"
    Write-Host "  - ZIP: $ZipPath"
}

Package
