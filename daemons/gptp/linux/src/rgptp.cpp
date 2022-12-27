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
#include <linux/gpio.h>
#include <sys/ioctl.h>

#define QSOCKET_QC_GPTP_TIME_SERVICE_ID 5010
#define QSOCKET_QC_GPTP_TIME_INSTANCE_ID 10
#define GPIO_EXP_PATH "/sys/class/gpio/export"
#define GPIO_UNEXP_PATH "/sys/class/gpio/unexport"
#define SPARE_GPIO_PIN "82"
#define SPARE_GPIO_PIN_NO 82
#define GPIO_VAL "/value"
#define GPIO_DIR "/direction"
#define GPIO_FILE_SYS "/sys/class/gpio/gpio"
#define RGPTP_GPIO_DIR GPIO_FILE_SYS SPARE_GPIO_PIN GPIO_DIR
#define RGPTP_GPIO_VAL GPIO_FILE_SYS SPARE_GPIO_PIN GPIO_VAL
#define GPIO_DEV_NAME "/dev/gpiochip0"


struct sync_data{
    uint64_t ptp_time;
    bool sync;
};

struct rgptp_data{
    int sk;
    int gpio_fd;
    pthread_t thread_id;
    timer_t timer_id;
    PortInit_t *port_init;
    struct sync_data data;
    struct sockaddr_qrtr clnt;
};

static struct rgptp_data rgptp;

static int rgptpGpioInit(rgptp_data *rpgtp)
{

#ifndef GPIO_CHIP
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
    if(exportfd < 0) {
        GPTP_LOG_ERROR("Cannot open GPIO to export it\n");
        close(exportfd);
        return -1;
    }

    n = write(exportfd, SPARE_GPIO_PIN, 3);
    if(n < 0) {
        GPTP_LOG_ERROR("GPIO %s exported failed:%s\n", SPARE_GPIO_PIN, strerror(errno));
        close(exportfd);
        return -1;
    }
    close(exportfd);
    GPTP_LOG_INFO("GPIO %s exported succesfully\n", SPARE_GPIO_PIN);

    //Set direction as output to toggle
    directionfd = open(RGPTP_GPIO_DIR, O_RDWR);
    if(directionfd < 0) {
        GPTP_LOG_ERROR("Cannot open GPIO direction \n");
        close(directionfd);
        return -1;
    }
    n = write(directionfd, "out", 4);
    if(n < 0) {
        GPTP_LOG_ERROR("GPIO set direction failed:%s\n", strerror(errno));
    }
    close(directionfd);

    rpgtp->gpio_fd = open(RGPTP_GPIO_VAL, O_RDWR);
    if(rpgtp->gpio_fd < 0) {
        GPTP_LOG_ERROR("Cannot open GPIO value \n");
        close(rpgtp->gpio_fd);
        return -1;
    }

#else
	struct gpiohandle_request req;
	struct gpiochip_info cinfo;
	struct gpioline_info linfo;
	int fd, ret;
	printf("rgptp init\n");
	fd = open(GPIO_DEV_NAME, O_RDONLY);

	if (fd < 0) {
		printf("Unabled to open %s: %s", GPIO_DEV_NAME, strerror(errno));
		return -1;
	}

	ret = ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &cinfo);

	if (ret < 0) {
		printf("ERROR get chip info ret=%d\n", ret);
		return -1;
	}

	printf("GPIO chip: %s, \"%s\", %u GPIO lines\n",
	       cinfo.name, cinfo.label, cinfo.lines);

	memset(&linfo, 0, sizeof(linfo));
	linfo.line_offset = SPARE_GPIO_PIN_NO;

	ret = ioctl(fd, GPIO_GET_LINEINFO_IOCTL, &linfo);

	if (ret == -1) {
		printf("Failed to issue LINEINFO IOCTL\n");
		return -1;
	}
	req.lineoffsets[0] = SPARE_GPIO_PIN_NO;
	req.lines = 1;
	req.flags = GPIOHANDLE_REQUEST_OUTPUT;

	ret = ioctl(fd, GPIO_GET_LINEHANDLE_IOCTL, &req);

	if (ret == -1) {
		fprintf(stderr, "Failed to issue GET LINEHANDLE IOCTL (%d) %s\n",
		        errno, strerror(errno));
		return -1;
	}

	rpgtp->gpio_fd = req.fd;

	if (close(fd) == -1) {
		perror("Failed to close GPIO device");
	}

	printf("gpio init successful\n");

