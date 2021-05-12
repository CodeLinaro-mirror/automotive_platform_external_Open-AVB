/******************************************************************************
 Copyright (c) 2021, The Linux Foundation. All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are
 met:
     * Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
     * Redistributions in binary form must reproduce the above
       copyright notice, this list of conditions and the following
       disclaimer in the documentation and/or other materials provided
       with the distribution.
     * Neither the name of The Linux Foundation nor the names of its
       contributors may be used to endorse or promote products derived
       from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ******************************************************************************/

#include <ctype.h>
#include <signal.h>
#include <inttypes.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <libqrtr.h>
#include <linux/qrtr.h>
#include <gptp_log.hpp>
#include "ieee1588.hpp"
#include "avbts_clock.hpp"
#include "avbts_osnet.hpp"
#include "common_tstamper.hpp"
#include "avbts_oslock.hpp"
#include "avbts_persist.hpp"
#include "gptp_cfg.hpp"
#include "rgptp.hpp"
#include <poll.h>
#include <msm_ipc.h>
#include <sys/ioctl.h>

#define QSOCKET_QC_GPTP_TIME_SERVICE_ID 5010
#define QSOCKET_QC_GPTP_TIME_INSTANCE_ID 10
#define QSOCKET_QC_GPTP_TIME_VERSION 0
#define GPIO_EXP_PATH "/sys/class/gpio/export"
#define GPIO_UNEXP_PATH "/sys/class/gpio/unexport"
#define SPARE_GPIO_PIN "74"
#define GPIO_VAL "/value"
#define GPIO_DIR "/direction"
#define GPIO_FILE_SYS "/sys/class/gpio/gpio"
#define RGPTP_GPIO_DIR GPIO_FILE_SYS SPARE_GPIO_PIN GPIO_DIR
#define RGPTP_GPIO_VAL GPIO_FILE_SYS SPARE_GPIO_PIN GPIO_VAL
#define CTL_CMD_REMOVE_CLIENT       6

struct sync_data {
    uint64_t ptp_time;
    bool sync;
};


union address_t {
    struct sockaddr sa;
    struct sockaddr_msm_ipc sa_msm_ipc;
};

struct rgptp_data {
    int sk;
    int ctrl_sk;
    int gpio_fd;
    pthread_t thread_id;
    pthread_t ctrl_thread_id;
    timer_t timer_id;
    PortInit_t *port_init;
    struct sync_data data;
    union address_t clnt;
};


union ctl_msg {
    uint32_t cmd;
    struct {
        uint32_t cmd;
        uint32_t service;
        uint32_t instance;
        uint32_t node_id;
        uint32_t port_id;
    } srv;
    struct {
        uint32_t cmd;
        uint32_t node_id;
        uint32_t port_id;
    } cli;
};


static struct rgptp_data rgptp;

static int rgptpGpioInit(rgptp_data *rpgtp)
{
    ssize_t n;
    int exportfd, directionfd;
    //Unexport the GPIO if it was already opened in previous session
    exportfd = open(GPIO_UNEXP_PATH, O_WRONLY);

    if (exportfd < 0) {
        GPTP_LOG_DEBUG("Cannot open GPIO to unexport it\n");
    } else {
        int n = 0;
        n = write(exportfd, SPARE_GPIO_PIN, 3);

        if (n < 0) {
            GPTP_LOG_DEBUG("GPIO %s unexport failed:%s\n", SPARE_GPIO_PIN, strerror(errno));
        } else {
            GPTP_LOG_DEBUG("GPIO %s unexported succesfully\n", SPARE_GPIO_PIN);
        }
    }

    close(exportfd);
    exportfd = open(GPIO_EXP_PATH, O_WRONLY);

    if (exportfd < 0) {
        GPTP_LOG_ERROR("Cannot open GPIO to export it\n");
        close(exportfd);
        return -1;
    }

    n = write(exportfd, SPARE_GPIO_PIN, 3);

    if (n < 0) {
        GPTP_LOG_ERROR("GPIO %s exported failed:%s\n", SPARE_GPIO_PIN, strerror(errno));
        close(exportfd);
        return -1;
    }

    close(exportfd);
    GPTP_LOG_INFO("GPIO %s exported succesfully\n", SPARE_GPIO_PIN);
    //Set direction as output to toggle
    directionfd = open(RGPTP_GPIO_DIR, O_RDWR);

    if (directionfd < 0) {
        GPTP_LOG_ERROR("Cannot open GPIO direction \n");
        close(directionfd);
        return -1;
    }

    n = write(directionfd, "out", 4);

    if (n < 0) {
        GPTP_LOG_ERROR("GPIO set direction failed:%s\n", strerror(errno));
    }

    close(directionfd);
    rpgtp->gpio_fd = open(RGPTP_GPIO_VAL, O_RDWR);

    if (rpgtp->gpio_fd < 0) {
        GPTP_LOG_ERROR("Cannot open GPIO value \n");
        close(rpgtp->gpio_fd);
        return -1;
    }

    return 0;
}

