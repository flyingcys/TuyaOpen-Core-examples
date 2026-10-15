/**
 * @file port_network.c
 * @brief 标准网络 ops 实例（lwIP socket 实现）+ TAL_NETWORK_OPS_T 组装（esp32 port）
 *
 * 与 linux 版（POSIX socket）对应：errno 转换表、fd_set 系列、select、
 * socket/connect/bind/send/recv、timeout/reuse/nagle/keepalive、
 * gethostbyname、str2addr/addr2str、setsockopt 等基础成员一一对应；可选能力
 * （局域网/对端服务、p2p）的成员仅在对应宏定义时才编译并挂载，关掉时成员为
 * NULL，core 侧 TAL_NET_EXEC_OP 有兜底（运行时报 "not available"）。
 *
 * errno 映射：core 比较的是 UNW_* 私有错误码空间（tuya_cloud_types.h），
 * lwIP 的 errno 为标准 errno 值，故转换表映射到 UNW_*（与 linux 版同表）。
 *
 * 2026-10 port 去 tkl 化：原 include/network/tkl_network.h 已删——该头只有
 * 本文件一个 includer，其声明内联到下方"原 tkl_network.h 内联区"（剥掉了原
 * #ifndef/#endif guard）。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 *
 */

#include "tuya_error_code.h"

#include "tal_network_ops.h"   /* 契约层（port/interface）：网络 ops 类型 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * 原 tkl_network.h 内联区（2026-10 去 tkl 化：该头只有本文件一个 includer，已
 * 删除）。原 extern "C" 块内的声明整体搬到 include 之后；原 #ifndef/#endif guard
 * 已剥掉（否则此处只有 #endif 会报 "#endif without #if"）。
 * 与 linux 版 port_network.c 的内联区同构：只有卡与 lwIP 实现需要用到的声明，
 * 不含平台私有扩展（esp32 无 connect_raw 之外的原生差异）。
 * --------------------------------------------------------------------------- */

TUYA_ERRNO port_net_get_errno(void);

OPERATE_RET port_net_fd_set(const int fd, TUYA_FD_SET_T* fds);

OPERATE_RET port_net_fd_isset(const int fd, TUYA_FD_SET_T* fds);

OPERATE_RET port_net_fd_zero(TUYA_FD_SET_T* fds);

int port_net_select(const int maxfd, TUYA_FD_SET_T *readfds, TUYA_FD_SET_T *writefds, TUYA_FD_SET_T *errorfds,
                    const uint32_t ms_timeout);

int port_net_get_nonblock(const int fd);

OPERATE_RET port_net_set_block(const int fd, const BOOL_T block);

TUYA_ERRNO port_net_close(const int fd);

int port_net_socket_create(const TUYA_PROTOCOL_TYPE_E type);

TUYA_ERRNO port_net_connect(const int fd, const TUYA_IP_ADDR_T addr, const uint16_t port);

TUYA_ERRNO port_net_bind(const int fd, const TUYA_IP_ADDR_T addr, const uint16_t port);

TUYA_ERRNO port_net_listen(const int fd, const int backlog);

TUYA_ERRNO port_net_accept(const int fd, TUYA_IP_ADDR_T *addr, uint16_t *port);

TUYA_ERRNO port_net_send(const int fd, const void *buf, const uint32_t nbytes);

TUYA_ERRNO port_net_send_to(const int fd, const void *buf, const uint32_t nbytes, const TUYA_IP_ADDR_T addr,
                            const uint16_t port);

TUYA_ERRNO port_net_recv(const int fd, void *buf, const uint32_t nbytes);

int port_net_recv_nd_size(const int fd, void *buf, const uint32_t buf_size, const uint32_t nd_size);

TUYA_ERRNO port_net_recvfrom(const int fd, void *buf, const uint32_t nbytes, TUYA_IP_ADDR_T *addr, uint16_t *port);

OPERATE_RET port_net_gethostbyname(const char *domain, TUYA_IP_ADDR_T *addr);

TUYA_IP_ADDR_T port_net_str2addr(const char *ip_str);

char *port_net_addr2str(const TUYA_IP_ADDR_T ipaddr);

OPERATE_RET port_net_setsockopt(const int fd, const TUYA_OPT_LEVEL level, const TUYA_OPT_NAME optname,
                                const void *optval, const int optlen);

