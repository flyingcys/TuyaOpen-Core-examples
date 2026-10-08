/**
 * @file tkl_netif.c
 * @brief 本机网络接口信息实现（POSIX ioctl）：取 IP / 链路状态回调
 *
 * 由原 tkl_wired.c 改造：实现不限有线（遍历所有非回环接口，wlan0/eth0 皆可），
 * 故弱化 wired 命名；与 socket 通信实现（tkl_network.c）分离，独立成文件。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */
#include "tkl_netif.h"

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

static TKL_NETIF_LINK_CHANGE_CB s_netif_link_cb;
static pthread_t s_netif_link_thread;

/* 回环接口不算网络链路 */
static int is_loopback_interface(const char *if_name)
{
    if (if_name == NULL) {
        return 0;
    }

    return (strcmp(if_name, "lo") == 0 || strncmp(if_name, "loopback", 8) == 0);
}

/* 取指定接口的链路状态（ioctl SIOCGIFFLAGS） */
static OPERATE_RET __netif_get_status_by_name(const char *if_name, TKL_NETIF_LINK_STAT_E *status)
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

    *status = (ifr.ifr_flags & IFF_UP) ? TKL_NETIF_LINK_UP : TKL_NETIF_LINK_DOWN;
    close(sockfd);

    return OPRT_OK;
}

/* 遍历非回环接口，任一 up 即视为链路 up */
static OPERATE_RET __netif_get_status(TKL_NETIF_LINK_STAT_E *status)
{
    struct if_nameindex *name_list;
    int i;

    *status = TKL_NETIF_LINK_DOWN;

    name_list = if_nameindex();
    if (NULL == name_list) {
        return OPRT_NOT_FOUND;
    }

    for (i = 0; name_list[i].if_index != 0; ++i) {
        if (is_loopback_interface(name_list[i].if_name))
            continue;

        TKL_NETIF_LINK_STAT_E if_status = TKL_NETIF_LINK_DOWN;
        if (!__netif_get_status_by_name(name_list[i].if_name, &if_status)) {
            *status = if_status;
            if (if_status == TKL_NETIF_LINK_UP) {
                break;
            }
        }
    }

    if_freenameindex(name_list);
    return OPRT_OK;
}

/* 链路状态轮询线程：3s 一次，变化时回调 */
static void *netif_link_event_cb(void *arg)
{
    TKL_NETIF_LINK_STAT_E last_stat = -1;
    TKL_NETIF_LINK_STAT_E stat = TKL_NETIF_LINK_DOWN;

    (void)arg;
    pthread_detach(pthread_self());
    while (1) {
        __netif_get_status(&stat);
        if (stat != last_stat) {
            s_netif_link_cb(stat);
            last_stat = stat;
        }

        sleep(3);
    }

    return NULL;
}

/* 取指定接口的 IP/掩码（ioctl SIOCGIFADDR / SIOCGIFNETMASK） */
static OPERATE_RET __netif_get_ip_by_name(const char *if_name, NW_IP_S *ip)
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

OPERATE_RET tkl_netif_get_ip(NW_IP_S *ip)
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

        TKL_NETIF_LINK_STAT_E if_status = TKL_NETIF_LINK_DOWN;
        if (!__netif_get_status_by_name(name_list[i].if_name, &if_status) && if_status == TKL_NETIF_LINK_UP) {
            if (!__netif_get_ip_by_name(name_list[i].if_name, ip)) {
                rt = OPRT_OK;
                break;
            }
        }
    }

    if_freenameindex(name_list);

    return rt;
}

OPERATE_RET tkl_netif_set_link_cb(TKL_NETIF_LINK_CHANGE_CB cb)
{
    if (cb) {
        TKL_NETIF_LINK_STAT_E status = TKL_NETIF_LINK_DOWN;

        __netif_get_status(&status);

        s_netif_link_cb = cb;
        if (!s_netif_link_thread) {
            int ret = pthread_create(&s_netif_link_thread, NULL, netif_link_event_cb, NULL);
            if (OPRT_OK != ret) {
                return ret;
            }
        }

        cb(status);
    }

    return OPRT_OK;
}
