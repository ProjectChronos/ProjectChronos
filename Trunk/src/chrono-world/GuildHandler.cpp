//
// Chrono Emu (C) 2016
//
// Guild System
//

#include "StdAfx.h"

void WorldSession::HandleGuildQuery(WorldPacket & recv_data)
{
	CHECK_PACKET_SIZE(recv_data, 4);
	WorldPacket data;

	Guild *pGuild;
	uint32 guildId;

	recv_data >> guildId;

	pGuild = objmgr.GetGuild( guildId );

	if(!pGuild)
		return;

	pGuild->FillQueryData(&data);
	SendPacket(&data);
}

void WorldSession::HandleCreateGuild(WorldPacket & recv_data)
{
	sLog.outDebug("GUILD CREATE");
	/*	std::string guildName;
	uint64 count;
	int i;
	Player * plyr = GetPlayer();

	if(!plyr)
	return;

	recv_data >> guildName;

	Guild *pGuild = new Guild;

	count = objmgr.GetTotalGuildCount();

	pGuild->SetGuildId( (uint32)(count+1) );
	pGuild->SetGuildName( guildName );
	pGuild->SetGuildRankName("Guild Master", 0);
	pGuild->SetGuildRankName("Officer", 1);
	pGuild->SetGuildRankName("Veteran", 2);  
	pGuild->SetGuildRankName("Member", 3);
	pGuild->SetGuildRankName("Initiate", 4);

	for(i = 5;i < 10;i++)
	pGuild->SetGuildRankName("Unused", i);

	pGuild->SetGuildEmblemStyle( 0xFFFF );
	pGuild->SetGuildEmblemColor( 0xFFFF );
	pGuild->SetGuildBorderStyle( 0xFFFF );
	pGuild->SetGuildBorderColor( 0xFFFF );
	pGuild->SetGuildBackgroundColor( 0xFFFF );

	objmgr.AddGuild(pGuild);

	plyr->SetGuildId( pGuild->GetGuildId() );
	//plyr->SetUInt32Value(PLAYER_GUILDID, pGuild->GetGuildId() );
	plyr->SetGuildRank(GUILDRANK_GUILD_MASTER);
	//plyr->SetUInt32Value(PLAYER_GUILDRANK,GUILDRANK_GUILD_MASTER);
	pGuild->SetGuildLeaderGuid( plyr->GetGUID() );

	pGuild->AddNewGuildMember( plyr );

	pGuild->SaveToDb();*/
}

void WorldSession::SendGuildCommandResult(uint32 typecmd,const char *  str,uint32 cmdresult)
{
	WorldPacket data;
	data.SetOpcode(SMSG_GUILD_COMMAND_RESULT);
	data << typecmd;
	data << str;
	data << cmdresult;
	SendPacket(&data);
}

void WorldSession::HandleInviteToGuild(WorldPacket & recv_data)
{
	CHECK_PACKET_SIZE(recv_data, 1);
	WorldPacket data;
	std::string inviteeName;

	recv_data >> inviteeName;

	Player *plyr = objmgr.GetPlayer( inviteeName.c_str() , false);
	Player *inviter = GetPlayer();
	Guild *pGuild = objmgr.GetGuild( inviter->GetGuildId() );
	 
	
	if(!plyr)
	{
		SendGuildCommandResult(GUILD_INVITE_S,inviteeName.c_str(),GUILD_PLAYER_NOT_FOUND);
		return;
	}
	else if(!pGuild)
	{
		SendGuildCommandResult(GUILD_CREATE_S,"",GUILD_PLAYER_NOT_IN_GUILD);
		return;
	}
	if( plyr->GetGuildId() )
	{
		SendGuildCommandResult(GUILD_INVITE_S,plyr->GetName(),ALREADY_IN_GUILD);
		return;
	}
	else if( plyr->GetGuildInvitersGuid())
	{
		SendGuildCommandResult(GUILD_INVITE_S,plyr->GetName(),ALREADY_INVITED_TO_GUILD);
		return;
	}
	else if(!pGuild->HasRankRight(inviter->GetGuildRank(),GR_RIGHT_INVITE))
	{
		SendGuildCommandResult(GUILD_INVITE_S,"",GUILD_PERMISSIONS);
		return;
	}
	else if(plyr->GetTeam()!=_player->GetTeam() && _player->GetSession()->GetPermissionCount() == 0)
	{
		SendGuildCommandResult(GUILD_INVITE_S,"",GUILD_NOT_ALLIED);
		return;
	}
	SendGuildCommandResult(GUILD_INVITE_S,inviteeName.c_str(),GUILD_U_HAVE_INVITED);
	//41
  
	data.Initialize(SMSG_GUILD_INVITE);
	data << inviter->GetName();
	data << pGuild->GetGuildName();
	plyr->GetSession()->SendPacket(&data);
	plyr->SetGuildInvitersGuid( inviter->GetLowGUID() );	
}

void WorldSession::HandleGuildAccept(WorldPacket & recv_data)
{
	Player *plyr = GetPlayer();

	if(!plyr)
		return;

	Player *inviter = objmgr.GetPlayer( plyr->GetGuildInvitersGuid() );
	plyr->UnSetGuildInvitersGuid();

	if(!inviter)
	{
		return;
	}

	Guild *pGuild = objmgr.GetGuild( inviter->GetGuildId() );

	if(!pGuild)
	{
		return;
	}

	plyr->SetGuildId( pGuild->GetGuildId() );
	plyr->SetGuildRank(GUILDRANK_INITIATE);

	pGuild->AddNewGuildMember( plyr );
	//plyr->SaveGuild();

	WorldPacket data;
	data.Initialize(SMSG_GUILD_EVENT);
	data << uint8(GUILD_EVENT_JOINED);
	data << uint8(1);
	data << plyr->GetName();
	pGuild->SendMessageToGuild(0, &data, G_MSGTYPE_ALL);
}

