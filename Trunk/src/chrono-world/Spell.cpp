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

#define SPELL_CHANNEL_UPDATE_INTERVAL 1000

/// externals for spell system
extern pSpellEffect SpellEffectsHandler[TOTAL_SPELL_EFFECTS];
extern pSpellTarget SpellTargetHandler[TOTAL_SPELL_TARGET];

enum SpellTargetSpecification
{
    TARGET_SPECT_NONE       = 0,
    TARGET_SPEC_INVISIBLE   = 1,
    TARGET_SPEC_DEAD        = 2,
};

void SpellCastTargets::read( WorldPacket & data,uint64 caster )
{
	m_unitTarget = m_itemTarget = 0;m_srcX = m_srcY = m_srcZ = m_destX = m_destY = m_destZ = 0;
    m_strTarget = "";

    data >> m_targetMask;
    WoWGuid guid;

	sLog.outDebug("targetmask = %i",m_targetMask);

    //if(m_targetMask & TARGET_FLAG_SELF)
    if(m_targetMask == TARGET_FLAG_SELF)
    {
        m_unitTarget = caster;
    }

	if( m_targetMask & (TARGET_FLAG_OBJECT | TARGET_FLAG_UNIT | TARGET_FLAG_CORPSE | TARGET_FLAG_PVP_CORPSE ) )
	{
		data >> guid;
		m_unitTarget = guid.GetOldGuid();
	}

    if(m_targetMask & TARGET_FLAG_ITEM )
    {
        data >> guid;
        m_itemTarget = guid.GetOldGuid();
    }
	
    if(m_targetMask & TARGET_FLAG_TRADE_ITEM )
    {
        data >> guid;
        m_itemTarget = guid.GetOldGuid();
    }

    if(m_targetMask & TARGET_FLAG_SOURCE_LOCATION)
    {
        data >> m_srcX >> m_srcY >> m_srcZ;
    }

    if(m_targetMask & TARGET_FLAG_DEST_LOCATION)
    {
        data >> m_destX >> m_destY >> m_destZ;
    }


    if(m_targetMask & TARGET_FLAG_STRING)
    {
        data >> m_strTarget;
    }
}

void SpellCastTargets::write( WorldPacket& data )
{
    data << m_targetMask;

	if( m_targetMask & (TARGET_FLAG_UNIT | TARGET_FLAG_CORPSE | TARGET_FLAG_PVP_CORPSE | TARGET_FLAG_OBJECT) )
        FastGUIDPack( data, m_unitTarget );

    if( m_targetMask & ( TARGET_FLAG_ITEM | TARGET_FLAG_TRADE_ITEM ) )
        FastGUIDPack( data, m_itemTarget );

    if(m_targetMask & TARGET_FLAG_SOURCE_LOCATION)
        data << m_srcX << m_srcY << m_srcZ;

    if(m_targetMask & TARGET_FLAG_DEST_LOCATION)
        data << m_destX << m_destY << m_destZ;

    if(m_targetMask & TARGET_FLAG_STRING)
        data << m_strTarget;
}

Spell::Spell(Object* Caster, SpellEntry *info, bool triggered, Aura* aur)
{
	POINTERLOGNEW(this);
	ASSERT( Caster != nullptr && info != nullptr );

	CanDelete=false;
	m_spellInfo = info;
	m_caster = Caster;
	duelSpell = false;
	
	m_spellScript = sScriptMgr.CreateAIScriptClassForSpell(m_spellInfo->Id, this);

	switch( Caster->GetTypeId() )
	{
		case TYPEID_PLAYER:
        {
		    g_caster = nullptr;
		    i_caster = nullptr;
		    u_caster = TO_UNIT( Caster );
		    p_caster = TO_PLAYER( Caster );
			if( p_caster->GetDuelState() == DUEL_STATE_STARTED )
			    duelSpell = true;
        }break;

		case TYPEID_UNIT:
        {
		    g_caster = nullptr;
		    i_caster = nullptr;
		    p_caster = nullptr;
		    u_caster = TO_UNIT( Caster );
		    if( u_caster->IsPet() && static_cast< Pet* >( u_caster)->GetPetOwner() != nullptr && static_cast< Pet* >( u_caster )->GetPetOwner()->GetDuelState() == DUEL_STATE_STARTED )
			    duelSpell = true;
        }break;

		case TYPEID_ITEM:
		case TYPEID_CONTAINER:
        {
		    g_caster = nullptr;
		    u_caster = nullptr;
		    p_caster = nullptr;
		    i_caster = static_cast< Item* >( Caster );
			if( i_caster->GetOwner() && i_caster->GetOwner()->GetDuelState() == DUEL_STATE_STARTED )
				duelSpell = true;
        }break;
		
		case TYPEID_GAMEOBJECT:
        {
		    u_caster = nullptr;
		    p_caster = nullptr;
		    i_caster = nullptr;
		    g_caster = TO_GAMEOBJECT( Caster );
        }break;
        default:
            sLog.outDebug("[DEBUG][SPELL] Incompatible object type, please report this to the dev's");
        break;
	}

	m_spellState = SPELL_STATE_NULL;

	m_castPositionX = m_castPositionY = m_castPositionZ = 0;
	//TriggerSpellId = 0;
	//TriggerSpellTarget = 0;
	m_triggeredSpell = triggered;
	m_AreaAura = false;
  
	m_triggeredByAura = aur;

	damageToHit = 0;
	castedItemId = 0;
	
	m_usesMana = false;
	m_Spell_Failed = false;
	m_CanRelect = false;
	m_IsReflected = false;
	hadEffect = false;
	bDurSet = false;
	bRadSet[0] = false;
	bRadSet[1] = false;
	bRadSet[2] = false;
	
	cancastresult = SPELL_CANCAST_OK;
	
	m_requiresCP = false;
	unitTarget = nullptr;
	ModeratedTargets.clear();
	itemTarget = nullptr;
	gameObjTarget = nullptr;
	playerTarget = nullptr;
	corpseTarget = nullptr;
	judgement = false;
	add_damage = 0;
	m_Delayed = false;
	pSpellId = 0;
	m_cancelled = false;
	ProcedOnSpell = 0;
	forced_basepoints[0] = forced_basepoints[1] = forced_basepoints[2] = 0;
	extra_cast_number = 0;
	m_reflectedParent = nullptr;
	m_isCasting = false;
}

Spell::~Spell()
{
	POINTERLOGDELETE(this);
	
	//delete only if no aura references on it :)
	if (m_spellScript != nullptr)
		m_spellScript->TryDelete();
	
	sEventMgr.RemoveEvents(this);
}

void Spell::Destructor()
{
	delete this;
}

//i might forget conditions here. Feel free to add them
bool Spell::IsStealthSpell()
{
	//check if aura name is some stealth aura
	if( m_spellInfo->EffectApplyAuraName[0] == 16 ||
		m_spellInfo->EffectApplyAuraName[1] == 16 ||
		m_spellInfo->EffectApplyAuraName[2] == 16 )
		return true;
	return false;
}

//i might forget conditions here. Feel free to add them
bool Spell::IsInvisibilitySpell()
{
	//check if aura name is some invisibility aura
	if( m_spellInfo->EffectApplyAuraName[0] == 18 ||
		m_spellInfo->EffectApplyAuraName[1] == 18 ||
		m_spellInfo->EffectApplyAuraName[2] == 18 )
		return true;
	return false;
}

void Spell::FillSpecifiedTargetsInArea( float srcx, float srcy, float srcz, uint32 ind, uint32 specification )
{
    FillSpecifiedTargetsInArea( ind, srcx, srcy, srcz, GetRadius(ind), specification );
}

// for the moment we do invisible targets
void Spell::FillSpecifiedTargetsInArea(uint32 i,float srcx,float srcy,float srcz, float range, uint32 specification)
{
	TargetsList *tmpMap=&m_targetUnits[i];
    float r = range * range;
	uint8 did_hit_result;
	
	uint32 TargetInfo = m_spellInfo->EffectImplicitTargetB[i];
	
    for(std::set<Object*>::iterator itr = m_caster->GetInRangeSetBegin(); itr != m_caster->GetInRangeSetEnd(); itr++ )
    {
        // don't add objects that are not units and that are dead
        if( !( (*itr)->IsUnit() ) || ! TO_UNIT( *itr )->isAlive())
            continue;
        
        //TO_UNIT( *itr )->IsStealth()
        if( m_spellInfo->TargetCreatureType)
        {
            if((*itr)->GetTypeId()!= TYPEID_UNIT)
                continue;
            CreatureInfo *inf = ((Creature*)(*itr))->GetCreatureName();
            if(!inf || !(1<<(inf->Type-1) & m_spellInfo->TargetCreatureType))
                continue;
        }

        if(IsInrange(srcx,srcy,srcz,(*itr),r))
        {
            if( u_caster != nullptr )
            {
				if( TargetsHostile(i) && isAttackable( u_caster, static_cast< Unit* >( *itr ), !(m_spellInfo->c_is_flags & SPELL_FLAG_IS_TARGETINGSTEALTHED) ) )
                {
					did_hit_result = DidHit(i, TO_UNIT( *itr ) );
					if( did_hit_result != SPELL_DID_HIT_SUCCESS )
						ModeratedTargets.push_back(SpellTargetMod((*itr)->GetGUID(), did_hit_result));
					else
						tmpMap->push_back((*itr)->GetGUID());
                }

            }
            else //cast from GO
            {
                if(g_caster && g_caster->GetUInt32Value(OBJECT_FIELD_CREATED_BY) && g_caster->m_summoner)
                {
                    //trap, check not to attack owner and friendly
                    if(isAttackable(g_caster->m_summoner,(Unit*)(*itr),!(m_spellInfo->c_is_flags & SPELL_FLAG_IS_TARGETINGSTEALTHED)))
                        tmpMap->push_back((*itr)->GetGUID());
                }
                else
                    tmpMap->push_back((*itr)->GetGUID());
            }
            if( m_spellInfo->MaxTargets)
            {
                if( m_spellInfo->MaxTargets >= tmpMap->size())
                    return;
            }
        }
    }
}
void Spell::FillAllTargetsInArea(LocationVector & location,uint32 ind)
{
    FillAllTargetsInArea(ind,location.x,location.y,location.z,GetRadius(ind));
}

void Spell::FillAllTargetsInArea(float srcx,float srcy,float srcz,uint32 ind)
{
	FillAllTargetsInArea(ind,srcx,srcy,srcz,GetRadius(ind));
}

/// We fill all the targets in the area, including the stealth ed one's
void Spell::FillAllTargetsInArea(uint32 i,float srcx,float srcy,float srcz, float range)
{
	TargetsList *tmpMap=&m_targetUnits[i];
	float r = range*range;
	uint8 did_hit_result;
	for( std::set<Object*>::iterator itr = m_caster->GetInRangeSetBegin(); itr != m_caster->GetInRangeSetEnd(); itr++ )
	{
		if( !( (*itr)->IsUnit() ) || ! TO_UNIT( *itr )->isAlive() || ( TO_CREATURE( *itr )->IsTotem() && !TO_UNIT( *itr )->IsPlayer() ) )
			continue;

		if( m_spellInfo->TargetCreatureType )
		{
			if( (*itr)->GetTypeId()!= TYPEID_UNIT )
				continue;
			CreatureInfo *inf = ((Creature*)(*itr))->GetCreatureName();
			if( !inf || !( 1 << (inf->Type-1) & m_spellInfo->TargetCreatureType ) )
				continue;
		}
		if( IsInrange( srcx, srcy, srcz, (*itr), r ) )
		{
			if( u_caster != nullptr )
			{
				if( isAttackable( u_caster, TO_UNIT( *itr ), !(m_spellInfo->c_is_flags & SPELL_FLAG_IS_TARGETINGSTEALTHED) ) )
				{
					did_hit_result = DidHit(i, TO_UNIT( *itr ) );
					if( did_hit_result == SPELL_DID_HIT_SUCCESS )
						tmpMap->push_back( (*itr)->GetGUID() );
					else
						ModeratedTargets.push_back( SpellTargetMod( (*itr)->GetGUID(), did_hit_result ) );
				}
				if ( TargetsFriendly(i) && isFriendly ( u_caster, *itr ) )
					tmpMap->push_back( (*itr)->GetGUID() );
			}
			else //cast from GO
			{
				if( g_caster != nullptr && g_caster->GetUInt32Value( OBJECT_FIELD_CREATED_BY ) && g_caster->m_summoner != nullptr )
				{
					//trap, check not to attack owner and friendly
					if( TargetsHostile(i) && isAttackable( g_caster->m_summoner, *itr, !(m_spellInfo->c_is_flags & SPELL_FLAG_IS_TARGETINGSTEALTHED) ) )
						tmpMap->push_back( (*itr)->GetGUID() );
						
					if ( TargetsFriendly(i) && isFriendly ( g_caster->m_summoner, *itr ) )
						tmpMap->push_back( (*itr)->GetGUID() );
				}
				else
					tmpMap->push_back( (*itr)->GetGUID() );
			}			
			if( m_spellInfo->MaxTargets )
				if( m_spellInfo->MaxTargets == tmpMap->size() )
					return;
		}
	}	
}

// We fill all the targets in the area, including the stealth ed one's
void Spell::FillAllFriendlyInArea( uint32 i, float srcx, float srcy, float srcz, float range )
{
	TargetsList *tmpMap=&m_targetUnits[i];
	float r = range * range;
	uint8 did_hit_result;
	for( std::set<Object*>::iterator itr = m_caster->GetInRangeSetBegin(); itr != m_caster->GetInRangeSetEnd(); itr++ )
	{
		if( !((*itr)->IsUnit()) || !TO_UNIT( *itr )->isAlive() )
			continue;

		if( m_spellInfo->TargetCreatureType )
		{
			if((*itr)->GetTypeId()!= TYPEID_UNIT)
				continue;
			CreatureInfo *inf = ((Creature*)(*itr))->GetCreatureName();
			if(!inf || !(1<<(inf->Type-1) & m_spellInfo->TargetCreatureType))
				continue;
		}

		if( IsInrange( srcx, srcy, srcz, (*itr), r ) )
		{
			if( u_caster != nullptr )
			{
				if( isFriendly( u_caster, TO_UNIT( *itr ) ) )
				{
					did_hit_result = DidHit(i, TO_UNIT( *itr ) );
					if( did_hit_result == SPELL_DID_HIT_SUCCESS )
						tmpMap->push_back( (*itr)->GetGUID() );
					else
						ModeratedTargets.push_back( SpellTargetMod( (*itr)->GetGUID(), did_hit_result ) );
				}
			}
			else //cast from GO
			{
				if( g_caster != nullptr && g_caster->GetUInt32Value( OBJECT_FIELD_CREATED_BY ) && g_caster->m_summoner != nullptr )
				{
					//trap, check not to attack owner and friendly
					if( isFriendly( g_caster->m_summoner, TO_UNIT( *itr ) ) )
						tmpMap->push_back( (*itr)->GetGUID() );
				}
				else
					tmpMap->push_back( (*itr)->GetGUID() );
			}			
			if( m_spellInfo->MaxTargets )
				if( m_spellInfo->MaxTargets == tmpMap->size() )
					return;
		}
	}	
}

uint64 Spell::GetSinglePossibleEnemy(uint32 i,float prange)
{
	float r;
	if(prange)
		r = prange;
	else
	{
		r = m_spellInfo->base_range_or_radius_sqr;
		if( m_spellInfo->SpellGroupType && u_caster)
		{
			SM_FFValue(u_caster->SM_FRadius,&r,m_spellInfo->SpellGroupType);
			SM_PFValue(u_caster->SM_PRadius,&r,m_spellInfo->SpellGroupType);
		}
	}
	float srcx = m_caster->GetPositionX(), srcy = m_caster->GetPositionY(), srcz = m_caster->GetPositionZ();
	for( std::set<Object*>::iterator itr = m_caster->GetInRangeSetBegin(); itr != m_caster->GetInRangeSetEnd(); itr++ )
	{
		if( !( (*itr)->IsUnit() ) || !TO_UNIT( *itr )->isAlive() )
			continue;

		if( m_spellInfo->TargetCreatureType )
		{
			if( (*itr)->GetTypeId() != TYPEID_UNIT )
				continue;
			CreatureInfo *inf = ((Creature*)(*itr))->GetCreatureName();
			if(!inf || !(1<<(inf->Type-1) & m_spellInfo->TargetCreatureType))
				continue;
		}	
		if(IsInrange(srcx,srcy,srcz,(*itr),r))
		{
			if( u_caster != nullptr )
			{
				if(isAttackable(u_caster, TO_UNIT( *itr ),!(m_spellInfo->c_is_flags & SPELL_FLAG_IS_TARGETINGSTEALTHED)) && DidHit(i,((Unit*)*itr))==SPELL_DID_HIT_SUCCESS)
					return (*itr)->GetGUID(); 			
			}
			else //cast from GO
			{
				if(g_caster && g_caster->GetUInt32Value(OBJECT_FIELD_CREATED_BY) && g_caster->m_summoner)
				{
					//trap, check not to attack owner and friendly
					if( isAttackable( g_caster->m_summoner, TO_UNIT( *itr ),!(m_spellInfo->c_is_flags & SPELL_FLAG_IS_TARGETINGSTEALTHED)))
						return (*itr)->GetGUID();
				}
			}			
		}
	}
	return 0;
}

uint64 Spell::GetSinglePossibleFriend(uint32 i,float prange)
{
	float r;
	if(prange)
		r = prange;
	else
	{
		r = m_spellInfo->base_range_or_radius_sqr;
		if( m_spellInfo->SpellGroupType && u_caster)
		{
			SM_FFValue(u_caster->SM_FRadius,&r,m_spellInfo->SpellGroupType);
			SM_PFValue(u_caster->SM_PRadius,&r,m_spellInfo->SpellGroupType);
		}
	}
	float srcx=m_caster->GetPositionX(),srcy=m_caster->GetPositionY(),srcz=m_caster->GetPositionZ();
	for(std::set<Object*>::iterator itr = m_caster->GetInRangeSetBegin(); itr != m_caster->GetInRangeSetEnd(); itr++ )
	{
		if( !( (*itr)->IsUnit() ) || !TO_UNIT( *itr )->isAlive() )
			continue;
		if( m_spellInfo->TargetCreatureType )
		{
			if((*itr)->GetTypeId()!= TYPEID_UNIT)
				continue;
			CreatureInfo *inf = ((Creature*)(*itr))->GetCreatureName();
				if(!inf || !(1<<(inf->Type-1) & m_spellInfo->TargetCreatureType))
					continue;
		}	
		if(IsInrange(srcx,srcy,srcz,(*itr),r))
		{
			if( u_caster != nullptr )
			{
				if( isFriendly( u_caster, TO_UNIT( *itr ) ) && DidHit(i, ((Unit*)*itr))==SPELL_DID_HIT_SUCCESS)
					return (*itr)->GetGUID(); 			
			}
			else //cast from GO
			{
				if(g_caster && g_caster->GetUInt32Value(OBJECT_FIELD_CREATED_BY) && g_caster->m_summoner)
				{
					//trap, check not to attack owner and friendly
					if( isFriendly( g_caster->m_summoner, TO_UNIT( *itr ) ) )
						return (*itr)->GetGUID();
				}
			}			
		}
	}
	return 0;
}

