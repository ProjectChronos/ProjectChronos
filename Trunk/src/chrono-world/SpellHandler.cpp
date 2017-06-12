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

#include "StdAfx.h"

void WorldSession::HandleUseItemOpcode(WorldPacket& recvPacket)
{
	CHECK_INWORLD_RETURN
	typedef std::list<Aura*> AuraList;
	
	Player* p_User = GetPlayer();
	sLog.outDetail("WORLD: got use Item packet, data length = %i",recvPacket.size());
	int8 tmp1,slot,tmp3;
	//uint64 item_guid;
	//uint8 cn;
	uint32 spellId = 0;
	bool HasTarget = false;

	recvPacket >> tmp1 >> slot >> tmp3; //>> cn >> item_guid;
	Item* tmpItem = nullptr;
	tmpItem = p_User->GetItemInterface()->GetInventoryItem(tmp1,slot);
	if (!tmpItem)
		tmpItem = p_User->GetItemInterface()->GetInventoryItem(slot);
	if (!tmpItem)
		return;
	ItemPrototype *itemProto = tmpItem->GetProto();
	if(!itemProto)
		return;

	if(_player->getDeathState()==CORPSE)
		return;

	if(itemProto->Bonding == ITEM_BIND_ON_USE)
		tmpItem->SoulBind();

	if(sScriptMgr.CallScriptedItem(tmpItem,_player))
		return;

	if(itemProto->QuestId)
	{
		// Item Starter
		Quest *qst = QuestStorage.LookupEntry(itemProto->QuestId);
		if(!qst) 
			return;

        WorldPacket data;
        sQuestMgr.BuildQuestDetails(&data, qst, tmpItem, 0, language, _player);
		SendPacket(&data);
	}
	
	SpellCastTargets targets(recvPacket, _player->GetGUID());
	uint32 x;
	for(x = 0; x < 5; x++)
	{
		if(itemProto->Spells[x].Trigger == USE)
		{
			if(itemProto->Spells[x].Id)
			{
				spellId = itemProto->Spells[x].Id;
				break;
			}
		}
	}
	if(!spellId)
		return;
	// check for spell id
	SpellEntry *spellInfo = dbcSpell.LookupEntryForced( spellId );

	if(!spellInfo)
	{
		sLog.outError("WORLD: unknown spell id %i\n", spellId);
		return;
	}

	if (spellInfo->AuraInterruptFlags & AURA_INTERRUPT_ON_STAND_UP)
	{
		if (p_User->CombatStatus.IsInCombat() || p_User->IsMounted())
		{
			_player->GetItemInterface()->BuildInventoryChangeError(tmpItem,nullptr,INV_ERR_CANT_DO_IN_COMBAT);
			return;
		}
		if(p_User->GetStandState()!=1)
		p_User->SetStandState(STANDSTATE_SIT);
		// loop through the auras and removing existing eating spells
	}

	if(itemProto->RequiredLevel)
	{
		if(_player->getLevel() < itemProto->RequiredLevel)
		{
			_player->GetItemInterface()->BuildInventoryChangeError(tmpItem,nullptr,INV_ERR_ITEM_RANK_NOT_ENOUGH);
			return;
		}
	}

	if(itemProto->RequiredSkill)
	{
		if(!_player->_HasSkillLine(itemProto->RequiredSkill))
		{
			_player->GetItemInterface()->BuildInventoryChangeError(tmpItem,nullptr,INV_ERR_ITEM_RANK_NOT_ENOUGH);
			return;
		}

		if(itemProto->RequiredSkillRank)
		{
			if(_player->_GetSkillLineCurrent(itemProto->RequiredSkill, false) < itemProto->RequiredSkillRank)
			{
				_player->GetItemInterface()->BuildInventoryChangeError(tmpItem,nullptr,INV_ERR_ITEM_RANK_NOT_ENOUGH);
				return;
			}
		}
	}
	
	if( itemProto->AllowableClass && !(_player->getClassMask() & itemProto->AllowableClass) || itemProto->AllowableRace && !(_player->getRaceMask() & itemProto->AllowableRace) )
	{
		_player->GetItemInterface()->BuildInventoryChangeError(tmpItem,nullptr,INV_ERR_YOU_CAN_NEVER_USE_THAT_ITEM);
		return;
	}		

	if( !_player->Cooldown_CanCast( itemProto, x ) )
	{
		_player->SendCastResult(spellInfo->Id, SPELL_FAILED_NOT_READY, 0, 0);
		return;
	}

	if(_player->m_currentSpell)
	{
		_player->SendCastResult(spellInfo->Id, SPELL_FAILED_SPELL_IN_PROGRESS, 0, 0);
		return;
	}

	if( itemProto->ForcedPetId >= 0 )
	{
		if( itemProto->ForcedPetId == 0 )
		{
			if( _player->GetGUID() != targets.m_unitTarget )
			{
				_player->SendCastResult(spellInfo->Id, SPELL_FAILED_BAD_TARGETS, 0, 0);
				return;
			}
		}
		else
		{
			if( !_player->GetSummon() || _player->GetSummon()->GetEntry() != (uint32)itemProto->ForcedPetId )
			{
				_player->SendCastResult(spellInfo->Id, SPELL_FAILED_SPELL_IN_PROGRESS, 0, 0);
				return;
			}
		}
	}
	
	for(x=0; x < 3; x++)
	{
		if(spellInfo->EffectImplicitTargetA[x] == 6 || spellInfo->EffectImplicitTargetB[x] == 6)
			HasTarget = true;
	}
	if(HasTarget)
		targets.m_unitTarget = _player->GetSelection();

	Spell *spell = new Spell(_player, spellInfo, false, nullptr);
	uint8 result;
	spell->extra_cast_number=0;
	spell->i_caster = tmpItem;
	//GetPlayer()->setCurrentSpell(spell);
	result = spell->prepare(&targets);
	
	//if( result == SPELL_CANCAST_OK ) // incorrect - should be on Completed Cast
	//	_player->Cooldown_AddItem( itemProto, x );
}