void WorldSession::HandleGuildDecline(WorldPacket & recv_data)
{
	WorldPacket data;

	Player *plyr = GetPlayer();

	if(!plyr)
		return;

	Player *inviter = objmgr.GetPlayer( plyr->GetGuildInvitersGuid() );
	plyr->UnSetGuildInvitersGuid(); 

	if(!inviter)
		return;

	data.Initialize(SMSG_GUILD_DECLINE);
	data << plyr->GetName();
	inviter->GetSession()->SendPacket(&data);
}
void WorldSession::HandleSetGuildInformation(WorldPacket & recv_data)
{
	CHECK_PACKET_SIZE(recv_data, 1);
	WorldPacket data;
	std::string NewGuildInfo;
	recv_data >> NewGuildInfo;

	Guild *pGuild = objmgr.GetGuild( GetPlayer()->GetGuildId() );

	if(!pGuild)
		return;

	uint32 plyrRank = GetPlayer()->GetGuildRank();

	if(!pGuild->HasRankRight(plyrRank,GR_RIGHT_EGUILDINFO))
	{
		SendGuildCommandResult(3,"",GUILD_PERMISSIONS);
		 return;
	}

	pGuild->SetGuildInfo(NewGuildInfo);

	pGuild->FillGuildRosterData(&data);
	GetPlayer()->GetSession()->SendPacket(&data);

	pGuild->UpdateGuildToDb();
}

void WorldSession::HandleGuildInfo(WorldPacket & recv_data)
{
	WorldPacket data;

	Guild *pGuild = objmgr.GetGuild( GetPlayer()->GetGuildId() );

	if(!pGuild)
		return;

	data.Initialize(SMSG_GUILD_INFO);//not sure
	data << pGuild->GetGuildName();
	data << pGuild->GetCreatedYear();
	data << pGuild->GetCreatedMonth();
	data << pGuild->GetCreatedDay();
	data << uint32(pGuild->GetGuildMembersCount());
	data << uint32(pGuild->GetGuildMembersCount());//accountcount

	SendPacket(&data);
}

void WorldSession::HandleGuildRoster(WorldPacket & recv_data)
{
	WorldPacket data;

	Guild *pGuild = objmgr.GetGuild( GetPlayer()->GetGuildId() );

	if(!pGuild)
		return;

	pGuild->FillGuildRosterData(&data);
	GetPlayer()->GetSession()->SendPacket(&data);
}

void WorldSession::HandleGuildPromote(WorldPacket & recv_data)
{
	CHECK_PACKET_SIZE(recv_data, 1);
	std::string name;
	int32 plyrRank;
	int32 pTargetRank;

	recv_data >> name;

	Player *plyr = GetPlayer();

	if(!plyr)
		return;

	Guild *pGuild = objmgr.GetGuild( plyr->GetGuildId() );

	if(!pGuild)
		return;

	PlayerInfo *pGuildMember = pGuild->GetGuildMember(name);

	if(!pGuildMember)
	{
		 SendGuildCommandResult(GUILD_FOUNDER_S,name.c_str(),GUILD_PLAYER_NOT_IN_GUILD_S);
		return;
	}

	plyrRank = plyr->GetGuildRank();
	pTargetRank = pGuildMember->Rank;

	if(!pGuild->HasRankRight(plyrRank,GR_RIGHT_PROMOTE))
	{
		 SendGuildCommandResult(3,"",GUILD_PERMISSIONS);
		return;
	}

	if( plyr->GetLowGUID() == pGuildMember->guid )
	{
		sChatHandler.SystemMessage(this, "You cant promote yourself!");
		return;
	}

	if( plyrRank >= pTargetRank )
	{
		sChatHandler.SystemMessage(this, "You must be a higher ranking guild member than the person you are going to promote!");
		return;
	}

	if( (pTargetRank - plyrRank) == 1 )
	{
		sChatHandler.SystemMessage(this, "You can only promote up to one rank below yours!");
		return;
	}		  

	pTargetRank--;

	if(pTargetRank < GUILDRANK_GUILD_MASTER)
		pTargetRank = GUILDRANK_GUILD_MASTER;

	pGuildMember->Rank = pTargetRank;

	Player *pTarget = objmgr.GetPlayer( name.c_str() );
	if(pTarget)
	{
		//They Online
		if(pTarget->GetInstanceID() != _player->GetInstanceID())
		{
			sEventMgr.AddEvent(pTarget, &Player::SetGuildRank, (uint32)pTargetRank, 1, 1, 1,EVENT_FLAG_DO_NOT_EXECUTE_IN_WORLD_CONTEXT);
		}
		else
		{
			pTarget->SetGuildRank(pTargetRank);
		}
	//	pTarget->SetUInt32Value(PLAYER_GUILDRANK, pTargetRank);
	}

	//Save new Info to DB
	pGuild->UpdateGuildMembersDB(pGuildMember);

	//check if its unused rank or not

	WorldPacket data(SMSG_GUILD_EVENT, 100);
	data << uint8(GUILD_EVENT_PROMOTION);
	data << uint8(3);
	data << plyr->GetName();
	data << name.c_str();
	data << pGuild->GetRankName( pTargetRank );
	pGuild->SendMessageToGuild(0, &data, G_MSGTYPE_ALL);
}