uint8 Spell::DidHit(uint32 effindex,Unit* target)
{
	//note resistchance is vise versa, is full hit chance
	Unit* u_victim = target;
	Player* p_victim = ( target->GetTypeId() == TYPEID_PLAYER ) ? TO_PLAYER( target ) : nullptr;

	// 
	float baseresist[3] = { 4.0f, 5.0f, 6.0f };
	int32 lvldiff;
	float resistchance ;
	if( u_victim == nullptr )
		return SPELL_DID_HIT_MISS;
	
	/************************************************************************/
	/* Elite mobs always hit                                                */
	/************************************************************************/
	if(u_caster && u_caster->GetTypeId()==TYPEID_UNIT && ((Creature*)u_caster)->GetCreatureName() && ((Creature*)u_caster)->GetCreatureName()->Rank >= 3)
		return SPELL_DID_HIT_SUCCESS;

	/************************************************************************/
	/* Can't resist non-unit                                                */
	/************************************************************************/
	if(!u_caster)
		return SPELL_DID_HIT_SUCCESS;

	/************************************************************************/
	/* Can't reduce your own spells                                         */
	/************************************************************************/
	if(u_caster == u_victim)
		return SPELL_DID_HIT_SUCCESS;

	/************************************************************************/
	/* Check if the unit is evading                                         */
	/************************************************************************/
	if(u_victim->GetTypeId()==TYPEID_UNIT && u_victim->GetAIInterface()->getAIState()==STATE_EVADE)
		return SPELL_DID_HIT_EVADE;

	/************************************************************************/
	/* Check if the target is immune to this spell school                   */
	/************************************************************************/
	if(u_victim->IsImmuneForCustomEffect(m_spellInfo->School))
		return SPELL_DID_HIT_IMMUNE;
	
	/*************************************************************************/
	/* Check if the target is immune to this mechanic                        */
	/*************************************************************************/
 
	if(u_victim->IsImmuneFor(m_spellInfo->MechanicsType))
		return SPELL_DID_HIT_IMMUNE; // Moved here from Spell::CanCast

	/*************************************************************************/
	/* Check if the target is immune to this spell type                      */
	/*************************************************************************/

	if(m_spellInfo->c_is_flags & SPELL_FLAG_IS_POISON && u_victim->IsImmuneForCustomEffect(IMMUNE_POISON))
		return SPELL_DID_HIT_IMMUNE;

	if(m_spellInfo->DispelType == DISPEL_CURSE && u_victim->IsImmuneForCustomEffect(IMMUNE_POISON))
		return SPELL_DID_HIT_IMMUNE;
	
	/************************************************************************/
	/* Check if the target has a % resistance to this mechanic              */
	/************************************************************************/
		/* Never mind, it's already done below. Lucky I didn't go through with this, or players would get double resistance. */

	/************************************************************************/
	/* Check if the spell is a melee attack and if it was missed/parried    */
	/************************************************************************/
	uint32 melee_test_result;
	if( m_spellInfo->is_melee_spell || m_spellInfo->is_ranged_spell )
	{
		uint32 _type;
		if( GetType() == SPELL_DMG_TYPE_RANGED )
			_type = RANGED;
		else
		{
			if (m_spellInfo->Flags4 & 0x1000000)
				_type =  OFFHAND;
			else
				_type = MELEE;
		}

		melee_test_result = u_caster->GetSpellDidHitResult( u_victim, _type, m_spellInfo );
		if(melee_test_result != SPELL_DID_HIT_SUCCESS)
			return (uint8)melee_test_result;
	}

	/************************************************************************/
	/* Check if the spell is resisted.                                      */
	/************************************************************************/
	if( m_spellInfo->School==0  || m_spellInfo->is_ranged_spell ) // all ranged spells are physical too...
		return SPELL_DID_HIT_SUCCESS;

	bool pvp =(p_caster && p_victim);

	if(pvp)
		lvldiff = p_victim->getLevel() - p_caster->getLevel();
	else
		lvldiff = u_victim->getLevel() - u_caster->getLevel();
	if (lvldiff < 0)
	{
		resistchance = baseresist[0] +lvldiff;
	}
	else
	{
		if(lvldiff < 3)
		{ 
				resistchance = baseresist[lvldiff];
		}
		else
		{
			if(pvp)
				resistchance = baseresist[2] + (((float)lvldiff-2.0f)*7.0f);
			else
				resistchance = baseresist[2] + (((float)lvldiff-2.0f)*11.0f);
		}
	}
	//check mechanical resistance
	//i have no idea what is the best pace for this code
	if( m_spellInfo->MechanicsType<27)
	{
		if(p_victim)
			resistchance += p_victim->MechanicsResistancesPCT[m_spellInfo->MechanicsType];
		else 
			resistchance += u_victim->MechanicsResistancesPCT[m_spellInfo->MechanicsType];
	}
	//rating bonus
	if( p_caster != nullptr )
	{
		resistchance -= p_caster->CalcRating( PLAYER_RATING_MODIFIER_SPELL_HIT );
		resistchance -= p_caster->GetHitFromSpell();
	}

	if(p_victim)
		resistchance += p_victim->m_resist_hit[2];

	if( this->m_spellInfo->Effect[effindex] == SPELL_EFFECT_DISPEL && m_spellInfo->SpellGroupType && u_caster)
	{
		SM_FFValue(u_caster->SM_FRezist_dispell,&resistchance,m_spellInfo->SpellGroupType);
		SM_PFValue(u_caster->SM_PRezist_dispell,&resistchance,m_spellInfo->SpellGroupType);
#ifdef COLLECTION_OF_UNTESTED_STUFF_AND_TESTERS
		int spell_flat_modifers=0;
		int spell_pct_modifers=0;
		SM_FIValue(u_caster->SM_FRezist_dispell,&spell_flat_modifers,m_spellInfo->SpellGroupType);
		SM_FIValue(u_caster->SM_PRezist_dispell,&spell_pct_modifers,m_spellInfo->SpellGroupType);
		if(spell_flat_modifers!=0 || spell_pct_modifers!=0)
			printf("!!!!!spell dipell resist mod flat %d , spell dipell resist mod pct %d , spell dipell resist %d, spell group %u\n",spell_flat_modifers,spell_pct_modifers,resistchance,m_spellInfo->SpellGroupType);
#endif
	}

	if(m_spellInfo->SpellGroupType && u_caster)
	{
		float hitchance=0;
		SM_FFValue(u_caster->SM_FHitchance,&hitchance,m_spellInfo->SpellGroupType);
		resistchance -= hitchance;
#ifdef COLLECTION_OF_UNTESTED_STUFF_AND_TESTERS
		float spell_flat_modifers=0;
		SM_FFValue(u_caster->SM_FHitchance,&spell_flat_modifers,m_spellInfo->SpellGroupType);
		if(spell_flat_modifers!=0 )
			printf("!!!!!spell to hit mod flat %f, spell resist chance %f, spell group %u\n",spell_flat_modifers,resistchance,m_spellInfo->SpellGroupType);
#endif
	}

	if (m_spellInfo->Attributes & ATTRIBUTES_IGNORE_INVULNERABILITY)
		resistchance = 0.0f;

	if(resistchance >= 100.0f)
		return SPELL_DID_HIT_RESIST;
	else
	{
		uint32 res;
		if(resistchance<=1.0)//resist chance >=1
			res =  (Rand(1.0f) ? SPELL_DID_HIT_RESIST : SPELL_DID_HIT_SUCCESS);
		else
			res =  (Rand(resistchance) ? SPELL_DID_HIT_RESIST : SPELL_DID_HIT_SUCCESS);

		if (res == SPELL_DID_HIT_SUCCESS) // proc handling. mb should be moved outside this function
			target->HandleProc(PROC_ON_SPELL_CRIT | PROC_ON_SPELL_HIT,u_caster,m_spellInfo);	

		return res;
	}
 
}
//generate possible target list for a spell. Use as last resort since it is not acurate
//this function makes a rough estimation for possible target !
//!!!disabled parts that were not tested !!
void Spell::GenerateTargets(SpellCastTargets *store_buff)
{
	float r = m_spellInfo->base_range_or_radius_sqr;
	if( m_spellInfo->SpellGroupType && u_caster)
	{
		SM_FFValue(u_caster->SM_FRadius,&r,m_spellInfo->SpellGroupType);
		SM_PFValue(u_caster->SM_PRadius,&r,m_spellInfo->SpellGroupType);
	}
	uint32 cur;
	for(uint32 i=0;i<3;i++)
		for(uint32 j=0;j<2;j++)
		{
			if(j==0)
				cur = m_spellInfo->EffectImplicitTargetA[i];
			else // if(j==1)
				cur = m_spellInfo->EffectImplicitTargetB[i];		
			switch(cur)
			{
				case EFF_TARGET_NONE:{
					//this is bad for us :(
					}break;
				case EFF_TARGET_SELF:{
						if(m_caster->IsUnit())
							store_buff->m_unitTarget = m_caster->GetGUID();
					}break;		
					// need more research
				case 4:{ // dono related to "Wandering Plague", "Spirit Steal", "Contagion of Rot", "Retching Plague" and "Copy of Wandering Plague"
					}break;			
				case EFF_TARGET_PET:
					{// Target: Pet
						if(p_caster && p_caster->GetSummon())
							store_buff->m_unitTarget = p_caster->GetSummon()->GetGUID();
					}break;
				case EFF_TARGET_SINGLE_ENEMY:// Single Target Enemy
				case 77:					// grep: i think this fits
				case 8: // related to Chess Move (DND), Firecrackers, Spotlight, aedm, Spice Mortar
				case EFF_TARGET_ALL_ENEMY_IN_AREA: // All Enemies in Area of Effect (TEST)
				case EFF_TARGET_ALL_ENEMY_IN_AREA_INSTANT: // All Enemies in Area of Effect instant (e.g. Flamestrike)
				case EFF_TARGET_ALL_ENEMIES_AROUND_CASTER:
				case EFF_TARGET_IN_FRONT_OF_CASTER:
				case EFF_TARGET_ALL_ENEMY_IN_AREA_CHANNELED:// All Enemies in Area of Effect(Blizzard/Rain of Fire/volley) channeled
				case 31:// related to scripted effects
				case 53:// Target Area by Players CurrentSelection()
				case 54:// Targets in Front of the Caster
					{
						if( p_caster != nullptr )
						{
							Unit *selected = p_caster->GetMapMgr()->GetUnit(p_caster->GetSelection());
							if(isAttackable(p_caster,selected,!(m_spellInfo->c_is_flags & SPELL_FLAG_IS_TARGETINGSTEALTHED)))
								store_buff->m_unitTarget = p_caster->GetSelection();
						}
						else if( u_caster != nullptr )
						{
							if(	u_caster->GetAIInterface()->GetNextTarget() &&
								isAttackable(u_caster,u_caster->GetAIInterface()->GetNextTarget(),!(m_spellInfo->c_is_flags & SPELL_FLAG_IS_TARGETINGSTEALTHED)) &&
								u_caster->GetDistanceSq(u_caster->GetAIInterface()->GetNextTarget()) <= r)
							{
								store_buff->m_unitTarget = u_caster->GetAIInterface()->GetNextTarget()->GetGUID();
							}
							if(u_caster->GetAIInterface()->getAITargetsCount())
							{
								//try to get most hated creature
								TargetMap *m_aiTargets = u_caster->GetAIInterface()->GetAITargets();
								TargetMap::iterator itr;
								for(itr = m_aiTargets->begin(); itr != m_aiTargets->end();itr++)
								{
									if( /*m_caster->GetMapMgr()->GetUnit(itr->first->GetGUID()) &&*/ itr->first->GetMapMgr() == m_caster->GetMapMgr() && 
										itr->first->isAlive() &&
										m_caster->GetDistanceSq(itr->first) <= r &&
										isAttackable(u_caster,itr->first,!(m_spellInfo->c_is_flags & SPELL_FLAG_IS_TARGETINGSTEALTHED))
										)
									{
										store_buff->m_unitTarget=itr->first->GetGUID();
										break;
									}
								}
							}
						}
						//try to get a whatever target
						if(!store_buff->m_unitTarget)
						{
							store_buff->m_unitTarget=GetSinglePossibleEnemy(i);
						}
						//if we still couldn't get a target, check maybe we could use 
//						if(!store_buff->m_unitTarget)
//						{
//						}
					}break;
					// spells like 17278:Cannon Fire and 21117:Summon Son of Flame A
				case 17: // A single target at a xyz location or the target is a possition xyz
				case 18:// Land under caster.Maybe not correct
					{
						store_buff->m_srcX=m_caster->GetPositionX();
						store_buff->m_srcY=m_caster->GetPositionY();
						store_buff->m_srcZ=m_caster->GetPositionZ();
						store_buff->m_targetMask |= TARGET_FLAG_SOURCE_LOCATION;
					}break;
				case EFF_TARGET_ALL_PARTY_AROUND_CASTER:
					{// All Party Members around the Caster in given range NOT RAID!			
						Player* p = p_caster;
						if( p == nullptr)
						{
							if( TO_CREATURE( u_caster )->IsTotem() )
								p = TO_PLAYER( TO_CREATURE( u_caster )->GetTotemOwner() );
						}
						if( p == nullptr )
							break;

						if(IsInrange(m_caster->GetPositionX(),m_caster->GetPositionY(),m_caster->GetPositionZ(),p,r))
						{
							store_buff->m_unitTarget = m_caster->GetGUID();
							break;
						}
						SubGroup * subgroup = p->GetGroup() ?
							p->GetGroup()->GetSubGroup(p->GetSubGroup()) : 0;

						if(subgroup)
						{
							p->GetGroup()->Lock();
							for(GroupMembersSet::iterator itr = subgroup->GetGroupMembersBegin(); itr != subgroup->GetGroupMembersEnd(); ++itr)
							{
								if(!(*itr)->m_loggedInPlayer || m_caster == (*itr)->m_loggedInPlayer) 
									continue;
								if(IsInrange(m_caster->GetPositionX(),m_caster->GetPositionY(),m_caster->GetPositionZ(),(*itr)->m_loggedInPlayer,r))
								{
									store_buff->m_unitTarget = (*itr)->m_loggedInPlayer->GetGUID();
									break;
								}
							}
							p->GetGroup()->Unlock();
						}
					}break;
				case EFF_TARGET_SINGLE_FRIEND:
				case 45:// Chain,!!only for healing!! for chain lightning =6 
				case 57:// Targeted Party Member
					{// Single Target Friend
						if( p_caster != nullptr )
						{
							if(isFriendly(p_caster,p_caster->GetMapMgr()->GetUnit(p_caster->GetSelection())))
								store_buff->m_unitTarget = p_caster->GetSelection();
							else store_buff->m_unitTarget = p_caster->GetGUID();
						}
						else if( u_caster != nullptr )
						{
							if(u_caster->GetUInt64Value(UNIT_FIELD_CREATEDBY))
								store_buff->m_unitTarget = u_caster->GetUInt64Value(UNIT_FIELD_CREATEDBY);
							else store_buff->m_unitTarget = u_caster->GetGUID();
						}
						else store_buff->m_unitTarget=GetSinglePossibleFriend(i,r);			
					}break;
				case EFF_TARGET_GAMEOBJECT:
					{
						if(p_caster && p_caster->GetSelection())
							store_buff->m_unitTarget = p_caster->GetSelection();
					}break;
				case EFF_TARGET_DUEL: 
					{// Single Target Friend Used in Duel
						if(p_caster && p_caster->DuelingWith && p_caster->DuelingWith->isAlive() && IsInrange(p_caster,p_caster->DuelingWith,r))
							store_buff->m_unitTarget = p_caster->GetSelection();
					}break;
				case EFF_TARGET_GAMEOBJECT_ITEM:{// Gameobject/Item Target
						//shit
					}break;
				case 27:{ // target is owner of pet
					// please correct this if not correct does the caster variablen need a Pet caster variable?
						if(u_caster && u_caster->IsPet())
							store_buff->m_unitTarget = static_cast< Pet* >( u_caster )->GetPetOwner()->GetGUID();
					}break;
				case EFF_TARGET_MINION:
				case 73:
					{// Minion Target
						if(m_caster->GetUInt64Value(UNIT_FIELD_SUMMON) == 0)
							store_buff->m_unitTarget = m_caster->GetGUID();
						else store_buff->m_unitTarget = m_caster->GetUInt64Value(UNIT_FIELD_SUMMON);
					}break;
				case 33://Party members of totem, inside given range
				case EFF_TARGET_SINGLE_PARTY:// Single Target Party Member
				case EFF_TARGET_ALL_PARTY: // all Members of the targets party
					{
						Player *p=nullptr;
						if( p_caster != nullptr )
								p = p_caster;
						else if( u_caster && u_caster->GetTypeId() == TYPEID_UNIT && TO_CREATURE( u_caster )->IsTotem() )
								p = TO_PLAYER( TO_CREATURE( u_caster )->GetTotemOwner() );
						if( p_caster != nullptr )
						{
							if(IsInrange(m_caster->GetPositionX(),m_caster->GetPositionY(),m_caster->GetPositionZ(),p,r))
							{
								store_buff->m_unitTarget = p->GetGUID();
								break;
							}
							SubGroup * pGroup = p_caster->GetGroup() ?
								p_caster->GetGroup()->GetSubGroup(p_caster->GetSubGroup()) : 0;

							if( pGroup )
							{
								p_caster->GetGroup()->Lock();
								for(GroupMembersSet::iterator itr = pGroup->GetGroupMembersBegin();
									itr != pGroup->GetGroupMembersEnd(); ++itr)
								{
									if(!(*itr)->m_loggedInPlayer || p == (*itr)->m_loggedInPlayer) 
										continue;
									if(IsInrange(m_caster->GetPositionX(),m_caster->GetPositionY(),m_caster->GetPositionZ(),(*itr)->m_loggedInPlayer,r))
									{
										store_buff->m_unitTarget = (*itr)->m_loggedInPlayer->GetGUID();
										break;
									}
								}
								p_caster->GetGroup()->Unlock();
							}
						}
					}break;
				case 38:{//Dummy Target
					//have no idea
					}break;
				case EFF_TARGET_SELF_FISHING://Fishing
				case 46://Unknown Summon Atal'ai Skeleton
				case 47:// Portal
				case 52:	// Lightwells, etc
					{
						store_buff->m_unitTarget = m_caster->GetGUID();
					}break;
				case 40://Activate Object target(probably based on focus)
				case EFF_TARGET_TOTEM_EARTH:
				case EFF_TARGET_TOTEM_WATER:
				case EFF_TARGET_TOTEM_AIR:
				case EFF_TARGET_TOTEM_FIRE:// Totem
					{
						if( p_caster != nullptr )
						{
							uint32 slot = m_spellInfo->Effect[i] - SPELL_EFFECT_SUMMON_TOTEM_SLOT1;
							if(p_caster->m_TotemSlots[slot] != 0)
								store_buff->m_unitTarget = p_caster->m_TotemSlots[slot]->GetGUID();
						}
					}break;
				case 61:{ // targets with the same group/raid and the same class
					//shit again
				}break;
				case EFF_TARGET_ALL_FRIENDLY_IN_AREA:{

				}break;
					
			}//end switch
		}//end for
	if(store_buff->m_unitTarget)
		store_buff->m_targetMask |= TARGET_FLAG_UNIT;
	if(store_buff->m_srcX)
		store_buff->m_targetMask |= TARGET_FLAG_SOURCE_LOCATION;
	if(store_buff->m_destX)
		store_buff->m_targetMask |= TARGET_FLAG_DEST_LOCATION;
}//end function

uint8 Spell::prepare( SpellCastTargets * targets )
{
	sLog.outDebug("Spell::prepare");

	uint8 ccr;

	chaindamage = 0;
	m_targets = *targets;

	if( !m_triggeredSpell && p_caster != nullptr && p_caster->CastTimeCheat )
		m_castTime = 0;
	else
	{
		m_castTime = GetCastTime( dbcSpellCastTime.LookupEntry( m_spellInfo->CastingTimeIndex ) );

		if( m_castTime && m_spellInfo->SpellGroupType && u_caster != nullptr )
		{
			SM_FIValue( u_caster->SM_FCastTime, (int32*)&m_castTime, m_spellInfo->SpellGroupType );
			SM_PIValue( u_caster->SM_PCastTime, (int32*)&m_castTime, m_spellInfo->SpellGroupType );
		}

		// handle MOD_CAST_TIME
		if( u_caster != nullptr && m_castTime )
		{
			m_castTime = float2int32( m_castTime * u_caster->GetFloatValue( UNIT_MOD_CAST_SPEED ) );
		}
	}

	uint8 forced_cancast_failure = 0;
	if( p_caster != nullptr )
	{
		if( p_caster->cannibalize )
		{
			sEventMgr.RemoveEvents( p_caster, EVENT_CANNIBALIZE );
			p_caster->SetUInt32Value( UNIT_NPC_EMOTESTATE, 0 );
			p_caster->cannibalize = false;
		}

		if( GetGameObjectTarget() )
		{
			if( p_caster->IsStealth() )
			{
				p_caster->RemoveAura( p_caster->m_stealth );
			}

			if( (GetSpellProto()->Effect[0] == SPELL_EFFECT_OPEN_LOCK ||
				GetSpellProto()->Effect[1] == SPELL_EFFECT_OPEN_LOCK ||
				GetSpellProto()->Effect[2] == SPELL_EFFECT_OPEN_LOCK) && 
				p_caster->m_bgFlagIneligible)
			{
				forced_cancast_failure = SPELL_FAILED_BAD_TARGETS;
			}
		}
	}
	
	//let us make sure cast_time is within decent range
	//this is a hax but there is no spell that has more then 10 minutes cast time

	if( m_castTime < 0 )
		m_castTime = 0;
	else if( m_castTime > 60 * 10 * 1000)
		m_castTime = 60 * 10 * 1000; //we should limit cast time to 10 minutes right ?

	m_timer = m_castTime;

	//if( p_caster != nullptr )
	//   m_castTime -= 100;	  // session update time


	if( !m_triggeredSpell && p_caster != nullptr && p_caster->CooldownCheat )
		p_caster->ClearCooldownForSpell( m_spellInfo->Id );
	
	if( m_triggeredSpell )
		cancastresult = SPELL_CANCAST_OK;
	else
		cancastresult = CanCast(false);

	sLog.outDebug( "CanCast result: %u." , cancastresult );

	ccr = cancastresult;
	
	if( forced_cancast_failure )
		cancastresult = forced_cancast_failure;
		
	if( cancastresult != SPELL_CANCAST_OK )
	{
		sLog.outDebug("cancastresult != SPELL_CANCAST_OK");
		SendCastResult( cancastresult );

		if( m_triggeredByAura )
		{
			SendChannelUpdate( 0 );
			if( u_caster != nullptr )
				u_caster->RemoveAura( m_triggeredByAura );
		}
		else
		{
			// HACK, real problem is the way spells are handled
			// when a spell is channeling and a new spell is casted
			// that is a channeling spell, but not triggert by a aura
			// the channel bar/spell is bugged
			if( u_caster->GetUInt64Value( UNIT_FIELD_CHANNEL_OBJECT) > 0 && u_caster->GetCurrentSpell() )
			{
				u_caster->GetCurrentSpell()->cancel();
				SendChannelUpdate( 0 );
				cancel();
				return ccr;
			}
		}
		finish();
		return ccr;
	}
	else
	{
		sLog.outDebug("SendSpellStart()");
		SendSpellStart();

		// start cooldown handler
		if( p_caster != nullptr && !p_caster->CastTimeCheat && !m_triggeredSpell )
		{
			AddStartCooldown();
		}
		
		if( i_caster == nullptr )
		{
			if( p_caster != nullptr && m_timer > 0 && !m_triggeredSpell )
				p_caster->delayAttackTimer( m_timer + 1000 );
				//p_caster->setAttackTimer(m_timer + 1000, false);
		}
		
		// aura state removal
		if( m_spellInfo->CasterAuraState )
			u_caster->RemoveFlag( UNIT_FIELD_AURASTATE, m_spellInfo->CasterAuraState );
	}

	m_spellState = SPELL_STATE_PREPARING;
	
	//instant cast(or triggered) and not channeling
	if( u_caster != nullptr && ( m_castTime > 0 || m_spellInfo->ChannelInterruptFlags ) && !m_triggeredSpell )	
	{
		sLog.outDebug("Casting");
		m_castPositionX = m_caster->GetPositionX();
		m_castPositionY = m_caster->GetPositionY();
		m_castPositionZ = m_caster->GetPositionZ();
	
		u_caster->castSpell( this );
	}
	else
	{
		sLog.outDebug("Instant Cast");
		cast( false );
	}

	return ccr;
}

