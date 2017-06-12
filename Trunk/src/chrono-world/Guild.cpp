//
// Chrono Emu (C) 2016
//
// Guild System
//

#include "StdAfx.h"

Guild::~Guild()
{
	std::list<RankInfo*>::iterator itr = m_rankList.begin();
	for(; itr != m_rankList.end(); ++itr)
		delete (*itr);
	m_rankList.clear();
}
void Guild::SendMessageToGuild(uint64 ExcludePlayer, WorldPacket *data, uint8 Type)
{
	std::list<PlayerInfo*>::iterator i;
	for (i = m_guildMembers.begin(); i != m_guildMembers.end();++i)
	{
		if(Type == G_MSGTYPE_ALLBUTONE)
		{
			if((*i)->guid == ExcludePlayer)
				continue;
		}

		Player *pMember = objmgr.GetPlayer( (*i)->guid);
		if (!pMember || !pMember->IsInWorld() || !pMember->GetSession())
			continue;

		uint32 MemberRank = pMember->GetGuildRank();
		if(Type == G_MSGTYPE_OFFICERCHAT)
		{
			if(!HasRankRight(MemberRank,GR_RIGHT_OFFCHATLISTEN))
				continue;
		}
		else if(Type == G_MSGTYPE_PUBLICCHAT)
		{
			if(!HasRankRight(MemberRank,GR_RIGHT_GCHATLISTEN))
				continue;
		}
		pMember->GetSession()->SendPacket(data);
	}
}

void Guild::GuildMemberLogoff(Player *pMember)
{
	//set offline info
	std::list<PlayerInfo*>::iterator i;
	for (i = m_guildMembers.begin(); i != m_guildMembers.end();++i) 
	{
		if ((*i)->guid == pMember->GetGUID())
		{
			(*i)->lastLevel = pMember->getLevel();
			(*i)->lastZone = pMember->GetZoneId();
			(*i)->Rank = pMember->GetGuildRank();
			(*i)->lastOnline = UNIXTIME;
			return;
		}
	}
}

void Guild::AddNewGuildMember(Player *plyr)
{
	PlayerInfo *gMember=objmgr.GetPlayerInfo(plyr->GetLowGUID());
	gMember->Rank = plyr->GetGuildRank();
	gMember->lastLevel =plyr->getLevel();
	gMember->lastZone =plyr->GetZoneId();
	gMember->publicNote = nullptr;
	gMember->officerNote = nullptr;
	gMember->lastOnline = UNIXTIME;

	AddGuildMember(gMember);
	sHookInterface.OnGuildJoin(plyr, this);
}

bool Guild::DeleteGuildMember(uint64 guid)
{
	std::list<PlayerInfo*>::iterator i;
	for (i = m_guildMembers.begin(); i != m_guildMembers.end();++i) 
	{
		if ((*i)->guid == guid)
		{
			m_guildMembers.erase(i);

			CharacterDatabase.Execute("UPDATE characters SET guildid=0,guildRank=0 WHERE guid=%u AND guildid=%u",GET_LOWGUID_PART(guid), m_guildId);
			return true;
		}
	}
	return false;
}

bool Guild::DeleteGuildMember(string name)
{
	std::list<PlayerInfo*>::iterator i;
	for (i = m_guildMembers.begin(); i != m_guildMembers.end();++i) 
	{
		if (!strcmp((*i)->name, name.c_str()))
		{
			m_guildMembers.erase(i);
			CharacterDatabase.Execute("UPDATE characters SET guildid=0,guildRank=0 WHERE name='%s' AND guildid=%u",CharacterDatabase.EscapeString(name).c_str(), m_guildId);
			return true;
		}
	}
	return false;
}

void Guild::DeleteGuildMembers()
{
	Player *plyr;
	std::list<PlayerInfo*>::iterator i;
	for (i = m_guildMembers.begin(); i != m_guildMembers.end();) 
	{
		plyr = objmgr.GetPlayer( (*i)->guid );
		if(plyr)
		{
			plyr->SetGuildId(0);
			plyr->SetGuildRank(0);
		}
	
		i++;
	}
	m_guildMembers.clear();
}

PlayerInfo* Guild::GetGuildMember(uint64 guid)
{
	std::list<PlayerInfo*>::iterator i;

	for (i = m_guildMembers.begin(); i != m_guildMembers.end();++i) 
	{
		if ((*i)->guid== guid)
		{
			return *i;
		}
	}
	return false;
}

PlayerInfo* Guild::GetGuildMember(std::string name)
{
	std::list<PlayerInfo*>::iterator i;
	for (i = m_guildMembers.begin(); i != m_guildMembers.end();++i) 
	{
		if((*i)->name == name)
			return *i;
	}
	return false;
}