void WorldSession::HandleGuildDemote(WorldPacket & recv_data)
{
	CHECK_PACKET_SIZE(recv_data, 1);
	std::string name;
	uint32 plyrRank;
	uint32 pTargetRank;

	recv_data >> name;

	Player *plyr = GetPlayer();

	if(!plyr)
		return;

	Guild *pGuild = objmgr.GetGuild( plyr->GetGuildId() );

	if(!pGuild)
		return;

	PlayerInfo *pGuildMember = pGuild->GetGuildMember(name);

	if(!pGuildMember)
	{
		SendGuildCommandResult(GUILD_FOUNDER_S,name.c_str(),GUILD_PLAYER_NOT_IN_GUILD_S);
		return;
	}

	plyrRank = plyr->GetGuildRank();
	pTargetRank = pGuildMember->Rank;

	if(!pGuild->HasRankRight(plyrRank,GR_RIGHT_DEMOTE))
	{
		 SendGuildCommandResult(3,"",GUILD_PERMISSIONS);
		return;
	}

	if( plyr->GetLowGUID() == pGuildMember->guid )
	{
		sChatHandler.SystemMessage(this, "You cant demote yourself!");
		return;
	}

	if( plyrRank >= pTargetRank )
	{
		sChatHandler.SystemMessage(this, "You must be a higher ranking guild member than the person you are going to demote!");
		return;
	}

	if(pTargetRank == pGuild->GetNrRanks()-1)
	{
		sChatHandler.SystemMessage(this, "You cannot demote that member any further!");
		return;
	}

	pTargetRank++;
	if(pTargetRank > pGuild->GetNrRanks()-1)
		pTargetRank = (uint32)pGuild->GetNrRanks()-1;

	pGuildMember->Rank = pTargetRank;

	Player *pTarget = objmgr.GetPlayer( name.c_str() );
	if(pTarget)
	{
		//They Online
		pTarget->SetGuildRank(pTargetRank);
		//pTarget->SetUInt32Value(PLAYER_GUILDRANK, pTargetRank);
	}

	//Save new Info to DB
	pGuild->UpdateGuildMembersDB(pGuildMember);

	WorldPacket data(SMSG_GUILD_EVENT, 100);
	data << uint8(GUILD_EVENT_DEMOTION);
	data << uint8(3);
	data << plyr->GetName();
	data << name.c_str();
	data << pGuild->GetRankName( pTargetRank );
	pGuild->SendMessageToGuild(0, &data, G_MSGTYPE_ALL);
}

void WorldSession::HandleGuildLeave(WorldPacket & recv_data)
{
	Player *plyr = GetPlayer();

	if(!plyr)
		return;

	Guild *pGuild = objmgr.GetGuild( plyr->GetGuildId() );

	if(!pGuild)
		return;

	if( pGuild->GetGuildLeaderGuid() == plyr->GetGUID() )
		return;

	plyr->SetGuildId(0);
	plyr->SetGuildRank(0);
	pGuild->DeleteGuildMember(plyr->GetGUID());

	WorldPacket data(100);
	data.Initialize(SMSG_GUILD_EVENT);
	data << uint8(GUILD_EVENT_LEFT);
	data << uint8(1);
	data << plyr->GetName();
	pGuild->SendMessageToGuild(0, &data, G_MSGTYPE_ALL);
}

void WorldSession::HandleGuildRemove(WorldPacket & recv_data)
{
	CHECK_PACKET_SIZE(recv_data, 1);
	WorldPacket data(100);
	std::string name;
	bool result = false;

	recv_data >> name;
	Player *plyr = objmgr.GetPlayer( name.c_str() );
	Player *pRemover = GetPlayer();

	if(!pRemover)
		return;

	Guild *pGuild = objmgr.GetGuild( pRemover->GetGuildId() );

	if(!pGuild)
		return;

	uint32 RemoverRank = pRemover->GetGuildRank();

	if(!pGuild->HasRankRight(RemoverRank,GR_RIGHT_REMOVE))
	{
		//Players not allowed to remove a guild member
		 SendGuildCommandResult(3,"",GUILD_PERMISSIONS);
		return;
	}

	if(plyr)
	{
		plyr->SetGuildId(0);
		plyr->SetGuildRank(0);
		result = pGuild->DeleteGuildMember(plyr->GetGUID());
	}
	else
	{
		result = pGuild->DeleteGuildMember(name);
	}

	if(result)
	{
		data.Initialize(SMSG_GUILD_EVENT);
		data << uint8(GUILD_EVENT_REMOVED);
		data << uint8(2);
		data << name.c_str();
		data << pRemover->GetName();
		pGuild->SendMessageToGuild(0, &data, G_MSGTYPE_ALL);
	}
}

void WorldSession::HandleGuildDisband(WorldPacket & recv_data)
{
	Player *pLeader = GetPlayer();

	if(!pLeader)
		return;

	Guild *pGuild = objmgr.GetGuild( pLeader->GetGuildId() );

	if(!pGuild)
		return;

	if(pLeader->GetGUID() != pGuild->GetGuildLeaderGuid())
		return;

	pGuild->DeleteGuildMembers();
	pGuild->RemoveFromDb();
}

