#include <windows.h>
#include <pdh.h>
#include <string>
#include <atomic>
#include <thread>
#include <iostream>
#include <shared_mutex>
#include <deque>
#include "CH347DLL.H"
#include <PdhMsg.h>
#pragma comment(lib, "pdh.lib")
#pragma comment(lib ,"CH347DLL.lib")

class SystemMonitor {


public:
    SystemMonitor() : m_query(NULL), m_cpuCounter(NULL), m_memCounter(NULL), m_netCounter(NULL), m_initialized(false), lookup_inter(1000), \
        m_bMonitorThread(false)
    {
        CPU_UseRate_vec.resize(m_onitor_range_size, 0);
        MEM_UseRate_vec.resize(m_onitor_range_size, 0);
        MEM_UsedGB_vec.resize(m_onitor_range_size, 0);
        NET_Speed_vec.resize(m_onitor_range_size, 0);
        Initialize();
    }

    ~SystemMonitor() {
        Cleanup();
        stopMonitor();
    }

    bool CheckMonitorStatus() {
        return m_monitorThread.joinable();
    }

    std::vector<float> GetCpuUsedVect() {
        std::shared_lock<std::shared_mutex> lock(m_mtx);
        return CPU_UseRate_vec;
    }

    std::vector<float> GetMemUsedVect() {
        std::shared_lock<std::shared_mutex> lock(m_mtx);
        return MEM_UseRate_vec;
    }

    std::vector<float> GetMemGBUsedVect() {
        std::shared_lock<std::shared_mutex> lock(m_mtx);
        return MEM_UsedGB_vec;
    }

    std::vector<float> GetNetSpeedVect() {
        std::shared_lock<std::shared_mutex> lock(m_mtx);
        return NET_Speed_vec;
    }

    void SetLookupInter(int ms) {
        lookup_inter = ms;
    }

    bool StartMonitor() {
        m_bMonitorThread = true;
        if (m_monitorThread.joinable())
            return true;
        else {
            m_monitorThread = std::thread(MonitorThreadFun, this);
            if (m_monitorThread.joinable()) {
                return true;
            }
            else {
                return false;
            }
        }
    }

    void stopMonitor() {
        m_bMonitorThread = false;
        if (m_monitorThread.joinable()) {
            m_monitorThread.join();
        }
    }

    std::vector<std::string> EnumNetworkInterfaces() {
        std::vector<std::string> interfaces;
        DWORD bufferSize = 0;        // 注意：是缓冲区大小（字符数）
        DWORD instanceCount = 0;     // 实例数量

        // 第一次调用：获取所需缓冲区大小和实例数量
        PDH_STATUS status = PdhEnumObjectItemsA(
            nullptr,                    // szDataSource
            nullptr,                    // szMachineName
            "Network Interface",        // szObjectName
            nullptr, &bufferSize,                 // mszCounterList, pcCounterListLength
            nullptr, &instanceCount,       // mszInstanceList, pcInstanceListLength → 输出所需大小
            PERF_DETAIL_WIZARD,             // pcInstanceListLength 实际上也输出实例数量
            0         // dwDetailLevel
        );

        if (status != PDH_MORE_DATA) {
            std::cout << "第一次枚举失败，错误码: " << status << "\n";
            return interfaces; // 返回空
        }
        std::vector<char> instanceBuffer(bufferSize);
        std::vector<char> counterBuffer(bufferSize);

        // 此时 bufferSize 是所需缓冲区大小（字符数），instanceCount 是实例数量

        if (bufferSize == 0 || instanceCount == 0) {
            std::cout << "未检测到任何网络接口。\n";
            return interfaces;
        }
        // 分配缓冲区（注意：是字符数组，不是字符串数组）
        std::vector<char> buffer(bufferSize);

        // 第二次调用：填充数据
        status = PdhEnumObjectItemsA(
            nullptr,
            nullptr,
            "Network Interface",
            counterBuffer.data(),  // 计数器列表（不需要可传 nullptr）
            &bufferSize,
            instanceBuffer.data(), // 实例列表（网卡名称）
            &instanceCount,
            PERF_DETAIL_WIZARD,
            0
        );

        if (status != ERROR_SUCCESS) {
            return interfaces;
        }
        // 4. 解析实例列表（网卡名称）
        LPSTR currentInstance = instanceBuffer.data();
        for (DWORD i = 0; i < instanceCount; i++) {
            if (currentInstance[0] != '\0') {
                interfaces.push_back(std::string(currentInstance));
            }               
            currentInstance += strlen(currentInstance) + 1;  // 移动到下一个实例           
        }

        return interfaces;
    }

