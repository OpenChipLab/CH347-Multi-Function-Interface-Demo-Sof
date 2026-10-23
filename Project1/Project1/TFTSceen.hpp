#pragma once
#include "ch3xx.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_image.h>
#include <vector>
#include <mutex>
#include <windows.h>
#include <locale>
#include <codecvt> // 注意：C++17 后 deprecated，但多数编译器仍支持
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize.h"
#define X_OFFSET 2   // 水平方向偏移量
#define Y_OFFSET 1   // 垂直方向偏移量
#define LCD_WIDTH   128
#define LCD_HEIGHT  160
#define HIGHT 1
#define LOW   0
#define ITEM_EMPTY ("ITEM EMPTY")
class TFTScreen
{
public:
	static std::string ConvertToUtf8(const std::string& localPath) {
		// 多字节字符串 (如GBK) 转换为宽字符 (UTF-16)
		int wLen = MultiByteToWideChar(CP_ACP, 0, localPath.c_str(), -1, nullptr, 0);
		if (wLen == 0) return "";

		std::wstring wstr(wLen, 0);
		MultiByteToWideChar(CP_ACP, 0, localPath.c_str(), -1, &wstr[0], wLen);

		// 宽字符 (UTF-16) 转换为UTF-8
		int u8Len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
		if (u8Len == 0) return "";

		std::string utf8_str(u8Len - 1, 0); // -1 to exclude the null terminator
		WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &utf8_str[0], u8Len, nullptr, nullptr);

		return utf8_str;
	}

#ifdef _WIN32
	static FILE* fopen_utf8(const char* utf8_path, const char* mode)
	{
		// 将 UTF-8 string 转为 wstring (UTF-16)
		int size_needed = MultiByteToWideChar(CP_UTF8, 0, utf8_path, -1, NULL, 0);
		std::wstring wpath(size_needed - 1, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, utf8_path, -1, &wpath[0], size_needed);

		// 转换 mode 为宽字符
		size_needed = MultiByteToWideChar(CP_UTF8, 0, mode, -1, NULL, 0);
		std::wstring wmode(size_needed - 1, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, mode, -1, &wmode[0], size_needed);

		return _wfopen(wpath.c_str(), wmode.c_str());
	}
#else
	// Linux / macOS: 原生支持 UTF-8 路径
	inline FILE* fopen_utf8(const char* utf8_path, const char* mode)
	{
		return fopen(utf8_path, mode);
	}