void WorldSession::HandleGuildLeader(WorldPacket & recv_data)
{
	CHECK_PACKET_SIZE(recv_data, 1);
	std::string name;
	recv_data >> name;

	Player *pLeader = GetPlayer();

	if(!pLeader)
		return;

	Guild *pGuild = objmgr.GetGuild( pLeader->GetGuildId() );

	if(!pGuild)
		return;

	PlayerInfo *pGuildMember = pGuild->GetGuildMember(name);
	if(!pGuildMember)
		return;

	if(pLeader->GetLowGUID() == pGuildMember->guid)
		return;

	if(pLeader->GetGUID() != pGuild->GetGuildLeaderGuid())
		return;

	PlayerInfo *pGuildLeader = pGuild->GetGuildMember(pLeader->GetGUID());
	if(!pGuildLeader)
		return;

	pGuildLeader->Rank = GUILDRANK_OFFICER;
	pLeader->SetGuildRank(GUILDRANK_OFFICER);  

	pGuildMember->Rank = GUILDRANK_GUILD_MASTER;
	Player *pNewLeader = objmgr.GetPlayer( name.c_str() );
	if(pNewLeader)
	{
  //	  pNewLeader->SetUInt32Value(PLAYER_GUILDRANK,GUILDRANK_GUILD_MASTER);
		pNewLeader->SetGuildRank(GUILDRANK_GUILD_MASTER);
	   // pNewLeader->SaveGuild();
	}
	else
	{
		pGuild->UpdateGuildMembersDB(pGuildMember);
	}

	pGuild->SetGuildLeaderGuid( pGuildMember->guid );
	pGuild->UpdateGuildToDb();

	WorldPacket data;
	data.Initialize(SMSG_GUILD_EVENT);
	data << (uint8)GUILD_EVENT_LEADER_CHANGED;
	data << (uint8)2;
	data << pLeader->GetName();
	data << pGuildMember->name;
	pGuild->SendMessageToGuild(0, &data, G_MSGTYPE_ALL);

	/*
	//demote old guildleader
	data.Initialize(SMSG_GUILD_EVENT);
	data << uint8(GUILD_EVENT_DEMOTION);
	data << uint8(3);
	data << pLeader->GetName();
	data << pLeader->GetName();
	data << pGuild->GetRankName( GUILDRANK_OFFICER );
	pGuild->SendMessageToGuild(0, &data, G_MSGTYPE_ALL);

	//promote new guildmaster
	data.Initialize(SMSG_GUILD_EVENT);
	data << uint8(GUILD_EVENT_PROMOTION);
	data << uint8(3);
	data << pLeader->GetName();
	data << pNewLeader->GetName();
	data << pGuild->GetRankName( GUILDRANK_GUILD_MASTER );
	pGuild->SendMessageToGuild(0, &data, G_MSGTYPE_ALL);
	*/
	//data.clear();
	//pGuild->FillGuildRosterData(&data);
	//pGuild->SendMessageToGuild(0, &data, G_MSGTYPE_ALL);
}

void WorldSession::HandleGuildMotd(WorldPacket & recv_data)
{
	WorldPacket data;
	std::string motd;
	if(recv_data.size())
		recv_data >> motd;

	Guild *pGuild = objmgr.GetGuild( GetPlayer()->GetGuildId() );

	if(!pGuild)
		return;

	pGuild->SetGuildMotd(motd);

	data.SetOpcode(SMSG_GUILD_EVENT);
	data << uint8(GUILD_EVENT_MOTD);
	data << uint8(0x01);
	data << pGuild->GetGuildMotd();
	pGuild->SendMessageToGuild(0, &data, G_MSGTYPE_ALL);

	pGuild->UpdateGuildToDb();
}

void WorldSession::HandleGuildRank(WorldPacket & recv_data)
{
	CHECK_PACKET_SIZE(recv_data, 9);
	WorldPacket data;
	Guild *pGuild;
	std::string rankname;
	uint32 rankId;
	uint32 rights;

	pGuild = objmgr.GetGuild(GetPlayer()->GetGuildId());
	if(!pGuild)
	{
		SendGuildCommandResult(GUILD_CREATE_S,"",GUILD_PLAYER_NOT_IN_GUILD);
		return;
	}

	if(GetPlayer()->GetGUID() != pGuild->GetGuildLeaderGuid())
	{
		SendGuildCommandResult(GUILD_INVITE_S,"",GUILD_PERMISSIONS);
		return;
	}

	recv_data >> rankId;
	recv_data >> rights;
	recv_data >> rankname;

	sLog.outDebug( "WORLD: Changed RankName to %s , Rights to 0x%.4X",rankname.c_str() ,rights );

	pGuild->SetRankName(rankId,rankname);
	pGuild->SetRankRights(rankId,rights);

	pGuild->FillQueryData(&data);
	pGuild->SendMessageToGuild(0, &data, G_MSGTYPE_ALL);

	data.clear();
	pGuild->FillGuildRosterData(&data);
	pGuild->SendMessageToGuild(0, &data, G_MSGTYPE_ALL);

	pGuild->SaveRanksToDb();
}