OPERATE_RET port_net_set_timeout(const int fd, const int ms_timeout, const TUYA_TRANS_TYPE_E type);

OPERATE_RET port_net_set_reuse(const int fd);

OPERATE_RET port_net_disable_nagle(const int fd);

OPERATE_RET port_net_set_keepalive(int fd, const BOOL_T alive, const uint32_t idle, const uint32_t intr,
                                   const uint32_t cnt);

/* p2p 专用（CONFIG_ENABLE_TUYA_P2P，仅 p2p-core/pjproject 使用）*/
OPERATE_RET port_net_set_bufsize(const int fd, const int buf_size, const TUYA_TRANS_TYPE_E type);

OPERATE_RET port_net_getsockname(int fd, TUYA_IP_ADDR_T *addr, uint16_t *port);

OPERATE_RET port_net_getpeername(int fd, TUYA_IP_ADDR_T *addr, uint16_t *port);

OPERATE_RET port_net_fd_clear(const int fd, TUYA_FD_SET_T* fds);

TUYA_ERRNO port_net_shutdown(const int fd, const int how);

/* TUYA_FD_SET_T（UINT8_T 数组）转 lwIP fd_set：lwIP 的 fd_set 仅
 * FD_SETSIZE 位（CONFIG_LWIP_MAX_SOCKETS，默认 10 → 4 字节），
 * TUYA_FD_MAX_COUNT=64（8 字节）足够容纳 */
#define TAL_TO_SYS_FD_SET(fds) ((fd_set *)fds)

typedef struct NETWORK_ERRNO_TRANS {
    int sys_err;
    int priv_err;
} NETWORK_ERRNO_TRANS_S;

static const NETWORK_ERRNO_TRANS_S unw_errno_trans[] = {{EINTR, UNW_EINTR},
                                                        {EBADF, UNW_EBADF},
                                                        {EAGAIN, UNW_EAGAIN},
                                                        {EFAULT, UNW_EFAULT},
                                                        {EBUSY, UNW_EBUSY},
                                                        {EINVAL, UNW_EINVAL},
                                                        {ENFILE, UNW_ENFILE},
                                                        {EMFILE, UNW_EMFILE},
                                                        {ENOSPC, UNW_ENOSPC},
                                                        {EPIPE, UNW_EPIPE},
                                                        {EWOULDBLOCK, UNW_EWOULDBLOCK},
                                                        {ENOTSOCK, UNW_ENOTSOCK},
                                                        {ENOPROTOOPT, UNW_ENOPROTOOPT},
                                                        {EADDRINUSE, UNW_EADDRINUSE},
                                                        {EADDRNOTAVAIL, UNW_EADDRNOTAVAIL},
                                                        {ENETDOWN, UNW_ENETDOWN},
                                                        {ENETUNREACH, UNW_ENETUNREACH},
                                                        {ENETRESET, UNW_ENETRESET},
                                                        {ECONNRESET, UNW_ECONNRESET},
                                                        {ENOBUFS, UNW_ENOBUFS},
                                                        {EISCONN, UNW_EISCONN},
                                                        {ENOTCONN, UNW_ENOTCONN},
                                                        {ETIMEDOUT, UNW_ETIMEDOUT},
                                                        {ECONNREFUSED, UNW_ECONNREFUSED},
                                                        {EHOSTDOWN, UNW_EHOSTDOWN},
                                                        {EHOSTUNREACH, UNW_EHOSTUNREACH},
                                                        {ENOMEM, UNW_ENOMEM},
                                                        {EMSGSIZE, UNW_EMSGSIZE}};

/**
* @brief Get error code of network
*
* @param void
*
* @note This API is used for getting error code of network.
*
* @return 0 on success. Others on error, please refer to the error no of the target system
*/
TUYA_ERRNO port_net_get_errno(void)
{
    int sys_err = errno;
    int i;

    for (i = 0; i < (int)(sizeof(unw_errno_trans) / sizeof(unw_errno_trans[0])); i++) {
        if (unw_errno_trans[i].sys_err == sys_err) {
            return unw_errno_trans[i].priv_err;
        }
    }

    return -100 - sys_err;
}

