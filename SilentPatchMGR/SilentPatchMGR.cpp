#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include "Utils/MemoryMgr.h"
#include "Utils/Patterns.h"

#include <shlwapi.h>
#include <ShlObj.h>
#include <shellapi.h>
#include <array>
#include <optional>
#include <string>

#include <Commctrl.h>

#pragma comment(lib, "Shlwapi.lib")

#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#pragma comment(lib, "Comctl32.lib")

#define DEBUG_DOCUMENTS_PATH	1

HMODULE hDLLModule;

std::wstring& TrimZeros( std::wstring& str )
{
	auto pos = str.find_last_not_of( L'\0' );
	if ( pos == std::string::npos )
	{
		str.clear();
	}
	else
	{
		str.erase( pos + 1 );
	}
	return str;
}

const std::wstring& GetINIPath()
{
	static const std::wstring path = [] {
		std::wstring result(MAX_PATH, '\0');
		GetModuleFileNameW( hDLLModule, result.data(), result.size() - 3 ); // Minus max required space for extension
		PathRenameExtensionW( result.data(), L".ini" );
		return TrimZeros( result );
	}();
	return path;
}

namespace FSFix
{
	namespace internal
	{
		bool DirectoryExists( LPCWSTR lpPath )
		{
			const DWORD attribs = GetFileAttributesW( lpPath );
			return attribs != INVALID_FILE_ATTRIBUTES && (attribs & FILE_ATTRIBUTE_DIRECTORY) != 0;
		}

		std::wstring GetUserProfilePath( size_t& documentsPathLength )
		{
			std::wstring result(MAX_PATH, '\0');
			if ( GetEnvironmentVariableW( L"USERPROFILE", result.data(), result.size() ) != 0 )
			{
				PathAppendW( result.data(), L"Documents" );
				documentsPathLength = wcslen( result.c_str() ); // We'll restore the null terminator later for deleting directories
				PathAppendW( result.data(), L"MGR" );
			}
			return TrimZeros( result );
		}

		std::wstring GetFixedDocumentsPath()
		{
			std::wstring result(MAX_PATH, '\0');
			PWSTR documentsPath;
#if DEBUG_DOCUMENTS_PATH
			HRESULT hr = S_OK;
			constexpr wchar_t debugPath[] = L"H:\\ŻąłóРстуぬねのはen";
			documentsPath = static_cast<PWSTR>(CoTaskMemAlloc( sizeof(debugPath) ));
			memcpy( documentsPath, debugPath, sizeof(debugPath) );
#else
			HRESULT hr = SHGetKnownFolderPath( FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &documentsPath );
#endif
			if ( SUCCEEDED(hr) )
			{
				PathCombineW( result.data(), documentsPath, L"MGR" );
				CoTaskMemFree( documentsPath );
			}
			return TrimZeros( result );
		}

		constexpr const wchar_t* OPT_RELOCATE_SAVE_DIR = L"RelocateSaveDirectory";
		std::optional<bool> ReadSaveRelocOption( const std::wstring& iniPath )
		{
			std::optional<bool> result;

			int iniOption = GetPrivateProfileIntW( L"SilentPatch", OPT_RELOCATE_SAVE_DIR, -1, iniPath.c_str() );
			if ( iniOption != -1 )
			{
				result = iniOption != 0;
			}

			return result;
		}

		void WriteSaveRelocOption( const std::wstring& iniPath, bool reloc )
		{
			WritePrivateProfileStringW( L"SilentPatch", OPT_RELOCATE_SAVE_DIR, std::to_wstring(reloc).c_str(), iniPath.c_str() );
		}

