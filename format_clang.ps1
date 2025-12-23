param(
  [Parameter(Mandatory=$true)]
  [string]$Path
)

# Read as UTF-8 (common with clang), fall back if needed
try {
  $lines = Get-Content -LiteralPath $Path -Encoding UTF8
} catch {
  $lines = Get-Content -LiteralPath $Path
}

# --- Filters ---
# Remove the huge compile command line(s)
$dropPatterns = @(
  '^\s*C:/ProgramData/chocolatey/bin/ccache\.exe\b',
  '^\s*.*\\clang-cl\.exe\b'
)

function Should-DropLine([string]$s) {
  foreach ($p in $dropPatterns) {
    if ($s -match $p) { return $true }
  }
  return $false
}

# --- Simple color helpers ---
$esc = [char]27
$RESET  = "$esc[0m"
$BOLD   = "$esc[1m"
$DIM    = "$esc[2m"
$RED    = "$esc[31m"
$GREEN  = "$esc[32m"
$YELLOW = "$esc[33m"
$CYAN   = "$esc[36m"
$GRAY   = "$esc[90m"

function Write-ColoredLine([string]$line) {
  if ($line -match '^\s*FAILED:') {
    Write-Host ($RED + $BOLD + $line + $RESET)
    return
  }
  if ($line -match '\berror:\b' -or $line -match '^\s*.*\(\d+,\d+\):\s*error:') {
    Write-Host ($RED + $BOLD + $line + $RESET)
    return
  }
  if ($line -match '\bfatal error:\b') {
    Write-Host ($RED + $BOLD + $line + $RESET)
    return
  }
  if ($line -match '\bwarning:\b') {
    Write-Host ($YELLOW + $line + $RESET)
    return
  }
  if ($line -match '\bnote:\b') {
    Write-Host ($CYAN + $line + $RESET)
    return
  }
  if ($line -match '^\s*\[(INFO|BUILD|SUCCESS)\]') {
    Write-Host ($GREEN + $line + $RESET)
    return
  }

  # De-emphasize noisy include traces, if any
  if ($line -match '^\s*In file included from ') {
    Write-Host ($DIM + $line + $RESET)
    return
  }

  # Default: print as-is
  Write-Host $line
}

# --- Print, filtering the noisy command lines ---
foreach ($line in $lines) {
  if ([string]::IsNullOrWhiteSpace($line)) {
    Write-Host ""
    continue
  }

  if (Should-DropLine $line) {
    continue
  }

  Write-ColoredLine $line
}