void rgptpTimeoutThread( void *arg )
{
    int ret = -1;
    struct sync_data snd_buff = { 0, 0 };
    struct rgptp_data *info = NULL;
    info = (rgptp_data*)arg;

    if (info == NULL) {
        GPTP_LOG_ERROR("info value is null \n");
        return;
    } else if (info->port_init == NULL) {
        GPTP_LOG_ERROR("port_init value is null \n");
        return;
    } else if (info->port_init->timestamper == NULL) {
        GPTP_LOG_ERROR("timestamper value is null \n");
        return;
    }

    write(info->gpio_fd, "0", 2);
    write(info->gpio_fd, "1", 2);
    info->port_init->timestamper->HWTimestamper_getptptime(&snd_buff.ptp_time);
    snd_buff.sync = info->port_init->clock->getSyncStatus();
    ret =  sendto(info->sk, &snd_buff, sizeof(sync_data), MSG_DONTWAIT,
                  (struct sockaddr *)&info->clnt.sa, sizeof(union address_t));

    if (ret < 0) {
        GPTP_LOG_ERROR("%s, sendto - failure errno:%d --> %s",
                       __func__, errno, strerror(errno));
    }

    return;
}

static void *rgptpCtrlSrvThread(void *arg)
{
    struct pollfd fds;
    int ret = -1;
    ssize_t n;
    struct rgptp_data *info = (rgptp_data*)arg;
    union ctl_msg rx_ctl_msg;
    IEEE1588Clock *pClock;

    if (info == NULL) {
        GPTP_LOG_ERROR("info value is null \n");
        return NULL;
    } else if (info->port_init == NULL) {
        GPTP_LOG_ERROR("port_init value is null \n");
        return NULL;
    }

    pClock = info->port_init->clock;

    if (pClock == NULL) {
        GPTP_LOG_ERROR("clock value is null \n");
        return NULL;
    }

    while (1) {
        if (info->ctrl_sk == 0) {
            break;
        }

        fds.fd = info->ctrl_sk;
        fds.revents = 0;
        fds.events = POLLIN | POLLERR;
        ret = poll(&fds, 1, 1);

        if (ret < 0) {
            GPTP_LOG_ERROR("%s, ctrl poll error: %s \n", __func__, strerror(errno));
            return NULL;
        } else if (ret == 0) {
            continue;
        }

        n = recvfrom(info->ctrl_sk, &rx_ctl_msg, sizeof(rx_ctl_msg), MSG_DONTWAIT, NULL,
                     NULL);

        if (n < 0) {
            GPTP_LOG_WARNING("%s, recvfrom status: %s \n", __func__, strerror(errno));
            continue;
        }

        if (rx_ctl_msg.cmd == CTL_CMD_REMOVE_CLIENT) {
            if ((rx_ctl_msg.cli.node_id ==
                    info->clnt.sa_msm_ipc.address.addr.port_addr.node_id)
                    && (rx_ctl_msg.cli.port_id ==
                        info->clnt.sa_msm_ipc.address.addr.port_addr.port_id)) {
                GPTP_LOG_ERROR("%s, remove client message \n", __func__);
                pClock->deleteTimer(&info->timer_id);
            }
        }
    }

    return NULL;
}


