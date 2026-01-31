#include "Application.h"
#include "IO.h"
#include "SusParser.h"
#include "ScoreConverter.h"
#include "Score.h"
#include <iostream>
#include <filesystem>

namespace mmw = MikuMikuWorld;
mmw::Application app;

// Batch convert SUS files to CCMMWS format
int batchConvert(const std::string& inputDir, const std::string& outputDir)
{
	namespace fs = std::filesystem;
	
	int successCount = 0;
	int failCount = 0;
	
	std::cout << "Batch converting SUS files from: " << inputDir << std::endl;
	std::cout << "Output directory: " << outputDir << std::endl;
	
	for (const auto& entry : fs::recursive_directory_iterator(inputDir))
	{
		if (!entry.is_regular_file())
			continue;
			
		std::string ext = entry.path().extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
		
		if (ext != ".txt" && ext != ".sus")
			continue;
		
		try
		{
			// Parse SUS file
			mmw::SusParser parser;
			mmw::Score score = mmw::ScoreConverter::susToScore(parser.parse(entry.path().string()));
			
			// Create output path preserving relative structure
			fs::path relativePath = fs::relative(entry.path(), inputDir);
			fs::path outPath = fs::path(outputDir) / relativePath;
			outPath.replace_extension(".ccmmws");
			
			// Create directory if needed
			fs::create_directories(outPath.parent_path());
			
			// Save as CCMMWS
			mmw::serializeScore(score, outPath.string());
			successCount++;
			
			if ((successCount + failCount) % 100 == 0)
				std::cout << "Progress: " << (successCount + failCount) << " files processed" << std::endl;
		}
		catch (const std::exception& e)
		{
			failCount++;
			std::cerr << "Error converting " << entry.path().filename().string() << ": " << e.what() << std::endl;
		}
	}
	
	std::cout << "\nConversion complete: " << successCount << " successful, " << failCount << " failed" << std::endl;
	return failCount > 0 ? 1 : 0;
}

int main()
{
	int argc;
	LPWSTR* args;
	args = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (!args)
	{
		IO::messageBox(APP_NAME, "CommandLineToArgvW failed...", IO::MessageBoxButtons::Ok,
		               IO::MessageBoxIcon::Error);
		return 1;
	}

	// Check for batch conversion mode
	if (argc >= 4)
	{
		std::string arg1 = IO::wideStringToMb(args[1]);
		if (arg1 == "--batch-convert")
		{
			std::string inputDir = IO::wideStringToMb(args[2]);
			std::string outputDir = IO::wideStringToMb(args[3]);
			return batchConvert(inputDir, outputDir);
		}
	}

	try
	{
		std::string dir = IO::File::getFilepath(IO::wideStringToMb(args[0]));
		mmw::Result result = app.initialize(dir);

		if (!result.isOk())
			throw(std::exception(result.getMessage().c_str()));

		for (int i = 1; i < argc; ++i)
			app.appendOpenFile(IO::wideStringToMb(args[i]));

		app.handlePendingOpenFiles();
		app.run();
	}
	catch (const std::out_of_range& ex)
	{
		// Capture stack trace for map access errors
		std::string msg =
		    std::string(
		        "MAP ACCESS ERROR - Invalid key in map/vector\n\n")
		        .append("Error: ").append(ex.what())
		        .append("\n\nThis crash was caused by accessing a non-existent map key or vector index.")
		        .append("\nPlease report this with your score file.")
		        .append("\n\nApplication Version: ")
		        .append(mmw::Application::getAppVersion());

		IO::messageBox(APP_NAME, msg, IO::MessageBoxButtons::Ok, IO::MessageBoxIcon::Error);
	}
	catch (const std::exception& ex)
	{
		std::string msg =
		    std::string(
		        "An unhandled exception has occurred and the application will now close.\n\n")
		        .append(ex.what())
		        .append("\n\nApplication Version: ")
		        .append(mmw::Application::getAppVersion());

		IO::messageBox(APP_NAME, msg, IO::MessageBoxButtons::Ok, IO::MessageBoxIcon::Error);
	}

	app.dispose();
	return 0;
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_TIMER:
		if (mmw::Application::windowState.windowDragging &&
		    wParam == mmw::Application::windowState.windowTimerId)
		{
			// grabbing the glfw window blocks the message queue causing the application to stop
			// rendering so we handle the message ourselves and update the UI explicitly
			if (app.getGlfwWindow())
				app.update();

			return 0;
		}
		break;

	case WM_ENTERSIZEMOVE:
		mmw::Application::windowState.windowDragging = true;
		break;

	case WM_EXITSIZEMOVE:
		mmw::Application::windowState.windowDragging = false;
		break;

	case WM_DROPFILES:
		if (HDROP dropHandle = reinterpret_cast<HDROP>(wParam); dropHandle != NULL)
		{
			const UINT filesCount = ::DragQueryFileW(dropHandle, 0xFFFFFFFF, NULL, 0u);
			for (UINT i = 0; i < filesCount; ++i)
			{
				const UINT bufferSize = ::DragQueryFileW(dropHandle, i, NULL, 0u);
				if (bufferSize > 0)
				{
					std::wstring wFilename(bufferSize + 1, 0);
					if (::DragQueryFileW(dropHandle, i, wFilename.data(),
					                     static_cast<UINT>(wFilename.size())) != 0)
						app.appendOpenFile(IO::wideStringToMb(wFilename.data()));
				}
			}

			::DragFinish(dropHandle);
		}
		break;

	default:
		// we don't handle this message ourselves so delegate it to the original glfw window's proc
		return CallWindowProcW((WNDPROC)glfwGetWindowUserPointer(app.getGlfwWindow()), hwnd, uMsg,
		                       wParam, lParam);
	}

	return ::DefWindowProcW(hwnd, uMsg, wParam, lParam);
}
