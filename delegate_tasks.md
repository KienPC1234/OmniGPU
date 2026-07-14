**Chiến lược cốt lõi:** Dự án OmniGPU giống hệt một **nhà hàng giao đồ ăn tự động siêu tốc**.

* **Dev A (Tài xế & Đầu bếp):** Xây đường cao tốc (Mạng TCP), vận hành bếp chính (GPU Host), và hút chân không đồ ăn (Nén ảnh).
* **Dev B (Thu ngân & Đóng gói):** Lắp máy dịch tự động mọi ngôn ngữ sang tiếng Anh (Mesa Zink & clvk), ghi order (Chặn hàm Vulkan), và nhét đồ ăn vào hộp chuẩn (FlatBuffers).

---

### Giai đoạn 1: Nền móng mạng (Đổ đường nhựa & Xếp hộp giấy)

* **Dev A: Xây đường cao tốc (TCP Network)**
* **Viết Server/Client C++:** Dựng một đường hầm nối thẳng máy ảo và máy thật.
* **Bật `TCP_NODELAY`:** Vô hiệu hóa đèn đỏ, xe tải (gói tin) cứ có hàng là đạp ga chạy ngay lập tức.


* **Dev B: Chế tạo hộp đựng (FlatBuffers Serialization)**
* **Tích hợp FlatBuffers:** Đúc các hộp nhựa cứng để nén chặt mọi loại dữ liệu C++.
* **Định nghĩa khuôn (`.fbs`):** Phân loại rõ ràng hộp nào đựng lệnh vẽ, hộp nào đựng mảng dữ liệu.



### Giai đoạn 2: Lắp máy phiên dịch (Đồng nhất ngôn ngữ)

* **Dev B: Dịch thuật đồ họa & tính toán (Zink & clvk)**
* **Nhúng Mesa Zink:** Đặt file `opengl32.dll` vào máy ảo, tự động dịch mọi lệnh vẽ OpenGL thành Vulkan (Giống việc dịch khách gọi món Pháp sang tiếng Anh).
* **Nhúng clvk:** Đặt file OpenCL giả vào máy ảo, dịch mọi lệnh tính toán OpenCL thành Vulkan (Giống việc dịch khách gọi món Tây Ban Nha sang tiếng Anh).
* **Chốt hạ:** Gạch bỏ hoàn toàn khối lượng code thừa. Từ giờ, cả hai Dev CHỈ code cho một đường ống Vulkan duy nhất.



### Giai đoạn 3: Bắt lệnh Vulkan & Nấu ăn (Bắt đầu phục vụ)

* **Dev B: Cỗ máy ghi Order (Vulkan API Interception)**
* **Viết Python Script:** Viết script đọc file `vulkan.h` để tự động đẻ ra hàng trăm hàm chặn lệnh (Thay vì ngồi gõ code bằng tay như chép phạt).
* **Gom đơn (Batching):** Gộp 20 lệnh vẽ nhỏ vào chung 1 hộp FlatBuffers rồi mới quăng lên xe tải (Giống như gom 20 cốc trà sữa giao cùng 1 chuyến).


* **Dev A: Nấu ăn & Hút chân không (Host Render & Compression)**
* **Vẽ ẩn (Headless Render):** Ép GPU máy thật vẽ hình ảnh vào bộ nhớ RAM ngầm, không bật bất kỳ cửa sổ nào (Nấu ăn trong phòng kín).
* **Nén siêu tốc (LZ4/TurboJPEG):** Bóp nghẹt bức ảnh 8MB xuống 1MB chỉ trong 1 mili-giây.
* **Giao khung hình (Framebuffer):** Bắn ảnh đã nén về lại máy ảo để hiển thị cho người dùng.



### Giai đoạn 4: Mở rộng chuỗi nhà hàng (Tối ưu & Tự động hóa)

* **Dev A: Quản lý hàng chờ (Multi-threading & Multi-GPU)**
* **Mở nhiều quầy:** Thiết lập Server nhận nhiều máy ảo kết nối cùng lúc, mỗi máy ảo chạy một luồng độc lập.
* **Chia chác GPU:** Tự động điều phối linh hoạt (Ví dụ: Khách A đẩy vào card RTX 3050, khách B đẩy vào eGPU rời).


* **Dev B: Tự động hóa & Đóng gói (Caching & CI/CD)**
* **Bộ nhớ đệm (Caching):** Máy ảo tự học thuộc lòng giới hạn phần cứng của Host ngay lần kết nối đầu tiên, tuyệt đối không gửi gói tin hỏi đi hỏi lại gây tắc đường mạng.
* **Đóng gói phiên bản:** Cấu hình luồng công việc tự động biên dịch mã nguồn (Automated build workflows) để liên tục xuất ra file `.dll` và `.so` hoàn chỉnh.
