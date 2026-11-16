# Bộ prompt ngắn theo giai đoạn

## Cách dùng
- Gửi **một prompt mỗi lần** cho agent trong IDE.
- Sau khi agent sửa code, chạy lệnh kiểm tra ở checkpoint.
- Nếu thất bại, chưa chuyển bước; gửi lỗi thực tế cho agent.
- Các prompt yêu cầu agent không làm vượt phạm vi và phải báo cáo lệnh đã chạy.

---

## P0 — Khảo sát môi trường (chưa sửa code)

### P0.1 — Kiểm tra công cụ
**Prompt cho agent**
> Đọc AGENTS.md. Chưa sửa file. Kiểm tra gcc, make, git, sqlite3 và các header/thư viện SQLite có sẵn. Báo lệnh đã chạy, phiên bản và phần còn thiếu.

**Lệnh bạn chạy trên Ubuntu**
```bash
gcc --version
make --version
git --version
sqlite3 --version
```
Nếu `sqlite3` chưa có, chưa cần cài ngay; kiểm tra với agent xem thư viện phát triển có sẵn không.

### P0.2 — Khảo sát thư mục
**Prompt**
> Đọc AGENTS.md và kiểm tra cấu trúc thư mục hiện tại. Không xóa hoặc ghi đè file. Liệt kê file đang có và đề xuất các thư mục tối thiểu cho giai đoạn đầu.

**Lệnh kiểm tra**
```bash
pwd
find . -maxdepth 2 -type f -print
git status --short
```
`git status` có thể báo không phải repository nếu nhóm chưa khởi tạo Git.

**Checkpoint P0:** biết rõ thư mục làm việc, công cụ biên dịch và file hiện có.

---

## P1 — Khởi tạo dự án C

### P1.1 — Tạo bộ khung tối thiểu
**Prompt**
> Đọc AGENTS.md. Tạo bộ khung C tối thiểu gồm src/server, src/client, src/common, include và tests, cùng Makefile có mục tiêu all, server, client, clean. Chỉ tạo chương trình mẫu tối thiểu để kiểm tra build; chưa viết socket hay luật chơi. Dùng -Wall -Wextra -Wpedantic. Liệt kê file đã tạo.

**Lệnh kiểm tra**
```bash
make clean
make
find . -maxdepth 3 -type f -print
```

### P1.2 — Kiểm tra build sạch
**Prompt**
> Chạy make clean rồi make. Chỉ sửa warning/error phát sinh trong bộ khung hiện tại. Không thêm tính năng. Báo chính xác kết quả.

**Checkpoint P1:** `make` thành công, không có warning từ mã của nhóm.

---

## P2 — TCP server/client tối thiểu

### P2.1 — Server lắng nghe
**Prompt**
> Đọc AGENTS.md. Viết server TCP IPv4 tối thiểu: tạo socket, bind địa chỉ loopback và một port cấu hình rõ ràng, listen, accept kết nối, in thông báo kết nối và đóng socket đúng cách. Chưa thêm đa luồng, JSON, tài khoản hoặc luật chơi. Cập nhật Makefile và biên dịch.

**Kiểm tra**
```bash
make
```
Chạy server theo cách agent ghi trong README/hướng dẫn. Ở terminal khác:
```bash
ss -ltn
```

### P2.2 — Client kết nối
**Prompt**
> Thêm client TCP tối thiểu kết nối tới server ở loopback/port đã thống nhất. Báo kết quả kết nối; xử lý lỗi socket/connect; chưa thêm giao thức ứng dụng. Giữ thay đổi nhỏ và biên dịch.

**Kiểm tra**
```bash
make
```
Chạy server trước, sau đó chạy client ở terminal khác. Xác nhận server ghi nhận client.

### P2.3 — Trao đổi byte đơn giản
**Prompt**
> Bổ sung một lần trao đổi thông điệp thử nghiệm giữa client và server để chứng minh hai chiều gửi/nhận hoạt động. Chưa coi một recv() là ranh giới thông điệp; ở bước này chỉ dùng payload thử nghiệm nhỏ và ghi rõ giới hạn còn lại. Biên dịch và hướng dẫn chạy.

**Checkpoint P2:** client kết nối được và có trao đổi dữ liệu; server không crash khi client thoát bình thường.

