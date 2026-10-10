/**
 * @file tal_fs.h
 * @brief TAL 文件系统接口：文件 9 + 路径 4 + 目录 5 共 18 接口
 *
 * 只定义文件操作语义，不涉及实现：介质、路径映射、句柄形态全部由平台 port
 * 的 tkl_fs / tkl_dir 决定（Linux = POSIX 包装；RTOS 平台 = SD/flash 上的
 * FAT/ littlefs），iot-core 不持有任何文件系统逻辑，仅经 tal_fs_ops.h 的
 * ops 卡透传（见 port/src/tkl_fs.c 的 tal_fs_ops_platform）。
 *
 * 当前唯一调用方是 p2p-core/local_store（本地录像分段存储）：不用 p2p 的平台
 * 无需实现这套适配，port 侧 tkl_fs.c 整文件由 ENABLE_TUYA_P2P 门控。
 *
 * 返回值约定（与 port 的 tkl_fs 逐条一致，详见各接口注释）：
 *   - 句柄类：tal_fopen 返回文件句柄，NULL 失败；tal_fgets 返回 buf，
 *     NULL 表示失败或已到文件尾
 *   - 读写类：tal_fread / tal_fwrite 返回实际读/写字节数，<= 0 视为失败或 EOF
 *   - tal_fileno 返回 fd，-1 表示失败
 *   - 其余（close / seek / flush / fsync / 路径 / 目录）：0 成功，非 0 失败
 * 句柄类型 TUYA_FILE / TUYA_DIR / TUYA_FILEINFO 由 port 头 tkl_fs.h 提供。
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */
#ifndef __TAL_FS_H__
#define __TAL_FS_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/********************************************************************************
 *********************************** file ***************************************
 ********************************************************************************/

/**
* @brief Open file
*
* @param[in] path: path of file
* @param[in] mode: file open mode: "r","w"...
*
* @note This API is used to open a file
*
* @return the file handle, NULL means failed
*/
TUYA_FILE tal_fopen(const char *path, const char *mode);

/**
* @brief Close file
*
* @param[in] file: file handle
*
* @note This API is used to close a file
*
* @return 0 on success. EOF on failed
*/
int tal_fclose(TUYA_FILE file);

/**
* @brief write file
*
* @param[in] buf: buffer for writing file
* @param[in] bytes: buffer size
* @param[in] file: file handle
*
* @note This API is used to write a file
*
* @return the bytes write to file
*/
int tal_fwrite(void *buf, int bytes, TUYA_FILE file);

/**
* @brief Read file
*
* @param[in] buf: buffer for reading file
* @param[in] bytes: buffer size
* @param[in] file: file handle
*
* @note This API is used to read a file
*
* @return the bytes read from file
*/
int tal_fread(void *buf, int bytes, TUYA_FILE file);

/**
* @brief Seek to the offset position of the file
*
* @param[in] file: file handle
* @param[in] offs: offset
* @param[in] whence: seek start point mode
*
* @note This API is used to seek to the offset position of the file.
*
* @return 0 on success, others on failed
*/
int tal_fseek(TUYA_FILE file, int64_t offs, int whence);

/**
* @brief flush the IO read/write stream
*
* @param[in] file char stream
*
* @note This API is used to flush the IO read/write stream.
*
* @return 0 success,-1 failed
*/
int tal_fflush(TUYA_FILE file);

/**
* @brief write buffer to flash
*
* @param[in] fd: file fd
*
* @note This API is used to write buffer to flash
* @note 参数是 fd（用 tal_fileno 从文件句柄取），与 tkl_fsync 对齐
*
* @return 0 on success. others on failed
*/
int tal_fsync(int fd);

/**
* @brief get the file fd
*
* @param[in] file char stream
*
* @note This API is used to get the file fd.
*
* @return the file fd
*/
int tal_fileno(TUYA_FILE file);

/**
* @brief Read string from file
*
* @param[in] buf: buffer for reading file
* @param[in] len: buffer size
* @param[in] file: file handle
*
* @note This API is used to read string from file
*
* @return the content get from file, NULL means failed
*/
char *tal_fgets(char *buf, int len, TUYA_FILE file);

/********************************************************************************
 ************************************ path **************************************
 ********************************************************************************/

/**
* @brief Check whether the file or directory exists
*
* @param[in] path: path of directory
* @param[out] is_exist: the file or directory exists or not
*
* @note This API is used to check whether the file or directory exists.
*
* @return 0 on success. Others on failed
*/
int tal_fs_is_exist(const char *path, BOOL_T *is_exist);

/**
* @brief Make directory
*
* @param[in] path: path of directory
*
* @note This API is used for making a directory
*
* @return 0 on success. Others on failed
*/
int tal_fs_mkdir(const char *path);

/**
* @brief Remove file or directory
*
* @param[in] path: path of directory
*
* @note This API is used for removing a file or directory
*
* @return 0 on success. Others on failed
*/
int tal_fs_remove(const char *path);

/**
* @brief File rename
*
* @param[in] path_old: old path of directory
* @param[in] path_new: new path of directory
*
* @note This API is used to rename the file.
*
* @return 0 on success. Others on failed
*/
int tal_fs_rename(const char *path_old, const char *path_new);

/********************************************************************************
 ********************************** directory ***********************************
 ********************************************************************************/

/**
* @brief Open directory
*
* @param[in] path: path of directory
* @param[out] dir: handle of directory
*
* @note This API is used to open a directory
*
* @return 0 on success. Others on failed
*/
int tal_dir_open(const char *path, TUYA_DIR *dir);

/**
* @brief Close directory
*
* @param[in] dir: handle of directory
*
* @note This API is used to close a directory
*
* @return 0 on success. Others on failed
*/
int tal_dir_close(TUYA_DIR dir);

/**
* @brief Read directory
*
* @param[in] dir: handle of directory
* @param[out] info: file information
*
* @note This API is used to read a directory.
* Read the file information of the current node, and the internal pointer points to the next node.
*
* @return 0 on success. Others on failed
*/
int tal_dir_read(TUYA_DIR dir, TUYA_FILEINFO *info);

/**
* @brief Get the name of the file node
*
* @param[in] info: file information
* @param[out] name: file name
*
* @note This API is used to get the name of the file node.
*
* @return 0 on success. Others on failed
*/
int tal_dir_name(TUYA_FILEINFO info, const char **name);

/**
* @brief Check whether the node is a directory
*
* @param[in] info: file information
* @param[out] is_dir: is directory or not
*
* @note This API is used to check whether the node is a directory.
*
* @return 0 on success. Others on failed
*/
int tal_dir_is_directory(TUYA_FILEINFO info, BOOL_T *is_dir);

#ifdef __cplusplus
} // extern "C"
#endif /* __cplusplus */

#endif
