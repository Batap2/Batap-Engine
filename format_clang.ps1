param(
  [Parameter(Mandatory = $true)]
  [string]$Path
)

if (-not (Test-Path -LiteralPath $Path)) {
  Write-Error "File not found: $Path"
  exit 2
}

$reFailed  = '^\s*FAILED:\s+(.*)$'
$reInclude = '^\s*In file included from\s+(.*)$'
$reError   = '(?i):\s*error:\s'

# Unicode box-drawing via codepoints (encoding-proof)
$BRANCH = [char]0x251C   # ├
$LAST   = [char]0x2514   # └
$H      = [char]0x2500   # ─

$curFailed = $null
$includes  = New-Object System.Collections.Generic.List[string]
$errors    = New-Object System.Collections.Generic.List[string]

function Normalize-Include([string]$s) {
  $t = $s.Trim()
  if ($t.EndsWith(":")) { $t = $t.Substring(0, $t.Length - 1) }
  return $t
}

function Print-IncludeTree([System.Collections.Generic.List[string]]$inc) {
  if ($inc.Count -eq 0) { return }

  Write-Output "includes:"
  for ($i = 0; $i -lt $inc.Count; $i++) {
    $isLast = ($i -eq $inc.Count - 1)
    $prefix = if ($isLast) { "  $LAST$H " } else { "  $BRANCH$H " }
    Write-Output ($prefix + $inc[$i])
  }
}

function Write-FailedLine([string]$text) {
  # "FAILED: rest..."
  Write-Host "FAILED" -ForegroundColor Red -NoNewline
  Write-Host ($text.Substring(6))  # ": rest..."
}

function Write-ErrorLine([string]$text) {
  # "...: error: rest"
  $idx = $text.IndexOf("error", [System.StringComparison]::OrdinalIgnoreCase)
  if ($idx -lt 0) {
    Write-Output $text
    return
  }

  Write-Host $text.Substring(0, $idx) -NoNewline
  Write-Host "error" -ForegroundColor Red -NoNewline
  Write-Host $text.Substring($idx + 5)
}

function Flush-Block {
  if (-not $curFailed) { return }

  Write-FailedLine ("FAILED: " + $curFailed)

  if ($includes.Count -gt 0) {
    Print-IncludeTree $includes
  }

  foreach ($e in $errors) {
    Write-ErrorLine $e
  }

  Write-Output ""

  $curFailed = $null
  $includes.Clear() | Out-Null
  $errors.Clear() | Out-Null
}

Get-Content -LiteralPath $Path | ForEach-Object {
  $line = $_.TrimEnd()

  if ($line -match $reFailed) {
    Flush-Block
    $curFailed = $Matches[1].Trim()
    return
  }

  if ($line -match $reInclude) {
    if (-not $curFailed) { $curFailed = "(unknown target)" }
    $includes.Add((Normalize-Include $Matches[1])) | Out-Null
    return
  }

  if ($line -match $reError) {
    if (-not $curFailed) { $curFailed = "(unknown target)" }
    $errors.Add($line) | Out-Null
    return
  }
}

Flush-Block
exit 0