void Spell::cancel()
{
	SendInterrupted(0);
	SendCastResult(SPELL_FAILED_INTERRUPTED);

	if(m_spellState == SPELL_STATE_CASTING)
	{
		if( u_caster != nullptr )
			u_caster->RemoveAura(m_spellInfo->Id);
	
		if(m_timer > 0 || m_Delayed)
		{
			if(p_caster && p_caster->IsInWorld())
			{
				Unit *pTarget = p_caster->GetMapMgr()->GetUnit(m_caster->GetUInt64Value(UNIT_FIELD_CHANNEL_OBJECT));
				if(!pTarget)
					pTarget = p_caster->GetMapMgr()->GetUnit(p_caster->GetSelection());
				  
				if(pTarget)
				{
					pTarget->RemoveAura(m_spellInfo->Id, m_caster->GetGUID());
				}
				if(m_AreaAura)//remove of blizz and shit like this
				{
					
					DynamicObject* dynObj=m_caster->GetMapMgr()->GetDynamicObject(m_caster->GetUInt32Value(UNIT_FIELD_CHANNEL_OBJECT));
					if(dynObj)
					{
						//remove next update
						dynObj->m_aliveDuration = 1;
					}
				}

				if(p_caster->GetSummonedObject())
				{
					if(p_caster->GetSummonedObject()->IsInWorld())
						p_caster->GetSummonedObject()->RemoveFromWorld(true);
					// for now..
					ASSERT(p_caster->GetSummonedObject()->GetTypeId() == TYPEID_GAMEOBJECT);
					delete ((GameObject*)(p_caster->GetSummonedObject()));
					p_caster->SetSummonedObject(nullptr);
				}
				if (m_timer > 0)
					p_caster->delayAttackTimer(-m_timer);
//				p_caster->setAttackTimer(1000, false);
			 }
		}
		SendChannelUpdate(0);
	}

	//m_spellState = SPELL_STATE_FINISHED;

	// prevent memory corruption. free it up later.
	// if this is true it means we are currently in the cast() function somewhere else down the stack
	// (recursive spells) and we don't wanna have this class delete'd when we return to it.
	// at the end of cast() it will get freed anyway.
	if( !m_isCasting )
		finish();
}

void Spell::AddCooldown()
{
	if( p_caster != nullptr )
		p_caster->Cooldown_Add( m_spellInfo, i_caster );
}

void Spell::AddStartCooldown()
{
	if( p_caster != nullptr )
		p_caster->Cooldown_AddStart( m_spellInfo );
}

void Spell::cast(bool check)
{
	if( duelSpell && (
		( p_caster != nullptr && p_caster->GetDuelState() != DUEL_STATE_STARTED ) ||
		( u_caster != nullptr && u_caster->IsPet() && static_cast< Pet* >( u_caster )->GetPetOwner() && static_cast< Pet* >( u_caster )->GetPetOwner()->GetDuelState() != DUEL_STATE_STARTED ) ) )
	{
		// Can't cast that!
		SendInterrupted( SPELL_FAILED_TARGET_FRIENDLY );
		finish();
		return;
	}

	sLog.outDebug("Spell::cast %u, Unit: %u", m_spellInfo->Id, m_caster->GetLowGUID());

	if(check)
		cancastresult = CanCast(true);
	else 
		cancastresult = SPELL_CANCAST_OK;

	if(cancastresult == SPELL_CANCAST_OK)
	{
		if (p_caster && !m_triggeredSpell && p_caster->IsInWorld() && GET_TYPE_FROM_GUID(m_targets.m_unitTarget)==HIGHGUID_TYPE_UNIT)
		{
			sQuestMgr.OnPlayerCast(p_caster,m_spellInfo->Id,m_targets.m_unitTarget);
		}
		if( m_spellInfo->Attributes & ATTRIBUTE_ON_NEXT_ATTACK )
		{
			if(!m_triggeredSpell)
			{
				// on next attack - we don't take the mana till it actually attacks.
				if(!HasPower())
				{
					SendInterrupted(SPELL_FAILED_NO_POWER);
					finish();
					return; 
				}
			}
			else
			{
				// this is the actual spell cast
				if(!TakePower())	// shouldn't happen
				{
					SendInterrupted(SPELL_FAILED_NO_POWER);
					finish();
					return;
				}
			}
		}
		else
		{
			if(!m_triggeredSpell)
			{
				if(!TakePower()) //not enough mana
				{
					//sLog.outDebug("Spell::Not Enough Mana");
					SendInterrupted(SPELL_FAILED_NO_POWER);
					finish();
					return; 
				}
			}
		}
		
		for(uint32 i=0;i<3;i++)
        {
			if( m_spellInfo->Effect[i] && m_spellInfo->Effect[i] != SPELL_EFFECT_PERSISTENT_AREA_AURA)
				 FillTargetMap(i);
        }

		SendCastResult(cancastresult);
		if(cancastresult != SPELL_CANCAST_OK)
		{
			finish();
			return;
		}

		m_isCasting = true;

		//sLog.outString( "CanCastResult: %u" , cancastresult );
		if(!m_triggeredSpell)
			AddCooldown();

		if( p_caster )
		{
			if( m_spellInfo->NameHash == SPELL_HASH_SLAM)
			{
				/* slam - reset attack timer */
				p_caster->setAttackTimer( 0, true );
				p_caster->setAttackTimer( 0, false );
			}
			if( p_caster->IsStealth() && !(m_spellInfo->AttributesEx & ATTRIBUTESEX_NOT_BREAK_STEALTH) && m_spellInfo->Id != 1 ) // <-- baaaad, baaad hackfix - for some reason some spells were triggering Spell ID #1 and stuffing up the spell system.
			{
				/* talents procing - don't remove stealth either */
				if (!(m_spellInfo->Attributes & ATTRIBUTES_PASSIVE) && 
					!( pSpellId && dbcSpell.LookupEntry(pSpellId)->Attributes & ATTRIBUTES_PASSIVE ) )
				{
					p_caster->RemoveAura(p_caster->m_stealth);
					p_caster->m_stealth = 0;
				}
			}
		}

		/*SpellExtraInfo* sp = objmgr.GetSpellExtraData(m_spellInfo->Id);
		if(sp)
		{
			Unit *Target = objmgr.GetUnit(m_targets.m_unitTarget);
			if(Target)
				Target->RemoveBySpecialType(sp->specialtype, p_caster->GetGUID());
		}*/

		if(!(m_spellInfo->Attributes & ATTRIBUTE_ON_NEXT_ATTACK  && !m_triggeredSpell))//on next attack
		{
			SendSpellGo();

			//******************** SHOOT SPELLS ***********************
			//* Flags are now 1,4,19,22 (4718610) //0x480012

			if (m_spellInfo->Flags4 & 0x8000 && m_caster->IsPlayer() && m_caster->IsInWorld())
			{
                /// Part of this function contains a hack fix
                /// hack fix for shoot spells, should be some other resource for it
                //p_caster->SendSpellCoolDown(m_spellInfo->Id, m_spellInfo->RecoveryTime ? m_spellInfo->RecoveryTime : 2300);
				WorldPacket data(SMSG_SPELL_COOLDOWN, 14);
				data << m_spellInfo->Id;
				data << p_caster->GetNewGUID();
				data << uint32(m_spellInfo->RecoveryTime ? m_spellInfo->RecoveryTime : 2300);
				p_caster->GetSession()->SendPacket(&data);
			}
			else
			{			
				if( m_spellInfo->ChannelInterruptFlags != 0 && !m_triggeredSpell)
				{
					/*
					Channeled spells are handled a little differently. The five second rule starts when the spell's channeling starts; i.e. when you pay the mana for it.
					The rule continues for at least five seconds, and longer if the spell is channeled for more than five seconds. For example,
					Mind Flay channels for 3 seconds and interrupts your regeneration for 5 seconds, while Tranquility channels for 10 seconds
					and interrupts your regeneration for the full 10 seconds.
					*/

					uint32 channelDuration = GetDuration();
					m_spellState = SPELL_STATE_CASTING;
					SendChannelStart(channelDuration);
					if( p_caster != nullptr )
					{
						//Use channel interrupt flags here
						if(m_targets.m_targetMask == TARGET_FLAG_DEST_LOCATION || m_targets.m_targetMask == TARGET_FLAG_SOURCE_LOCATION)
							u_caster->SetUInt64Value(UNIT_FIELD_CHANNEL_OBJECT, p_caster->GetSelection());					
						else if(p_caster->GetSelection() == m_caster->GetGUID())
						{
							if(p_caster->GetSummon())
								u_caster->SetUInt64Value(UNIT_FIELD_CHANNEL_OBJECT, p_caster->GetSummon()->GetGUID());
							else if(m_targets.m_unitTarget)
								u_caster->SetUInt64Value(UNIT_FIELD_CHANNEL_OBJECT, m_targets.m_unitTarget);
							else
								u_caster->SetUInt64Value(UNIT_FIELD_CHANNEL_OBJECT, p_caster->GetSelection());
						}
						else
						{
							if(p_caster->GetSelection())
								u_caster->SetUInt64Value(UNIT_FIELD_CHANNEL_OBJECT, p_caster->GetSelection());
							else if(p_caster->GetSummon())
								u_caster->SetUInt64Value(UNIT_FIELD_CHANNEL_OBJECT, p_caster->GetSummon()->GetGUID());
							else if(m_targets.m_unitTarget)
								u_caster->SetUInt64Value(UNIT_FIELD_CHANNEL_OBJECT, m_targets.m_unitTarget);
							else
							{
								m_isCasting = false;
								cancel();
								return;
							}
						}
					}
					if(u_caster && u_caster->GetPowerType()==POWER_TYPE_MANA)
					{
						if(channelDuration <= 5000)
							u_caster->DelayPowerRegeneration(5000);
						else
							u_caster->DelayPowerRegeneration(channelDuration);
					}
				}
			}

			std::vector<uint64>::iterator i, i2;
			
			// this is here to avoid double search in the unique list
			//bool canreflect = false, reflected = false;
			for(int j=0;j<3;j++)
			{
				switch(m_spellInfo->EffectImplicitTargetA[j])
				{
					case 6:
					case 22:
					case 24:
					case 25:
						SetCanReflect();
						break;
				}
				if(GetCanReflect())
					continue;
				else
					break;
			}

			if(GetCanReflect() && m_caster->IsInWorld())
			{
				for(i= UniqueTargets.begin();i != UniqueTargets.end();i++)
				{
					Unit *Target = m_caster->GetMapMgr()->GetUnit(*i);
					if(Target)
                    {
                       SetReflected(Reflect(Target));
                    }
					
                    // if the spell is reflected
					if(IsReflected())
						break;
				}
			}
			bool isDuelEffect = false;
			//uint32 spellid = m_spellInfo->Id;

			//call script before doing effects/aura
			if (m_spellScript != nullptr)
				m_spellScript->OnCast();

            // if the spell is not reflected
			if(!IsReflected())
			{
				for(uint32 x=0;x<3;x++)
				{
                    // check if we actualy have a effect
					if( m_spellInfo->Effect[x])
					{
						isDuelEffect = isDuelEffect ||  m_spellInfo->Effect[x] == SPELL_EFFECT_DUEL;
						if( m_spellInfo->Effect[x] == SPELL_EFFECT_PERSISTENT_AREA_AURA)
                        {
							HandleCastEffects(m_caster->GetGUID(),x);
                        }
						else if (m_targetUnits[x].size()>0)
						{
							for(i= m_targetUnits[x].begin();i != m_targetUnits[x].end();)
                            {
								i2 = i++;
								HandleCastEffects((*i2),x);
                            }
						}

						// Capt: The way this is done is NOT GOOD. Target code should be redone.
						else if( m_spellInfo->Effect[x] == SPELL_EFFECT_TELEPORT_UNITS)
                        {
							HandleCastEffects(m_caster->GetGUID(),x);
                        }
						else if( m_spellInfo->Effect[x] == SPELL_EFFECT_SUMMON_WILD)
                        {
							HandleCastEffects(m_caster->GetGUID(),x);
                        }
					}
				}
	
				/* don't call HandleAddAura unless we actually have auras... - Burlex*/
				if( m_spellInfo->EffectApplyAuraName[0] != 0 || m_spellInfo->EffectApplyAuraName[1] != 0 ||
				   m_spellInfo->EffectApplyAuraName[2] != 0)
				{
					hadEffect = true; // spell has had an effect (for item removal & possibly other things)

					for(i= UniqueTargets.begin();i != UniqueTargets.end();i++)
					{
						HandleAuraAdd((*i));
					}
				}
				// spells that proc on spell cast, some talents
				if(p_caster && p_caster->IsInWorld())
				{
					for(i= UniqueTargets.begin();i != UniqueTargets.end();i++)
					{
						Unit * Target = p_caster->GetMapMgr()->GetUnit((*i));

						if(!Target)
							continue; //we already made this check, so why make it again ?

						if(!m_triggeredSpell)
						{
							p_caster->HandleProc(CPROC_CAST_SPECIFIC | PROC_ON_CAST_SPELL,Target, m_spellInfo);
							p_caster->m_procCounter = 0; //this is required for to be able to count the depth of procs (though i have no idea where/why we use proc on proc)
						}

						Target->RemoveFlag(UNIT_FIELD_AURASTATE,m_spellInfo->TargetAuraState);
					}
				}
			}
			// we're much better to remove this here, because otherwise spells that change powers etc,
			// don't get applied.

			if(u_caster && !m_triggeredSpell && !m_triggeredByAura)
				u_caster->RemoveAurasByInterruptFlagButSkip(AURA_INTERRUPT_ON_CAST_SPELL, m_spellInfo->Id);

			m_isCasting = false;

			if (m_spellState != SPELL_STATE_CASTING)
				finish();
		} 
		else //this shit has nothing to do with instant, this only means it will be on NEXT melee hit
		{
			// we're much better to remove this here, because otherwise spells that change powers etc,
			// don't get applied.

			if(u_caster && !m_triggeredSpell && !m_triggeredByAura)
				u_caster->RemoveAurasByInterruptFlagButSkip(AURA_INTERRUPT_ON_CAST_SPELL, m_spellInfo->Id);

			//not sure if it must be there...
			/*if( p_caster != nullptr )
			{
				if(p_caster->m_onAutoShot)
				{
					p_caster->GetSession()->OutPacket(SMSG_CANCEL_AUTO_REPEAT);
					p_caster->GetSession()->OutPacket(SMSG_CANCEL_COMBAT);
					p_caster->m_onAutoShot = false;
				}
			}*/
			
			m_isCasting = false;
			SendCastResult(cancastresult);
			if( u_caster != nullptr )  
				u_caster->SetOnMeleeSpell(m_spellInfo->Id);

			finish();
		}
	}
	else
	{
		// cancast failed
		SendCastResult(cancastresult);
		finish();
	}
}

void Spell::AddTime(uint32 type)
{
	if(u_caster && u_caster->IsPlayer())
	{
		if( m_spellInfo->InterruptFlags & CAST_INTERRUPT_ON_DAMAGE_TAKEN)
		{
			cancel();
			return;
		}
		if( m_spellInfo->SpellGroupType && u_caster)
		{
			float ch=0;
			SM_FFValue(u_caster->SM_PNonInterrupt,&ch,m_spellInfo->SpellGroupType);
			if(Rand(ch))
				return;
		}
		if( p_caster != nullptr )
		{
			if(Rand(p_caster->SpellDelayResist[type]))
				return;
		}
		if(m_spellState==SPELL_STATE_PREPARING)
		{
			int32 delay = m_castTime/4;
			m_timer+=delay;
			if(m_timer>m_castTime)
			{
				delay -= (m_timer - m_castTime);
				m_timer=m_castTime;
				if(delay<0)
					delay = 1;
			}

			WorldPacket data(SMSG_SPELL_DELAYED, 12);
			data << u_caster->GetGUID();
			data << uint32(delay);
			u_caster->SendMessageToSet(&data, true);

			if(!p_caster)
			{
				if(m_caster->GetTypeId() == TYPEID_UNIT)
					u_caster->GetAIInterface()->AddStopTime(delay);
			}
			//in case cast is delayed, make sure we do not exit combat 
			else
			{
//				sEventMgr.ModifyEventTimeLeft(p_caster,EVENT_ATTACK_TIMEOUT,PLAYER_ATTACK_TIMEOUT_INTERVAL,true);
				// also add a new delay to offhand and main hand attacks to avoid cutting the cast short
				p_caster->delayAttackTimer(delay);
			}
		}
		else if( m_spellInfo->ChannelInterruptFlags != 48140)
		{
			int32 delay = GetDuration()/3;
			m_timer-=delay;
			if(m_timer<0)
				m_timer=0;
			else
				p_caster->delayAttackTimer(-delay);

			m_Delayed = true;
			if(m_timer>0)
				SendChannelUpdate(m_timer);

		}
	}
}

void Spell::update(uint32 difftime)
{
	// skip cast if we're more than 2/3 of the way through
	// TODO: determine which spells can be casted while moving.
	// Client knows this, so it should be easy once we find the flag.
	// XD, it's already there!
	if( ( m_spellInfo->InterruptFlags & CAST_INTERRUPT_ON_MOVEMENT ) &&
		(((float)m_castTime / 1.5f) > (float)m_timer ) && 
//		float(m_castTime)/float(m_timer) >= 2.0f		&&
		(
		m_castPositionX != m_caster->GetPositionX() ||
		m_castPositionY != m_caster->GetPositionY() ||
		m_castPositionZ != m_caster->GetPositionZ()
		)
		)
	{	
		if( u_caster != nullptr )
		{
			if(u_caster->HasNoInterrupt() == 0 && m_spellInfo->EffectMechanic[1] != 14)
			{
				cancel();
				return;
			}
		}
	}

	if(m_cancelled)
	{
		cancel();
		return;
	}

	switch(m_spellState)
	{
	case SPELL_STATE_PREPARING:
		{
			//printf("spell::update m_timer %u, difftime %d, newtime %d\n", m_timer, difftime, m_timer-difftime);
			if((int32)difftime >= m_timer)
				cast(true);
			else 
			{
				m_timer -= difftime;
				if((int32)difftime >= m_timer)
				{
					m_timer = 0;
					cast(true);
				}
			}
			
			
		} break;
	case SPELL_STATE_CASTING:
		{
			if(m_timer > 0)
			{
				if((int32)difftime >= m_timer)
					m_timer = 0;
				else
					m_timer -= difftime;
			}
			if(m_timer <= 0)
			{
				SendChannelUpdate(0);
				finish();
			}
		} break;
	}
}

