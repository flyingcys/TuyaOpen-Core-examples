/**
 * @file fs_card.c
 * @brief 文件系统平台 ops 卡组装 + POSIX 实现（2026-10-13 自平台 port 下沉）
 *
 * @note 本文件在 p2p_demo 与 agent_demo 各有一份副本（内容须逐字节同步）：
 *       - p2p_demo/src/adapt/fs_card.c：服务 local_store（P2P 录像存储）；
 *       - agent_demo/src/adapt/fs_card.c：备用——agent_demo 的 DATASINK 已收窄为
 *         MEM|URL（见其 CMakeLists.txt），当前不链接 FS；恢复 FILE 源时
 *         删掉收窄注入即可（卡已就位）。
 *
 * 文件系统是可选能力，调用方两类：
 *   - p2p-core/local_store（本地录像分段存储）；
 *   - audio_player 的文件源（datasink_file.c → tal_fopen，AI 播放器的
 *     AI_PLAYER_SRC_FILE 路径；agent_demo 当前未启用该源，见上）。
 * core 的 tal_fs.c 无条件编入 tal_system 静态库，引用本文件的
 * tal_fs_ops_platform；但只有上述调用方存在时 tal_fs.o 才会被拉进链接
 * （静态库按需拉入）。故本文件整文件由 ENABLE_TUYA_P2P || ENABLE_AI_PLAYER
 * 门控——两者全关的平台不编译、不必实现这 18 个接口；两个宏由 core 的
 * tuyaopen-core.cmake 经 tuyaopen-headers 注入（宿主与应用共享），
 * 消费者（p2p_demo / agent_demo）各自把本文件编进目标。
 *
 * 实现为 POSIX 直包装（open/read/write/opendir/readdir...），经 TAL_FS_OPS_T
 * 分发；两者全关时 core 的 tal_fs.c 无人引用其符号，静态库不拉入 tal_fs.o，
 * 卡不存在也不会链接失败。
 *
 * @copyright Copyright 2020-2021 Tuya Inc. All Rights Reserved.
 *
 */
#if defined(ENABLE_TUYA_P2P) || defined(ENABLE_AI_PLAYER)

#include "tal_fs_ops.h"
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <stdio.h>

/* ---------------------------------------------------------------------------
 * 原 tkl_fs.h 内联区（2026-10 去 tkl 化：该头只有本文件一个 includer，已删除）
 * 类型与接口声明原样保留，实现见本文件下方
 * --------------------------------------------------------------------------- */
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
int fs_card_mkdir(const char* path);

/**
* @brief Remove directory
*
* @param[in] path: path of directory
*
* @note This API is used for removing a directory
*
* @return 0 on success. Others on failed
*/
int fs_card_remove(const char* path);


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
int fs_card_is_exist(const char* path, BOOL_T* is_exist);

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
int fs_card_rename(const char* path_old, const char* path_new);

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
int fs_card_dir_open(const char* path, TUYA_DIR* dir);

/**
* @brief Close directory
*
* @param[in] dir: handle of directory
*
* @note This API is used to close a directory
*
* @return 0 on success. Others on failed
*/
int fs_card_dir_close(TUYA_DIR dir);


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
int fs_card_dir_read(TUYA_DIR dir, TUYA_FILEINFO* info);

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
int fs_card_dir_name(TUYA_FILEINFO info, const char** name);

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
int fs_card_dir_is_directory(TUYA_FILEINFO info, BOOL_T* is_dir);

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
TUYA_FILE fs_card_fopen(const char* path, const char* mode);

/**
* @brief Close file
*
* @param[in] file: file handle
*
* @note This API is used to close a file
*
* @return 0 on success. EOF on failed
*/
int fs_card_fclose(TUYA_FILE file);

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
int fs_card_fread(void* buf, int bytes, TUYA_FILE file);

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
int fs_card_fwrite(void* buf, int bytes, TUYA_FILE file);

/**
* @brief write buffer to flash
*
* @param[in] fd: file fd
*
* @note This API is used to write buffer to flash
*
* @return 0 on success. others on failed
*/
int fs_card_fsync(int fd);

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
char* fs_card_fgets(char* buf, int len, TUYA_FILE file);

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
int fs_card_fseek(TUYA_FILE file, int64_t offs, int whence);