    bool ReInitialize() {
        std::string name = (std::string("\\Network Interface(") + net_name + std::string(")"));
        name = name + std::string("\\Bytes Received/sec");
        PDH_STATUS status = PdhAddCounter(m_query, name.c_str(), 0, &m_netCounter);
        if (status != ERROR_SUCCESS) {
            if (m_netCounter) {
                PdhRemoveCounter(m_netCounter);
                m_netCounter = NULL;
            }
            return false;
        } else {
            return true;
        }
        return true;
    }

private:
    // 初始化PDH查询和计数器
    bool Initialize() {
        if (m_initialized) return true; // 避免重复初始化

        // 打开查询
        PDH_STATUS status = PdhOpenQuery(NULL, 0, &m_query);
        if (status != ERROR_SUCCESS) {
            Cleanup();
            return false;
        }

        // 添加CPU使用率计数器 (所有核心的总和)
        status = PdhAddCounter(m_query, "\\Processor(_Total)\\% Processor Time", 0, &m_cpuCounter);
        if (status != ERROR_SUCCESS) {
            Cleanup();
            return false;
        }

        // 添加内存使用率计数器
        status = PdhAddCounter(m_query, "\\Memory\\% Committed Bytes In Use", 0, &m_memCounter);
        if (status != ERROR_SUCCESS) {
            Cleanup();
            return false;
        }

        auto v = EnumNetworkInterfaces();
        if (!v.empty()) {
            if (std::find(v.begin(), v.end(), net_name_default) != v.end()) {
                net_name = net_name_default;
            }
            else {
                net_name = v[0];
            }
        }
        else {
            net_name = net_name_default;
        }
        std::string name = (std::string("\\Network Interface(") + net_name + std::string(")"));
        //添加网口网速计数器
        // \\Network Interface(Intel[R] Ethernet Connection [14] I219-LM)\\Bytes Received/sec
        // \\Network Interface(Intel[R] Wi-Fi 6 AX200 160MHz)\\Bytes Received/sec
        name = name + std::string("\\Bytes Received/sec");
        status = PdhAddCounter(m_query, name.c_str(), 0, &m_netCounter);
        if (status != ERROR_SUCCESS) {
            Cleanup();
            return false;
        }

        m_initialized = true;
        return true;
    }

    // 清理资源
    void Cleanup() {
        if (m_cpuCounter) {
            PdhRemoveCounter(m_cpuCounter);
            m_cpuCounter = NULL;
        }
        if (m_memCounter) {
            PdhRemoveCounter(m_memCounter);
            m_memCounter = NULL;
        }
        if (m_netCounter) {
            PdhRemoveCounter(m_netCounter);
            m_netCounter = NULL;
        }
        if (m_query) {
            PdhCloseQuery(m_query);
            m_query = NULL;
        }
        m_initialized = false;
    }

