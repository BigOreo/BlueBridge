# InputLeap -- mouse and keyboard sharing utility
# Copyright (C) InputLeap contributors
#
# This package is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# found in the file LICENSE that should have accompanied this file.
#
# This package is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

# Windows installer smoke test. Installs silently, checks the files, the
# background service and the firewall rule, starts the app, connects a client
# to a server over TLS on this computer, then uninstalls and checks that
# everything was removed. Needs to run as administrator.
#
#   powershell -File src\test\smoke\windows_installer.ps1 -Installer <setup.exe>

param(
    [Parameter(Mandatory = $true)][string]$Installer
)

$ErrorActionPreference = "Stop"

$AppDir = Join-Path $env:ProgramFiles "InputLeap"
$ServiceName = "InputLeap"
$FirewallRule = "InputLeap Listener"
$Work = Join-Path ([IO.Path]::GetTempPath()) ("installer-smoke-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $Work | Out-Null

function Step($message) { Write-Host "-- $message" }

function Fail($message) {
    Write-Host "FAIL: $message"
    foreach ($log in Get-ChildItem $Work -Filter *.log -ErrorAction SilentlyContinue) {
        Write-Host "`n==== $($log.Name) ===="
        Get-Content $log.FullName -Tail 80
    }
    Write-Host "installer smoke test FAILED"
    exit 1
}

function Wait-For($description, [scriptblock]$condition, $timeout = 30) {
    $deadline = (Get-Date).AddSeconds($timeout)
    while ((Get-Date) -lt $deadline) {
        if (& $condition) { return }
        Start-Sleep -Milliseconds 250
    }
    Fail "timed out waiting for $description"
}

function Run-Silent($exe, $logName) {
    $log = Join-Path $Work $logName
    $proc = Start-Process -FilePath $exe -Wait -PassThru `
        -ArgumentList "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/LOG=`"$log`""
    if ($proc.ExitCode -ne 0) { Fail "$exe exited with code $($proc.ExitCode)" }
}

# Creates a profile with a TLS certificate and returns its fingerprint in
# the trusted fingerprint file format.
function New-Profile($path) {
    # openssl prints progress on stderr, which Windows PowerShell would turn
    # into a terminating error; the exit code is checked instead
    $ErrorActionPreference = "Continue"
    $ssl = Join-Path $path "SSL"
    New-Item -ItemType Directory -Path (Join-Path $ssl "Fingerprints") -Force | Out-Null
    $pem = Join-Path $ssl "InputLeap.pem"
    $key = "$pem.key"
    $crt = "$pem.crt"
    & openssl req -x509 -nodes -newkey rsa:2048 -days 1 -subj "/CN=InputLeap" `
        -keyout $key -out $crt 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { Fail "openssl could not create a certificate" }
    Set-Content -Path $pem -Value ((Get-Content $key -Raw) + (Get-Content $crt -Raw)) -NoNewline
    Remove-Item $key, $crt
    $out = & openssl x509 -in $pem -noout -fingerprint -sha256
    return "v2:sha256:" + ($out.Split("=", 2)[1] -replace ":", "").ToLower()
}

function Set-Trusted($dir, $file, $fingerprint) {
    Set-Content -Path (Join-Path $dir "SSL\Fingerprints\$file") -Value $fingerprint
}

Step "installing $Installer"
Run-Silent $Installer "install.log"

foreach ($exe in "input-leap.exe", "input-leaps.exe", "input-leapc.exe", "input-leapd.exe") {
    if (-not (Test-Path (Join-Path $AppDir $exe))) { Fail "$exe was not installed" }
}
Step "PASS: the program files are installed"

Wait-For "the service to run" {
    $service = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
    $service -and $service.Status -eq "Running"
}
Step "PASS: the background service is running"

if (-not (Get-NetFirewallRule -DisplayName $FirewallRule -ErrorAction SilentlyContinue)) {
    Fail "the firewall rule was not added"
}
Step "PASS: the firewall rule is in place"

Step "starting the app"
$app = Start-Process -FilePath (Join-Path $AppDir "input-leap.exe") -PassThru
Start-Sleep -Seconds 10
if ($app.HasExited) { Fail "the app exited with code $($app.ExitCode) right after starting" }
Stop-Process -Id $app.Id -Force
Step "PASS: the app starts and keeps running"

Step "connecting a client to a server on this computer"
$serverProfile = Join-Path $Work "server-profile"
$clientProfile = Join-Path $Work "client-profile"
$serverFingerprint = New-Profile $serverProfile
$clientFingerprint = New-Profile $clientProfile
Set-Trusted $serverProfile "TrustedClients.txt" $clientFingerprint
Set-Trusted $clientProfile "TrustedServers.txt" $serverFingerprint
$config = Join-Path $Work "server.conf"
Set-Content -Path $config -Value @"
section: screens
    server:
    client:
end
section: links
    server:
        right = client
    client:
        left = server
end
"@
$serverLog = Join-Path $Work "server.log"
$clientLog = Join-Path $Work "client.log"
$address = "127.0.0.1:24890"
$server = Start-Process -FilePath (Join-Path $AppDir "input-leaps.exe") -PassThru -WindowStyle Hidden `
    -ArgumentList "-f", "--no-tray", "--name", "server", "--config", "`"$config`"", "--address", $address, `
        "--profile-dir", "`"$serverProfile`"", "--debug", "DEBUG", "--log", "`"$serverLog`""
Wait-For "the server to listen" {
    (Test-Path $serverLog) -and (Select-String -Path $serverLog -Pattern "started server" -Quiet)
}
$client = Start-Process -FilePath (Join-Path $AppDir "input-leapc.exe") -PassThru -WindowStyle Hidden `
    -ArgumentList "-f", "--no-tray", "--name", "client", "--profile-dir", "`"$clientProfile`"", `
        "--debug", "DEBUG", "--log", "`"$clientLog`"", $address
Wait-For "the client to connect" {
    Select-String -Path $serverLog -Pattern 'client "client" has connected' -SimpleMatch -Quiet
}
if (-not (Select-String -Path $serverLog -Pattern "accepted secure socket" -Quiet)) {
    Fail "the connection is not encrypted"
}
Stop-Process -Id $server.Id -Force
Wait-For "the client to notice the server stopped" {
    Select-String -Path $clientLog -Pattern "disconnected from server" -Quiet
}
Stop-Process -Id $client.Id -Force -ErrorAction SilentlyContinue
Step "PASS: the installed client connected to the installed server over TLS"

Step "uninstalling"
Run-Silent (Join-Path $AppDir "unins000.exe") "uninstall.log"
# the uninstaller copies itself to a temporary folder and returns early
Wait-For "the uninstaller to finish" { -not (Test-Path (Join-Path $AppDir "input-leap.exe")) } 60

if (Get-Service -Name $ServiceName -ErrorAction SilentlyContinue) { Fail "the service was not removed" }
if (Get-NetFirewallRule -DisplayName $FirewallRule -ErrorAction SilentlyContinue) {
    Fail "the firewall rule was not removed"
}
$left = Get-Process -Name "input-leap*" -ErrorAction SilentlyContinue
if ($left) { Fail "still running after uninstall: $($left.Name -join ', ')" }
Step "PASS: uninstalling removes the program, the service and the firewall rule"

Remove-Item -Recurse -Force $Work
Write-Host "installer smoke test passed"
