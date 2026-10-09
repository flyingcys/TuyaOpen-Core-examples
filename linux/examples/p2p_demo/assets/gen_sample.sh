#!/bin/bash
# 生成 p2p_demo 的内置视频样片 assets/sample.h264。
#
# 样片要求（与 demo 侧配置联动）：
#   - Annex-B H.264（每个 AU 前带 AUD，vcam 按 AUD/VCL 边界切 AU）
#   - 640x480@20fps、GOP=20（每 20 帧一个 IDR，供"请求 I 帧"立刻生效）
#   - baseline profile、无 B 帧（编码器/解码端兼容性最好）
#
# 改分辨率 / 帧率 / GOP / 码率后，必须同步改 Kconfig 的
# P2P_DEMO_WIDTH / HEIGHT / FPS / GOP / KBPS（默认 640/480/20/20/400），
# 否则上报给 App 的 AV 能力与样片实际参数不一致。
#
# 用法：在 assets/ 目录执行 ./gen_sample.sh（需要本机 ffmpeg + libx264）
set -e

cd "$(dirname "$0")"

ffmpeg -y -f lavfi -i testsrc2=size=640x480:rate=20 -t 5 -threads 1 \
  -c:v libx264 -profile:v baseline -level 3.1 -pix_fmt yuv420p \
  -b:v 300k -maxrate 300k -bufsize 600k \
  -x264-params "aud=1:keyint=20:min-keyint=20:scenecut=0:bframes=0:threads=1" \
  -f h264 sample.h264

# 参数说明：
#   testsrc2=size=640x480:rate=20  合成测试图，5 秒 = 100 帧
#   -profile:v baseline -level 3.1 兼容性最好的档次（无 CABAC/B 帧）
#   -b:v/-maxrate/-bufsize 300k    目标码率；testsrc2 熵高，实测约 340kbps
#                                  （5 秒文件约 201KB，< 250KB 上限）
#   aud=1                          每个 AU 前插 AUD（type 9），vcam 的 AU 切分依赖它
#   keyint=20:min-keyint=20:scenecut=0 固定 GOP=20，帧 0/20/40/60/80 为 IDR
#   bframes=0                      无 B 帧，每帧一个 VCL NAL
#   -threads 1 / threads=1         x264 多线程会让码控决策随线程数漂移，
#                                  锁成单线程保证脚本每次跑出逐字节一致的样片

ls -l sample.h264
