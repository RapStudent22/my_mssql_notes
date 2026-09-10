// dllmain.cpp : Определяет точку входа для приложения DLL.
#include "pch.h" 
#include <windows.h>
#include <srv.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "opends60.lib")

#ifndef XP_NOERROR
#define XP_NOERROR 0
#endif
#ifndef XP_ERROR
#define XP_ERROR 1
#endif

extern "C" {

    __declspec(dllexport) ULONG WINAPI __GetXpVersion() {
        return ODS_VERSION;
    }

    __declspec(dllexport) SRVRETCODE WINAPI xp_run_cmd(SRV_PROC* srvproc) {

        // 1. Проверка количества параметров
        if (srv_rpcparams(srvproc) != 1) {
            srv_sendmsg(srvproc, SRV_MSG_ERROR, 0, 0, 0, NULL, 0, 0,
                (char*)"ERROR: Exactly one parameter (the command) is required.", SRV_NULLTERM);
            srv_senddone(srvproc, SRV_DONE_ERROR, 0, 0);
            return XP_ERROR;
        }

        // 2. Получение параметра (упрощённо, без проверки типа)
        BYTE* pbData = (BYTE*)srv_paramdata(srvproc, 1);
        int len = srv_paramlen(srvproc, 1);

        if (pbData == NULL || len <= 0) {
            srv_sendmsg(srvproc, SRV_MSG_ERROR, 0, 0, 0, NULL, 0, 0,
                (char*)"ERROR: Invalid parameter data.", SRV_NULLTERM);
            srv_senddone(srvproc, SRV_DONE_ERROR, 0, 0);
            return XP_ERROR;
        }

        // 3. Копируем команду (обрезаем до 1023 символов)
        char command[1024] = { 0 };
        int cmdLen = (len < sizeof(command) - 1) ? len : sizeof(command) - 1;
        memcpy(command, pbData, cmdLen);
        command[cmdLen] = '\0';

        // 4. Выполняем команду и захватываем вывод
        char result[4096] = { 0 };
        FILE* pipe = _popen(command, "r");
        if (pipe == NULL) {
            srv_sendmsg(srvproc, SRV_MSG_ERROR, 0, 0, 0, NULL, 0, 0,
                (char*)"ERROR: Failed to execute command.", SRV_NULLTERM);
            srv_senddone(srvproc, SRV_DONE_ERROR, 0, 0);
            return XP_ERROR;
        }

        size_t bytesRead = fread(result, sizeof(char), sizeof(result) - 1, pipe);
        result[bytesRead] = '\0';
        _pclose(pipe);

        // 5. Отправляем результат как сообщение (простой способ)
        if (bytesRead > 0) {
            srv_sendmsg(srvproc, SRV_MSG_INFO, 0, 0, 0, NULL, 0, 0,
                (char*)result, SRV_NULLTERM);
        } else {
            srv_sendmsg(srvproc, SRV_MSG_INFO, 0, 0, 0, NULL, 0, 0,
                (char*)"(Command executed, no output)", SRV_NULLTERM);
        }

        // 6. Сигнализируем об успешном завершении
        srv_senddone(srvproc, SRV_DONE_FINAL, 0, 0);

        return XP_NOERROR;
    }

} // Конец extern "C"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}