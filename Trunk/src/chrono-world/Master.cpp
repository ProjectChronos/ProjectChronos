/*
 * Project Chronos
 * Copyright (C) 2010 ChronoEmu Team <http://www.forsakengaming.com/>
 * Copyright (C) 2017 Project Chronos <https://github.com/ProjectChronos/ProjectChronos/>
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

#include "StdAfx.h"

#define BANNER "Project Chronos r%u/%s-%s-%s :: World Server\n"

#ifndef WIN32
#include <sched.h>
#endif

#include "svn_revision.h"

#include <signal.h>

createFileSingleton( Master );
std::string LogFileName;
bool bLogChat;
bool crashed = false;

volatile bool Master::m_stopEvent = false;

// Database defines.
SERVER_DECL Database* Database_Character;
SERVER_DECL Database* Database_World;

// mainserv defines
SessionLogWriter* GMCommand_Log;
SessionLogWriter* Anticheat_Log;
SessionLogWriter* Player_Log;
extern DayWatcherThread * dw;
extern CharacterLoaderThread * ctl;

void Master::_OnSignal(int s)
{
	switch (s)
	{
#ifndef WIN32
	case SIGHUP:
		sWorld.Rehash(true);
		break;
#endif
	case SIGINT:
	case SIGTERM:
	case SIGABRT:
#ifdef _WIN32
	case SIGBREAK:
#endif
		Master::m_stopEvent = true;
		break;
	}

	signal(s, _OnSignal);
}

Master::Master()
{

}

Master::~Master()
{
}

struct Addr
{
	unsigned short sa_family;
	/* sa_data */
	unsigned short Port;
	unsigned long IP; // inet_addr
	unsigned long unusedA;
	unsigned long unusedB;
};

#define DEF_VALUE_NOT_SET 0xDEADBEEF

#ifdef WIN32
        static const char* default_config_file = "chrono-world.conf";
        static const char* default_realm_config_file = "chrono-realms.conf";
#else
        static const char* default_config_file = CONFDIR "/chrono-world.conf";
        static const char* default_realm_config_file = CONFDIR "/chrono-realms.conf";
#endif

bool bServerShutdown = false;
bool StartConsoleListener();
void CloseConsoleListener();
ThreadContext * GetConsoleListener();

