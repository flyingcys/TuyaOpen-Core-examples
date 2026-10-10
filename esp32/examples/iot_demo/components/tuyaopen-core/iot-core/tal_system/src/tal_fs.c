/**
 * @file tal_fs.c
 * @brief tal_fs 平台分发：18 接口透传到 port 的 tkl_fs / tkl_dir（tal_fs_ops_platform）
 *
 * iot-core 不持有任何文件系统逻辑——介质、路径映射、句柄形态都在 port
 * （见 tal_fs_ops.h 契约）。本文件只做 ops 卡分发，与 tal_kv.c 的
 * TAL_KV_EXEC_OP / tal_api.c 的 TAL_API_EXEC_OP 同构。
 *
 * 唯一调用方是 p2p-core/local_store：不用 p2p 时无人引用本文件任何符号，
 * 静态库不拉入 tal_fs.o，port 侧的 tal_fs_ops_platform 也就不需要存在
 * （tkl_fs.c 整文件由 ENABLE_TUYA_P2P 门控）。
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 */
#include "tal_fs.h"
#include "tal_fs_ops.h"
#include "tal_log.h"

/* 平台 ops 卡（port 的 tkl_fs.c 定义），core 侧经 getter 分发 */
extern TAL_FS_OPS_T tal_fs_ops_platform;

TAL_FS_OPS_T *tal_fs_get_ops(void)
{
    return &tal_fs_ops_platform;
}

#define TAL_FS_EXEC_OP(op_name, default_ret, ...)                                                                      \
    do {                                                                                                               \
        TAL_FS_OPS_T *ops = tal_fs_get_ops();                                                                          \
        if (NULL != ops && NULL != ops->op_name) {                                                                     \
            return ops->op_name(__VA_ARGS__);                                                                          \
        }                                                                                                              \
        PR_ERR("TAL FS operation %s not available", #op_name);                                                         \
        return default_ret;                                                                                            \
    } while (0)

/* ---------------------------------------------------------------------------
 * 文件
 * --------------------------------------------------------------------------- */
TUYA_FILE tal_fopen(const char *path, const char *mode)
{
    TAL_FS_EXEC_OP(fopen, NULL, path, mode);
}

int tal_fclose(TUYA_FILE file)
{
    TAL_FS_EXEC_OP(fclose, -1, file);
}

int tal_fwrite(void *buf, int bytes, TUYA_FILE file)
{
    TAL_FS_EXEC_OP(fwrite, -1, buf, bytes, file);
}

int tal_fread(void *buf, int bytes, TUYA_FILE file)
{
    TAL_FS_EXEC_OP(fread, -1, buf, bytes, file);
}

int tal_fseek(TUYA_FILE file, int64_t offs, int whence)
{
    TAL_FS_EXEC_OP(fseek, -1, file, offs, whence);
}

int tal_fflush(TUYA_FILE file)
{
    TAL_FS_EXEC_OP(fflush, -1, file);
}

int tal_fsync(int fd)
{
    TAL_FS_EXEC_OP(fsync, -1, fd);
}

int tal_fileno(TUYA_FILE file)
{
    TAL_FS_EXEC_OP(fileno, -1, file);
}

char *tal_fgets(char *buf, int len, TUYA_FILE file)
{
    TAL_FS_EXEC_OP(fgets, NULL, buf, len, file);
}

/* ---------------------------------------------------------------------------
 * 路径
 * --------------------------------------------------------------------------- */
int tal_fs_is_exist(const char *path, BOOL_T *is_exist)
{
    TAL_FS_EXEC_OP(fs_is_exist, -1, path, is_exist);
}

int tal_fs_mkdir(const char *path)
{
    TAL_FS_EXEC_OP(fs_mkdir, -1, path);
}

int tal_fs_remove(const char *path)
{
    TAL_FS_EXEC_OP(fs_remove, -1, path);
}

int tal_fs_rename(const char *path_old, const char *path_new)
{
    TAL_FS_EXEC_OP(fs_rename, -1, path_old, path_new);
}

/* ---------------------------------------------------------------------------
 * 目录
 * --------------------------------------------------------------------------- */
int tal_dir_open(const char *path, TUYA_DIR *dir)
{
    TAL_FS_EXEC_OP(dir_open, -1, path, dir);
}

int tal_dir_close(TUYA_DIR dir)
{
    TAL_FS_EXEC_OP(dir_close, -1, dir);
}

int tal_dir_read(TUYA_DIR dir, TUYA_FILEINFO *info)
{
    TAL_FS_EXEC_OP(dir_read, -1, dir, info);
}

int tal_dir_name(TUYA_FILEINFO info, const char **name)
{
    TAL_FS_EXEC_OP(dir_name, -1, info, name);
}

int tal_dir_is_directory(TUYA_FILEINFO info, BOOL_T *is_dir)
{
    TAL_FS_EXEC_OP(dir_is_directory, -1, info, is_dir);
}
