/**
* @file tkl_fs.h
* @brief Common process - adapter the file operation api provide by OS
* @version 0.1
* @date 2020-11-09
*
* 文件系统是 p2p 可选能力：只保留 local_store 实际用到的 18 个接口
* （9 文件 + 4 路径 + 5 目录），实现见 port/src/tkl_fs.c（整文件由
* ENABLE_TUYA_P2P 门控），经 TAL_FS_OPS_T 卡（tal_fs_ops.h）由 core 的
* tal_fs.h/tal_fs.c 分发。零调用接口（mode/feof/ftell/fgetsize/faccess/
* fgetc/ftruncate/dir_is_regular）已从本头删除。
*
* @copyright Copyright 2021-2030 Tuya Inc. All Rights Reserved.
*
*/
#ifndef __TKL_FS_H__
#define __TKL_FS_H__


#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/********************************************************************************
 *********************************tuya_os_fs_intf********************************
 ********************************************************************************/

/**
* @brief Make directory
*
* @param[in] path: path of directory
*
* @note This API is used for making a directory
*
* @return 0 on success. Others on failed
*/
int tkl_fs_mkdir(const char* path);

/**
* @brief Remove directory
*
* @param[in] path: path of directory
*
* @note This API is used for removing a directory
*
* @return 0 on success. Others on failed
*/
int tkl_fs_remove(const char* path);


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
int tkl_fs_is_exist(const char* path, BOOL_T* is_exist);

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
int tkl_fs_rename(const char* path_old, const char* path_new);

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
int tkl_dir_open(const char* path, TUYA_DIR* dir);

/**
* @brief Close directory
*
* @param[in] dir: handle of directory
*
* @note This API is used to close a directory
*
* @return 0 on success. Others on failed
*/
int tkl_dir_close(TUYA_DIR dir);


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
int tkl_dir_read(TUYA_DIR dir, TUYA_FILEINFO* info);

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
int tkl_dir_name(TUYA_FILEINFO info, const char** name);

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
int tkl_dir_is_directory(TUYA_FILEINFO info, BOOL_T* is_dir);

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
TUYA_FILE tkl_fopen(const char* path, const char* mode);

/**
* @brief Close file
*
* @param[in] file: file handle
*
* @note This API is used to close a file
*
* @return 0 on success. EOF on failed
*/
int tkl_fclose(TUYA_FILE file);

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
int tkl_fread(void* buf, int bytes, TUYA_FILE file);

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
int tkl_fwrite(void* buf, int bytes, TUYA_FILE file);

/**
* @brief write buffer to flash
*
* @param[in] fd: file fd
*
* @note This API is used to write buffer to flash
*
* @return 0 on success. others on failed
*/
int tkl_fsync(int fd);

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
char* tkl_fgets(char* buf, int len, TUYA_FILE file);

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
int tkl_fseek(TUYA_FILE file, int64_t offs, int whence);

/**
* @brief flush the IO read/write stream
*
* @param[in] file char stream
*
* @note This API is used to flush the IO read/write stream.
*
* @return 0 success,-1 failed
*/
int tkl_fflush(TUYA_FILE file);

/**
* @brief get the file fd
*
* @param[in] file char stream
*
* @note This API is used to get the file fd.
*
* @return the file fd
*/
int tkl_fileno(TUYA_FILE file);


#ifdef __cplusplus
} // extern "C"
#endif /* __cplusplus */

#endif // 