		const wchar_t* GetSaveDataPath()
		{
			static const std::array<wchar_t, MAX_PATH> path = [] {
				size_t userProfileDirPos = 0;
				const auto userProfilePath = GetUserProfilePath( userProfileDirPos );
				const auto documentsPath = GetFixedDocumentsPath();

				const auto& iniPath = GetINIPath();

				bool useDocumentsPath = false;
				
				auto relocIniOption = ReadSaveRelocOption( iniPath );
				if ( relocIniOption )
				{
					// Use option from INI and skip any relocation
					useDocumentsPath = *relocIniOption;
				}
				else
				{
					// Try to figure out if both directories are identical (either same path or pointing to the same directory via a hard link)
					bool skipMoveQuestion = true;
					if ( HANDLE sourceDirectory = CreateFileW( userProfilePath.data(), 0, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr ); sourceDirectory != INVALID_HANDLE_VALUE )
					{
						// Source exists so we have something to move
						skipMoveQuestion = false;
						if ( HANDLE destinationDirectory = CreateFileW( documentsPath.data(), 0, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr ); destinationDirectory != INVALID_HANDLE_VALUE )
						{	
							FILE_ID_INFO srcIdInfo, destIdInfo;
							if ( GetFileInformationByHandleEx( sourceDirectory, FileIdInfo, &srcIdInfo, sizeof(srcIdInfo) ) != FALSE &&
								GetFileInformationByHandleEx( destinationDirectory, FileIdInfo, &destIdInfo, sizeof(destIdInfo) ) != FALSE )
							{
								skipMoveQuestion = srcIdInfo.VolumeSerialNumber == destIdInfo.VolumeSerialNumber &&
													memcmp( &srcIdInfo.FileId, &destIdInfo.FileId, sizeof(srcIdInfo.FileId) ) == 0;
							}
							else
							{
								// Windows 7 fallback
								BY_HANDLE_FILE_INFORMATION srcInfo, destInfo;
								if ( GetFileInformationByHandle( sourceDirectory, &srcInfo) != FALSE && GetFileInformationByHandle( destinationDirectory, &destInfo) != FALSE )
								{
									skipMoveQuestion = srcInfo.dwVolumeSerialNumber == destInfo.dwVolumeSerialNumber &&
														srcInfo.nFileIndexLow == destInfo.nFileIndexLow && srcInfo.nFileIndexHigh == destInfo.nFileIndexHigh;
								}
							}
							CloseHandle( destinationDirectory );
						}

						CloseHandle( sourceDirectory );
					}


					if ( !skipMoveQuestion )
					{
						auto fnDialogFunc = [] ( HWND hwnd, UINT msg, WPARAM, LPARAM, LONG_PTR ) -> HRESULT
						{
							if ( msg == TDN_CREATED )
							{
								HMODULE gameModule = GetModuleHandle( nullptr );
								if ( HICON mainIcon = LoadIcon( gameModule, TEXT("MAINICON") ); mainIcon != nullptr )
								{
									SendMessage( hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(mainIcon) );
								}
								if ( HICON smallIcon = LoadIcon( gameModule, TEXT("SMALLICON") ); smallIcon != nullptr )
								{
									SendMessage( hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon) );
								}
							}

							return S_OK;
						};

						std::wstring contentString;
						contentString.append( L"SilentPatch found your save games in the following location:\n\n" );
						contentString.append( userProfilePath.data() );
						contentString.append( L"\n\nThis does not appear to be your real Documents directory. "
												L"Do you wish to relocate them to the following instead?\n\n" );
						contentString.append( documentsPath.data() );
						contentString.append( L"\n\nThis way saves will be located in your real Documents directory and "
												L"Steam Cloud will start to function properly.\n\n"
												L"If you select \"Don't ask me again\" your choice will be remembered in " );
						contentString.append( PathFindFileNameW( iniPath.c_str() ) );

						contentString.append( L" file." );

						TASKDIALOGCONFIG dialogConfig { sizeof(dialogConfig) };
						dialogConfig.dwFlags = TDF_CAN_BE_MINIMIZED;
						dialogConfig.dwCommonButtons = TDCBF_YES_BUTTON|TDCBF_NO_BUTTON;
						dialogConfig.pszWindowTitle = L"SilentPatch";
						dialogConfig.pszContent = contentString.c_str();
						dialogConfig.nDefaultButton = IDYES;
						dialogConfig.pszVerificationText = L"Don't ask me again";
						dialogConfig.pszMainIcon = TD_INFORMATION_ICON;
						dialogConfig.pfCallback = fnDialogFunc;

						int buttonResult;
						BOOL dontAskAgain;
						if ( SUCCEEDED(TaskDialogIndirect( &dialogConfig, &buttonResult, nullptr, &dontAskAgain )) )
						{
							if ( buttonResult == IDYES )
							{
								// Copying just to be sure it's double null terminated at the end
								auto stringToDoubleNullTerminated = []( std::wstring str )
								{
									str.append( 2, L'\0' );
									return str;
								};

								const std::wstring source = stringToDoubleNullTerminated( userProfilePath );
								const std::wstring destination = stringToDoubleNullTerminated( documentsPath );

								SHFILEOPSTRUCTW moveOp = {};
								moveOp.wFunc = FO_MOVE;
								moveOp.fFlags = FOF_NOCONFIRMMKDIR;
								moveOp.pFrom = source.c_str();
								moveOp.pTo = destination.c_str();
								const int moveResult = SHFileOperationW( &moveOp );
								if ( moveResult == 0 )
								{
									if ( moveOp.fAnyOperationsAborted == FALSE )
									{
										// Remember "Yes" only now, after everything succeeded
										if ( dontAskAgain != FALSE )
										{
											WriteSaveRelocOption( iniPath, true );
										}

										// All went fine, only NOW we can decide to use a real Documents path
										useDocumentsPath = true;

										// Also try to delete the original Documents directory - it's likely empty now
										RemoveDirectoryW( std::wstring(userProfilePath.data(), userProfileDirPos).c_str() );
									}
									else
									{
										MessageBoxW( nullptr, L"Move operation has been aborted by the user. The game will continue using an original save path.\n\n"
																L"Please verify that all your saves are still present in the source folder - if not, move them back from the destination folder.",
																L"SilentPatch", MB_OK|MB_ICONERROR|MB_SETFOREGROUND );
									}
								}
								else
								{
									MessageBoxW( nullptr, L"Move operation failed. The game will continue using an original save path.\n\n"
										L"Please verify that all your saves are still present in the source folder - if not, move them back from the destination folder.",
										
										L"SilentPatch", MB_OK|MB_ICONERROR|MB_SETFOREGROUND );
								}
							}
							else
							{
								// Remember "No" instantly
								if ( dontAskAgain != FALSE )
								{
									WriteSaveRelocOption( iniPath, false );
								}
							}
						}
					}
				}

				std::array<wchar_t, MAX_PATH> result;
				PathCombineW( result.data(), useDocumentsPath ? documentsPath.data() : userProfilePath.data(), L"SaveData" );
				return result;
			} ();