void Spell::finish()
{
	sLog.outDebug("Finish the cast!");

	m_spellState = SPELL_STATE_FINISHED;
	if( u_caster != nullptr )
	{
		u_caster->m_canMove = true;
		// mana           channeled                                                     power type is mana
		if(m_usesMana && (m_spellInfo->ChannelInterruptFlags == 0 && !m_triggeredSpell) && u_caster->GetPowerType()==POWER_TYPE_MANA)
		{
			/*
			Five Second Rule
			After a character expends mana in casting a spell, the effective amount of mana gained per tick from spirit-based regeneration becomes a ratio of the normal 
			listed above, for a period of 5 seconds. During this period mana regeneration is said to be interrupted. This is commonly referred to as the five second rule. 
			By default, your interrupted mana regeneration ratio is 0%, meaning that spirit-based mana regeneration is suspended for 5 seconds after casting.
			Several effects can increase this ratio, including:
			*/

			u_caster->DelayPowerRegeneration(5000);
		}
	}
	/* Mana Regenerates while in combat but not for 5 seconds after each spell */
	/* Only if the spell uses mana, will it cause a regen delay.
	   is this correct? is there any spell that doesn't use mana that does cause a delay?
	   this is for creatures as effects like chill (when they have frost armor on) prevents regening of mana	*/
	
	//moved to spellhandler.cpp -> remove item when click on it! not when it finishes 

	//enable pvp when attacking another player with spells
	if( p_caster != nullptr )
	{
		if (m_spellInfo->Attributes & ATTRIBUTES_STOP_ATTACK && unitTarget) //fixes issues with client server sync error
		{
			p_caster->EventAttackStop();
			p_caster->smsg_AttackStop( unitTarget );
		}

		if(m_requiresCP && !GetSpellFailed())
		{
			if(p_caster->m_spellcomboPoints)
			{
				p_caster->m_comboPoints = p_caster->m_spellcomboPoints;
				p_caster->UpdateComboPoints(); //this will make sure we do not use any wrong values here
			}
			else
			{
				p_caster->NullComboPoints();
			}
		}
		if(m_Delayed)
		{
			Unit *pTarget = nullptr;
			if( p_caster->IsInWorld() )
			{
				pTarget = p_caster->GetMapMgr()->GetUnit(m_caster->GetUInt64Value(UNIT_FIELD_CHANNEL_OBJECT));
				if(!pTarget)
					pTarget = p_caster->GetMapMgr()->GetUnit(p_caster->GetSelection());
			}
			   
			if(pTarget)
			{
				pTarget->RemoveAura(m_spellInfo->Id, m_caster->GetGUID());
			}
		}
	}

	if( m_spellInfo->Effect[0] == SPELL_EFFECT_SUMMON_OBJECT ||
		m_spellInfo->Effect[1] == SPELL_EFFECT_SUMMON_OBJECT ||
		m_spellInfo->Effect[2] == SPELL_EFFECT_SUMMON_OBJECT)
		if( p_caster != nullptr )
			p_caster->SetSummonedObject(nullptr);		
	/* 
	Set cooldown on item
	*/
	if( i_caster && i_caster->GetOwner() && cancastresult == SPELL_CANCAST_OK && !GetSpellFailed() )
	{
			uint32 x;
		for(x = 0; x < 5; x++)
		{
			if(i_caster->GetProto()->Spells[x].Trigger == USE)
			{
				if(i_caster->GetProto()->Spells[x].Id)
					break;
			}
		}
		i_caster->GetOwner()->Cooldown_AddItem( i_caster->GetProto() , x );
	}
	/*
	We set current spell only if this spell has cast time or is channeling spell
	otherwise it's instant spell and we delete it right after completion
	*/
	
	if( u_caster != nullptr )
	{
		if(!m_triggeredSpell && (m_spellInfo->ChannelInterruptFlags || m_castTime>0))
 			u_caster->SetCurrentSpell(nullptr);
	}
 
	if( u_caster != nullptr && u_caster->GetCurrentSpell() == this )
		u_caster->SetCurrentSpell(nullptr); 
	
	if( p_caster )
		if( hadEffect || ( cancastresult == SPELL_CANCAST_OK && !GetSpellFailed() ) )
			RemoveItems();

	sEventMgr.AddEvent(this, &Spell::EventDelete, EVENT_UNK, 1000, 0, EVENT_FLAG_DO_NOT_EXECUTE_IN_WORLD_CONTEXT);
}

void Spell::SendCastResult(uint8 result)
{
	sLog.outDebug("SendCastResult %u", result);

	if(!m_caster->IsInWorld())
		return;

	Player * plr = p_caster;

	if(!plr && u_caster)
		plr = u_caster->m_redirectSpellPackets;
	if(!plr)
		return;

	WorldPacket data(SMSG_CAST_RESULT, 6);
    data << (uint32) m_spellInfo->Id;

	if(result != SPELL_CANCAST_OK)
	{

        data << (uint8) 2;
        data << (uint8) result;

		switch( result )
		{
			case SPELL_FAILED_REQUIRES_SPELL_FOCUS:
				data << m_spellInfo->RequiresSpellFocus;
				break;
            case SPELL_FAILED_EQUIPPED_ITEM_CLASS:
                data << uint32(m_spellInfo->EquippedItemClass);
                data << uint32(m_spellInfo->EquippedItemSubClass);
                data << uint32(m_spellInfo->RequiredItemFlags);
				break;
		}
	}
	else
		data << uint8(0);

	plr->GetSession()->SendPacket(&data);
}

// uint16 0xFFFF
enum SpellStartFlags
{
    //0x01
    SPELL_START_FLAG_DEFAULT = 0x02, // atm set as default flag
    //0x04
    //0x08
    //0x10
    SPELL_START_FLAG_RANGED = 0x20,
    //0x40
    //0x80
    //0x100
    //0x200
    //0x400
    //0x800
    //0x1000
    //0x2000
    //0x4000
    //0x8000
};

void Spell::SendSpellStart()
{
	// no need to send this on passive spells
	if( !m_caster->IsInWorld() || m_spellInfo->Attributes & 64 || m_triggeredSpell )
		return;

	sLog.outDebug("Send spell start!");

	WorldPacket data( 26 );

	uint16 cast_flags = 2;

	if( GetType() == SPELL_DMG_TYPE_RANGED )
		cast_flags |= 0x20;

    // hacky yeaaaa
    if( m_spellInfo->Id == 8326 ) // death
        cast_flags = 0x0F;

	data.SetOpcode( SMSG_SPELL_START );
	if( i_caster != nullptr )
		data << i_caster->GetNewGUID() << u_caster->GetNewGUID();
	else
		data << m_caster->GetNewGUID() << m_caster->GetNewGUID();

	data << m_spellInfo->Id;
	data << cast_flags;
	data << (uint32)m_castTime;
		
	m_targets.write( data );

	if( GetType() == SPELL_DMG_TYPE_RANGED )
	{
		ItemPrototype* ip = nullptr;
        if( m_spellInfo->Id == SPELL_RANGED_THROW ) // throw
        {
			if( p_caster != nullptr )
			{
				Item *itm = p_caster->GetItemInterface()->GetInventoryItem( EQUIPMENT_SLOT_RANGED );
				if( itm != nullptr )
				{
	                ip = itm->GetProto();
					/* Throwing Weapon Patch by Supalosa
					p_caster->GetItemInterface()->RemoveItemAmt(it->GetEntry(),1);
					(Supalosa: Instead of removing one from the stack, remove one from durability)
					We don't need to check if the durability is 0, because you can't cast the Throw spell if the thrown weapon is broken, because it returns "Requires Throwing Weapon" or something.
					*/
	
					// burlex - added a check here anyway (wpe suckers :P)
					if( itm->GetDurability() > 0 )
					{
	                    itm->SetDurability( itm->GetDurability() - 1 );
						if( itm->GetDurability() == 0 )
							p_caster->ApplyItemMods( itm, EQUIPMENT_SLOT_RANGED, false, true );
					}
				}
				else
				{
					ip = ItemPrototypeStorage.LookupEntry( 2512 );	/*rough arrow*/
				}
            }
        }
        else if( m_spellInfo->Flags4 & FLAGS4_PLAYER_RANGED_SPELLS )
        {
			if( p_caster != nullptr )
				ip = ItemPrototypeStorage.LookupEntry( p_caster->GetUInt32Value( PLAYER_AMMO_ID ) );
			else
				ip = ItemPrototypeStorage.LookupEntry( 2512 );	/*rough arrow*/
        }
		
		if( ip != nullptr )
			data << ip->DisplayInfoID << ip->InventoryType;
	}

	m_caster->SendMessageToSet( &data, true );
}

/************************************************************************/
/* General Spell Go Flags, for documentation reasons                    */
/************************************************************************/
enum SpellGoFlags
{
    //0x01
    //0x02
    //0x04
    //0x08
    //0x10
    SPELL_GO_FLAGS_RANGED           = 0x20,
    //0x40
    //0x80
    SPELL_GO_FLAGS_ITEM_CASTER      = 0x100,
    //0x200
    SPELL_GO_FLAGS_EXTRA_MESSAGE    = 0x400, //TARGET MISSES AND OTHER MESSAGES LIKE "Resist"
    //0x800
    //0x1000
    //0x2000
    //0x4000
    //0x8000
};

void Spell::SendSpellGo()
{
    sLog.outDebug("Sending SMSG_SPELL_GO id=%u", m_spellInfo->Id);

	// Fill UniqueTargets
	TargetsList::iterator i, j;
	for( uint32 x = 0; x < 3; x++ )
	{
		if( m_spellInfo->Effect[x] )
		{
            bool add = true;
			for( i = m_targetUnits[x].begin(); i != m_targetUnits[x].end(); i++ )
			{
				add = true;
				for( j = UniqueTargets.begin(); j != UniqueTargets.end(); j++ )
				{
					if( (*j) == (*i) )
					{
						add = false;
						break;
					}
				}
				if( add && (*i) != 0 )
					UniqueTargets.push_back( (*i) );
                //TargetsList::iterator itr = std::unique(m_targetUnits[x].begin(), m_targetUnits[x].end());
                //UniqueTargets.insert(UniqueTargets.begin(),));
                //UniqueTargets.insert(UniqueTargets.begin(), itr);
			}
		}
	}
	
    // no need to send this on passive spells
    if( !m_caster->IsInWorld() || m_spellInfo->Attributes & 64 )
        return;

	// Start Spell
	WorldPacket data(SMSG_SPELL_GO, 53);;
	uint16 flags = 0;

	if ( GetType() == SPELL_DMG_TYPE_RANGED )
		flags |= 0x20; // 0x20 RANGED

	if( i_caster != nullptr )
		flags |= SPELL_GO_FLAGS_ITEM_CASTER; // 0x100 ITEM CASTER

	if( ModeratedTargets.size() > 0 )
		flags |= 0x400; // 0x400 TARGET MISSES AND OTHER MESSAGES LIKE "Resist"

    // hacky..
    if( m_spellInfo->Id == 8326 ) // death
        flags = SPELL_GO_FLAGS_ITEM_CASTER | 0x0D;
    

	if( i_caster != nullptr && u_caster != nullptr ) // this is needed for correct cooldown on items
	{
		data << i_caster->GetNewGUID() << u_caster->GetNewGUID();
	} 
	else 
	{
		data << m_caster->GetNewGUID() << m_caster->GetNewGUID();
	}

	data << m_spellInfo->Id;
	data << flags;
	data << (uint8)(UniqueTargets.size()); //number of hits
	
	writeSpellGoTargets( &data );
	data << (uint8)(ModeratedTargets.size()); //number if misses
	if( flags & 0x400 )writeSpellMissedTargets( &data );
	m_targets.write( data ); // this write is included the target flag

	// er why handle it being nullptr inside if if you can't get into if if its nullptr
	if( GetType() == SPELL_DMG_TYPE_RANGED )
	{
		ItemPrototype* ip = nullptr;
		if( m_spellInfo->Id == SPELL_RANGED_THROW )
		{
			if( p_caster != nullptr )
			{
				Item* it = p_caster->GetItemInterface()->GetInventoryItem( EQUIPMENT_SLOT_RANGED );
				if( it != nullptr )
					ip = it->GetProto();
			}
			else
			{
				ip = ItemPrototypeStorage.LookupEntry(2512);	/*rough arrow*/
			}
        }
		else
		{
			if( p_caster != nullptr )
				ip = ItemPrototypeStorage.LookupEntry(p_caster->GetUInt32Value( PLAYER_AMMO_ID ) );
			else // HACK FIX
				ip = ItemPrototypeStorage.LookupEntry(2512);	/*rough arrow*/
		}
		if( ip != nullptr)
			data << ip->DisplayInfoID << ip->InventoryType;
	}
   
	m_caster->SendMessageToSet( &data, true );

	// spell log execute is still send 2.08
	// as I see with this combination, need to test it more
	//if (flags != 0x120 && m_spellInfo->Attributes & 16) // not ranged and flag 5
	  //  SendLogExecute(0,m_targets.m_unitTarget);
}

void Spell::writeSpellGoTargets( WorldPacket * data )
{
	TargetsList::iterator i;
	for ( i = UniqueTargets.begin(); i != UniqueTargets.end(); i++ )
	{
		SendCastSuccess(*i);
		*data << (*i);
	}
}

void Spell::writeSpellMissedTargets( WorldPacket * data )
{
	/*
	 * The flags at the end known to us so far are.
	 * 1 = Miss
	 * 2 = Resist
	 * 3 = Dodge // mellee only
	 * 4 = Deflect
	 * 5 = Block // mellee only
	 * 6 = Evade
	 * 7 = Immune
	 */
	SpellTargetsList::iterator i;
	if(u_caster && u_caster->isAlive())
	{
		for ( i = ModeratedTargets.begin(); i != ModeratedTargets.end(); i++ )
		{
			*data << (*i).TargetGuid;       // uint64
			*data << (*i).TargetModType;    // uint8
			///handle proc on resist spell
			Unit* target = u_caster->GetMapMgr()->GetUnit((*i).TargetGuid);
			if(target && target->isAlive())
			{
				u_caster->HandleProc(PROC_TARGET_RESIST_SPELL,target,m_spellInfo/*,damage*/);		/** Damage is uninitialized at this point - burlex */
				target->CombatStatusHandler_ResetPvPTimeout(); // aaa
				u_caster->CombatStatusHandler_ResetPvPTimeout(); // bbb
			}
		}
	}
	else
		for ( i = ModeratedTargets.begin(); i != ModeratedTargets.end(); i++ )
		{
			*data << (*i).TargetGuid;       // uint64
			*data << (*i).TargetModType;    // uint8
		}
}

void Spell::SendLogExecute(uint32 damage, uint64 & targetGuid)
{
	WorldPacket data(SMSG_SPELLLOGEXECUTE, 32);
	data << m_caster->GetNewGUID();
	data << m_spellInfo->Id;
	data << uint32(1);
	data << m_spellInfo->SpellVisual;
	data << uint32(1);
	if (m_caster->GetGUID() != targetGuid)
		data << targetGuid;
	if (damage)
		data << damage;
	m_caster->SendMessageToSet(&data,true);
}

void Spell::SendInterrupted(uint8 result)
{
	sLog.outDebug("Spell interrupted!");

	SetSpellFailed();

    WorldPacket data(SMSG_SPELL_FAILURE);
    data.append(m_caster->GetNewGUID());
    data << m_spellInfo->Id;
    data << result;
    m_caster->SendMessageToSet(&data, true);

    data.Initialize(SMSG_SPELL_FAILED_OTHER);
    data.append(m_caster->GetNewGUID());
    data << m_spellInfo->Id;
    m_caster->SendMessageToSet(&data, true);
}

void Spell::SendChannelUpdate(uint32 time)
{
	if(time == 0)
	{
		if(u_caster && u_caster->IsInWorld())
		{
			DynamicObject* dynObj=u_caster->GetMapMgr()->GetDynamicObject(u_caster->GetUInt32Value(UNIT_FIELD_CHANNEL_OBJECT));
			if(dynObj)
			{
				dynObj->m_aliveDuration = 1;
			}
			u_caster->SetUInt64Value(UNIT_FIELD_CHANNEL_OBJECT,0);
			u_caster->SetUInt32Value(UNIT_CHANNEL_SPELL,0);
		}
	}

	if (!p_caster)
		return;

	if (p_caster->GetTypeId() == TYPEID_PLAYER)
    {
		WorldPacket data(MSG_CHANNEL_UPDATE, 4);
		data << time;
		p_caster->GetSession()->SendPacket(&data);
	}

}

void Spell::SendChannelStart(uint32 duration)
{
	if (m_caster->GetTypeId() == TYPEID_PLAYER )
	{	
		// Send Channel Start
		WorldPacket data(MSG_CHANNEL_START, 4+4);
		data << m_spellInfo->Id;
		data << duration;
		((Player*)m_caster)->GetSession()->SendPacket(&data);
	}

	m_castTime = m_timer = duration;

	if( u_caster != nullptr )
		u_caster->SetUInt32Value(UNIT_CHANNEL_SPELL,m_spellInfo->Id);

	/*
	Unit* target = objmgr.GetCreature( TO_PLAYER( m_caster )->GetSelection());
	if(!target)
		target = objmgr.GetObject<Player>( TO_PLAYER( m_caster )->GetSelection());
	if(!target)
		return;
 
	m_caster->SetUInt32Value(UNIT_FIELD_CHANNEL_OBJECT,target->GetGUIDLow());
	m_caster->SetUInt32Value(UNIT_FIELD_CHANNEL_OBJECT+1,target->GetGUIDHigh());
	//disabled it can be not only creature but GO as well
	//and GO is not selectable, so this method will not work
	//these fields must be filled @ place of call
	*/
}

void Spell::SendResurrectRequest(Player* target)
{
	WorldPacket data(SMSG_RESURRECT_REQUEST, 13);
	data << m_caster->GetGUID();
	data << uint32(0) << uint8(0);

	target->GetSession()->SendPacket(&data);
}

bool Spell::HasPower()
{
	int32 powerField;
	if( u_caster != nullptr )
		if(u_caster->HasFlag(UNIT_NPC_FLAGS,UNIT_NPC_FLAG_TRAINER))
			return true;

	if(p_caster && p_caster->PowerCheat)
		return true;

	switch(m_spellInfo->powerType)
	{
	case POWER_TYPE_HEALTH:{
		powerField = UNIT_FIELD_HEALTH;
						   }break;
	case POWER_TYPE_MANA:{
		powerField = UNIT_FIELD_POWER1;
		m_usesMana=true;
						 }break;
	case POWER_TYPE_RAGE:{
		powerField = UNIT_FIELD_POWER2;
						 }break;
	case POWER_TYPE_FOCUS:{
		powerField = UNIT_FIELD_POWER3;
						  }break;
	case POWER_TYPE_ENERGY:{
		powerField = UNIT_FIELD_POWER4;
						   }break;
	default:{
		sLog.outDebug("unknown power type");
		// we should'nt be here to return
		return false;
			}break;
	}
	

	//FIXME: add handler for UNIT_FIELD_POWER_COST_MODIFIER
	//UNIT_FIELD_POWER_COST_MULTIPLIER
	if( u_caster != nullptr )
	{
		if( m_spellInfo->AttributesEx & 2 ) // Uses %100 mana
		{
			m_caster->SetUInt32Value(powerField, 0);
			return true;
		}
	}

	int32 currentPower = m_caster->GetUInt32Value(powerField);

	int32 cost;
	if( m_spellInfo->ManaCostPercentage)//Percentage spells cost % of !!!BASE!!! mana
	{
		if( m_spellInfo->powerType==POWER_TYPE_MANA)
			cost = (m_caster->GetUInt32Value(UNIT_FIELD_BASE_MANA)*m_spellInfo->ManaCostPercentage)/100;
		else
			cost = (m_caster->GetUInt32Value(UNIT_FIELD_BASE_HEALTH)*m_spellInfo->ManaCostPercentage)/100;
	}
	else 
	{
		cost = m_spellInfo->manaCost;
	}

	if((int32)m_spellInfo->powerType==POWER_TYPE_HEALTH)
		cost -= m_spellInfo->baseLevel;//FIX for life tap	
	else if( u_caster != nullptr )
	{
		if( m_spellInfo->powerType==POWER_TYPE_MANA)
			cost += u_caster->PowerCostMod[m_spellInfo->School];//this is not percent!
		else
			cost += u_caster->PowerCostMod[0];
		cost +=float2int32(cost*u_caster->GetFloatValue(UNIT_FIELD_POWER_COST_MULTIPLIER+m_spellInfo->School));
	}

	//apply modifiers
	if( m_spellInfo->SpellGroupType && u_caster)
	{
		SM_FIValue(u_caster->SM_FCost,&cost,m_spellInfo->SpellGroupType);
		SM_PIValue(u_caster->SM_PCost,&cost,m_spellInfo->SpellGroupType);
	}

	if (cost <=0)
		return true;

	//FIXME:DK:if field value < cost what happens
	if(powerField == UNIT_FIELD_HEALTH)
	{
		return true;
	}
	else
	{
		if(cost <= currentPower) // Unit has enough power (needed for creatures)
		{
			return true;
		}
		else
			return false;	 
	} 
}

bool Spell::TakePower()
{
	sLog.outDebug("Take Power!!!");

	int32 powerField;
	if( u_caster != nullptr )
	if(u_caster->HasFlag(UNIT_NPC_FLAGS,UNIT_NPC_FLAG_TRAINER))
		return true;

	if(p_caster && p_caster->PowerCheat)
		return true;

	switch(m_spellInfo->powerType)
	{
		case POWER_TYPE_HEALTH:{
			powerField = UNIT_FIELD_HEALTH;
							   }break;
		case POWER_TYPE_MANA:{
			powerField = UNIT_FIELD_POWER1;
			m_usesMana=true;
							 }break;
		case POWER_TYPE_RAGE:{
			powerField = UNIT_FIELD_POWER2;
							 }break;
		case POWER_TYPE_FOCUS:{
			powerField = UNIT_FIELD_POWER3;
							  }break;
		case POWER_TYPE_ENERGY:{
			powerField = UNIT_FIELD_POWER4;
							  }break;
		default:{
			sLog.outDebug("unknown power type");
			// we should'nt be here to return
			return false;
				}break;
	}

	//FIXME: add handler for UNIT_FIELD_POWER_COST_MODIFIER
	//UNIT_FIELD_POWER_COST_MULTIPLIER
	if( u_caster != nullptr )
	{
		if( m_spellInfo->AttributesEx & ATTRIBUTESEX_DRAIN_WHOLE_MANA ) // Uses %100 mana
		{
			m_caster->SetUInt32Value(powerField, 0);
			return true;
		}
	}
	   
	int32 currentPower = m_caster->GetUInt32Value(powerField);

	int32 cost;
	if( m_spellInfo->ManaCostPercentage)//Percentage spells cost % of !!!BASE!!! mana
	{
		if( m_spellInfo->powerType==POWER_TYPE_MANA)
			cost = (m_caster->GetUInt32Value(UNIT_FIELD_BASE_MANA)*m_spellInfo->ManaCostPercentage)/100;
		else
			cost = (m_caster->GetUInt32Value(UNIT_FIELD_BASE_HEALTH)*m_spellInfo->ManaCostPercentage)/100;
	}
	else 
	{
		cost = m_spellInfo->manaCost;
	}
	
	if((int32)m_spellInfo->powerType==POWER_TYPE_HEALTH)
			cost -= m_spellInfo->baseLevel;//FIX for life tap	
	else if( u_caster != nullptr )
	{
		if( m_spellInfo->powerType==POWER_TYPE_MANA)
			cost += u_caster->PowerCostMod[m_spellInfo->School];//this is not percent!
		else
			cost += u_caster->PowerCostMod[0];
		cost +=float2int32(cost*u_caster->GetFloatValue(UNIT_FIELD_POWER_COST_MULTIPLIER+m_spellInfo->School));
	}

	sLog.outDebug("Cost: %u", cost);

	//apply modifiers
	if( m_spellInfo->SpellGroupType && u_caster)
	{
		  SM_FIValue(u_caster->SM_FCost,&cost,m_spellInfo->SpellGroupType);
		  SM_PIValue(u_caster->SM_PCost,&cost,m_spellInfo->SpellGroupType);
	}
		 
#ifdef COLLECTION_OF_UNTESTED_STUFF_AND_TESTERS
	int spell_flat_modifers=0;
	int spell_pct_modifers=0;
	SM_FIValue(u_caster->SM_FCost,&spell_flat_modifers,m_spellInfo->SpellGroupType);
	SM_FIValue(u_caster->SM_PCost,&spell_pct_modifers,m_spellInfo->SpellGroupType);
	if(spell_flat_modifers!=0 || spell_pct_modifers!=0)
		printf("!!!!!spell cost mod flat %d , spell cost mod pct %d , spell dmg %d, spell group %u\n",spell_flat_modifers,spell_pct_modifers,cost,m_spellInfo->SpellGroupType);
#endif

	if (cost <=0)
		return true;

	//FIXME:DK:if field value < cost what happens
	if(powerField == UNIT_FIELD_HEALTH)
	{
		m_caster->DealDamage(u_caster, cost, 0, 0, 0,true);
		return true;
	}
	else
	{
		if(cost <= currentPower) // Unit has enough power (needed for creatures)
		{
			m_caster->SetUInt32Value(powerField, currentPower - cost);
			return true;
		}
		else
			return false;	 
	} 
}

