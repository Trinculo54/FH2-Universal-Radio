#pragma once

#include <windows.h>

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace fh6::subprocess {

std::wstring widen(std::string_view s);
std::string narrow(std::wstring_view ws);

std::wstring quote(const std::wstring& s);

// GetStdHandle returns NULL under windowed DLL injection, which breaks
// STARTF_USESTDHANDLES; NUL is a safe substitute.
HANDLE open_nul(DWORD access);

// <dir>\helpers.log, shared by every helper process; FILE_APPEND_DATA makes
// writes atomic across concurrent children. Call set_log_directory (with the
// mod's data folder) before the first spawn; without it the log goes to %TEMP%.
void set_log_directory(const std::filesystem::path& dir);
HANDLE open_stderr_log();
std::filesystem::path stderr_log_path();

// JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE reaps the whole tree (incl. grandchildren
// like yt-dlp's deno) when the last handle drops.
HANDLE create_kill_on_close_job();

const wchar_t* safe_spawn_cwd();

// CREATE_SUSPENDED + AssignProcessToJobObject + ResumeThread so fast children
// can't spawn descendants outside the job.
HANDLE spawn_in_job(HANDLE job, const std::wstring& cmd, HANDLE stdin_h, HANDLE stdout_h,
                    HANDLE stderr_h);

// Read a pipe to EOF. max_bytes caps the result (0 = unbounded); excess is read
// and dropped so the writer never blocks.
std::string drain_to_eof(HANDLE pipe, std::size_t max_bytes = 0);

// Spawn cmd in a kill-on-close job and capture its stdout (or stderr, if
// capture_stderr) to EOF, then reap it. Returns "" if the spawn failed.
std::string capture_output(const std::wstring& cmd, bool capture_stderr = false,
                           std::size_t max_bytes = 0);

// Manually terminates a process and all its children.
// Crucial for Wine/Proton where Job Objects often fail to reap children.
void kill_process_tree(DWORD pid);

// Terminate a process and its entire tree, then close and null the handle.
// No-op when proc is null. Consolidates the teardown idiom every source uses.
void reap(HANDLE& proc) noexcept;

std::string describe_launch_failure(const std::wstring& bin, DWORD ec, bool from_config);

} // namespace fh6::subprocess
