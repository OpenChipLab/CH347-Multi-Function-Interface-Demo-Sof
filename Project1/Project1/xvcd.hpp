#include "Pipe.hpp"
class XVCD : public Pipe {
public:
    XVCD() {
    }

    ~XVCD() {
    }

    void StartThreadProc() {
        cmd_line = ".\\CH347_XVCD_GUI\\ch347_xvcd.exe -a " + std::string(host_address) +
            " -p " + port +
            " -i " + std::string(device_index) +
            " -s " + std::to_string(speed[selected_speed_index]);
        Pipe::StartThreadProc();  //我要调用父类的这个函数
    }

public:
    char host_address[32] = "127.0.0.1";
    char port[16] = "2542";
    char device_index[8] = "0";
    
    int selected_speed_index = 3;
    const char* speed_items[8] = {
        "468.75KHZ",  // 速度0
        "937.5KHZ",   // 速度1
        "1.875MHZ",   // 速度2
        "3.75MHZ",    // 速度3
        "7.5MHZ",     // 速度4
        "15MHZ",      // 速度5
        "30MHZ",      // 速度6
        "60MHZ"       // 速度7
    };
    const int speed[8] = {
        468750,      // 468.75 KHz = 468.75 * 1000 = 468,750 Hz
        937500,      // 937.5  KHz = 937.5  * 1000 = 937,500 Hz
        1875000,     // 1.875  MHz = 1.875  * 1e6  = 1,875,000 Hz
        3750000,     // 3.75   MHz = 3.75   * 1e6  = 3,750,000 Hz
        7500000,     // 7.5    MHz = 7.5    * 1e6  = 7,500,000 Hz
        15000000,    // 15     MHz = 15     * 1e6  = 15,000,000 Hz
        30000000,    // 30     MHz = 30     * 1e6  = 30,000,000 Hz
        60000000     // 60     MHz = 60     * 1e6  = 60,000,000 Hz   
    };
};