void Spell::HandleCastEffects(uint64 guid, uint32 i)
{
	Object* target = nullptr;
	float dist=0.0f;

	//any no reason to do this?
	if (m_spellInfo->speed <=0)
	{
		HandleEffects(guid, i);
		return;
	}

	if(guid == m_caster->GetGUID())
 	{
		if (m_targets.m_targetMask & TARGET_FLAG_DEST_LOCATION)
			dist = m_caster->CalcDistance(m_targets.m_destX, m_targets.m_destY, m_targets.m_destZ);
		if (m_targets.m_targetMask & TARGET_FLAG_SOURCE_LOCATION)
			dist = m_caster->CalcDistance(m_targets.m_srcX, m_targets.m_srcY, m_targets.m_srcZ);
	}
	else
	{
		if(!m_caster->IsInWorld() || m_targets.m_targetMask & TARGET_FLAG_TRADE_ITEM)
		{
			HandleEffects(guid, i);
			return;
		}
		else
		{
			target = nullptr;
			switch(GET_TYPE_FROM_GUID(guid))
			{
				case HIGHGUID_TYPE_UNIT: target = m_caster->GetMapMgr()->GetCreature(GET_LOWGUID_PART(guid)); break;
				case HIGHGUID_TYPE_PET: target = m_caster->GetMapMgr()->GetPet(GET_LOWGUID_PART(guid)); break;
				case HIGHGUID_TYPE_PLAYER: target =  m_caster->GetMapMgr()->GetPlayer((uint32)guid);  break;
				case HIGHGUID_TYPE_ITEM:
					{
						if(p_caster != nullptr)
							itemTarget = p_caster->GetItemInterface()->GetItemByGUID(guid);
 
						if (itemTarget != nullptr)
							HandleEffects(guid, i);
					}break;
				case HIGHGUID_TYPE_GAMEOBJECT: target = m_caster->GetMapMgr()->GetGameObject(GET_LOWGUID_PART(guid)); break;
				case HIGHGUID_TYPE_CORPSE: target = objmgr.GetCorpse((uint32)guid); break;
			}
			
			if (target != nullptr)
				dist=m_caster->CalcDistance(target);
		}
	}

	if (dist == 0)
		HandleEffects(guid, i);
	else
	{
		float time = ((dist*1000.0f)/m_spellInfo->speed);
		if(time <= 100)
			HandleEffects(guid, i);
		else
			sEventMgr.AddEvent(this, &Spell::HandleEffects, guid, i, EVENT_SPELL_HIT, time, 1, EVENT_FLAG_DO_NOT_EXECUTE_IN_WORLD_CONTEXT);
	}
}

void Spell::HandleEffects(uint64 guid, uint32 i)
{   

	if (m_spellScript != nullptr)
		m_spellScript->OnEffect(i);

	if(guid == m_caster->GetGUID())
	{
		unitTarget = u_caster;
		gameObjTarget = g_caster;
		playerTarget = p_caster;
		itemTarget = i_caster;
	}
	else
	{
		if(!m_caster->IsInWorld())
		{
			unitTarget = 0;
			playerTarget = 0;
			itemTarget = 0;
			gameObjTarget = 0;
			corpseTarget = 0;
		}
		else if(m_targets.m_targetMask & TARGET_FLAG_TRADE_ITEM)
		{
			if( p_caster != nullptr )
			{
				Player * plr = p_caster->GetTradeTarget();
				if(plr)
					itemTarget = plr->getTradeItem((uint32)guid);
			}
		}
		else
		{
			unitTarget = nullptr;
			switch(GET_TYPE_FROM_GUID(guid))
			{
			case HIGHGUID_TYPE_UNIT:
				unitTarget = m_caster->GetMapMgr()->GetCreature(GET_LOWGUID_PART(guid));
				break;
			case HIGHGUID_TYPE_PET:
				unitTarget = m_caster->GetMapMgr()->GetPet(GET_LOWGUID_PART(guid));
				break;
			case HIGHGUID_TYPE_PLAYER:
				{
					unitTarget =  m_caster->GetMapMgr()->GetPlayer((uint32)guid);
					playerTarget = TO_PLAYER(unitTarget);
				}break;
			case HIGHGUID_TYPE_ITEM:
				if( p_caster != nullptr )
					itemTarget = p_caster->GetItemInterface()->GetItemByGUID(guid);

				break;
			case HIGHGUID_TYPE_GAMEOBJECT:
				gameObjTarget = m_caster->GetMapMgr()->GetGameObject(guid);
				break;
			case HIGHGUID_TYPE_CORPSE:
				corpseTarget = objmgr.GetCorpse((uint32)guid);
				break;
			}
		}
	}	

	damage = CalculateEffect(i,unitTarget);  
	sLog.outDebug( "WORLD: Spell effect id = %u, damage = %d", m_spellInfo->Effect[i], damage); 
	
	if( m_spellInfo->Effect[i]<TOTAL_SPELL_EFFECTS)
	{
		/*if(unitTarget && p_caster && isAttackable(p_caster,unitTarget))
			sEventMgr.ModifyEventTimeLeft(p_caster,EVENT_ATTACK_TIMEOUT,PLAYER_ATTACK_TIMEOUT_INTERVAL);*/

		(*this.*SpellEffectsHandler[m_spellInfo->Effect[i]])(i);
	}
	else
		sLog.outError("SPELL: unknown effect %u spellid %u",m_spellInfo->Effect[i], m_spellInfo->Id);
}

void Spell::HandleAuraAdd(uint64 guid)
{
	Unit * Target = 0;
	if(guid == 0)
		return;

	if(u_caster && u_caster->GetGUID() == guid)
		Target = u_caster;
	else if(m_caster->IsInWorld())
		Target = m_caster->GetMapMgr()->GetUnit(guid);

	if(!Target)
		return;

	if (m_spellInfo->speed > 0)
	{
		float dist = m_caster->CalcDistance(Target);
		float time = ((dist*1000.0f)/m_spellInfo->speed);

		if (time<=100)
			HandleAddAura(guid);
		else
			sEventMgr.AddEvent(this, &Spell::HandleAddAura, guid, EVENT_SPELL_HIT, time, 1, 0); 
	}
	else
		HandleAddAura(guid);
}

void Spell::HandleAddAura(uint64 guid)
{
	Unit * Target = 0;
	if(guid == 0)
		return;

	if(u_caster && u_caster->GetGUID() == guid)
		Target = u_caster;
	else if(m_caster->IsInWorld())
		Target = m_caster->GetMapMgr()->GetUnit(guid);

	if(!Target)
		return;

	// Applying an aura to a flagged target will cause you to get flagged.
    // self casting doesnt flag himself.
	if(Target->IsPlayer() && p_caster && p_caster != TO_PLAYER(Target))
	{
		if(TO_PLAYER(Target)->IsPvPFlagged())
			p_caster->SetPvPFlag();
	}

	// remove any auras with same type
	if( m_spellInfo->buffType > 0)
		Target->RemoveAurasByBuffType(m_spellInfo->buffType, m_caster->GetGUID(),m_spellInfo->Id);

	uint32 spellid = 0;

	if( m_spellInfo->MechanicsType == 25 && m_spellInfo->Id != 25771) // Cast spell Forbearance
		spellid = 25771;
	else if( m_spellInfo->MechanicsType == 16 && m_spellInfo->Id != 11196) // Cast spell Recently Bandaged
		spellid = 11196;
	else if( m_spellInfo->Id == 20572 || m_spellInfo->Id == 33702 || m_spellInfo->Id == 33697) // Cast spell Blood Fury
		spellid = 23230;

	if(spellid && p_caster)
	{
		SpellEntry *spellInfo = dbcSpell.LookupEntry( spellid );
		if(!spellInfo) return;
		Spell *spell = new Spell(p_caster, spellInfo ,true, nullptr);
		SpellCastTargets targets(Target->GetGUID());
		spell->prepare(&targets);	
	}

	// avoid map corruption
	if(Target->GetInstanceID()!=m_caster->GetInstanceID())
		return;

	std::map<uint32,Aura*>::iterator itr=Target->tmpAura.find(m_spellInfo->Id);
	if(itr!=Target->tmpAura.end())
	{
		if(itr->second)
		{
			if( Target->isDead() && !(m_spellInfo->Flags4 & FLAGS4_DEATH_PERSISTANT))
			{
				delete itr->second;
				Target->tmpAura.erase(itr);
				return;
			}
			/* this proc stuff has been removed because it doesn't make sense at all -BT */
			/* and it's also a very bad way of doing it */
			
			if(itr->second->GetSpellProto()->procCharges>0)
			{
				Aura *aur=nullptr;
				for(int i=0;i<itr->second->GetSpellProto()->procCharges-1;i++)
				{
					aur = new Aura(itr->second->GetSpellProto(),itr->second->GetDuration(),itr->second->GetCaster(),itr->second->GetTarget());
					
					//reference back to spellscript class
					if (m_spellScript != nullptr)
						m_spellScript->AddRef(aur);
					Target->AddAura(aur, m_spellScript);
					aur=nullptr;
				}
				if(!(itr->second->GetSpellProto()->procFlags & CPROC_REMOVEONUSE))
				{
					SpellCharge charge;
					charge.count=itr->second->GetSpellProto()->procCharges;
					charge.spellId=itr->second->GetSpellId();
					charge.ProcFlag=itr->second->GetSpellProto()->procFlags;
					charge.lastproc = 0;
					Target->m_chargeSpells.insert(make_pair(itr->second->GetSpellId(),charge));
				}
			}

			Target->AddAura(itr->second, m_spellScript); // the real spell is added last so the modifier is removed last

			Target->tmpAura.erase(itr);
		}
	}
}

/*
void Spell::TriggerSpell()
{
	if(TriggerSpellId != 0)
	{
		// check for spell id
		SpellEntry *spellInfo = sSpellStore.LookupEntry(TriggerSpellId );

		if(!spellInfo)
		{
			sLog.outError("WORLD: unknown spell id %i\n", TriggerSpellId);
			return;
		}

		Spell *spell = new Spell(m_caster, spellInfo,false, nullptr);
		WPAssert(spell);

		SpellCastTargets targets;
		if(TriggerSpellTarget)
			targets.m_unitTarget = TriggerSpellTarget;
		else
			targets.m_unitTarget = m_targets.m_unitTarget;

		spell->prepare(&targets);
	}
}*/
/*
void Spell::DetermineSkillUp()
{
	skilllinespell* skill = objmgr.GetSpellSkill(m_spellInfo->Id);
	if (m_caster->GetTypeId() == TYPEID_PLAYER)
	{
		if ((skill) && TO_PLAYER( m_caster )->HasSkillLine( skill->skilline ) )
		{
			uint32 amt = TO_PLAYER( m_caster )->GetBaseSkillAmt(skill->skilline);
			uint32 max = TO_PLAYER( m_caster )->GetSkillMax(skill->skilline);
			if (amt >= skill->grey) //grey
			{
			}
			else if ((amt >= (((skill->grey - skill->green) / 2) + skill->green)) && (amt < max))
			{
				if (rand()%100 < 33) //green
					TO_PLAYER( m_caster )->AdvanceSkillLine(skill->skilline);	

			}
			else if ((amt >= skill->green) && (amt < max))
			{
				if (rand()%100 < 66) //yellow
					TO_PLAYER( m_caster )->AdvanceSkillLine(skill->skilline);		

			}
			else 
			{
				if (amt < max) //brown
					TO_PLAYER( m_caster )->AdvanceSkillLine(skill->skilline);
			}
		}
	}
}
*/

bool Spell::IsAspect()
{
	return (
		(m_spellInfo->Id ==  2596) || (m_spellInfo->Id ==  5118) || (m_spellInfo->Id == 14320) || (m_spellInfo->Id == 13159) || (m_spellInfo->Id == 13161) || (m_spellInfo->Id == 20190) || 
		(m_spellInfo->Id == 20043) || (m_spellInfo->Id == 14322) || (m_spellInfo->Id == 14321) || (m_spellInfo->Id == 13163) || (m_spellInfo->Id == 14319) || (m_spellInfo->Id == 14318) || (m_spellInfo->Id == 13165)); 
}

bool Spell::IsSeal()
{
	return (
		(m_spellInfo->Id == 13903) || (m_spellInfo->Id == 17177) || (m_spellInfo->Id == 20154) || (m_spellInfo->Id == 20162) || (m_spellInfo->Id == 20164) || (m_spellInfo->Id == 20165) || 
		(m_spellInfo->Id == 20166) || (m_spellInfo->Id == 20287) || (m_spellInfo->Id == 20288) || (m_spellInfo->Id == 20289) || (m_spellInfo->Id == 20290) || (m_spellInfo->Id == 20291) || 
		(m_spellInfo->Id == 20292) || (m_spellInfo->Id == 20293) || (m_spellInfo->Id == 20305) || (m_spellInfo->Id == 20306) || (m_spellInfo->Id == 20307) || (m_spellInfo->Id == 20308) || 
		(m_spellInfo->Id == 20347) || (m_spellInfo->Id == 20348) || (m_spellInfo->Id == 20349) || (m_spellInfo->Id == 20356) || (m_spellInfo->Id == 20357) || (m_spellInfo->Id == 20375) || 
		(m_spellInfo->Id == 20915) || (m_spellInfo->Id == 20918) || (m_spellInfo->Id == 20919) || (m_spellInfo->Id == 20920) || (m_spellInfo->Id == 21082) || (m_spellInfo->Id == 21084)); 
}

