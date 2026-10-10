/**
 * @file tal_fs_ops.h
 * @brief tal_fs 的平台 ops 契约：文件系统 18 接口（9 文件 + 4 路径 + 5 目录）
 *
 * 定义 TAL_FS_OPS_T：port 在 tkl_fs.c 组装 tal_fs_ops_platform（成员绑定本平台
 * tkl_fopen / tkl_fs_* / tkl_dir_* 实现），core 的 tal_fs.c 经 getter 完成
 * tal_fs_* 分发，对上签名不变。
 *
 * 与主卡 TAL_API_OPS_T（tal_api_ops.h）解耦：文件系统是 p2p 可选能力
 * （唯一调用方 p2p-core/local_store），不用 p2p 的平台整文件
 * port/src/tkl_fs.c 由 ENABLE_TUYA_P2P 门控，不必适配这套接口。
 *
 * 契约只收编 local_store 实际用到的 18 个接口；其余（tkl_fs_mode / tkl_feof /
 * tkl_ftell / tkl_fgetsize / tkl_faccess / tkl_fgetc / tkl_ftruncate /
 * tkl_dir_is_regular）零调用，已从 port 头 tkl_fs.h 删除，实现暂留
 * port/src/tkl_fs.c（未挂卡，待清理）。
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */
#ifndef __TAL_FS_OPS_H__
#define __TAL_FS_OPS_H__

#include "tuya_cloud_types.h"

#include "tal_fs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief tal_fs 平台 ops：成员与 port 的 tkl_fs / tkl_dir 实现一一对应
 *
 * @note 句柄类型沿用 port 定义（TUYA_FILE / TUYA_DIR / TUYA_FILEINFO 均为
 *       void* 别名），可直接绑定 port 的 tkl_* 实现；返回值语义与 tal_fs.h
 *       对应接口逐条一致（句柄类 NULL 失败；读写类返回字节数；tal_fileno
 *       返回 fd；其余 0 成功、非 0 失败）
 */
typedef struct {
    //! 文件
    /* 打开文件；见 tal_fs.h 的 tal_fopen */
    TUYA_FILE (*fopen)(const char *path, const char *mode);
    /* 关闭文件；见 tal_fs.h 的 tal_fclose */
    int (*fclose)(TUYA_FILE file);
    /* 写文件；见 tal_fs.h 的 tal_fwrite */
    int (*fwrite)(void *buf, int bytes, TUYA_FILE file);
    /* 读文件；见 tal_fs.h 的 tal_fread */
    int (*fread)(void *buf, int bytes, TUYA_FILE file);
    /* 定位文件读写位置；见 tal_fs.h 的 tal_fseek */
    int (*fseek)(TUYA_FILE file, int64_t offs, int whence);
    /* 冲刷文件流缓冲；见 tal_fs.h 的 tal_fflush */
    int (*fflush)(TUYA_FILE file);
    /* 落盘（按 fd）；见 tal_fs.h 的 tal_fsync */
    int (*fsync)(int fd);
    /* 取文件 fd；见 tal_fs.h 的 tal_fileno */
    int (*fileno)(TUYA_FILE file);
    /* 读一行；见 tal_fs.h 的 tal_fgets */
    char *(*fgets)(char *buf, int len, TUYA_FILE file);
    //! 路径
    /* 判断文件/目录是否存在；见 tal_fs.h 的 tal_fs_is_exist */
    int (*fs_is_exist)(const char *path, BOOL_T *is_exist);
    /* 建目录；见 tal_fs.h 的 tal_fs_mkdir */
    int (*fs_mkdir)(const char *path);
    /* 删除文件/目录；见 tal_fs.h 的 tal_fs_remove */
    int (*fs_remove)(const char *path);
    /* 重命名；见 tal_fs.h 的 tal_fs_rename */
    int (*fs_rename)(const char *path_old, const char *path_new);
    //! 目录
    /* 打开目录；见 tal_fs.h 的 tal_dir_open */
    int (*dir_open)(const char *path, TUYA_DIR *dir);
    /* 关闭目录；见 tal_fs.h 的 tal_dir_close */
    int (*dir_close)(TUYA_DIR dir);
    /* 读目录项（内部指针后移）；见 tal_fs.h 的 tal_dir_read */
    int (*dir_read)(TUYA_DIR dir, TUYA_FILEINFO *info);
    /* 取目录项名字；见 tal_fs.h 的 tal_dir_name */
    int (*dir_name)(TUYA_FILEINFO info, const char **name);
    /* 判断目录项是否目录；见 tal_fs.h 的 tal_dir_is_directory */
    int (*dir_is_directory)(TUYA_FILEINFO info, BOOL_T *is_dir);
} TAL_FS_OPS_T;

/**
 * @brief Get the platform ops of tal_fs
 *
 * @return pointer to the platform ops, never NULL (defined in the port)
 */
TAL_FS_OPS_T *tal_fs_get_ops(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