---

## P3 — Đóng khung TCP và JSON-line

### P3.1 — Thiết kế framing
**Prompt**
> Đọc AGENTS.md. Đề xuất API C cho giao thức TCP JSON-line: gửi đủ byte, nhận thông điệp tới newline, xử lý partial read, nhiều thông điệp trong một lần recv, EOF và giới hạn kích thước. Trước tiên chỉ trình bày thiết kế và các file dự kiến, chưa sửa code.

### P3.2 — Viết hàm gửi đủ byte
**Prompt**
> Cài hàm gửi đủ toàn bộ buffer qua TCP, xử lý partial send và lỗi EINTR, trả mã lỗi rõ ràng. Thêm test phù hợp. Không sửa luật chơi hoặc các mô-đun không liên quan.

**Kiểm tra**
```bash
make clean
make
```

### P3.3 — Nhận thông điệp theo newline
**Prompt**
> Cài bộ nhận JSON-line có buffer tích lũy, trả đúng một thông điệp hoàn chỉnh mỗi lần gọi, giữ lại phần dư cho lần sau, xử lý EOF, thông điệp quá dài và dữ liệu bị chia nhỏ. Không giả định một recv() tương ứng một thông điệp. Thêm test cho các trường hợp này.

### P3.4 — Phân tích JSON
**Prompt**
> Khảo sát lựa chọn parser JSON C sẵn có trong môi trường và đề xuất phương án ít phụ thuộc nhất. Chưa cài thư viện mới nếu chưa được xác nhận. Sau khi thống nhất, tạo API parse/validate trường type và payload, xử lý JSON sai định dạng an toàn.

**Checkpoint P3:** test được partial message, nhiều message liền nhau, message quá dài và JSON lỗi.

---

## P4 — Nhiều client và độ bền server

### P4.1 — Chọn mô hình đồng thời
**Prompt**
> Dựa trên yêu cầu IT4062, so sánh ngắn gọn mô hình đa luồng với poll/select cho tối đa 100 client. Chọn một mô hình phù hợp với code hiện tại, nêu ưu/nhược điểm và các trạng thái dùng chung cần bảo vệ. Chưa sửa code.

### P4.2 — Triển khai mô hình đã chọn
**Prompt**
> Triển khai mô hình đồng thời đã thống nhất để server phục vụ nhiều client. Một client ngắt kết nối hoặc gửi dữ liệu lỗi không được làm dừng server. Quản lý vòng đời socket và bộ nhớ rõ ràng; không thêm luật chơi.

### P4.3 — Kiểm thử nhiều client
**Prompt**
> Tạo script kiểm thử mở nhiều client thử nghiệm, gửi thông điệp hợp lệ và ngắt kết nối theo kịch bản. Bắt đầu với 2, 10 rồi tăng dần; không mặc định đã đạt 100 nếu chưa chạy test. Ghi số client thành công và lỗi quan sát được.

**Lệnh kiểm tra**
```bash
make clean
make
ss -ltn
```

**Checkpoint P4:** server tiếp tục hoạt động sau khi một client thoát bất ngờ; kiểm thử tải có kết quả ghi lại.

---

## P5 — SQLite, tài khoản và phiên

### P5.1 — Thiết kế schema
**Prompt**
> Dựa trên báo cáo nhóm, đề xuất schema SQLite cho users và login_sessions trước. Chỉ trình bày bảng, khóa, ràng buộc và truy vấn cần thiết; chưa triển khai các bảng game.

### P5.2 — Khởi tạo database
**Prompt**
> Cài khởi tạo SQLite cho users và login_sessions theo schema đã duyệt. Dùng prepared statements, kiểm tra lỗi và giải phóng statement/database đúng cách. Thêm test khởi tạo và truy vấn.

### P5.3 — Đăng ký
**Prompt**
> Thêm thông điệp REGISTER và phản hồi REGISTER_RESULT. Kiểm tra username trùng, đầu vào sai và lỗi database. Không lưu mật khẩu thuần; nếu cần thư viện băm mật khẩu, báo phụ thuộc trước khi thêm.