void Guild::FillGuildRosterData(WorldPacket *data)
{
	Player *pMember;
	std::list<PlayerInfo*>::iterator i;
	std::list<RankInfo*>::iterator ritr;

	data->Initialize(SMSG_GUILD_ROSTER);
	*data << uint32(GetGuildMembersCount());
	*data << m_motd;
	*data << m_guildInfo;
	*data << (uint32)m_rankList.size();
	for (ritr = m_rankList.begin(); ritr != m_rankList.end();ritr++)
		*data << (*ritr)->rights;
	for (i = m_guildMembers.begin(); i != m_guildMembers.end(); i++) 
	{
		pMember = objmgr.GetPlayer((*i)->guid);
	
		if (pMember && pMember->GetSession() && pMember->IsInWorld())
		{
			*data << pMember->GetGUID();
			*data << (uint8)1; 
			*data << pMember->GetName();
			*data << pMember->GetGuildRank();
			*data << (uint8)pMember->getLevel();
			*data << pMember->getClass();
			*data << pMember->GetZoneId();
			if((*i)->publicNote)
                *data << (*i)->publicNote;
			else
				*data << uint8(0);

			if((*i)->officerNote)
                *data << (*i)->officerNote;
			else
				*data << uint8(0);
		}
		else
		{
			*data << (*i)->guid;
			*data << uint32(0);		// highguid
			*data << (uint8)0; 
			*data << (*i)->name;
			*data << (*i)->Rank;
			*data << (uint8)((*i)->lastLevel);
			*data << (uint8)((*i)->cl);
			*data << (*i)->lastZone;
			*data << float((UNIXTIME-(*i)->lastOnline)/86400.0);
			if((*i)->publicNote)
                *data << (*i)->publicNote;
			else
				*data << uint8(0);

			if((*i)->officerNote)
				*data << (*i)->officerNote;
			else
				*data << uint8(0);
		}
	}
}

void Guild::BroadCastToGuild(WorldSession *session, std::string msg)
{

	if (session)
	{
		if(session->GetPlayer())
		{	
			uint32 MemberRank = session->GetPlayer()->GetGuildRank();
			if(!HasRankRight(MemberRank,GR_RIGHT_GCHATSPEAK))
				return;

			WorldPacket *data;
			if(session->GetPlayer()->m_modlanguage >=0)
				data = sChatHandler.FillMessageData( CHAT_MSG_GUILD, session->GetPlayer()->m_modlanguage,  msg.c_str(),session->GetPlayer()->GetGUID());
			else
				data = sChatHandler.FillMessageData( CHAT_MSG_GUILD, LANG_UNIVERSAL, msg.c_str(),session->GetPlayer()->GetGUID());
			SendMessageToGuild(0, data, G_MSGTYPE_PUBLICCHAT);
			delete data;
		}
	}
}

void Guild::OfficerChannelChat(WorldSession *session, std::string msg)
{
	if (session)
	{
		if(session->GetPlayer())
		{
			uint32 MemberRank = session->GetPlayer()->GetGuildRank();
			if(!HasRankRight(MemberRank,GR_RIGHT_OFFCHATSPEAK))
				return;

			WorldPacket *data;
			if(session->GetPlayer()->m_modlanguage >=0)
				data = sChatHandler.FillMessageData( CHAT_MSG_OFFICER, session->GetPlayer()->m_modlanguage,  msg.c_str(),session->GetPlayer()->GetGUID());
			else
				data = sChatHandler.FillMessageData( CHAT_MSG_OFFICER, LANG_UNIVERSAL, msg.c_str(),session->GetPlayer()->GetGUID());
			SendMessageToGuild(0, data, G_MSGTYPE_OFFICERCHAT);
			delete data;
		}
	}
}

void Guild::SetPublicNote(uint64 guid, std::string publicNote)
{
	std::list<PlayerInfo*>::iterator i;
	for (i = m_guildMembers.begin(); i != m_guildMembers.end();++i) 
	{
		if ((*i)->guid == guid)
		{
			if((*i)->publicNote)
				free((*i)->publicNote);

			(*i)->publicNote = strdup(publicNote.c_str());
			break;
		}
	}
}

void Guild::SetOfficerNote(uint64 guid, std::string officerNote)
{
	std::list<PlayerInfo*>::iterator i;
	for (i = m_guildMembers.begin(); i != m_guildMembers.end();++i) 
	{
		if ((*i)->guid == guid)
		{
			if((*i)->officerNote)
				free((*i)->officerNote);

			(*i)->officerNote = strdup(officerNote.c_str());
			break;
		}
	}
}