bool Master::Run(int argc, char ** argv)
{
	char * config_file = (char*)default_config_file;
	char * realm_config_file = (char*)default_realm_config_file;

	int file_log_level = DEF_VALUE_NOT_SET;
	int screen_log_level = DEF_VALUE_NOT_SET;
	int do_check_conf = 0;
	int do_version = 0;
	int do_cheater_check = 0;
	int do_database_clean = 0;
	time_t curTime;

	struct chrono_option longopts[] =
	{
		{ "checkconf",			chrono_no_argument,				&do_check_conf,			1		},
		{ "version",			chrono_no_argument,				&do_version,			1		},
		{ "conf",				chrono_required_argument,		nullptr,					'c'		},
		{ "realmconf",			chrono_required_argument,		nullptr,					'r'		},
		{ 0, 0, 0, 0 }
	};

	char c;
	while ((c = chrono_getopt_long_only(argc, argv, ":f:", longopts, nullptr)) != -1)
	{
		switch (c)
		{
		case 'c':
			config_file = new char[strlen(chrono_optarg)];
			strcpy(config_file, chrono_optarg);
			break;

		case 'r':
			realm_config_file = new char[strlen(chrono_optarg)];
			strcpy(realm_config_file, chrono_optarg);
			break;

		case 0:
			break;
		default:
			printf("Usage: %s [--checkconf] [--conf <filename>] [--realmconf <filename>] [--version]\n", argv[0]);
			return true;
		}
	}

	// Startup banner
	UNIXTIME = time(nullptr);
	g_localTime = *localtime(&UNIXTIME);

	printf(BANNER, BUILD_REVISION, CONFIG, PLATFORM_TEXT, ARCH);
	printf("Built at %s on %s by %s@%s\n", BUILD_TIME, BUILD_DATE, BUILD_USER, BUILD_HOST);
	Log.Line();

	if(do_version)
		return true;

	if( do_check_conf )
	{
		Log.Notice( "Config", "Checking config file: %s", config_file );
		if( Config.MainConfig.SetSource(config_file, true ) )
			Log.Success( "Config", "Passed without errors." );
		else
			Log.Warning( "Config", "Encountered one or more errors." );

		Log.Notice( "Config", "Checking config file: %s\n", realm_config_file );
		if( Config.RealmConfig.SetSource( realm_config_file, true ) )
			Log.Success( "Config", "Passed without errors.\n" );
		else
			Log.Warning( "Config", "Encountered one or more errors.\n" );

		/* test for die variables */
		string die;
		if( Config.MainConfig.GetString( "die", "msg", &die) || Config.MainConfig.GetString("die2", "msg", &die ) )
			Log.Warning( "Config", "Die directive received: %s", die.c_str() );

		return true;
	}

	printf( "The key combination <Ctrl-C> will safely shut down the server at any time.\n" );
	Log.Line();
    
#ifndef WIN32
	if(geteuid() == 0 || getegid() == 0)
		Log.LargeErrorMessage( LARGERRORMESSAGE_WARNING, "You are running Ascent as root.", "This is not needed, and may be a possible security risk.", "It is advised to hit CTRL+C now and", "start as a non-privileged user.", nullptr);
#endif

	InitRandomNumberGenerators();
	Log.Success( "Rnd", "Initialized Random Number Generators." );

	ThreadPool.Startup();
	uint32 LoadingTime = getMSTime();

	Log.Notice( "Config", "Loading Config Files...\n" );
	if( Config.MainConfig.SetSource( config_file ) )
		Log.Success( "Config", ">> chrono-world.conf" );
	else
	{
		Log.Error( "Config", ">> chrono-world.conf" );
		return false;
	}

	string die;
	if( Config.MainConfig.GetString( "die", "msg", &die) || Config.MainConfig.GetString( "die2", "msg", &die ) )
	{
		Log.Notice( "Config", "Die directive received: %s", die.c_str() );
		return false;
	}	

	if(Config.RealmConfig.SetSource(realm_config_file))
		Log.Success( "Config", ">> chrono-realms.conf" );
	else
	{
		Log.Error( "Config", ">> chrono-realms.conf" );
		return false;
	}

	if( !_StartDB() )
	{
		Database::CleanupLibs();
		return false;
	}

	// Whats the world DB version??
	if (!CheckWorldDBVersion())
	{
		return false;
	}

	Log.Line();
	sLog.Init();
	sLog.outString( "" );

	//ScriptSystem = new ScriptEngine;
	//ScriptSystem->Reload();

	new EventMgr;
	new World;

	// open cheat log file
	Anticheat_Log = new SessionLogWriter(FormatOutputString( "logs", "cheaters", false).c_str(), false );
	GMCommand_Log = new SessionLogWriter(FormatOutputString( "logs", "gmcommand", false).c_str(), false );
	Player_Log = new SessionLogWriter(FormatOutputString( "logs", "players", false).c_str(), false );

	/* load the config file */
	sWorld.Rehash(false);

	// Initialize Opcode Table
	WorldSession::InitPacketHandlerTable();

	string host = Config.MainConfig.GetStringDefault( "Listen", "Host", DEFAULT_HOST );
	int wsport = Config.MainConfig.GetIntDefault( "Listen", "WorldServerPort", DEFAULT_WORLDSERVER_PORT );

	new ScriptMgr;

	if( !sWorld.SetInitialWorldSettings() )
	{
		Log.Error( "Server", "SetInitialWorldSettings() failed. Something went wrong? Exiting." );
		return false;
	}

	g_bufferPool.Init();
	sWorld.SetStartTime((uint32)UNIXTIME);
	
	WorldRunnable * wr = new WorldRunnable();
	ThreadPool.ExecuteTask(wr);

	_HookSignals();

	ConsoleThread * console = new ConsoleThread();
	ThreadPool.ExecuteTask(console);

	uint32 realCurrTime, realPrevTime;
	realCurrTime = realPrevTime = getMSTime();

	// Socket loop!
	uint32 start;
	uint32 diff;
	uint32 last_time = now();
	uint32 etime;

	// Start Network Subsystem
	sLog.outString( "Starting network subsystem..." );
	new SocketMgr;
	new SocketGarbageCollector;
	sSocketMgr.SpawnWorkerThreads();

	LoadingTime = getMSTime() - LoadingTime;
	sLog.outString ( "\nServer is ready for connections. Startup time: %ums\n", LoadingTime );

	Log.Notice("RemoteConsole", "Starting...");
	if( StartConsoleListener() )
	{
#ifdef WIN32
		ThreadPool.ExecuteTask( GetConsoleListener() );
#endif
		Log.Notice("RemoteConsole", "Now open.");
	}
	else
	{
		Log.Warning("RemoteConsole", "Not enabled or failed listen.");
	}
	
 
	/* write pid file */
	FILE * fPid = fopen( "ascent.pid", "w" );
	if( fPid )
	{
		uint32 pid;
#ifdef WIN32
		pid = GetCurrentProcessId();
#else
		pid = getpid();
#endif
		fprintf( fPid, "%u", (unsigned int)pid );
		fclose( fPid );
	}
#ifdef WIN32
	HANDLE hThread = GetCurrentThread();
#endif

	uint32 loopcounter = 0;
	//ThreadPool.Gobble();

#ifndef CLUSTERING
	/* Connect to realmlist servers / logon servers */
	new LogonCommHandler();
	sLogonCommHandler.Startup();

	// Create listener
	ListenSocket<WorldSocket> * ls = new ListenSocket<WorldSocket>(host.c_str(), wsport);
    bool listnersockcreate = ls->IsOpen();
#ifdef WIN32
	if( listnersockcreate )
		ThreadPool.ExecuteTask(ls);
#endif
	while( !m_stopEvent && listnersockcreate )
#else
	new ClusterInterface;
	sClusterInterface.ConnectToRealmServer();
	while(!m_stopEvent)
#endif
	{
		start = now();
		diff = start - last_time;
		if(! ((++loopcounter) % 10000) )		// 5mins
		{
			ThreadPool.ShowStats();
			ThreadPool.IntegrityCheck();
			g_bufferPool.Optimize();
		}

		/* since time() is an expensive system call, we only update it once per server loop */
		curTime = time(nullptr);
		if( UNIXTIME != curTime )
		{
			UNIXTIME = time(nullptr);
			g_localTime = *localtime(&curTime);
		}

/*#ifndef CLUSTERING
		sClusterInterface.Update();
#endif*/
		sSocketGarbageCollector.Update();

		/* UPDATE */
		last_time = now();
		etime = last_time - start;
		if( 50 > etime )
		{
#ifdef WIN32
			WaitForSingleObject( hThread, 50 - etime );
#else
			Sleep( 50 - etime );
#endif
		}
	}
	_UnhookSignals();

    wr->Terminate();
	ThreadPool.ShowStats();
	/* Shut down console system */
	console->terminate();
	delete console;

	// begin server shutdown
	Log.Notice( "Shutdown", "Initiated at %s", ConvertTimeStampToDataTime( (uint32)UNIXTIME).c_str() );

	if( lootmgr.is_loading )
	{
		Log.Notice( "Shutdown", "Waiting for loot to finish loading..." );
		while( lootmgr.is_loading )
			Sleep( 100 );
	}

	// send a query to wake it up if its inactive
	Log.Notice( "Database", "Clearing all pending queries..." );

	// kill the database thread first so we don't lose any queries/data
	CharacterDatabase.EndThreads();
	WorldDatabase.EndThreads();

	Log.Notice("Rnd", "~Rnd()");
	CleanupRandomNumberGenerators();

	Log.Notice( "CharacterLoaderThread", "Exiting..." );
	ctl->Terminate();
	ctl = nullptr;

	Log.Notice( "DayWatcherThread", "Exiting..." );
	dw->terminate();
	dw = nullptr;

#ifndef CLUSTERING
	ls->Close();
#endif

	CloseConsoleListener();
	delete ls;
	
	sWorld.SaveAllPlayers();

	Log.Notice( "Network", "Shutting down network subsystem." );
#ifdef WIN32
	sSocketMgr.ShutdownThreads();
#endif
	sSocketMgr.CloseAll();

	bServerShutdown = true;
	ThreadPool.Shutdown();

	sWorld.LogoutPlayers();
	sLog.outString( "" );

	delete LogonCommHandler::getSingletonPtr();

	sWorld.ShutdownClasses();
	Log.Notice( "World", "~World()" );
	delete World::getSingletonPtr();

	sScriptMgr.UnloadScripts();
	delete ScriptMgr::getSingletonPtr();
	
	Log.Notice( "ChatHandler", "~ChatHandler()" );
	delete ChatHandler::getSingletonPtr();

	Log.Notice( "EventMgr", "~EventMgr()" );
	delete EventMgr::getSingletonPtr();

	Log.Notice( "Database", "Closing Connections..." );
	_StopDB();

	Log.Notice( "Network", "Deleting Network Subsystem..." );
	delete SocketMgr::getSingletonPtr();
	delete SocketGarbageCollector::getSingletonPtr();

#ifdef ENABLE_LUA_SCRIPTING
	sLog.outString("Deleting Script Engine...");
	LuaEngineMgr::getSingleton().Unload();
#endif
	//delete ScriptSystem;

	delete GMCommand_Log;
	delete Anticheat_Log;
	delete Player_Log;

	// remove pid
	remove( "ascent.pid" );
	g_bufferPool.Destroy();

	Log.Notice( "Shutdown", "Shutdown complete." );

#ifdef WIN32
	WSACleanup();

	// Terminate Entire Application
	//HANDLE pH = OpenProcess(PROCESS_TERMINATE, TRUE, GetCurrentProcessId());
	//TerminateProcess(pH, 0);
	//CloseHandle(pH);

#endif

	return true;
}