/**
* @brief Add file descriptor to set
*
* @param[in] fd: file descriptor
* @param[in] fds: set of file descriptor
*
* @note This API is used to add file descriptor to set.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET port_net_fd_set(const int fd, TUYA_FD_SET_T *fds)
{
    if ((fd < 0) || (fds == NULL)) {
        return -3000 + fd;
    }

    FD_SET(fd, TAL_TO_SYS_FD_SET(fds));

    return OPRT_OK;
}

/**
* @brief Check file descriptor is in set
*
* @param[in] fd: file descriptor
* @param[in] fds: set of file descriptor
*
* @note This API is used to check the file descriptor is in set.
*
* @return TRUE or FALSE
*/
OPERATE_RET port_net_fd_isset(const int fd, TUYA_FD_SET_T *fds)
{
    return (OPERATE_RET)FD_ISSET(fd, TAL_TO_SYS_FD_SET(fds));
}

/**
* @brief Clear all file descriptor in set
*
* @param[in] fds: set of file descriptor
*
* @note This API is used to clear all file descriptor in set.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET port_net_fd_zero(TUYA_FD_SET_T *fds)
{
    if (fds == NULL) {
        return 0xFFFFFFFF;
    }

    FD_ZERO(TAL_TO_SYS_FD_SET(fds));

    return OPRT_OK;
}

/**
* @brief Get available file descriptors
*
* @param[in] maxfd: max count of file descriptor
* @param[out] readfds: a set of readalbe file descriptor
* @param[out] writefds: a set of writable file descriptor
* @param[out] errorfds: a set of except file descriptor
* @param[in] ms_timeout: time out
*
* @note This API is used to get available file descriptors.
*
* @return the count of available file descriptors.
*/
int port_net_select(const int maxfd, TUYA_FD_SET_T *readfds, TUYA_FD_SET_T *writefds, TUYA_FD_SET_T *errorfds,
                   const uint32_t ms_timeout)
{
    struct timeval *tmp = NULL;
    struct timeval timeout = {ms_timeout / 1000, (ms_timeout % 1000) * 1000};

    if (maxfd <= 0) {
        return maxfd;
    }

    if (0 != ms_timeout) {
        tmp = &timeout;
    }

    return select(maxfd, TAL_TO_SYS_FD_SET(readfds), TAL_TO_SYS_FD_SET(writefds), TAL_TO_SYS_FD_SET(errorfds), tmp);
}

/**
* @brief Get no block file descriptors
*
* @param[in] fd: file descriptor
*
* @note This API is used to get no block file descriptors.
*
* @return the count of no block file descriptors.
*/
int port_net_get_nonblock(const int fd)
{
    if (fd < 0) {
        return -3000 + fd;
    }

    if ((fcntl(fd, F_GETFL, 0) & O_NONBLOCK) == O_NONBLOCK) {
        return 1;
    }

    return 0;
}

