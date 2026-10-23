// Dear ImGui: standalone example application for SDL3 + SDL_Renderer
// (SDL is a cross-platform general purpose library for handling windows, inputs, OpenGL/Vulkan/Metal graphics context creation, etc.)

// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

// Important to understand: SDL_Renderer is an _optional_ component of SDL3.
// For a multi-platform app consider using e.g. SDL+DirectX on Windows and SDL+OpenGL on Linux/OSX.
#include "imgui.h"
#include "implot.h"
#include "implot_internal.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "imgui_markdown.h"
#include <windows.h>
#include <gdiplus.h>
#include <stdio.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_image.h>
#include <SDL3/SDL_system.h>
#include <commdlg.h> 
#include <future>
#include <string>
#include <atomic>
#include <sstream>
#include <vector>
#include "SimpleIni.h"
#include "sys_monitor.hpp"
#include "xvcd.hpp"
#include "openocd.hpp"
#include "ch3xx.hpp"
#include "TFTSceen.hpp"
#include "markdown.hpp"
#include "markdown_info.h"
#ifdef __EMSCRIPTEN__
#include "../libs/emscripten/emscripten_mainloop_stub.h"
#endif
#pragma comment(lib, "comdlg32.lib")
#define FONT_SIZE (13.0f)
#define INI_NAME ("Cfg.INI")
#define WIN_W 800
#define WIN_H 600
enum FILETYPE
{
    MCUFile = 0,
    PicFile,
};
enum Tab
{
    Info = 0,
    TemHum,
    Fpga,
    Mcu,
    LCD,
    //Uart,
    Settings,
};
ImVec4 g_sys_color = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
ImFont* g_tem_hum_font = NULL;

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_OPEN 1001
#define ID_TRAY_EXIT 1002

CSimpleIniA g_ini;
NOTIFYICONDATA nid = { 0 };
HWND g_hWnd = NULL;
SDL_Renderer* g_renderer;
SDL_Texture* g_icon_texture;
SDL_Texture* g_dev_status_texture = NULL;
std::atomic<bool> g_running = true;
WNDPROC g_oldWndProc = NULL;
static Tab g_tab = Tab::Info;
SystemMonitor g_sysMonitor;
Ch3xx g_ch3xx;
XVCD g_xvcd;
OPENOCD g_openocd;
TFTScreen g_tft(g_ch3xx);
MarkDown g_markdown(FONT_SIZE);
static ImGui::MarkdownConfig mdConfig;

std::string OpenFilePicker(FILETYPE type, HWND hwndOwner)
{
    OPENFILENAME ofn;
    char szFile[MAX_PATH] = { 0 };
    std::string filePath;

    ZeroMemory(&ofn, sizeof(OPENFILENAME));
    ofn.lStructSize = sizeof(OPENFILENAME);
    ofn.hwndOwner = hwndOwner;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    switch (type)
    {
    case PicFile:
        ofn.lpstrFilter =
            "PNG Files (*.png)\0*.png\0"
            "BMP Files (*.bmp)\0*.bmp\0"
            "JPEG Files (*.jpg)\0*.jpg\0"
            "All Files (*.*)\0*.*\0";
        break;
    case MCUFile:
        ofn.lpstrFilter =
            "HEX Files (*.hex)\0*.hex\0"
            "BIN Files (*.bin)\0*.bin\0"
            "ELF Files (*.elf)\0*.elf\0"
            "All Files (*.*)\0*.*\0";
        break;
    }  
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileName(&ofn))
    {
        filePath = TFTScreen::ConvertToUtf8(std::string(szFile));
    }
    return filePath;
}

// 自定义 WndProc
LRESULT CALLBACK CustomWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // 处理托盘图标消息
    if (msg == WM_TRAYICON && lParam == WM_LBUTTONUP) {
        ShowWindow(hwnd, SW_RESTORE);
        SetForegroundWindow(hwnd);
        return 0;
    }
    if (msg == WM_TRAYICON && lParam == WM_RBUTTONUP) {
        HMENU hMenu = CreatePopupMenu();
        AppendMenu(hMenu, MF_STRING, ID_TRAY_OPEN, "显示窗口");
        AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, "退出");

        POINT pt;
        GetCursorPos(&pt);
        TrackPopupMenu(hMenu, TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, NULL);
        DestroyMenu(hMenu);
        return 0;
    }

    if (msg == WM_COMMAND) {
        switch (LOWORD(wParam)) {
        case ID_TRAY_OPEN:
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
            break;
        case ID_TRAY_EXIT:
            g_running = false;
            Shell_NotifyIcon(NIM_DELETE, &nid);
            break;
        }
        return 0;
    }

    // 其他消息交给原始 WndProc
    return CallWindowProc(g_oldWndProc, hwnd, msg, wParam, lParam);
}

VOID CALLBACK Ch3xxNotify(ULONG status)
{
    if (status == Ch3xx::PLUG_OUT) {
        g_ch3xx.b_opened = false;
        g_ch3xx.StopMeasure();
        g_ch3xx.temperature = 0.0f;
        g_ch3xx.humidity = 0.0f;
        g_ch3xx.b_th_status = false;
        g_ch3xx.CloseDevice();
    }
    if (status == Ch3xx::PLUG_IN) {
        g_ch3xx.OpenDvice();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        g_ch3xx.StartMeasure();
        g_tft.Init();
    }
}

static void HelpMarker(const char* desc)
{
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip())
    {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void PreViewPicture(SDL_Texture* text, bool& bOpen)
{
    ImGui::OpenPopup(u8"图片预览");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5, 0.5));
    if (ImGui::BeginPopupModal(u8"图片预览", &bOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (text) {
            ImGui::Image(text, ImVec2(LCD_WIDTH * 1.5f, LCD_HEIGHT * 1.5f));
        }
        else {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), u8"图片无法加载!");
        }
        if (ImGui::Button(u8"关闭", ImVec2(-FLT_MIN, 30))) {
            ImGui::CloseCurrentPopup();
            SDL_DestroyTexture(text);
            bOpen = false;
        }       
        ImGui::EndPopup();
    }
}

void RenderInfoPage() {
    g_markdown.Markdown(general_overview);
}

