#include "stdafx.h"

using namespace std::experimental::filesystem;

// Throw some error message
inline void errorMsg( const std::wstring& errorText )
{
	MessageBoxW( NULL, errorText.c_str(), reinterpret_cast<LPCWSTR>(L"Error !"), MB_ICONERROR | MB_OK );
	TerminateProcess( GetCurrentProcess(), EXIT_FAILURE );
}

// Inject a Steam overlay library
inline void overlayInject( const HANDLE game_process, const LPVOID function_address, const std::wstring& module_path )
{
	// Allocate some memory
	auto base_address = VirtualAllocEx( game_process, NULL, wcslen( module_path.c_str() ) * sizeof( wchar_t ), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );
	if (base_address == nullptr)
		errorMsg( L"Can not allocate the required base address in the remote process." );

	// Write the path into the remote process	
	auto write_path = WriteProcessMemory( game_process, base_address, module_path.c_str(), wcslen( module_path.c_str() ) * sizeof( wchar_t ), NULL );

	if (write_path == FALSE)
		errorMsg( L"Can not write the path inside the remote process." );

	// Make the remote process invoke the function
	auto invoke_function = CreateRemoteThread( game_process, NULL, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(function_address), base_address, NULL, 0 );
	if (!invoke_function || invoke_function == INVALID_HANDLE_VALUE)
		errorMsg( L"Can not invoke the required function." );

	// Wait for the thread to exit
	WaitForSingleObject( invoke_function, INFINITE );

	// Free the memory in the remote process
	VirtualFreeEx( game_process, base_address, 0, MEM_RELEASE );

	// Close a thread handle
	CloseHandle( invoke_function );
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

	winreg::RegKey steam_reg{ HKEY_CURRENT_USER, L"Software\\Valve\\Steam" };
	CSimpleIniW config_ini;

	// Retrieve a Steam path from registry
	std::wstring steam_path = steam_reg.GetStringValue( L"SteamPath" );
	if (!steam_path.empty())
	{
        #ifdef _WIN64
		auto steam_overlay_lib = (path( steam_path ) / "GameOverlayRenderer64.dll");
        #else
		auto steam_overlay_lib = (path( steam_path ) / "GameOverlayRenderer.dll");
        #endif

		if (exists( steam_overlay_lib ))
		{
			volatile bool ini_found = false;

			path current_directory = current_path();
			for (auto& listed_files : recursive_directory_iterator( current_directory ))
			{
				if (listed_files.path().filename().compare( L"cream_api.ini" ) == 0)
				{
					ini_found = true;

					if (config_ini.LoadFile( listed_files.path().c_str() ) == SI_Error::SI_OK)
					{
						PROCESS_INFORMATION process_info;
						STARTUPINFOW startup_info;

						ZeroMemory( &startup_info, sizeof( startup_info ) );
						startup_info.cb = sizeof startup_info;

						if (CreateProcessW( config_ini.GetValue( L"steam", L"exetoload", L"valve.exe" ), NULL, NULL, NULL, FALSE, 0, NULL, NULL, &startup_info, &process_info ))
						{
							WaitForSingleObject( process_info.hProcess, 1000 );

							// Get the address of a LoadLibraryW function
							auto load_library = GetProcAddress( GetModuleHandleW( L"kernel32" ), "LoadLibraryW" );

							// Inject the overlay library
							overlayInject( process_info.hProcess, load_library, steam_overlay_lib );

							CloseHandle( process_info.hThread );
							CloseHandle( process_info.hProcess );

							return 0;
						}
					}
					else
						errorMsg( L"cream_api.ini can not be loaded." );
				}
			}
			if (!ini_found)
				errorMsg( L"cream_api.ini was not found." );
		}
		else
			errorMsg( L"Steam overlay library does not exist." );
	}
	else
		errorMsg( L"Steam is probably not installed." );

    return -1;
}