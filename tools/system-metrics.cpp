#include "system-metrics.h"

#include "ggml-backend.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#ifdef __linux__
#    include <dirent.h>
#endif

#ifdef _WIN32
#    include <windows.h>
#endif

namespace {

static double clamp_percent(double value) {
    return std::max(0.0, std::min(100.0, value));
}

#ifdef __linux__
static bool sample_linux_cpu(double & usage) {
    std::ifstream stat("/proc/stat");
    if (!stat) {
        return false;
    }

    std::string label;
    std::uint64_t user = 0;
    std::uint64_t nice = 0;
    std::uint64_t system = 0;
    std::uint64_t idle = 0;
    std::uint64_t iowait = 0;
    std::uint64_t irq = 0;
    std::uint64_t softirq = 0;
    std::uint64_t steal = 0;
    stat >> label >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
    if (!stat || label != "cpu") {
        return false;
    }

    const std::uint64_t total = user + nice + system + idle + iowait + irq + softirq + steal;
    const std::uint64_t idle_total = idle + iowait;

    static std::mutex       mutex;
    static std::uint64_t    previous_total = 0;
    static std::uint64_t    previous_idle = 0;
    std::lock_guard<std::mutex> lock(mutex);

    if (previous_total != 0 && total > previous_total && idle_total >= previous_idle) {
        const std::uint64_t total_delta = total - previous_total;
        const std::uint64_t idle_delta = idle_total - previous_idle;
        usage = clamp_percent(100.0 * static_cast<double>(total_delta - std::min(total_delta, idle_delta)) /
                              static_cast<double>(total_delta));
    } else {
        usage = 0.0;
    }

    previous_total = total;
    previous_idle = idle_total;
    return true;
}
#endif

#ifdef _WIN32
static bool sample_windows_cpu(double & usage) {
    FILETIME idle_time;
    FILETIME kernel_time;
    FILETIME user_time;
    if (!GetSystemTimes(&idle_time, &kernel_time, &user_time)) {
        return false;
    }

    const auto to_ticks = [](const FILETIME & value) {
        return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
    };
    const std::uint64_t idle = to_ticks(idle_time);
    const std::uint64_t kernel = to_ticks(kernel_time);
    const std::uint64_t user = to_ticks(user_time);

    static std::mutex       mutex;
    static std::uint64_t    previous_idle = 0;
    static std::uint64_t    previous_total = 0;
    std::lock_guard<std::mutex> lock(mutex);

    const std::uint64_t total = kernel + user;
    if (previous_total != 0 && total > previous_total && idle >= previous_idle) {
        const std::uint64_t total_delta = total - previous_total;
        const std::uint64_t idle_delta = idle - previous_idle;
        usage = clamp_percent(100.0 * static_cast<double>(total_delta - std::min(total_delta, idle_delta)) /
                              static_cast<double>(total_delta));
    } else {
        usage = 0.0;
    }

    previous_idle = idle;
    previous_total = total;
    return true;
}
#endif

static void sample_cpu(AceSystemMetrics & metrics) {
#ifdef __linux__
    metrics.cpu_available = sample_linux_cpu(metrics.cpu_usage);
#elif defined(_WIN32)
    metrics.cpu_available = sample_windows_cpu(metrics.cpu_usage);
#else
    // Keep the field unavailable on platforms where this small probe has no
    // portable implementation. The UI will render an honest unavailable
    // state rather than a guessed number.
    metrics.cpu_available = false;
#endif
    metrics.cpu_cores = std::thread::hardware_concurrency();
}

static void sample_ggml_devices(AceSystemMetrics & metrics) {
    const std::size_t device_count = ggml_backend_dev_count();
    std::size_t       selected_total = 0;

    for (std::size_t i = 0; i < device_count; ++i) {
        ggml_backend_dev_t device = ggml_backend_dev_get(i);
        if (!device) {
            continue;
        }

        const enum ggml_backend_dev_type type = ggml_backend_dev_type(device);
        if (type != GGML_BACKEND_DEVICE_TYPE_GPU && type != GGML_BACKEND_DEVICE_TYPE_IGPU) {
            continue;
        }

        metrics.gpu_available = true;
        const char * name = ggml_backend_dev_description(device);
        const char * backend = ggml_backend_dev_name(device);

        std::size_t free_memory = 0;
        std::size_t total_memory = 0;
        ggml_backend_dev_memory(device, &free_memory, &total_memory);

        // Prefer the largest reported device, which is the useful reading on
        // systems with an integrated GPU plus a discrete accelerator.
        if (total_memory >= selected_total) {
            selected_total = total_memory;
            if (name) {
                metrics.gpu_name = name;
            }
            if (backend) {
                metrics.gpu_backend = backend;
            }
            if (total_memory > 0) {
                metrics.vram_total = total_memory;
                metrics.vram_used = total_memory - std::min(total_memory, free_memory);
                metrics.vram_available = true;
            }
        }
    }
}

#ifdef __linux__
static bool read_percent_file(const std::string & path, double & value) {
    std::ifstream file(path);
    double         parsed = 0.0;
    if (!(file >> parsed)) {
        return false;
    }
    value = clamp_percent(parsed);
    return true;
}

static bool read_bytes_file(const std::string & path, std::size_t & value) {
    std::ifstream file(path);
    std::uint64_t  parsed = 0;
    if (!(file >> parsed)) {
        return false;
    }
    value = static_cast<std::size_t>(parsed);
    return true;
}

static void sample_linux_drm(AceSystemMetrics & metrics) {
    DIR * drm = opendir("/sys/class/drm");
    if (!drm) {
        return;
    }

    while (dirent * entry = readdir(drm)) {
        const std::string entry_name = entry->d_name;
        if (entry_name.size() < 5 || entry_name.compare(0, 4, "card") != 0 || entry_name.find('-') != std::string::npos) {
            continue;
        }

        const std::string device_path = "/sys/class/drm/" + entry_name + "/device/";
        double            usage = 0.0;
        if (!metrics.gpu_usage_available && read_percent_file(device_path + "gpu_busy_percent", usage)) {
            metrics.gpu_available = true;
            metrics.gpu_usage = usage;
            metrics.gpu_usage_available = true;
        }

        // This is a useful fallback for Vulkan/other backends that do not
        // report memory through ggml, notably on AMD DRM devices.
        if (!metrics.vram_available) {
            std::size_t used = 0;
            std::size_t total = 0;
            if (read_bytes_file(device_path + "mem_info_vram_used", used) &&
                read_bytes_file(device_path + "mem_info_vram_total", total) && total > 0) {
                metrics.gpu_available = true;
                metrics.vram_used = std::min(used, total);
                metrics.vram_total = total;
                metrics.vram_available = true;
            }
        }
    }

    closedir(drm);
}
#endif

static bool sample_nvidia_smi(AceSystemMetrics & metrics) {
#ifdef _WIN32
    FILE * pipe = _popen("nvidia-smi --query-gpu=utilization.gpu,memory.used,memory.total --format=csv,noheader,nounits", "r");
#else
    FILE * pipe = popen("nvidia-smi --query-gpu=utilization.gpu,memory.used,memory.total --format=csv,noheader,nounits 2>/dev/null", "r");
#endif
    if (!pipe) {
        return false;
    }

    char line[512] = {};
    const bool got_line = std::fgets(line, sizeof(line), pipe) != nullptr;
#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    if (!got_line) {
        return false;
    }