void TemHumPage()
{
    std::string sensor_status = "已连接";
    const static std::string addr = "0x44";

    // 分两列布局
    ImGui::Columns(2, "monitor_cols", false);

    // 左侧：温度
    ImGui::BeginChild("temp_child", ImVec2(0, 200), true);
    {
        ImGui::AlignTextToFramePadding();
        ImGui::Text(u8"温度");
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f)); // 红色
        ImGui::PushFont(g_tem_hum_font);
        static float text_width = ImGui::CalcTextSize("15.02 C").x;
        static float text_htight = ImGui::CalcTextSize("15.02 C").y;
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - text_width) * 0.5f);
        ImGui::SetCursorPosY((ImGui::GetContentRegionAvail().y - text_htight) * 0.5f + 0.5f * text_htight);
        ImGui::Text(u8"%.2f ℃", g_ch3xx.temperature.load());
        ImGui::PopFont();
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();

    // 右侧：湿度
    ImGui::NextColumn();
    ImGui::BeginChild("humidity_child", ImVec2(0, 200), true);
    {
        ImGui::AlignTextToFramePadding();
        ImGui::Text(u8"湿度");
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.7f, 1.0f, 1.0f)); // 蓝色
        ImGui::PushFont(g_tem_hum_font);
        static float text_width = ImGui::CalcTextSize("15.02 %RH").x;
        static float text_htight = ImGui::CalcTextSize("15.02 C").y;
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - text_width) * 0.5f);
        ImGui::SetCursorPosY((ImGui::GetContentRegionAvail().y - text_htight) * 0.5f + 0.5f * text_htight);
        ImGui::Text(u8"%.2f %%RH", g_ch3xx.humidity.load());
        ImGui::PopFont();
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();

    // 结束分栏
    ImGui::Columns(1);

    // 底部状态栏
    //ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.12f, 0.18f, 0.8f));
    ImGui::BeginChild("status_bar", ImVec2(-FLT_MIN, 30), true);
    {
        ImGui::Text(u8" 传感器状态: ");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.8f, 0.3f, 1.0f)); // 绿色
        if (g_ch3xx.b_th_status)
            ImGui::Text(u8"已连接");
        else
            ImGui::Text(u8"未连接");
        ImGui::PopStyleColor();

        ImGui::SameLine();

        std::string i2c_text = "I2C地址:   " + addr; // 假设addr是你的I2C地址字符串
        float text_width = ImGui::CalcTextSize(i2c_text.c_str()).x;

        // 获取当前窗口的可用宽度
        float avail_width = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail_width - text_width - 2 * ImGui::GetStyle().ItemSpacing.x);

        ImGui::Text(u8" I2C地址: ");
        ImGui::SameLine();
        // 设置光标位置以达到右对齐效果
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.8f, 0.3f, 1.0f)); // 绿色
        ImGui::Text(u8"%s  ", addr.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleColor();  

    ImGui::Text(u8"采集状态设置");
    ImGui::SameLine();
    if (g_ch3xx.CheckMeasureStatus()) {
        if (ImGui::Button(u8"停止采集", ImVec2(150, 0))) {
            g_ch3xx.StopMeasure();
        }
    } else {
        if (ImGui::Button(u8"开始采集", ImVec2(150, 0))) {
            g_ch3xx.StartMeasure();
        }
    }
    ImGui::Separator();

    ImGui::Text(u8"数据采集间隔设置(按住并左右拖动下方控件)");
    float temhum_monitor_inter = g_ch3xx.lookup_inter / 1000.0f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(u8"温湿度数据采集间隔(S)").x-ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::DragFloat(u8"温湿度数据采集间隔(S)", &temhum_monitor_inter, 0.05f, 0.5f, 10.0f, "%.2f s");
    g_ch3xx.lookup_inter = temhum_monitor_inter * 1000.0f;
    g_ch3xx.Notify();
    ImGui::Separator();
}

void RenderTemHumPage() {
    // 标题栏
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.7f, 1.0f, 1.0f)); // 浅蓝文字
    ImGui::Text(u8"通过CH347的I2C接口读取SHT40温湿度传感器数据");
    ImGui::Spacing();
    ImGui::PopStyleColor();
    if (ImGui::BeginTabBar("TabBarTemHum")) {
        if (ImGui::BeginTabItem(u8" 功能介绍 ")) {
            g_markdown.Markdown(i2c_overview);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(u8" 温湿度采集 ")) {          
            TemHumPage();
            ImGui::EndTabItem();
        }       
        ImGui::EndTabBar();
    }
}

