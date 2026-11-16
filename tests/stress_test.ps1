param (
    [int[]]$Stages = @(2, 10, 25, 50, 100),
    [string]$ServerHost = "127.0.0.1",
    [int]$ServerPort = 8080
)

function Run-Batch {
    param (
        [int]$Count,
        [string]$Mode = "normal"
    )

    Write-Host "`n=================================================="
    Write-Host "[*] Chạy kiểm thử với $Count clients đồng thời (Kịch bản: $Mode)..." -ForegroundColor Cyan
    $sw = [System.Diagnostics.Stopwatch]::StartNew()

    $results = [System.Collections.Concurrent.ConcurrentBag[int]]::new()
    $errors = [System.Collections.Concurrent.ConcurrentBag[string]]::new()

    $tasks = [System.Collections.Generic.List[System.Threading.Tasks.Task]]::new()

    for ($i = 1; $i -le $Count; $i++) {
        $clientId = $i
        $task = [System.Threading.Tasks.Task]::Run([Action]{
            try {
                $client = New-Object System.Net.Sockets.TcpClient
                $client.ReceiveTimeout = 4000
                $client.SendTimeout = 4000
                $client.Connect($ServerHost, $ServerPort)
                $stream = $client.GetStream()

                if ($Mode -eq "normal") {
                    $msg = "HELLO_CLIENT_$clientId`n"
                    $bytes = [System.Text.Encoding]::UTF8.GetBytes($msg)
                    $stream.Write($bytes, 0, $bytes.Length)

                    $buf = New-Object byte[] 128
                    $read = $stream.Read($buf, 0, $buf.Length)
                    $resp = [System.Text.Encoding]::UTF8.GetString($buf, 0, $read)
                    $client.Close()

                    if ($resp -match "PONG") {
                        $results.Add(1)
                    } else {
                        $results.Add(0)
                        $errors.Add("Client $clientId: Phản hồi không đúng: $resp")
                    }
                }
                elseif ($Mode -eq "abrupt_disconnect") {
                    $msg = "PARTIAL_BURST_WITHOUT_NEWLINE"
                    $bytes = [System.Text.Encoding]::UTF8.GetBytes($msg)
                    $stream.Write($bytes, 0, $bytes.Length)
                    [System.Threading.Thread]::Sleep(10)
                    $client.Close()
                    $results.Add(1)
                }
            } catch {
                $results.Add(0)
                $errors.Add("Client $clientId Exception: $($_.Exception.Message)")
            }
        })
        $tasks.Add($task)
        [System.Threading.Thread]::Sleep(3)
    }

    [System.Threading.Tasks.Task]::WaitAll($tasks.ToArray())
    $sw.Stop()

    $successCount = 0
    foreach ($r in $results) {
        if ($r -eq 1) { $successCount++ }
    }
    $failureCount = $Count - $successCount

    Write-Host "[+] Hoàn thành đợt $Count clients trong $($sw.Elapsed.TotalSeconds.ToString("0.00"))s."
    Write-Host "    - Thành công: $successCount / $Count" -ForegroundColor Green
    if ($failureCount -gt 0) {
        Write-Host "    - Thất bại: $failureCount / $Count" -ForegroundColor Red
        Write-Host "    - Chi tiết lỗi quan sát được:"
        $shown = 0
        foreach ($err in $errors) {
            if ($shown -lt 5) {
                Write-Host "      * $err" -ForegroundColor Yellow
                $shown++
            }
        }
    }

    return ($failureCount -eq 0)
}

# Thực thi tăng dần tải theo kịch bản P4.3
$allPassed = $true
foreach ($stage in $Stages) {
    $ok = Run-Batch -Count $stage -Mode "normal"
    if (-not $ok) {
        $allPassed = $false
        Write-Host "[!] Đợt test $stage clients gặp lỗi. Dừng tăng tải." -ForegroundColor Yellow
        break
    }
    [System.Threading.Thread]::Sleep(400)
}

# Kiểm tra độ bền: client gửi dở dang rồi ngắt đột ngột
Write-Host "`n[*] Kiểm thử độ bền: 5 clients gửi partial data và ngắt kết nối đột ngột..." -ForegroundColor Yellow
Run-Batch -Count 5 -Mode "abrupt_disconnect" | Out-Null

# Kiểm chứng server vẫn tiếp tục hoạt động
Write-Host "`n[*] Kiểm chứng server vẫn hoạt động sau khi client thoát bất ngờ:" -ForegroundColor Cyan
$serverAlive = Run-Batch -Count 2 -Mode "normal"
if ($serverAlive) {
    Write-Host "`n[V] CHECKPOINT P4 THÀNH CÔNG: Server tiếp tục hoạt động ổn định và xử lý trọn vẹn kịch bản tải!" -ForegroundColor Green
} else {
    Write-Host "`n[X] CHECKPOINT P4 THẤT BẠI: Server bị gián đoạn!" -ForegroundColor Red
}