    static void MonitorThreadFun(SystemMonitor* sys) {
        if (sys->Initialize() == false) {
            throw "System monitor Initialize failed";
            return;
        }
        sys->GetCPULoad();
        sys->GetMemoryUsage();
        sys->GetMemoryUsageGB();
        sys->GetNetSpeed();

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        while (sys->m_bMonitorThread) {
            sys->GetCPULoad();
            sys->GetMemoryUsage();
            sys->GetMemoryUsageGB();
            sys->GetNetSpeed();
            std::unique_lock<std::shared_mutex> lock(sys->m_mtx);

            for (int i = 0; i < sys->m_onitor_range_size - 1; ++i) {
                sys->CPU_UseRate_vec[i] = sys->CPU_UseRate_vec[i + 1];
                sys->MEM_UseRate_vec[i] = sys->MEM_UseRate_vec[i + 1];
                sys->MEM_UsedGB_vec[i] = sys->MEM_UsedGB_vec[i + 1];
                sys->NET_Speed_vec[i] = sys->NET_Speed_vec[i + 1];
            }
            sys->CPU_UseRate_vec[sys->m_onitor_range_size - 1] = sys->CPU_UseRate;
            sys->MEM_UseRate_vec[sys->m_onitor_range_size - 1] = sys->MEM_UseRate;
            sys->MEM_UsedGB_vec[sys->m_onitor_range_size - 1] = sys->MEM_UsedGB;
            sys->NET_Speed_vec[sys->m_onitor_range_size - 1] = sys->NET_Speed;
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(sys->lookup_inter));
        }
        return;
    }

    // 获取CPU使用率 (%)
    // 注意：首次调用或长时间未调用后，可能需要两次Collect才能获得有效值
    void GetCPULoad() {
        if (!m_initialized || !m_query || !m_cpuCounter) return;

        PDH_FMT_COUNTERVALUE value;
        PdhCollectQueryData(m_query); // 收集数据
        PDH_STATUS status = PdhGetFormattedCounterValue(m_cpuCounter, PDH_FMT_DOUBLE, NULL, &value);

        if (status != ERROR_SUCCESS) CPU_UseRate = -1.0;
        CPU_UseRate = value.doubleValue;
    }

    // 获取内存使用率 (%)
    void GetMemoryUsage() {
        if (!m_initialized || !m_query || !m_memCounter) return;

        PDH_FMT_COUNTERVALUE value;
        PdhCollectQueryData(m_query); // 收集数据
        PDH_STATUS status = PdhGetFormattedCounterValue(m_memCounter, PDH_FMT_DOUBLE, NULL, &value);

        if (status != ERROR_SUCCESS) MEM_UseRate = -1.0;
        MEM_UseRate = value.doubleValue;
    }

    void GetNetSpeed() {
        static std::deque<double> g_speedHistory;
        const int WINDOW_SIZE = 5;
        if (!m_initialized || !m_query || !m_netCounter) return;

        PDH_FMT_COUNTERVALUE value;
        PDH_STATUS status = PdhCollectQueryData(m_query);
        if (status != ERROR_SUCCESS) {
            OutputDebugStringA("PdhCollectQueryData failed!\n");
            return;
        }

        status = PdhGetFormattedCounterValue(m_netCounter, PDH_FMT_DOUBLE, NULL, &value);
        if (status != ERROR_SUCCESS ||
            (value.CStatus != PDH_CSTATUS_VALID_DATA && value.CStatus != PDH_CSTATUS_NEW_DATA)) {
            double lastSpeed = g_speedHistory.empty() ? 0.0 : g_speedHistory.back();
            g_speedHistory.push_back(lastSpeed * 0.8);
        }
        else {
            double bytesPerSec = value.doubleValue;
            double kbps = (bytesPerSec * 8.0) / 1024.0;
            g_speedHistory.push_back(kbps);
        }
        while (g_speedHistory.size() > WINDOW_SIZE) {
            g_speedHistory.pop_front();
        }

        // 计算平均值
        double sum = 0.0;
        for (double speed : g_speedHistory) {
            sum += speed;
        }
        NET_Speed = g_speedHistory.empty() ? 0.0 : sum / g_speedHistory.size();
    }

    // 获取内存使用率 (以GB为单位)
    // 需要结合内存总量计算
    void GetMemoryUsageGB() {
        MEMORYSTATUSEX statex;
        statex.dwLength = sizeof(statex);
        if (GlobalMemoryStatusEx(&statex)) {
            // 已使用内存 = 总内存 - 可用内存
            ULONGLONG usedBytes = statex.ullTotalPhys - statex.ullAvailPhys;
            MEM_UsedGB = static_cast<double>(usedBytes) / (1024.0 * 1024.0 * 1024.0);
        }
        else {
            MEM_UsedGB = -1.0;
        }
    }

private:
        PDH_HQUERY    m_query;        // 性能查询句柄
        PDH_HCOUNTER  m_cpuCounter;   // CPU计数器句柄
        PDH_HCOUNTER  m_memCounter;   // 内存计数器句柄
        PDH_HCOUNTER  m_netCounter;   // 网络计数器句柄
        bool          m_initialized;  // 初始化状态
        std::thread m_monitorThread;
        std::atomic<bool> m_bMonitorThread;
        mutable std::shared_mutex m_mtx;
        const int m_onitor_range_size = 60;
public:
    std::atomic<double> CPU_UseRate;
    std::atomic<double> MEM_UseRate;
    std::atomic<double> MEM_UsedGB;
    std::atomic<double> NET_Speed;  // unit:KB/S
    std::vector<float> CPU_UseRate_vec;
    std::vector<float> MEM_UseRate_vec;
    std::vector<float> MEM_UsedGB_vec;
    std::vector<float> NET_Speed_vec;
    std::atomic<int> lookup_inter;  //ms 
    const std::string net_name_default = "Intel[R] Ethernet Connection [14] I219-LM";
    // CH339W net card default name
    std::string net_name = net_name_default;
};