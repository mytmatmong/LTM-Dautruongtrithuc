CC ?= gcc
CFLAGS ?= -Wall -Wextra -Wpedantic -std=c99 -Iinclude
BIN_DIR = bin

# Thư viện socket, sqlite3 và crypt
ifeq ($(OS),Windows_NT)
    LDLIBS += -lws2_32 -lsqlite3
else
    LDLIBS += -pthread -lsqlite3 -lcrypt
endif

SERVER_BIN = $(BIN_DIR)/server
CLIENT_BIN = $(BIN_DIR)/client
TEST_COMMON_BIN = $(BIN_DIR)/test_common
TEST_NET_SEND_BIN = $(BIN_DIR)/test_net_send
TEST_NET_RECV_BIN = $(BIN_DIR)/test_net_recv
TEST_DB_BIN = $(BIN_DIR)/test_db
TEST_AUTH_BIN = $(BIN_DIR)/test_auth
TEST_LOBBY_BIN = $(BIN_DIR)/test_lobby
TEST_NET_ERROR_BIN = $(BIN_DIR)/test_net_error

SERVER_SRCS = src/server/main.c src/server/db.c src/common/protocol.c src/common/net_framing.c src/common/auth_crypto.c src/common/auth_msg.c src/common/match.c
CLIENT_SRCS = src/client/main.c src/common/protocol.c src/common/net_framing.c src/common/auth_crypto.c src/common/auth_msg.c src/common/match.c
TEST_COMMON_SRCS = tests/test_common.c src/common/protocol.c
TEST_NET_SEND_SRCS = tests/test_net_send.c src/common/net_framing.c
TEST_NET_RECV_SRCS = tests/test_net_recv.c src/common/net_framing.c
TEST_DB_SRCS = tests/test_db.c src/server/db.c
TEST_AUTH_SRCS = tests/test_auth.c src/server/db.c src/common/auth_crypto.c src/common/auth_msg.c
TEST_LOBBY_SRCS = tests/test_lobby.c src/common/match.c src/common/net_framing.c
TEST_NET_ERROR_SRCS = tests/test_net_error.c src/server/db.c src/common/protocol.c src/common/net_framing.c src/common/auth_crypto.c src/common/auth_msg.c src/common/match.c

.PHONY: all server client test clean

all: server client test

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

server: $(BIN_DIR) $(SERVER_SRCS)
	$(CC) $(CFLAGS) $(SERVER_SRCS) -o $(SERVER_BIN) $(LDLIBS)

client: $(BIN_DIR) $(CLIENT_SRCS)
	$(CC) $(CFLAGS) $(CLIENT_SRCS) -o $(CLIENT_BIN) $(LDLIBS)

test: $(BIN_DIR) $(TEST_COMMON_SRCS) $(TEST_NET_SEND_SRCS) $(TEST_NET_RECV_SRCS) $(TEST_DB_SRCS) $(TEST_AUTH_SRCS) $(TEST_LOBBY_SRCS) $(TEST_NET_ERROR_SRCS)
	$(CC) $(CFLAGS) $(TEST_COMMON_SRCS) -o $(TEST_COMMON_BIN) $(LDLIBS)
	$(CC) $(CFLAGS) $(TEST_NET_SEND_SRCS) -o $(TEST_NET_SEND_BIN) $(LDLIBS)
	$(CC) $(CFLAGS) $(TEST_NET_RECV_SRCS) -o $(TEST_NET_RECV_BIN) $(LDLIBS)
	$(CC) $(CFLAGS) $(TEST_DB_SRCS) -o $(TEST_DB_BIN) $(LDLIBS)
	$(CC) $(CFLAGS) $(TEST_AUTH_SRCS) -o $(TEST_AUTH_BIN) $(LDLIBS)
	$(CC) $(CFLAGS) $(TEST_LOBBY_SRCS) -o $(TEST_LOBBY_BIN) $(LDLIBS)
	$(CC) $(CFLAGS) $(TEST_NET_ERROR_SRCS) -o $(TEST_NET_ERROR_BIN) $(LDLIBS)
	-$(TEST_COMMON_BIN)
	-$(TEST_NET_SEND_BIN)
	-$(TEST_NET_RECV_BIN)
	-$(TEST_DB_BIN)
	-$(TEST_AUTH_BIN)
	-$(TEST_LOBBY_BIN)
	-$(TEST_NET_ERROR_BIN)

clean:
	rm -rf $(BIN_DIR)
