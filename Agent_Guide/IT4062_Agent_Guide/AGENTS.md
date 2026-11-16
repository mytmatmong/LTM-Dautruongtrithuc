# Quy ước chung cho AI Agent — IT4062 Đấu trường tri thức

## 1. Nguồn yêu cầu
- Đề bài chính thức: BTL_IT4062_20261_DeTai_va_Barem.pdf.
- Đặc tả hiện tại của nhóm: Báo cáo chủ đề 4.docx.
- Nếu báo cáo nhóm và đề chính thức khác nhau, không tự ý coi đề xuất của nhóm là luật bắt buộc. Nêu khác biệt và hỏi người dùng khi ảnh hưởng hành vi.
- Không tự bịa luật cho tình huống chưa được xác định. Ghi lại thành câu hỏi cần chốt.

## 2. Công nghệ và phạm vi
- Ngôn ngữ C, biên dịch trên Linux bằng gcc.
- Dùng TCP socket.
- Giao thức ứng dụng dùng JSON UTF-8; mỗi thông điệp kết thúc bằng newline `\\n`.
- SQLite là cơ sở dữ liệu dự kiến.
- Phiên bản đầu dùng CLI; không tự thêm web/mobile hoặc framework không được yêu cầu.
- Có Makefile; hướng tới biên dịch sạch với `-Wall -Wextra`.
- Không thêm thư viện bên ngoài nếu chưa giải thích lý do và xin xác nhận.

## 3. Nguyên tắc thiết kế
- Server là nguồn sự thật: server xác thực phiên, kiểm tra hạn trả lời, chấm đáp án, tính điểm, loại người chơi và quyết định người thắng.
- Client chỉ gửi yêu cầu và hiển thị phản hồi; không tự quyết định kết quả.
- Tách phần mạng, giao thức, luật chơi và lưu trữ thành các mô-đun có giao diện rõ ràng.
- Không dùng một lần `recv()` để giả định nhận đủ một thông điệp. TCP là luồng byte: phải xử lý thông điệp bị chia nhỏ hoặc nhiều thông điệp đến cùng lúc.
- Xử lý `send()`/`recv()` lỗi, EOF, ngắt kết nối, dữ liệu sai định dạng và giải phóng tài nguyên.
- Trạng thái dùng chung phải an toàn khi nhiều client hoạt động đồng thời.
- Không để một client lỗi làm dừng toàn bộ server.
- Không dùng busy-wait cho bộ đếm thời gian hoặc I/O.
- Không lưu mật khẩu dạng văn bản thuần. Nếu triển khai xác thực mật khẩu, chọn cách băm phù hợp và ghi rõ phụ thuộc thư viện.

## 4. Luật chơi đã mô tả trong báo cáo
- Tối đa 100 người trong một trận.
- Mỗi người bắt đầu với 10 điểm.
- Người phụ có 30 giây; người chính có 60 giây.
- Chọn người chính bằng câu hỏi chọn vai; xét đáp án đúng hợp lệ đến server sớm nhất.
- Người chính đúng: nhận tổng điểm của người phụ trả lời sai; người phụ trả lời sai bị loại. Nếu không còn người phụ trả lời đúng, người chính thắng.
- Người chính sai: bị loại; điểm được chia đều cho người chơi phụ còn lại; người phụ đúng sớm nhất thành người chính tiếp theo. Nếu tất cả người phụ sai, trận kết thúc không có người thắng.
- Người chính có tối đa 2 lần bỏ qua. Báo cáo mô tả cách chia điểm khi bỏ qua nhưng ghi nhận một số tình huống chưa rõ.
- Không tự quyết định luật cho tình huống bỏ qua mà không có người phụ đúng, trạng thái người phụ trả lời sai trong nhánh bỏ qua, hoặc các trường hợp biên chưa được thống nhất.

## 5. Quy trình làm việc bắt buộc
Với mỗi yêu cầu:
1. Đọc file này và các file liên quan trước khi sửa.
2. Nêu ngắn gọn file sẽ tạo/sửa và mục tiêu.
3. Thực hiện đúng phạm vi; không tái cấu trúc không liên quan.
4. Biên dịch và chạy kiểm thử liên quan.
5. Báo cáo: thay đổi, lệnh đã chạy, kết quả, lỗi còn lại, bước tiếp theo.
6. Không tuyên bố kiểm thử thành công nếu chưa thực sự chạy lệnh.
7. Không xóa hoặc ghi đè dữ liệu/người dùng tạo khi chưa xin phép.

## 6. Quy tắc chất lượng
- Hàm ngắn, tên rõ nghĩa, kiểm tra giá trị trả về.
- Quản lý bộ nhớ, socket, file và statement SQLite có đường giải phóng rõ ràng.
- Không để secret hoặc mật khẩu thật trong source control.
- Cập nhật README hoặc tài liệu giao thức khi thay đổi giao thức.
- Mỗi giai đoạn phải có tiêu chí nghiệm thu; không chuyển sang tính năng lớn tiếp theo nếu checkpoint hiện tại còn lỗi.