uint8 Spell::CanCast(bool tolerate)
{
	uint32 i;
	if(objmgr.IsSpellDisabled(m_spellInfo->Id))
		return SPELL_FAILED_SPELL_UNAVAILABLE;
	if(u_caster->HasAura(5384))
		u_caster->RemoveAura(5384);

	if (m_spellScript != nullptr)
	{
		SpellCastError scriptresult=m_spellScript->CanCast(tolerate);
		
		if ((m_spellScript->flags & SSCRIPT_FLAG_COMPLETE_CHECK) || scriptresult != SPELL_CANCAST_OK)
			return scriptresult;
	}

	if( p_caster != nullptr )
	{
		//blizzard while moving, looks really sexy
		if ( p_caster->m_isMoving && m_spellInfo->ChannelInterruptFlags != 0)
			return SPELL_FAILED_MOVING;

		uint32 self_rez = p_caster->GetUInt32Value(PLAYER_SELF_RES_SPELL);
		// if theres any spells that should be cast while dead let me know
		if( !p_caster->isAlive() && self_rez != m_spellInfo->Id)
		{
			// spirit of redemption
			if( p_caster->m_canCastSpellsWhileDead )
			{
				// casting a spell on self
				if( m_targets.m_targetMask & TARGET_FLAG_SELF || m_targets.m_unitTarget == p_caster->GetGUID() ||
					!IsHealingSpell(m_spellInfo) )		// not a holy spell
				{
					return SPELL_FAILED_SPELL_UNAVAILABLE;
				}
			}
			else		// not SOR
				return SPELL_FAILED_CASTER_DEAD;
		}

		if (m_spellInfo->MechanicsType == MECHANIC_MOUNTED)
		{
				// Qiraj battletanks work everywhere on map 531
				if ( p_caster->GetMapId() == 531 && ( m_spellInfo->Id == 25953 || m_spellInfo->Id == 26054 || m_spellInfo->Id == 26055 || m_spellInfo->Id == 26056 ) )
					return SPELL_CANCAST_OK;

				if (CollideInterface.IsIndoor( p_caster->GetMapId(), p_caster->GetPositionX(), p_caster->GetPositionY(), p_caster->GetPositionZ() + 2.0f ))
					return SPELL_FAILED_NO_MOUNTS_ALLOWED;
		}
		else if( m_spellInfo->Attributes & ATTRIBUTES_ONLY_OUTDOORS )
		{
				if( CollideInterface.IsIndoor( p_caster->GetMapId(),p_caster->GetPositionX(), p_caster->GetPositionY(), p_caster->GetPositionZ() + 2.0f ) )
					return SPELL_FAILED_ONLY_OUTDOORS;
		}

		// check for cooldowns
		if(!tolerate && !p_caster->Cooldown_CanCast(m_spellInfo))
				return SPELL_FAILED_NOT_READY;

		if(p_caster->GetDuelState() == DUEL_STATE_REQUESTED)
		{
			for(i = 0; i < 3; ++i)
			{
				if( m_spellInfo->Effect[i] && m_spellInfo->Effect[i] != SPELL_EFFECT_APPLY_AURA && m_spellInfo->Effect[i] != SPELL_EFFECT_APPLY_PET_AURA
					&& m_spellInfo->Effect[i] != SPELL_EFFECT_APPLY_AREA_AURA)
				{
					return SPELL_FAILED_TARGET_DUELING;
				}
			}
		}

		// check for duel areas
		if(p_caster && m_spellInfo->Id == 7266)
		{
			AreaTable* at = dbcArea.LookupEntry( p_caster->GetAreaID() );
			if(at->AreaFlags & AREA_CITY_AREA)
				return SPELL_FAILED_NO_DUELING;
		}

		// check if spell is allowed while player is on a taxi
		if(p_caster->m_onTaxi)
		{
			if( m_spellInfo->Id != 33836) // exception for Area 52 Special
				return SPELL_FAILED_NOT_ON_TAXI;
		}

		// check if spell is allowed while player is on a transporter
		if(p_caster->m_CurrentTransporter)
		{
			// no mounts while on transporters
			if( m_spellInfo->EffectApplyAuraName[0] == SPELL_AURA_MOUNTED || m_spellInfo->EffectApplyAuraName[1] == SPELL_AURA_MOUNTED || m_spellInfo->EffectApplyAuraName[2] == SPELL_AURA_MOUNTED)
				return SPELL_FAILED_NOT_ON_TRANSPORT;
		}

		// check if spell is allowed while not mounted
		if(p_caster->IsMounted() && !(m_spellInfo->Attributes & ATTRIBUTES_MOUNT_CASTABLE))
			return SPELL_FAILED_NOT_MOUNTED;

		// check if spell is allowed while shapeshifted
		if(p_caster->GetShapeShift())
		{
			switch(p_caster->GetShapeShift())
			{
				case FORM_TREE:
				case FORM_BATTLESTANCE:
				case FORM_DEFENSIVESTANCE:
				case FORM_BERSERKERSTANCE:
				case FORM_SHADOW:
				case FORM_STEALTH:
				case FORM_MOONKIN:
				{
					break;
				}

				case FORM_SWIFT:
				case FORM_FLIGHT:
				{
					// check if item is allowed (only special items allowed in flight forms)
					if(i_caster && !(i_caster->GetProto()->Flags & ITEM_FLAG_SHAPESHIFT_OK))
						return SPELL_FAILED_NO_ITEMS_WHILE_SHAPESHIFTED;

					break;
				}

				//case FORM_CAT: 
				//case FORM_TRAVEL:
				//case FORM_AQUA:
				//case FORM_BEAR:
				//case FORM_AMBIENT:
				//case FORM_GHOUL:
				//case FORM_DIREBEAR:
				//case FORM_CREATUREBEAR:
				//case FORM_GHOSTWOLF:
				//case FORM_SPIRITOFREDEMPTION:

				default:
				{
					// check if item is allowed (only special & equipped items allowed in other forms)
					if(i_caster && !(i_caster->GetProto()->Flags & ITEM_FLAG_SHAPESHIFT_OK))
						if(i_caster->GetProto()->InventoryType == INVTYPE_NON_EQUIP)
							return SPELL_FAILED_NO_ITEMS_WHILE_SHAPESHIFTED;
				}
			}
		}

		// item spell checks
		if(i_caster && i_caster->GetProto())
		{
			if( i_caster->GetProto()->ZoneNameID && i_caster->GetProto()->ZoneNameID != i_caster->GetZoneId() ) 
				return SPELL_FAILED_NOT_HERE;
			if( i_caster->GetProto()->MapID && i_caster->GetProto()->MapID != i_caster->GetMapId() )
				return SPELL_FAILED_NOT_HERE;

			if(i_caster->GetProto()->Spells[0].Charges != 0)
			{
				// check if the item has the required charges
				if(i_caster->GetUInt32Value(ITEM_FIELD_SPELL_CHARGES) == 0)
					return SPELL_FAILED_NO_CHARGES_REMAIN;

				// for items that combine to create a new item, check if we have the required quantity of the item
				if(i_caster->GetProto()->ItemId == m_spellInfo->Reagent[0])
					if(p_caster->GetItemInterface()->GetItemCount(m_spellInfo->Reagent[0]) < 1 + m_spellInfo->ReagentCount[0])
						return SPELL_FAILED_ITEM_GONE;
			}
		}

		else if( i_caster && !i_caster->GetProto() )
		{
			// how the hell does this happen?
			return SPELL_FAILED_ITEM_GONE;
		}

		// check if we have the required reagents
		for(i=0; i<8 ;i++)
		{
			if( m_spellInfo->Reagent[i] == 0 || m_spellInfo->ReagentCount[i] == 0)
				continue;

			if(p_caster->GetItemInterface()->GetItemCount(m_spellInfo->Reagent[i]) < m_spellInfo->ReagentCount[i])
				return SPELL_FAILED_ITEM_GONE;
		}

		// check if we have the required tools, totems, etc
		if( m_spellInfo->Totem[0] != 0)
		{
			if(!p_caster->GetItemInterface()->GetItemCount(m_spellInfo->Totem[0]))
				return SPELL_FAILED_TOTEMS;
		}
		if( m_spellInfo->Totem[1] != 0)
		{
			if(!p_caster->GetItemInterface()->GetItemCount(m_spellInfo->Totem[1]))
				return SPELL_FAILED_TOTEMS;
		}

		// check if we have the required gameobject focus
		float focusRange;

		if( m_spellInfo->RequiresSpellFocus)
		{
			bool found = false;

            for(std::set<Object*>::iterator itr = p_caster->GetInRangeSetBegin(); itr != p_caster->GetInRangeSetEnd(); itr++ )
			{
				if((*itr)->GetTypeId() != TYPEID_GAMEOBJECT)
					continue;

				if((*itr)->GetUInt32Value(GAMEOBJECT_TYPE_ID) != GAMEOBJECT_TYPE_SPELL_FOCUS)
					continue;

				GameObjectInfo *info = ((GameObject*)(*itr))->GetInfo();
				if(!info)
				{
					sLog.outDebug("Warning: could not find info about game object %u",(*itr)->GetEntry());
					continue;
				}

				// professions use rangeIndex 1, which is 0yds, so we will use 5yds, which is standard interaction range.
				if(info->sound1)
					focusRange = float(info->sound1);
				else
					focusRange = GetMaxRange(dbcSpellRange.LookupEntry(m_spellInfo->rangeIndex));

				// check if focus object is close enough
				if(!IsInrange(p_caster->GetPositionX(), p_caster->GetPositionY(), p_caster->GetPositionZ(), (*itr), (focusRange * focusRange)))
					continue;

				if(info->SpellFocus == m_spellInfo->RequiresSpellFocus)
				{
					found = true;
					break;
				}
			}

			if(!found)
				return SPELL_FAILED_REQUIRES_SPELL_FOCUS;
		}

/*		if (m_spellInfo->RequiresAreaId && m_spellInfo->RequiresAreaId != p_caster->GetMapMgr()->GetAreaID(p_caster->GetPositionX(),p_caster->GetPositionY()))
			return SPELL_FAILED_REQUIRES_AREA;*/

		// aurastate check
		if( m_spellInfo->CasterAuraState )
		{
			if( !p_caster->HasFlag( UNIT_FIELD_AURASTATE, m_spellInfo->CasterAuraState ) )
				return SPELL_FAILED_CASTER_AURASTATE;
		}
	}

	// Targetted Item Checks
	if(m_targets.m_itemTarget && p_caster)
	{
		Item *i_target = nullptr;

		// check if the targeted item is in the trade box
		if( m_targets.m_targetMask & TARGET_FLAG_TRADE_ITEM )
		{
			switch( m_spellInfo->Effect[0] )
			{
				// only lockpicking and enchanting can target items in the trade box
				case SPELL_EFFECT_OPEN_LOCK:
				case SPELL_EFFECT_ENCHANT_ITEM:
				case SPELL_EFFECT_ENCHANT_ITEM_TEMPORARY:
				{
					// check for enchants that can only be done on your own items
					if( m_spellInfo->Flags3 & FLAGS3_ENCHANT_OWN_ONLY )
						return SPELL_FAILED_BAD_TARGETS;

					// get the player we are trading with
					Player* t_player = p_caster->GetTradeTarget();
					// get the targeted trade item
					if( t_player != nullptr )
						i_target = t_player->getTradeItem((uint32)m_targets.m_itemTarget);
				}
			}
		}
		// targeted item is not in a trade box, so get our own item
		else
		{
			i_target = p_caster->GetItemInterface()->GetItemByGUID( m_targets.m_itemTarget );
		}

		// check to make sure we have a targeted item
		if( i_target == nullptr )
			return SPELL_FAILED_BAD_TARGETS;

		ItemPrototype* proto = i_target->GetProto();

		// check to make sure we have it's prototype info
		if(!proto) return SPELL_FAILED_BAD_TARGETS;

		// check to make sure the targeted item is acceptable
		switch(m_spellInfo->Effect[0])
		{
			// Lock Picking Targeted Item Check
			case SPELL_EFFECT_OPEN_LOCK:
			{
				// this is currently being handled in SpellEffects
				break;
			}

			// Enchanting Targeted Item Check
			case SPELL_EFFECT_ENCHANT_ITEM:
			case SPELL_EFFECT_ENCHANT_ITEM_TEMPORARY:
			{
				// check if we have the correct class, subclass, and inventory type of target item
				//[INFINITE] might need to setup a workaround for this using db data cause it's not in the dbc files

				//if( m_spellInfo->EquippedItemClass != (int32)proto->Class)
					//return SPELL_FAILED_BAD_TARGETS;

				if( m_spellInfo->EquippedItemSubClass && !(m_spellInfo->EquippedItemSubClass & (1 << proto->SubClass)))
					return SPELL_FAILED_BAD_TARGETS;

				if( m_spellInfo->RequiredItemFlags && !(m_spellInfo->RequiredItemFlags & (1 << proto->InventoryType)))
					return SPELL_FAILED_BAD_TARGETS;

				if (m_spellInfo->Effect[0] == SPELL_EFFECT_ENCHANT_ITEM && 
					m_spellInfo->baseLevel && (m_spellInfo->baseLevel > proto->ItemLevel))
					return int8(SPELL_FAILED_BAD_TARGETS); // maybe there is different err code
				
				if( i_caster && i_caster->GetProto()->Flags == 2097216)
					break;

				if( m_spellInfo->Flags3 & FLAGS3_ENCHANT_OWN_ONLY && !(i_target->IsSoulbound()))
					return SPELL_FAILED_BAD_TARGETS;

				break;
			}

			// Disenchanting Targeted Item Check
			case SPELL_EFFECT_DISENCHANT:
			{
				// check if item can be disenchanted
				if(proto->DisenchantReqSkill < 1)
					return SPELL_FAILED_CANT_BE_DISENCHANTED;

				// check if we have high enough skill
				if((int32)p_caster->_GetSkillLineCurrent(SKILL_ENCHANTING) < proto->DisenchantReqSkill)
					return SPELL_FAILED_ERROR; //return SPELL_FAILED_CANT_BE_DISENCHANTED_SKILL;

				break;
			}

			// Feed Pet Targeted Item Check
			case SPELL_EFFECT_FEED_PET:
			{
				Pet *pPet = p_caster->GetSummon();

				// check if we have a pet
				if(!pPet)
					return SPELL_FAILED_NO_PET;

				// check if item is food
				if(!proto->FoodType)
					return SPELL_FAILED_BAD_TARGETS;

				// check if food type matches pets diet
				if(!(pPet->GetPetDiet() & (1 << (proto->FoodType - 1))))
					return SPELL_FAILED_WRONG_PET_FOOD;

				// check food level: food should be max 30 lvls below pets level
				if(pPet->getLevel() > proto->ItemLevel + 30)
					return SPELL_FAILED_FOOD_LOWLEVEL;

				break;
			}
		}
	}

	// set up our max Range
	float maxRange = GetMaxRange( dbcSpellRange.LookupEntry( m_spellInfo->rangeIndex ) );
		// latency compensation!!
		// figure out how much extra distance we need to allow for based on our movespeed and latency.
		if( u_caster && m_targets.m_unitTarget )
		{
			Unit * utarget;
			utarget = m_caster->GetMapMgr()->GetUnit( m_targets.m_unitTarget );
			if(utarget && utarget->IsPlayer() && TO_PLAYER( utarget )->m_isMoving )
				{
					// this only applies to PvP.
					uint32 lat = TO_PLAYER( utarget )->GetSession() ? TO_PLAYER( utarget )->GetSession()->GetLatency() : 0;

					// if we're over 500 get fucked anyway.. your gonna lag! and this stops cheaters too
					lat = ( lat > 500 ) ? 500 : lat;

					// calculate the added distance
					maxRange += ( u_caster->m_runSpeed * 0.001f ) * float( lat );
				}
		}
		if( p_caster && p_caster->m_isMoving )
		{
			// this only applies to PvP.
			uint32 lat = p_caster->GetSession() ? p_caster->GetSession()->GetLatency() : 0;

			// if we're over 500 get fucked anyway.. your gonna lag! and this stops cheaters too
			lat = ( lat > 500) ? 500 : lat;

			// calculate the added distance
			maxRange += ( u_caster->m_runSpeed * 0.001f ) * float( lat );
		}
	if( m_spellInfo->SpellGroupType && u_caster != nullptr )
	{
		SM_FFValue( u_caster->SM_FRange, &maxRange, m_spellInfo->SpellGroupType );
		SM_PFValue( u_caster->SM_PRange, &maxRange, m_spellInfo->SpellGroupType );
	}

	// Targeted Location Checks (AoE spells)
	if( m_targets.m_targetMask == TARGET_FLAG_DEST_LOCATION )
	{
		if( !IsInrange( m_targets.m_destX, m_targets.m_destY, m_targets.m_destZ, m_caster, ( maxRange * maxRange ) ) )
			return SPELL_FAILED_OUT_OF_RANGE;
	}

	Unit *target = nullptr;

	// Targeted Unit Checks
	if(m_targets.m_unitTarget)
	{
		target = (m_caster->IsInWorld()) ? m_caster->GetMapMgr()->GetUnit(m_targets.m_unitTarget) : nullptr;

		if(target)
		{
			// Partha: +2.52yds to max range, this matches the range the client is calculating.
			// see extra/supalosa_range_research.txt for more info

			if( tolerate ) // add an extra 33% to range on final check (squared = 1.78x)
			{
				if( !IsInrange( m_caster->GetPositionX(), m_caster->GetPositionY(), m_caster->GetPositionZ(), target, ( ( maxRange + 2.52f ) * ( maxRange + 2.52f ) * 1.78f ) ) )
					return SPELL_FAILED_OUT_OF_RANGE;
			}
			else
			{
				if( !IsInrange( m_caster->GetPositionX(), m_caster->GetPositionY(), m_caster->GetPositionZ(), target, ( ( maxRange + 2.52f ) * ( maxRange + 2.52f ) ) ) )
					return SPELL_FAILED_OUT_OF_RANGE;
			}

			if( p_caster != nullptr )
			{
				if( m_spellInfo->Id == SPELL_RANGED_THROW)
				{
					Item * itm = p_caster->GetItemInterface()->GetInventoryItem(EQUIPMENT_SLOT_RANGED);
					if(!itm || itm->GetDurability() == 0)
						return SPELL_FAILED_NO_AMMO;
				}

				if ( target != m_caster && !m_caster->IsInLineOfSight(target) )
 				{
					return SPELL_FAILED_LINE_OF_SIGHT;
				}

				// check aurastate
				if( m_spellInfo->TargetAuraState )
				{
					if( !target->HasFlag( UNIT_FIELD_AURASTATE, m_spellInfo->TargetAuraState ) )
					{
						return SPELL_FAILED_TARGET_AURASTATE;
					}
				}

				if(target->IsPlayer())
				{
					// disallow spell casting in sanctuary zones
					// allow attacks in duels
					if( p_caster->DuelingWith != target && !isFriendly( p_caster, target ) )
					{
						AreaTable* atCaster = dbcArea.LookupEntry( p_caster->GetAreaID() );
						AreaTable* atTarget = dbcArea.LookupEntry( TO_PLAYER( target )->GetAreaID() );
						if( atCaster->AreaFlags & 0x800 || atTarget->AreaFlags & 0x800 )
							return SPELL_FAILED_NOT_HERE;
					}
				}
				
				// pet training
				if( m_spellInfo->EffectImplicitTargetA[0] == EFF_TARGET_PET &&
					m_spellInfo->Effect[0] == SPELL_EFFECT_LEARN_SPELL )
				{
					Pet *pPet = p_caster->GetSummon();
					// check if we have a pet
					if( pPet == nullptr )
						return SPELL_FAILED_NO_PET;

					// other checks
					SpellEntry* trig = dbcSpell.LookupEntry( m_spellInfo->EffectTriggerSpell[0] );
					if( trig == nullptr )
						return SPELL_FAILED_SPELL_UNAVAILABLE;

					uint32 status = pPet->CanLearnSpell( trig );
					if( status != 0 )
						return status;
				}
			}

			// if the target is not the unit caster and not the masters pet
			if(target != u_caster && !m_caster->IsPet())
			{
				// Dummy spells check
				if( m_spellInfo->Id == 4130)// Banish Burning Exile
				{
					if(target->GetEntry()!= 2760) // target needs to be a Burning Exile
						return SPELL_FAILED_BAD_TARGETS;
				}
				if( m_spellInfo->Id == 4131)// Banish Cresting Exile
				{
					if(target->GetEntry()!= 2761) // target needs to be a Cresting Exile
						return SPELL_FAILED_BAD_TARGETS;
				}
				if( m_spellInfo->Id == 4132)// Banish Thundering Exile
				{
					if(target->GetEntry()!= 2762) // target needs to be a Thundering Exile
						return SPELL_FAILED_BAD_TARGETS;
				}

				/***********************************************************
				* Inface checks, these are checked in 2 ways
				* 1e way is check for damage type, as 3 is always ranged
				* 2e way is trough the data in the extraspell db
				*
				**********************************************************/

				/* burlex: units are always facing the target! */
				if(p_caster && !(m_spellInfo->buffType & SPELL_TYPE_CURSE))
				{
					if( m_spellInfo->Spell_Dmg_Type == SPELL_DMG_TYPE_RANGED )
					{ // our spell is a ranged spell
						if(!p_caster->isInFront(target))
							return SPELL_FAILED_UNIT_NOT_INFRONT;
					}
					else
					{ // our spell is not a ranged spell
						if( /* GetType() == SPELL_DMG_TYPE_MAGIC && */ m_spellInfo->in_front_status == 1 ) //why an extra check?
						{
							// must be in front
							if(!u_caster->isInFront(target))
								return SPELL_FAILED_UNIT_NOT_INFRONT;
						}
						else if( m_spellInfo->in_front_status == 2)
						{
							// behind
							if(target->isInFront(u_caster))
								return SPELL_FAILED_NOT_BEHIND;
						}
					}
				}
			}

			// if target is already skinned, don't let it be skinned again
			if( m_spellInfo->Effect[0] == SPELL_EFFECT_SKINNING) // skinning
				if(target->IsUnit() && (((Creature*)target)->Skinned) )
					return SPELL_FAILED_TARGET_UNSKINNABLE;

			// all spells with target 61 need to be in group or raid
			// TODO: need to research this if its not handled by the client!!!
			if(
				m_spellInfo->EffectImplicitTargetA[0] == 61 || 
				m_spellInfo->EffectImplicitTargetA[1] == 61 || 
				m_spellInfo->EffectImplicitTargetA[2] == 61)
			{
				if( target->IsPlayer() && !TO_PLAYER( target )->InGroup() )
					return SPELL_FAILED_NOT_READY;//return SPELL_FAILED_TARGET_NOT_IN_PARTY or SPELL_FAILED_TARGET_NOT_IN_PARTY;
			}

			// pet's owner stuff
			/*if (m_spellInfo->EffectImplicitTargetA[0] == 27 || 
				m_spellInfo->EffectImplicitTargetA[1] == 27 || 
				m_spellInfo->EffectImplicitTargetA[2] == 27)
			{
				if (!target->IsPlayer())
					return SPELL_FAILED_TARGET_NOT_PLAYER; //if you are there something is very wrong
			}*/

			// target 39 is fishing, all fishing spells are handled
			if( m_spellInfo->EffectImplicitTargetA[0] == 39 )//|| 
			 //m_spellInfo->EffectImplicitTargetA[1] == 39 || 
			 //m_spellInfo->EffectImplicitTargetA[2] == 39)
			{
				uint32 entry = m_spellInfo->EffectMiscValue[0];
				if(entry == GO_FISHING_BOBBER)
				{
					//uint32 mapid = p_caster->GetMapId();
					float px=u_caster->GetPositionX();
					float py=u_caster->GetPositionY();
					//float pz=u_caster->GetPositionZ();
					float orient = m_caster->GetOrientation();
					float posx = 0,posy = 0,posz = 0;
					float co = cos(orient);
					float si = sin(orient);
					MapMgr * map = m_caster->GetMapMgr(); 

					float r;
					for(r=20; r>10; r--)
					{
						posx = px + r * co;
						posy = py + r * si;
						/*if(!(map->GetWaterType(posx,posy) & 1))//water 
							continue;*/
						posz = map->GetLiquidHeight(posx,posy);
						if(posz > map->GetLandHeight(posx,posy))//water
							break;
					}
					if(r<=10)
						return SPELL_FAILED_NOT_FISHABLE;

					// if we are already fishing, dont cast it again
					if(p_caster->GetSummonedObject())
						if(p_caster->GetSummonedObject()->GetEntry() == GO_FISHING_BOBBER)
							return SPELL_FAILED_SPELL_IN_PROGRESS;
				}
			}

			if( p_caster != nullptr )
			{
				if( m_spellInfo->NameHash == SPELL_HASH_GOUGE )// Gouge
					if(!target->isInFront(p_caster))
						return SPELL_FAILED_NOT_INFRONT;

				if( m_spellInfo->Category==1131)//Hammer of wrath, requires target to have 20- % of hp
				{
					if(target->GetUInt32Value(UNIT_FIELD_HEALTH) == 0)
						return SPELL_FAILED_BAD_TARGETS;

					if(target->GetUInt32Value(UNIT_FIELD_MAXHEALTH)/target->GetUInt32Value(UNIT_FIELD_HEALTH)<5)
						 return SPELL_FAILED_BAD_TARGETS;
				}
				else if( m_spellInfo->Category==672)//Conflagrate, requires immolation spell on victim
				{
					if(!target->HasAuraVisual(46))
						return SPELL_FAILED_BAD_TARGETS;
				}

				if(target->dispels[m_spellInfo->DispelType])
					return SPELL_FAILED_PREVENTED_BY_MECHANIC-1;			// hackfix - burlex
				
				// Removed by Supalosa and moved to 'completed cast'
				//if(target->MechanicsDispels[m_spellInfo->MechanicsType])
				//	return SPELL_FAILED_PREVENTED_BY_MECHANIC-1; // Why not just use 	SPELL_FAILED_DAMAGE_IMMUNE                                   = 144,
			}

			/*// if we're replacing a higher rank, deny it
			AuraCheckResponse acr = target->AuraCheck(m_spellInfo->NameHash, m_spellInfo->RankNumber,m_caster);
			if( acr.Error == AURA_CHECK_RESULT_HIGHER_BUFF_PRESENT )
				return SPELL_FAILED_AURA_BOUNCED;*/

			//check if we are trying to stealth or turn invisible but it is not allowed right now
			if( IsStealthSpell() || IsInvisibilitySpell() )
			{
				//if we have Faerie Fire, we cannot stealth or turn invisible
				if( u_caster->HasNegativeAuraWithNameHash( SPELL_HASH_FAERIE_FIRE ) || u_caster->HasNegativeAuraWithNameHash( SPELL_HASH_FAERIE_FIRE__FERAL_ ) )
					return SPELL_FAILED_SPELL_UNAVAILABLE;
			}

			/*SpellReplacement*rp=objmgr.GetReplacingSpells(m_spellInfo->Id);
			if(rp)
			{
				if(isAttackable(u_caster,target))//negative, replace only our own spell
				{
					for(uint32 x=0;x<rp->count;x++)
					{
						if(target->HasActiveAura(rp->spells[x],m_caster->GetGUID()))
						{
							return SPELL_FAILED_AURA_BOUNCED;
						}
					}
				}
				else
				{
					for(uint32 x=0;x<rp->count;x++)
					{
						if(target->HasActiveAura(rp->spells[x]))
						{
							return SPELL_FAILED_AURA_BOUNCED;
						}
					}
				}
			}	*/		
		}
	}	

	//Checking for Debuffs that dont allow power word:shield, those Pala spells, ice block or use first aid, hacky, is there any way to check if he has "immune mechanic"?
	if (m_spellInfo->MechanicsType == 25 && ((target) ? target->HasAura(25771) : u_caster->HasAura(25771))) //Forbearance
		return SPELL_FAILED_DAMAGE_IMMUNE;
	if (m_spellInfo->MechanicsType == 16 && ((target) ? target->HasAura(11196) : u_caster->HasAura(11196)))
		return SPELL_FAILED_DAMAGE_IMMUNE;

	// Special State Checks (for creatures & players)
	if( u_caster )
	{
		if( u_caster->SchoolCastPrevent[m_spellInfo->School] ) 
		{	
			uint32 now_ = getMSTime();
			if( now_ > u_caster->SchoolCastPrevent[m_spellInfo->School] )//this limit has expired,remove
				u_caster->SchoolCastPrevent[m_spellInfo->School] = 0;
			else 
			{
				// HACK FIX
				switch( m_spellInfo->NameHash )
				{
					// This is actually incorrect. school lockouts take precedence over silence.
					// So ice block/divine shield are not usable while their schools are locked out,
					// but can be used while silenced.
					/*case SPELL_HASH_ICE_BLOCK: //Ice Block
					case 0x9840A1A6: //Divine Shield
						break;
					*/
					case SPELL_HASH_WILL_OF_THE_FORSAKEN: //Will of the Forsaken
						{
							if( u_caster->m_special_state & ( UNIT_STATE_FEAR | UNIT_STATE_CHARM | UNIT_STATE_SLEEP ) )
								break;
						}break;

					case SPELL_HASH_DEATH_WISH: //Death Wish
					case SPELL_HASH_FEAR_WARD: //Fear Ward
					case SPELL_HASH_BERSERKER_RAGE: //Berserker Rage
						{
							if( u_caster->m_special_state & UNIT_STATE_FEAR )
								break;
						}break;

						{
							if( u_caster->m_special_state & ( UNIT_STATE_FEAR | UNIT_STATE_CHARM | UNIT_STATE_SLEEP | UNIT_STATE_ROOT | UNIT_STATE_STUN | UNIT_STATE_CONFUSE | UNIT_STATE_SNARE ) )
								break;
						}
							break;

					case SPELL_HASH_BARKSKIN: // Barksin
					{ // This spell is usable while stunned, frozen, incapacitated, feared or asleep.  Lasts 12 sec.
						if( u_caster->m_special_state & ( UNIT_STATE_STUN | UNIT_STATE_FEAR | UNIT_STATE_SLEEP ) ) // Uh, what unit_state is Frozen? (freezing trap...)
							break;
					}

					default:
						return SPELL_FAILED_SILENCED;
				}
			}
		}

		if( u_caster->m_auracount[SPELL_AURA_MOD_SILENCE] && m_spellInfo->School != NORMAL_DAMAGE )// can only silence non-physical
		{
			switch( m_spellInfo->NameHash )
			{
				case SPELL_HASH_ICE_BLOCK: //Ice Block
				case SPELL_HASH_DIVINE_SHIELD: //Divine Shield
				break;

				default:
				return SPELL_FAILED_SILENCED;
			}
		}

		if(target) /* -Supalosa- Shouldn't this be handled on Spell Apply? */
		{
			for( int i = 0; i < 3; i++ ) // if is going to cast a spell that breaks stun remove stun auras, looks a bit hacky but is the best way i can find
			{
				if( m_spellInfo->EffectApplyAuraName[i] == SPELL_AURA_MECHANIC_IMMUNITY )
				{
					target->RemoveAllAurasByMechanic( m_spellInfo->EffectMiscValue[i] , -1 , true );
					// Remove all debuffs of that mechanic type.
					// This is also done in SpellAuras.cpp - wtf?
				}
				/*
				if( m_spellInfo->EffectApplyAuraName[i] == SPELL_AURA_MECHANIC_IMMUNITY && (m_spellInfo->EffectMiscValue[i] == 12 || m_spellInfo->EffectMiscValue[i] == 17))
				{
					for(uint32 x=MAX_POSITIVE_AURAS;x<MAX_AURAS;x++)
						if(target->m_auras[x])
							if(target->m_auras[x]->GetSpellProto()->MechanicsType == m_spellInfo->EffectMiscValue[i])
								target->m_auras[x]->Remove();
				}
				*/
			}
		}

		if( u_caster->IsPacified() && m_spellInfo->School == NORMAL_DAMAGE ) // only affects physical damage
		{
			// HACK FIX
			switch( m_spellInfo->NameHash )
			{
				case SPELL_HASH_ICE_BLOCK: //Ice Block
				case SPELL_HASH_DIVINE_SHIELD: //Divine Shield
				case SPELL_HASH_WILL_OF_THE_FORSAKEN: //Will of the Forsaken
				{
					if( u_caster->m_special_state & (UNIT_STATE_FEAR | UNIT_STATE_CHARM | UNIT_STATE_SLEEP))
						break;
				}break;

				default:
					return SPELL_FAILED_PACIFIED;
			}
		}

		if( u_caster->IsStunned() || u_caster->IsFeared())
		{
			// HACK FIX
			switch( m_spellInfo->NameHash )
			{
				case SPELL_HASH_ICE_BLOCK: //Ice Block
				case SPELL_HASH_DIVINE_SHIELD: //Divine Shield
				case SPELL_HASH_DIVINE_PROTECTION: //Divine Protection
				case SPELL_HASH_BARKSKIN: //Barkskin
					break;
				/* -Supalosa- For some reason, being charmed or sleep'd is counted as 'Stunned'. 
				Check it: http://www.wowhead.com/?spell=700 */

				case SPELL_HASH_WILL_OF_THE_FORSAKEN: /* Will of the Forsaken (Undead Racial) */
					break;

				case SPELL_HASH_BERSERKER_RAGE://Berserker Rage frees the caster from fear effects.
					{
						if( u_caster->IsStunned() )
							return SPELL_FAILED_STUNNED;
					}break;
					
				case SPELL_HASH_BLINK:
					break;

				default:
					return SPELL_FAILED_STUNNED;
			}
		}

		if(u_caster->GetUInt64Value(UNIT_FIELD_CHANNEL_OBJECT) > 0)
		{
			SpellEntry * t_spellInfo = (u_caster->GetCurrentSpell() ? u_caster->GetCurrentSpell()->m_spellInfo : nullptr);

			if(!t_spellInfo || !m_triggeredSpell)
				return SPELL_FAILED_SPELL_IN_PROGRESS;
			else if (t_spellInfo)
			{
				if(
					t_spellInfo->EffectTriggerSpell[0] != m_spellInfo->Id &&
					t_spellInfo->EffectTriggerSpell[1] != m_spellInfo->Id &&
					t_spellInfo->EffectTriggerSpell[2] != m_spellInfo->Id)
				{
					return SPELL_FAILED_SPELL_IN_PROGRESS;
				}
			}
		}
	}

	// no problems found, so we must be ok
	return SPELL_CANCAST_OK;
}

