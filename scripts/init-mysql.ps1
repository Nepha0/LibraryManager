param(
    [string]$Dsn = $(if ($env:LIBRARYMANAGER_DSN) { $env:LIBRARYMANAGER_DSN } else { 'LibraryManagerDSN' }),
    [switch]$Smoke
)

$ErrorActionPreference = 'Stop'
if ($Dsn -notmatch '^[\w .-]+$') {
    throw 'Invalid ODBC data source name.'
}
$database = if ($Smoke) { 'library_manager_smoke' } else { 'library_manager' }
$settings = [System.Data.Odbc.OdbcConnectionStringBuilder]::new()
$settings.Dsn = $Dsn
if ($env:LIBRARYMANAGER_DB_USER) {
    $settings['UID'] = $env:LIBRARYMANAGER_DB_USER
    $settings['PWD'] = [string]$env:LIBRARYMANAGER_DB_PASSWORD
} elseif ($env:LIBRARYMANAGER_DB_PASSWORD) {
    throw 'Set LIBRARYMANAGER_DB_USER when providing LIBRARYMANAGER_DB_PASSWORD.'
}

$connection = [System.Data.Odbc.OdbcConnection]::new($settings.ConnectionString)
try {
    $connection.Open()
    $command = $connection.CreateCommand()
    try {
        if ($Smoke) {
            $command.CommandText = 'CREATE DATABASE IF NOT EXISTS library_manager_smoke CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci'
            [void]$command.ExecuteNonQuery()
            $command.CommandText = 'USE library_manager_smoke'
            [void]$command.ExecuteNonQuery()
        }
        $command.CommandText = 'SELECT DATABASE()'
        if ($command.ExecuteScalar() -cne $database) {
            throw "The DSN did not select the expected database: $database"
        }
        $command.CommandText = "SET SESSION character_set_client = 'utf8mb4', character_set_connection = 'utf8mb4'"
        [void]$command.ExecuteNonQuery()
        $directory = Join-Path (Split-Path -Parent $PSScriptRoot) 'database/migrations/mysql'
        foreach ($name in @('001_backend_b.sql', '002_demo_catalog.sql', '003_unified_schema_v1.sql')) {
            $content = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $directory $name)
            $content = $content -replace '(?m)^\s*--[^\r\n]*', ''
            foreach ($statement in ($content -split '(?m);[ \t]*\r?$')) {
                if ([string]::IsNullOrWhiteSpace($statement)) { continue }
                $sql = $statement.Trim()

                # MySQL ODBC rejects SET NAMES. The connection character set
                # has already been configured above, so this directive is redundant.
                if ($sql -match '^SET\s+NAMES\b') {
                    continue
                }

                # The unified-schema migration performs conditional DDL with its
                # own prepared statements. MySQL does not permit nesting those
                # statements inside the UTF-8 wrapper used below.
                if ($sql -match '^(PREPARE|EXECUTE|DEALLOCATE|SET)\b') {
                    $command.CommandText = $sql
                    [void]$command.ExecuteNonQuery()
                    continue
                }

                # Pass UTF-8 bytes as ASCII so ANSI ODBC drivers preserve Chinese seed text.
                $bytes = [Text.Encoding]::UTF8.GetBytes($sql)
                $hex = [BitConverter]::ToString($bytes).Replace('-', '')
                $command.CommandText = "SET @library_migration_sql = CONVERT(0x$hex USING utf8mb4)"
                [void]$command.ExecuteNonQuery()
                $command.CommandText = 'PREPARE library_migration FROM @library_migration_sql'
                [void]$command.ExecuteNonQuery()
                try {
                    $command.CommandText = 'EXECUTE library_migration'
                    [void]$command.ExecuteNonQuery()
                }
                finally {
                    $command.CommandText = 'DEALLOCATE PREPARE library_migration'
                    [void]$command.ExecuteNonQuery()
                }
            }
            Write-Host "Applied $name to $database"
        }
    }
    finally { $command.Dispose() }
}
finally { $connection.Dispose() }
