
#include "pch.h"
#include "Common/Public/DebugOutput.h"
#include "Primitives/FormatString.hpp"

#include <csignal>
#include <iostream>
#include <Windows.h>

namespace shz
{

	namespace
	{

		class ConsoleSetUpHelper
		{
		public:
			ConsoleSetUpHelper()
			{
				// Set proper console mode to ensure colored output (required ENABLE_VIRTUAL_TERMINAL_PROCESSING flag is not set
				// by default for stdout when starting an app from Windows terminal).
				for (auto StdHandle : { GetStdHandle(STD_OUTPUT_HANDLE), GetStdHandle(STD_ERROR_HANDLE) })
				{
					DWORD Mode = 0;
					// https://docs.microsoft.com/en-us/windows/console/setconsolemode
					if (GetConsoleMode(StdHandle, &Mode))
					{
						// Characters written by the WriteFile or WriteConsole function or echoed by the ReadFile or
						// ReadConsole function are parsed for ASCII control sequences, and the correct action is performed.
						// Backspace, tab, bell, carriage return, and line feed characters are processed. It should be
						// enabled when using control sequences or when ENABLE_VIRTUAL_TERMINAL_PROCESSING is set.
						Mode |= ENABLE_PROCESSED_OUTPUT;

						// When writing with WriteFile or WriteConsole, characters are parsed for VT100 and similar
						// control character sequences that control cursor movement, color/font mode, and other operations
						// that can also be performed via the existing Console APIs.
						Mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;

						SetConsoleMode(StdHandle, Mode);
					}
				}
			}
		};

	} // namespace

	namespace
	{
		enum class ETextColor
		{
			Auto, // Text color is determined based on message severity

			Default,

			Black,
			DarkRed,
			DarkGreen,
			DarkYellow,
			DarkBlue,
			DarkMagenta,
			DarkCyan,
			DarkGray,

			Red,
			Green,
			Yellow,
			Blue,
			Magenta,
			Cyan,
			White,
			Gray
		};

		String formatDebugMessage(DEBUG_MESSAGE_SEVERITY Severity,
			const Char* Message,
			const char* Function, // type of __FUNCTION__
			const char* File,     // type of __FILE__
			int Line)
		{
			std::stringstream msg_ss;

			static const Char* const strSeverities[] = { "Info", "Warning", "ERROR", "CRITICAL ERROR" };
			const auto* MessageSevery = strSeverities[static_cast<int>(Severity)];

			msg_ss << "Shizen Engine: " << MessageSevery;
			if (Function != nullptr || File != nullptr)
			{
				msg_ss << " in ";
				if (Function != nullptr)
				{
					msg_ss << Function << "()";
					if (File != nullptr)
						msg_ss << " (";
				}

				if (File != nullptr)
				{
					msg_ss << File << ", " << Line << ')';
				}
			}
			msg_ss << ": " << Message << '\n';

			return msg_ss.str();
		}

		const char* textColorToTextColorCode(DEBUG_MESSAGE_SEVERITY Severity, ETextColor Color)
		{
			switch (Color)
			{
			case ETextColor::Auto:
			{
				switch (Severity)
				{
				case DEBUG_MESSAGE_SEVERITY_INFO:
					return TextColorCode::Default;

				case DEBUG_MESSAGE_SEVERITY_WARNING:
					return TextColorCode::Yellow;

				case DEBUG_MESSAGE_SEVERITY_ERROR:
				case DEBUG_MESSAGE_SEVERITY_FATAL_ERROR:
					return TextColorCode::Red;

				default:
					return TextColorCode::Default;
				}
			}
#define TEX_COLOR_TO_CODE(Color) \
    case ETextColor::Color: return TextColorCode::Color

			TEX_COLOR_TO_CODE(Default);

			TEX_COLOR_TO_CODE(Black);
			TEX_COLOR_TO_CODE(DarkRed);
			TEX_COLOR_TO_CODE(DarkGreen);
			TEX_COLOR_TO_CODE(DarkYellow);
			TEX_COLOR_TO_CODE(DarkBlue);
			TEX_COLOR_TO_CODE(DarkMagenta);
			TEX_COLOR_TO_CODE(DarkCyan);
			TEX_COLOR_TO_CODE(DarkGray);

			TEX_COLOR_TO_CODE(Red);
			TEX_COLOR_TO_CODE(Green);
			TEX_COLOR_TO_CODE(Yellow);
			TEX_COLOR_TO_CODE(Blue);
			TEX_COLOR_TO_CODE(Magenta);
			TEX_COLOR_TO_CODE(Cyan);
			TEX_COLOR_TO_CODE(White);
#undef TEX_COLOR_TO_CODE

			default:
				return TextColorCode::Default;
			}
		}

		void OutputDebugMessage(DEBUG_MESSAGE_SEVERITY Severity,
			const Char* Message,
			const char* Function,
			const char* File,
			int Line,
			ETextColor Color)
		{
			static ConsoleSetUpHelper SetUpConsole;

			auto msg = formatDebugMessage(Severity, Message, Function, File, Line);
			OutputDebugStringA(msg.c_str());

			const auto* ColorCode = textColorToTextColorCode(Severity, Color);
			std::cout << ColorCode << msg << TextColorCode::Default;
		}

		static void OutputDebugMessage(DEBUG_MESSAGE_SEVERITY Severity,
			const Char* Message,
			const char* Function,
			const char* File,
			int Line)
		{
			return OutputDebugMessage(Severity, Message, Function, File, Line, ETextColor::Auto);
		}
	}

	DebugMessageCallbackType DebugMessageCallback = OutputDebugMessage;

} // namespace shz
