# IT4062 — Đấu trường tri thức trực tuyến

## Mục tiêu
Xây dựng chương trình gameshow trắc nghiệm nhiều người chơi theo mô hình client-server bằng C trên Linux.

## Bắt đầu
1. Mở thư mục dự án trong AI IDE.
2. Đảm bảo agent đọc `AGENTS.md`.
3. Mở `COMMANDS_VI.md` và thực hiện lần lượt từng prompt, không gửi cả danh sách cùng lúc.
4. Sau mỗi prompt, tự chạy lệnh kiểm tra được nêu ở mục tương ứng.

## Quy tắc
- Mỗi prompt là một nhiệm vụ riêng.
- Không bỏ qua checkpoint.
- Nếu lỗi, gửi nguyên văn lỗi và yêu cầu agent chỉ sửa lỗi đó.
- Giữ báo cáo cuối mỗi bước để biết chính xác phần nào đã hoàn thành.

## Trạng thái
- [ ] P0: Khảo sát môi trường và cấu trúc dự án
- [ ] P1: Khởi tạo Makefile và chương trình mẫu
- [ ] P2: TCP server/client tối thiểu
- [ ] P3: Framing TCP và giao thức JSON-line
- [ ] P4: Phục vụ nhiều client và xử lý ngắt kết nối
- [ ] P5: SQLite và tài khoản/phiên
- [ ] P6: Phòng chờ và trạng thái trận
- [ ] P7: Luồng câu hỏi và bộ đếm thời gian
- [ ] P8: Logic điểm số, loại người chơi, bỏ qua
- [ ] P9: Đồng bộ thông báo và bảo vệ trạng thái dùng chung
- [ ] P10: Kiểm thử tải, lỗi mạng, tài nguyên và tài liệu bàn giao