void FpgaPage()
{
    float windows_w = ImGui::GetContentRegionAvail().x;
    float windows_h = ImGui::GetContentRegionAvail().y;
    float child_h = (windows_h * 0.5f);

    // Start imgui
    ImGui::BeginChild("Child_1", ImVec2(-FLT_MIN, child_h), true);

    // 使用表格布局：2列，自动列宽
    if (ImGui::BeginTable("ConfigTable", 2, ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableSetupColumn("Labels", ImGuiTableColumnFlags_WidthFixed, 0);  // 标签列宽
        ImGui::TableSetupColumn("Inputs", ImGuiTableColumnFlags_WidthStretch, 0); // 输入列拉伸
        ImVec2 sz = ImGui::CalcTextSize("Text");
        float top_bottom_padding = (windows_h * 0.5f - 20.0f) * 0.05f;  // 顶部和底部各留 20px
        float total_usable_height = (windows_h * 0.5f - 20.0f) - 2 * top_bottom_padding;

        const int row_count = 4;
        float row_height = total_usable_height / (float)row_count;
        // 确保最小高度，防止崩溃
        row_height = max(row_height, top_bottom_padding);
        ImGui::TableNextRow(0, top_bottom_padding);
        ImGui::TableNextColumn();
        ImGui::TableNextColumn(); // 跳过两列，不绘制内容
        // 第一行：Host Address
        ImGui::TableNextRow(0, row_height); // 最小行高 40
        ImGui::TableNextColumn();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text(u8"主机地址:");
        ImGui::SameLine();
        HelpMarker(u8"Vivado中设置需与此设置保持一致");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-FLT_MIN); // 自动填满
        ImGui::InputText("##host_address", g_xvcd.host_address, IM_ARRAYSIZE(g_xvcd.host_address));

        // 第二行：Port
        ImGui::TableNextRow(0, row_height); // 最小行高 40
        ImGui::TableSetColumnIndex(0);
        ImGui::Text(u8"端口:");
        ImGui::SameLine();
        HelpMarker(u8"Vivado中设置需与此设置保持一致");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("##port", g_xvcd.port, IM_ARRAYSIZE(g_xvcd.port));

        // 第三行：Device Index
        ImGui::TableNextRow(0, row_height); // 最小行高 40
        ImGui::TableSetColumnIndex(0);
        ImGui::Text(u8"设备序号:");
        ImGui::SameLine();
        HelpMarker(u8"设备序号：0、1、2…(按插入顺序)");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-FLT_MIN);
        static int device_index_tmp = 0;
        if (ImGui::InputInt("##device_index", &device_index_tmp))
        {
            if (device_index_tmp < 0) device_index_tmp = 0;
            if (device_index_tmp > 15) device_index_tmp = 15;
            _itoa(device_index_tmp, g_xvcd.device_index, 10);
            char tmp[128];
            sprintf(tmp, "CH347 index set %d\n", device_index_tmp);
            g_xvcd.AppendText(std::string(tmp));
        }

        // 第四行：Jtag Speed
        ImGui::TableNextRow(0, row_height); // 最小行高 40;
        ImGui::TableSetColumnIndex(0);
        ImGui::Text(u8"Jtag速率:");
        ImGui::SameLine();
        HelpMarker(u8"如固化失败，尝试降低JTAG速率并重新开始");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-FLT_MIN);
        int speed_item_count = IM_ARRAYSIZE(g_xvcd.speed_items);
        if (ImGui::BeginCombo("##jtag_speed_combo", g_xvcd.speed_items[g_xvcd.selected_speed_index]))
        {
            for (int i = 0; i < speed_item_count; i++)
            {
                bool is_selected = (g_xvcd.selected_speed_index == i);
                if (ImGui::Selectable(g_xvcd.speed_items[i], is_selected))
                {
                    g_xvcd.selected_speed_index = i;
                    char tmp[128];
                    sprintf(tmp, "CH347 Clock set %s\n", g_xvcd.speed_items[i]);
                    g_xvcd.AppendText(std::string(tmp));
                }
                if (is_selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::TableNextRow(0, top_bottom_padding);
        ImGui::TableNextColumn();
        ImGui::TableNextColumn();
        ImGui::EndTable();
    }
    ImGui::EndChild();

    child_h = (int)(windows_h * 0.38f);
    ImGui::BeginChild("Child_2", ImVec2(-FLT_MIN, child_h), true, ImGuiWindowFlags_None);
    static char temp_buf[4096];
    std::string txt = g_xvcd.GetText();
    strcpy(temp_buf, txt.c_str());
    ImGui::InputTextMultiline("##log", temp_buf, sizeof(temp_buf),
        ImVec2(-FLT_MIN, -FLT_MIN),
        ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_NoHorizontalScroll);
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem(u8"清空")) {
            g_xvcd.ClearText();
        }
        ImGui::EndPopup();
    }
    ImGui::EndChild();
    if (!g_xvcd.IsThreadRunning()) {
        if (ImGui::Button(u8"开始", ImVec2(-FLT_MIN, -FLT_MIN))) {
            // pause spi loop show
            g_tft.InterruptLoopShow(true);
            while (g_tft.GetShowingStatus()) {
                Sleep(800);
            }
            g_xvcd.StartThreadProc();
        }
    }
    else {
        if (ImGui::Button(u8"停止", ImVec2(-FLT_MIN, -FLT_MIN))) {
            g_xvcd.StopThreadProc();
            g_xvcd.ClearText();
            g_tft.InterruptLoopShow(false);
        }
    }
}

void RenderFpgaPage() {
    
    if (ImGui::BeginTabBar("TabBarFpga")) {
        if (ImGui::BeginTabItem(u8" 功能介绍 ")) {
            g_markdown.Markdown(jtag_overview);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(u8" FPGA下载 ")) {
            FpgaPage();
            ImGui::EndTabItem();
        } 
        ImGui::EndTabBar();
    }
}