void Guild::CreateRank(std::string name,uint32 rights)
{
	if(m_rankList.size() >= MAX_GUILD_RANKS)
		return;

	RankInfo *newrank;

	newrank = new RankInfo;
	newrank->rankid = (uint32)m_rankList.size();
	newrank->name = name;
	newrank->rights = rights;
	m_rankList.push_back(newrank);
}

std::string Guild::GetRankName(uint32 rankId)
{
	std::list<RankInfo*>::iterator itr;
	if(rankId > m_rankList.size()-1) return "Internal Error";

	for (itr = m_rankList.begin(); itr != m_rankList.end();itr++)
	{
		if ((*itr)->rankid == rankId)
			return (*itr)->name;
	}
	return "None";
}

uint32 Guild::GetRankRights(uint32 rankId)
{
	std::list<RankInfo*>::iterator itr;
	if(rankId > m_rankList.size()-1) return 0;

	for (itr = m_rankList.begin(); itr != m_rankList.end();itr++)
	{
		if ((*itr)->rankid == rankId)
			return (*itr)->rights;
	}
	return 0;
}

void Guild::SetRankName(uint32 rankId, std::string name)
{
	std::list<RankInfo*>::iterator itr;
	if(rankId > m_rankList.size()-1) return;

	for (itr = m_rankList.begin(); itr != m_rankList.end();itr++)
	{
		if ((*itr)->rankid == rankId)
		{
			(*itr)->name = name;
			break;
		}
	}
}

void Guild::SetRankRights(uint32 rankId, uint32 rights)
{
	std::list<RankInfo*>::iterator itr;
	if(rankId > m_rankList.size()-1) return;

	for (itr = m_rankList.begin(); itr != m_rankList.end();itr++)
	{
		if ((*itr)->rankid == rankId)
		{
			(*itr)->rights = rights;
			break;
		}
	}
}

void Guild::FillQueryData(WorldPacket *data)
{
	data->Initialize(SMSG_GUILD_QUERY_RESPONSE);
	*data << GetGuildId();
	*data << GetGuildName();
	std::list<RankInfo*>::iterator itr;
	for (itr = m_rankList.begin(); itr != m_rankList.end();itr++)
		*data << (*itr)->name;
	for (uint32 r = (uint32)m_rankList.size(); r < MAX_GUILD_RANKS;++r)
		*data << uint8(0);
	*data << GetGuildEmblemStyle();
	*data << GetGuildEmblemColor();
	*data << GetGuildBorderStyle();
	*data << GetGuildBorderColor();
	*data << GetGuildBackgroundColor();
}

void Guild::SaveToDb()
{
	std::stringstream query;
	query << "DELETE FROM guilds WHERE guildId = " << m_guildId;
	CharacterDatabase.Execute( query.str( ).c_str( ) );

	query.rdbuf()->str("");

	query << "INSERT INTO guilds VALUES (";
	query << m_guildId << ",'";
	query << CharacterDatabase.EscapeString(m_guildName) << "', ";
	query << m_leaderGuid << ", ";
	query << m_emblemStyle << ", ";
	query << m_emblemColor << ", ";
	query << m_borderStyle << ", ";
	query << m_borderColor << ", ";
	query << m_backgroundColor << ", ";
	query << "'" << CharacterDatabase.EscapeString(m_guildInfo) << "', ";
	query << "'" << CharacterDatabase.EscapeString(m_motd) << "', NOW())";

	CharacterDatabase.Execute( query.str().c_str() );
}

void Guild::UpdateGuildToDb()
{
	std::stringstream query;

	query << "UPDATE guilds SET leaderGuid = " << m_leaderGuid << ", ";
	query << "emblemStyle = " << m_emblemStyle << ", ";
	query << "emblemColor = " << m_emblemColor << ", ";
	query << "borderStyle = " << m_borderStyle << ", ";
	query << "borderColor = " << m_borderColor << ", ";
	query << "backgroundColor = " << m_backgroundColor << ", ";
	query << "guildInfo = '" << CharacterDatabase.EscapeString(m_guildInfo) << "', ";
	query << "motd = '" << CharacterDatabase.EscapeString(m_motd) << "' ";
	query << "WHERE guildId = " << m_guildId;
	CharacterDatabase.Execute( query.str( ).c_str( ) );
}

void Guild::SaveRanksToDb()
{
	std::stringstream query;

	query << "DELETE FROM guild_ranks WHERE guildId = " << m_guildId;
	CharacterDatabase.Execute( query.str( ).c_str( ) );

	std::list<RankInfo*>::iterator itr;
	for (itr = m_rankList.begin(); itr != m_rankList.end();itr++)
	{
		query.rdbuf()->str("");
		query << "INSERT INTO guild_ranks VALUES (";
		query << m_guildId << ", ";
		query << (*itr)->rankid << ", ";
		query << "'" << CharacterDatabase.EscapeString((*itr)->name) << "', ";
		query << (*itr)->rights;
		query << ")";
		CharacterDatabase.Execute( query.str().c_str() );
	}
}

