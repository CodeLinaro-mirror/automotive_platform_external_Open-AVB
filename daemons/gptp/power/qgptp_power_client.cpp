/******************************************************************************
Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
SPDX-License-Identifier: BSD-3-Clause-Clear
******************************************************************************/

#include <android/binder_process.h>
#include <PowerPolicyClientBase.h>
#include <aidl/android/frameworks/automotive/powerpolicy/CarPowerPolicy.h>
#include <aidl/android/frameworks/automotive/powerpolicy/PowerComponent.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <string.h>
#include <log/log.h>


#define LOGE(fmt, ...) __android_log_print (ANDROID_LOG_ERROR,"gptp_lpm", fmt, __VA_ARGS__); printf(fmt,##__VA_ARGS__)
#define LOGW(fmt, ...) __android_log_print (ANDROID_LOG_WARN,"gptp_lpm", fmt, __VA_ARGS__); printf(fmt,##__VA_ARGS__)
#define LOGI(fmt, ...) __android_log_print (ANDROID_LOG_INFO,"gptp_lpm", fmt, __VA_ARGS__); printf(fmt,##__VA_ARGS__)
#define LOGD(fmt, ...) __android_log_print (ANDROID_LOG_DEBUG,"gptp_lpm", fmt, __VA_ARGS__); printf(fmt,##__VA_ARGS__)

enum _LOGGER_SEVERITY {
    QCLOG_ERROR         = ANDROID_LOG_ERROR,
    QCLOG_WARNING       = ANDROID_LOG_WARN,
    QCLOG_INFO          = ANDROID_LOG_INFO,
    QCLOG_DEBUG2        = ANDROID_LOG_DEBUG
};

#define GPTP_LOG_ERROR(fmt, ...) LOGE("[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__); printf(fmt,##__VA_ARGS__)
#define GPTP_LOG_WARNING(fmt, ...) LOGW("[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__); printf(fmt,##__VA_ARGS__)
#define GPTP_LOG_INFO(fmt, ...) LOGI("[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__); printf(fmt,##__VA_ARGS__)
#define GPTP_LOG_DEBUG(fmt, ...) LOGD("[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__); printf(fmt,##__VA_ARGS__)


using aidl::android::frameworks::automotive::powerpolicy::CarPowerPolicy;
using aidl::android::frameworks::automotive::powerpolicy::PowerComponent;
using android::frameworks::automotive::powerpolicy::hasComponent;
using ndk::ScopedAStatus;

static constexpr const char* GPTP_SOCKET_PATH = "/dev/socket/gptp_power_socket";

enum PowerEvent {
    POWER_SUSPEND = 0,
    POWER_RESUME  = 1,
};

static void notifyGptp(PowerEvent ev) {
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) return;

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strlcpy(addr.sun_path, GPTP_SOCKET_PATH, sizeof(addr.sun_path));

    sendto(fd, &ev, sizeof(ev), 0,
           reinterpret_cast<sockaddr*>(&addr),
           sizeof(addr));
    close(fd);
}

class GptpPowerService
    : public android::frameworks::automotive::powerpolicy::PowerPolicyClientBase {
public:
    std::vector<PowerComponent> getComponentsOfInterest() override {
        return {
            PowerComponent::CPU,
            PowerComponent::DISPLAY
        };
    }

    ScopedAStatus onPolicyChanged(const CarPowerPolicy& policy) override {
        bool cpuEnabled =
            hasComponent(policy.enabledComponents, PowerComponent::CPU);
        bool displayEnabled =
            hasComponent(policy.enabledComponents, PowerComponent::DISPLAY);

        if (!cpuEnabled || !displayEnabled) {
            GPTP_LOG_INFO(
            "PowerPolicy: CPU and DISPLAY are OFF → notify gPTP SUSPEND "
            "(CPU=%d, DISPLAY=%d)",
            cpuEnabled, displayEnabled);

            notifyGptp(POWER_SUSPEND);
        }
        if (cpuEnabled && displayEnabled) {
            GPTP_LOG_INFO("PowerPolicy: CPU and DISPLAY ON → notify gPTP RESUME");
            notifyGptp(POWER_RESUME);
        }
        return ScopedAStatus::ok();
    }

    void onInitFailed() override {
        GPTP_LOG_ERROR("PowerPolicyClientBase init failed");
    }
};

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(1);

    GPTP_LOG_ERROR("PowerPolicy: gptp client init");
    auto svc = ndk::SharedRefBase::make<GptpPowerService>();
    svc->init();   // registers with Car Power Manager
    GPTP_LOG_ERROR("PowerPolicy: gptp client init done");

    ABinderProcess_joinThreadPool();
    return 0;
}