/**
* @brief flush the IO read/write stream
*
* @param[in] file char stream
*
* @note This API is used to flush the IO read/write stream.
*
* @return 0 success,-1 failed
*/
int fs_card_fflush(TUYA_FILE file);

/**
* @brief get the file fd
*
* @param[in] file char stream
*
* @note This API is used to get the file fd.
*
* @return the file fd
*/
int fs_card_fileno(TUYA_FILE file);


/**
* @brief Make directory
*
* @param[in] path: path of directory
*
* @note This API is used for making a directory
*
* @return 0 on success. Others on failed
*/
TUYA_WEAK_ATTRIBUTE int fs_card_mkdir(const char* path)
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
TUYA_WEAK_ATTRIBUTE int fs_card_remove(const char* path)
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
TUYA_WEAK_ATTRIBUTE int fs_card_is_exist(const char* path, BOOL_T* is_exist)
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
TUYA_WEAK_ATTRIBUTE int fs_card_rename(const char* path_old, const char* path_new)
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
TUYA_WEAK_ATTRIBUTE int fs_card_dir_open(const char* path, TUYA_DIR* dir)
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
TUYA_WEAK_ATTRIBUTE int fs_card_dir_close(TUYA_DIR dir)
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
TUYA_WEAK_ATTRIBUTE int fs_card_dir_read(TUYA_DIR dir, TUYA_FILEINFO* info)
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
TUYA_WEAK_ATTRIBUTE int fs_card_dir_name(TUYA_FILEINFO info, const char** name)
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
TUYA_WEAK_ATTRIBUTE int fs_card_dir_is_directory(TUYA_FILEINFO info, BOOL_T* is_dir)
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
TUYA_WEAK_ATTRIBUTE TUYA_FILE fs_card_fopen(const char* path, const char* mode)
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
TUYA_WEAK_ATTRIBUTE int fs_card_fclose(TUYA_FILE file)
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
TUYA_WEAK_ATTRIBUTE int fs_card_fread(void* buf, int bytes, TUYA_FILE file)
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
TUYA_WEAK_ATTRIBUTE int fs_card_fwrite(void* buf, int bytes, TUYA_FILE file)
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
TUYA_WEAK_ATTRIBUTE int fs_card_fsync(int fd)
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
TUYA_WEAK_ATTRIBUTE char* fs_card_fgets(char* buf, int len, TUYA_FILE file)
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
TUYA_WEAK_ATTRIBUTE int fs_card_fseek(TUYA_FILE file, int64_t offs, int whence)
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
TUYA_WEAK_ATTRIBUTE int fs_card_fflush(TUYA_FILE file)
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
TUYA_WEAK_ATTRIBUTE int fs_card_fileno(TUYA_FILE file)
{
    FILE* fp = (FILE*)file;
    return fileno(fp);
}


/* ---------------------------------------------------------------------------
 * 文件系统平台 ops 卡：由本文件组装，core 的 tal_fs.c 经 extern 引用并完成
 * tal_fs_* / tal_dir_* 分发。文件系统为可选能力（p2p-core/local_store 与
 * agent_demo/src/components/audio_player 的文件源），与主卡（port_api.c）解耦；整文件随
 * ENABLE_TUYA_P2P || ENABLE_AI_PLAYER 门控，两者全关的平台无需适配。
 * --------------------------------------------------------------------------- */
TAL_FS_OPS_T tal_fs_ops_platform = {
    .fopen = fs_card_fopen,
    .fclose = fs_card_fclose,
    .fwrite = fs_card_fwrite,
    .fread = fs_card_fread,
    .fseek = fs_card_fseek,
    .fflush = fs_card_fflush,
    .fsync = fs_card_fsync,
    .fileno = fs_card_fileno,
    .fgets = fs_card_fgets,
    .fs_is_exist = fs_card_is_exist,
    .fs_mkdir = fs_card_mkdir,
    .fs_remove = fs_card_remove,
    .fs_rename = fs_card_rename,
    .dir_open = fs_card_dir_open,
    .dir_close = fs_card_dir_close,
    .dir_read = fs_card_dir_read,
    .dir_name = fs_card_dir_name,
    .dir_is_directory = fs_card_dir_is_directory,
};

#endif /* ENABLE_TUYA_P2P */

