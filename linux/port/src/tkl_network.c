/**
 * @file tkl_network.c
 * @brief Linux TKL network implementation (POSIX sockets) for tkl_network.h.
 *
 * Implements the tkl_net_* interface declared in port/include/network/tkl_network.h
 * on top of BSD sockets, and assembles the standard network card
 * (tal_network_card_platform) consumed by core's tal_network.c. Porting a new
 * platform means providing this one file (implementation + card) in its port.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */
#include "tal_api.h"
#include "tal_network.h"
#include "tkl_network.h"
#include "tkl_wired.h"

#define TAL_TO_SYS_FD_SET(fds) ((fd_set *)fds)

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define ENABLE_BIND_INTERFACE 1

typedef struct NETWORK_ERRNO_TRANS {
    int sys_err;
    int priv_err;
} NETWORK_ERRNO_TRANS_S;

const NETWORK_ERRNO_TRANS_S unw_errno_trans[] = {{EINTR, UNW_EINTR},
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
 * @return 0 on success. Others on error, please refer to the error no of the
 * target system
 */
TUYA_ERRNO tkl_net_get_errno(void)
{
    int sys_err;

    int i = 0;
    sys_err = errno;

    for (i = 0; i < sizeof(unw_errno_trans) / sizeof(unw_errno_trans[0]); i++) {
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
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET tkl_net_fd_set(int fd, TUYA_FD_SET_T *fds)
{
    int ret = OPRT_OK;

    if ((fd < 0) || (fds == NULL)) {
        return -3000 + fd;
    }

    FD_SET(fd, TAL_TO_SYS_FD_SET(fds));

    return ret;
}

/**
 * @brief Clear file descriptor from set
 *
 * @param[in] fd: file descriptor
 * @param[in] fds: set of file descriptor
 *
 * @note This API is used to clear file descriptor from set.
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET tkl_net_fd_clear(int fd, TUYA_FD_SET_T *fds)
{
    int ret = OPRT_OK;

    if ((fd < 0) || (fds == NULL)) {
        return -3000 + fd;
    }

    FD_CLR(fd, TAL_TO_SYS_FD_SET(fds));

    return ret;
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
OPERATE_RET tkl_net_fd_isset(int fd, TUYA_FD_SET_T *fds)
{
    int ret = FALSE;

    ret = FD_ISSET(fd, TAL_TO_SYS_FD_SET(fds));

    return ret;
}

/**
 * @brief Clear all file descriptor in set
 *
 * @param[in] fds: set of file descriptor
 *
 * @note This API is used to clear all file descriptor in set.
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET tkl_net_fd_zero(TUYA_FD_SET_T *fds)
{
    int ret = OPRT_OK;

    if (fds == NULL) {
        return 0xFFFFFFFF;
    }

    FD_ZERO(TAL_TO_SYS_FD_SET(fds));

    return ret;
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
int tkl_net_select(const int maxfd, TUYA_FD_SET_T *readfds, TUYA_FD_SET_T *writefds, TUYA_FD_SET_T *errorfds,
                         const uint32_t ms_timeout)
{
    int ret = -1;

    if (maxfd <= 0) {
        return maxfd;
    }

    struct timeval *tmp = NULL;
    struct timeval timeout = {ms_timeout / 1000, (ms_timeout % 1000) * 1000};
    tmp = ms_timeout ? &timeout : NULL;
    ret = select(maxfd, TAL_TO_SYS_FD_SET(readfds), TAL_TO_SYS_FD_SET(writefds), TAL_TO_SYS_FD_SET(errorfds), tmp);

    return ret;
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
int tkl_net_get_nonblock(const int fd)
{
    int ret = 0;

    if (fd < 0) {
        return -3000 + fd;
    }

    if ((fcntl(fd, F_GETFL, 0) & O_NONBLOCK) == O_NONBLOCK) {
        ret = 1;
    }

    return ret;
}

/**
 * @brief Set block flag for file descriptors
 *
 * @param[in] fd: file descriptor
 * @param[in] block: block flag
 *
 * @note This API is used to set block flag for file descriptors.
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET tkl_net_set_block(const int fd, const BOOL_T block)
{
    int ret = 0;

    if (fd < 0) {
        return -3000 + fd;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    if (block) {
        flags &= (~O_NONBLOCK);
    } else {
        flags |= O_NONBLOCK;
    }

    if (fcntl(fd, F_SETFL, flags) < 0) {
        ret = -1;
    }

    return ret;
}

/**
 * @brief Close file descriptors
 *
 * @param[in] fd: file descriptor
 *
 * @note This API is used to close file descriptors.
 *
 * @return 0 on success. Others on error, please refer to the error no of the
 * target system
 */
TUYA_ERRNO tkl_net_close(const int fd)
{
    int ret = 0;

    if (fd < 0) {
        return -3000 + fd;
    }

    ret = close(fd);

    return ret;
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
int tkl_net_socket_create(const TUYA_PROTOCOL_TYPE_E type)
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
 * @return 0 on success. Others on error, please refer to the error no of the
 * target system
 */
TUYA_ERRNO tkl_net_connect(const int fd, const TUYA_IP_ADDR_T addr, const uint16_t port)
{
    int ret = -1;

    if (fd < 0) {
        return -3000 + fd;
    }

    struct sockaddr_in sock_addr;
    uint16_t tmp_port = port;
    TUYA_IP_ADDR_T tmp_addr = addr;

    sock_addr.sin_family = AF_INET;
    sock_addr.sin_port = htons(tmp_port);
    sock_addr.sin_addr.s_addr = htonl(tmp_addr);

    ret = connect(fd, (struct sockaddr *)&sock_addr, sizeof(struct sockaddr_in));
    return ret;
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
 * @return 0 on success. Others on error, please refer to the error no of the
 * target system
 */
TUYA_ERRNO tkl_net_connect_raw(const int fd, void *p_socket_addr, const int len)
{
    int ret = -1;

    if (fd < 0) {
        return -3000 + fd;
    }

    ret = connect(fd, (struct sockaddr *)p_socket_addr, len);

    return ret;
}

#if defined(ENABLE_BIND_INTERFACE) && 1 == ENABLE_BIND_INTERFACE
static TUYA_ERRNO __bind_interface(const int fd, const TUYA_IP_ADDR_T addr)
{
    int ret = 0;
    int i = 0;
    int sock_fd;
    struct sockaddr_in *sin;
    struct ifreq ifr;

    struct if_nameindex *name_list = if_nameindex();
    if (NULL == name_list) {
        printf("name_list NULL\n");
        return -1;
    }

    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        printf("socket create fail\n");
        return -2;
    }

    for (i = 0; name_list[i].if_index != 0; i++) {
        if (strcmp(name_list[i].if_name, "lo") == 0) {
            continue;
        }

        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, name_list[i].if_name, sizeof(ifr.ifr_name) - 1);
        if ((ret = ioctl(sock_fd, SIOCGIFADDR, &ifr)) < 0) {
            continue;
        }

        sin = (struct sockaddr_in *)&ifr.ifr_addr;
        if (sin->sin_addr.s_addr == htonl(addr)) {
            ret = setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, name_list[i].if_name, sizeof(ifr.ifr_name));
            break;
        }
    }
    close(sock_fd);
    if_freenameindex(name_list);

    return ret;
}
#endif

/**
 * @brief Bind to network
 *
 * @param[in] fd: file descriptor
 * @param[in] addr: address information of server
 * @param[in] port: port information of server
 *
 * @note This API is used for binding to network.
 *
 * @return 0 on success. Others on error, please refer to the error no of the
 * target system
 */
TUYA_ERRNO tkl_net_bind(const int fd, const TUYA_IP_ADDR_T addr, const uint16_t port)
{
    int ret = -1;

    if (fd < 0) {
        return -3000 + fd;
    }

    uint16_t tmp_port = port;
    TUYA_IP_ADDR_T tmp_addr = addr;

    struct sockaddr_in sock_addr;
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_port = htons(tmp_port);
    sock_addr.sin_addr.s_addr = htonl(tmp_addr);

    ret = bind(fd, (struct sockaddr *)&sock_addr, sizeof(struct sockaddr_in));
#if ENABLE_BIND_INTERFACE
    if ((0 == ret) && (addr != INADDR_ANY)) {
        if (0 != __bind_interface(fd, addr)) {
            printf("name_list NULL\n");
        }
    }
#endif

    return ret;
}

/**
 * @brief Listen to network
 *
 * @param[in] fd: file descriptor
 * @param[in] backlog: max count of backlog connection
 *
 * @note This API is used for listening to network.
 *
 * @return 0 on success. Others on error, please refer to the error no of the
 * target system
 */
TUYA_ERRNO tkl_net_listen(const int fd, const int backlog)
{
    int ret = -1;

    if (fd < 0) {
        return -3000 + fd;
    }

    ret = listen(fd, backlog);

    return ret;
}

/**
 * @brief Listen to network
 *
 * @param[in] fd: file descriptor
 * @param[out] addr: the accept ip addr
 * @param[out] port: the accept port number
 *
 * @note This API is used for listening to network.
 *
 * @return 0 on success. Others on error, please refer to the error no of the
 * target system
 */
int tkl_net_accept(const int fd, TUYA_IP_ADDR_T *addr, uint16_t *port)
{
    int ret = -1;

    if (fd < 0) {
        return -3000 + fd;
    }

    struct sockaddr_in sock_addr;
    socklen_t len = sizeof(struct sockaddr_in);
    ret = accept(fd, (struct sockaddr *)&sock_addr, &len);
    if (ret < 0) {
        return -1;
    }
    if (addr) {
        *addr = ntohl((sock_addr.sin_addr.s_addr));
    }
    if (port) {
        *port = ntohs((sock_addr.sin_port));
    }

    return ret;
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
 * @return 0 on success. Others on error, please refer to the error no of the
 * target system
 */
TUYA_ERRNO tkl_net_send(const int fd, const void *buf, const uint32_t nbytes)
{
    int ret = -1;

    if ((fd < 0) || (buf == NULL) || (nbytes == 0)) {
        return -3000 + fd;
    }

    ret = send(fd, buf, nbytes, 0);

    return ret;
}

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
 * @return 0 on success. Others on error, please refer to the error no of the
 * target system
 */
TUYA_ERRNO tkl_net_send_to(const int fd, const void *buf, const uint32_t nbytes, const TUYA_IP_ADDR_T addr,
                                 const uint16_t port)
{
    int ret = -1;

    if ((fd < 0) || (buf == NULL) || (nbytes == 0)) {
        return -3000 + fd;
    }

    uint16_t tmp_port = port;
    TUYA_IP_ADDR_T tmp_addr = addr;

    struct sockaddr_in sock_addr;
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_port = htons(tmp_port);
    sock_addr.sin_addr.s_addr = htonl(tmp_addr);

    ret = sendto(fd, buf, nbytes, 0, (struct sockaddr *)&sock_addr, sizeof(sock_addr));

    return ret;
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
 * @return 0 on success. Others on error, please refer to the error no of the
 * target system
 */
TUYA_ERRNO tkl_net_recv(const int fd, void *buf, const uint32_t nbytes)
{
    int ret = -1;

    if ((fd < 0) || (buf == NULL) || (nbytes == 0)) {
        return -3000 + fd;
    }

    ret = recv(fd, buf, nbytes, 0);
    if (ret <= 0) {
        if ((UNW_EINTR == tkl_net_get_errno()) || (UNW_EAGAIN == tkl_net_get_errno())) {
            tal_system_sleep(10);
            ret = recv(fd, buf, nbytes, 0);
        }
    }

    return ret;
}

/**
 * @brief Receive data from network with need size
 *
 * @param[in] fd: file descriptor
 * @param[in] buf: receive data buffer
 * @param[in] nbytes: buffer lenth
 * @param[in] nd_size: the need size
 *
 * @note This API is used for receiving data from network with need size
 *
 * @return >0 on success. Others on error
 */
int tkl_net_recv_nd_size(const int fd, void *buf, const uint32_t buf_size, const uint32_t nd_size)
{
    int ret = -1;

    if ((fd < 0) || (NULL == buf) || (buf_size == 0) || (nd_size == 0) || (buf_size < nd_size)) {
        return -3000 + fd;
    }

    uint32_t rd_size = 0;

    while (rd_size < nd_size) {
        ret = recv(fd, ((uint8_t *)buf + rd_size), nd_size - rd_size, 0);
        if (ret <= 0) {
            if (UNW_EWOULDBLOCK == tkl_net_get_errno() || UNW_EINTR == tkl_net_get_errno() ||
                UNW_EAGAIN == tkl_net_get_errno()) {
                tal_system_sleep(10);
                continue;
            }
            break;
        }
        rd_size += ret;
    }
    if (rd_size < nd_size) {
        ret = -2;
    } else {
        ret = rd_size;
    }

    return ret;
}

/**
 * @brief Receive data from specified server
 *
 * @param[in] fd: file descriptor
 * @param[in] buf: receive data buffer
 * @param[in] nbytes: buffer lenth
 * @param[in] addr: address information of server
 * @param[in] port: port information of server
 *
 * @note This API is used for receiving data from specified server
 *
 * @return 0 on success. Others on error, please refer to the error no of the
 * target system
 */
TUYA_ERRNO tkl_net_recvfrom(const int fd, void *buf, const uint32_t nbytes, TUYA_IP_ADDR_T *addr, uint16_t *port)
{
    int ret = -1;

    if ((fd < 0) || (buf == NULL) || (nbytes == 0)) {
        return -3000 + fd;
    }

    struct sockaddr_in sock_addr;
    socklen_t addr_len = sizeof(sock_addr);
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
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET tkl_net_setsockopt(const int fd, const TUYA_OPT_LEVEL level, const TUYA_OPT_NAME optname,
                                     const void *optval, const int optlen)
{
    int ret = -1;

    ret = setsockopt(fd, level, optname, optval, optlen);

    return ret;
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
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET tkl_net_getsockopt(const int fd, const TUYA_OPT_LEVEL level, const TUYA_OPT_NAME optname,
                                     void *optval, int *optlen)
{
    int ret = -1;

    ret = getsockopt(fd, level, optname, optval, (socklen_t *)optlen);

    return ret;
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
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET tkl_net_set_timeout(const int fd, const int ms_timeout, const TUYA_TRANS_TYPE_E type)
{
    int ret = -1;

    if (fd < 0) {
        return -3000 + fd;
    }

    // NOTE： use TUYA_timeval avoid conflict with sys/time.h
    struct timeval timeout = {ms_timeout / 1000, (ms_timeout % 1000) * 1000};
    int optname = ((type == TRANS_RECV) ? SO_RCVTIMEO : SO_SNDTIMEO);

    ret = tkl_net_setsockopt(fd, SOL_SOCKET, optname, (const char *)&timeout, sizeof(timeout));

    return ret;
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
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET tkl_net_set_bufsize(const int fd, const int buf_size, const TUYA_TRANS_TYPE_E type)
{
    int ret = -1;

    if (fd < 0) {
        return -3000 + fd;
    }

    int size = buf_size;
    int optname = ((type == TRANS_RECV) ? SO_RCVBUF : SO_SNDBUF);

    ret = tkl_net_setsockopt(fd, SOL_SOCKET, optname, (const char *)&size, sizeof(size));

    return ret;
}

/**
 * @brief Enable reuse option of socket fd
 *
 * @param[in] fd: file descriptor
 *
 * @note This API is used to enable reuse option of socket fd.
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET tkl_net_set_reuse(const int fd)
{
    int ret = -1;

    if (fd < 0) {
        return -3000 + fd;
    }

    int flag = 1;

    ret = tkl_net_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&flag, sizeof(int));

    return ret;
}

/**
 * @brief Disable nagle option of socket fd
 *
 * @param[in] fd: file descriptor
 *
 * @note This API is used to disable nagle option of socket fd.
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET tkl_net_disable_nagle(const int fd)
{
    int ret = -1;

    if (fd < 0) {
        return -3000 + fd;
    }

    int flag = 1;

    ret = tkl_net_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&flag, sizeof(int));

    return ret;
}

/**
 * @brief Enable broadcast option of socket fd
 *
 * @param[in] fd: file descriptor
 *
 * @note This API is used to enable broadcast option of socket fd.
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET tkl_net_set_broadcast(const int fd)
{
    int ret = -1;

    if (fd < 0) {
        return -3000 + fd;
    }
    int flag = 1;
    ret = tkl_net_setsockopt(fd, SOL_SOCKET, SO_BROADCAST, (const char *)&flag, sizeof(int));

    return ret;
}

/**
 * @brief Get address information by domain
 *
 * @param[in] domain: domain information
 * @param[in] addr: address information
 *
 * @note This API is used for getting address information by domain.
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET tkl_net_gethostbyname(const char *domain, TUYA_IP_ADDR_T *addr)
{
    int ret = -1;

    if ((domain == NULL) || (addr == NULL)) {
        return -2;
    }

    struct hostent *h = NULL;
    h = gethostbyname(domain);
    if (h) {
        *addr = ntohl(((struct in_addr *)(h->h_addr_list[0]))->s_addr);
        ret = OPRT_OK;
    }

    return ret;
}

/**
 * @brief Set keepalive option of socket fd to monitor the connection
 *
 * @param[in] fd: file descriptor
 * @param[in] alive: keepalive option, enable or disable option
 * @param[in] idle: keep idle option, if the connection has no data exchange
 * with the idle time(in seconds), start probe.
 * @param[in] intr: keep interval option, the probe time interval.
 * @param[in] cnt: keep count option, probe count.
 *
 * @note This API is used to set keepalive option of socket fd to monitor the
 * connection.
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET tkl_net_set_keepalive(int fd, const BOOL_T alive, const uint32_t idle, const uint32_t intr,
                                        const uint32_t cnt)
{
    int ret = 0;

    if (fd < 0) {
        return -3000 + fd;
    }

    int keepalive = alive;
    int keepidle = idle;
    int keepinterval = intr;
    int keepcount = cnt;

    ret |= tkl_net_setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (void *)&keepalive, sizeof(keepalive));
    ret |= tkl_net_setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, (void *)&keepidle, sizeof(keepidle));
    ret |= tkl_net_setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, (void *)&keepinterval, sizeof(keepinterval));
    ret |= tkl_net_setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, (void *)&keepcount, sizeof(keepcount));

    return ret;
}

/**
 * @brief Get ip address by socket fd
 *
 * @param[in] fd: file descriptor
 * @param[out] addr: ip address
 *
 * @note This API is used for getting ip address by socket fd.
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET tkl_net_get_socket_ip(int fd, TUYA_IP_ADDR_T *addr)
{
    int ret = -1;

    struct sockaddr_in sock_addr;
    memset(&sock_addr, 0, sizeof(sock_addr));
    socklen_t len = sizeof(sock_addr);

    if (0 == getsockname(fd, (struct sockaddr *)&sock_addr, &len)) {
        *addr = ntohl(sock_addr.sin_addr.s_addr);
        ret = OPRT_OK;
    }

    return ret;
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
TUYA_IP_ADDR_T tkl_net_str2addr(const char *ip_str)
{

    if (ip_str == NULL) {
        return 0xFFFFFFFF;
    }

    TUYA_IP_ADDR_T addr1 = inet_addr(ip_str);
    TUYA_IP_ADDR_T addr2 = ntohl(addr1);

    return addr2;
}

/**
 * @brief Change ip address to string
 *
 * @param[in] ipaddr: ip address
 *
 * @note This API is used to change ip address(in host byte order) to string(in
 * IPv4 numbers-and-dots(xx.xx.xx.xx) notion).
 *
 * @return ip string
 */
char *tkl_net_addr2str(TUYA_IP_ADDR_T ipaddr)
{
    struct in_addr hostaddr;
    hostaddr.s_addr = htonl(ipaddr);
    return inet_ntoa(hostaddr);
}

/**
 * @brief Disable send and/or receive on a socket
 *
 * @param[in] fd: file descriptor
 * @param[in] how: 0 to shut down receive, 1 to shut down send, 2 for both
 *
 * @note This API is used to shut down part of a full-duplex connection while
 * the file descriptor stays open.
 *
 * @return 0 on success. Others on error, please refer to the error no of the
 * target system
 */
TUYA_ERRNO tkl_net_shutdown(const int fd, const int how)
{
    if (fd < 0) {
        return -3000 + fd;
    }

    return shutdown(fd, how);
}

/**
 * @brief Get the local address a socket fd is bound to
 *
 * @param[in] fd: file descriptor
 * @param[out] addr: local ip address in host byte order, may be NULL
 * @param[out] port: local port in host byte order, may be NULL
 *
 * @note Unlike tkl_net_get_socket_ip() this also reports the port, which
 * is the only way to learn the port picked when binding to port 0.
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET tkl_net_getsockname(int fd, TUYA_IP_ADDR_T *addr, uint16_t *port)
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
 * @brief Get the address of the peer connected to a socket fd
 *
 * @param[in] fd: file descriptor
 * @param[out] addr: peer ip address in host byte order, may be NULL
 * @param[out] port: peer port in host byte order, may be NULL
 *
 * @note This API is used for getting the remote name of a connected socket.
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
OPERATE_RET tkl_net_getpeername(int fd, TUYA_IP_ADDR_T *addr, uint16_t *port)
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

/* ---------------------------------------------------------------------------
 * 标准网络卡：由本平台 TKL 实现组装，core 的 tal_network.c 经
 * extern 引用并完成 tal_net_* 分发。移植新平台时在各自 port 的
 * tkl_network.c 中提供同名卡即可。
 * --------------------------------------------------------------------------- */
TAL_NETWORK_CARD_T tal_network_card_platform = {
    .name = "tkl",
    .type = TAL_NET_TYPE_PLATFORM,
    .ipaddr = 0,
    .ops =
        {
            .get_errno = tkl_net_get_errno,
            .fd_set = tkl_net_fd_set,
            .fd_clear = tkl_net_fd_clear,
            .fd_isset = tkl_net_fd_isset,
            .fd_zero = tkl_net_fd_zero,
            .select = tkl_net_select,
            .get_nonblock = tkl_net_get_nonblock,
            .set_block = tkl_net_set_block,
            .close = tkl_net_close,
            .shutdown = tkl_net_shutdown,
            .socket_create = tkl_net_socket_create,
            .connect = tkl_net_connect,
            .connect_raw = tkl_net_connect_raw,
            .bind = tkl_net_bind,
            .listen = tkl_net_listen,
            .send = tkl_net_send,
            .send_to = tkl_net_send_to,
            .accept = tkl_net_accept,
            .recv = tkl_net_recv,
            .recv_nd_size = tkl_net_recv_nd_size,
            .recvfrom = tkl_net_recvfrom,
            .set_timeout = tkl_net_set_timeout,
            .set_bufsize = tkl_net_set_bufsize,
            .set_reuse = tkl_net_set_reuse,
            .disable_nagle = tkl_net_disable_nagle,
            .set_broadcast = tkl_net_set_broadcast,
            .gethostbyname = tkl_net_gethostbyname,
            .set_keepalive = tkl_net_set_keepalive,
            .get_socket_ip = tkl_net_get_socket_ip,
            .getsockname = tkl_net_getsockname,
            .getpeername = tkl_net_getpeername,
            .str2addr = tkl_net_str2addr,
            .addr2str = tkl_net_addr2str,
            .setsockopt = tkl_net_setsockopt,
            .getsockopt = tkl_net_getsockopt,
        },
};

/* ===== 有线网卡接口（原 tkl_wired.c 内联；仅 Linux 文件实现）=====
 *
 * 只保留真正被使用的两个对外接口：
 *   tkl_wired_get_ip        —— 上层取有线 IP（tuya_main.c / p2p pj_tuyaos_local_ip.c）
 *   tkl_wired_set_status_cb —— 上层注册链路状态回调（内部起轮询线程）
 * 原 tkl_wired_get_status 降为 static（状态轮询线程内部使用）；
 * set_ip / get_mac / set_mac 无调用方，未内联。
 * ===== */

static TKL_WIRED_STATUS_CHANGE_CB s_wired_event_cb;
static pthread_t s_wired_event_thread;

/* 回环接口不算有线链路 */
static int is_loopback_interface(const char *if_name)
{
    if (if_name == NULL) {
        return 0;
    }

    return (strcmp(if_name, "lo") == 0 || strncmp(if_name, "loopback", 8) == 0);
}

/* 取指定接口的链路状态（ioctl SIOCGIFFLAGS） */
static OPERATE_RET __wired_get_status_by_name(const char *if_name, TKL_WIRED_STAT_E *status)
{
    int sockfd;
    struct ifreq ifr;

    memset((void *)&ifr, 0, sizeof(ifr));

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return OPRT_SOCK_ERR;
    }

    strncpy(ifr.ifr_name, if_name, strlen(if_name));
    if (ioctl(sockfd, SIOCGIFFLAGS, &ifr) < 0) {
        perror("ioctl");
        close(sockfd);
        return OPRT_COM_ERROR;
    }

    *status = (ifr.ifr_flags & IFF_UP) ? TKL_WIRED_LINK_UP : TKL_WIRED_LINK_DOWN;
    close(sockfd);

    return OPRT_OK;
}

/* 遍历非回环接口，任一 up 即视为有线链路 up */
static OPERATE_RET __wired_get_status(TKL_WIRED_STAT_E *status)
{
    struct if_nameindex *name_list;
    int i;

    *status = TKL_WIRED_LINK_DOWN;

    name_list = if_nameindex();
    if (NULL == name_list) {
        return OPRT_NOT_FOUND;
    }

    for (i = 0; name_list[i].if_index != 0; ++i) {
        if (is_loopback_interface(name_list[i].if_name))
            continue;

        TKL_WIRED_STAT_E if_status = TKL_WIRED_LINK_DOWN;
        if (!__wired_get_status_by_name(name_list[i].if_name, &if_status)) {
            *status = if_status;
            if (if_status == TKL_WIRED_LINK_UP) {
                break;
            }
        }
    }

    if_freenameindex(name_list);
    return OPRT_OK;
}

/* 状态轮询线程：3s 一次，变化时回调（原 tkl_wired.c 行为） */
static void *wired_status_event_cb(void *arg)
{
    TKL_WIRED_STAT_E last_stat = -1;
    TKL_WIRED_STAT_E stat = TKL_WIRED_LINK_DOWN;

    (void)arg;
    pthread_detach(pthread_self());
    while (1) {
        __wired_get_status(&stat);
        if (stat != last_stat) {
            s_wired_event_cb(stat);
            last_stat = stat;
        }

        sleep(3);
    }

    return NULL;
}

/* 取指定接口的 IP/掩码（ioctl SIOCGIFADDR / SIOCGIFNETMASK） */
static OPERATE_RET __wired_get_ip_by_name(const char *if_name, NW_IP_S *ip)
{
    int sock;
    struct ifreq ifr;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        printf("socket create failse...GetLocalIp!\n");
        return OPRT_COM_ERROR;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, if_name, sizeof(ifr.ifr_name) - 1);
    if (ioctl(sock, SIOCGIFADDR, &ifr) < 0) {
        close(sock);
        return OPRT_COM_ERROR;
    }
    strncpy(ip->ip, inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr), sizeof(ip->ip));

    if (ioctl(sock, SIOCGIFNETMASK, &ifr) < 0) {
        close(sock);
        return OPRT_COM_ERROR;
    }
    strncpy(ip->mask, inet_ntoa(((struct sockaddr_in *)&ifr.ifr_netmask)->sin_addr), sizeof(ip->mask));

    close(sock);
    return OPRT_OK;
}

/**
 * @brief  get the ip address of the wired link（取第一个非回环且 up 的接口的 IP）
 *
 * @param[out]  ip: the ip address
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_wired_get_ip(NW_IP_S *ip)
{
    OPERATE_RET rt = OPRT_NOT_FOUND;
    struct if_nameindex *name_list;
    int i;

    name_list = if_nameindex();
    if (NULL == name_list) {
        return OPRT_NOT_FOUND;
    }

    for (i = 0; name_list[i].if_index != 0; ++i) {
        if (is_loopback_interface(name_list[i].if_name))
            continue;

        TKL_WIRED_STAT_E if_status = TKL_WIRED_LINK_DOWN;
        if (!__wired_get_status_by_name(name_list[i].if_name, &if_status) && if_status == TKL_WIRED_LINK_UP) {
            if (!__wired_get_ip_by_name(name_list[i].if_name, ip)) {
                rt = OPRT_OK;
                break;
            }
        }
    }

    if_freenameindex(name_list);

    return rt;
}

/**
 * @brief  set the status change callback（注册回调并起轮询线程，重复调用幂等）
 *
 * @param[in]   cb: the callback when link status changed
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_wired_set_status_cb(TKL_WIRED_STATUS_CHANGE_CB cb)
{
    if (cb) {
        TKL_WIRED_STAT_E status = TKL_WIRED_LINK_DOWN;

        __wired_get_status(&status);

        s_wired_event_cb = cb;
        if (!s_wired_event_thread) {
            int ret = pthread_create(&s_wired_event_thread, NULL, wired_status_event_cb, NULL);
            if (OPRT_OK != ret) {
                return ret;
            }
        }

        cb(status);
    }

    return OPRT_OK;
}