#endif
    return 0;
}

void rgptpTimeoutThread( void *arg )
{
    int ret = -1;
    struct sync_data snd_buff = { 0, 0 };
    struct rgptp_data *info = NULL;

    info = (rgptp_data*)arg;
    if(info == NULL)
        return;

#ifndef GPIO_CHIP
	write(info->gpio_fd, "0", 2);
	write(info->gpio_fd, "1", 2);
#else
	struct gpiohandle_data data;
	data.values[0] = 0;
	ret = ioctl(info->gpio_fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data);

	if (ret == -1) {
		GPTP_LOG_ERROR("%s,Failed to issue GPIOHANDLE_SET_LINE_VALUES_IOCTL (%d) %s\n",
		               __func__, errno, strerror(errno));
	}

	data.values[0] = 1;
	ret = ioctl(info->gpio_fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data);

	if (ret == -1) {
		GPTP_LOG_ERROR("%s,Failed to issue GPIOHANDLE_SET_LINE_VALUES_IOCTL (%d) %s\n",
		               __func__, errno, strerror(errno));
	}
#endif

    info->port_init->timestamper->HWTimestamper_getptptime(&snd_buff.ptp_time);
    snd_buff.sync = info->port_init->clock->getSyncStatus();
    ret = sendto(info->sk, &snd_buff, sizeof(sync_data), MSG_DONTWAIT,
                    (sockaddr *)&info->clnt, sizeof(struct sockaddr_qrtr));
    if(ret < 0) {
        GPTP_LOG_ERROR("%s, sendto - failure errno:%d --> %s",
                                __func__, errno, strerror(errno));
    }

    return;
}

static void *rgptpSrvThread(void *arg)
{
    int ret = -1;
    ssize_t n;
    char buff[20] = {0};
    socklen_t rl;
    struct sockaddr_qrtr rq;
    struct qrtr_packet pkt;
    struct rgptp_data *info = (rgptp_data*)arg;
    IEEE1588Clock *pClock = info->port_init->clock;
    bool timerActiveStatus = false;

    while(1) {
        rl = sizeof(rq);
        memset(&rq, 0, sizeof(rq));

        if(info->sk == 0) {
            break;
        }
        ret = qrtr_poll(info->sk, 1);
        if(ret < 0) {
            GPTP_LOG_ERROR("%s, qrtr_poll error: %s \n",__func__, strerror(errno));
            return NULL;
        }
        else if(ret == 0) {
            continue;
        }

        n = recvfrom(info->sk,&buff, sizeof(buff), 0, (sockaddr *)&rq, &rl);
        if (n < 0) {
            GPTP_LOG_WARNING("%s, recvfrom status: %s \n",__func__, strerror(errno));
            continue;
        }

        GPTP_LOG_INFO("recvfrom - node = %u port = %u len = %d",
                                                rq.sq_node, rq.sq_port, n);
        ret = qrtr_decode(&pkt, &buff, n, &rq);
        if (ret < 0) {
            continue;
        }
        GPTP_LOG_INFO("qrtr_decode - service=%u instance =%u version =%u ",
                                                pkt.service, pkt.instance, pkt.version);
        GPTP_LOG_INFO("qrtr_decode - node = %u port = %u ", pkt.node, pkt.port);
        GPTP_LOG_INFO("%s  qrtr_decode pkt - type = %d data_len = %d data = %s\n",__func__,
                                                            pkt.type, pkt.data_len, pkt.data);

        switch (pkt.type) {
            case QRTR_TYPE_DATA:
                memset(&info->clnt, 0, sizeof(struct sockaddr_qrtr));
                info->clnt.sq_family = AF_QIPCRTR;
                info->clnt.sq_node = pkt.node;
                info->clnt.sq_port = pkt.port;
                GPTP_LOG_INFO("%s,%d addTimer", __func__, getpid());
                pClock->addTimer((info->port_init->rgptpSyncTime * 1000000),
                                 rgptpTimeoutThread, info, false, &info->timer_id);
                timerActiveStatus = true;
                break;

            case QRTR_TYPE_DEL_CLIENT:
                if ((info->clnt.sq_node == pkt.node) && (info->clnt.sq_port == pkt.port)
                        && (timerActiveStatus == true)) {
                    GPTP_LOG_INFO("%s,%d deleteTimer", __func__, getpid());
                    pClock->deleteTimer(&info->timer_id);
                    timerActiveStatus = false;
                }

                break;

            case QRTR_TYPE_BYE:
                if ((info->clnt.sq_node == pkt.node) && (timerActiveStatus == true)) {
                    GPTP_LOG_INFO("%s,%d deleteTimer", __func__, getpid());
                    pClock->deleteTimer(&info->timer_id);
                    timerActiveStatus = false;
                }

                break;

            default:
                GPTP_LOG_ERROR("%s,%d unknown message", __func__, getpid());
                break;
        }
    }

    return NULL;
}

