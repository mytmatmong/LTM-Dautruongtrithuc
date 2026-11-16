import socket
import json
import time
import sys

def test_lobby_e2e():
    SERVER_HOST = '127.0.0.1'
    SERVER_PORT = 8080

    def create_client():
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect((SERVER_HOST, SERVER_PORT))
        f = s.makefile('r', encoding='utf-8')
        return s, f

    def send_and_recv(s, f, obj):
        msg = (json.dumps(obj) + '\n').encode('utf-8')
        s.sendall(msg)
        line = f.readline()
        if not line:
            raise RuntimeError("Connection closed by server")
        return json.loads(line)

    print("=== TEST 1: Chặn tham gia phòng chờ khi chưa đăng nhập / session không hợp lệ ===")
    s1, f1 = create_client()
    resp = send_and_recv(s1, f1, {'type': 'JOIN_LOBBY', 'session_id': 'fake_token_123'})
    assert resp.get('status') == 'error', f"Lỗi: không chặn session ảo: {resp}"
    print(f" -> OK: Bị từ chối: {resp}")

    print("=== TEST 2: Đăng ký & Đăng nhập cho user 1 ===")
    u1 = f"user1_{int(time.time())}"
    r_reg1 = send_and_recv(s1, f1, {'type': 'REGISTER', 'username': u1, 'password': 'Password123!'})
    assert r_reg1.get('status') == 'ok', f"Đăng ký thất bại: {r_reg1}"

    r_login1 = send_and_recv(s1, f1, {'type': 'LOGIN', 'username': u1, 'password': 'Password123!'})
    assert r_login1.get('status') == 'ok', f"Đăng nhập thất bại: {r_login1}"
    token1 = r_login1.get('session_id') or r_login1['payload']['session_id']
    print(f" -> OK: User 1 đăng nhập với token: {token1}")

    print("=== TEST 3: User 1 tham gia phòng chờ (JOIN_LOBBY) ===")
    r_join1 = send_and_recv(s1, f1, {'type': 'JOIN_LOBBY', 'session_id': token1})
    assert r_join1.get('status') == 'ok', f"Join lobby thất bại: {r_join1}"
    print(f" -> OK: Phản hồi JOIN_LOBBY_RESULT: {r_join1}")

    # Đọc bản tin broadcast LOBBY_UPDATED trên client 1
    broadcast1 = json.loads(f1.readline())
    assert broadcast1.get('type') == 'LOBBY_UPDATED', f"Không nhận được LOBBY_UPDATED: {broadcast1}"
    print(f" -> OK: Nhận thông điệp broadcast: {broadcast1}")

    print("=== TEST 4: Chặn người tham gia trùng (User 1 gửi lại JOIN_LOBBY) ===")
    r_join_dup = send_and_recv(s1, f1, {'type': 'JOIN_LOBBY', 'session_id': token1})
    assert r_join_dup.get('status') == 'error', f"Không chặn trùng: {r_join_dup}"
    assert r_join_dup['error']['code'] == 'ALREADY_JOINED', f"Mã lỗi không khớp: {r_join_dup}"
    print(f" -> OK: Bị từ chối trùng: {r_join_dup}")

    print("=== TEST 5: User 2 đăng nhập và vào phòng chờ, kiểm tra broadcast cả 2 ===")
    s2, f2 = create_client()
    u2 = f"user2_{int(time.time())}"
    send_and_recv(s2, f2, {'type': 'REGISTER', 'username': u2, 'password': 'Password123!'})
    r_login2 = send_and_recv(s2, f2, {'type': 'LOGIN', 'username': u2, 'password': 'Password123!'})
    token2 = r_login2.get('session_id') or r_login2['payload']['session_id']

    r_join2 = send_and_recv(s2, f2, {'type': 'JOIN_LOBBY', 'session_id': token2})
    assert r_join2.get('status') == 'ok', f"User 2 join lobby thất bại: {r_join2}"

    # Cả client 1 và client 2 đều phải nhận được LOBBY_UPDATED với player_count = 2
    b_u2 = json.loads(f2.readline())
    assert b_u2.get('type') == 'LOBBY_UPDATED' and b_u2.get('player_count') >= 2
    b_u1 = json.loads(f1.readline())
    assert b_u1.get('type') == 'LOBBY_UPDATED' and b_u1.get('player_count') >= 2
    print(f" -> OK: Cả hai client nhận cập nhật: player_count = {b_u1['player_count']}")

    print("=== TEST 6: User 2 (không phải host) cố tình START_GAME -> Bị từ chối ===")
    r_start_unauth = send_and_recv(s2, f2, {'type': 'START_GAME', 'session_id': token2})
    assert r_start_unauth.get('status') == 'error', f"Lỗi: cho phép non-host start: {r_start_unauth}"
    assert r_start_unauth['error']['code'] == 'UNAUTHORIZED'
    print(f" -> OK: Chặn user 2: {r_start_unauth}")

    print("=== TEST 7: User 1 (Host) bắt đầu trận START_GAME -> Cả 2 nhận GAME_STARTED ===")
    r_start_ok = send_and_recv(s1, f1, {'type': 'START_GAME', 'session_id': token1})
    assert r_start_ok.get('status') == 'ok', f"Host start thất bại: {r_start_ok}"
    print(f" -> OK: Phản hồi START_GAME_RESULT: {r_start_ok}")

    # Đọc broadcast GAME_STARTED
    b_start1 = json.loads(f1.readline())
    b_start2 = json.loads(f2.readline())
    assert b_start1.get('type') == 'GAME_STARTED' and b_start1.get('state') == 'SELECTING_MAIN'
    assert b_start2.get('type') == 'GAME_STARTED' and b_start2.get('initial_score') == 10
    print(f" -> OK: Broadcast GAME_STARTED thành công tới cả 2 clients: {b_start1}")

    print("=== TEST 8 (P7.2): Nhận câu hỏi chọn người chơi chính (SELECTION_QUESTION) ===")
    b_q1 = json.loads(f1.readline())
    b_q2 = json.loads(f2.readline())
    assert b_q1.get('type') == 'SELECTION_QUESTION', f"Không nhận được SELECTION_QUESTION: {b_q1}"
    assert b_q2.get('type') == 'SELECTION_QUESTION'
    assert 'correct_choice' not in b_q1 and 'correct_choice' not in b_q1.get('payload', {})
    print(f" -> OK: Cả hai client nhận câu hỏi chọn người chơi chính an toàn (không lộ đáp án): {b_q1}")

    print("=== TEST 9 (P7.2): Client nộp SELECTION_ANSWER & Server phân vai ROLE_ASSIGNED ===")
    # Client 2 nộp đáp án đúng "A"
    r_ans2 = send_and_recv(s2, f2, {'type': 'SELECTION_ANSWER', 'session_id': token2, 'choice': 'A'})
    assert r_ans2.get('status') == 'ok', f"Client 2 nộp đáp án lỗi: {r_ans2}"

    # Client 1 nộp đáp án đúng "A" sau Client 2
    r_ans1 = send_and_recv(s1, f1, {'type': 'SELECTION_ANSWER', 'session_id': token1, 'choice': 'A'})
    assert r_ans1.get('status') == 'ok', f"Client 1 nộp đáp án lỗi: {r_ans1}"

    # Khi cả 2 đã nộp, Server đánh giá và broadcast ROLE_ASSIGNED
    role1 = json.loads(f1.readline())
    role2 = json.loads(f2.readline())
    assert role1.get('type') == 'ROLE_ASSIGNED'
    assert role2.get('type') == 'ROLE_ASSIGNED'
    # Client 2 nộp trước nên phải là MAIN
    assert role1['payload']['main_username'] == u2
    print(f" -> OK: Phân vai thành công! Người chơi chính (nộp đúng sớm nhất): {role1['payload']['main_username']}")

    print("=== TEST 10 (P7.4): Nộp đáp án lượt chơi chính thức ANSWER_SUBMIT / ANSWER_ACK ===")
    # Gửi ANSWER_SUBMIT từ Client 2
    r_sub = send_and_recv(s2, f2, {'type': 'ANSWER_SUBMIT', 'session_id': token2, 'match_id': 1, 'round_id': 1, 'choice': 'B'})
    assert r_sub.get('type') == 'ANSWER_ACK'
    print(f" -> OK: Nhận phản hồi ANSWER_ACK: {r_sub}")

    # Gửi lại lần 2 từ cùng Client 2 -> Phải bị từ chối ALREADY_SUBMITTED
    r_sub_dup = send_and_recv(s2, f2, {'type': 'ANSWER_SUBMIT', 'session_id': token2, 'match_id': 1, 'round_id': 1, 'choice': 'C'})
    assert r_sub_dup.get('status') == 'error'
    assert r_sub_dup['error']['code'] == 'ALREADY_SUBMITTED'
    print(f" -> OK: Chặn nộp đúp thành công (ALREADY_SUBMITTED): {r_sub_dup}")

    s1.close()
    s2.close()
    print("=== TẤT CẢ TEST E2E LOBBY, START_GAME & P7.2 ĐÃ VƯỢT QUA! ===")
    print("=== TẤT CẢ TEST E2E LOBBY, START_GAME, P7.2 & P7.4 ĐÃ VƯỢT QUA! ===")

if __name__ == '__main__':
    test_lobby_e2e()
