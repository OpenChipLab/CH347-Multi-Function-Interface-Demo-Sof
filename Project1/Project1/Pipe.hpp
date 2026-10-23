#pragma once
#include <thread>
#include <Windows.h>
#include <mutex>

class Pipe
{
public:
    Pipe() {};
    ~Pipe() {
        if (IsThreadRunning()) {
            StopThreadProc();
        }
    }

    VOID AppendText(std::string str) {
        mtx.lock();
        strcat(text_buf, str.c_str());
        mtx.unlock();
    }

    std::string GetText() {
        mtx.lock();
        std::string str = std::string(text_buf);
        mtx.unlock();
        return str;
    }

    void ClearText() {
        mtx.lock();
        ZeroMemory(text_buf, sizeof(text_buf));
        mtx.unlock();
    }

    BOOL IsThreadRunning()
    {
        return (process_thread.joinable());
    }

    void StartThreadProc()
    {
        if (!IsThreadRunning())
            process_thread = std::thread(ThreadProc, this);
    }

    void StopThreadProc()
    {
        TerminateProcess(hProcess, 0); // 强制终止进程
        CloseHandle(hProcess);
        hProcess = NULL;
        TerminateThread(process_thread.native_handle(), 0);
        process_thread.join();
    }

private:
    static DWORD WINAPI ThreadProc(Pipe* pipe)
    {
        SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
        HANDLE hReadPipe, hWritePipe;

        // 创建匿名管道重定向输出
        CreatePipe(&hReadPipe, &hWritePipe, &sa, 0);

        STARTUPINFO si = { sizeof(si) };
        si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
        si.wShowWindow = SW_HIDE; // 隐藏控制台窗口
        si.hStdOutput = hWritePipe; // 重定向输出到管道
        si.hStdError = hWritePipe;
        PROCESS_INFORMATION pi;

        std::string cmdLine = pipe->cmd_line;

        // 启动外部进程
        if (CreateProcess(NULL, &cmdLine[0], NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
        {
            CloseHandle(pi.hThread);
            pipe->hProcess = pi.hProcess; // 保存进程句柄

            // 实时读取输出
            char buffer[4096];
            DWORD bytesRead;
            while (ReadFile(hReadPipe, buffer, sizeof(buffer), &bytesRead, NULL) && bytesRead > 0)
            {
                buffer[bytesRead] = '\0';
                pipe->mtx.lock();
                strcat(pipe->text_buf, buffer);
                pipe->mtx.unlock();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(hReadPipe);
            CloseHandle(hWritePipe);
        }
        return 0;
    }

public:
    std::string cmd_line;

private:
	std::thread process_thread;
	HANDLE hProcess;
	char text_buf[4096] = "";
	std::mutex mtx;
};