			return path.data();
		}

		BOOL CreateDirectoryRecursively( LPCWSTR dirName )
		{
			if ( DirectoryExists( dirName ) )
			{
				return TRUE;
			}

			// Try to create a parent directory
			wchar_t parentDir[MAX_PATH];
			PathCombineW( parentDir, dirName, L".." );

			BOOL result = CreateDirectoryRecursively( parentDir );
			if ( result != FALSE )
			{
				result = CreateDirectoryW( dirName, nullptr );
			}

			return result;
		}

		void GetFinalPath( char* utfBuffer, size_t bufferSize )
		{
			const wchar_t* saveDataPath = internal::GetSaveDataPath();
			WideCharToMultiByte( CP_UTF8, 0, saveDataPath, -1, utfBuffer, bufferSize, nullptr, nullptr );
		}

		void GetFinalPath( char* utfBuffer, size_t bufferSize, const char* fileName )
		{
			GetFinalPath( utfBuffer, bufferSize );
			PathAppendA( utfBuffer, fileName );
		}
	
		HANDLE WINAPI CreateFileUTF8( LPCSTR utfFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile )
		{
			int requiredSize = MultiByteToWideChar( CP_UTF8, 0, utfFileName, -1, nullptr, 0 );
			wchar_t* wideBuffer = static_cast<wchar_t*>(_malloca( sizeof(wideBuffer[0]) * requiredSize ));
			MultiByteToWideChar( CP_UTF8, 0, utfFileName, -1, wideBuffer, requiredSize );

			HANDLE result = CreateFileW( wideBuffer, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile );

			_freea( wideBuffer );
			return result;
		}