static void *rgptpSrvThread(void *arg)
{
    int ret = -1;
    ssize_t n;
    char buff[20] = {0};
    socklen_t rl;
    struct rgptp_data *info = (rgptp_data*)arg;
    IEEE1588Clock *pClock;
    union address_t addr;
    socklen_t addr_size;
    struct pollfd fds;

    if (info == NULL) {
        GPTP_LOG_ERROR("info value is null \n");
        return NULL;
    } else if (info->port_init == NULL) {
        GPTP_LOG_ERROR("port_init value is null \n");
        return NULL;
    }

    pClock = info->port_init->clock;

    if (pClock == NULL) {
        GPTP_LOG_ERROR("clock value is null \n");
        return NULL;
    }

    while (1) {
        if (info->sk == 0) {
            break;
        }

        fds.fd = info->sk;
        fds.revents = 0;
        fds.events = POLLIN | POLLERR;
        ret = poll(&fds, 1, 1);

        if (ret < 0) {
            GPTP_LOG_ERROR("%s, qrtr_poll error: %s \n", __func__, strerror(errno));
            return NULL;
        } else if (ret == 0) {
            continue;
        }

        addr_size = sizeof(union address_t);
        n = recvfrom(info->sk, &buff, sizeof(buff), 0, (struct sockaddr *)&addr.sa,
                     &addr_size);

        if (n < 0) {
            GPTP_LOG_WARNING("%s, recvfrom status: %s \n", __func__, strerror(errno));
            continue;
        }

        GPTP_LOG_INFO("recvfrom - node = %u port = %u len = %d",
                      addr.sa_msm_ipc.address.addr.port_addr.node_id,
                      addr.sa_msm_ipc.address.addr.port_addr.port_id, n);
        info->clnt.sa_msm_ipc.family = AF_MSM_IPC;
        info->clnt.sa_msm_ipc.address.addrtype =  MSM_IPC_ADDR_ID;
        info->clnt.sa_msm_ipc.address.addr.port_addr.node_id =
            addr.sa_msm_ipc.address.addr.port_addr.node_id;
        info->clnt.sa_msm_ipc.address.addr.port_addr.port_id =
            addr.sa_msm_ipc.address.addr.port_addr.port_id;
        pClock->addTimer((info->port_init->rgptpSyncTime * 1000000),
                         rgptpTimeoutThread, info, false, &info->timer_id);
    }

    return NULL;
}

static int rgptpQtrInit(rgptp_data *rgptp)
{
    int ret = 0;
    socklen_t sl = 0;
    union address_t addr;

    do {
        rgptp->sk = socket(AF_MSM_IPC, SOCK_DGRAM | SOCK_CLOEXEC, 0);

        if (rgptp->sk < 0) {
            GPTP_LOG_ERROR("rgptp create socket error: %s", strerror(errno));
            ret = rgptp->sk;
            break;
        }

        rgptp->ctrl_sk = socket(AF_MSM_IPC, SOCK_DGRAM | SOCK_CLOEXEC, 0);

        if (rgptp->ctrl_sk < 0) {
            GPTP_LOG_ERROR("rgptp create ctrl socket error: %s", strerror(errno));
            ret = rgptp->ctrl_sk;
            break;
        }

        if (ioctl(rgptp->ctrl_sk, IPC_ROUTER_IOCTL_BIND_CONTROL_PORT, NULL) < 0) {
            GPTP_LOG_ERROR("%s: failed to bind as control port\n", __func__);
            break;
        }

        memset(&addr, 0, sizeof(struct sockaddr_msm_ipc));
        addr.sa_msm_ipc.family = AF_MSM_IPC;
        addr.sa_msm_ipc.address.addrtype = MSM_IPC_ADDR_NAME;
        addr.sa_msm_ipc.address.addr.port_name.service =
            QSOCKET_QC_GPTP_TIME_SERVICE_ID;
        addr.sa_msm_ipc.address.addr.port_name.instance =
            ( QSOCKET_QC_GPTP_TIME_INSTANCE_ID << 8 ) | QSOCKET_QC_GPTP_TIME_VERSION ;

        if (bind(rgptp->sk, &addr.sa, sizeof(addr)) < 0) {
            GPTP_LOG_ERROR("%s Failed for service_id=0x%x version=0x%x on %d\n", __func__,
                           QSOCKET_QC_GPTP_TIME_SERVICE_ID, QSOCKET_QC_GPTP_TIME_INSTANCE_ID, rgptp->sk);
            break;
        }

        ret = pthread_create(&rgptp->thread_id, NULL, rgptpSrvThread, rgptp);

        if (ret) {
            GPTP_LOG_ERROR("%s: rgptp server thread create failed: %s\n",
                           __func__, strerror(errno));
            rgptp->thread_id = 0;
            goto failed;
        }

        /* Control thread is used to identify the close of qmi from the client side */
        ret = pthread_create(&rgptp->ctrl_thread_id, NULL, rgptpCtrlSrvThread, rgptp);

        if (ret) {
            GPTP_LOG_ERROR("%s: rgptp ctrl server thread create failed: %s\n",
                           __func__, strerror(errno));
            rgptp->ctrl_thread_id = 0;
            goto failed;
        }
    } while (0);

    return 0;
failed:
    close(rgptp->sk);
    close(rgptp->ctrl_sk);
    return -1;
}