void WorldSession::HandleGuildAddRank(WorldPacket & recv_data)
{
	CHECK_PACKET_SIZE(recv_data, 1);
	WorldPacket data;
	Guild *pGuild;
	std::string rankname;

	pGuild = objmgr.GetGuild(GetPlayer()->GetGuildId());
	if(!pGuild)
	{
		//not in Guild
		SendGuildCommandResult(GUILD_CREATE_S,"",GUILD_PLAYER_NOT_IN_GUILD);
		return;
	}

	if(GetPlayer()->GetGUID() != pGuild->GetGuildLeaderGuid())
	{
		//not GuildMaster
		SendGuildCommandResult(GUILD_INVITE_S,"",GUILD_PERMISSIONS);
		return;
	}

	recv_data >> rankname;

	pGuild->CreateRank(rankname,GR_RIGHT_GCHATLISTEN | GR_RIGHT_GCHATSPEAK);

	pGuild->FillQueryData(&data);
	pGuild->SendMessageToGuild(0, &data, G_MSGTYPE_ALL);

	data.clear();
	//Maybe theres an event for this
	pGuild->FillGuildRosterData(&data);
	pGuild->SendMessageToGuild(0, &data, G_MSGTYPE_ALL);

	pGuild->SaveRanksToDb();
}

void WorldSession::HandleGuildDelRank(WorldPacket & recv_data)
{
	WorldPacket data;
	Guild *pGuild;

	pGuild = objmgr.GetGuild(GetPlayer()->GetGuildId());	
	if(!pGuild)
	{
		SendGuildCommandResult(GUILD_CREATE_S,"",GUILD_PLAYER_NOT_IN_GUILD);
		return;
	}

	if(GetPlayer()->GetGUID() != pGuild->GetGuildLeaderGuid())
	{
		SendGuildCommandResult(GUILD_INVITE_S,"",GUILD_PERMISSIONS);
		return;
	}

	pGuild->DelRank();

	pGuild->FillQueryData(&data);
	pGuild->SendMessageToGuild(0, &data, G_MSGTYPE_ALL);
	data.clear();

	//Maybe theres an event for this
	pGuild->FillGuildRosterData(&data);
	pGuild->SendMessageToGuild(0, &data, G_MSGTYPE_ALL);

	pGuild->SaveRanksToDb();
}

void WorldSession::HandleGuildSetPublicNote(WorldPacket & recv_data)
{
	CHECK_PACKET_SIZE(recv_data, 2);
	WorldPacket data;
	std::string TargetsName;
	std::string publicNote;

	recv_data >> TargetsName;
	recv_data >> publicNote;

	Player *pPlayer = GetPlayer();

	if(!pPlayer)
		return;

	Guild *pGuild = objmgr.GetGuild( pPlayer->GetGuildId() );

	if(!pGuild)
		return;

	PlayerInfo *pGuildMember = pGuild->GetGuildMember(TargetsName);

	if(!pGuildMember)
	{
		SendGuildCommandResult(GUILD_FOUNDER_S,TargetsName.c_str(),GUILD_PLAYER_NOT_IN_GUILD_S);
		return;
	}

	uint32 plyrRank = pPlayer->GetGuildRank();

	if(!pGuild->HasRankRight(plyrRank,GR_RIGHT_EPNOTE))
	{
		SendGuildCommandResult(3,"",GUILD_PERMISSIONS);
		return;
	}

	pGuild->SetPublicNote(pGuildMember->guid, publicNote);

	//Save new Info to DB
	pGuild->UpdateGuildMembersDB(pGuildMember);

	//Send Update (this seems to be how blizz does it couldn't find an event for it)
	pGuild->FillGuildRosterData(&data);
	pGuild->SendMessageToGuild(0, &data, G_MSGTYPE_ALL);
}

void WorldSession::HandleGuildSetOfficerNote(WorldPacket & recv_data)
{
	CHECK_PACKET_SIZE(recv_data, 2);
	WorldPacket data;
	std::string TargetsName;
	std::string officerNote;

	recv_data >> TargetsName;
	recv_data >> officerNote;

	Player *pPlayer = GetPlayer();


	if(!pPlayer)
		return;

	Guild *pGuild = objmgr.GetGuild( pPlayer->GetGuildId() );

	if(!pGuild)
		return;

	PlayerInfo *pGuildMember = pGuild->GetGuildMember(TargetsName);

	if(!pGuildMember)
	{
		SendGuildCommandResult(GUILD_FOUNDER_S, TargetsName.c_str(),GUILD_PLAYER_NOT_IN_GUILD_S);
		return;
	}

	uint32 plyrRank = pPlayer->GetGuildRank();

	if(!pGuild->HasRankRight(plyrRank,GR_RIGHT_EOFFNOTE))
	{
		SendGuildCommandResult(3,"",GUILD_PERMISSIONS);
		return;
	}

	pGuild->SetOfficerNote(pGuildMember->guid, officerNote);

	//Save new Info to DB
	pGuild->UpdateGuildMembersDB(pGuildMember);

	//Send Update (this seems to be how blizz does it couldn't find an event for it)
	pGuild->FillGuildRosterData(&data);
	pGuild->SendMessageToGuild(0, &data, G_MSGTYPE_ALL);
}