void McuPage()
{
    float windows_w = ImGui::GetContentRegionAvail().x;
    float windows_h = ImGui::GetContentRegionAvail().y;
    float child_h = (windows_h * 0.5f);
    static char startAdd[128] = "0x08000000";

    // Start imgui
    ImGui::BeginChild("Child_1", ImVec2(-FLT_MIN, child_h), true);

    // 使用表格布局：2列，自动列宽
    if (ImGui::BeginTable("ConfigTable", 2, ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableSetupColumn("Labels", ImGuiTableColumnFlags_WidthFixed, 0);  // 标签列宽
        ImGui::TableSetupColumn("Inputs", ImGuiTableColumnFlags_WidthStretch, 0); // 输入列拉伸
        ImVec2 sz = ImGui::CalcTextSize("Text");
        float top_bottom_padding = (windows_h * 0.5f - 20.0f) * 0.05f;  // 顶部和底部各留 20px
        float total_usable_height = (windows_h * 0.5f - 20.0f) - 2 * top_bottom_padding;

        const int row_count = 5;
        float row_height = total_usable_height / (float)row_count;
        // 确保最小高度，防止崩溃
        row_height = max(row_height, top_bottom_padding);

        ImGui::TableNextRow(0, top_bottom_padding);
        ImGui::TableNextColumn();
        ImGui::TableNextColumn(); // 跳过两列，不绘制内容

        // 第1行：Device Index
        ImGui::TableNextRow(0, row_height); // 最小行高 40
        ImGui::TableSetColumnIndex(0);
        ImGui::Text(u8"设备序号:");
        ImGui::SameLine();
        HelpMarker(u8"设备序号：0、1、2…(按插入顺序)");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-FLT_MIN);
        static int device_index_tmp = 0;
        if (ImGui::InputInt("##device_index", &device_index_tmp))
        {
            if (device_index_tmp < 0) device_index_tmp = 0;
            if (device_index_tmp > 15) device_index_tmp = 15;
            _itoa(device_index_tmp, g_openocd.device_index, 10);
            char tmp[128];
            sprintf(tmp, "CH347 index set %d\n", device_index_tmp);
            g_openocd.AppendText(std::string(tmp));
        }

        //第二行：jtag or swd
        const char* transport_select[2]{ "jtag" , "swd" };
        static int transport_select_index = 1;
        ImGui::TableNextRow(0, row_height); // 最小行高 40;
        ImGui::TableSetColumnIndex(0);
        ImGui::Text(u8"接口选择:");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##transport_select", transport_select[transport_select_index])) {
            for (size_t i = 0; i < 2; i++) {
                bool is_selected = (transport_select_index == i);
                if (ImGui::Selectable(transport_select[i], is_selected)) {
                    transport_select_index = i;
                    g_openocd.transport_select = std::string(transport_select[transport_select_index]);
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        // 第2行：Swd Speed
        ImGui::TableNextRow(0, row_height); // 最小行高 40;
        ImGui::TableSetColumnIndex(0);
        if(transport_select_index == 1)
            ImGui::Text(u8"SWD速率:");
        else
            ImGui::Text(u8"JTAG速率:");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-FLT_MIN);
        const char** speed_items = (transport_select_index == 1) ? g_openocd.swd_speed_items : g_xvcd.speed_items;
        int speed_item_count = (transport_select_index == 1) ? IM_ARRAYSIZE(g_openocd.swd_speed_items) : IM_ARRAYSIZE(g_openocd.jtag_speed_items);
        if (ImGui::BeginCombo("##speed_combo", (transport_select_index == 1) ? \
            g_openocd.swd_speed_items[g_openocd.selected_speed_index] : \
            g_openocd.jtag_speed_items[g_openocd.selected_speed_index]
        ))
        {
            for (int i = 0; i < speed_item_count; i++)
            {
                bool is_selected = (g_openocd.selected_speed_index == i);
                if (ImGui::Selectable(speed_items[i], is_selected))
                {
                    g_openocd.selected_speed_index = i;
                    char tmp[128];
                    sprintf(tmp, "CH347 Clock set %s\n", speed_items[i]);
                    g_openocd.AppendText(std::string(tmp));
                }
                if (is_selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        //第3行：Target Name xxx.cfg
        ImGui::TableNextRow(0, row_height); // 最小行高 40
        ImGui::TableSetColumnIndex(0);
        ImGui::Text(u8"目标设备:");
        ImGui::TableSetColumnIndex(1);
        static int selected_index = 0; // -1 表示未选择
        float expected_startAddr_input_x = ImGui::CalcTextSize(startAdd).x + ImGui::GetStyle().FramePadding.x * 1.0f + 50;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - expected_startAddr_input_x);
        if (ImGui::BeginCombo("##target_label", g_openocd.all_targets[selected_index].c_str(), 0)) {
            static ImGuiTextFilter filter;
            if (ImGui::IsWindowAppearing())
            {
                ImGui::SetKeyboardFocusHere();
                filter.Clear();
            }
            ImGui::SetNextItemShortcut(ImGuiMod_Ctrl | ImGuiKey_F);
            filter.Draw("##Filter", -FLT_MIN);
            for (int n = 0; n < g_openocd.all_targets.size(); n++)
            {
                const bool is_selected = (selected_index == n);
                if (filter.PassFilter(g_openocd.all_targets[n].c_str()))
                    if (ImGui::Selectable(g_openocd.all_targets[n].c_str(), is_selected)) {
                        selected_index = n;
                        g_openocd.target_cfg = g_openocd.all_targets[selected_index];
                    }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::CalcTextSize(startAdd).x + 10);
        ImGui::InputText("##start_address", startAdd, sizeof(startAdd));
        ImGui::SameLine();
        HelpMarker(u8"指定bin文件烧录的起始地址");

        // 第4行：Download file
        ImGui::TableNextRow(0, row_height); // 最小行高 40
        ImGui::TableNextColumn();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text(u8"下载文件:");
        ImGui::TableSetColumnIndex(1);
        float expected_button_x = ImGui::CalcTextSize(u8"打开烧录文件").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - expected_button_x - 3.0f * ImGui::GetStyle().FramePadding.x);
        char download_file_path[MAX_PATH] = "";
        strcpy(download_file_path, g_openocd.download_file.c_str());
        ImGui::InputText("##download_file", download_file_path, MAX_PATH);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(expected_button_x);
        if (ImGui::Button(u8"打开烧录文件")) {
            std::string str = OpenFilePicker(MCUFile, g_hWnd);
            if (!str.empty()) {
                g_openocd.download_file = str;
            }
        }

        ImGui::TableNextRow(0, top_bottom_padding);
        ImGui::TableNextColumn();
        ImGui::TableNextColumn();
        ImGui::EndTable();
    }

    ImGui::EndChild();
    static bool show_popup = false;
    child_h = (int)(windows_h * 0.38f);
    ImGui::BeginChild("Child_2", ImVec2(-FLT_MIN, child_h), true, ImGuiWindowFlags_None);
    static char temp_buf[4096];
    std::string txt = g_openocd.GetText();
    strcpy(temp_buf, txt.c_str());
    ImGui::InputTextMultiline("##log", temp_buf, sizeof(temp_buf),
        ImVec2(-FLT_MIN, -FLT_MIN),
        ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_NoHorizontalScroll);
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem(u8"清空")) {
            g_openocd.ClearText();
        }
        ImGui::EndPopup();
    }
    ImGui::EndChild();
    if (show_popup) {
        ImGui::OpenPopup(u8"警告");
    }
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(u8"警告", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text(u8"请先选择下载文件");
        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(-FLT_MIN, 0))) {
            ImGui::CloseCurrentPopup();
        }
        show_popup = false;
        ImGui::SetItemDefaultFocus();
        ImGui::EndPopup();
    }
    if (!g_openocd.IsThreadRunning()) {
        if (ImGui::Button(u8"开始", ImVec2(-FLT_MIN, -FLT_MIN))) {
            // check download file is not NULL
            if (g_openocd.download_file.empty()) {
                show_popup = true;
                return;
            }
            // pause spi loop show
            g_tft.InterruptLoopShow(true);
            while (g_tft.GetShowingStatus()) {
                Sleep(800);
            }
            g_openocd.start_address = startAdd;
            g_openocd.StartThreadProc();
        }
    }
    else {
        if (ImGui::Button(u8"停止", ImVec2(-FLT_MIN, -FLT_MIN))) {
            g_openocd.StopThreadProc();
            g_openocd.ClearText();
            g_tft.InterruptLoopShow(false);
        }
    }
}