#endif

	TFTScreen(Ch3xx& ch3xx) : m_ch3xx(ch3xx)
	{	
		m_TFT_Pic_data = (unsigned char*)malloc(LCD_WIDTH * LCD_HEIGHT * m_channel_num);
		if (m_TFT_Pic_data == NULL) {
			throw("TFTScreen malloc failed");
			return;
		}
		Init();
	}

	~TFTScreen()
	{
		m_bStop = true;
		m_bPause = true;
		m_bShowing = false;
		Nofify();
		if (m_loopshow_thread.joinable()) {
			m_loopshow_thread.join();
		}
		free(m_TFT_Pic_data);
		SDL_DestroyTexture(texture);
	}

	bool GetShowingStatus() {
		return m_bShowing;
	}

	void StartLoopShow() {
		if (m_loopshow_thread.joinable())
			return;
		m_loopshow_thread = std::thread(LoopShowThread, this);
	}

	void InterruptLoopShow(bool b_stop) {
		std::unique_lock<std::mutex> lock(m_pause_mtx);
		m_bPause = b_stop;
		m_pause_cv.notify_all();
	}

	void Nofify()
	{
		std::unique_lock<std::mutex> lock(m_pause_mtx);
		m_pause_cv.notify_all();
	}

	void Init(UINT16 color = 0xF800) {
		bool res = m_ch3xx.SpiInit();
		if (res) {
			DcResGpioInit();
			ST7735_Init();
			ST7735_FillScreen(color);  //blue
		}
	}

	void SetRenderer(SDL_Renderer* renderer) {
		m_renderer = renderer;
	}

	bool TFTLoadImage(std::string filepath) {
		bool res = false;
		int width = 0, height = 0, channels = 0;
		std::string utf8_path = ConvertToUtf8(filepath);

		char pic_path[MAX_PATH] = "";
		SDL_Surface* surf = IMG_Load(filepath.c_str());

		if (surf == NULL) {
			return false;
		}
		texture = SDL_CreateTextureFromSurface(m_renderer, surf);
		//	SDL_RenderTextureRotated	 
		SDL_DestroySurface(surf);
		strcpy(pic_path, filepath.c_str());

		FILE* f = fopen_utf8(filepath.c_str(), "rb");
		if (!f) {
			return false;
		}
		unsigned char* pic_data = stbi_load_from_file(f, &width, &height, &channels, 0);
		//unsigned char* pic_data = stbi_load(pic_path, &width, &height, &channels, 0);
		if (!pic_data) {
			fprintf(stderr, "stbi can't load image\n");
			fclose(f);
			return false;
		}
		fclose(f);
		if (channels > m_channel_num) {
			m_TFT_Pic_data = (unsigned char*)realloc(m_TFT_Pic_data, channels * LCD_WIDTH * LCD_HEIGHT);
			if (!m_TFT_Pic_data) {
				stbi_image_free(pic_data);
				throw ("TFTScreen realloc failed");
			}
			m_channel_num = channels;
		}

		int result = stbir_resize_uint8(pic_data, width, height, 0,
			m_TFT_Pic_data, LCD_WIDTH, LCD_HEIGHT, 0, channels);
		if (!result) {
			fprintf(stderr, "stbir zoomed filed\n");
			stbi_image_free(pic_data);
			return false;
		}
		stbi_image_free(pic_data);
		return true;
	}

	void TFTWritePicture() {
		uint32_t total_pixels = LCD_WIDTH * LCD_HEIGHT;

		ST7735_SetAddressWindow(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);

		std::vector<uint8_t> data;
		data.reserve(total_pixels * 2);

		for (int i = 0; i < LCD_WIDTH * LCD_HEIGHT; i++) {
			uint8_t r = m_TFT_Pic_data[i * 3 + 0];    
			uint8_t g = m_TFT_Pic_data[i * 3 + 1];    
			uint8_t b = m_TFT_Pic_data[i * 3 + 2];    

			uint16_t b5 = (b >> 3) & 0x1F; 
			uint16_t g6 = (g >> 2) & 0x3F; 
			uint16_t r5 = (r >> 3) & 0x1F; 
			// BGR565：
			uint16_t bgr565 = (b5 << 11) | (g6 << 5) | r5;
			data.push_back((bgr565 >> 8) & 0xFF);
			data.push_back(bgr565 & 0xFF);       
		}
		WriteData(data);
	}

	int GetItemsSize() {
		std::lock_guard<std::mutex> lock(m_mtx);
		return m_items.size();
	}

	std::pair<std::string, bool> GetItem(int index) {
		std::lock_guard<std::mutex> lock(m_mtx);
		if (m_items.empty() || index >= m_items.size())
			return std::make_pair(ITEM_EMPTY, false);
		return m_items.at(index);
	}

	void PushItem(std::string str, bool b) {
		std::lock_guard<std::mutex> lock(m_mtx);
		m_items.push_back(std::make_pair(str, b));
	}

	void SetItemBool(int index, bool b) {
		std::lock_guard<std::mutex> lock(m_mtx);
		if (m_items.empty() || index >= m_items.size())
			return;
		m_items.at(index).second = b;
	}

	void EraseItem(int index) {
		std::lock_guard<std::mutex> lock(m_mtx);
		if (m_items.empty() || index >= m_items.size())
			return;
		m_items.erase(m_items.begin() + index);
	}

