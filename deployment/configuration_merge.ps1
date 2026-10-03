# Semantic three-way merge. Conflicting edits to the same entity require review.
function New-NovaMergeMap { return ,([Collections.Generic.Dictionary[string,object]]::new([StringComparer]::Ordinal)) }
function Get-NovaCanonicalValue {
    param($Value)
    if ($null -eq $Value) { return 'null' }
    if ($Value -is [Collections.IDictionary]) {
        $parts = @(); $keys = [string[]]@($Value.Keys); [Array]::Sort($keys,[StringComparer]::Ordinal)
        foreach ($key in $keys) { $parts += (($key | ConvertTo-Json -Compress) + ':' + (Get-NovaCanonicalValue $Value[$key])) }
        return '{' + ($parts -join ',') + '}'
    }
    if ($Value -is [pscustomobject]) {
        $map = New-NovaMergeMap; foreach ($property in $Value.PSObject.Properties) { $map.Add($property.Name,$property.Value) }
        return Get-NovaCanonicalValue $map
    }
    if ($Value -is [Collections.IEnumerable] -and $Value -isnot [string]) {
        return '[' + (@(foreach ($item in $Value) { Get-NovaCanonicalValue $item }) -join ',') + ']'
    }
    return ConvertTo-Json -InputObject $Value -Compress
}
function Merge-NovaMap {
    param($Base,$Local,$Remote,[string]$Label,$Conflicts)
    $result = New-NovaMergeMap; $keys = New-NovaMergeMap
    foreach ($map in @($Base,$Local,$Remote)) { foreach ($key in $map.Keys) { $keys[$key] = $true } }
    foreach ($key in $keys.Keys) {
        $states = @(foreach ($map in @($Base,$Local,$Remote)) { if ($map.ContainsKey($key)) { '1:' + (Get-NovaCanonicalValue $map[$key]) } else { '0:' } })
        $chosen = $null
        if ($states[1] -eq $states[2]) { $chosen = $Local }
        elseif ($states[1] -eq $states[0]) { $chosen = $Remote }
        elseif ($states[2] -eq $states[0]) { $chosen = $Local }
        else { $Conflicts.Add("$Label/$key"); continue }
        if ($chosen.ContainsKey($key)) { $result.Add($key,$chosen[$key]) }
    }
    return ,$result
}
function Get-NovaSettingsMap {
    param($Data)
    $map = New-NovaMergeMap
    foreach ($row in $Data.settings) {
        if ($row.scope -eq 'batch_tasks') { continue }
        $key = ConvertTo-Json -InputObject @($row.scope,$row.section,$row.key) -Compress
        if ($map.ContainsKey($key)) { throw 'Duplicate settings key / 设置键重复。' }
        $map.Add($key,$row)
    }
    return ,$map
}
function Get-NovaTasksMap {
    param($Data)
    $map = New-NovaMergeMap
    $rows = @($Data.settings | Where-Object { $_.scope -eq 'batch_tasks' })
    if (-not $rows.Count) { return ,$map }
    $collection = @($rows | Where-Object { $_.section -eq 'Collection' })
    $version = @($collection | Where-Object { $_.key -eq 'Version' })
    $counter = @($collection | Where-Object { $_.key -eq 'Count' }); $count = 0
    if ($version.Count -ne 1 -or $version[0].value -ne '3' -or $counter.Count -ne 1 -or
        -not [int]::TryParse([string]$counter[0].value,[ref]$count) -or $count -lt 0 -or $count -gt 16) {
        throw 'Unsupported task collection / 任务集合版本或数量无效，请先完成数据迁移。'
    }
    for ($index = 0; $index -lt $count; $index++) {
        $task = New-NovaMergeMap
        foreach ($row in @($rows | Where-Object { $_.section -eq "Task$index" })) {
            if ($task.ContainsKey($row.key)) { throw 'Duplicate task field / 任务字段重复。' }
            $task.Add($row.key,$row.value)
        }
        foreach ($required in @('Id','Name','Command','Mode','DirectoryCount')) {
            if (-not $task.ContainsKey($required)) { throw 'Missing task field / 任务字段缺失。' }
        }
        $id = [string]$task['Id']
        if ($id -notmatch '^[1-9][0-9]{0,18}$' -or ($id.Length -eq 19 -and [string]::CompareOrdinal($id,'9223372036854775807') -gt 0) -or $map.ContainsKey($id)) {
            throw 'Invalid or duplicate task ID / 任务 ID 无效或重复。'
        }
        $directories = 0
        if (-not [int]::TryParse([string]$task['DirectoryCount'],[ref]$directories) -or $directories -lt 0 -or $directories -gt 24 -or
            $task['Mode'] -notin @('0','1') -or [string]::IsNullOrWhiteSpace($task['Name']) -or [string]::IsNullOrWhiteSpace($task['Command'])) { throw 'Invalid task / 任务配置无效。' }
        for ($step = 0; $step -lt $directories; $step++) {
            if (-not $task.ContainsKey("Directory$step") -or -not $task.ContainsKey("StepCommand$step")) { throw 'Missing subtask / 子任务数据缺失。' }
        }
        $map.Add($id,$task)
    }
    return ,$map
}
function Get-NovaEntityMap {
    param($Rows)
    $map = New-NovaMergeMap
    foreach ($row in $Rows) {
        $id = [string]$row.id
        if ($id -notmatch '^[1-9][0-9]{0,18}$' -or $map.ContainsKey($id)) { throw 'Invalid or duplicate entity ID / 数据 ID 无效或重复。' }
        $map.Add($id,$row)
    }
    return ,$map
}
function Get-NovaMergedConfiguration {
    param($Base,$Local,$Remote)
    foreach ($data in @($Base,$Local,$Remote)) {
        if ($data.version -ne 1 -or $data.application -ne 'NOVA Desktop') { throw 'Unsupported JSON version / JSON 版本不受支持。' }
    }
    $conflicts = [Collections.Generic.List[string]]::new()
    $settings = Merge-NovaMap (Get-NovaSettingsMap $Base) (Get-NovaSettingsMap $Local) (Get-NovaSettingsMap $Remote) 'settings' $conflicts
    $tasks = Merge-NovaMap (Get-NovaTasksMap $Base) (Get-NovaTasksMap $Local) (Get-NovaTasksMap $Remote) 'tasks' $conflicts
    $spaces = Merge-NovaMap (Get-NovaEntityMap $Base.workspaces) (Get-NovaEntityMap $Local.workspaces) (Get-NovaEntityMap $Remote.workspaces) 'workspaces' $conflicts
    $items = Merge-NovaMap (Get-NovaEntityMap $Base.items) (Get-NovaEntityMap $Local.items) (Get-NovaEntityMap $Remote.items) 'items' $conflicts
    if ($tasks.Count -gt 16 -or $spaces.Count -gt 8) { $conflicts.Add('capacity / 超过容量') }
    foreach ($space in $spaces.Values) {
        if (@($items.Values | Where-Object { $_.workspace_id -eq $space.id }).Count -gt 20) { $conflicts.Add("items/$($space.id)/capacity") }
    }
    foreach ($item in $items.Values) { if (-not $spaces.ContainsKey([string]$item.workspace_id)) { $conflicts.Add("items/$($item.id)/workspace") } }
    if ($conflicts.Count) { return [pscustomobject]@{ Data = $null; Conflicts = @($conflicts.ToArray()) } }
    $settingsRows = @($settings.Values)
    if (@($Base.settings + $Local.settings + $Remote.settings | Where-Object { $_.scope -eq 'batch_tasks' }).Count) {
        $settingsRows += [pscustomobject]@{ scope='batch_tasks'; section='Collection'; key='Version'; value='3' }
        $settingsRows += [pscustomobject]@{ scope='batch_tasks'; section='Collection'; key='Count'; value=[string]$tasks.Count }
        $order = @(); foreach ($data in @($Local,$Remote,$Base)) { foreach ($id in (Get-NovaTasksMap $data).Keys) { if ($tasks.ContainsKey($id) -and $id -notin $order) { $order += $id } } }
        for ($index = 0; $index -lt $order.Count; $index++) {
            foreach ($key in $tasks[$order[$index]].Keys) { $settingsRows += [pscustomobject]@{ scope='batch_tasks'; section="Task$index"; key=$key; value=$tasks[$order[$index]][$key] } }
        }
    }
    $spaceRows = @($spaces.Values | Sort-Object position,id); $position = 0
    foreach ($space in $spaceRows) { $space.position = $position; $position++ }
    $itemRows = @($items.Values | Sort-Object workspace_id,position,id); $positions = @{}
    foreach ($item in $itemRows) { $id = [string]$item.workspace_id; if (-not $positions.ContainsKey($id)) { $positions[$id]=0 }; $item.position=$positions[$id]; $positions[$id]++ }
    [pscustomobject]@{ Data = [ordered]@{version=1;application='NOVA Desktop';initialized=($Local.initialized -or $Remote.initialized);settings=@($settingsRows | Sort-Object scope,section,key);workspaces=$spaceRows;items=$itemRows}; Conflicts=@() }
}
