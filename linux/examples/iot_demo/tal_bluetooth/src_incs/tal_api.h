/* tal_api.h（core 伞形头）的 host 精简替身，仅服务本 target。
 *
 * core 的 tal_api.h 是模块内部聚合头：include tal_kv.h（tal_kv，牵 lfs.h）、
 * tal_security.h（tuya_transport/tls，牵 tal_x509.h 等一串）与十余个兄弟头，
 * 这些路径经 tal_system/tuya_transport/tal_kv 的 target 依赖在 core 内部
 * 传播，模块外消费方不可达。而 tal_bluetooth.c 对该聚合头的实际使用
 * 只有 tal_malloc/tal_free（tal_memory.h 声明），故这里只转发必需头。
 *
 * 生效方式：本目录经 target_include_directories(tal_bluetooth PRIVATE ...)
 * 置于 core 头目录之前，仅 shadow tal_bluetooth.c 这一个编译单元的
 * "tal_api.h" 查找；core 内部与其余编译单元不受影响。
 */
#ifndef __TAL_API_HOST_SHIM_H__
#define __TAL_API_HOST_SHIM_H__

#include "tuya_cloud_types.h"
#include "tal_memory.h"

#endif /* __TAL_API_HOST_SHIM_H__ */