static const char *REQUIRED_WORLD_DB_VERSION = "06_12_2017_chronos_db_version";

bool Master::CheckWorldDBVersion()
{
	QueryResult* wqr = WorldDatabase.QueryNA("SELECT LastUpdate FROM chronos_db_version;");
	if (wqr == NULL)
	{
		Log.Error("Database", "World database is missing the table `chronos_db_version` OR the table doesn't contain any rows. Can't validate database version. Exiting.");
		Log.Error("Database", "You may need to update your database");
		return false;
	}

	Field* f = wqr->Fetch();
	const char *WorldDBVersion = f->GetString();

	Log.Notice("Database", "Last world database update: %s", WorldDBVersion);
	int result = strcmp(WorldDBVersion, REQUIRED_WORLD_DB_VERSION);
	if (result != 0)
	{
		Log.Error("Database", "Last world database update doesn't match the required one which is %s.", REQUIRED_WORLD_DB_VERSION);

		if (result < 0)
		{
			Log.Error("Database", "You need to apply the world update queries that are newer than %s. Exiting.", WorldDBVersion);
			Log.Error("Database", "You can find the world update queries in the chronodb\chrono_world_updates sub-directory of your project chronos source directory.");
		}
		else
			Log.Error("Database", "Your world database is probably too new for this version, you need to update your server. Exiting.");

		delete wqr;
		return false;
	}

	delete wqr;
	return true;
}

