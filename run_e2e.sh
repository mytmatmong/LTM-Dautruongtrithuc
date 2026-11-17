#!/bin/bash
set -e
PORT=8989
DB_PATH="/dev/shm/tmp/e2e_test.db"

rm -f "$DB_PATH"*
/dev/shm/ltm_repo/bin/server $PORT "$DB_PATH" > /dev/shm/tmp/server_e2e.log 2>&1 &
SERVER_PID=$!
sleep 1

python3 - << 'EOF'
import socket

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.connect(('127.0.0.1', 8989))

# Test Register
s.sendall(b'{"type":"REGISTER","username":"user_e2e","password":"password123"}\n')
res = s.recv(1024).decode()
print("E2E Register response:", res.strip())
assert '"status":"ok"' in res

# Test Login
s.sendall(b'{"type":"LOGIN","username":"user_e2e","password":"password123"}\n')
res2 = s.recv(1024).decode()
print("E2E Login response:", res2.strip())
assert '"status":"ok"' in res2

s.close()
print("E2E socket test passed successfully!")
EOF

kill $SERVER_PID || true
rm -f "$DB_PATH"*
echo "ALL E2E CHECKS PASSED!"
