import sys
import struct
import time

def main():
    if len(sys.argv) != 3:
        print("Usage: python3 send_kernel_tty.py <device_path> <kernel_file>")
        print("Example: python3 send_kernel_tty.py /dev/pts/1 kernel.bin")
        sys.exit(1)

    device = sys.argv[1]
    kernel_path = sys.argv[2]

    # 讀取內核二進位檔
    with open(kernel_path, "rb") as f:
        kernel_data = f.read()

    try:
        with open(device, "wb", buffering=0) as f_out:
            print(f"Opened device: {device}")

            # 1. 發送 load 指令
            # print("Sending 'load' command...")
            # f_out.write(b'load\n')
            # time.sleep(0.5)

            # 2. 發送 Header
            # < = little-endian 位元組序（低位元組在前）
            # I = unsigned int，4 bytes（uint32）
            # II = 兩個 unsigned int，共 8 bytes
            header = struct.pack('<II', 0x544F4F42, len(kernel_data))
            print(f"Sending Header (Size: {len(kernel_data)} bytes)...")
            f_out.write(header)
            time.sleep(0.05)   # 等板子印完 "Receiving kernel..."，避免 RX FIFO 溢位

            # 3. 發送數據：chunked + per-chunk delay，讓板子在 timer ISR 期間能 drain FIFO
            print("Sending Kernel Data...")
            CHUNK = 256
            for i in range(0, len(kernel_data), CHUNK):
                f_out.write(kernel_data[i:i+CHUNK])
                time.sleep(0.025)

        print("Transfer complete. Use screen to interact with the new kernel.")

    except PermissionError:
        print(f"Error: Permission denied. Try 'sudo chmod 666 {device}'")
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    main()