void WorldSession::HandleSaveGuildEmblem(WorldPacket & recv_data)
{
	CHECK_PACKET_SIZE(recv_data, 20);
	WorldPacket data;

	Guild *pGuild = objmgr.GetGuild( GetPlayer()->GetGuildId() );

	if(!pGuild)
	{
		data.Initialize(MSG_SAVE_GUILD_EMBLEM);
		data <<	uint32(ERR_GUILDEMBLEM_NOGUILD);
		SendPacket(&data);
		return;
	}

	Player *plyr = GetPlayer();

	if(!plyr)
		return;

	if(plyr->GetGuildRank() != GUILDRANK_GUILD_MASTER)
	{
		data.Initialize(MSG_SAVE_GUILD_EMBLEM);
		data <<	uint32(ERR_GUILDEMBLEM_NOTGUILDMASTER);
		SendPacket(&data);
		return;
	}
	uint32 Money = plyr->GetUInt32Value(PLAYER_FIELD_COINAGE);
	uint32 Cost = 100000; //10 Gold
	if(Money < Cost)
	{
		data.Initialize(MSG_SAVE_GUILD_EMBLEM);
		data << uint32(ERR_GUILDEMBLEM_NOTENOUGHMONEY);
		SendPacket(&data);
		return;
	}
	plyr->SetUInt32Value(PLAYER_FIELD_COINAGE,(Money-Cost));

	uint64 DesignerGuid;
	recv_data >> DesignerGuid;

	uint32 emblemPart;
	recv_data >> emblemPart;
	pGuild->SetGuildEmblemStyle( emblemPart );
	recv_data >> emblemPart;
	pGuild->SetGuildEmblemColor( emblemPart );
	recv_data >> emblemPart;
	pGuild->SetGuildBorderStyle( emblemPart );
	recv_data >> emblemPart;
	pGuild->SetGuildBorderColor( emblemPart );
	recv_data >> emblemPart;
	pGuild->SetGuildBackgroundColor( emblemPart );

	data.Initialize(MSG_SAVE_GUILD_EMBLEM);
	data <<	uint32(ERR_GUILDEMBLEM_SUCCESS);
	SendPacket(&data);

	data.clear();
	pGuild->FillQueryData(&data);
	pGuild->SendMessageToGuild(plyr->GetGUID(), &data, G_MSGTYPE_ALL);

	pGuild->UpdateGuildToDb();
}

// Charter part
void WorldSession::HandleCharterBuy(WorldPacket & recv_data)
{
	uint64 creature_guid;
	uint32 unk2;
	string name;

	recv_data >> creature_guid;                        // NPC GUID
	recv_data.read<uint32>();                          // 0
	recv_data.read<uint64>();                          // 0
	recv_data >> name;                                 // name
	recv_data.read<uint32>();                          // 0
	recv_data.read<uint32>();                          // 0
	recv_data.read<uint32>();                          // 0
	recv_data.read<uint32>();                          // 0
	recv_data.read<uint32>();                          // 0
	recv_data.read<uint32>();                          // 0
	recv_data.read<uint32>();                          // 0
	recv_data.read<uint32>();                          // 0
	recv_data.read<uint32>();                          // 0
	recv_data.read<uint32>();                          // 0
	recv_data.read<uint16>();                          // 0
	recv_data.read<uint8>();                           // 0

	recv_data >> unk2;                                 // index
	recv_data.read<uint32>();                          // 0

	Creature * crt = _player->GetMapMgr()->GetCreature((uint32)creature_guid);
	if(!crt)
	{
		Disconnect();
		return;
	}

	if (crt->GetEntry() == 19861 || crt->GetEntry() == 18897 || crt->GetEntry() == 19856)		/* i am lazy! */
	{
		{
			Guild * g = objmgr.GetGuildByGuildName(name);
			Charter * c = objmgr.GetCharterByName(name, CHARTER_TYPE_GUILD);
			if (g != 0 || c != 0)
			{
				SendNotification("A guild with that name already exists.");
				return;
			}

			if (_player->m_charters[CHARTER_TYPE_GUILD])
			{
				SendNotification("You already have a guild charter.");
				return;
			}

			ItemPrototype * ip = ItemPrototypeStorage.LookupEntry(ITEM_ENTRY_GUILD_CHARTER);
			assert(ip);
			SlotResult res = _player->GetItemInterface()->FindFreeInventorySlot(ip);
			if (res.Result == 0)
			{
				_player->GetItemInterface()->BuildInventoryChangeError(0, 0, INV_ERR_INVENTORY_FULL);
				return;
			}

			uint32 error = _player->GetItemInterface()->CanReceiveItem(ItemPrototypeStorage.LookupEntry(ITEM_ENTRY_GUILD_CHARTER), 1);
			if (error)
			{
				_player->GetItemInterface()->BuildInventoryChangeError(nullptr, nullptr, error);
			}
			else
			{
				// Meh...
				WorldPacket data(SMSG_PLAY_OBJECT_SOUND, 12);
				data << uint32(0x000019C2);
				data << creature_guid;
				SendPacket(&data);

				// Create the item and charter
				Item * i = objmgr.CreateItem(ITEM_ENTRY_GUILD_CHARTER, _player);
				c = objmgr.CreateCharter(_player->GetLowGUID(), CHARTER_TYPE_GUILD);
				c->GuildName = name;
				c->ItemGuid = i->GetGUID();


				i->SetUInt32Value(ITEM_FIELD_STACK_COUNT, 1);
				i->SetUInt32Value(ITEM_FIELD_FLAGS, 1);
				i->SetUInt32Value(ITEM_FIELD_ENCHANTMENT, c->GetID());
				i->SetUInt32Value(ITEM_FIELD_PROPERTY_SEED, 57813883);
				_player->GetItemInterface()->AddItemToFreeSlot(i);
				c->SaveToDB();

				/*data.clear();
				data.resize(45);
				BuildItemPushResult(&data, _player->GetGUID(), ITEM_PUSH_TYPE_RECEIVE, 1, ITEM_ENTRY_GUILD_CHARTER, 0);
				SendPacket(&data);*/
				SendItemPushResult(i, false, true, false, true, _player->GetItemInterface()->LastSearchItemBagSlot(), _player->GetItemInterface()->LastSearchItemSlot(), 1);

				_player->m_charters[CHARTER_TYPE_GUILD] = c;
				_player->SaveToDB(false);
			}
		}
	}
}