void RenderMcuPage() {
    if (ImGui::BeginTabBar("TabBarMcu")) {       
        if (ImGui::BeginTabItem(u8" 功能介绍 ")) {
            g_markdown.Markdown(swd_overview);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(u8" MCU下载 ")) {
            McuPage();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

//void RenderUartPage() 
//{
//    ImGui::Text("Open COMTransmit:");
//    ImGui::SameLine();
//    if (ImGui::Button("COMTransmit.exe")) {
//        auto future = std::async(std::launch::async, []() {
//            char buffer[MAX_PATH];
//            GetModuleFileNameA(NULL, buffer, MAX_PATH);
//            std::string exePath(buffer);
//            std::string exeDir = exePath.substr(0, exePath.find_last_of("\\/"));
//            std::string targetExe = exeDir + "\\COMTransmit\\COMTransmit.exe";
//            std::string cmd = "cmd /c start \"\" \"" + targetExe + "\"";
//            std::system(cmd.c_str());
//            });
//    }
//}

void RenderSettingPage() 
{
    ImGui::Text(u8"程序主题设置");
     // 初始颜色设置为黑色不透明
    static bool b_usedef = true;
    if (ImGui::ColorEdit4(u8"当前程序主题色", &g_sys_color.x)) {
        ImGuiStyle& style = ImGui::GetStyle();
        style.Colors[ImGuiCol_WindowBg] = g_sys_color; // 应用到窗口背景颜色
        b_usedef = false;
    }
    ImGui::SameLine();
    ImGui::Checkbox(u8"使用默认", &b_usedef);
    if (b_usedef) {
        g_sys_color = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
        ImGuiStyle& style = ImGui::GetStyle();
        style.Colors[ImGuiCol_WindowBg] = g_sys_color; // 应用到窗口背景颜色
    }
    ImGui::Separator();
    g_markdown.Markdown(end_overview);
}

void LcdPage()
{
    static bool b_show_preview = false;
    static SDL_Texture* preview_text = NULL;

    if (b_show_preview) {
        PreViewPicture(preview_text, b_show_preview);
    }

    if (g_tft.last_load_result == false) {
        ImGui::OpenPopup(u8"警告");
    }
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(u8"警告", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        std::string info = std::string(u8"无法加载图片:\n") + g_tft.last_load_file_path + std::string(u8"\n请尝试其他图片");
        ImGui::Text(info.c_str());
        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(-FLT_MIN, 0))) {
            ImGui::CloseCurrentPopup();
        }
        g_tft.last_load_result = true;
        ImGui::SetItemDefaultFocus();
        ImGui::EndPopup();
    }
    ImVec2 space_vec = ImGui::GetContentRegionAvail();
    float left_x = (space_vec.x * 0.7) > (space_vec.y / 1.25) ? (space_vec.y / 1.25) : (space_vec.x * 0.7);
    // 开始左侧子区域    
    ImGui::BeginChild("LeftPanel", ImVec2(left_x, 0), true); // 宽度固定，高度自动填充
    {
        if (g_tft.texture) {
            ImGui::Image(g_tft.texture.load(), ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y - 20.0));
            char pic_file_path[MAX_PATH] = "";
            //std::string pic_info = TFTScreen::ConvertToUtf8(g_tft.GetItem(g_tft.showing_index).first.c_str());
            strcpy(pic_file_path, g_tft.GetItem(g_tft.showing_index).first.c_str());

            if (g_tft.GetItem(g_tft.showing_index).first != ITEM_EMPTY) {
                ImGui::Text(pic_file_path);
                if (ImGui::IsItemHovered()) // 检查当前项是否被悬停
                {
                    ImGui::BeginTooltip(); // 开始工具提示
                    ImGui::Text(pic_file_path);
                    ImGui::EndTooltip(); // 结束工具提示
                }
            }
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.7f, 1.0f, 1.0f)); // 浅蓝文字
            static float text_width = ImGui::CalcTextSize(u8"添加图片后将进行轮询播放展示").x;
            static float text_htight = ImGui::CalcTextSize(u8"添加图片后将进行轮询播放展示").y;
            ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - text_width) * 0.5f);
            ImGui::SetCursorPosY((ImGui::GetContentRegionAvail().y - text_htight) * 0.5f + 0.5f * text_htight);
            ImGui::Text(u8"添加图片将进行轮询播放展示"); 
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("RightPanel", ImVec2(0, 0), 0); // 宽度为0自动填充剩余空间
    {
        ImGui::BeginChild("Select", ImVec2(0, space_vec.y - 50), true);
        {
            ImGuiMultiSelectIO* ms_io = ImGui::BeginMultiSelect(ImGuiMultiSelectFlags_NoAutoSelect | ImGuiMultiSelectFlags_NoAutoClear | ImGuiMultiSelectFlags_ClearOnEscape, -1, g_tft.GetItemsSize());
            ImGuiSelectionExternalStorage storage_wrapper;
            storage_wrapper.UserData = (void*)(&g_tft);
            storage_wrapper.AdapterSetItemSelected = [](ImGuiSelectionExternalStorage* self, int n, bool selected) {
                auto tft = (TFTScreen*)self->UserData;
                tft->SetItemBool(n, selected);
            };
            storage_wrapper.ApplyRequests(ms_io);
            for (int n = 0; n < g_tft.GetItemsSize(); n++)
            {
                auto item = g_tft.GetItem(min(n, g_tft.GetItemsSize() - 1));
                if (item.first == "ITEM EMPTY")
                    continue;
                bool deleteItem = false;
                ImGui::PushID(n);
                ImGui::SetNextItemSelectionUserData(n);
                ImGui::Checkbox(item.first.c_str(), &item.second);
                if (ImGui::IsItemHovered()) // 检查当前项是否被悬停
                {
                    ImGui::BeginTooltip(); // 开始工具提示
                    ImGui::Text(item.first.c_str());
                    ImGui::EndTooltip(); // 结束工具提示
                }
                g_tft.SetItemBool(n, item.second);
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem(u8"预览图片")) {
                        b_show_preview = true;
                        // load texture
                        std::string u8_str = item.first.c_str();
                        SDL_Surface* surf = IMG_Load(u8_str.c_str());
                        preview_text = SDL_CreateTextureFromSurface(g_renderer, surf);
                        SDL_DestroySurface(surf);
                    }
                    if (ImGui::MenuItem(u8"移除")) {
                        deleteItem = true;
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopID();
                if (deleteItem) {
                    g_tft.EraseItem(n);
                }
            }
            ms_io = ImGui::EndMultiSelect();
            storage_wrapper.ApplyRequests(ms_io);
        }
        ImGui::EndChild();
        ImGui::BeginChild("##Other", ImVec2(0, 0), ImGuiChildFlags_None);
        {
            if (ImGui::Button(u8"添加图片", ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y))) {
                std::string str = OpenFilePicker(PicFile, g_hWnd);
                if (!str.empty()) {
                    g_tft.PushItem(str, true);
                }
            }
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
}

