$Exclude = @(
    "node_modules",".git","dist","bin","include","site","docs",".vscode",".cache","build"
)

$RootPath = $PSScriptRoot

$BR_TEE  = "|-- "
$BR_LAST = "\-- "
$BR_PIPE = "|   "
$BR_SP   = "    "

function Show-Tree {
    param(
        [string]$CurrentPath,
        [string]$Indent = ""
    )

    $items = Get-ChildItem -LiteralPath $CurrentPath -Force |
        Where-Object {
            $name = $_.Name
            -not ($Exclude | Where-Object { $name -like $_ })
        } |
        Sort-Object @{Expression = {$_.PSIsContainer}; Descending = $true}, Name

    $count = $items.Count
    $index = 0

    foreach ($item in $items) {
        $index++
        $isLast = ($index -eq $count)

        if ($isLast) {
            Write-Host ($Indent + $BR_LAST + $item.Name)
            $newIndent = $Indent + $BR_SP
        }
        else {
            Write-Host ($Indent + $BR_TEE + $item.Name)
            $newIndent = $Indent + $BR_PIPE
        }

        if ($item.PSIsContainer) {
            Show-Tree -CurrentPath $item.FullName -Indent $newIndent
        }
    }
}

Write-Host $RootPath
Show-Tree -CurrentPath $RootPath