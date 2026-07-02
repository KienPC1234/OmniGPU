**Chiến lược cốt lõi:** Tưởng tượng dự án OmniGPU là việc mở một **nhà hàng giao thức ăn siêu tốc**.

* **Dev A (Người Vận Chuyển):** Xây đường cao tốc (Mạng TCP), quản lý nhà kho (Bộ nhớ Host), và hút chân không đồ ăn (Nén ảnh).
* **Dev B (Người Ghi Order):** Dịch menu món ăn (Mesa Zink), ghi chú chính xác yêu cầu (Chặn hàm API), và đóng hộp vừa khít (FlatBuffers).

---

### Giai đoạn 1: Nền móng mạng (Xây đường cao tốc & Hộp đựng)

* **Dev A: Xây đường cao tốc (TCP Network)**
* **Viết Server/Client bằng C++:** Dùng thư viện chuẩn (như Asio) để kết nối 2 máy.
* **Bật `TCP_NODELAY`:** Chỉnh code để bỏ đèn đỏ (Nagle's Algorithm), xe cứ có hàng là chạy ngay.
* **Đo tốc độ:** Gửi thử một mảng chữ "Hello" và đo xem mất mấy mili-giây.


* **Dev B: Làm hộp đựng (Data Serialization)**
* **Tích hợp FlatBuffers:** Cài đặt thư viện để biến dữ liệu C++ thành mảng byte thô.
* **Thiết kế khuôn hộp:** Viết file `.fbs` định nghĩa cấu trúc gói tin (VD: gói tin gồm số nguyên ID lệnh và mảng data).
* **Test đóng gói:** Đóng gói chữ "Hello" nhét vào hộp FlatBuffers đưa cho Dev A gửi.



### Giai đoạn 2: Tính toán OpenCL (Giao gạo chưa nấu - Chạy tính toán thô)

* **Dev B: Máy ghi bill tự động (API Interception)**
* **Dùng AI viết script:** Tạo script Python tự động đọc file `cl2.hpp` để sinh ra các hàm C++ chặn lệnh OpenCL (thay vì gõ tay hàng trăm hàm).
* **Đóng gói con trỏ (Pointers):** Biến địa chỉ RAM ảo của máy ảo thành mảng byte thực tế gửi đi.


* **Dev A: Quản lý kho bếp (Host Memory Management)**
* **Nhận lệnh và gọi GPU thật:** Server nhận mảng byte từ Dev B, gọi hàm OpenCL thật trên card Host để xử lý.
* **Trả kết quả:** Tính toán xong, gửi ngược kết quả (như mảng số liệu) về lại qua TCP.



### Giai đoạn 3: Đồ họa Vulkan & Mesa Zink (Nấu ăn & Giao tận nhà)

* **Dev B: Dịch menu và Gom đơn (Zink & Batching)**
* **Nhúng Mesa Zink:** Đặt file `opengl32.dll` (của Zink) vào Client để nó tự dịch OpenGL thành Vulkan.
* **Chặn lệnh Vulkan:** Viết script sinh code tương tự Giai đoạn 2 để chặn các hàm Vulkan.
* **Gom lệnh (Batching):** Thay vì gửi từng lệnh nhỏ, gom 10-20 lệnh vẽ vào chung 1 hộp FlatBuffers rồi mới gửi (Giống như gom 10 cốc trà sữa giao 1 chuyến).


* **Dev A: Hút chân không ảnh (Image Compression)**
* **Render ẩn (Headless Render):** Ép GPU Host vẽ hình ảnh vào một bộ nhớ ẩn (Buffer) thay vì vẽ ra màn hình.
* **Nén ảnh siêu tốc:** Tích hợp **LZ4** hoặc **TurboJPEG** để bóp dung lượng bức ảnh vừa vẽ từ 8MB xuống 1MB.
* **Giao khung hình (Framebuffer):** Bắn bức ảnh đã nén về lại Client để hiển thị.



### Giai đoạn 4: Mở rộng quán (Tối ưu & Đóng gói)

* **Dev A: Xử lý Đa luồng (Multi-threading)**
* **Phân luồng:** Viết code để Server nhận được nhiều Client cùng lúc (Mỗi khách 1 luồng riêng).
* **Chia GPU:** Code tự động gán Client 1 vào GPU 1, Client 2 vào GPU 2 (nếu máy có 2 card).


* **Dev B: Đóng hộp sản phẩm (Release Build)**
* **Viết file CMake:** Cấu hình để code tự động biên dịch ra file `.dll` cho Windows và `.so` cho Linux.
* **Tạo bộ nhớ đệm (Caching):** Code cho Client tự nhớ các thông số cấu hình GPU (Giới hạn RAM, chuẩn màu) ngay lần hỏi đầu tiên, lần sau tự lấy ra dùng không hỏi qua mạng nữa.



Bảng phân công này bẻ nhỏ dự án khổng lồ thành việc lắp ráp các đường ống song song: người lo vận chuyển dữ liệu, người lo dịch thuật và đóng gói đồ họa.