private:
	void DcResGpioInit()
	{
		UCHAR def = (0x00 | (0x01 << res_index) | (0x01 << dc_index));
		m_gpio_current_state = m_ch3xx.GPIOInit(def, def, def);
	}

	void ControlReset(BOOL is_high)
	{
		UCHAR def = (0x00 | (0x01 << res_index) | (0x01 << dc_index));
		if (is_high) {
			m_gpio_current_state = m_gpio_current_state | (0x01 << res_index);
			m_ch3xx.GPIOSet(def, def, m_gpio_current_state);
		}
		else {
			m_gpio_current_state = m_gpio_current_state & ((0xFF - 1) << res_index);
			m_ch3xx.GPIOSet(def, def, m_gpio_current_state);
		}
	}

	void ControlDc(BOOL is_high)
	{
		UCHAR def = (0x00 | (0x01 << res_index) | (0x01 << dc_index));
		if (is_high) {
			m_gpio_current_state = m_gpio_current_state | (0x01 << dc_index);
			m_ch3xx.GPIOSet(def, def, m_gpio_current_state);
		}
		else {
			m_gpio_current_state = m_gpio_current_state & ((0xFF - 1) << dc_index);
			m_ch3xx.GPIOSet(def, def, m_gpio_current_state);
		}
	}

	void ST7735_Init()
	{
		ControlReset(0);
		Sleep(20);
		ControlReset(1);
		Sleep(120);

		WriteCommand(0x01);
		Sleep(150);

		WriteCommand(0x11); // 或使用定义 ST7735_SLPOUT
		Sleep(255);

		// 4. 设置帧率（正常模式）
		WriteCommand(0xB1); // 或 ST7735_FRMCTR1
		WriteData(0x01); // 帧率参数
		WriteData(0x2C);
		WriteData(0x2D);

		// 5. 设置帧率（空闲模式）
		WriteCommand(0xB2); // ST7735_FRMCTR2
		WriteData(0x01);
		WriteData(0x2C);
		WriteData(0x2D);

		// 6. 设置帧率（部分模式/全模式）
		WriteCommand(0xB3); // ST7735_FRMCTR3
		WriteData(0x01);
		WriteData(0x2C);
		WriteData(0x2D);
		WriteData(0x01);
		WriteData(0x2C);
		WriteData(0x2D);

		// 7. 显示反转控制
		WriteCommand(0xB4); // ST7735_INVCTR
		WriteData(0x07); // 设置无显示反转

		// 8. 电源控制1
		WriteCommand(0xC0); // ST7735_PWCTR1
		WriteData(0xA2);
		WriteData(0x02);
		WriteData(0x84);

		// 9. 电源控制2
		WriteCommand(0xC1); // ST7735_PWCTR2
		WriteData(0xC5);

		// 10. 电源控制3
		WriteCommand(0xC2); // ST7735_PWCTR3
		WriteData(0x0A);
		WriteData(0x00);

		// 11. 电源控制4
		WriteCommand(0xC3); // ST7735_PWCTR4
		WriteData(0x8A);
		WriteData(0x2A);

		// 12. 电源控制5
		WriteCommand(0xC4); // ST7735_PWCTR5
		WriteData(0x8A);
		WriteData(0xEE);

		// 13. VCOM控制
		WriteCommand(0xC5); // ST7735_VMCTR1
		WriteData(0x0E);

		// 14. 内存数据访问控制
	   // 设置颜色格式和扫描方向
		WriteCommand(0x36);
		WriteData(0xC8);

		// 15. 设置像素/接口格式为16位BGR565
		WriteCommand(0x3A); // 或 ST7735_COLMOD
		WriteData(0x05); // 16 bits per pixel

		// 16. 设置伽马校正（正极性）
		WriteCommand(0xE0);
		WriteData(0x0F); WriteData(0x1A); WriteData(0x0F);
		WriteData(0x18); WriteData(0x2F); WriteData(0x28);
		WriteData(0x20); WriteData(0x22); WriteData(0x1F);
		WriteData(0x1B); WriteData(0x23); WriteData(0x37);
		WriteData(0x00); WriteData(0x07); WriteData(0x02);
		WriteData(0x10);

		// 17. 设置伽马校正（负极性）
		WriteCommand(0xE1);
		WriteData(0x0F); WriteData(0x1B); WriteData(0x0F);
		WriteData(0x17); WriteData(0x33); WriteData(0x2C);
		WriteData(0x29); WriteData(0x2E); WriteData(0x30);
		WriteData(0x30); WriteData(0x39); WriteData(0x3F);
		WriteData(0x00); WriteData(0x07); WriteData(0x03);
		WriteData(0x10);

		// 18. 关闭休眠模式（正常模式）
		WriteCommand(0x13); // 或 ST7735_NORON
		Sleep(10);

		// 19. 开启显示
		WriteCommand(0x29); // 或 ST7735_DISPON
		Sleep(120); // 等待显示稳定
	}

	void ST7735_SetAddressWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
		// 设置列地址范围 (0x2A)
		WriteCommand(0x2A);
		WriteData(x0 + X_OFFSET >> 8);
		WriteData(x0 + X_OFFSET & 0xFF);
		WriteData(x1 + X_OFFSET >> 8);
		WriteData(x1 + X_OFFSET & 0xFF);

		// 设置行地址范围 (0x2B)
		WriteCommand(0x2B);
		WriteData(y0 + Y_OFFSET >> 8);
		WriteData(y0 + Y_OFFSET & 0xFF);
		WriteData(y1 + Y_OFFSET >> 8);
		WriteData(y1 + Y_OFFSET & 0xFF);

		// 发送存储器写命令 (0x2C)
		WriteCommand(0x2C);
	}

	void ST7735_FillScreen(uint16_t color)
	{
		uint32_t total_pixels = LCD_WIDTH * LCD_HEIGHT;

		// 设置整个屏幕为写入区域
		ST7735_SetAddressWindow(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);

		// 循环发送颜色数据
		std::vector<uint8_t> data;
		data.reserve(total_pixels * 2);

		for (uint32_t i = 0; i < total_pixels; ++i) {
			data.push_back((color >> 8) & 0xFF); // 高字节
			data.push_back(color & 0xFF);        // 低字节
		}
		WriteData(data);
	}

	void WriteCommand(UCHAR command)
	{
		if (m_gpio_dc == HIGHT) {
			ControlDc(LOW);
			m_gpio_dc = LOW;
		}
		std::vector<UCHAR> data{ command };
		m_ch3xx.WriteSpi(data);
	}

	void WriteData(std::vector<UCHAR> data)
	{
		if (m_gpio_dc == LOW) {
			ControlDc(HIGHT);
			m_gpio_dc = HIGHT;
		}
		m_ch3xx.WriteSpi(data);
	}

	void WriteData(UCHAR data)
	{
		if (m_gpio_dc == LOW) {
			ControlDc(HIGHT);
			m_gpio_dc = HIGHT;
		}
		std::vector<UCHAR> data_vect{ data };
		m_ch3xx.WriteSpi(data_vect);
	}

	static void LoopShowThread(TFTScreen* tft)
	{
		int current_loop_inter = INT_MAX;
		while (!tft->m_bStop) {
			for (int i = 0; i < tft->GetItemsSize(); i++)
			{
				std::unique_lock<std::mutex> lock(tft->m_pause_mtx);
				tft->m_pause_cv.wait_for(lock, std::chrono::milliseconds(tft->loop_inter),[&]() {
					if (tft->m_bPause)
						return false;
					if (current_loop_inter > tft->loop_inter) {
						return true;
					} else {
						return false;
					}
				});
				if (tft->m_bStop)
					return;
				current_loop_inter = tft->loop_inter;
				lock.unlock();
				// not need show
				if (!tft->GetItem(i).second) {
					continue;
				}
				tft->showing_index = i;
				tft->last_load_result = tft->TFTLoadImage(tft->GetItem(i).first);
				if (!tft->last_load_result) {
					tft->last_load_file_path = tft->GetItem(i).first;
					tft->m_items.at(i).second = false;
					continue;
				}
				tft->m_bShowing = true;
				tft->TFTWritePicture();
				tft->m_bShowing = false;
			}
		}
	}

public:
	std::atomic<SDL_Texture*> texture;
	std::atomic<int> showing_index = 0;
	std::atomic<bool> last_load_result = true;
	std::string last_load_file_path;
	std::atomic<int> loop_inter = 5000;  //5000ms change picture
	std::atomic<int> dc_index = 0;
	std::atomic<int> res_index = 1;
	
private:
	int m_channel_num = 4;
	Ch3xx& m_ch3xx;
	SDL_Renderer* m_renderer;
	UCHAR m_gpio_current_state = 0XFF;
	BOOL m_gpio_res = HIGHT; //control hardware reset     gpio4   if 0, reset
	BOOL m_gpio_dc = HIGHT;  //control data or command    gpio6   if 0, command, if 1, data
	unsigned char* m_TFT_Pic_data;
	std::thread m_loopshow_thread;
	std::mutex m_mtx;
	std::vector<std::pair<std::string, bool>> m_items;
	std::atomic<bool> m_bStop = false;
	std::atomic<bool> m_bShowing = false;
	std::condition_variable m_pause_cv;
	std::mutex m_pause_mtx;
	bool m_bPause = false;
};
