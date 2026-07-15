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
  * **[x] Mở nhiều quầy:** Thiết lập Server nhận nhiều máy ảo kết nối cùng lúc, mỗi máy ảo chạy một luồng độc lập.
  * **[x] Chia chác GPU:** Tự động điều phối linh hoạt (Ví dụ: Khách A đẩy vào card RTX 3050, khách B đẩy vào eGPU rời).
  * **[x] Sửa lỗi rò rỉ bộ nhớ Session:** Gán `running_ = false` ở cuối hàm `Session::handle_client()` tại `session.cpp` để `cleanup_stopped_sessions()` nhận diện và giải phóng tài nguyên.
  * **[x] Bảo đảm an toàn đa luồng cho GpuManager:** Thêm `std::mutex` và lock bảo vệ khi đọc/ghi danh sách `gpus_` và `sessionCount` trong `GpuManager` giữa luồng chính và luồng Session phụ.

* **Dev B: Tự động hóa & Đóng gói (Caching & CI/CD)**
  * **[x] Bộ nhớ đệm (Caching):** 
    * [x] Đọc/ghi và lưu GPU capabilities ra file JSON dưới client.
    * [x] Tích hợp dữ liệu cached capabilities này vào các hàm hook Vulkan thích hợp (như `vkGetPhysicalDeviceProperties`...) để trả về thông số cho app khách mà không cần gửi tin hỏi lại host.
  * **[x] Sửa lỗi đóng gói & CI/CD:**
    * [x] Sửa `deploy.yml`: Không upload trực tiếp file `.zip` của Windows ở job Linux runner khi chưa download artifact từ job trước đó.
    * [x] Sửa `CMakePresets.json` & các workflow: Định nghĩa đầy đủ `testPresets` cho `debug`, `release`, `linux` và gọi đúng `--preset` trong các lệnh `ctest`.

---

### Giai đoạn 5: Giao diện điều khiển & CLI (CLI, Configuration & Control)

* **Dev A: CLI & Trình cấu hình Host (Host Control CLI)**
  * **[x] Trình quản lý CLI:** Viết CLI tool trên Host để kiểm tra trạng thái các GPU, giám sát FPS nén, xem danh sách Session đang kết nối, và ép ngắt kết nối Session.
  * **[x] Tùy biến cấu hình (Config Manager):** Hỗ trợ nạp cấu hình từ file YAML/JSON (giới hạn FPS tối đa, chất lượng nén ảnh JPEG, port mạng, bật/tắt Multi-GPU).

* **Dev B: Cấu hình Guest & CLI (Guest CLI & Config)**
  * **[x] Tự động cấu hình Linux:** Viết script Bash để tự động xuất và thiết lập các biến môi trường cần thiết (`VK_ICD_FILENAMES`, `LD_LIBRARY_PATH`, `LD_PRELOAD`) giúp game/ứng dụng chạy thẳng qua OmniGPU.
  * **[x] Bật/Tắt các Layer dịch:** Cho phép cấu hình qua file cấu hình `omnigpu_guest.json` để linh hoạt bật/tắt Zink hoặc clvk tùy theo nhu cầu đồ họa/tính toán.

---

### Giai đoạn 6: Trình khởi chạy (Launcher) & Tối ưu hóa hiệu năng (Launcher & Performance Optimization)

* **Dev A: Viết Tool Launcher siêu tốc (Windows/Linux Launcher CLI/GUI)**
  * **[x] Trình khởi chạy an toàn (Windows):** Xây dựng một công cụ nhỏ (GUI/CLI), cho phép chọn file `.exe` của game/ứng dụng. Thay vì tiêm DLL nguy hiểm (dễ bị Anti-cheat và Antivirus chặn), tool sẽ tự động sao chép các file dịch `opengl32.dll` (Zink) và `OpenCL.dll` (clvk) vào thư mục game, thiết lập biến môi trường `VK_ICD_FILENAMES` tạm thời trỏ đến `vk_icd.json` của OmniGPU, rồi khởi chạy game một cách chính thống và an toàn.

* **Dev B: Tích hợp sâu Zink & clvk & Tối ưu luồng dữ liệu**
  * **[x] Nâng cấp & Tích hợp sâu:** Cấu hình tự động tải phiên bản mới nhất của Mesa Zink và clvk. Tối ưu hóa hiệu năng nén ảnh và cơ chế gộp lệnh Vulkan (Command Batching) để tăng FPS.
  * **[x] Kiểm thử tích hợp tự động:** Bổ sung các kịch bản kiểm thử tích hợp (integration tests) cho luồng dịch chuyển đổi API đồ họa OpenGL/OpenCL -> Vulkan -> Host GPU.

---

### Yêu cầu cốt lõi chung của Hệ thống (Đã sửa đổi & Hoàn thành)

