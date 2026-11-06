// GUI.cpp
// Adapted from 7-Zip 0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Original Main2 dispatcher and WinMain exception/exit-code policy.
// Qt startup/platform initialization and retained engine factories are supplied
// by the entry/service owners; archive work remains behind ArchiveBackend.
#ifdef _WIN32
#include "C/DllSecur.h"
#endif
#include "7zip/UI/Common/ArchiveCommandLine.h"
#include "7zip/UI/Common/ExitCode.h"
#include "7zip/UI/GUI/ExtractRes.h"
#include "Common/CommandLineParser.h"
#include "Common/MyException.h"
#include "Common/StringConvert.h"
#include "archive/hashing/qtfilecrc.h"
#include "benchmark/BenchmarkDialog.h"
#include "gui/ExtractGUI.h"
#include "gui/HashGUI.h"
#include "gui/UpdateCallbackGUI.h"
#include "gui/UpdateGUI.h"
#include "gui/qtguicommand.h"
#include "localization/LangUtils.h"
#include "platform/shell/qtshellarchiveservices.h"
#include "ui/diagnostics/qtdiagnostictext.h"
#ifndef _WIN32
#include <QCoreApplication>
#include <QMessageBox>
#endif

using namespace NWindows;
extern bool g_DisableUserQuestions;

static void ErrorMessage(LPCWSTR message) {
    if (!g_DisableUserQuestions) {
#ifdef _WIN32
        MessageBoxW(NULL, message, L"7-Zip", MB_ICONERROR | MB_OK);
#else
        QMessageBox::critical(nullptr, QStringLiteral("7-Zip"), QString::fromWCharArray(message));
#endif
    }
}

static void ErrorMessage(const char *s) { ErrorMessage(QtDiagnosticText(s)); }

static void ErrorLangMessage(UINT resourceID) { ErrorMessage(LangString(resourceID)); }

static const char *const kNoFormats = QT_TRANSLATE_NOOP(
    "SevenZipDiagnostics", "7-Zip cannot find the code that works with archives.");

static int ShowMemErrorMessage() {
    ErrorLangMessage(IDS_MEM_ERROR);
    return NExitCode::kMemoryError;
}

static int ShowSysErrorMessage(HRESULT errorCode) {
    if (errorCode == E_OUTOFMEMORY)
        return ShowMemErrorMessage();
    ErrorMessage(HResultToMessage(errorCode));
    return NExitCode::kFatalError;
}

static void ThrowException_if_Error(HRESULT res) {
    if (res != S_OK)
        throw CSystemException(res);
}

