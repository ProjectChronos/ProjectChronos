/*
 * Chrono Emulator
 * Copyright (C) 2010 ChronoEmu Team <http://www.forsakengaming.com/>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "Common.h"
#include "Config/ConfigEnv.h"
#include "Log.h"
#include "NGLog.h"
#include <stdarg.h>

string FormatOutputString(const char * Prefix, const char * Description, bool useTimeStamp)
{

	char p[MAX_PATH];
	p[0] = 0;
	time_t t = time(nullptr);
	tm * a = gmtime(&t);
	strcat(p, Prefix);
	strcat(p, "/");
	strcat(p, Description);
	if(useTimeStamp)
	{
		char ftime[100];
		snprintf(ftime, 100, "-%-4d-%02d-%02d %02d-%02d-%02d", a->tm_year+1900, a->tm_mon+1, a->tm_mday, a->tm_hour, a->tm_min, a->tm_sec);
		strcat(p, ftime);
	}

	strcat(p, ".log");
	return string(p);
}

createFileSingleton( oLog );
createFileSingleton(CLog);
initialiseSingleton( WorldLog );

SERVER_DECL time_t UNIXTIME;
SERVER_DECL tm g_localTime;
#ifndef WIN32
static const char* colorstrings[TBLUE+1] = {
"",
"\033[22;31m",
"\033[22;32m",
"\033[01;33m",
//"\033[22;37m",
"\033[0m",
"\033[01;37m",
"\033[22;34m",
};
#endif

void oLog::outTime()
{
#ifndef WIN32
	char buf[256];
	time_t t = time(nullptr);
	struct tm *tm = localtime(&t);

	if (tm)
	{
		strftime(buf, 256, "[%Y-%m-%d %T] ", tm);
		fprintf(m_file, buf);
	}
#endif
}

void oLog::outString( const char * str, ... )
{
	va_list ap;
	char buf[32768];

	if(m_fileLogLevel < 0 && m_screenLogLevel < 0)
		return;

	va_start(ap, str);
	vsnprintf(buf, 32768, str, ap);
	va_end(ap);
	
	if(m_screenLogLevel >= 0)
	{
		printf(buf);
		putc('\n', stdout);
	}
	if(m_fileLogLevel >= 0 && m_file)
	{
		outTime();
		fprintf(m_file, buf);
		putc('\n', m_file);
	}
}

void oLog::outError( const char * err, ... )
{
	va_list ap;
	char buf[32768];

	if(m_fileLogLevel < 1 && m_screenLogLevel < 1)
		return;

	va_start(ap, err);
	vsnprintf(buf, 32768, err, ap);
	va_end(ap);

	if(m_screenLogLevel >= 1)
	{
#ifdef WIN32
		SetConsoleTextAttribute(stderr_handle, FOREGROUND_RED | FOREGROUND_INTENSITY);
#else
		puts(colorstrings[TRED]);
#endif
		fprintf(stderr, buf);
		putc('\n', stderr);
#ifdef WIN32
		SetConsoleTextAttribute(stderr_handle, FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_GREEN);
#else
		puts(colorstrings[TNORMAL]);
#endif
	}
	if(m_fileLogLevel >= 1 && m_file)
	{
		outTime();
		fprintf(m_file, buf);
		putc('\n', m_file);
	}
}

void oLog::outDetail( const char * str, ... )
{
	va_list ap;
	char buf[32768];

	if(m_fileLogLevel < 2 && m_screenLogLevel < 2)
		return;

	va_start(ap, str);
	vsnprintf(buf, 32768, str, ap);
	va_end(ap);

	if(m_screenLogLevel >= 2)
	{
		printf(buf);
		putc('\n', stdout);
	}
	if(m_fileLogLevel >= 2 && m_file)
	{
		outTime();
		fprintf(m_file, buf);
		putc('\n', m_file);
	}
}

void oLog::outDebug( const char * str, ... )
{
	va_list ap;
	char buf[32768];

	if(m_fileLogLevel < 3 && m_screenLogLevel < 3)
		return;

	va_start(ap, str);
	vsnprintf(buf, 32768, str, ap);
	va_end(ap);

	if(m_screenLogLevel >= 3)
	{
		printf(buf);
		putc('\n', stdout);
	}
	if(m_fileLogLevel >= 3 && m_file)
	{
		outTime();
		fprintf(m_file, buf);
		putc('\n', m_file);
	}
}

void oLog::Init()
{
	m_screenLogLevel = Config.MainConfig.GetIntDefault("LogLevel", "Screen", 0);
	m_fileLogLevel = Config.MainConfig.GetIntDefault("LogLevel", "File", 0);
	m_file = nullptr;

	if (m_fileLogLevel >= 0)
	{
		const char *filename = "file.log";
		m_file = fopen(filename, "w");
		if (m_file == nullptr)
		{
			fprintf(stderr, "%s: Error opening '%s': %s\n", __FUNCTION__, filename, strerror(errno));
		}
	}

	// get error handle
#ifdef WIN32
	stderr_handle = GetStdHandle(STD_ERROR_HANDLE);
	stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
#endif
}

void SessionLogWriter::write(const char* format, ...)
{
	if(!m_file)
		return;

	va_list ap;
	va_start(ap, format);
	char out[32768];

	time_t t = time(nullptr);
	tm* aTm = localtime(&t);
	sprintf(out, "[%-4d-%02d-%02d %02d:%02d:%02d] ",aTm->tm_year+1900,aTm->tm_mon+1,aTm->tm_mday,aTm->tm_hour,aTm->tm_min,aTm->tm_sec);
	size_t l = strlen(out);
	vsnprintf(&out[l], 32768 - l, format, ap);

	fprintf(m_file, "%s\n", out);
	va_end(ap);
}

WorldLog::WorldLog()
{
	bEnabled = false;
	bEnabledXml = false;
	onlyPlayer = nullptr;
	m_file=nullptr;
	m_xml=nullptr;

	if (Config.MainConfig.GetBoolDefault("LogLevel", "World", false))
	{
		Log.Notice("WorldLog", "Enabling packetlog output to \"world.log\"");
		Enable();
	} else {
		Disable();
	}

	if (Config.MainConfig.GetBoolDefault("LogLevel", "WorldXml", false))
	{
		Log.Notice("WorldLog", "Enabling packetlog output to \"world.xml\"");
		EnableXml();
	} else {
		DisableXml();
	}

}

void WorldLog::Enable()
{
	if(bEnabled)
		return;

	bEnabled = true;
	if(m_file != nullptr)
	{
		Disable();
		bEnabled=true;
	}
	m_file = fopen("world.log", "w");
}

void WorldLog::Disable()
{
	if(!bEnabled)
		return;

	bEnabled = false;
	if(!m_file)
		return;

	fflush(m_file);
	fclose(m_file);
	m_file=nullptr;
}

void WorldLog::EnableXml()
{
	if(bEnabledXml)
		return;

	bEnabledXml = true;
	if(m_xml != nullptr)
	{
		DisableXml();
		bEnabledXml=true;
	}
	m_xml = fopen("world.xml", "w");
	if (m_xml)
	{
		fprintf(m_xml, "<?xml version=\"1.0\" ?><log>");
	}
}

void WorldLog::DisableXml()
{
	if(!bEnabledXml)
		return;

	bEnabledXml = false;
	if(!m_xml)
		return;

	fprintf(m_xml, "</log>");
	fflush(m_xml);
	fclose(m_xml);
	m_xml=nullptr;
}

WorldLog::~WorldLog()
{
	if (m_file)
	{
		fclose(m_file);
		m_file = nullptr;
	}

	if (m_xml)
	{
		fprintf(m_xml, "</log>");
		fclose(m_xml);
		m_xml = nullptr;
	}
}

void oLog::outColor(uint32 colorcode, const char * str, ...)
{
	if( !str ) return;
	va_list ap;
	va_start(ap, str);
#ifdef WIN32
	SetConsoleTextAttribute(stdout_handle, colorcode);
#else
	printf(colorstrings[colorcode]);
#endif
	vprintf( str, ap );
	fflush(stdout);
	va_end(ap);
}

void SessionLogWriter::Open()
{
	m_file = fopen(m_filename, "a");
}

void SessionLogWriter::Close()
{
	if(!m_file) return;
	fflush(m_file);
	fclose(m_file);
	m_file=nullptr;
}

SessionLogWriter::SessionLogWriter(const char * filename, bool open)
{
	m_filename = strdup(filename);
	m_file=nullptr;
	if(open)
		Open();
}

SessionLogWriter::~SessionLogWriter()
{
	if(m_file)
		Close();

	free(m_filename);
}