bool Master::_StartDB()
{
	string hostname, username, password, database;
	int port = 0;
	// Configure Main Database
	
	bool result = Config.MainConfig.GetString( "WorldDatabase", "Username", &username );
	Config.MainConfig.GetString( "WorldDatabase", "Password", &password );
	result = !result ? result : Config.MainConfig.GetString( "WorldDatabase", "Hostname", &hostname );
	result = !result ? result : Config.MainConfig.GetString( "WorldDatabase", "Name", &database );
	result = !result ? result : Config.MainConfig.GetInt( "WorldDatabase", "Port", &port );
	Database_World = Database::Create();

	if(result == false)
	{
		DEBUG_LOG( "sql: One or more parameters were missing from WorldDatabase directive." );
		return false;
	}

	// Initialize it
	if( !WorldDatabase.Initialize(hostname.c_str(), (unsigned int)port, username.c_str(),
		password.c_str(), database.c_str(), Config.MainConfig.GetIntDefault( "WorldDatabase", "ConnectionCount", 3 ), 16384 ) )
	{
		DEBUG_LOG( "sql: Main database initialization failed. Exiting." );
		return false;
	}

	result = Config.MainConfig.GetString( "CharacterDatabase", "Username", &username );
	Config.MainConfig.GetString( "CharacterDatabase", "Password", &password );
	result = !result ? result : Config.MainConfig.GetString( "CharacterDatabase", "Hostname", &hostname );
	result = !result ? result : Config.MainConfig.GetString( "CharacterDatabase", "Name", &database );
	result = !result ? result : Config.MainConfig.GetInt( "CharacterDatabase", "Port", &port );
	Database_Character = Database::Create();

	if(result == false)
	{
		DEBUG_LOG( "sql: One or more parameters were missing from Database directive." );
		return false;
	}

	// Initialize it
	if( !CharacterDatabase.Initialize( hostname.c_str(), (unsigned int)port, username.c_str(),
		password.c_str(), database.c_str(), Config.MainConfig.GetIntDefault( "CharacterDatabase", "ConnectionCount", 5 ), 16384 ) )
	{
		DEBUG_LOG( "sql: Main database initialization failed. Exiting." );
		return false;
	}

	return true;
}