/**
* @brief Set block flag for file descriptors
*
* @param[in] fd: file descriptor
* @param[in] block: block flag
*
* @note This API is used to set block flag for file descriptors.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET port_net_set_block(const int fd, const BOOL_T block)
{
    int flags;

    if (fd < 0) {
        return -3000 + fd;
    }

    /* lwIP 的 fcntl 支持 F_GETFL/F_SETFL（O_NONBLOCK），与 linux 版同机制；
     * ioctlsocket(FIONBIO) 在 esp-idf 的默认配置下无公开声明 */
    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return OPRT_COM_ERROR;
    }
    if (block) {
        flags &= ~O_NONBLOCK;
    } else {
        flags |= O_NONBLOCK;
    }
    if (fcntl(fd, F_SETFL, flags) < 0) {
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

/**
* @brief Close file descriptors
*
* @param[in] fd: file descriptor
*
* @note This API is used to close file descriptors.
*
* @return 0 on success. Others on error, please refer to the error no of the target system
*/
TUYA_ERRNO port_net_close(const int fd)
{
    if (fd < 0) {
        return -3000 + fd;
    }

    return close(fd);
}

/**
* @brief Create a tcp/udp socket
*
* @param[in] type: protocol type, tcp or udp
*
* @note This API is used for creating a tcp/udp socket.
*
* @return file descriptor
*/
int port_net_socket_create(const TUYA_PROTOCOL_TYPE_E type)
{
    int fd = -1;

    if (PROTOCOL_TCP == type) {
        fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    } else if (PROTOCOL_RAW == type) {
        fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    } else {
        fd = socket(AF_INET, SOCK_DGRAM, 0);
    }

    return fd;
}

/**
* @brief Connect to network
*
* @param[in] fd: file descriptor
* @param[in] addr: address information of server
* @param[in] port: port information of server
*
* @note This API is used for connecting to network.
*
* @return 0 on success. Others on error, please refer to the error no of the target system
*/
TUYA_ERRNO port_net_connect(const int fd, const TUYA_IP_ADDR_T addr, const uint16_t port)
{
    struct sockaddr_in sock_addr;

    if (fd < 0) {
        return -3000 + fd;
    }

    sock_addr.sin_family = AF_INET;
    sock_addr.sin_port = htons(port);
    sock_addr.sin_addr.s_addr = htonl(addr);

    return connect(fd, (struct sockaddr *)&sock_addr, sizeof(struct sockaddr_in));
}

/**
* @brief Connect to network with raw data
*
* @param[in] fd: file descriptor
* @param[in] p_socket: raw socket data
* @param[in] len: data lenth
*
* @note This API is used for connecting to network with raw data.
*
* @return 0 on success. Others on error, please refer to the error no of the target system
*/
TUYA_ERRNO port_net_connect_raw(const int fd, void *p_socket_addr, const int len)
{
    if (fd < 0) {
        return -3000 + fd;
    }

    return connect(fd, (struct sockaddr *)p_socket_addr, len);
}

/**
* @brief Bind to network
*
* @param[in] fd: file descriptor
* @param[in] addr: address information of server
* @param[in] port: port information of server
*
* @note This API is used for binding to network.
*
* @return 0 on success. Others on error, please refer to the error no of the target system
*/
TUYA_ERRNO port_net_bind(const int fd, const TUYA_IP_ADDR_T addr, const uint16_t port)
{
    struct sockaddr_in sock_addr;

    if (fd < 0) {
        return -3000 + fd;
    }

    sock_addr.sin_family = AF_INET;
    sock_addr.sin_port = htons(port);
    sock_addr.sin_addr.s_addr = htonl(addr);

    return bind(fd, (struct sockaddr *)&sock_addr, sizeof(struct sockaddr_in));
}

/**
* @brief Send data to network
*
* @param[in] fd: file descriptor
* @param[in] buf: send data buffer
* @param[in] nbytes: buffer lenth
*
* @note This API is used for sending data to network
*
* @return 0 on success. Others on error, please refer to the error no of the target system
*/
TUYA_ERRNO port_net_send(const int fd, const void *buf, const uint32_t nbytes)
{
    if ((fd < 0) || (buf == NULL) || (nbytes == 0)) {
        return -3000 + fd;
    }

    return send(fd, buf, nbytes, 0);
}

/**
* @brief Receive data from network
*
* @param[in] fd: file descriptor
* @param[in] buf: receive data buffer
* @param[in] nbytes: buffer lenth
*
* @note This API is used for receiving data from network
*
* @return 0 on success. Others on error, please refer to the error no of the target system
*/
TUYA_ERRNO port_net_recv(const int fd, void *buf, const uint32_t nbytes)
{
    int ret = -1;

    if ((fd < 0) || (buf == NULL) || (nbytes == 0)) {
        return -3000 + fd;
    }

    ret = recv(fd, buf, nbytes, 0);
    if (ret <= 0) {
        if ((UNW_EINTR == port_net_get_errno()) || (UNW_EAGAIN == port_net_get_errno())) {
            vTaskDelay(pdMS_TO_TICKS(10)); /* 10ms（原 tal 层 sleep(10) 的等价延时） */
            ret = recv(fd, buf, nbytes, 0);
        }
    }

    return ret;
}

/**
* @brief Set socket options
*
* @param[in] fd: file descriptor
* @param[in] level: setting level
* @param[in] optname: the name of the option
* @param[in] optval: the value of option
* @param[in] optlen: the length of the option value
*
* @note This API is used for setting socket options.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET port_net_setsockopt(const int fd, const TUYA_OPT_LEVEL level, const TUYA_OPT_NAME optname,
                               const void *optval, const int optlen)
{
    return setsockopt(fd, level, optname, optval, optlen);
}

/**
* @brief Get socket options
*
* @param[in] fd: file descriptor
* @param[in] level: getting level
* @param[in] optname: the name of the option
* @param[out] optval: the value of option
* @param[out] optlen: the length of the option value
*
* @note This API is used for getting socket options.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET port_net_getsockopt(const int fd, const TUYA_OPT_LEVEL level, const TUYA_OPT_NAME optname, void *optval,
                               int *optlen)
{
    return getsockopt(fd, level, optname, optval, (socklen_t *)optlen);
}

/**
* @brief Set timeout option of socket fd
*
* @param[in] fd: file descriptor
* @param[in] ms_timeout: timeout in ms
* @param[in] type: transfer type, receive or send
*
* @note This API is used for setting timeout option of socket fd.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET port_net_set_timeout(const int fd, const int ms_timeout, const TUYA_TRANS_TYPE_E type)
{
    struct timeval timeout = {ms_timeout / 1000, (ms_timeout % 1000) * 1000};
    int optname = ((type == TRANS_RECV) ? SO_RCVTIMEO : SO_SNDTIMEO);

    if (0 != port_net_setsockopt(fd, SOL_SOCKET, optname, (const char *)&timeout, sizeof(timeout))) {
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

/**
* @brief Enable reuse option of socket fd
*
* @param[in] fd: file descriptor
*
* @note This API is used to enable reuse option of socket fd.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET port_net_set_reuse(const int fd)
{
    int flag = 1;

    if (0 != port_net_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&flag, sizeof(int))) {
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

/**
* @brief Disable nagle option of socket fd
*
* @param[in] fd: file descriptor
*
* @note This API is used to disable nagle option of socket fd.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET port_net_disable_nagle(const int fd)
{
    int flag = 1;

    if (0 != port_net_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&flag, sizeof(int))) {
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

/**
* @brief Get address information by domain
*
* @param[in] domain: domain information
* @param[in] addr: address information
*
* @note This API is used for getting address information by domain.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET port_net_gethostbyname(const char *domain, TUYA_IP_ADDR_T *addr)
{
    struct hostent *h = NULL;

    if ((domain == NULL) || (addr == NULL)) {
        return OPRT_INVALID_PARM;
    }

    h = gethostbyname(domain);
    if (NULL == h) {
        return OPRT_COM_ERROR;
    }

    *addr = ntohl(((struct in_addr *)(h->h_addr_list[0]))->s_addr);

    return OPRT_OK;
}

/**
* @brief Set keepalive option of socket fd to monitor the connection
*
* @param[in] fd: file descriptor
* @param[in] alive: keepalive option, enable or disable option
* @param[in] idle: keep idle option, if the connection has no data exchange with the idle time(in seconds), start probe.
* @param[in] intr: keep interval option, the probe time interval.
* @param[in] cnt: keep count option, probe count.
*
* @note This API is used to set keepalive option of socket fd to monitor the connection.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET port_net_set_keepalive(int fd, const BOOL_T alive, const uint32_t idle, const uint32_t intr,
                                  const uint32_t cnt)
{
    int ret = 0;
    int keepalive = alive;
    int keepidle = idle;
    int keepinterval = intr;
    int keepcount = cnt;

    ret |= port_net_setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (void *)&keepalive, sizeof(keepalive));
    ret |= port_net_setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, (void *)&keepidle, sizeof(keepidle));
    ret |= port_net_setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, (void *)&keepinterval, sizeof(keepinterval));
    ret |= port_net_setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, (void *)&keepcount, sizeof(keepcount));

    if (0 != ret) {
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

/**
* @brief Change ip string to address
*
* @param[in] ip_str: ip string
*
* @note This API is used to change ip string to address.
*
* @return ip address
*/
TUYA_IP_ADDR_T port_net_str2addr(const char *ip_str)
{
    TUYA_IP_ADDR_T addr1;
    TUYA_IP_ADDR_T addr2;

    if (ip_str == NULL) {
        return 0xFFFFFFFF;
    }

    addr1 = inet_addr(ip_str);
    addr2 = ntohl(addr1);

    return addr2;
}

/**
* @brief Change ip address to string
*
* @param[in] ipaddr: ip address
*
* @note This API is used to change ip address(in host byte order) to string(in IPv4 numbers-and-dots(xx.xx.xx.xx) notion).
*
* @return ip string
*/
char *port_net_addr2str(const TUYA_IP_ADDR_T ipaddr)
{
    struct in_addr hostaddr;

    hostaddr.s_addr = htonl(ipaddr);

    return inet_ntoa(hostaddr);
}

/* ===== 局域网/对端服务接口（云通道不需要，按能力开关隔离）=====
 * 挂载条件由 port 的 CMake 从 CONFIG_* 映射注入（core 的宏不传播到 port 目标）：
 *   - listen/accept：局域网协议、p2p（sock_tal）、AI monitor（本地服务端）共用；
 *   - send_to/recvfrom：局域网协议、p2p 共用（AI monitor 不用）；
 *   - recv_nd_size：仅局域网协议（tuya_lan.c 收帧）使用。
 * 对应能力全关时不编译、卡成员为 NULL，core 侧 TAL_NET_EXEC_OP 有兜底
 * （运行时报 "not available"），port 移植时无需适配。
 * ===== */
#if defined(ENABLE_LOCAL_LINK) || defined(ENABLE_TUYA_P2P) || defined(ENABLE_AI_COMPONENTS)
/**
* @brief Listen to network
*
* @param[in] fd: file descriptor
* @param[in] backlog: max count of backlog connection
*
* @note This API is used for listening to network.
*
* @return 0 on success. Others on error, please refer to the error no of the target system
*/
TUYA_ERRNO port_net_listen(const int fd, const int backlog)
{
    if (fd < 0) {
        return -3000 + fd;
    }

    return listen(fd, backlog);
}

/**
* @brief Accept a connection on a listening socket
*
* @param[in] fd: file descriptor
* @param[out] addr: the accept ip addr
* @param[out] port: the accept port number
*
* @note This API is used for accepting a connection.
*
* @return the new socket fd on success. Others on error
*/
int port_net_accept(const int fd, TUYA_IP_ADDR_T *addr, uint16_t *port)
{
    struct sockaddr_in sock_addr;
    socklen_t len = sizeof(struct sockaddr_in);
    int cfd;

    if (fd < 0) {
        return -3000 + fd;
    }

    cfd = accept(fd, (struct sockaddr *)&sock_addr, &len);
    if (cfd < 0) {
        return -1;
    }

    if (addr) {
        *addr = ntohl(sock_addr.sin_addr.s_addr);
    }
    if (port) {
        *port = ntohs(sock_addr.sin_port);
    }

    return cfd;
}
#endif /* ENABLE_LOCAL_LINK || ENABLE_TUYA_P2P || ENABLE_AI_COMPONENTS */

#if defined(ENABLE_LOCAL_LINK) || defined(ENABLE_TUYA_P2P)
/**
* @brief Send data to specified server
*
* @param[in] fd: file descriptor
* @param[in] buf: send data buffer
* @param[in] nbytes: buffer lenth
* @param[in] addr: address information of server
* @param[in] port: port information of server
*
* @note This API is used for sending data to network
*
* @return 0 on success. Others on error, please refer to the error no of the target system
*/
TUYA_ERRNO port_net_send_to(const int fd, const void *buf, const uint32_t nbytes, const TUYA_IP_ADDR_T addr,
                           const uint16_t port)
{
    struct sockaddr_in sock_addr;

    if ((fd < 0) || (buf == NULL) || (nbytes == 0)) {
        return -3000 + fd;
    }

    sock_addr.sin_family = AF_INET;
    sock_addr.sin_port = htons(port);
    sock_addr.sin_addr.s_addr = htonl(addr);

    return sendto(fd, buf, nbytes, 0, (struct sockaddr *)&sock_addr, sizeof(sock_addr));
}

/**
* @brief Receive data from specified server
*
* @param[in] fd: file descriptor
* @param[in] buf: receive data buffer
* @param[in] nbytes: buffer lenth
* @param[out] addr: address information of server
* @param[out] port: port information of server
*
* @note This API is used for receiving data from specified server
*
* @return 0 on success. Others on error, please refer to the error no of the target system
*/
TUYA_ERRNO port_net_recvfrom(const int fd, void *buf, const uint32_t nbytes, TUYA_IP_ADDR_T *addr, uint16_t *port)
{
    struct sockaddr_in sock_addr;
    socklen_t addr_len = sizeof(struct sockaddr_in);
    int ret;

    if ((fd < 0) || (buf == NULL) || (nbytes == 0)) {
        return -3000 + fd;
    }

    ret = recvfrom(fd, buf, nbytes, 0, (struct sockaddr *)&sock_addr, &addr_len);
    if (ret <= 0) {
        return ret;
    }

    if (addr) {
        *addr = ntohl(sock_addr.sin_addr.s_addr);
    }
    if (port) {
        *port = ntohs(sock_addr.sin_port);
    }

    return ret;
}
#endif /* ENABLE_LOCAL_LINK || ENABLE_TUYA_P2P */

#if defined(ENABLE_LOCAL_LINK)
/**
* @brief Receive data from network with need size
*
* @param[in] fd: file descriptor
* @param[in] buf: receive data buffer
* @param[in] buf_size: buffer lenth
* @param[in] nd_size: the need size
*
* @note This API is used for receiving data from network with need size
*
* @return >0 on success. Others on error
*/
int port_net_recv_nd_size(const int fd, void *buf, const uint32_t buf_size, const uint32_t nd_size)
{
    uint32_t rd_size = 0;
    int ret = 0;

    if ((fd < 0) || (NULL == buf) || (buf_size == 0) || (nd_size == 0) || (buf_size < nd_size)) {
        return -3000 + fd;
    }

    while (rd_size < nd_size) {
        ret = recv(fd, ((uint8_t *)buf + rd_size), nd_size - rd_size, 0);
        if (ret <= 0) {
            TUYA_ERRNO err = port_net_get_errno();
            if (UNW_EWOULDBLOCK == err || UNW_EINTR == err || UNW_EAGAIN == err) {
                vTaskDelay(pdMS_TO_TICKS(10)); /* 10ms（原 tal 层 sleep(10) 的等价延时） */
                continue;
            }

            break;
        }

        rd_size += ret;
    }

    if (rd_size < nd_size) {
        return -2;
    }

    return (int)rd_size;
}
#endif /* ENABLE_LOCAL_LINK */

#if defined(ENABLE_TUYA_P2P)
/**
* @brief Clear file descriptor from set
*
* @param[in] fd: file descriptor
* @param[in] fds: set of file descriptor
*
* @note This API is used to clear file descriptor from set.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET port_net_fd_clear(const int fd, TUYA_FD_SET_T *fds)
{
    if ((fd < 0) || (fds == NULL)) {
        return -3000 + fd;
    }

    FD_CLR(fd, TAL_TO_SYS_FD_SET(fds));

    return OPRT_OK;
}

/**
* @brief Shutdown file descriptors
*
* @param[in] fd: file descriptor
* @param[in] how: shutdown type
*
* @note This API is used to shutdown file descriptors.
*
* @return 0 on success. Others on error, please refer to the error no of the target system
*/
TUYA_ERRNO port_net_shutdown(const int fd, const int how)
{
    if (fd < 0) {
        return -3000 + fd;
    }

    return shutdown(fd, how);
}

/**
* @brief Set buffer_size option of socket fd
*
* @param[in] fd: file descriptor
* @param[in] buf_size: buffer size in byte
* @param[in] type: transfer type, receive or send
*
* @note This API is used for setting buffer_size option of socket fd.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET port_net_set_bufsize(const int fd, const int buf_size, const TUYA_TRANS_TYPE_E type)
{
    int size = buf_size;
    int optname = ((type == TRANS_RECV) ? SO_RCVBUF : SO_SNDBUF);

    if (0 != port_net_setsockopt(fd, SOL_SOCKET, optname, (const char *)&size, sizeof(size))) {
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

/**
* @brief Get socket name
*
* @param[in] fd: file descriptor
* @param[out] addr: ip address
* @param[out] port: port information
*
* @note This API is used to Get the current name for the specified socket
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET port_net_getsockname(int fd, TUYA_IP_ADDR_T *addr, uint16_t *port)
{
    struct sockaddr_in sock_addr;
    socklen_t len = sizeof(sock_addr);

    if (fd < 0) {
        return -3000 + fd;
    }

    memset(&sock_addr, 0, sizeof(sock_addr));
    if (0 != getsockname(fd, (struct sockaddr *)&sock_addr, &len)) {
        return OPRT_COM_ERROR;
    }
    if (addr) {
        *addr = ntohl(sock_addr.sin_addr.s_addr);
    }
    if (port) {
        *port = ntohs(sock_addr.sin_port);
    }

    return OPRT_OK;
}

/**
* @brief Get name of connected peer socket
*
* @param[in] fd: file descriptor
* @param[out] addr: ip address
* @param[out] port: port information
*
* @note This API is used to Get the name of connected peer socket.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET port_net_getpeername(int fd, TUYA_IP_ADDR_T *addr, uint16_t *port)
{
    struct sockaddr_in sock_addr;
    socklen_t len = sizeof(sock_addr);

    if (fd < 0) {
        return -3000 + fd;
    }

    memset(&sock_addr, 0, sizeof(sock_addr));
    if (0 != getpeername(fd, (struct sockaddr *)&sock_addr, &len)) {
        return OPRT_COM_ERROR;
    }
    if (addr) {
        *addr = ntohl(sock_addr.sin_addr.s_addr);
    }
    if (port) {
        *port = ntohs(sock_addr.sin_port);
    }

    return OPRT_OK;
}
#endif /* ENABLE_TUYA_P2P */

/* ---------------------------------------------------------------------------
 * 标准网络 ops 实例：由本平台 TKL 实现组装，core 的 tal_network.c 经
 * extern 引用并完成 tal_net_* 分发。移植新平台时在各自 port 的
 * port_network.c 中提供同名实例即可。
 * ops 成员与实现同开关：可选能力（局域网/对端服务、p2p）的 ops 仅在对应宏
 * 定义时才挂载，关掉时成员为 NULL，core 侧 TAL_NET_EXEC_OP 会打印
 * "not available"。
 * --------------------------------------------------------------------------- */
TAL_NETWORK_OPS_T tal_network_ops_platform = {
    .name = "tkl",
    .type = TAL_NET_TYPE_PLATFORM,
    .ipaddr = 0,
    .ops =
        {
            .get_errno = port_net_get_errno,
            .fd_set = port_net_fd_set,
            .fd_isset = port_net_fd_isset,
            .fd_zero = port_net_fd_zero,
            .select = port_net_select,
            .get_nonblock = port_net_get_nonblock,
            .set_block = port_net_set_block,
            .close = port_net_close,
            .socket_create = port_net_socket_create,
            .connect = port_net_connect,
            .bind = port_net_bind,
            .send = port_net_send,
            .recv = port_net_recv,
            .set_timeout = port_net_set_timeout,
            .set_reuse = port_net_set_reuse,
            .disable_nagle = port_net_disable_nagle,
            .gethostbyname = port_net_gethostbyname,
            .set_keepalive = port_net_set_keepalive,
            .str2addr = port_net_str2addr,
            .addr2str = port_net_addr2str,
            .setsockopt = port_net_setsockopt,
            .getsockopt = port_net_getsockopt,
#if defined(ENABLE_LOCAL_LINK) || defined(ENABLE_TUYA_P2P) || defined(ENABLE_AI_COMPONENTS)
            /* 局域网/对端服务成员（实现见本文件对应集中块） */
            .listen = port_net_listen,
            .accept = port_net_accept,
#endif
#if defined(ENABLE_LOCAL_LINK) || defined(ENABLE_TUYA_P2P)
            .send_to = port_net_send_to,
            .recvfrom = port_net_recvfrom,
#endif
#if defined(ENABLE_LOCAL_LINK)
            /* 局域网协议专用成员（tuya_lan.c 收帧） */
            .recv_nd_size = port_net_recv_nd_size,
#endif
#if defined(ENABLE_TUYA_P2P)
            /* p2p 专用成员（实现见本文件 p2p 集中块） */
            .fd_clear = port_net_fd_clear,
            .shutdown = port_net_shutdown,
            .set_bufsize = port_net_set_bufsize,
            .getsockname = port_net_getsockname,
            .getpeername = port_net_getpeername,
#endif
        },
};