		BOOL WINAPI CloseHandleChecked( HANDLE hObject )
		{
			BOOL result = TRUE;
			if ( hObject != INVALID_HANDLE_VALUE )
			{
				result = CloseHandle( hObject );
			}
			return result;
		}
	}

	auto* const pCreateFileUTF8 = &internal::CreateFileUTF8;
	auto* const pCloseHandleChecked = &internal::CloseHandleChecked;

	BOOL CreateDirectoryRecursivelyUTF8( LPCSTR utfDirName )
	{
		int requiredSize = MultiByteToWideChar( CP_UTF8, 0, utfDirName, -1, nullptr, 0 );
		wchar_t* wideBuffer = static_cast<wchar_t*>(_malloca( sizeof(wideBuffer[0]) * requiredSize ));
		MultiByteToWideChar( CP_UTF8, 0, utfDirName, -1, wideBuffer, requiredSize );

		BOOL result = internal::CreateDirectoryRecursively( wideBuffer );

		_freea( wideBuffer );
		return result;
	}

	void sprintf_GetGraphicsOption( char* utfBuffer, size_t bufferSize )
	{
		internal::GetFinalPath( utfBuffer, bufferSize, "GraphicOption" );	
	}

	void sprintf_GetSaveData( char* utfBuffer, size_t bufferSize )
	{
		internal::GetFinalPath( utfBuffer, bufferSize );
	}

	void sprintf_GetFormatArgument( char* utfBuffer, size_t bufferSize, const char* /*format*/, const char* /*arg1*/, const char* fileName )
	{
		internal::GetFinalPath( utfBuffer, bufferSize, fileName );	
	}

	void sprintf_AppendGraphicsOption( char* utfBuffer, size_t /*bufferSize*/ )
	{
		PathAppendA( utfBuffer, "GraphicOption" );
	}

	void sprintf_AppendFormatArgument( char* utfBuffer, size_t /*bufferSize*/, const char* /*format*/, const char* /*arg1*/, const char* fileName )
	{
		PathAppendA( utfBuffer, fileName );
	}
}

