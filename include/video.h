#ifndef VIDEO_H
#define VIDEO_H

#define FB_WIDTH  1920
#define FB_HEIGHT 1080
#define FB_BPP    4               /* XRGB8888 = 4 byte/pixel */

// QEMU ramfb：透過 fw_cfg 把自選的 FB_BASE 註冊成 ramfb 表面。
// 必須在 QEMU 啟動時加 -device ramfb 才有效；OrangePi RV2 板上由 U-Boot 預設好，
// 那邊改用另一套（commit #12 處理），目前 build 只驅動 QEMU 路徑。
void video_init(void);

// 把 width*height 的 XRGB8888 影像置中拷貝到 framebuffer，
// 只 flush 對應行的 D-cache 範圍（整片 8MB flush 太貴）。
void video_bmp_display(unsigned int* bmp_image, int width, int height);

#endif /* VIDEO_H */
