#pragma once
#include <Windows.h>
#include "CH347DLL.H"
#include <atomic>
#include <thread>
#include <shared_mutex>
#pragma comment(lib, "CH347DLL.lib")
#define SHT40_ADDR 0x44
#define SHT40_COMMAND 0xfd

//typedef VOID(CALLBACK *mPCH347_NOTIFY_ROUTINE)( // 设备插拔通知事件回调程序
//ULONG iEventStatus);                        // 设备插拔事件和当前状态(在下行定义): 0=设备拔出事件, 3=设备插入事件
class Ch3xx
{
public:
	Ch3xx() :m_devIndex(0), m_hDev(INVALID_HANDLE_VALUE), m_bMeasure(false), lookup_inter(500), b_opened(false)
	{
		temperature_vec.resize(60, 0.0f);
		humidity_vec.resize(60, 0.0f);
		OpenDvice();
	}

	~Ch3xx()
	{
		StopMeasure();
		CloseDevice();
	}

	void CloseDevice() {
		CH347CloseDevice(m_devIndex);
	}

	void OpenDvice() {
		if (b_opened) return;
		m_hDev = CH347OpenDevice(m_devIndex);
		if (m_hDev == INVALID_HANDLE_VALUE) {
			b_opened = false;
		}
		else {
			b_opened = true;
			CH347SetTimeout(m_devIndex, 1000, 1000);
			CH347I2C_Set(m_devIndex, 1);
		}
	}

	void SetLookupInter(int ms) {
		lookup_inter = ms;
	}

	bool CheckDeviceOpen() {
		return b_opened;
	}

	bool CheckMeasureStatus() {
		return m_measureThread.joinable();
	}

	bool SetNotifyRoutine(mPCH347_NOTIFY_ROUTINE fun) {
		const char* s = "VID_1A86&PID_55";
		return CH347SetDeviceNotify(m_devIndex, (PCHAR)s, fun);
	}

	bool StartMeasure() {
		m_bMeasure = true;
		if (m_measureThread.joinable()) {
			return true;
		} else {
			m_measureThread = std::thread(MeasureThread, this);
			if (!m_measureThread.joinable()) {
				return false;
			}
		}
		return true;
	}

	void StopMeasure() {
		m_bMeasure = false;
		Notify();
		if (m_measureThread.joinable()) {
			m_measureThread.join();
		}
	}

	std::vector<float> GetTemperVect()
	{
		std::shared_lock<std::shared_mutex> lock(m_mtx);
		return temperature_vec;
	}

	std::vector<float> GetHumiVect()
	{
		std::shared_lock<std::shared_mutex> lock(m_mtx);
		return humidity_vec;
	}

	UCHAR GPIOInit(UCHAR enable, UCHAR dir, UCHAR data) {
		UCHAR state;
		CH347GPIO_Set(m_devIndex, enable, dir, data);
		CH347GPIO_Get(m_devIndex, &dir, &state);
		return state;
	}

	void GPIOSet(UCHAR enable, UCHAR dir, UCHAR data) {
		CH347GPIO_Set(m_devIndex, enable, dir, data);
	}

	bool SpiInit() {
		mSpiCfgS spi_cfg;
		spi_cfg.iMode = 3;
		spi_cfg.iClock = 1;     //30M
		spi_cfg.iByteOrder = 1;   //MSB
		spi_cfg.iSpiWriteReadInterval = 0;
		spi_cfg.iSpiOutDefaultData = 0xFF;
		spi_cfg.iChipSelect = 0x0080;
		spi_cfg.CS1Polarity = 0;
		spi_cfg.CS2Polarity = 0;
		spi_cfg.iIsAutoDeativeCS = 0;
		spi_cfg.iActiveDelay = 0;
		spi_cfg.iDelayDeactive = 0;

		if (CH347SPI_Init(m_devIndex, &spi_cfg) == FALSE) {
			return false;
		}
	}

	void WriteSpi(std::vector<UCHAR> data)
	{
		CH347SPI_Write(m_devIndex, 0x80, data.size(), 128, data.data());
	}

	void Notify()
	{
		std::unique_lock<std::mutex> cv_lock(m_cv_mtx);
		m_cv.notify_all();
	}

private:
	static void MeasureThread(Ch3xx* dev) {
		int current_inter = INT_MAX;
		BOOL ret = FALSE;
		unsigned char writedata[2]{ SHT40_ADDR << 1, SHT40_COMMAND };
		unsigned char readdata[10];
		uint16_t temp_raw = 0;
		uint16_t rh_raw = 0;
		ULONG sht40_sensor = 0;
		while (dev->m_bMeasure && dev->b_opened) {
			std::unique_lock<std::mutex> cv_lock(dev->m_cv_mtx);
			dev->m_cv.wait_for(cv_lock, std::chrono::milliseconds(dev->lookup_inter), [&]() {
				if (current_inter > dev->lookup_inter) {
					return true;
				} else {
					return false;
				}
				});
			cv_lock.unlock();
			if (!dev->m_bMeasure)
				return;
			ret = CH347StreamI2C_RetACK(dev->m_devIndex, 2, writedata, 0, NULL, &sht40_sensor);
			if (ret == FALSE) {
				fprintf(stderr, "write faild\n");
				dev->b_th_status = false;
				return;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			ret = CH347StreamI2C_RetACK(dev->m_devIndex, 1, writedata, 6, readdata, &sht40_sensor);
			if (ret == FALSE) {
				fprintf(stderr, "read faild\n");
				dev->b_th_status = false;
				return;
			}
			if (sht40_sensor == 0) {
				dev->b_th_status = false;
			} else {
				dev->b_th_status = true;
			}
			temp_raw = (readdata[0] << 8) | readdata[1];
			rh_raw = (readdata[3] << 8) | readdata[4];

			dev->temperature = -45.0f + 175.0f * temp_raw / 65535.0f;
			dev->humidity = -6.0f + 125.0f * rh_raw / 65535.0f;

			std::unique_lock<std::shared_mutex> lock(dev->m_mtx);
			for (size_t i = 0; i < dev->m_monitor_range_size - 1; i++) {
				dev->humidity_vec[i] = dev->humidity_vec[i + 1];
				dev->temperature_vec[i] = dev->temperature_vec[i + 1];
			}
			dev->temperature_vec[dev->m_monitor_range_size - 1] = dev->temperature;
			dev->humidity_vec[dev->m_monitor_range_size - 1] = dev->humidity;
			lock.unlock();
			current_inter = dev->lookup_inter;
		}
	}

public:
	enum DeviceStatus
	{
		PLUG_OUT = 0,
		PLUG_IN = 3,
	};

	std::atomic<float> temperature;
	std::atomic<float> humidity;
	std::atomic<int> lookup_inter;
	std::atomic<bool> b_opened;
	std::vector<float> temperature_vec;
	std::vector<float> humidity_vec;
	std::atomic<bool> b_th_status = false;

private:
	int m_devIndex;
	HANDLE m_hDev;
	std::atomic<bool> m_bMeasure;
	std::thread m_measureThread;
	const int m_monitor_range_size = 60;
	std::shared_mutex m_mtx;
	std::mutex m_cv_mtx;
	std::condition_variable m_cv;
};