### P5.4 — Đăng nhập/đăng xuất
**Prompt**
> Thêm LOGIN/LOGIN_RESULT và LOGOUT theo giao thức đã thống nhất. Tạo, xác thực và vô hiệu hóa session ở server; yêu cầu game chỉ được chấp nhận khi session hợp lệ. Thêm test thành công và thất bại.

**Checkpoint P5:** đăng ký, đăng nhập, đăng xuất và kiểm tra session chạy được; dữ liệu được lưu trong SQLite.

---

## P6 — Phòng chờ và trạng thái trận

### P6.1 — Mô hình dữ liệu trận
**Prompt**
> Dựa trên báo cáo, thiết kế cấu trúc dữ liệu trong RAM cho một trận: match id, danh sách người chơi, điểm, vai trò, trạng thái và lượt hiện tại. Đề xuất API và quy tắc bảo vệ trạng thái; chưa cài luật chấm điểm.

### P6.2 — Tham gia phòng chờ
**Prompt**
> Cài JOIN_LOBBY và LOBBY_UPDATED. Chặn người dùng chưa đăng nhập, người tham gia trùng và vượt quá 100 người. Khởi tạo 10 điểm khi trận bắt đầu theo đặc tả đã thống nhất; thêm test giới hạn.

### P6.3 — Bắt đầu trận
**Prompt**
> Cài chuyển trạng thái lobby sang trận đấu và START_GAME/GAME_STARTED theo quyền điều khiển đã thống nhất. Không tự thêm giao diện quản trị; nếu chưa có quyết định ai được bắt đầu trận, hỏi trước.

**Checkpoint P6:** quản lý được danh sách người chơi, giới hạn trận và trạng thái trận.

---

## P7 — Câu hỏi và thời gian

### P7.1 — Ngân hàng câu hỏi
**Prompt**
> Thiết kế và cài mô-đun lấy câu hỏi cùng đáp án đúng từ SQLite. Đáp án đúng chỉ được server giữ; không gửi đáp án đúng trong thông điệp câu hỏi tới client.

### P7.2 — Chọn người chơi chính
**Prompt**
> Cài luồng câu hỏi chọn người chơi chính. Server ghi thời điểm nhận đáp án hợp lệ; nếu bằng nhau theo độ phân giải thời gian, dùng quy tắc tie-break xác định và tài liệu hóa. Không dựa vào đồng hồ client.

### P7.3 — Bộ đếm thời gian lượt
**Prompt**
> Cài thời hạn 30 giây cho người phụ và 60 giây cho người chính bằng cơ chế không busy-wait. Khi hết hạn, chốt lượt và từ chối đáp án đến muộn. Thêm test hết giờ và đáp án đúng hạn.

### P7.4 — Nhận đáp án
**Prompt**
> Cài ANSWER_SUBMIT/ANSWER_ACK. Kiểm tra session, match, round, vai trò, hạn giờ, định dạng lựa chọn và việc người chơi đã gửi đáp án chưa. Chỉ nhận tối đa một đáp án hợp lệ mỗi người mỗi lượt.

**Checkpoint P7:** câu hỏi được phát, deadline do server quản lý và đáp án muộn bị từ chối.

---

## P8 — Luật chơi và điểm số

### P8.1 — Chốt luật còn thiếu
**Prompt**
> Đối chiếu luật trong AGENTS.md với code hiện tại. Liệt kê những tình huống luật chưa được quyết định, đặc biệt nhánh bỏ qua và chia điểm dư. Không tự chọn luật; đưa ra câu hỏi ngắn để nhóm xác nhận.

### P8.2 — Tách bộ xử lý kết quả
**Prompt**
> Tạo hàm xử lý kết quả một lượt độc lập với socket: nhận trạng thái người chơi và đáp án đã chốt, trả về thay đổi điểm, loại người, vai trò mới và trạng thái trận. Chỉ triển khai nhánh luật đã được xác nhận; thêm unit test.

### P8.3 — Người chính đúng/sai
**Prompt**
> Cài và kiểm thử hai nhánh người chính trả lời đúng và sai theo luật đã duyệt. Bảo đảm mỗi lượt chỉ được chấm một lần và không cập nhật điểm một phần nếu phát sinh lỗi.

### P8.4 — Bỏ qua
**Prompt**
> Chỉ cài SKIP_REQUEST/SKIP_RESULT sau khi luật bỏ qua đã được nhóm xác nhận. Kiểm tra giới hạn 2 lần và các tình huống biên đã chốt. Nếu còn điểm chưa thống nhất, dừng tại đó và liệt kê câu hỏi.