* **[x] Cài đặt cơ chế Hook thực tế (Đã hoàn thành):**
  * Điền code thực tế vào hàm `initialize_hooks()` trong `vk_intercept_gen.cpp` để nạp địa chỉ hàm gốc (từ Vulkan loader thật của hệ thống) vào map `g_original_fns` nhằm tránh crash khi gọi hàm gốc qua con trỏ `nullptr`.
* **[x] Export Vulkan ICD Entrypoints (Đã hoàn thành):**
  * Cấu hình dự án để DLL/SO `omnigpu_guest` export ký hiệu chuẩn `vk_icdGetInstanceProcAddr` / `vkGetInstanceProcAddr` để Vulkan Loader nhận diện driver khách.
* **[x] Chuyển đổi khởi tạo từ main() sang DllMain / Constructor (Đã hoàn thành):**
  * Di chuyển toàn bộ logic khởi tạo (kết nối Socket, handshake, khởi tạo hook) ra khỏi hàm `main()` của `main.cpp`. Cài đặt cơ chế tự khởi chạy khi DLL được nạp (`DllMain` trên Windows và `__attribute__((constructor))` trên Linux). Hiện tại do `omnigpu_guest` build dạng `SHARED` library nên hàm `main()` này hoàn toàn không được chạy, dẫn đến toàn bộ Driver khách bị tê liệt.

---

### Giai đoạn 7: Tối ưu băng thông mạng & Nén động (Network Bandwidth Optimization & Dynamic Compression)

* **Dev A: Cơ chế nén hình ảnh động (Adaptive Image Compression)**
  * **[x] Nén động:** Tự động phát hiện băng thông mạng và độ trễ ping thời gian thực để chuyển đổi giữa nén không hao hụt (LZ4) đối với giao diện tĩnh và nén hao hụt chất lượng cao (WebP/AV1/JPEG) đối với các khung cảnh chuyển động nhanh.
* **Dev B: Gộp lệnh thích ứng (Adaptive Command Batching)**
  * **[x] Điều khiển kích thước batch:** Tự động điều chỉnh kích thước gói dữ liệu (byte threshold) và thời gian flush tối đa dựa trên hiệu năng mạng để giảm giật lag.

---

### Giai đoạn 8: Hỗ trợ Multi-GPU & Quản lý RAM ảo trên Host (Multi-GPU & Virtual VRAM)

* **Dev A: Cân bằng tải Multi-GPU (Multi-GPU Load Balancing)**
  * **[x] Cân bằng tải:** Hỗ trợ Host phân phối tài nguyên hiển thị của các Session sang các card đồ họa ít tải nhất, hoặc ghép song song GPU vật lý trên Host để phục vụ một client đồ họa nặng.
* **Dev B: Quản lý bộ nhớ đệm VRAM ảo (Virtual VRAM & Cache Optimization)**
  * **[x] Tối ưu hóa bộ đệm tài nguyên:** Tối ưu hóa việc lưu trữ tạm thời (cache) các đối tượng lớn như Vertex Buffer, Index Buffer, Texture trực tiếp trong VRAM Host để giảm thiểu tối đa việc truyền tải lặp lại dữ liệu tĩnh qua mạng.

---

### Giai đoạn 9: Đóng gói và Trình cài đặt tự động (Automated Packaging & Deployment Installer)

* **Dev A: Trình cài đặt Host (Windows MSI Installer / Linux Package)**
  * **[x] Trình cài đặt tự động:** Tạo file cài đặt Host dưới dạng dịch vụ chạy ngầm của hệ điều hành (Windows Service hoặc Linux systemd service), tự động quản lý khởi chạy khi khởi động máy.
* **Dev B: Trình tích hợp Guest và Tự động cấu hình**
  * **[x] Cấu hình hệ thống khách:** Đóng gói thư viện khách và tự động đăng ký đường dẫn driver ICD (`vk_icd.json`) vào các vị trí chuẩn của hệ thống (Registry đối với Windows, `/usr/share/vulkan/icd.d/` đối với Linux) để game tự động chạy qua mạng không cần copy thủ công.

---

### Giai đoạn 10: Mã hóa luồng Video nén phần cứng (Hardware-accelerated Video Encoding / NVENC / AMF)

* **Dev A: Tích hợp mã hóa phần cứng (Hardware Encoder Integration)**
  * **[x] Tích hợp NVENC / AMF / VA-API:** Thay vì gửi các frame ảnh nén riêng lẻ, Host sẽ sử dụng bộ giải mã phần cứng trên GPU để nén luồng khung hình thành video định dạng H.264/HEVC/AV1 thời gian thực.
* **Dev B: Giải mã Video phía Client (Hardware Decoder Client)**
  * **[x] Giải mã phần cứng khách:** Guest DLL sẽ nhận luồng video, giải mã bằng phần cứng trên thiết bị khách (nếu hỗ trợ) hoặc đa luồng CPU để tối ưu hóa băng thông mạng và độ trễ phản hồi tối đa.

