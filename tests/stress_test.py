import socket
import threading
import time
import sys

SERVER_HOST = "127.0.0.1"
SERVER_PORT = 8080

class TestStats:
    def __init__(self):
        self.lock = threading.Lock()
        self.success_count = 0
        self.error_count = 0
        self.errors = []

    def record_success(self):
        with self.lock:
            self.success_count += 1

    def record_error(self, err_msg):
        with self.lock:
            self.error_count += 1
            if len(self.errors) < 10:
                self.errors.append(err_msg)

def run_client_scenario(client_id, mode, stats):
    """
    mode:
      - 'normal': kết nối, gửi PING\n, nhận PONG\n, ngắt kết nối bình thường.
      - 'abrupt_disconnect': kết nối, gửi dở dang rồi đóng socket ngay lập tức (kiểm tra độ bền server).
      - 'burst': gửi liên tiếp 3 tin nhắn kèm newline và kiểm tra đủ 3 phản hồi.
    """
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(5.0)
        s.connect((SERVER_HOST, SERVER_PORT))

        if mode == 'normal':
            msg = f"HELLO_FROM_CLIENT_{client_id}\n"
            s.sendall(msg.encode('utf-8'))
            data = s.recv(1024)
            if b"PONG\n" in data:
                stats.record_success()
            else:
                stats.record_error(f"Client {client_id}: Phản hồi không đúng: {data}")
            s.close()

        elif mode == 'burst':
            for i in range(3):
                s.sendall(f"BURST_MSG_{i}\n".encode('utf-8'))
                data = s.recv(1024)
                if b"PONG\n" not in data:
                    stats.record_error(f"Client {client_id}: burst {i} fail")
                    s.close()
                    return
            stats.record_success()
            s.close()

        elif mode == 'abrupt_disconnect':
            # Gửi dữ liệu dở dang (chưa có \n) rồi đóng socket đột ngột (RST/FIN)
            s.sendall(b"PARTIAL_DATA_WITHOUT_NEWLINE")
            time.sleep(0.05)
            s.close()
            # Kịch bản này thành công nếu không làm sập server
            stats.record_success()

    except Exception as e:
        stats.record_error(f"Client {client_id} Exception: {e}")

def run_batch_test(num_clients, mode='normal'):
    print(f"\n==================================================")
    print(f"[*] Bắt đầu kiểm thử tải với {num_clients} clients đồng thời (Kịch bản: '{mode}')...")
    stats = TestStats()
    threads = []

    start_time = time.time()
    for i in range(num_clients):
        t = threading.Thread(target=run_client_scenario, args=(i + 1, mode, stats))
        threads.append(t)
        t.start()
        # Giảm nghẽn syn flood bằng giãn cách cực nhỏ
        time.sleep(0.005)

    for t in threads:
        t.join()

    duration = time.time() - start_time
    print(f"[+] Hoàn thành đợt test ({num_clients} clients) trong {duration:.2f} giây.")
    print(f"    - Thành công: {stats.success_count}/{num_clients}")
    print(f"    - Gặp lỗi: {stats.error_count}/{num_clients}")
    if stats.errors:
        print(f"    - Chi tiết lỗi quan sát được (tối đa 10):")
        for err in stats.errors:
            print(f"      * {err}")
    
    return stats.success_count == num_clients

if __name__ == "__main__":
    target_stages = [2, 10, 25, 50, 100]
    if len(sys.argv) > 1:
        target_stages = [int(x) for x in sys.argv[1:]]

    all_passed = True
    for stage in target_stages:
        # Chạy kiểm thử bình thường
        ok = run_batch_test(stage, mode='normal')
        if not ok:
            all_passed = False
            print(f"[!] Dừng tăng tải do đợt {stage} clients xuất hiện lỗi.")
            break
        time.sleep(0.5)

    # Thử nghiệm kịch bản client thoát đột ngột để kiểm tra checkpoint độ bền
    print(f"\n[*] Kiểm thử độ bền: Client thoát bất ngờ / gửi dữ liệu dở dang...")
    run_batch_test(5, mode='abrupt_disconnect')

    # Kiểm tra lại server còn sống và phục vụ client bình thường sau sự cố không
    print(f"\n[*] Kiểm tra xác nhận server vẫn tiếp tục hoạt động sau lỗi:")
    ok_post = run_batch_test(2, mode='normal')
    if ok_post:
        print("[V] Server vẫn sống và phục vụ tốt sau khi client thoát bất ngờ!")
    else:
        print("[X] Server gặp sự cố sau khi client thoát bất ngờ!")