void Spell::RemoveItems()
{
	// Item Charges & Used Item Removal
	if(i_caster)
	{
		// Stackable Item -> remove 1 from stack
		if(i_caster->GetUInt32Value(ITEM_FIELD_STACK_COUNT) > 1)
		{
			i_caster->ModUnsigned32Value(ITEM_FIELD_STACK_COUNT, -1);
			i_caster->m_isDirty = true;
		}
		// Expendable Item
		else if(i_caster->GetProto()->Spells[0].Charges < 0
		     || i_caster->GetProto()->Spells[1].Charges == -1) // hackfix for healthstones/mana gems/depleted items
		{
			// if item has charges remaining -> remove 1 charge
			if(((int32)i_caster->GetUInt32Value(ITEM_FIELD_SPELL_CHARGES)) < -1)
			{
				i_caster->ModUnsigned32Value(ITEM_FIELD_SPELL_CHARGES, 1);
				i_caster->m_isDirty = true;
			}
			// if item has no charges remaining -> delete item
			else
			{
				i_caster->GetOwner()->GetItemInterface()->SafeFullRemoveItemByGuid(i_caster->GetGUID());
				i_caster = nullptr;
			}
		}
		// Non-Expendable Item -> remove 1 charge
		else if(i_caster->GetProto()->Spells[0].Charges > 0)
		{
			i_caster->ModUnsigned32Value(ITEM_FIELD_SPELL_CHARGES, -1);
			i_caster->m_isDirty = true;
		}
	} 

	// Ammo Removal
	if( m_spellInfo->Flags3 == FLAGS3_REQ_RANGED_WEAPON || m_spellInfo->Flags4 & FLAGS4_PLAYER_RANGED_SPELLS)
	{
		p_caster->GetItemInterface()->RemoveItemAmt_ProtectPointer(p_caster->GetUInt32Value(PLAYER_AMMO_ID), 1, &i_caster);
	}

	// Reagent Removal
	for(uint32 i=0; i<8 ;i++)
	{
		if( m_spellInfo->Reagent[i])
		{
			p_caster->GetItemInterface()->RemoveItemAmt_ProtectPointer(m_spellInfo->Reagent[i], m_spellInfo->ReagentCount[i], &i_caster);
		}
	}
}

// Add ARMOR CHECKS; Add npc that have ranged weapons use them.
int32 Spell::CalculateEffect(uint32 i,Unit *target)
{
	int32 value = 0;

	float basePointsPerLevel    = m_spellInfo->EffectRealPointsPerLevel[i];
	float randomPointsPerLevel  = m_spellInfo->EffectDicePerLevel[i];
	int32 basePoints = m_spellInfo->EffectBasePoints[i] + 1;
	int32 randomPoints = m_spellInfo->EffectDieSides[i];

	//added by Zack : some talents inherit their basepoints from the previously casted spell: see mage - Master of Elements
	if(forced_basepoints[i])
		basePoints = forced_basepoints[i];

	if( u_caster != nullptr )
	{
		int32 diff = -(int32)m_spellInfo->baseLevel;
		if (m_spellInfo->maxLevel && u_caster->getLevel()>m_spellInfo->maxLevel)
			diff +=m_spellInfo->maxLevel;
		else
			diff +=u_caster->getLevel();
		randomPoints += float2int32(diff * randomPointsPerLevel);
		basePoints += float2int32(diff * basePointsPerLevel );
	}

	if(randomPoints <= 1)
		value = basePoints;
	else
		value = basePoints + rand() % randomPoints;
	
	// Seal of Righteousness
	if (GetUnitTarget() && p_caster && m_spellInfo->Id == 20187)
	{
		if (p_caster->HasActiveAura(21084))
			value = float2int32(1 + 0.32f * u_caster->GetDamageDoneMod(SCHOOL_HOLY) + 0.2f * u_caster->GetAP());
	}

	if( p_caster != nullptr )
	{	
		int32 comboDamage = (int32)m_spellInfo->EffectPointsPerComboPoint[i];
	
		if(comboDamage)
		{
			value += ( comboDamage * p_caster->m_comboPoints );
			m_requiresCP = true;
			//this is ugly so i will explain the case maybe someone ha a better idea :
			// while casting a spell talent will trigger uppon the spell prepare faze
			// the effect of the talent is to add 1 combo point but when triggering spell finishes it will clear the extra combo point
			if( p_caster != nullptr )
				p_caster->m_spellcomboPoints = 0;
		}

		SpellOverrideMap::iterator itr = p_caster->mSpellOverrideMap.find(m_spellInfo->Id);
		if(itr != p_caster->mSpellOverrideMap.end())
		{
			ScriptOverrideList::iterator itrSO;
			for(itrSO = itr->second->begin(); itrSO != itr->second->end(); ++itrSO)
			{
				//DK:FIXME->yeni bir map olu�tur
                // Capt: WHAT THE FUCK DOES THIS MEAN....
				// Supa: WHAT THE FUCK DOES THIS MEAN?
				// Sphere: WHAT THE FUCK DOES THIS MEAN??
				value += RandomUInt((*itrSO)->damage);
			}
		}
	 }
	 
	if (m_spellScript != nullptr)
		m_spellScript->CalculateEffect(i, target, &value);

	// TODO: INHERIT ITEM MODS FROM REAL ITEM OWNER - BURLEX BUT DO IT PROPERLY

	if( u_caster != nullptr )
	{
		int32 spell_flat_modifers=0;
		int32 spell_pct_modifers=0;
		int32 spell_pct_modifers2=0;//separated from the other for debugging purpuses

		SM_FIValue(u_caster->SM_FSPELL_VALUE,&spell_flat_modifers,m_spellInfo->SpellGroupType);
		SM_FIValue(u_caster->SM_PSPELL_VALUE,&spell_pct_modifers,m_spellInfo->SpellGroupType);

		SM_FIValue(u_caster->SM_FEffectBonus,&spell_flat_modifers,m_spellInfo->SpellGroupType);
		SM_FIValue(u_caster->SM_PEffectBonus,&spell_pct_modifers,m_spellInfo->SpellGroupType);

		value = value + value*(spell_pct_modifers+spell_pct_modifers2)/100 + spell_flat_modifers;

	}
	else if( i_caster != nullptr && target)
	{	
		//we should inherit the modifiers from the conjured food caster
		Unit *item_creator = target->GetMapMgr()->GetUnit( i_caster->GetUInt64Value( ITEM_FIELD_CREATOR ) );
		if( item_creator != nullptr )
		{
			int32 spell_flat_modifers=0;
			int32 spell_pct_modifers=0;
			int32 spell_pct_modifers2=0;//separated from the other for debugging purpuses

			SM_FIValue(item_creator->SM_FSPELL_VALUE,&spell_flat_modifers,m_spellInfo->SpellGroupType);
			SM_FIValue(item_creator->SM_PSPELL_VALUE,&spell_pct_modifers,m_spellInfo->SpellGroupType);

			SM_FIValue(item_creator->SM_FEffectBonus,&spell_flat_modifers,m_spellInfo->SpellGroupType);
			SM_FIValue(item_creator->SM_PEffectBonus,&spell_pct_modifers,m_spellInfo->SpellGroupType);
#ifdef COLLECTION_OF_UNTESTED_STUFF_AND_TESTERS
			if(spell_flat_modifers!=0 || spell_pct_modifers!=0 || spell_pct_modifers2!=0)
				printf("!!!!ITEMCASTER ! : spell value mod flat %d , spell value mod pct %d, spell value mod pct2 %d , spell dmg %d, spell group %u\n",spell_flat_modifers,spell_pct_modifers,spell_pct_modifers2,value,m_spellInfo->SpellGroupType);
#endif
			value = value + value*(spell_pct_modifers+spell_pct_modifers2)/100 + spell_flat_modifers;
		}
	}


	return value;
}

void Spell::HandleTeleport(uint32 id, Unit* Target)
{
	if(Target->GetTypeId()!=TYPEID_PLAYER)
		return;

	Player* pTarget = TO_PLAYER( Target );
   
	float x,y,z;
	uint32 mapid;
	
    // predefined behavior 
	if (m_spellInfo->Id == 8690 || m_spellInfo->Id == 556 || m_spellInfo->Id == 39937)// 556 - Astral Recall ; 39937 - Ruby Slippers
	{
		x = pTarget->GetBindPositionX();
		y = pTarget->GetBindPositionY();
		z = pTarget->GetBindPositionZ();
		mapid = pTarget->GetBindMapId();
	}
	else // normal behavior
	{
		TeleportCoords* TC = TeleportCoordStorage.LookupEntry(id);
		if(!TC)
			return;
		 
		x=TC->x;
		y=TC->y;
		z=TC->z;
		mapid=TC->mapId;
	}

	pTarget->EventAttackStop();
	pTarget->SetSelection(0);
	  
	// We use a teleport event on this one. Reason being because of UpdateCellActivity,
	// the game object set of the updater thread WILL Get messed up if we teleport from a gameobject
	// caster.
	if(!sEventMgr.HasEvent(pTarget, EVENT_PLAYER_TELEPORT))
		sEventMgr.AddEvent(pTarget, &Player::EventTeleport, mapid, x, y, z, EVENT_PLAYER_TELEPORT, 1, 1,EVENT_FLAG_DO_NOT_EXECUTE_IN_WORLD_CONTEXT);
}

void Spell::CreateItem(uint32 itemId)
{
    if( !itemId )
        return;

	Player*			pUnit = TO_PLAYER( m_caster );
	Item*			newItem;
	Item*			add;
	SlotResult		slotresult;
	ItemPrototype*	m_itemProto;

	m_itemProto = ItemPrototypeStorage.LookupEntry( itemId );
	if( m_itemProto == nullptr )
	    return;

	if (pUnit->GetItemInterface()->CanReceiveItem(m_itemProto, 1))
	{
		SendCastResult(SPELL_FAILED_TOO_MANY_OF_ITEM);
		return;
	}

	add = pUnit->GetItemInterface()->FindItemLessMax(itemId, 1, false);
	if (!add)
	{
		slotresult = pUnit->GetItemInterface()->FindFreeInventorySlot(m_itemProto);
		if(!slotresult.Result)
		{
			 SendCastResult(SPELL_FAILED_TOO_MANY_OF_ITEM);
			 return;
		}
		
		newItem = objmgr.CreateItem(itemId, pUnit);
		AddItemResult result = pUnit->GetItemInterface()->SafeAddItem(newItem, slotresult.ContainerSlot, slotresult.Slot);
		if(!result)
		{
			delete newItem;
			return;
		}

		newItem->SetUInt64Value(ITEM_FIELD_CREATOR,m_caster->GetGUID());
		newItem->SetUInt32Value(ITEM_FIELD_STACK_COUNT, damage);

		/*WorldPacket data(45);
		p_caster->GetSession()->BuildItemPushResult(&data, p_caster->GetGUID(), 1, 1, itemId ,0,0xFF,1,0xFFFFFFFF);
		p_caster->SendMessageToSet(&data, true);*/
		p_caster->GetSession()->SendItemPushResult(newItem,true,false,true,true,slotresult.ContainerSlot,slotresult.Slot,1);
		newItem->m_isDirty = true;

	} 
	else 
	{
		add->SetUInt32Value(ITEM_FIELD_STACK_COUNT,add->GetUInt32Value(ITEM_FIELD_STACK_COUNT) + damage);
		/*WorldPacket data(45);
		p_caster->GetSession()->BuildItemPushResult(&data, p_caster->GetGUID(), 1, 1, itemId ,0,0xFF,1,0xFFFFFFFF);
		p_caster->SendMessageToSet(&data, true);*/
		p_caster->GetSession()->SendItemPushResult(add,true,false,true,false,p_caster->GetItemInterface()->GetBagSlotByGuid(add->GetGUID()),0xFFFFFFFF,1);
		add->m_isDirty = true;
	}
}

/*void Spell::_DamageRangeUpdate()
{
	if(unitTarget)
	{
		if(unitTarget->isAlive())
		{
			m_caster->SpellNonMeleeDamageLog(unitTarget,m_spellInfo->Id, damageToHit);
		}
		else
		{	if( u_caster != nullptr )
			if(u_caster->GetCurrentSpell() != this)
			{
					if(u_caster->GetCurrentSpell() != nullptr)
					{
						u_caster->GetCurrentSpell()->SendChannelUpdate(0);
						u_caster->GetCurrentSpell()->cancel();
					}
			}
			SendChannelUpdate(0);
			cancel();
		}
		sEventMgr.RemoveEvents(this, EVENT_SPELL_DAMAGE_HIT);
		delete this;
	}
	else if(gameObjTarget)
	{
		sEventMgr.RemoveEvents(this, EVENT_SPELL_DAMAGE_HIT);
		delete this;
		//Go Support
	}
	else
	{
		sEventMgr.RemoveEvents(this, EVENT_SPELL_DAMAGE_HIT);
		delete this;
	}
}*/

void Spell::SendHealSpellOnPlayer(Object* caster, Object* target, uint32 dmg,bool critical)
{
	if(!caster || !target || !target->IsPlayer())
		return;
	WorldPacket data(SMSG_HEALSPELL_ON_PLAYER, 27);
	// Bur: I know it says obsolete, but I just logged this tonight and got this packet.
	
	data << target->GetNewGUID();
	data << caster->GetNewGUID();
	data << (pSpellId ? pSpellId : m_spellInfo->Id);
	data << uint32(dmg);	// amt healed
	data << uint8(critical);	 //this is critical message

	caster->SendMessageToSet(&data, true);
}

void Spell::SendHealManaSpellOnPlayer(Object * caster, Object * target, uint32 dmg, uint32 powertype)
{
	if(!caster || !target || !target->IsPlayer())
		return;

	WorldPacket data(SMSG_HEALMANASPELL_ON_PLAYER, 30);

	data << target->GetNewGUID();
	data << caster->GetNewGUID();
	data << (pSpellId ? pSpellId : m_spellInfo->Id);
	data << powertype;
	data << dmg;

	caster->SendMessageToSet(&data, true);
}