void WorldSession::HandleCastSpellOpcode(WorldPacket& recvPacket)
{
	CHECK_INWORLD_RETURN
	if(_player->getDeathState()==CORPSE)
		return;

	uint32 spellId;

	recvPacket >> spellId;
	// check for spell id
	SpellEntry *spellInfo = dbcSpell.LookupEntryForced(spellId );
	/*
	if(!spellInfo)
	{
		sLog.outError("WORLD: unknown spell id %i\n", spellId);
		return;
	}

	sLog.outDetail("WORLD: got cast spell packet, spellId - %i (%s), data length = %i",
		spellId, spellInfo->Name, recvPacket.size());
	*/
	// Cheat Detection only if player and not from an item
	// this could fuck up things but meh it's needed ALOT of the newbs are using WPE now
	// WPE allows them to mod the outgoing packet and basicly choose what ever spell they want :(
	if( !GetPlayer()->HasSpell(spellId) || spellInfo->Attributes & ATTRIBUTES_PASSIVE )
	{
		sLog.outDetail("WORLD: Spell isn't casted because player \"%s\" is cheating", GetPlayer()->GetName());
		return;
	}
	if (GetPlayer()->GetOnMeleeSpell() != spellId)
	{
		//autoshot 75
		if((spellInfo->Flags4 & FLAGS4_PLAYER_RANGED_SPELLS) /*spellInfo->Attributes == 327698*/)	// auto shot..
		{
			//sLog.outString( "HandleSpellCast: Auto Shot-type spell cast (id %u, name %s)" , spellInfo->Id , spellInfo->Name );
			Item *weapon = GetPlayer()->GetItemInterface()->GetInventoryItem(EQUIPMENT_SLOT_RANGED);
			if(!weapon) 
				return;
			uint32 spellid;
			switch(weapon->GetProto()->SubClass)
			{
			/*case 2:			 // bows
			case 3:			 // guns
            case 18:		 // crossbow
				spellid = SPELL_RANGED_GENERAL;*/
				break;
			case 16:			// thrown
				spellid = SPELL_RANGED_THROW;
				break;
			case 19:			// wands
				spellid = SPELL_RANGED_WAND;
				break;
			default:
				spellid = 0;
				break;
			}
	   
			if(!spellid) 
				spellid = spellInfo->Id;
			
			if(!_player->m_onAutoShot)
			{
				_player->m_AutoShotTarget = _player->GetSelection();
				uint32 duration = _player->GetUInt32Value(UNIT_FIELD_RANGEDATTACKTIME);
				SpellCastTargets targets(recvPacket,GetPlayer()->GetGUID());
				if(!targets.m_unitTarget)
				{
					sLog.outString( "Cancelling auto-shot cast because targets.m_unitTarget is nullptr!" );
					return;
				}
				SpellEntry *sp = dbcSpell.LookupEntry(spellid);
			
				_player->m_AutoShotSpell = sp;
				_player->m_AutoShotDuration = duration;
				//This will fix fast clicks
				if(_player->m_AutoShotAttackTimer < 500)
					_player->m_AutoShotAttackTimer = 500;
				_player->m_onAutoShot = true;
			}

			return;
		}
		/*const char * name = sSpellStore.LookupString(spellInfo->Name);
		if(name)
			sChatHandler.SystemMessageToPlr(_player, "%sSpell Cast:%s %s %s[Group %u, family %u]", MSG_COLOR_LIGHTBLUE,
			MSG_COLOR_SUBWHITE, name, MSG_COLOR_YELLOW, spellInfo->SpellGroupType, spellInfo->SpellFamilyName);*/
        if(_player->m_currentSpell)
        {
			if( _player->m_currentSpell->getState() == SPELL_STATE_CASTING )
			{
				// cancel the existing channel spell, cast this one
				_player->m_currentSpell->cancel();
			}
			else
			{
				// send the error message
				_player->SendCastResult(spellInfo->Id, SPELL_FAILED_SPELL_IN_PROGRESS, 0, 0);
				return;
			}
        }
		SpellCastTargets targets(recvPacket,GetPlayer()->GetGUID());

		// some anticheat stuff
		if( spellInfo->self_cast_only )
		{
			if( targets.m_unitTarget && targets.m_unitTarget != _player->GetGUID() )
			{
				// send the error message
				_player->SendCastResult(spellInfo->Id, SPELL_FAILED_BAD_TARGETS, 0, 0);
				return;
			}
		}
		Spell *spell = new Spell(GetPlayer(), spellInfo, false, nullptr);
		spell->prepare(&targets);
	}
}

