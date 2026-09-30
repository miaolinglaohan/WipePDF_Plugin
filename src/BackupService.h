#pragma once
#include "PluginInit.h"
#include <string>

namespace wipepdf {

struct BackupStats {
    int fileCount = 0;
    double totalSizeMB = 0.0;
};

class BackupService {
public:
    // Returns the dedicated backup directory path, e.g. %TEMP%\WipePDF_Backups\
    // (guaranteed to end with a backslash; directory is created if missing).
    static std::wstring GetBackupDirectory();

    // Saves a full backup copy of pddoc to the dedicated backup directory.
    // Automatically purges backups older than maxAgeDays (default 7 days).
    // Returns the generated backup file path, or empty string on failure.
    static std::wstring CreateBackup(PDDoc pddoc, int maxAgeDays = 7);

    // Opens the dedicated backup directory in Windows File Explorer.
    static bool OpenBackupFolder();

    // Queries current statistics of the backup directory (file count & total MB).
    static BackupStats GetStats();

    // Deletes all backup PDF files in the backup directory.
    // Returns the deleted file count and freed disk space in MB.
    static BackupStats ClearAll();

    // Quietly deletes backup files older than maxAgeDays.
    static int PurgeExpired(int maxAgeDays = 7);
};

} // namespace wipepdf