void LcdSettins()
{
    ImGui::Text(u8"LCD基础设置:");
    float value = g_tft.loop_inter / 1000.0f;
    ImGui::Text(u8"LCD图片轮询播放间隔(S)");
    ImGui::DragFloat("##LCD inter time", &value, 0.1f, 1.0f, 10.0f, "%.2f s");
    g_tft.loop_inter = value * 1000.0f;
    g_tft.Nofify();
    int dc_index_t = g_tft.dc_index;
    int res_index_t = g_tft.res_index;
    float width = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;   
    ImGui::Text(u8"LCD屏幕DC引脚对应CH347的GPIO");   
    ImGui::SameLine();
    ImGui::SetNextItemWidth(width - ImGui::CalcTextSize(u8"LCD屏幕DC引脚对应CH347的GPIO").x - ImGui::GetStyle().ItemSpacing.x);
    ImGui::SliderInt("##DC GPIO index", &dc_index_t, 0, 10);
    if (ImGui::BeginItemTooltip())
    {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(u8"用于设置LCD DC功能引脚\n保持与CH347硬件接线一致");
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::SameLine();
    ImGui::Text(u8"LCD屏幕RST引脚对应CH347的GPIO");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(width - ImGui::CalcTextSize(u8"LCD屏幕RST引脚对应CH347的GPIO").x - ImGui::GetStyle().ItemSpacing.x);
    ImGui::SliderInt("##RST GPIO index", &res_index_t, 0, 10);
    if (ImGui::BeginItemTooltip())
    {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(u8"用于设置LCD RST功能引脚\n保持与CH347硬件接线一致");
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    static float tft_fill_color[3] = { 1.0f, 0.0f, 0.2f };
    ImGui::Separator();
    ImGui::Text(u8"LCD填充屏幕测试：");
    ImGui::ColorEdit3(u8"填充LCD屏幕", tft_fill_color);
    ImGui::SameLine();
    HelpMarker(u8"选择颜色后点击填充LCD屏幕按钮\n用于测试LCD基本功能");
    if (ImGui::Button(u8"填充屏幕", ImVec2(-FLT_MIN, 0))) {
        UINT8 r = static_cast<Uint8>(tft_fill_color[0] * 255);
        UINT8 g = static_cast<Uint8>(tft_fill_color[1] * 255);
        UINT8 b = static_cast<Uint8>(tft_fill_color[2] * 255);
        uint16_t b5 = (b >> 3) & 0x1F;
        uint16_t g6 = (g >> 2) & 0x3F;
        uint16_t r5 = (r >> 3) & 0x1F;
        uint16_t bgr565 = (b5 << 11) | (g6 << 5) | r5;
        g_tft.Init(bgr565);
    }
    g_tft.dc_index = dc_index_t;
    g_tft.res_index = res_index_t;
    ImGui::Separator();
}

void RenderLcdPage() {
    if (ImGui::BeginTabBar("TabBarLcd")) {
        if (ImGui::BeginTabItem(u8" 功能介绍 ")) {
            g_markdown.Markdown(spi_overview);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(u8" LCD屏幕控制 ")) {
            LcdPage();
            ImGui::EndTabItem();
        }      
        if (ImGui::BeginTabItem(u8" LCD设置 ")) {
            LcdSettins();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

// 主界面绘制函数
void RenderMainWindow() {
    static ImGuiIO& io = ImGui::GetIO();
    ImGuiWindowFlags imgui_window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
    ImGui::Begin("NULL", NULL, imgui_window_flags);    
    // 创建两列：左侧菜单（固定宽度），右侧内容
    ImGui::Columns(2, "split", false); // 不显示分隔线
    ImGui::SetColumnWidth(0, 160.0f); // 左侧菜单宽度
    // ----------------------
    // 左侧菜单
    // ----------------------
    ImGui::BeginChild("left_panel", ImVec2(150, 0), true);
    ImGui::Dummy({ 0.0f, 20.0f });
    if (g_icon_texture)
        ImGui::Image(g_icon_texture, ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().x));
    else
        ImGui::Dummy({ 0.0f, ImGui::GetContentRegionAvail().x });
    ImGui::Dummy({ 0.0f, 20.0f });
    //ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Button, g_tab == Tab::Info ? ImVec4(0.2f, 0.47f, 1.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    if (ImGui::Button(u8"软件概述", { 130.0f, 40.0f }))
    {
        g_tab = Tab::Info;
    }
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Button, g_tab == Tab::TemHum ? ImVec4(0.2f, 0.47f, 1.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    if (ImGui::Button(u8"   I2C\n温湿度采集", { 130.0f, 40.0f }))
    {
        g_tab = Tab::TemHum;
    }
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Button, g_tab == Tab::Fpga ? ImVec4(0.2f, 0.47f, 1.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    if (ImGui::Button(u8"    JTAG\nFPGA下载调试", { 130.0f, 40.0f }))
    {
        g_tab = Tab::Fpga;
    }
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Button, g_tab == Tab::Mcu ? ImVec4(0.2f, 0.47f, 1.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    if (ImGui::Button(u8" JTAG/SWD\n  MCU下载", { 130.0f,40.0f }))
    {
        g_tab = Tab::Mcu;
    }
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Button, g_tab == Tab::LCD ? ImVec4(0.2f, 0.47f, 1.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    if (ImGui::Button(u8"    SPI\nLCD屏幕控制", { 130.0f, 40.0f }))
    {
        g_tab = Tab::LCD;
    }
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Button, g_tab == Tab::Settings ? ImVec4(0.2f, 0.47f, 1.0f, 1.0f) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    if (ImGui::Button(u8"设置", { 130.0f, 40.0f }))
    {
        g_tab = Tab::Settings;
    }
    ImGui::PopStyleColor();
    //ImGui::Separator();

    ImVec2 contentRegionAvail = ImGui::GetContentRegionAvail();
    if (contentRegionAvail.y - 10 - 30 > 10) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + contentRegionAvail.y - 10 - 30);

        float circleRadius = 10.0f;
        float itemHeight = ImGui::GetTextLineHeight();
        ImGui::Dummy(ImVec2(circleRadius * 2, itemHeight)); // 占位

        ImVec2 circleScreenPos = ImGui::GetItemRectMin();
        circleScreenPos.x += circleRadius;
        circleScreenPos.y += itemHeight / 2.0f;
        if (g_ch3xx.CheckDeviceOpen()) {
            auto draw_list = ImGui::GetWindowDrawList();
            draw_list->AddCircleFilled(circleScreenPos, circleRadius, IM_COL32(7, 193, 96, 255));

            ImGui::SameLine();
            //ImGui::TextColored(ImVec4(0, 1, 0, 1), "Device Online");
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
            ImGui::TextWrapped(u8"设备已插入");
            ImGui::PopStyleColor();
        }
        else {
            auto draw_list = ImGui::GetWindowDrawList();
            draw_list->AddCircleFilled(circleScreenPos, circleRadius, IM_COL32(241, 231, 38, 255));

            ImGui::SameLine();
            //ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Device Offline");
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
            ImGui::TextWrapped(u8"设备未插入");
            ImGui::PopStyleColor();
        }
    }

    ImGui::EndChild();
    ImGui::NextColumn(); // 切换到右侧

    // ----------------------
    // 右侧内容区
    // ----------------------
    ImGui::BeginChild("right_panel", ImVec2(0, 0), true);

    switch (g_tab) {
    case Info: RenderInfoPage();     break;
    case TemHum: RenderTemHumPage(); break;
    case Fpga: RenderFpgaPage(); break;
    case Mcu: RenderMcuPage();    break;
    case LCD: RenderLcdPage(); break;
    //case Uart: RenderUartPage();      break;
    case Settings: RenderSettingPage();    break;
    }

    ImGui::EndChild();

    ImGui::End(); // 结束主窗口
}

// Main code
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // Setup SDL
    // [If using SDL_MAIN_USE_CALLBACKS: all code below until the main loop starts would likely be your SDL_AppInit() function]
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        printf("Error: SDL_Init(): %s\n", SDL_GetError());
        return -1;
    }
    // Create window with SDL_Renderer graphics context
    float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    SDL_WindowFlags window_flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    SDL_Window* window = SDL_CreateWindow(u8"CH347多功能接口演示", (int)(WIN_W * main_scale), (int)(WIN_H * main_scale), window_flags);
    if (window == nullptr)
    {
        printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
        return -1;
    }
    SDL_Surface* icon_surf = IMG_Load(".\\res\\icon.png");
    if (icon_surf){
        SDL_SetWindowIcon(window, icon_surf);
        SDL_DestroySurface(icon_surf);
    }
    SDL_PropertiesID props = SDL_GetWindowProperties(window);

    // 获取 HWND 指针
    g_hWnd = (HWND)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    if (!g_hWnd) {
        SDL_Log("Can't HWND: %s", SDL_GetError());
    }
    g_oldWndProc = (WNDPROC)SetWindowLongPtr(g_hWnd, GWLP_WNDPROC, (LONG_PTR)CustomWndProc);
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = g_hWnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    //nid.hIcon = LoadIcon(GetModuleHandle(NULL), IDI_APPLICATION);
    HICON h_icon = (HICON)LoadImage(NULL, ".\\res\\icon.ico", IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE | LR_SHARED);
    if (h_icon != NULL) {
        nid.hIcon = h_icon;
    }
    else {
        fprintf(stderr, "can't load icon");
    }
    if (g_ch3xx.CheckDeviceOpen())
        strcpy_s(nid.szTip, "CH347多功能接口演示\n--设备已插入--\n");
    else
        strcpy_s(nid.szTip, "CH347多功能接口演示\n--设备未插入--\n");
    Shell_NotifyIcon(NIM_ADD, &nid);
    
    g_renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(g_renderer, 1);
    if (g_renderer == nullptr)
    {
        SDL_Log("Error: SDL_CreateRenderer(): %s\n", SDL_GetError());
        return -1;
    }
    g_tft.SetRenderer(g_renderer);
    //load icon texture
    SDL_Surface* icon_surf_winmain = IMG_Load(".\\res\\icon.png");
    if (icon_surf_winmain) {
        g_icon_texture = SDL_CreateTextureFromSurface(g_renderer, icon_surf_winmain);
        SDL_DestroySurface(icon_surf_winmain);
    }
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext(); // 初始化ImPlot
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup scaling
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
    style.FontScaleDpi = main_scale;        // Set initial font scale. (using io.ConfigDpiScaleFonts=true makes this unnecessary. We leave both here for documentation purpose)
    style.FrameRounding = 8.0f; style.ChildRounding = 8;

    // Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForSDLRenderer(window, g_renderer);
    ImGui_ImplSDLRenderer3_Init(g_renderer);

    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return a nullptr. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype for higher quality font rendering.
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    // - Our Emscripten build process allows embedding fonts to be accessible at runtime from the "fonts/" folder. See Makefile.emscripten for details.
    style.FontSizeBase = FONT_SIZE;
    //io.Fonts->AddFontDefault();
    //io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf");
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf");
    //IM_ASSERT(font != nullptr);

    io.Fonts->AddFontFromFileTTF("c:/windows/fonts/simhei.ttf");
    g_tem_hum_font = io.Fonts->AddFontFromFileTTF("c:/windows/fonts/simhei.ttf", 45.0f);
    
    g_markdown.LoadFonts(std::string("c:/windows/fonts/simhei.ttf"));
    // Our state
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    //get pre setting
    const char* image_path = "image path";
    const char* image_bool = "image bool";
    const char* system_color = "system color";
    const char* measure_inter = "measure_inter";
    const char* loop_show_inter = "loop show inter";
    const char* net_name = "net card";
    const char* tft_gpio = "tft gpio";
    g_ini.SetUnicode(true);
    g_ini.SetMultiKey();
    // reload pre user setting
    CSimpleIni::TNamesDepend path_values;
    CSimpleIni::TNamesDepend bool_values;
    CSimpleIni ini_read;
    ini_read.SetMultiKey();
    ini_read.LoadFile(INI_NAME);
    ini_read.GetAllValues(image_path, "path", path_values);
    ini_read.GetAllValues(image_bool, "boolen", bool_values);
    const char* sys_color_str = ini_read.GetValue(system_color, "RGBA");
    const char* loop_show_inter_str = ini_read.GetValue(loop_show_inter, "time");
    const char* measure_inter_str = ini_read.GetValue(measure_inter, "time");
    //const char* net_str = ini_read.GetValue(net_name, "name");
    const char* cs_str = ini_read.GetValue(tft_gpio, "cs");
    const char* res_str = ini_read.GetValue(tft_gpio, "res");
    if (cs_str && res_str) {
        g_tft.dc_index = atoi(cs_str);
        g_tft.res_index = atoi(res_str);
        g_tft.Init();
    }
    if (sys_color_str) {
        std::istringstream iss(sys_color_str);
        std::vector<std::string> sys_color;
        std::string token;
        while (iss >> token) {
            sys_color.push_back(token);
        }
        if (sys_color.size() == 4) {
            g_sys_color.x = atof(sys_color[0].c_str());
            g_sys_color.y = atof(sys_color[1].c_str());
            g_sys_color.z = atof(sys_color[2].c_str());
            g_sys_color.w = atof(sys_color[3].c_str());
        }
    }
    if (loop_show_inter_str) {
        g_tft.loop_inter = atoi(loop_show_inter_str);
    }
    if (measure_inter_str) {
        g_ch3xx.lookup_inter = atoi(measure_inter_str);
    }
    /*if (net_str) {
        auto vect = g_sysMonitor.EnumNetworkInterfaces();
        if (!vect.empty())
            if (std::find(vect.begin(), vect.end(), net_str) == vect.end()) {
                g_sysMonitor.net_name = vect[0];
            }
            else {
                g_sysMonitor.net_name = net_str;
            }
        else
            g_sysMonitor.net_name = g_sysMonitor.net_name_default;
        g_sysMonitor.ReInitialize();
    }*/

    if (path_values.size() == bool_values.size()) {
        int index = 0;
        for (const auto& val : path_values) {
            g_tft.PushItem(std::string(val.pItem), true);
        }
        for (const auto& val : bool_values) {
            g_tft.SetItemBool(index, ((strcmp(val.pItem, "TRUE") == 0) ? true : false));
            index++;
        }
    }
    // clear ini
    CSimpleIni ini_tmp;
    ini_tmp.SaveFile(INI_NAME);

    // Main loop
#ifdef __EMSCRIPTEN__
    // For an Emscripten build we are disabling file-system access, so let's not attempt to do a fopen() of the imgui.ini file.
    // You may manually call LoadIniSettingsFromMemory() to load settings from your own storage.
    io.IniFilename = nullptr;
    EMSCRIPTEN_MAINLOOP_BEGIN
#else
    if (g_sysMonitor.StartMonitor() == false) {
        SDL_Log("Error: StartMonitor failed\n");
        return -1;
    }
    g_ch3xx.SetNotifyRoutine(Ch3xxNotify);
    if (g_ch3xx.CheckDeviceOpen()) {
        g_ch3xx.StartMeasure();
    }
    g_tft.StartLoopShow();
    // applicate system color
    style.Colors[ImGuiCol_WindowBg] = g_sys_color;
    while (g_running)
#endif
    {
        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        // [If using SDL_MAIN_USE_CALLBACKS: call ImGui_ImplSDL3_ProcessEvent() from your SDL_AppEvent() function]
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) {
                g_running = true;
                SDL_HideWindow(window);
            }
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))
                g_running = false;
            if (event.type == SDL_EVENT_DROP_FILE) {
                if (g_tab == Tab::Mcu) {
                    const char* droppedFile = event.drop.data;
                    if (droppedFile) {
                        g_openocd.download_file = droppedFile;
                    }
                }
                if (g_tab == Tab::LCD) {
                    const char* droppedFile = event.drop.data;
                    if (droppedFile) {
                        g_tft.PushItem(std::string(droppedFile), true);
                    }
                }
            }
        }
        // [If using SDL_MAIN_USE_CALLBACKS: all code below would likely be your SDL_AppIterate() function]
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
        {
            SDL_Delay(100);
            continue;
        }
        // Start the Dear ImGui frame
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        static int windows_w = 0;
        static int windows_h = 0;
        SDL_GetWindowSize(window, &windows_w, &windows_h);
        //ImGui::SetNextWindowSize(ImVec2(windows_w, windows_h));
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(windows_w), static_cast<float>(windows_h)));

        ImGui::SetNextWindowPos(ImVec2(0, 0));

        RenderMainWindow();
        // Rendering
        ImGui::Render();
        SDL_SetRenderScale(g_renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColorFloat(g_renderer, clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        SDL_RenderClear(g_renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), g_renderer);
        SDL_RenderPresent(g_renderer);
    }
