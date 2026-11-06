#include "mount/qtnativemountjob.h"
#include "mount/smb/smbmountconstants.h"
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <windows.h>

namespace {
// Fixed OS-client commands. Session credentials enter on stdin, never in a
// process command line or an encoded command visible to other users.
const char script[] = R"PS(
$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
[Console]::InputEncoding=[Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
try {
    $p=[Console]::In.ReadToEnd() | ConvertFrom-Json
    if ($p.operation -eq 'mount') {
        $owner=[Security.Principal.SecurityIdentifier]::new([string]$p.owner)
        $account=$owner.Translate([Security.Principal.NTAccount]).Value
        $used=@([IO.Directory]::GetLogicalDrives() | ForEach-Object { $_.Substring(0,2) })
        $used+=@(Get-SmbMapping | ForEach-Object LocalPath)
        $used+=@(Get-SmbGlobalMapping | ForEach-Object LocalPath)
        $drive=$null
        foreach ($letter in 90..68) {
            $candidate=([char]$letter).ToString()+':'
            if ($used -notcontains $candidate) { $drive=$candidate; break }
        }
        if (!$drive) { throw 'No unused drive letter is available.' }
        $secret=ConvertTo-SecureString ([string]$p.password) -AsPlainText -Force
        $credential=[PSCredential]::new([string]$p.user,$secret)
        New-SmbGlobalMapping -LocalPath $drive -RemotePath ([string]$p.remote) `
            -Credential $credential -TcpPort ([uint16]$p.port) -RequireIntegrity $false `
            -Persistent $false -FullAccess @($account) | Out-Null
        @{path=($drive+'\');error=''} | ConvertTo-Json -Compress
    } elseif ($p.operation -eq 'unmount') {
        if ([string]::IsNullOrEmpty([string]$p.drive)) {
            $mapping=@(Get-SmbGlobalMapping | Where-Object RemotePath -eq ([string]$p.remote))
        } else {
            $mapping=@(Get-SmbGlobalMapping | Where-Object LocalPath -eq ([string]$p.drive))
        }
        if ($mapping.Count -eq 0) {
            @{path='';error=''} | ConvertTo-Json -Compress
        } elseif ($mapping.Count -ne 1 -or $mapping[0].RemotePath -ne [string]$p.remote) {
            throw 'The drive belongs to another mapping. It has been preserved.'
        } else {
            Remove-SmbGlobalMapping -LocalPath $mapping[0].LocalPath -Force:([bool]$p.force) -Confirm:$false
            @{path='';error=''} | ConvertTo-Json -Compress
        }
    } else { throw 'Unsupported system mount operation.' }
} catch {
    @{path='';error=$_.Exception.Message} | ConvertTo-Json -Compress
    exit 1
}
)PS";
QString systemPowerShell() {
    wchar_t directory[MAX_PATH + 1]{};
    const UINT size = GetSystemDirectoryW(directory, MAX_PATH);
    return size && size < MAX_PATH ? QDir(QString::fromWCharArray(directory, int(size)))
                                         .filePath("WindowsPowerShell/v1.0/powershell.exe")
                                   : QString();
}
QString encodedScript() {
    const QString text = QString::fromLatin1(script);
    QByteArray utf16;
    utf16.reserve(text.size() * 2);
    for (const QChar c : text) {
        utf16.append(char(c.unicode() & 255));
        utf16.append(char(c.unicode() >> 8));
    }
    return QString::fromLatin1(utf16.toBase64());
}
} // namespace

struct QtNativeMountJob::State {
    QProcess Process;
    QString Path, Remote;
    QByteArray Input;
    bool Mounted = false, Unmounting = false, Running = false;
    void start(const QJsonObject &parameters) {
        Input = QJsonDocument(parameters).toJson(QJsonDocument::Compact);
        Running = true;
        Process.start(systemPowerShell(), {"-NoLogo", "-NoProfile", "-NonInteractive",
                                           "-EncodedCommand", encodedScript()});
    }
};
QtNativeMountJob::QtNativeMountJob(QObject *parent)
    : QObject(parent), state_(std::make_unique<State>()) {
    connect(&state_->Process, &QProcess::started, this, [this] {
        state_->Process.write(state_->Input);
        state_->Input.fill('\0');
        state_->Input.clear();
        state_->Process.closeWriteChannel();
    });
    const auto finished = [this](bool success) {
        if (!state_->Running)
            return;
        state_->Running = false;
        const auto output = state_->Process.readAllStandardOutput();
        const auto document = QJsonDocument::fromJson(output.trimmed());
        const auto result = document.object();
        QString error = result.value("error").toString();
        if (!success || document.isNull()) {
            if (error.isEmpty())
                error = tr("The system SMB client failed: %1").arg(state_->Process.errorString());
        }
        state_->Process.readAllStandardError();
        state_->Input.fill('\0');
        state_->Input.clear();
        if (state_->Unmounting) {
            if (error.isEmpty()) {
                state_->Mounted = false;
                state_->Path.clear();
            }
            emit unmountFinished(error);
        } else {
            state_->Path = result.value("path").toString();
            if (error.isEmpty() &&
                (state_->Path.size() != 3 || state_->Path[1] != ':' || state_->Path[2] != '\\'))
                error = tr("The system SMB client returned an invalid drive path.");
            state_->Mounted = error.isEmpty();
            emit mountFinished(state_->Mounted ? state_->Path : QString(), error);
        }
    };
    connect(&state_->Process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [finished](int code, QProcess::ExitStatus status) {
                finished(code == 0 && status == QProcess::NormalExit);
            });
    connect(&state_->Process, &QProcess::errorOccurred, this,
            [finished](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart)
                    finished(false);
            });
}
QtNativeMountJob::~QtNativeMountJob() = default;
bool QtNativeMountJob::mounted() const { return state_->Mounted; }
bool QtNativeMountJob::busy() const { return state_->Running; }
void QtNativeMountJob::mount(const QtNativeMountParameters &parameters, const QUrl &) {
    if (busy() || mounted()) {
        emit mountFinished({}, tr("A system mount operation is already active."));
        return;
    }
    // A UAC elevation of the same account can access its private pipe and map
    // only for that account. Different administrator credentials are not used.
    if (parameters.Owner != QtNativeClientOwnerIdentity()) {
        emit mountFinished({}, tr("Authorize mounting with the same Windows account that "
                                  "opened Sevenzip."));
        return;
    }
    state_->Remote = QStringLiteral("\\\\127.0.0.1\\") + parameters.Share;
    state_->Unmounting = false;
    state_->start({{"operation", "mount"},
                   {"owner", parameters.Owner},
                   {"user", QString::fromLatin1(kSevenzipSmbServerName) + '\\' + parameters.User},
                   {"password", parameters.Password},
                   {"remote", state_->Remote},
                   {"port", parameters.Port}});
}
void QtNativeMountJob::unmount(bool force) {
    if (busy())
        return;
    if (!mounted()) {
        emit unmountFinished({});
        return;
    }
    state_->Unmounting = true;
    state_->start({{"operation", "unmount"},
                   {"remote", state_->Remote},
                   {"drive", state_->Path.left(2)},
                   {"force", force}});
}
void QtNativeMountJob::recover(const QtNativeMountParameters &parameters, const QUrl &,
                               bool force) {
    if (parameters.Owner != QtNativeClientOwnerIdentity()) {
        emit unmountFinished(tr("Authorize mounting with the same Windows account that "
                                "opened Sevenzip."));
        return;
    }
    state_->Remote = QStringLiteral("\\\\127.0.0.1\\") + parameters.Share;
    state_->Mounted = true;
    state_->Path.clear();
    unmount(force);
}
