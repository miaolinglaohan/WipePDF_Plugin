#include "BackupService.h"
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <cstdio>

namespace wipepdf {

// Delete every regular file matching pattern inside dir. Returns the deleted
// file count and accumulates freed bytes into *freedBytes when non-null.
static int DeleteMatchingFiles(const std::wstring &dir, const std::wstring &pattern, ULONGLONG *freedBytes) {
    int count = 0;
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            ULARGE_INTEGER fsz;
            fsz.LowPart = fd.nFileSizeLow;
            fsz.HighPart = fd.nFileSizeHigh;
            if (DeleteFileW((dir + fd.cFileName).c_str())) {
                count++;
                if (freedBytes) *freedBytes += fsz.QuadPart;
            }
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
    return count;
}

// Sanitize a raw document name for safe use in a Windows filename.
// Strips the .pdf extension, replaces illegal characters, trims
// whitespace/dots, and truncates to leave room for the prefix + timestamp.
static std::wstring SanitizeForFileName(const std::wstring &raw) {
    if (raw.empty()) return raw;
    std::wstring name = raw;

    // Strip .pdf extension (case-insensitive)
    if (name.size() >= 4 && _wcsicmp(name.substr(name.size() - 4).c_str(), L".pdf") == 0) {
        name = name.substr(0, name.size() - 4);
    }

    // Replace Windows-illegal filename characters and control chars
    for (auto &ch : name) {
        if (ch < 32 || ch == L'\\' || ch == L'/' || ch == L':' || ch == L'*' ||
            ch == L'?' || ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|') {
            ch = L'_';
        }
    }

    // Trim leading/trailing spaces and dots (Windows forbids them at boundaries)
    size_t start = name.find_first_not_of(L" .");
    size_t end   = name.find_last_not_of(L" .");
    if (start == std::wstring::npos) return L"";
    name = name.substr(start, end - start + 1);

    // Truncate to 60 chars to stay well within MAX_PATH.
    // Full pattern: WipePDF_backup_<name>_<YYYYMMDD_HHMMSS_mmm>.pdf
    //               15 + name + 1 + 19 + 4 = 39 + name  →  39 + 60 = 99
    const size_t kMaxNameLen = 60;
    if (name.size() > kMaxNameLen) {
        name = name.substr(0, kMaxNameLen);
        size_t end2 = name.find_last_not_of(L" .");
        if (end2 == std::wstring::npos) return L"";
        name = name.substr(0, end2 + 1);
    }

    return name;
}

// Try to extract the leaf filename from an already-opened PDDoc by
// querying its backing ASFile.  Returns an empty string when the doc
// has no associated file (e.g. created in memory and not yet saved).
static std::wstring ExtractDocNameFromPDDoc(PDDoc pddoc) {
    std::wstring name;
    if (!pddoc) return name;

    DURING
        ASFile asFile = PDDocGetFile(pddoc);
        if (asFile) {
            ASPathName pathName = ASFileAcquirePathName(asFile);
            if (pathName) {
                char nameBuf[MAX_PATH] = {0};
                ASErrorCode err = ASFileSysGetNameFromPath(
                    ASGetDefaultFileSys(), pathName, nameBuf, sizeof(nameBuf));
                ASFileSysReleasePath(ASGetDefaultFileSys(), pathName);

                if (err == 0 && nameBuf[0] != '\0') {
                    // Convert from platform encoding (ANSI codepage) to wstring
                    int wlen = MultiByteToWideChar(CP_ACP, 0, nameBuf, -1, NULL, 0);
                    if (wlen > 1) {
                        name.resize(wlen - 1);
                        MultiByteToWideChar(CP_ACP, 0, nameBuf, -1, &name[0], wlen);
                    }
                }
            }
        }
    HANDLER
        name.clear();
    END_HANDLER

    return name;
}

std::wstring BackupService::GetBackupDirectory() {
    wchar_t tempDir[MAX_PATH] = {0};
    if (GetTempPathW(MAX_PATH, tempDir) == 0) return std::wstring();
    std::wstring dir = tempDir;
    if (!dir.empty() && dir.back() != L'\\') dir.push_back(L'\\');
    dir += L"WipePDF_Backups\\";
    CreateDirectoryW(dir.c_str(), NULL);
    return dir;
}

int BackupService::PurgeExpired(int maxAgeDays) {
    std::wstring dir = GetBackupDirectory();
    if (dir.empty()) return 0;

    std::wstring pattern = dir + L"WipePDF_backup_*.pdf";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return 0;

    FILETIME nowFt;
    GetSystemTimeAsFileTime(&nowFt);
    ULARGE_INTEGER nowVal;
    nowVal.LowPart = nowFt.dwLowDateTime;
    nowVal.HighPart = nowFt.dwHighDateTime;

    // 1 day = 24 * 3600 * 10^7 in 100-nanosecond units
    ULONGLONG maxAgeIntervals = (ULONGLONG)maxAgeDays * 24ULL * 3600ULL * 10000000ULL;

    int purgedCount = 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            ULARGE_INTEGER fileVal;
            fileVal.LowPart = fd.ftLastWriteTime.dwLowDateTime;
            fileVal.HighPart = fd.ftLastWriteTime.dwHighDateTime;
            if (nowVal.QuadPart > fileVal.QuadPart && (nowVal.QuadPart - fileVal.QuadPart) > maxAgeIntervals) {
                std::wstring filePath = dir + fd.cFileName;
                if (DeleteFileW(filePath.c_str())) {
                    purgedCount++;
                }
            }
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    // Legacy layout (v1.5.1 and earlier) stored backups directly in %TEMP%
    // instead of the dedicated subdirectory. Those files never show up in the
    // directory stats or clear operation and are not recreated by current
    // versions - remove the orphans so they don't accumulate forever.
    wchar_t legacyTemp[MAX_PATH] = {0};
    if (GetTempPathW(MAX_PATH, legacyTemp) != 0) {
        std::wstring legacyDir = legacyTemp;
        if (!legacyDir.empty() && legacyDir.back() != L'\\') legacyDir.push_back(L'\\');
        purgedCount += DeleteMatchingFiles(legacyDir, legacyDir + L"WipePDF_backup_*.pdf", NULL);
    }

    return purgedCount;
}

std::wstring BackupService::CreateBackup(PDDoc pddoc, const std::wstring &docName, int maxAgeDays) {
    if (!pddoc) return std::wstring();

    // 1. Quietly purge expired backups to avoid accumulating disk garbage
    if (maxAgeDays > 0) {
        PurgeExpired(maxAgeDays);
    }

    std::wstring dir = GetBackupDirectory();
    if (dir.empty()) return std::wstring();

    // 2. Resolve and sanitize the document name for the backup filename.
    //    Priority: explicit docName → auto-extract from PDDoc → pure timestamp
    std::wstring safeName;
    if (!docName.empty()) {
        safeName = SanitizeForFileName(docName);
    }
    if (safeName.empty()) {
        std::wstring autoName = ExtractDocNameFromPDDoc(pddoc);
        safeName = SanitizeForFileName(autoName);
    }

    // 3. Format a human-readable local timestamp: YYYYMMDD_HHMMSS_mmm
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t timeBuf[64];
    swprintf_s(timeBuf, L"%04d%02d%02d_%02d%02d%02d_%03d",
              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    // 4. Build backup path:
    //      WipePDF_backup_<docName>_<timestamp>.pdf   (when name is available)
    //      WipePDF_backup_<timestamp>.pdf             (fallback: pure timestamp)
    std::wstring prefix = L"WipePDF_backup_";
    if (!safeName.empty()) {
        prefix += safeName + L"_";
    }

    // Guarantee a unique path even if two backups land within the same
    // millisecond (GetLocalTime resolution): probe the filesystem and append
    // a sequence suffix while the name is taken.
    std::wstring backupPath = dir + prefix + timeBuf + L".pdf";
    int seq = 1;
    while (GetFileAttributesW(backupPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        backupPath = dir + prefix + timeBuf + L"_" + std::to_wstring(seq++) + L".pdf";
    }

    ASText diText = ASTextFromUnicode((const ASUTF16Val *)backupPath.c_str(), kUTF16HostEndian);
    if (!diText) return std::wstring();

    ASPathName backupPathName = ASFileSysCreatePathFromDIPathText(ASGetDefaultFileSys(), diText, NULL);
    ASTextDestroy(diText);
    if (!backupPathName) return std::wstring();

    PDDocSave(pddoc, (PDSaveFull | PDSaveCopy), backupPathName, ASGetDefaultFileSys(), NULL, NULL);
    ASFileSysReleasePath(ASGetDefaultFileSys(), backupPathName);

    // Verify file exists on disk
    DWORD attrs = GetFileAttributesW(backupPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        return std::wstring();
    }
    return backupPath;
}

bool BackupService::OpenBackupFolder() {
    std::wstring dir = GetBackupDirectory();
    if (dir.empty()) return false;
    HINSTANCE hInst = ShellExecuteW(NULL, L"open", dir.c_str(), NULL, NULL, SW_SHOWNORMAL);
    return ((INT_PTR)hInst > 32);
}

BackupStats BackupService::GetStats() {
    BackupStats stats;
    std::wstring dir = GetBackupDirectory();
    if (dir.empty()) return stats;

    std::wstring pattern = dir + L"WipePDF_backup_*.pdf";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return stats;

    ULONGLONG totalBytes = 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            stats.fileCount++;
            ULARGE_INTEGER fsz;
            fsz.LowPart = fd.nFileSizeLow;
            fsz.HighPart = fd.nFileSizeHigh;
            totalBytes += fsz.QuadPart;
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    stats.totalSizeMB = (double)totalBytes / (1024.0 * 1024.0);
    return stats;
}

BackupStats BackupService::ClearAll() {
    BackupStats stats;
    std::wstring dir = GetBackupDirectory();
    if (dir.empty()) return stats;

    std::wstring pattern = dir + L"WipePDF_backup_*.pdf";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return stats;

    ULONGLONG freedBytes = 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            ULARGE_INTEGER fsz;
            fsz.LowPart = fd.nFileSizeLow;
            fsz.HighPart = fd.nFileSizeHigh;
            std::wstring filePath = dir + fd.cFileName;
            if (DeleteFileW(filePath.c_str())) {
                stats.fileCount++;
                freedBytes += fsz.QuadPart;
            }
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    stats.totalSizeMB = (double)freedBytes / (1024.0 * 1024.0);
    return stats;
}

} // namespace wipepdf