    std::stringstream values(line);
    double             usage = 0.0;
    double             used_mib = 0.0;
    double             total_mib = 0.0;
    char               comma = 0;
    if (!(values >> usage >> comma >> used_mib >> comma >> total_mib) || comma != ',') {
        return false;
    }

    if (!metrics.gpu_usage_available) {
        metrics.gpu_available = true;
        metrics.gpu_usage = clamp_percent(usage);
        metrics.gpu_usage_available = true;
    }
    if (!metrics.vram_available && total_mib > 0.0) {
        metrics.vram_total = static_cast<std::size_t>(total_mib * 1024.0 * 1024.0);
        metrics.vram_used = std::min(metrics.vram_total,
                                     static_cast<std::size_t>(std::max(0.0, used_mib) * 1024.0 * 1024.0));
        metrics.vram_available = true;
    }
    if (metrics.gpu_backend.empty()) {
        metrics.gpu_backend = "nvidia-smi";
    }
    return true;
}

} // namespace

AceSystemMetrics system_metrics_sample() {
    AceSystemMetrics metrics;
    sample_cpu(metrics);
    sample_ggml_devices(metrics);
#ifdef __linux__
    sample_linux_drm(metrics);
#endif
    if (!metrics.gpu_usage_available || !metrics.vram_available) {
        sample_nvidia_smi(metrics);
    }
    return metrics;
}
