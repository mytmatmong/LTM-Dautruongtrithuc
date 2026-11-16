import socket
import json
import time
import sys

def test_auth_e2e():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect(('127.0.0.1', 8080))
    f = s.makefile('r')

    def send_msg(obj):
        s.sendall((json.dumps(obj) + '\n').encode('utf-8'))
        line = f.readline()
        if not line:
            raise RuntimeError("Connection closed by server")
        return json.loads(line)

    username = f"player_{int(time.time())}"
    password = "SafePassword123!"

    print(f"[TEST 1] Register new user '{username}'...")
    r1 = send_msg({'type': 'REGISTER', 'payload': {'username': username, 'password': password}})
    assert r1.get('status') == 'ok', f"Register failed: {r1}"
    print(f" -> OK: ID={r1['payload']['user_id']}")

    print(f"[TEST 2] Register duplicate username '{username}'...")
    r2 = send_msg({'type': 'REGISTER', 'payload': {'username': username, 'password': password}})
    assert r2.get('status') == 'error' and r2.get('error_code') == 'USER_EXISTS', f"Duplicate check failed: {r2}"
    print(f" -> OK: Rejected with USER_EXISTS")

    print(f"[TEST 3] Login with wrong password...")
    r3 = send_msg({'type': 'LOGIN', 'payload': {'username': username, 'password': 'WrongPassword'}})
    assert r3.get('status') == 'error' and r3.get('error_code') == 'AUTH_FAILED', f"Wrong pass check failed: {r3}"
    print(f" -> OK: Rejected with AUTH_FAILED")

    print(f"[TEST 4] Login with correct credentials...")
    r4 = send_msg({'type': 'LOGIN', 'payload': {'username': username, 'password': password}})
    assert r4.get('status') == 'ok' and 'session_id' in r4, f"Login failed: {r4}"
    token = r4['session_id']
    print(f" -> OK: Received session_id = {token}")

    print(f"[TEST 5] Send Game Request with valid session...")
    r5 = send_msg({'type': 'JOIN_LOBBY', 'session_id': token})
    assert r5.get('status') == 'ok', f"Game action failed: {r5}"
    print(f" -> OK: Authorized ({r5['payload']})")

    print(f"[TEST 6] Send Game Request with invalid session...")
    r6 = send_msg({'type': 'JOIN_LOBBY', 'session_id': 'invalid_token_xyz'})
    assert r6.get('status') == 'error' and r6.get('error_code') == 'UNAUTHORIZED', f"Unauthorized check failed: {r6}"
    print(f" -> OK: Rejected with UNAUTHORIZED")

    print(f"[TEST 7] Logout session...")
    r7 = send_msg({'type': 'LOGOUT', 'session_id': token})
    assert r7.get('status') == 'ok', f"Logout failed: {r7}"
    print(f" -> OK: Logged out successfully")

    print(f"[TEST 8] Send Game Request after logout...")
    r8 = send_msg({'type': 'JOIN_LOBBY', 'session_id': token})
    assert r8.get('status') == 'error' and r8.get('error_code') == 'UNAUTHORIZED', f"Post-logout check failed: {r8}"
    print(f" -> OK: Rejected after logout")

    s.close()
    print("=== ALL CLIENT E2E INTEGRATION TESTS PASSED! ===")

if __name__ == '__main__':
    test_auth_e2e()