### P8.5 — Kết thúc trận
**Prompt**
> Cài điều kiện kết thúc trận, GAME_FINISHED và kết quả cuối cùng theo luật đã duyệt. Thêm test có người thắng và không có người thắng.

**Checkpoint P8:** các nhánh luật được kiểm thử độc lập, bao gồm trường hợp biên đã thống nhất.

---

## P9 — Đồng bộ và an toàn trạng thái

### P9.1 — Phát thông báo
**Prompt**
> Cài cơ chế gửi ROUND_STARTED, ROUND_RESULT và SCORE_UPDATED tới đúng các client còn trong trận. Một client gửi chậm hoặc lỗi không được chặn vô hạn việc phục vụ các client khác.

### P9.2 — Bảo vệ trạng thái
**Prompt**
> Rà soát mọi đường code có thể thay đổi điểm, vai trò, trạng thái lượt và danh sách người chơi. Bảo vệ các thao tác liên quan để không có hai luồng cùng chấm một lượt hoặc cập nhật điểm không nhất quán. Thêm test đồng thời.

### P9.3 — Ghi lịch sử
**Prompt**
> Ghi answers, score_transfers, rounds và game_logs theo một giao dịch logic nhất quán. Kiểm tra lỗi giữa chừng để tránh điểm đã đổi nhưng lịch sử không được ghi. Thêm test rollback nếu có thể tái hiện.

**Checkpoint P9:** mọi client nhận trạng thái nhất quán; không chấm trùng hoặc sai điểm khi có yêu cầu đồng thời.

---

## P10 — Kiểm thử, dọn dẹp và bàn giao

### P10.1 — Kiểm tra build
**Prompt**
> Chạy clean build với -Wall -Wextra -Wpedantic. Sửa warning/error thuộc code nhóm, không tắt cảnh báo để che lỗi. Báo kết quả thực tế.

### P10.2 — Kiểm thử lỗi mạng
**Prompt**
> Bổ sung test client ngắt kết nối giữa thông điệp, gửi JSON lỗi, gửi message quá dài và đóng kết nối bất ngờ. Xác nhận server vẫn phục vụ client còn lại.

### P10.3 — Kiểm tra tài nguyên
**Prompt**
> Chạy Valgrind trên kịch bản test khả thi nếu công cụ có sẵn. Kiểm tra leak bộ nhớ và file descriptor; phân biệt rõ lỗi của chương trình với hạn chế môi trường. Không tuyên bố sạch nếu chưa có log.

### P10.4 — Tài liệu giao thức
**Prompt**
> Hoàn thiện tài liệu giao thức gồm các loại message, trường bắt buộc, ví dụ JSON, trình tự trao đổi, mã lỗi, timeout, xử lý disconnect và giới hạn kích thước. Đảm bảo tài liệu khớp code hiện tại.

### P10.5 — Đánh giá cuối
**Prompt**
> Đối chiếu code với mọi yêu cầu bắt buộc trong PDF đề bài và báo cáo nhóm. Tạo bảng PASS/FAIL/NOT TESTED, kèm lệnh hoặc log bằng chứng. Không đánh dấu PASS nếu chưa kiểm chứng.

**Checkpoint P10:** có source, Makefile, tài liệu giao thức, script tải và bằng chứng kiểm thử.

---

## Mẫu prompt sửa lỗi dùng ở mọi giai đoạn
> Đọc AGENTS.md. Đây là lỗi thực tế: [dán nguyên văn lỗi]. Hãy xác định nguyên nhân, sửa tối thiểu các file liên quan và không thêm tính năng mới. Sau đó chạy lại đúng lệnh gây lỗi, báo file đã sửa và kết quả thực tế. Nếu chưa thể kiểm chứng, nói rõ.

## Lưu ý
- Không chạy tất cả prompt một lượt.
- Nếu agent tự ý đổi giao thức, luật chơi hoặc thêm thư viện, yêu cầu dừng và giải thích.
- Chỉ tăng tải sau khi test ít client ổn định.
- Không dùng dữ liệu tài khoản thật trong kiểm thử.