static int Main2(const QtGUIServiceFactory &factory, const QtGUICommandSource &command) {
    CArcCmdLineOptions options;
    if (command.Configure) {
        command.Configure(options);
        g_DisableUserQuestions = options.YesToAll;
    } else {
        UStringVector commandStrings;
#ifdef _WIN32
        NCommandLineParser::SplitCommandLine(GetCommandLineW(), commandStrings);
#else
        for (const QString &argument : QCoreApplication::arguments())
            commandStrings.Add(UString(argument.toStdWString().c_str()));
#endif

#ifndef UNDER_CE
        if (commandStrings.Size() > 0)
            commandStrings.Delete(0);
#endif
        if (commandStrings.Size() == 0) {
            const UString message =
                QtDiagnosticText(QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Specify command"));
#ifdef _WIN32
            MessageBoxW(NULL, message, L"7-Zip", 0);
#else
            QMessageBox::information(nullptr, QStringLiteral("7-Zip"),
                                     QString::fromWCharArray(message));
#endif
            return 0;
        }

        CArcCmdLineParser parser;

        parser.Parse1(commandStrings, options);
        g_DisableUserQuestions = options.YesToAll;
        parser.Parse2(options);
    }

    const bool hashOnly = options.Command.CommandType == NCommandType::kHash;
    QtShellArchiveServices &services = factory(hashOnly);
    CCodecs *codecs = &services.GUICodecs(hashOnly);
    CExternalCodecs _externalCodecs;

    codecs->CaseSensitive_Change = options.CaseSensitive_Change;
    codecs->CaseSensitive = options.CaseSensitive;
    // The retained backend catalog already performed source Load/AddHash.

#ifdef Z7_EXTERNAL_CODECS
    {
        UString s;
        codecs->GetCodecsErrorMessage(s);
        if (!s.IsEmpty()) {
            if (!g_DisableUserQuestions)
                ErrorMessage(s);
        }
    }
#endif

    const bool isExtractGroupCommand = options.Command.IsFromExtractGroup();

    if (codecs->Formats.Size() == 0 && (isExtractGroupCommand

                                        || options.Command.IsFromUpdateGroup())) {
#ifdef Z7_EXTERNAL_CODECS
        if (!codecs->MainDll_ErrorPath.IsEmpty()) {
            UString s = QtDiagnosticText(
                QT_TRANSLATE_NOOP("SevenZipDiagnostics", "7-Zip cannot load module: "));
            s += fs2us(codecs->MainDll_ErrorPath);
            throw s;
        }
#endif
        throw kNoFormats;
    }

    CObjectVector<COpenType> formatIndices;
    if (!ParseOpenTypes(*codecs, options.ArcType, formatIndices)) {
        ErrorLangMessage(IDS_UNSUPPORTED_ARCHIVE_TYPE);
        return NExitCode::kFatalError;
    }

    CIntVector excludedFormats;
    FOR_VECTOR(k, options.ExcludedArcTypes) {
        CIntVector tempIndices;
        if (!codecs->FindFormatForArchiveType(options.ExcludedArcTypes[k], tempIndices) ||
            tempIndices.Size() != 1) {
            ErrorLangMessage(IDS_UNSUPPORTED_ARCHIVE_TYPE);
            return NExitCode::kFatalError;
        }
        excludedFormats.AddToUniqueSorted(tempIndices[0]);
        // excludedFormats.Sort();
    }

#ifdef Z7_EXTERNAL_CODECS
    if (isExtractGroupCommand || options.Command.IsFromUpdateGroup() ||
        options.Command.CommandType == NCommandType::kHash ||
        options.Command.CommandType == NCommandType::kBenchmark)
        ThrowException_if_Error(hashOnly ? services.LoadHashProviders(_externalCodecs)
                                         : services.LoadExternalCodecs(_externalCodecs));
#endif

    if (options.Command.CommandType == NCommandType::kBenchmark) {
        HRESULT operationResult = S_OK;
        HRESULT res = Benchmark(EXTERNAL_CODECS_VARS_L options.Properties,
                                options.NumIterations_Defined ? options.NumIterations
                                                              : k_NumBenchIterations_Default,
                                nullptr, nullptr, &operationResult);
        /*
        if (res == S_FALSE)
        {
          stdStream << "\nDecoding Error\n";
          return NExitCode::kFatalError;
        }
        */
        ThrowException_if_Error(res);
        if (operationResult != S_OK)
            return operationResult == E_ABORT ? NExitCode::kUserBreak : NExitCode::kFatalError;
    } else if (isExtractGroupCommand) {
        UStringVector ArchivePathsSorted;
        UStringVector ArchivePathsFullSorted;

        CMyComPtr2_Create<IFolderArchiveExtractCallback, CExtractCallbackImp> ecs;

#ifndef Z7_NO_CRYPTO
        ecs->PasswordIsDefined = options.PasswordEnabled;
        ecs->Password = options.Password;
#endif

        ecs->Init();

        CExtractOptions eo;
        (CExtractOptionsBase &)eo = options.ExtractOptions;
        eo.StdInMode = options.StdInMode;
        eo.StdOutMode = options.StdOutMode;
        eo.YesToAll = options.YesToAll;
        ecs->YesToAll = options.YesToAll;
        ecs->DisableUserQuestions = options.YesToAll;
        eo.TestMode = options.Command.IsTestCommand();
        ecs->TestMode = eo.TestMode;

#ifndef Z7_SFX
        eo.Properties = options.Properties;
#endif

        bool messageWasDisplayed = false;

#ifndef Z7_SFX
        CHashBundle hb;
        CHashBundle *hb_ptr = NULL;

        if (!options.HashMethods.IsEmpty()) {
            hb_ptr = &hb;
            ThrowException_if_Error(hb.SetMethods(EXTERNAL_CODECS_VARS_L options.HashMethods));
        }
#endif

        {
            CDirItemsStat st;
            HRESULT hresultMain =
                EnumerateDirItemsAndSort(options.arcCensor, NWildcard::k_RelatPath,
                                         UString(), // addPathPrefix
                                         ArchivePathsSorted, ArchivePathsFullSorted, st,
                                         NULL // &scan: change it!!!!
                );
            if (hresultMain != S_OK) {
                /*
                if (hresultMain != E_ABORT && messageWasDisplayed)
                  return NExitCode::kFatalError;
                */
                throw CSystemException(hresultMain);
            }
        }

        ecs->MultiArcMode = (ArchivePathsSorted.Size() > 1);

        HRESULT result = services.ExtractGUI(
            // EXTERNAL_CODECS_VARS_L
            *codecs, formatIndices, excludedFormats, ArchivePathsSorted, ArchivePathsFullSorted,
            options.Censor.Pairs.Front().Head, eo,
#ifndef Z7_SFX
            hb_ptr,
#endif
            options.ShowDialog, messageWasDisplayed, *ecs.ClsPtr(), options.ArcType);
        if (result != S_OK) {
            if (result != E_ABORT && messageWasDisplayed)
                return NExitCode::kFatalError;
            throw CSystemException(result);
        }
        if (!ecs->IsOK())
            return NExitCode::kFatalError;
        if (ecs->SomeItemsSkipped ||
            (ecs->ExtractReport.Status && !ecs->ExtractReport.Status->Complete))
            return NExitCode::kWarning;
    } else if (options.Command.IsFromUpdateGroup()) {
#ifndef Z7_NO_CRYPTO
        bool passwordIsDefined = options.PasswordEnabled && !options.Password.IsEmpty();
#endif

        CUpdateCallbackGUI callback;
        // callback.EnablePercents = options.EnablePercents;

#ifndef Z7_NO_CRYPTO
        callback.PasswordIsDefined = passwordIsDefined;
        callback.AskPassword = options.PasswordEnabled && options.Password.IsEmpty();
        callback.Password = options.Password;
#endif

        // callback.StdOutMode = options.UpdateOptions.StdOutMode;
        callback.Init();

        if (!options.UpdateOptions.InitFormatIndex(codecs, formatIndices, options.ArchiveName) ||
            !options.UpdateOptions.SetArcPath(codecs, options.ArchiveName)) {
            ErrorLangMessage(IDS_UPDATE_NOT_SUPPORTED);
            return NExitCode::kFatalError;
        }
        bool messageWasDisplayed = false;
        HRESULT result = services.UpdateGUI(*codecs, formatIndices, options.ArchiveName,
                                            options.Censor, options.UpdateOptions,
                                            options.ShowDialog, messageWasDisplayed, callback);

        if (result != S_OK) {
            if (result != E_ABORT && messageWasDisplayed)
                return NExitCode::kFatalError;
            throw CSystemException(result);
        }
        if (callback.FailedFiles.Size() > 0) {
            if (!messageWasDisplayed)
                throw CSystemException(E_FAIL);
            return NExitCode::kWarning;
        }
    } else if (options.Command.CommandType == NCommandType::kHash) {
        bool messageWasDisplayed = false;
        HRESULT result;
        if (command.FileHash) {
            const auto &selection = *command.FileHash;
            UStringVector paths;
            for (const auto &path : selection.Paths)
                paths.Add(path.toStdWString().c_str());
            const UString method = options.HashOptions.Methods.IsEmpty()
                                       ? UString()
                                       : options.HashOptions.Methods.Front();
            result = CalculateFileCrc(method, &_externalCodecs,
                                      selection.BasePrefix.toStdWString().c_str(), paths,
                                      selection.EnterToDirs, nullptr, &messageWasDisplayed);
            if (result != S_OK && result != S_FALSE && result != E_ABORT && !messageWasDisplayed) {
                services.ErrorMessageHRESULT(result);
                messageWasDisplayed = true;
            }
        } else
            result = services.HashCalcGUI(EXTERNAL_CODECS_VARS_L options.Censor,
                                          options.HashOptions, messageWasDisplayed);
        if (result == S_FALSE)
            return NExitCode::kWarning;

        if (result != S_OK) {
            if (result != E_ABORT && messageWasDisplayed)
                return NExitCode::kFatalError;
            throw CSystemException(result);
        }
        /*
        if (callback.FailedFiles.Size() > 0)
        {
          if (!messageWasDisplayed)
            throw CSystemException(E_FAIL);
          return NExitCode::kWarning;
        }
        */
    } else {
        throw QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Unsupported command");
    }
    return 0;
}

int SevenzipGUI(const QtGUIServiceFactory &factory, const QtGUICommandSource &command) {
    try {
#ifdef _WIN32
        My_SetDefaultDllDirectories();
#endif
        return Main2(factory, command);
    } catch (const CNewException &) {
        return ShowMemErrorMessage();
    } catch (const CMessagePathException &e) {
        ErrorMessage(e);
        return NExitCode::kUserError;
    } catch (const CSystemException &systemError) {
        if (systemError.ErrorCode == E_ABORT)
            return NExitCode::kUserBreak;
        return ShowSysErrorMessage(systemError.ErrorCode);
    } catch (const UString &s) {
        ErrorMessage(s);
        return NExitCode::kFatalError;
    } catch (const AString &s) {
        ErrorMessage(s);
        return NExitCode::kFatalError;
    } catch (const wchar_t *s) {
        ErrorMessage(s);
        return NExitCode::kFatalError;
    } catch (const char *s) {
        ErrorMessage(s);
        return NExitCode::kFatalError;
    } catch (int v) {
        ErrorMessage(
            QtDiagnosticText(QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Error: %1"), (unsigned)v));
        return NExitCode::kFatalError;
    } catch (...) {
        ErrorMessage(QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Unknown error"));
        return NExitCode::kFatalError;
    }
}