void SendShowSignatures(Charter * c, uint64 i, Player * p)
{
	WorldPacket data(SMSG_PETITION_SHOW_SIGNATURES, 100);
	data << i;
	data << (uint64)c->GetLeader();
	data << c->GetID();
	data << uint8(c->SignatureCount);
	for(uint32 i = 0; i < c->Slots; ++i)
	{
		if(c->Signatures[i] == 0) continue;
		data << uint64(c->Signatures[i]) << uint32(1);
	}
	data << uint8(0);
	p->GetSession()->SendPacket(&data);
}

void WorldSession::HandleCharterShowSignatures(WorldPacket & recv_data)
{
	Charter * pCharter;
	uint64 item_guid;
	recv_data >> item_guid;
	pCharter = objmgr.GetCharterByItemGuid(item_guid);
	
	if(pCharter)
		SendShowSignatures(pCharter, item_guid, _player);
}

void WorldSession::HandleCharterQuery(WorldPacket & recv_data)
{
	/*
	{SERVER} Packet: (0x01C7) SMSG_PETITION_QUERY_RESPONSE PacketSize = 77
	|------------------------------------------------|----------------|
	|00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F |0123456789ABCDEF|
	|------------------------------------------------|----------------|
	|20 08 00 00 28 32 01 00 00 00 00 00 53 74 6F 72 | ...(2......Stor|
	|6D 62 72 69 6E 67 65 72 73 00 00 09 00 00 00 09 |mbringers.......|
	|00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 |................|
	|00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 |................|
	|00 00 00 00 00 00 00 00 00 00 00 00 00		  |.............   |
	-------------------------------------------------------------------
	
	uint32 charter_id
	uint64 leader_guid
	string guild_name
	uint8  0			<-- maybe subname? or some shit.. who knows
	uint32 9
	uint32 9
	uint32[9] signatures
	uint8  0
	uint8  0
	*/
//this is wrong there are 42 bytes after 9 9, 4*9+2=38 not 42,
	//moreover it can't signature, blizz uses always fullguid so it must be uin64
	//moreover this does not show ppl who signed this, for this purpose there is another opcde
	uint32 charter_id;
	uint64 item_guid;
	recv_data >> charter_id;
	recv_data >> item_guid;
	/*Charter * c = objmgr.GetCharter(charter_id,CHARTER_TYPE_GUILD);
	if(c == 0)
		c = objmgr.GetCharter(charter_id, CHARTER_TYPE_ARENA_2V2);
	if(c == 0)
		c = objmgr.GetCharter(charter_id, CHARTER_TYPE_ARENA_3V3);
	if(c == 0)
		c = objmgr.GetCharter(charter_id, CHARTER_TYPE_ARENA_5V5);*/

	Charter * c = objmgr.GetCharterByItemGuid(item_guid);
	if(c == 0)
		return;

	WorldPacket data(SMSG_PETITION_QUERY_RESPONSE, 100);
	data << charter_id;
	data << (uint64)c->LeaderGuid;
	data << c->GuildName << uint8(0);
	if(c->CharterType == CHARTER_TYPE_GUILD)
	{
		data << uint32(9) << uint32(9);
	}
	else
	{
		/*uint32 v = c->CharterType;
		if(c->CharterType == CHARTER_TYPE_ARENA_3V3)
			v=2;
		else if(c->CharterType == CHARTER_TYPE_ARENA_5V5)
			v=4;

		data << v << v;*/
		data << uint32(c->Slots) << uint32(c->Slots);
	}

	data << uint32(0);                                      // 4
    data << uint32(0);                                      // 5
    data << uint32(0);                                      // 6
    data << uint32(0);                                      // 7
    data << uint32(0);                                      // 8
    data << uint16(0);                                      // 9 2 bytes field
    data << uint32(0x46);                                      // 10
    data << uint32(0);                                      // 11
    data << uint32(0);                                      // 13 count of next strings?
    data << uint32(0);                                      // 14

	if (c->CharterType == CHARTER_TYPE_GUILD)
	{
		data << uint32(0);
	}
	else
	{
		data << uint32(1);
	}

	SendPacket(&data);
}

void WorldSession::HandleCharterOffer( WorldPacket & recv_data )
{
	uint32 shit;
	uint64 item_guid, target_guid;
	Charter * pCharter;
	recv_data >> shit >> item_guid >> target_guid;
	
	if(!_player->IsInWorld()) return;
	Player * pTarget = _player->GetMapMgr()->GetPlayer((uint32)target_guid);
	pCharter = objmgr.GetCharterByItemGuid(item_guid);

	if(pTarget == 0 || pTarget->GetTeam() != _player->GetTeam() || pTarget == _player)
	{
		SendNotification("Target is of the wrong faction.");
		return;
	}

	if(!pTarget->CanSignCharter(pCharter, _player))
	{
		SendNotification("Target player cannot sign your charter for one or more reasons.");
		return;
	}

	SendShowSignatures(pCharter, item_guid, pTarget);
}