void rgptpDeInit(void)
{
    int ret = 0;
    IEEE1588Clock *pClock = rgptp.port_init->clock;

    if (rgptp.timer_id > 0) {
        pClock->deleteTimer(&rgptp.timer_id);
        rgptp.timer_id = 0;
    }

    if (rgptp.sk > 0) {
        close(rgptp.sk);
        rgptp.sk = 0;
    }

    if (rgptp.ctrl_sk > 0) {
        close(rgptp.ctrl_sk);
        rgptp.ctrl_sk = 0;
    }

    if (rgptp.thread_id > 0) {
        ret = pthread_detach(rgptp.thread_id);

        if (ret) {
            GPTP_LOG_ERROR("%s, gptp server thread detached failed: %s\n",
                           __func__, strerror(errno));
        }

        rgptp.thread_id = 0;
    }

    if (rgptp.ctrl_thread_id > 0) {
        ret = pthread_detach(rgptp.ctrl_thread_id);

        if (ret) {
            GPTP_LOG_ERROR("%s, gptp ctrl server thread detached failed: %s\n",
                           __func__, strerror(errno));
        }

        rgptp.ctrl_thread_id = 0;
    }

    if (rgptp.gpio_fd > 0) {
        int fd = 0;
        ret = close(rgptp.gpio_fd);

        if (ret) {
            GPTP_LOG_ERROR("%s, failure while closing GPIO fd : %s\n",
                           __func__, strerror(errno));
        }

        rgptp.gpio_fd = 0;
        //Unexport the GPIO
        fd = open(GPIO_UNEXP_PATH, O_WRONLY);

        if (fd < 0) {
            GPTP_LOG_ERROR("Cannot open GPIO to unexport it\n");
        } else {
            int n = 0;
            n = write(fd, SPARE_GPIO_PIN, 3);

            if (n < 0) {
                GPTP_LOG_ERROR("GPIO %s unexport failed:%s\n", SPARE_GPIO_PIN, strerror(errno));
            }
            else {
                GPTP_LOG_INFO("GPIO %s unexported succesfully\n", SPARE_GPIO_PIN);
            }
        }

        close(fd);
    }

    return;
}

void rgptpInit(PortInit_t *portInit)
{
    int ret = 0;

    do {
        memset(&rgptp, 0, sizeof(struct rgptp_data));
        rgptp.port_init = portInit;
        ret = rgptpGpioInit(&rgptp);

        if (ret < 0) {
            GPTP_LOG_ERROR("rgptpGpioInit failed");
            break;
        }

        ret = rgptpQtrInit(&rgptp);

        if (ret < 0) {
            GPTP_LOG_ERROR("rgptpQtrInit failed");
            goto failed;
        }
    } while (0);

    return;
failed:
    rgptpDeInit();
    return;
}
