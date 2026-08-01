#pragma once

#include <cstddef>
#include <string>

// A lightweight snapshot of host resources used by the local runtime.
// Individual fields carry their own availability flag because GPU utilization
// is not exposed by every backend/platform even when VRAM is available.
struct AceSystemMetrics {
    bool        cpu_available       = false;
    double      cpu_usage           = 0.0;
    unsigned    cpu_cores           = 0;
    bool        gpu_available       = false;
    bool        gpu_usage_available = false;
    double      gpu_usage           = 0.0;
    bool        vram_available      = false;
    std::size_t vram_used           = 0;
    std::size_t vram_total          = 0;
    bool        memory_available    = false;
    double      memory_usage        = 0.0;
    std::size_t memory_used         = 0;
    std::size_t memory_total        = 0;
    std::string gpu_name;
    std::string gpu_backend;
};

AceSystemMetrics system_metrics_sample();