void Master::_StopDB()
{
	delete Database_World;
	delete Database_Character;
}

#ifndef WIN32
// Unix crash handler :oOoOoOoOoOo
volatile bool m_crashed = false;
void segfault_handler(int c)
{
	if( m_crashed )
	{
		abort();
		return;		// not reached
	}

	m_crashed = true;

	printf ("Segfault handler entered...\n");
	try
	{
		if( World::getSingletonPtr() != 0 )
		{
			sLog.outString( "Waiting for all database queries to finish..." );
			WorldDatabase.EndThreads();
			CharacterDatabase.EndThreads();
			sLog.outString( "All pending database operations cleared.\n" );
			sWorld.SaveAllPlayers();
			sLog.outString( "Data saved." );
		}
	}
	catch(...)
	{
		sLog.outString( "Threw an exception while attempting to save all data." );
	}

	printf("Writing coredump...\n");
	abort();
}
#endif


void Master::_HookSignals()
{
	signal( SIGINT, _OnSignal );
	signal( SIGTERM, _OnSignal );
	signal( SIGABRT, _OnSignal );
#ifdef _WIN32
	signal( SIGBREAK, _OnSignal );
#else
	signal( SIGHUP, _OnSignal );
	signal(SIGUSR1, _OnSignal);
	
	// crash handler
	signal(SIGSEGV, segfault_handler);
	signal(SIGFPE, segfault_handler);
	signal(SIGILL, segfault_handler);
	signal(SIGBUS, segfault_handler);
#endif
}

void Master::_UnhookSignals()
{
	signal( SIGINT, 0 );
	signal( SIGTERM, 0 );
	signal( SIGABRT, 0 );
#ifdef _WIN32
	signal( SIGBREAK, 0 );
#else
	signal( SIGHUP, 0 );
#endif

}

#ifdef WIN32

Mutex m_crashedMutex;

// Crash Handler
void OnCrash( bool Terminate )
{
	sLog.outString( "Advanced crash handler initialized." );

	if( !m_crashedMutex.AttemptAcquire() )
		TerminateThread( GetCurrentThread(), 0 );

	try
	{
		if( World::getSingletonPtr() != 0 )
		{
			sLog.outString( "Waiting for all database queries to finish..." );
			WorldDatabase.EndThreads();
			CharacterDatabase.EndThreads();
			sLog.outString( "All pending database operations cleared.\n" );
			sWorld.SaveAllPlayers();
			sLog.outString( "Data saved." );
		}
	}
	catch(...)
	{
		sLog.outString( "Threw an exception while attempting to save all data." );
	}

	sLog.outString( "Closing." );
	
	// beep
	//printf("\x7");
	
	// Terminate Entire Application
	if( Terminate )
	{
		HANDLE pH = OpenProcess( PROCESS_TERMINATE, TRUE, GetCurrentProcessId() );
		TerminateProcess( pH, 1 );
		CloseHandle( pH );
	}
}

#endif