/**
 * @file tkl_fs.c
 * @brief 文件系统平台 ops 卡组装 + POSIX 实现（linux port）
 *
 * 文件系统是 p2p 可选能力：唯一调用方是 p2p-core/local_store（core 侧经
 * tal_fs.h 的透传落到本文件的 TAL_FS_OPS_T 卡）。不用 p2p 的平台整文件由
 * ENABLE_TUYA_P2P 门控——不编译、不必实现这 18 个接口；宏由 port 的 CMake
 * 从 CONFIG_ENABLE_TUYA_P2P 映射注入（见 port/CMakeLists.txt）。
 *
 * 实现为 POSIX 直包装（open/read/write/opendir/readdir...），经 TAL_FS_OPS_T
 * 分发；core 的 tal_fs.c 无 p2p 时无人引用其符号，静态库不拉入 tal_fs.o，
 * 卡不存在也不会链接失败。
 *
 * @copyright Copyright 2020-2021 Tuya Inc. All Rights Reserved.
 *
 */
#if defined(ENABLE_TUYA_P2P)

#include "tkl_fs.h"
#include "tal_fs_ops.h"
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <stdio.h>

/**
* @brief Make directory
*
* @param[in] path: path of directory
*
* @note This API is used for making a directory
*
* @return 0 on success. Others on failed
*/
TUYA_WEAK_ATTRIBUTE int tkl_fs_mkdir(const char* path)
{
    return mkdir(path, 0755);
}

/**
* @brief Remove directory
*
* @param[in] path: path of directory
*
* @note This API is used for removing a directory
*
* @return 0 on success. Others on failed
*/
TUYA_WEAK_ATTRIBUTE int tkl_fs_remove(const char* path)
{
    return remove(path);
}


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
TUYA_WEAK_ATTRIBUTE int tkl_fs_is_exist(const char* path, BOOL_T* is_exist)
{
    if (is_exist == NULL) {
        return -1;
    }

    int ret = access(path, F_OK);
    *is_exist = (ret == 0);
    return 0;
}

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
TUYA_WEAK_ATTRIBUTE int tkl_fs_rename(const char* path_old, const char* path_new)
{
    return rename(path_old, path_new);
}


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
TUYA_WEAK_ATTRIBUTE int tkl_dir_open(const char* path, TUYA_DIR* dir)
{
    if (dir == NULL) {
        return -1;
    }

    DIR* d = opendir(path);
    if (d == NULL) {
        return -1;
    }

    *dir = d;
    return 0;
}

/**
* @brief Close directory
*
* @param[in] dir: handle of directory
*
* @note This API is used to close a directory
*
* @return 0 on success. Others on failed
*/
TUYA_WEAK_ATTRIBUTE int tkl_dir_close(TUYA_DIR dir)
{
    DIR* dirp = (DIR*)dir;
    return closedir(dirp);
}

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
TUYA_WEAK_ATTRIBUTE int tkl_dir_read(TUYA_DIR dir, TUYA_FILEINFO* info)
{
    if (info == NULL) {
        return -1;
    }

    DIR* dirp = (DIR*)dir;
    struct dirent* dp = readdir(dirp);
    if (dp == NULL) {
        return -1;
    }

    *info = dp;
    return 0;
}

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
TUYA_WEAK_ATTRIBUTE int tkl_dir_name(TUYA_FILEINFO info, const char** name)
{
    if (name == NULL) {
        return -1;
    }

    struct dirent* dp = (struct dirent*)info;
    if (dp == NULL) {
        return -1;
    }

    *name = dp->d_name;
    return 0;
}

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
TUYA_WEAK_ATTRIBUTE int tkl_dir_is_directory(TUYA_FILEINFO info, BOOL_T* is_dir)
{
    if (is_dir == NULL) {
        return -1;
    }

    struct dirent* dp = (struct dirent*)info;
    *is_dir = (dp->d_type == DT_DIR);
    return 0;
}


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
TUYA_WEAK_ATTRIBUTE TUYA_FILE tkl_fopen(const char* path, const char* mode)
{
    return fopen(path, mode);
}

/**
* @brief Close file
*
* @param[in] file: file handle
*
* @note This API is used to close a file
*
* @return 0 on success. EOF on failed
*/
TUYA_WEAK_ATTRIBUTE int tkl_fclose(TUYA_FILE file)
{
    FILE* fp = (FILE*)file;
    return fclose(fp);
}

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
TUYA_WEAK_ATTRIBUTE int tkl_fread(void* buf, int bytes, TUYA_FILE file)
{
    FILE* fp = (FILE*)file;
    return fread(buf, 1, bytes, fp);
}

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
TUYA_WEAK_ATTRIBUTE int tkl_fwrite(void* buf, int bytes, TUYA_FILE file)
{
    FILE* fp = (FILE*)file;
    return fwrite(buf, 1, bytes, fp);
}

/**
* @brief write buffer to flash
*
* @param[in] fd: file fd
*
* @note This API is used to write buffer to flash
*
* @return 0 on success. others on failed
*/
TUYA_WEAK_ATTRIBUTE int tkl_fsync(int fd)
{
    return fsync(fd);
}

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
TUYA_WEAK_ATTRIBUTE char* tkl_fgets(char* buf, int len, TUYA_FILE file)
{
    FILE* fp = (FILE*)file;
    return fgets(buf, len, fp);
}


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
TUYA_WEAK_ATTRIBUTE int tkl_fseek(TUYA_FILE file, int64_t offs, int whence)
{
    FILE* fp = (FILE*)file;
    return fseek(fp, offs, whence);
}





/**
* @brief flush the IO read/write stream
*
* @param[in] file char stream
*
* @note This API is used to flush the IO read/write stream.
*
* @return 0 success,-1 failed
*/
TUYA_WEAK_ATTRIBUTE int tkl_fflush(TUYA_FILE file)
{
    FILE* fp = (FILE*)file;
    return fflush(fp);
}

/**
* @brief get the file fd
*
* @param[in] file char stream
*
* @note This API is used to get the file fd.
*
* @return the file fd
*/
TUYA_WEAK_ATTRIBUTE int tkl_fileno(TUYA_FILE file)
{
    FILE* fp = (FILE*)file;
    return fileno(fp);
}


/* ---------------------------------------------------------------------------
 * 文件系统平台 ops 卡：由本文件组装，core 的 tal_fs.c 经 extern 引用并完成
 * tal_fs_* / tal_dir_* 分发。文件系统为 p2p 可选能力（唯一调用方
 * p2p-core/local_store），与主卡（tkl_runtime.c）解耦；整文件随
 * ENABLE_TUYA_P2P 门控，不用 p2p 的平台无需适配。
 * --------------------------------------------------------------------------- */
TAL_FS_OPS_T tal_fs_ops_platform = {
    .fopen = tkl_fopen,
    .fclose = tkl_fclose,
    .fwrite = tkl_fwrite,
    .fread = tkl_fread,
    .fseek = tkl_fseek,
    .fflush = tkl_fflush,
    .fsync = tkl_fsync,
    .fileno = tkl_fileno,
    .fgets = tkl_fgets,
    .fs_is_exist = tkl_fs_is_exist,
    .fs_mkdir = tkl_fs_mkdir,
    .fs_remove = tkl_fs_remove,
    .fs_rename = tkl_fs_rename,
    .dir_open = tkl_dir_open,
    .dir_close = tkl_dir_close,
    .dir_read = tkl_dir_read,
    .dir_name = tkl_dir_name,
    .dir_is_directory = tkl_dir_is_directory,
};

#endif /* ENABLE_TUYA_P2P */

