#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static std::wstring trim(const std::wstring& value) {
    const auto first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return L"";
    const auto last = value.find_last_not_of(L" \t\r\n");
    return value.substr(first, last - first + 1);
}

static std::vector<std::wstring> split_args(const std::wstring& line) {
    std::vector<std::wstring> out;
    std::wstring current;
    bool quoted = false;
    for (wchar_t ch : line) {
        if (ch == L'"') { quoted = !quoted; continue; }
        if ((ch == L' ' || ch == L'\t') && !quoted) {
            if (!current.empty()) { out.push_back(current); current.clear(); }
        } else current.push_back(ch);
    }
    if (!current.empty()) out.push_back(current);
    return out;
}

static std::wstring lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return value;
}

static void print_error(const wchar_t* action) {
    const DWORD code = GetLastError();
    wchar_t* message = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                   FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
                   reinterpret_cast<LPWSTR>(&message), 0, nullptr);
    std::wcerr << action << L": " << (message ? message : L"Windows error")
               << L" (" << code << L")\n";
    if (message) LocalFree(message);
}

static void show_help() {
    std::wcout <<
        L"PortShell 0.1 - PlayPort Windows shell prototype\n\n"
        L"Built-ins:\n"
        L"  help                 Show this help\n"
        L"  pwd                  Print current directory\n"
        L"  cd [path]            Change directory\n"
        L"  dir [path]           List files\n"
        L"  mkdir <path>         Create a directory\n"
        L"  rmdir <path>         Remove an empty directory\n"
        L"  del <file>           Delete a file\n"
        L"  copy <src> <dst>     Copy a file\n"
        L"  type <file>          Print a text file\n"
        L"  where <program>      Search PATH for a program\n"
        L"  cls                  Clear the console\n"
        L"  exit                 Exit PortShell\n\n"
        L"Other commands are launched directly with CreateProcessW.\n"
        L"Examples: adb devices | adb connect 192.168.1.20:5555 | opencode\n"
        L"Set PORTSHELL_ROOT to choose the initial shared Wine-visible folder.\n\n";
}

static bool launch_external(const std::wstring& command) {
    std::wstring mutable_command = command;
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    if (!CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, TRUE,
                        CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr, &si, &pi)) {
        print_error(L"CreateProcessW failed (external process may be unsupported)");
        return false;
    }
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    if (exit_code != 0) std::wcerr << L"[process exit code: " << exit_code << L"]\n";
    return true;
}

static void list_dir(const fs::path& path) {
    std::error_code ec;
    std::wcout << L"\n Directory of " << fs::absolute(path, ec).wstring() << L"\n\n";
    for (fs::directory_iterator it(path, ec), end; !ec && it != end; it.increment(ec)) {
        const auto name = it->path().filename().wstring();
        if (it->is_directory(ec)) std::wcout << L"<DIR>       ";
        else {
            const auto size = it->file_size(ec);
            std::wcout << L"            " << size << L" ";
        }
        std::wcout << name << L"\n";
    }
    if (ec) std::wcerr << L"dir: " << ec.message().c_str() << L"\n";
    std::wcout << L"\n";
}

