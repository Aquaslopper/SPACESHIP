// Windows entry point for the FULL visual edition, not the experimental console port.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>

namespace {
int fail(const std::wstring& message) {
  MessageBoxW(nullptr, message.c_str(), L"Spaceship could not start", MB_OK | MB_ICONERROR);
  return 1;
}
bool fileExists(const std::wstring& path) {
  const DWORD attributes = GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR commandLine, int) {
  std::vector<wchar_t> buffer(32768);
  const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (!length || length >= buffer.size()) return fail(L"Cannot locate the game folder.");
  const std::wstring executable(buffer.data(), length);
  const std::wstring directory = executable.substr(0, executable.find_last_of(L"\\/"));
  const std::wstring game = directory + L"\\Spaceship.hta";
  if (!fileExists(game)) {
    return fail(L"Spaceship.hta is missing. Extract the entire downloaded ZIP into a writable folder, then run spaceship.exe there. Keep the EXE and HTA together.");
  }
  const UINT systemLength = GetSystemDirectoryW(buffer.data(), static_cast<UINT>(buffer.size()));
  if (!systemLength || systemLength >= buffer.size()) return fail(L"Cannot locate the Windows system folder.");
  const std::wstring host = std::wstring(buffer.data(), systemLength) + L"\\mshta.exe";
  if (!fileExists(host)) return fail(L"Windows HTML Application support (mshta.exe) is unavailable. This edition requires it. On a managed PC, ask your administrator whether HTAs are permitted; do not disable security protections.");
  // Noninteractive packaging check used by Windows CI. Does not claim to test rendering.
  if (std::wstring(commandLine) == L"--check") return 0;
  std::wstring command = L"\"" + host + L"\" \"" + game + L"\"";
  std::vector<wchar_t> writable(command.begin(), command.end());
  writable.push_back(L'\0');
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(host.c_str(), writable.data(), nullptr, nullptr, FALSE, 0,
                      nullptr, directory.c_str(), &startup, &process)) {
    const DWORD error = GetLastError();
    return fail(L"Windows could not start the game (error " + std::to_wstring(error) +
                L"). HTA execution may be blocked by a system policy. Keep security protections enabled and check with your administrator.");
  }
  WaitForSingleObject(process.hProcess, INFINITE);
  DWORD exitCode = 1;
  GetExitCodeProcess(process.hProcess, &exitCode);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  if (exitCode != 0) return fail(L"The Windows HTML Application host exited with error " +
                               std::to_wstring(exitCode) + L". Try opening Spaceship.hta directly to inspect the problem.");
  return 0;
}