/*
void Guild::SaveGuildMemberToDb(uint64 guid)
{
std::stringstream query;
std::list<PlayerInfo*>::iterator i;

for (i = m_guildMembers.begin(); i != m_guildMembers.end();++i) 
{
if((*i)->guid == guid)
{
query << "DELETE FROM playerguilds WHERE playerId = " << (*i)->guid;
sDatabase.Execute( query.str( ).c_str( ) );
query.rdbuf()->str("");	

query << "INSERT INTO playerguilds VALUES (" << (*i)->guid << ", " << m_guildId << ", '" << (*i)->memberName << "', " << (*i)->Rank << ", '" << (*i)->publicNote << "', '" << (*i)->officerNote << "', " << (*i)->lastOnline << ")";
sDatabase.Execute( query.str( ).c_str( ) );
break;
}
}
}
*/

void Guild::UpdateGuildMembersDB(PlayerInfo *Member)
{
  CharacterDatabase.Execute(
		"UPDATE characters SET publicNote='%s',officerNote='%s',guildid=%u,guildRank=%u WHERE guid=%u",
		Member->publicNote ? CharacterDatabase.EscapeString(Member->publicNote).c_str() : "",
		Member->officerNote ? CharacterDatabase.EscapeString(Member->officerNote).c_str() : "",
		m_guildId,Member->Rank, (uint32)Member->guid );
}

void Guild::SaveAllGuildMembersToDb()
{
	std::list<PlayerInfo*>::iterator i;

	for (i = m_guildMembers.begin(); i != m_guildMembers.end();++i) 
		CharacterDatabase.Execute(
		"UPDATE characters SET publicNote='%s',officerNote='%s',guildid=%u,guildRank=%u WHERE guid=%u",
		(*i)->publicNote ? CharacterDatabase.EscapeString((*i)->publicNote).c_str() : "",
		(*i)->officerNote ? CharacterDatabase.EscapeString((*i)->officerNote).c_str() : "",
		m_guildId,(*i)->Rank,(uint32)( (*i)->guid) );
	  
}

void Guild::RemoveFromDb()
{
	CharacterDatabase.Execute("DELETE FROM guilds WHERE guildId =%u",m_guildId );
	CharacterDatabase.Execute("DELETE FROM guild_ranks WHERE guildId = %u", m_guildId );
	CharacterDatabase.Execute("UPDATE characters SET guildid=0,guildRank=0,publicNote='',officerNote='' WHERE guildid=%u",m_guildId);
}

uint32 Guild::GetFreeGuildIdFromDb()
{

	QueryResult *result = CharacterDatabase.Query( "SELECT MAX(guildId) FROM guilds");
	if(result)
	{
		uint32 guildId = result->Fetch()->GetUInt32();
		delete result;

		return guildId+1;
	}
	return 0;
}

void Guild::LoadGuildCreationDate()
{
	Field *fields;

	QueryResult *result1 = CharacterDatabase.Query( "SELECT DATE_FORMAT(createdate,\"%%d\") FROM guilds WHERE guildId =%u", m_guildId );
	if(!result1) return;
	fields = result1->Fetch();
	m_createdDay = fields[0].GetUInt32();

	delete result1;

	QueryResult *result2 = CharacterDatabase.Query( "SELECT DATE_FORMAT(createdate,\"%%m\") FROM guilds WHERE guildId =%u", m_guildId );
	if(!result2) return;
	fields = result2->Fetch();
	m_createdMonth = fields[0].GetUInt32();

	delete result2;

	QueryResult *result3 = CharacterDatabase.Query( "SELECT DATE_FORMAT(createdate,\"%%Y\") FROM guilds WHERE guildId =%u", m_guildId );
	if(!result3) return;
	fields = result3->Fetch();
	m_createdYear = fields[0].GetUInt32();

	delete result3;
}

void Guild::RenameGuild(std::string guildName)
{
	SetGuildName(guildName);
	CharacterDatabase.Execute("UPDATE guilds SET guildName = '%s' WHERE guildId = %u", CharacterDatabase.EscapeString(guildName).c_str(), GetGuildId());
	uint32 ttime = (uint32)UNIXTIME;

	std::list<PlayerInfo*>::iterator itr = m_guildMembers.begin();
	for (; itr != m_guildMembers.end(); itr++)
	{
		Player * pMember = objmgr.GetPlayer((*itr)->guid);
		if(!pMember) continue;

		pMember->SetUInt32Value(PLAYER_GUILD_TIMESTAMP, ttime);
	}
}