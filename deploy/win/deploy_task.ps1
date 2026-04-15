param(
    [string]$TaskName = "VSCapture",
    [string]$ExePath,
    [string]$UserId
)
$xmlPath = Join-Path $env:TEMP "$TaskName.xml"
$xml = @"
<?xml version="1.0" encoding="UTF-16"?>
<Task version="1.4" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">

    <RegistrationInfo>
        <Description>$TaskName</Description>
    </RegistrationInfo>

    <Triggers>
        <LogonTrigger>
            <Enabled>true</Enabled>
            <Delay>PT30S</Delay>
        </LogonTrigger>
    </Triggers>

    <Principals>
        <Principal id="Author">
            <UserId>$UserId</UserId>
            <LogonType>InteractiveToken</LogonType>
            <RunLevel>HighestAvailable</RunLevel>
        </Principal>
    </Principals>

    <Settings>
        <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>
        <AllowHardTerminate>false</AllowHardTerminate>
        <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>

        <RestartOnFailure>
            <Interval>PT1M</Interval>
            <Count>30</Count>
        </RestartOnFailure>

        <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>
        <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>

        <StartWhenAvailable>true</StartWhenAvailable>
        <Enabled>true</Enabled>
    </Settings>

    <Actions Context="Author">
        <Exec>
            <Command>$ExePath</Command>
            <Arguments>--background</Arguments>
        </Exec>
    </Actions>

</Task>
"@
Set-Content -Path $xmlPath -Value $xml -Encoding Unicode

# Only delete if task exists
if (schtasks /query /tn "$TaskName" /fo LIST | findstr "$TaskName") {
    schtasks /end /tn "$TaskName" | Out-Null
    schtasks /delete /tn "$TaskName" /f | Out-Null
}

schtasks /create /tn "$TaskName" /xml "$xmlPath" /f
schtasks /run /tn "$TaskName"