static void InitASI()
{
	std::unique_ptr<ScopedUnprotect::Unprotect> Protect = ScopedUnprotect::UnprotectSectionOrFullModule( GetModuleHandle( nullptr ), ".text" );

	using namespace Memory;
	using namespace hook;

	// Path fixes
	// Not only the game uses %USERPROFILE%\Documents as a path to Documents,
	// but also sticks to ANSI which will make it not create save games for users with a "weird" user name
	// I fix this by correcting code logic for obtaining the directory and storing paths as UTF-8,
	// then converting them to wide paths when actually using them.
	// This way I can reuse existing buffers game provides and not worry about thread safety.
	{
		using namespace FSFix;

		// CreateDirectoryRecursively replaced with a UTF-8 friendly version
		{
			void* createDirRecursive = get_pattern( "56 8B B4 24 10 02 00 00 56", -6 );
			InjectHook( createDirRecursive, CreateDirectoryRecursivelyUTF8, PATCH_JUMP );
		}


		// getenv_s NOP'd
		{
			void* getEnvFunc = get_pattern( "59 33 DB 89 5D FC", -0x13 );
			Patch<uint8_t>( getEnvFunc, 0xC3 ); // retn
		}

		
		// ReadGraphicsOptions:
		{
			auto readGraphicsOptions = pattern( "83 C4 20 6A 00 68 80 00 00 00 6A 03" ).get_one();	

			// sprintf_s replaced with a function to obtain path to GraphicOption file
			InjectHook( readGraphicsOptions.get<void>( -5 ), sprintf_GetGraphicsOption );

			Patch( readGraphicsOptions.get<void>( 0x1E + 2 ), &pCreateFileUTF8 );
		}


		// WriteGraphicsOptions:
		{
			auto writeGraphicsOptions = pattern( "68 00 01 00 00 50 E8 ? ? ? ? 8D 4C 24 28 51 E8" ).get_one();

			// sprintf_s replaced with a function to obtain path to SaveData
			InjectHook( writeGraphicsOptions.get<void>( 6 ), sprintf_GetSaveData );

			// sprintf_s replaced with a function to append GraphicOption
			InjectHook( writeGraphicsOptions.get<void>( 0x43 ), sprintf_AppendGraphicsOption );

			Patch( writeGraphicsOptions.get<void>( 0x62 + 2 ), &pCreateFileUTF8 );

			// Don't close invalid handles
			Patch( writeGraphicsOptions.get<void>( 0x8C + 2 ), &pCloseHandleChecked );
		}

		
		// WriteSaveDataUnused (seems unused but maybe it's not, so patching it just in case):
		{
			auto writeSaveDataUnused = pattern( "83 C4 24 68 ? ? ? ? B9" ).get_one();

			// sprintf_s replaced with a function to obtain path to MGR.sav file
			InjectHook( writeSaveDataUnused.get<void>( -5 ), sprintf_GetFormatArgument );

			Patch( writeSaveDataUnused.get<void>( 0xC9 + 2 ), &pCreateFileUTF8 );

			// Don't close invalid handles
			Patch( writeSaveDataUnused.get<void>( 0x175 + 2 ), &pCloseHandleChecked );
		}


		// ReadSaveData:
		{
			auto readSaveData = pattern( "8B F0 83 FE FF 75 1E" ).get_one();

			// sprintf_s replaced with a function to obtain path to MGR.sav (from argument)
			InjectHook( readSaveData.get<void>( -0x25 ), sprintf_GetFormatArgument );

			Patch( readSaveData.get<void>( -6 + 2 ), &pCreateFileUTF8 );
		}

		
		// DataSave:
		{
			auto dataSave = pattern( "68 00 01 00 00 50 E8 ? ? ? ? 8D 4C 24 40" ).get_one();

			// sprintf_s replaced with a function to obtain path to SaveData
			InjectHook( dataSave.get<void>( 6 ), sprintf_GetSaveData );

			// sprintf_s replaced with a function to append MGR.sav (from argument)
			InjectHook( dataSave.get<void>( 0x47 ), sprintf_AppendFormatArgument );

			Patch( dataSave.get<void>( 0x4C + 2 ), &pCreateFileUTF8 );
			Patch( dataSave.get<void>( 0x1BA + 2 ), &pCreateFileUTF8 );

			Patch( dataSave.get<void>( 0x19A + 2 ), &pCloseHandleChecked );
			Patch( dataSave.get<void>( 0x264 + 2 ), &pCloseHandleChecked );
		}


		// SaveDataDelete:
		{
			auto saveDataDelete = pattern( "8B F0 83 FE FF 75 19" ).get_one();

			// sprintf_s replaced with a function to obtain path to MGR.sav (from argument)
			InjectHook( saveDataDelete.get<void>( -0x25 ), sprintf_GetFormatArgument );

			Patch( saveDataDelete.get<void>( -6 + 2 ), &pCreateFileUTF8 );
			Patch( saveDataDelete.get<void>( 0xCB + 2 ), &pCreateFileUTF8 );

			Patch( saveDataDelete.get<void>( 0x18C + 2 ), &pCloseHandleChecked );
		}
	}


	// Skip intro splashes
	if ( int skipIntros = GetPrivateProfileIntW( L"SilentPatch", L"SkipIntroSplashes", -1, GetINIPath().c_str() ); skipIntros != -1 )
	{
		if ( skipIntros != 0 )
		{
			auto showLogoSequence = pattern( "8B 8D 8C 00 00 00 85 C9" ).get_one();
			Patch<uint8_t>( showLogoSequence.get<void>( 8 ), 0xEB ); // je -> jmp
		}
	}

}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
	UNREFERENCED_PARAMETER(lpReserved);

	switch ( reason )
	{
	case DLL_PROCESS_ATTACH:
	{
		hDLLModule = hModule;
		break;
	}
	}

	return TRUE;
}


extern "C"
{
	static LONG InitCount = 0;
	__declspec(dllexport) void InitializeASI()
	{
		if ( _InterlockedCompareExchange( &InitCount, 1, 0 ) != 0 ) return;
		InitASI();
	}
}