void WorldSession::HandleCharterSign( WorldPacket & recv_data )
{
	uint64 item_guid;
	recv_data >> item_guid;

	Charter * c = objmgr.GetCharterByItemGuid(item_guid);
	if(c == 0)
		return;

	for(uint32 i = 0; i < 9; ++i)
	{
		if(c->Signatures[i] == _player->GetGUID())
		{
			SendNotification("You have already signed that charter.");
			return;
		}
	}

	if(c->IsFull())
		return;

	c->AddSignature(_player->GetLowGUID());
	c->SaveToDB();
	_player->m_charters[c->CharterType] = c;
	_player->SaveToDB(false);

	Player * l = _player->GetMapMgr()->GetPlayer(c->GetLeader());
	if(l == 0)
		return;

	WorldPacket data(SMSG_PETITION_SIGN_RESULTS, 100);
	data << item_guid << _player->GetGUID() << uint32(0);
	l->GetSession()->SendPacket(&data);
	data.clear();
	data << item_guid << (uint64)c->GetLeader() << uint32(0);
	SendPacket(&data);
}

void WorldSession::HandleCharterTurnInCharter(WorldPacket & recv_data)
{
	uint64 mooguid;
	recv_data >> mooguid;
	Charter * pCharter = objmgr.GetCharterByItemGuid(mooguid);
	if(!pCharter) return;

	if(pCharter->CharterType == CHARTER_TYPE_GUILD)
	{
		Charter * gc = _player->m_charters[CHARTER_TYPE_GUILD];
		if(gc == 0)
			return;
		if(gc->SignatureCount < 9 && Config.MainConfig.GetBoolDefault("Server", "RequireAllSignatures", false))
		{
			SendNotification("You don't have the required amount of signatures to turn in this petition.");
			return;
		}

		// dont know hacky or not but only solution for now
		// If everything is fine create guild

		Guild *pGuild = new Guild;
		uint32 guildId = pGuild->GetFreeGuildIdFromDb();

		if(guildId == 0)
		{
			printf("Error Getting Free Guild ID");
			delete pGuild;
			return;
		}

		//Guild Setup
		pGuild->SetGuildId( guildId );
		pGuild->SetGuildName( gc->GuildName );
		pGuild->CreateRank("Guild Master", GR_RIGHT_ALL);
		pGuild->CreateRank("Officer", GR_RIGHT_ALL);
		pGuild->CreateRank("Veteran", GR_RIGHT_GCHATLISTEN | GR_RIGHT_GCHATSPEAK);  
		pGuild->CreateRank("Member", GR_RIGHT_GCHATLISTEN | GR_RIGHT_GCHATSPEAK);
		pGuild->CreateRank("Initiate", GR_RIGHT_GCHATLISTEN | GR_RIGHT_GCHATSPEAK);
		pGuild->SetGuildEmblemStyle( 0xFFFF );
		pGuild->SetGuildEmblemColor( 0xFFFF );
		pGuild->SetGuildBorderStyle( 0xFFFF );
		pGuild->SetGuildBorderColor( 0xFFFF );
		pGuild->SetGuildBackgroundColor( 0xFFFF );

		objmgr.AddGuild(pGuild);

		//Guild Leader Setup
		_player->SetGuildId( pGuild->GetGuildId() );
		_player->SetGuildRank(GUILDRANK_GUILD_MASTER);
		pGuild->SetGuildLeaderGuid( _player->GetGUID() );
		pGuild->AddNewGuildMember( _player );

		//Other Guild Members Setup
		Player *pMember;  

		for(uint32 x=0;x<9;x++)
		{	   
			pMember = objmgr.GetPlayer(gc->Signatures[x]);
			if(pMember)//online
			{
				pMember->SetGuildId( pGuild->GetGuildId() );
				pMember->SetGuildRank(GUILDRANK_MEMBER);		
				//Charters
				pMember->m_charters[gc->CharterType] = 0;
				pMember->SaveToDB(false);
			}
			else
			{
				CharacterDatabase.Execute("UPDATE characters SET guildid = %u WHERE guid = %u", pGuild->GetGuildId(), gc->Signatures[x]);
				CharacterDatabase.Execute("UPDATE characters SET guildRank = %u WHERE guid = %u", GUILDRANK_MEMBER, gc->Signatures[x]);
			}

			PlayerInfo *p=objmgr.GetPlayerInfo(gc->Signatures[x]);
			if(!p)continue;//this should not ever happen though
			p->Rank = GUILDRANK_MEMBER;
			pGuild->AddGuildMember(p);			
		}	

		pGuild->SaveToDb();
		pGuild->SaveAllGuildMembersToDb();
		pGuild->SaveRanksToDb();

		// Destroy the charter
		_player->m_charters[CHARTER_TYPE_GUILD] = 0;
		gc->Destroy();

		_player->GetItemInterface()->RemoveItemAmt(ITEM_ENTRY_GUILD_CHARTER, 1);
		sHookInterface.OnGuildCreate(_player, pGuild);
	}
	WorldPacket data(4);
	data.SetOpcode(SMSG_TURN_IN_PETITION_RESULTS);
	data << uint32(0);
	SendPacket( &data );
}

void WorldSession::HandleCharterRename(WorldPacket & recv_data)
{
	uint64 guid;
	string name;
	recv_data >> guid >> name;

	Charter * pCharter = objmgr.GetCharterByItemGuid(guid);
	if(pCharter == 0)
		return;

	Guild * g = objmgr.GetGuildByGuildName(name);
	Charter * c = objmgr.GetCharterByName(name, (CharterTypes)pCharter->CharterType);
	if(c || g)
	{
		SendNotification("That name is in use by another guild.");
		return;
	}

	c = pCharter;
	c->GuildName = name;
	c->SaveToDB();
	WorldPacket data(MSG_PETITION_RENAME, 100);
	data << guid << name;
	SendPacket(&data);
}