#ifdef __EMSCRIPTEN__
    EMSCRIPTEN_MAINLOOP_END;
#endif
    //save spi tft image path
    for (int i = 0; i < g_tft.GetItemsSize(); i++) {
        auto value = g_tft.GetItem(i);
        g_ini.SetValue(image_path, "path", value.first.c_str());
        if (value.second)
            g_ini.SetValue(image_bool, "boolen", std::string("TRUE").c_str());
        else
            g_ini.SetValue(image_bool, "boolen", std::string("FALSE").c_str());
    }
    std::string color = std::to_string(g_sys_color.x) + std::string(" ")\
        + std::to_string(g_sys_color.y) + std::string(" ")\
        + std::to_string(g_sys_color.z) + std::string(" ")\
        + std::to_string(g_sys_color.w);
    g_ini.SetValue(system_color, "RGBA", color.c_str());
    g_ini.SetValue(loop_show_inter, "time", std::to_string(g_tft.loop_inter).c_str());
    g_ini.SetValue(measure_inter, "time", std::to_string(g_ch3xx.lookup_inter).c_str());
    //g_ini.SetValue(net_name, "name", g_sysMonitor.net_name.c_str());
    g_ini.SetValue(tft_gpio, "dc", std::to_string(g_tft.dc_index).c_str());
    g_ini.SetValue(tft_gpio, "res", std::to_string(g_tft.res_index).c_str());
    g_ini.SaveFile(INI_NAME);
    // Cleanup
    // [If using SDL_MAIN_USE_CALLBACKS: all code below would likely be your SDL_AppQuit() function]
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    if (g_icon_texture) {
        SDL_DestroyTexture(g_icon_texture);
    }
    SDL_DestroyRenderer(g_renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