void WorldSession::HandleCancelCastOpcode(WorldPacket& recvPacket)
{
	uint32 spellId;
	recvPacket >> spellId;

	if(GetPlayer()->m_currentSpell)
		GetPlayer()->m_currentSpell->cancel();
}

void WorldSession::HandleCancelAuraOpcode( WorldPacket& recvPacket)
{
	uint32 spellId;
	recvPacket >> spellId;
	
	for(uint32 x = 0; x < MAX_AURAS+MAX_POSITIVE_AURAS; ++x)
	{
		if(_player->m_auras[x] && _player->m_auras[x]->IsPositive() && _player->m_auras[x]->GetSpellId() == spellId)
		{
			_player->m_auras[x]->m_ignoreunapply = true; // prevent abuse
			_player->m_auras[x]->Remove();
		}
	}
	sLog.outDebug("removing aura %u",spellId);
}

void WorldSession::HandleCancelChannellingOpcode( WorldPacket& recvPacket)
{
	uint32 spellId;
	recvPacket >> spellId;

	Player *plyr = GetPlayer();
	if(!plyr)
		return;
	if(plyr->m_currentSpell)
	{		
		plyr->m_currentSpell->cancel();
	}
}

void WorldSession::HandleCancelAutoRepeatSpellOpcode(WorldPacket& recv_data)
{
	//sLog.outString("Received CMSG_CANCEL_AUTO_REPEAT_SPELL message.");
	//on original we automatically enter combat when creature got close to us
//	GetPlayer()->GetSession()->OutPacket(SMSG_CANCEL_COMBAT);
	GetPlayer()->m_onAutoShot = false;
}

void WorldSession::HandleAddDynamicTargetOpcode(WorldPacket & recvPacket)
{
	uint64 guid;
	uint32 spellid;
	uint8 flags;
	recvPacket >> guid >> spellid >> flags;
	
	SpellEntry * sp = dbcSpell.LookupEntry(spellid);
	// Summoned Elemental's Freeze
    if (spellid == 33395)
    {
        if (!_player->m_Summon)
            return;
    }
    else if (!_player->m_CurrentCharm || guid != _player->m_CurrentCharm->GetGUID())
    {
        return;
    }
	
	/* burlex: this is.. strange */
	SpellCastTargets targets;
	targets.m_targetMask = flags;

	if(flags == 0)
		targets.m_unitTarget = guid;
	else if(flags & 0x02)
	{
		WoWGuid guid;
		recvPacket >> flags;		// skip one byte
		recvPacket >> guid;
		targets.m_unitTarget = guid.GetOldGuid();
	}
	else if(flags & 0x20)
	{
		recvPacket >> flags;		// skip one byte
		recvPacket >> targets.m_srcX >> targets.m_srcY >> targets.m_srcZ;
	}
	else if(flags & 0x40)
	{
		recvPacket >> flags;		// skip one byte
		recvPacket >> targets.m_destX >> targets.m_destY >> targets.m_destZ;
	}
	else if (flags & 0x2000)
	{
		recvPacket >> flags;		// skip one byte
		recvPacket >> targets.m_strTarget;
	}
	if(spellid == 33395)	// Summoned Water Elemental's freeze
	{
		Spell * pSpell = new Spell(_player->m_Summon, sp, false, 0);
		pSpell->prepare(&targets);
	}
	else			// trinket?
	{
		Spell * pSpell = new Spell(_player->m_CurrentCharm, sp, false, 0);
		pSpell->prepare(&targets);
		sLog.outDebug("Spell::prepare 20");
	}
}