static bool run_builtin(const std::wstring& line, bool& should_exit) {
    const auto args = split_args(line);
    if (args.empty()) return true;
    const auto cmd = lower(args[0]);
    std::error_code ec;

    if (cmd == L"help" || cmd == L"?") show_help();
    else if (cmd == L"exit" || cmd == L"quit") should_exit = true;
    else if (cmd == L"pwd") std::wcout << fs::current_path(ec).wstring() << L"\n";
    else if (cmd == L"cd") {
        if (args.size() == 1) std::wcout << fs::current_path(ec).wstring() << L"\n";
        else {
            fs::current_path(fs::path(args[1]), ec);
            if (ec) std::wcerr << L"cd: " << ec.message().c_str() << L"\n";
        }
    } else if (cmd == L"dir" || cmd == L"ls") {
        list_dir(args.size() > 1 ? fs::path(args[1]) : fs::current_path());
    } else if (cmd == L"mkdir") {
        if (args.size() < 2) std::wcerr << L"usage: mkdir <path>\n";
        else if (!fs::create_directories(args[1], ec) && ec)
            std::wcerr << L"mkdir: " << ec.message().c_str() << L"\n";
    } else if (cmd == L"rmdir") {
        if (args.size() < 2) std::wcerr << L"usage: rmdir <path>\n";
        else if (!fs::remove(args[1], ec) || ec)
            std::wcerr << L"rmdir: " << (ec ? ec.message().c_str() : "directory not empty") << L"\n";
    } else if (cmd == L"del" || cmd == L"rm") {
        if (args.size() < 2) std::wcerr << L"usage: del <file>\n";
        else if (!fs::remove(args[1], ec) || ec)
            std::wcerr << L"del: " << (ec ? ec.message().c_str() : "file not found") << L"\n";
    } else if (cmd == L"copy" || cmd == L"cp") {
        if (args.size() < 3) std::wcerr << L"usage: copy <source> <destination>\n";
        else if (!fs::copy_file(args[1], args[2], fs::copy_options::overwrite_existing, ec) || ec)
            std::wcerr << L"copy: " << ec.message().c_str() << L"\n";
    } else if (cmd == L"type" || cmd == L"cat") {
        if (args.size() < 2) std::wcerr << L"usage: type <file>\n";
        else {
            HANDLE file = CreateFileW(args[1].c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) print_error(L"type");
            else {
                char buffer[4096]; DWORD read = 0;
                while (ReadFile(file, buffer, sizeof(buffer), &read, nullptr) && read)
                    std::cout.write(buffer, static_cast<std::streamsize>(read));
                CloseHandle(file);
            }
        }
    } else if (cmd == L"where") {
        if (args.size() < 2) std::wcerr << L"usage: where <program>\n";
        else {
            wchar_t found[MAX_PATH]{};
            const DWORD n = SearchPathW(nullptr, args[1].c_str(), L".exe", MAX_PATH, found, nullptr);
            if (n && n < MAX_PATH) std::wcout << found << L"\n";
            else std::wcout << L"Not found in PATH: " << args[1] << L"\n";
        }
    } else if (cmd == L"cls" || cmd == L"clear") {
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO info{};
        if (GetConsoleScreenBufferInfo(h, &info)) {
            DWORD written = 0;
            const DWORD cells = static_cast<DWORD>(info.dwSize.X) * static_cast<DWORD>(info.dwSize.Y);
            COORD home{0, 0};
            FillConsoleOutputCharacterW(h, L' ', cells, home, &written);
            FillConsoleOutputAttribute(h, info.wAttributes, cells, home, &written);
            SetConsoleCursorPosition(h, home);
        } else std::wcout << L"\x1b[2J\x1b[H";
    } else return false;
    return true;
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    _wsetlocale(LC_ALL, L".UTF8");

    if (const wchar_t* root = _wgetenv(L"PORTSHELL_ROOT"); root && *root) {
        std::error_code ec;
        fs::current_path(root, ec);
        if (ec) std::wcerr << L"PORTSHELL_ROOT unavailable: " << ec.message().c_str() << L"\n";
    }

    std::wcout << L"PortShell 0.1 | PlayPort terminal prototype\n"
                  L"Type 'help' for commands. External programs use CreateProcessW.\n\n";
    bool should_exit = false;
    std::wstring line;
    while (!should_exit) {
        std::wcout << L"PS " << fs::current_path().wstring() << L"> " << std::flush;
        if (!std::getline(std::wcin, line)) break;
        line = trim(line);
        if (line.empty()) continue;
        if (!run_builtin(line, should_exit)) launch_external(line);
    }
    return 0;
}
