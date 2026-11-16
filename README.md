## 📌 THÔNG TIN MÁY ẢO

* **Địa chỉ IP:** `192.168.113.3`
* **Cổng (Port):** `22`
* **Tài khoản (User):** `vboxuser`

---

## 🚀 I. QUY TRÌNH BẬT & KẾT NỐI

### Bước 1: Khởi động máy ảo (VirtualBox)
1. Mở ứng dụng **Oracle VM VirtualBox**.
2. Chọn máy ảo Ubuntu trong danh sách:
   * **Cách thường:** Nhấn **Start**.
   * **Cách tiết kiệm RAM/CPU (khuyên dùng):** Bấm mũi tên bên cạnh nút Start ➜ chọn **Headless Start** (máy ảo chạy ngầm không bật màn hình desktop giúp máy đỡ lag).
3. Đợi khoảng **15 – 30 giây** để hệ điều hành Ubuntu tải xong dịch vụ mạng và SSH.

---

### Bước 2: Kết nối SSH vào máy ảo
Mở Terminal trên Windows (PowerShell hoặc Terminal tích hợp trong IDE) và chạy câu lệnh:

```powershell
ssh vboxuser@192.168.113.3
```

* Nhập mật khẩu máy ảo khi được yêu cầu.
* Khi kết nối thành công, dấu nhắc lệnh sẽ đổi thành:
  ```bash
  vboxuser@LTM-HW5:~$
  ```
  *(Từ lúc này, mọi câu lệnh gõ trong tab terminal này sẽ thực thi trực tiếp trên Ubuntu).*

---

## 🛑 II. QUY TRÌNH TẮT (GIẢI PHÓNG TÀI NGUYÊN)

Nên thực hiện theo đúng thứ tự để giải phóng hoàn toàn RAM và CPU cho Windows:

### Bước 1: Thoát phiên SSH
Tại terminal Ubuntu đang kết nối, nhập lệnh:

```bash
exit
```
*(hoặc nhấn tổ hợp phím `Ctrl + D`)*

---

### Bước 2: Tắt máy ảo VirtualBox
Có 2 cách:

* **Cách 1 (Từ terminal trước khi exit - Nhanh nhất):**
  Chạy lệnh tắt máy trực tiếp trong Ubuntu:
  ```bash
  sudo poweroff
  ```

* **Cách 2 (Từ giao diện VirtualBox):**
  Trên cửa sổ máy ảo VirtualBox, bấm nút **[X]** ở góc trên bên phải ➜ Chọn **Send the shutdown signal** (hoặc **Power off the machine**) ➜ Bấm **OK**.

---

### Bước 3: Đóng IDE / PowerShell
Lưu code và đóng các ứng dụng trên Windows như bình thường.