void Spell::Heal(int32 amount, bool ForceCrit)
{
	int32 base_amount = amount; //store base_amount for later use

	if(!unitTarget || !unitTarget->isAlive())
		return;
	
	if( p_caster != nullptr )
		p_caster->last_heal_spell=m_spellInfo;

    //self healing shouldn't flag himself
	if(p_caster && playerTarget && p_caster != playerTarget)
	{
		// Healing a flagged target will flag you.
		if(playerTarget->IsPvPFlagged())
			p_caster->SetPvPFlag();
	}

	//Make it critical
	bool critical = false;
	int32 critchance = 0; 
	int32 bonus = 0;
	float healdoneaffectperc = 1.0f;
	if( u_caster != nullptr )
	{
		//Downranking
		if(p_caster && p_caster->IsPlayer() && m_spellInfo->baseLevel > 0 && m_spellInfo->maxLevel > 0)
		{
			float downrank1 = 1.0f;
			if (m_spellInfo->baseLevel < 20)
				downrank1 = 1.0f - (20.0f - float (m_spellInfo->baseLevel) ) * 0.0375f;
			float downrank2 = ( float(m_spellInfo->maxLevel + 5.0f) / float(p_caster->getLevel()) );
			if (downrank2 >= 1 || downrank2 < 0)
				downrank2 = 1.0f;
			healdoneaffectperc *= downrank1 * downrank2;
		}

		//Spells Not affected by Bonus Healing
		if(m_spellInfo->NameHash == SPELL_HASH_SEAL_OF_LIGHT) //Seal of Light
			healdoneaffectperc = 0.0f;

		//Basic bonus
		bonus += u_caster->HealDoneMod[m_spellInfo->School];

		/* Some unconfirmed stuff that seem right */
		SpellCastTime *sd = dbcSpellCastTime.LookupEntry(m_spellInfo->CastingTimeIndex);
		if(sd->CastTime < 3500)
		{
			float pct = sd->CastTime / 3500.0f;
			bonus = float2int32( bonus * pct );
		}

		//Basic bonus
		bonus += unitTarget->HealTakenMod[m_spellInfo->School];

		//Bonus from Intellect & Spirit
		if( p_caster != nullptr )  
		{
			for(uint32 a = 0; a < 6; a++)
				bonus += float2int32(p_caster->SpellHealDoneByAttribute[a][m_spellInfo->School] * p_caster->GetUInt32Value(UNIT_FIELD_STAT0 + a));
		}

		//Spell Coefficient
		if(  m_spellInfo->Dspell_coef_override >= 0 ) //In case we have forced coefficients
			bonus = float2int32( float( bonus ) * m_spellInfo->Dspell_coef_override );
		else
		{
			//Bonus to DH part
			if( m_spellInfo->fixed_dddhcoef >= 0 )
				bonus = float2int32( float( bonus ) * m_spellInfo->fixed_dddhcoef );
		}

		critchance = float2int32(u_caster->spellcritperc + u_caster->SpellCritChanceSchool[m_spellInfo->School]);

		if(m_spellInfo->SpellGroupType)
		{
			int penalty_pct = 0;
			int penalty_flt = 0;
			SM_FIValue( u_caster->SM_PPenalty, &penalty_pct, m_spellInfo->SpellGroupType );
			bonus += amount*penalty_pct/100;
			SM_FIValue( u_caster->SM_FPenalty, &penalty_flt, m_spellInfo->SpellGroupType );
			bonus += penalty_flt;
			SM_FIValue( u_caster->SM_CriticalChance,&critchance,m_spellInfo->SpellGroupType);
#ifdef COLLECTION_OF_UNTESTED_STUFF_AND_TESTERS
			int spell_flat_modifers=0;
			int spell_pct_modifers=0;
			SM_FIValue(u_caster->SM_FPenalty,&spell_flat_modifers,m_spellInfo->SpellGroupType);
			SM_FIValue(u_caster->SM_PPenalty,&spell_pct_modifers,m_spellInfo->SpellGroupType);
			if(spell_flat_modifers!=0 || spell_pct_modifers!=0)
				printf("!!!!!HEAL : spell dmg bonus(p=24) mod flat %d , spell dmg bonus(p=24) pct %d , spell dmg bonus %d, spell group %u\n",spell_flat_modifers,spell_pct_modifers,bonus,m_spellInfo->SpellGroupType);
#endif
		}

		if(u_caster->IsPlayer())
		{
			if(m_spellInfo->NameHash == SPELL_HASH_FLASH_OF_LIGHT || m_spellInfo->NameHash == SPELL_HASH_HOLY_LIGHT)
			{
				uint8 HLRank = 0;
				if(static_cast<Player*>(u_caster)->HasSpell(20237))
					HLRank = 1;
				if(static_cast<Player*>(u_caster)->HasSpell(20238))
					HLRank = 2;
				if(static_cast<Player*>(u_caster)->HasSpell(20239))
					HLRank = 3;

				if(HLRank)
				{
					float HLPct = 0.0f;
					switch(HLRank)
					{
						case 1:
							HLPct = 0.04f;
							break;
						case 2:
							HLPct = 0.08f;
							break;
						case 3:
							HLPct = 0.12f;
							break;
						default:
							HLPct = 0.0f;
							break;
					}
					
					HLPct += 1.0f;
					amount *= HLPct;
				}
			}
		}

		amount += float2int32( float( bonus ) * healdoneaffectperc ); //apply downranking on final value ?
		amount += amount*u_caster->HealDonePctMod[m_spellInfo->School]/100;
		amount += float2int32( float( amount ) * unitTarget->HealTakenPctMod[m_spellInfo->School] );

		if (m_spellInfo->SpellGroupType)
			SM_FIValue(u_caster->SM_PDamageBonus,&amount,m_spellInfo->SpellGroupType);
		
		if( critical = Rand(critchance) || ForceCrit )
		{
			/*int32 critbonus = amount >> 1;
			if( m_spellInfo->SpellGroupType)
					SM_PIValue(static_cast<Unit*>(u_caster)->SM_PCriticalDamage, &critbonus, m_spellInfo->SpellGroupType);
			amount += critbonus;*/

			int32 critical_bonus = 100;
			if( m_spellInfo->SpellGroupType )
				SM_FIValue( u_caster->SM_PCriticalDamage, &critical_bonus, m_spellInfo->SpellGroupType );

			if( critical_bonus > 0 )
			{
				// the bonuses are halved by 50% (funky blizzard math :S)
				float b = ( ( float(critical_bonus) / 2.0f ) / 100.0f );
				amount += float2int32( float(amount) * b );
			}

			//Shady: does it correct> caster casts heal and proc ..._VICTIM ? 
			// Or mb i'm completely wrong? So if true  - just replace with old string. 
			//u_caster->HandleProc(PROC_TARGET_SPELL_CRIT, unitTarget, m_spellInfo, amount);
			//Replaced with following one:
			
		
			unitTarget->HandleProc(PROC_TARGET_SPELL_CRIT, u_caster, m_spellInfo, amount);
			u_caster->HandleProc(PROC_ON_SPELL_CRIT, unitTarget, m_spellInfo, amount);
		}
		
	}

	if(amount < 0) 
		amount = 0;

	if( p_caster != nullptr )  
	{
		if( unitTarget->IsPlayer() )
		{
			SendHealSpellOnPlayer( p_caster, TO_PLAYER( unitTarget ), amount, critical );
		}
	}
	uint32 curHealth = unitTarget->GetUInt32Value(UNIT_FIELD_HEALTH);
	uint32 maxHealth = unitTarget->GetUInt32Value(UNIT_FIELD_MAXHEALTH);
	if((curHealth + amount) >= maxHealth)
		unitTarget->SetUInt32Value(UNIT_FIELD_HEALTH, maxHealth);
	else
		unitTarget->ModUnsigned32Value(UNIT_FIELD_HEALTH, amount);

	if (p_caster)
	{
		p_caster->m_casted_amount[m_spellInfo->School]=amount;
		p_caster->HandleProc( CPROC_CAST_SPECIFIC | PROC_ON_CAST_SPELL, unitTarget, m_spellInfo );
	}

	int doneTarget = 0;

	// add threat
	if( u_caster != nullptr )
	{
		//preventing overheal ;)
		if( (curHealth + base_amount) >= maxHealth )
			base_amount = maxHealth - curHealth;

		uint32 base_threat=GetBaseThreat(base_amount);
		int count = 0;
		Unit *unit;
		std::vector<Unit*> target_threat;
		if(base_threat)
		{
			/* 
			http://www.wowwiki.com/Threat
			Healing threat is global, and is normally .5x of the amount healed.
			Healing effects cause no threat if the target is already at full health.

			Example: Player 1 is involved in combat with 5 mobs. Player 2 (priest) heals Player 1 for 1000 health,
			and has no threat reduction talents. A 1000 heal generates 500 threat,
			however that 500 threat is split amongst the 5 mobs.
			Each of the 5 mobs now has 100 threat towards Player 2.
			*/

			target_threat.reserve(u_caster->GetInRangeCount()); // this helps speed

			for(std::set<Object*>::iterator itr = u_caster->GetInRangeSetBegin(); itr != u_caster->GetInRangeSetEnd(); ++itr)
			{
				if((*itr)->GetTypeId() != TYPEID_UNIT)
					continue;
				unit = static_cast<Unit*>((*itr));
				if(unit->GetAIInterface()->GetNextTarget() == unitTarget)
				{
					target_threat.push_back(unit);
					++count;
				}
			}
			count = ( count == 0 ? 1 : count );  // division against 0 protection

			// every unit on threatlist should get 1/2 the threat, divided by size of list
			uint32 threat = base_threat / (count * 2);

			// update threatlist (HealReaction)
			for(std::vector<Unit*>::iterator itr = target_threat.begin(); itr != target_threat.end(); ++itr)
			{
				// for now we'll just use heal amount as threat.. we'll prolly need a formula though
				TO_UNIT( *itr )->GetAIInterface()->HealReaction( u_caster, unitTarget, threat );

				if( (*itr)->GetGUID() == u_caster->CombatStatus.GetPrimaryAttackTarget() )
					doneTarget = 1;
			}
		}
		// remember that we healed (for combat status)
		if(unitTarget->IsInWorld() && u_caster->IsInWorld())
			u_caster->CombatStatus.WeHealed(unitTarget);
	}
}

void Spell::DetermineSkillUp(uint32 skillid,uint32 targetlevel)
{
	if(!p_caster)return;
	if(p_caster->GetSkillUpChance(skillid)<0.01)return;//to preven getting higher skill than max
	int32 diff=p_caster->_GetSkillLineCurrent(skillid,false)/5-targetlevel;
	if(diff<0)diff=-diff;
	float chance;
	if(diff<=5)chance=95.0;
	else if(diff<=10)chance=66;
	else if(diff <=15)chance=33;
	else return;
	if(Rand(chance*sWorld.getRate(RATE_SKILLCHANCE)))
		p_caster->_AdvanceSkillLine(skillid, float2int32( 1.0f * sWorld.getRate(RATE_SKILLRATE)));
}

void Spell::DetermineSkillUp(uint32 skillid)
{
	//This code is wrong for creating items and disenchanting. 
	if(!p_caster)return;
	float chance = 0.0f;
	skilllinespell* skill = objmgr.GetSpellSkill(m_spellInfo->Id);
	if( skill != nullptr && TO_PLAYER( m_caster )->_HasSkillLine( skill->skilline ) )
	{
		uint32 amt = TO_PLAYER( m_caster )->_GetSkillLineCurrent( skill->skilline, false );
		uint32 max = TO_PLAYER( m_caster )->_GetSkillLineMax( skill->skilline );
		if( amt >= max )
			return;
		if( amt >= skill->grey ) //grey
			chance = 0.0f;
		else if( ( amt >= ( ( ( skill->grey - skill->green) / 2 ) + skill->green ) ) ) //green
			chance = 33.0f;
		else if( amt >= skill->green ) //yellow
			chance = 66.0f;
		else //brown
			chance=100.0f;
	}
	if(Rand(chance*sWorld.getRate(RATE_SKILLCHANCE)))
		p_caster->_AdvanceSkillLine(skillid, float2int32( 1.0f * sWorld.getRate(RATE_SKILLRATE)));
}

void Spell::SafeAddTarget(TargetsList* tgt,uint64 guid)
{
	if(guid == 0)
		return;

	for(TargetsList::iterator i=tgt->begin();i!=tgt->end();i++)
		if((*i)==guid)
			return;
	
	tgt->push_back(guid);
}

void Spell::SafeAddMissedTarget(uint64 guid)
{
    for(SpellTargetsList::iterator i=ModeratedTargets.begin();i!=ModeratedTargets.end();i++)
        if((*i).TargetGuid==guid)
        {
            //sLog.outDebug("[SPELL] Something goes wrong in spell target system");
			// this isnt actually wrong, since we only have one missed target map,
			// whereas hit targets have multiple maps per effect.
            return;
        }

    ModeratedTargets.push_back(SpellTargetMod(guid,2));
}

void Spell::SafeAddModeratedTarget(uint64 guid, uint16 type)
{
	for(SpellTargetsList::iterator i=ModeratedTargets.begin();i!=ModeratedTargets.end();i++)
		if((*i).TargetGuid==guid)
        {
            //sLog.outDebug("[SPELL] Something goes wrong in spell target system");
			// this isnt actually wrong, since we only have one missed target map,
			// whereas hit targets have multiple maps per effect.
			return;
        }

	ModeratedTargets.push_back(SpellTargetMod(guid, (uint8)type));
}

bool Spell::Reflect(Unit *refunit)
{
	SpellEntry * refspell = nullptr;

	if( m_reflectedParent != nullptr )
		return false;

	// if the spell to reflect is a reflect spell, do nothing.
	for(int i=0; i<3; i++)
    {
		if( m_spellInfo->Effect[i] == 6 && (m_spellInfo->EffectApplyAuraName[i] == 74 || m_spellInfo->EffectApplyAuraName[i] == 28))
			return false;
    }
	for(std::list<struct ReflectSpellSchool*>::iterator i = refunit->m_reflectSpellSchool.begin();i != refunit->m_reflectSpellSchool.end();i++)
	{
		if((*i)->school == -1 || (*i)->school == (int32)m_spellInfo->School)
		{
			if(Rand((float)(*i)->chance))
			{
				//the god blessed special case : mage - Frost Warding = is an augmentation to frost warding
				if((*i)->require_aura_hash && u_caster && !u_caster->HasAurasWithNameHash((*i)->require_aura_hash))
                {
					continue;
                }
				refspell = m_spellInfo;
			}
		}
	}

	if(!refspell || m_caster == refunit) return false;

	Spell *spell = new Spell(refunit, refspell, true, nullptr);
	SpellCastTargets targets;
	targets.m_unitTarget = m_caster->GetGUID();
	spell->prepare(&targets);
	return true;
}

void ApplyDiminishingReturnTimer(uint32 * Duration, Unit * Target, SpellEntry * spell)
{
	uint32 status = GetDiminishingGroup(spell->NameHash);
	uint32 Grp = status & 0xFFFF;   // other bytes are if apply to pvp
	uint32 PvE = (status >> 16) & 0xFFFF;

	// Make sure we have a group
	if(Grp == 0xFFFF) return;

	// Check if we don't apply to pve
	if(!PvE && Target->GetTypeId() != TYPEID_PLAYER && !Target->IsPet())
		return;

	// TODO: check for spells that should do this
	float Dur = float(*Duration);

	// Unsure about these
	switch(Target->m_diminishCount[Grp])
	{
	case 0: // Full effect
		if ( ( Target->IsPlayer() || Target->IsPet() ) && Dur > 20000)
		{
			Dur = 20000;
		}
		break;
		
	case 1: // Reduced by 25%
		if ( ( Target->IsPlayer() || Target->IsPet() ) && Dur > 15000)
		{
			Dur = 15000;
		}
		break;
		
	case 2: // Reduced by 50%
		Dur *= 0.5f;
		if ( ( Target->IsPlayer() || Target->IsPet() ) && Dur > 10000)
		{
			Dur = 10000;
		}
		break;

	case 3: // Reduced by 75%
		Dur *= 0.25f;
		if ( ( Target->IsPlayer() || Target->IsPet() ) && Dur > 5000)
		{
			Dur = 5000;
		}
		break;

	default:// Target immune to spell
		{
			*Duration = 0;
			return;
		}break;
	}

	// Convert back
	*Duration = float2int32(Dur);

	// Reset the diminishing return counter, and add to the aura count (we don't decrease the timer till we
	// have no auras of this type left.
	++Target->m_diminishAuraCount[Grp];
	++Target->m_diminishCount[Grp];
}

void UnapplyDiminishingReturnTimer(Unit * Target, SpellEntry * spell)
{
	uint32 status = GetDiminishingGroup(spell->NameHash);
	uint32 Grp = status & 0xFFFF;   // other bytes are if apply to pvp
	uint32 PvE = (status >> 16) & 0xFFFF;

	// Make sure we have a group
	if(Grp == 0xFFFF) return;

	// Check if we don't apply to pve
	if(!PvE && Target->GetTypeId() != TYPEID_PLAYER && !Target->IsPet())
		return;

	Target->m_diminishAuraCount[Grp]--;

	// start timer decrease
	if(!Target->m_diminishAuraCount[Grp])
	{
		Target->m_diminishActive = true;
		Target->m_diminishTimer[Grp] = 15000;
	}
}

/// Calculate the Diminishing Group. This is based on a name hash.
/// this off course is very hacky, but as its made done in a proper way
/// I leave it here.
uint32 GetDiminishingGroup(uint32 NameHash)
{
	int32 grp = -1;
	bool pve = false;

	switch(NameHash)
	{
	case SPELL_HASH_CYCLONE:
	case SPELL_HASH_BLIND:
		grp = 0;
		pve = true;
		break;
	case SPELL_HASH_MIND_CONTROL:
		grp = 1;
		break;
	case SPELL_HASH_FEAR:
	case SPELL_HASH_PSYCHIC_SCREAM:
	case SPELL_HASH_HOWL_OF_TERROR:
	case SPELL_HASH_SEDUCTION:
		grp = 2;
		break;
	case SPELL_HASH_SAP:
	case SPELL_HASH_GOUGE:
	case SPELL_HASH_REPENTANCE:
	case SPELL_HASH_POLYMORPH:				// Polymorph
	case SPELL_HASH_POLYMORPH__CHICKEN:		// Chicken
	case SPELL_HASH_POLYMORPH__SHEEP:		// Good ol' sheep
		grp = 3;
		break;
	case SPELL_HASH_DEATH_COIL:
		grp = 4;
		break;
	case SPELL_HASH_KIDNEY_SHOT:
		grp = 5;
		pve = true;
		break;
	case SPELL_HASH_ENTRAPMENT:
		grp = 6;
		break;
	case SPELL_HASH_ENTANGLING_ROOTS:
	case SPELL_HASH_FROST_NOVA:
		grp = 7;
		break;
	case SPELL_HASH_FROSTBITE:
		grp = 8;
		break;
	case SPELL_HASH_HIBERNATE:
	case SPELL_HASH_WYVERN_STING:
	case SPELL_HASH_SLEEP:
	case SPELL_HASH_FROST_TRAP_AURA:
	case SPELL_HASH_FREEZING_TRAP_EFFECT:
		grp = 9;
		break;
	case SPELL_HASH_BASH:
	case SPELL_HASH_IMPACT:
	case SPELL_HASH_HAMMER_OF_JUSTICE:
	case SPELL_HASH_CHEAP_SHOT:
	case SPELL_HASH_CHARGE_STUN:
	case SPELL_HASH_INTERCEPT:
	case SPELL_HASH_CONCUSSION_BLOW:
		grp = 10;
		pve = true;
		break;
	case SPELL_HASH_STARFIRE_STUN:
	case SPELL_HASH_STUN:
	case SPELL_HASH_BLACKOUT:
		grp = 11;
		pve = true;
		break;
	case SPELL_HASH_HEX:
		grp = 12;
		break;
	}
	uint32 ret;
	if( pve )
		ret = grp | (1 << 16);
	else
		ret = grp;

	return ret;
}

/** There is Really No Reason to have these next two functions --Shane **/
void Spell::SendCastSuccess(Object * target)
{
	Player * plr = p_caster;
	if(!plr && u_caster)
		plr = u_caster->m_redirectSpellPackets;
	if(!plr||!plr->IsPlayer())
		return;

	sLog.outDebug("Send cast success! 1");

	//WorldPacket data(SMSG_TARGET_CAST_RESULT, 13);
	//data << ((target != 0) ? target->GetNewGUID() : uint8(0));
	//data << m_spellInfo->Id;
	
	//plr->GetSession()->SendPacket(&data);
}

void Spell::SendCastSuccess(const uint64& guid)
{
	Player * plr = p_caster;
	if(!plr && u_caster)
		plr = u_caster->m_redirectSpellPackets;
	if(!plr || !plr->IsPlayer())
		return;

	sLog.outDebug("Send cast success! 2");
    
	// fuck bytebuffers
	unsigned char buffer[13];
	uint32 c = FastGUIDPack(guid, buffer, 0);
#ifdef USING_BIG_ENDIAN
	*(uint32*)&buffer[c] = swap32(m_spellInfo->Id);         c += 4;
#else
	*(uint32*)&buffer[c] = m_spellInfo->Id;                 c += 4;
#endif

//	plr->GetSession()->OutPacket(SMSG_TARGET_CAST_RESULT, c, buffer);
}

void Spell::EventDelete()
{
	//this will execute until the events are finished, then delete :)
	if (!sEventMgr.HasEvent(this, EVENT_SPELL_HIT) && m_spellScript == nullptr)
		delete this;
}

int32 Spell::event_GetInstanceID()
{
	return m_caster->event_GetInstanceID();
}