static int rgptpQtrInit(rgptp_data *rgptp)
{
    int ret = 0;
    socklen_t sl = 0;
    struct sockaddr_qrtr sq;

    do {
        rgptp->sk = qrtr_open(0);
        if (rgptp->sk < 0) {
            GPTP_LOG_ERROR("rgptp create socket error: %s", strerror(errno));
            ret = rgptp->sk;
            break;
        }

        ret = qrtr_publish(rgptp->sk, QSOCKET_QC_GPTP_TIME_SERVICE_ID, 0,
                                                QSOCKET_QC_GPTP_TIME_INSTANCE_ID);
        if(ret) {
            GPTP_LOG_ERROR("qrtr_publish failed: %s", strerror(errno));
            goto failed;
        }

        sl = sizeof(sq);
        memset(&sq, 0, sizeof(sq));
        ret = getsockname(rgptp->sk, (sockaddr*)&sq, &sl);
        if(ret) {
            GPTP_LOG_ERROR("getsockname failed: %s",strerror(errno));
            goto failed;
        }
        GPTP_LOG_INFO("getsockname- sq_family=%u sq_node =%u sq_port =%u",
                                            sq.sq_family, sq.sq_node, sq.sq_port);

        ret = pthread_create(&rgptp->thread_id, NULL, rgptpSrvThread, rgptp);
        if(ret){
            GPTP_LOG_ERROR("%s: rgptp server thread create failed: %s\n",
                                                            __func__, strerror(errno));
			rgptp->thread_id = 0;
            goto failed;
        }
    }while (0);

    return 0;
failed:
    qrtr_close(rgptp->sk);
    return -1;
}

void rgptpDeInit(void)
{
    int ret = 0;
    IEEE1588Clock *pClock = rgptp.port_init->clock;

    if(rgptp.timer_id > 0) {
        pClock->deleteTimer(&rgptp.timer_id);
        rgptp.timer_id = 0;
    }

    if(rgptp.sk > 0) {
        ret = qrtr_bye(rgptp.sk, QSOCKET_QC_GPTP_TIME_SERVICE_ID,
                                        0, QSOCKET_QC_GPTP_TIME_INSTANCE_ID);
        if(ret){
            GPTP_LOG_ERROR("%s, qrtr_bye failed: %s", __func__, strerror(errno));
        }
        qrtr_close(rgptp.sk);
        rgptp.sk = 0;
    }

    if(rgptp.thread_id > 0) {
        ret = pthread_detach(rgptp.thread_id);
        if(ret){
            GPTP_LOG_ERROR("%s, gptp server thread detached failed: %s\n",
                                                        __func__, strerror(errno));
        }
        rgptp.thread_id = 0;
    }

#ifndef GPIO_CHIP

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
			} else {
				GPTP_LOG_INFO("GPIO %s unexported succesfully\n", SPARE_GPIO_PIN);
			}
		}

		close(fd);
	}

#else

	if (rgptp.gpio_fd > 0) {
		ret = close(rgptp.gpio_fd);

		if (ret) {
			GPTP_LOG_ERROR("%s, failure while closing GPIO fd : %s\n",
			               __func__, strerror(errno));
		}

		rgptp.gpio_fd = 0;
	}

#endif
	return;
}

void rgptpInit(PortInit_t *portInit)
{
    int ret = 0;

    do{
        memset(&rgptp, 0, sizeof(struct rgptp_data));
        rgptp.port_init = portInit;
        ret = rgptpGpioInit(&rgptp);
        if(ret < 0) {
            GPTP_LOG_ERROR("rgptpGpioInit failed");
            break;
        }
        ret = rgptpQtrInit(&rgptp);
        if(ret < 0) {
            GPTP_LOG_ERROR("rgptpQtrInit failed");
            goto failed;
        }
    }while(0);
    return;

failed:
    rgptpDeInit();
    return;
}
