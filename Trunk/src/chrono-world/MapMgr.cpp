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

//
// MapMgr.cpp
//

#include "StdAfx.h"
#define MAP_MGR_UPDATE_PERIOD 100
#define MAPMGR_INACTIVE_MOVE_TIME 10
extern bool bServerShutdown;

#ifdef WIN32
#pragma warning(disable:4355)
#endif

MapMgr::MapMgr(Map *map, uint32 mapId, uint32 instanceid) : CellHandler<MapCell>(map), _mapId(mapId), eventHolder(instanceid), m_stateManager(*this)
{
	_terrain = new TerrainHolder(mapId);
	m_instanceID = instanceid;
	pMapInfo = WorldMapInfoStorage.LookupEntry(mapId);
	m_UpdateDistance = pMapInfo->update_distance * pMapInfo->update_distance;
	iInstanceMode = 0;

	// Create script interface
	ScriptInterface = new MapScriptInterface( this );

	// Set up storage arrays
	m_CreatureArraySize = map->CreatureSpawnCount;

	//m_CreatureStorage = new Creature*[m_CreatureArraySize];
	//m_CreatureStorage = (Creature**)malloc(sizeof(Creature*) * m_CreatureArraySize);
	//memset(m_CreatureStorage,0,sizeof(Creature*)*m_CreatureArraySize);

	m_GOHighGuid = 0;
	m_CreatureHighGuid = 0;
	m_DynamicObjectHighGuid=0; 
	lastUnitUpdate = getMSTime();
	lastGameobjectUpdate = getMSTime();
	m_battleground = NULLBATTLEGROUND;

	m_holder = &eventHolder;
	m_event_Instanceid = eventHolder.GetInstanceID();
	forced_expire = false;
	InactiveMoveTime = 0;
	mLoopCounter=0;
	pInstance = nullptr;
	thread_kill_only = false;
	thread_running = false;

	// buffers
	m_updateBuffer.reserve(50000);
	m_createBuffer.reserve(20000);
	m_updateBuildBuffer.reserve(65536);

	// uint32 destsize = size + size/10 + 16;
	m_compressionBuffer.reserve(75000);

	m_PlayerStorage.clear();
	m_PetStorage.clear();
	m_DynamicObjectStorage.clear();

	_combatProgress.clear();
	_mapWideStaticObjects.clear();
	_updates.clear();
	_processQueue.clear();
	MapSessions.clear();

	activeGameObjects.clear();
	activeCreatures.clear();
	m_corpses.clear();
	_sqlids_creatures.clear();
	_sqlids_gameobjects.clear();
	_reusable_guids_creature.clear();

	SetCollision(true);
	
	mInstanceScript = nullptr;
}

void MapMgr::Init()
{
	// Create World State Manager
	//m_stateManager = new WorldStateManager(this);
	
	// Create script interface
	ScriptInterface = new MapScriptInterface(this);
}

// call me to break the circular reference, perform cleanup
void MapMgr::Destructor()
{
	delete this;
}

MapMgr::~MapMgr()
{
	SetThreadName("thread_proc");//free the name

	sEventMgr.RemoveEvents(this);
	sEventMgr.RemoveEventHolder(m_instanceID);

	if (ScriptInterface != nullptr)
	{
		delete ScriptInterface;
		ScriptInterface = nullptr;
	}
	
	if ( mInstanceScript != nullptr )
		mInstanceScript->Destroy();

	delete _terrain;

	// Remove objects
	if(_cells)
	{
		for (uint32 i = 0; i < _sizeX; i++)
		{
			if(_cells[i] != 0)
			{
				for (uint32 j = 0; j < _sizeY; j++)
				{
					if(_cells[i][j] != 0)
					{
						_cells[i][j]->_unloadpending=false;
						_cells[i][j]->RemoveObjects();
					}
				}
			}
		}
	}

	Object* pObject;
	for(set<Object* >::iterator itr = _mapWideStaticObjects.begin(); itr != _mapWideStaticObjects.end(); ++itr)
	{
		pObject = *itr;
		if(!pObject)
			continue;

		if(pObject->IsInWorld())
			pObject->RemoveFromWorld(false);

		switch(pObject->GetTypeFromGUID())
		{
		case HIGHGUID_TYPE_UNIT:
			TO_CREATURE(pObject)->Destructor();
			break;

		case HIGHGUID_TYPE_GAMEOBJECT:
			TO_GAMEOBJECT(pObject)->Destructor();
			break;
		default:
			pObject->Destructor();

		}
	}
	_mapWideStaticObjects.clear();

	Corpse* pCorpse;
	for(set<Corpse*>::iterator itr = m_corpses.begin(); itr != m_corpses.end(); ++itr)
	{
		pCorpse = *itr;
		if(!pCorpse)
			continue;

		if(pCorpse->IsInWorld())
			pCorpse->RemoveFromWorld(false);

		pCorpse->Destructor();
	}
	m_corpses.clear();

	//Clear our remaining containers
	m_PlayerStorage.clear();
	m_PetStorage.clear();
	m_DynamicObjectStorage.clear();
	m_CreatureStorage.clear();
	m_gameObjectStorage.clear();

	_combatProgress.clear();
	_updates.clear();
	_processQueue.clear();
	pMapInfo = nullptr;

	activeGameObjects.clear();
	activeCreatures.clear();
	_sqlids_creatures.clear();
	_sqlids_gameobjects.clear();
	_reusable_guids_creature.clear();

	m_battleground = NULLBATTLEGROUND;

	Log.Notice("MapMgr", "Instance %u shut down. (%s)" , m_instanceID, GetBaseMap()->GetName());
}

void MapMgr::PushObject(Object *obj)
{
	/////////////
	// Assertions
	/////////////
	ASSERT(obj);
	
	// That object types are not map objects. TODO: add AI groups here?
	if(obj->GetTypeId() == TYPEID_ITEM || obj->GetTypeId() == TYPEID_CONTAINER)
	{
		// mark object as updatable and exit
		return;
	}

	if(obj->GetTypeId() == TYPEID_CORPSE)
	{
		m_corpses.insert(((Corpse*)obj));
	}	
	
	obj->ClearInRangeSet();
	ASSERT(obj->GetMapId() == _mapId);
	if(!(obj->GetPositionX() < _maxX && obj->GetPositionX() > _minX) || 
	   !(obj->GetPositionY() < _maxY && obj->GetPositionY() > _minY))
	{
		if(obj->IsPlayer())
		{
			Player * plr = static_cast< Player* >( obj );
			if(plr->GetBindMapId() != GetMapId())
			{
				plr->SafeTeleport(plr->GetBindMapId(),0,plr->GetBindPositionX(),plr->GetBindPositionY(),plr->GetBindPositionZ(),0);
				plr->GetSession()->SystemMessage("Teleported you to your chrono location as you were out of the map boundaries.");
				return;
			}
			else
			{
				obj->GetPositionV()->ChangeCoords(plr->GetBindPositionX(),plr->GetBindPositionY(),plr->GetBindPositionZ(),0);
				plr->GetSession()->SystemMessage("Teleported you to your chrono location as you were out of the map boundaries.");
				WorldPacket * data = plr->BuildTeleportAckMsg(plr->GetPosition());
				plr->GetSession()->SendPacket(data);
				delete data;
			}
		}
		else
		{
			obj->GetPositionV()->ChangeCoords(0,0,0,0);
		}
	}

	ASSERT(obj->GetPositionY() < _maxY && obj->GetPositionY() > _minY);
	ASSERT(_cells);

	///////////////////////
	// Get cell coordinates
	///////////////////////

	uint32 x = GetPosX(obj->GetPositionX());
	uint32 y = GetPosY(obj->GetPositionY());

	if(x >= _sizeX || y >= _sizeY)
	{
		if(obj->IsPlayer())
		{
			Player * plr = static_cast< Player* >( obj );
			if(plr->GetBindMapId() != GetMapId())
			{
				plr->SafeTeleport(plr->GetBindMapId(),0,plr->GetBindPositionX(),plr->GetBindPositionY(),plr->GetBindPositionZ(),0);
				plr->GetSession()->SystemMessage("Teleported you to your chrono location as you were out of the map boundaries.");
				return;
			}
			else
			{
				obj->GetPositionV()->ChangeCoords(plr->GetBindPositionX(),plr->GetBindPositionY(),plr->GetBindPositionZ(),0);
				plr->GetSession()->SystemMessage("Teleported you to your chrono location as you were out of the map boundaries.");
				WorldPacket * data = plr->BuildTeleportAckMsg(plr->GetPosition());
				plr->GetSession()->SendPacket(data);
				delete data;
			}
		}
		else
		{
			obj->GetPositionV()->ChangeCoords(0,0,0,0);
		}

		x = GetPosX(obj->GetPositionX());
		y = GetPosY(obj->GetPositionY());
	}

	MapCell *objCell = GetCell(x,y);
	if (!objCell)
	{
		objCell = Create(x,y);
		objCell->Init(x, y, _mapId, this);
	}

	uint32 endX = (x <= _sizeX) ? x + 1 : (_sizeX-1);
	uint32 endY = (y <= _sizeY) ? y + 1 : (_sizeY-1);
	uint32 startX = x > 0 ? x - 1 : 0;
	uint32 startY = y > 0 ? y - 1 : 0;
	uint32 posX, posY;
	MapCell *cell;
	MapCell::ObjectSet::iterator iter;

	uint32 count;
	Player *plObj;

	if(obj->GetTypeId() == TYPEID_PLAYER)
		plObj = static_cast< Player* >( obj );
	else
		plObj = nullptr;

	if(plObj)
	{
		DEBUG_LOG("Creating player " I64FMT " for himself.", obj->GetGUID());
		count = plObj->BuildCreateUpdateBlockForPlayer(&m_createBuffer, plObj);
		plObj->PushCreationData(&m_createBuffer, count);
		m_createBuffer.clear();
	}

	//////////////////////
	// Build in-range data
	//////////////////////

	for (posX = startX; posX <= endX; posX++ )
	{
		for (posY = startY; posY <= endY; posY++ )
		{
			cell = GetCell(posX, posY);
			if (cell)
			{
				UpdateInRangeSet(obj, plObj, cell);
			}
		}
	}

	//Add to the cell's object list
	objCell->AddObject(obj);

	obj->SetMapCell(objCell);
	 //Add to the mapmanager's object list
	if(plObj)
	{
	   m_PlayerStorage[plObj->GetLowGUID()] = plObj;
	   UpdateCellActivity(x, y, 2);
	}
	else
	{
		switch(obj->GetTypeFromGUID())
		{
		case HIGHGUID_TYPE_PET:
			m_PetStorage[obj->GetUIdFromGUID()] = static_cast< Pet* >( obj );
			break;

		case HIGHGUID_TYPE_UNIT:
			{
				ASSERT((obj->GetUIdFromGUID()) <= m_CreatureHighGuid);
				m_CreatureStorage[obj->GetUIdFromGUID()] = TO_CREATURE(obj);
				if (TO_CREATURE(obj)->m_spawn != nullptr)
				{
					_sqlids_creatures.insert(std::make_pair(TO_CREATURE(obj)->GetSQL_id(), TO_CREATURE(obj)));
				}
			}break;

		case HIGHGUID_TYPE_GAMEOBJECT:
			{
				m_gameObjectStorage[obj->GetUIdFromGUID()] = TO_GAMEOBJECT(obj);
				if (TO_GAMEOBJECT(obj)->m_spawn != nullptr)
				{
					_sqlids_gameobjects.insert(std::make_pair(TO_GAMEOBJECT(obj)->m_spawn->id, TO_GAMEOBJECT(obj)));
				}
			}break;

		case HIGHGUID_TYPE_DYNAMICOBJECT:
			m_DynamicObjectStorage[obj->GetLowGUID()] = (DynamicObject*)obj;
			break;
		}
	}

	// Handle activation of that object.
	if(objCell->IsActive() && obj->CanActivate())
		obj->Activate(this);

	// Add the session to our set if it is a player.
	if(plObj)
	{
		MapSessions.insert(plObj->GetSession());

		// Change the instance ID, this will cause it to be removed from the world thread (return value 1)
		plObj->GetSession()->SetInstance(GetInstanceID());

		/* Add the map wide objects */
		if(_mapWideStaticObjects.size())
		{
			for(set<Object*>::iterator itr = _mapWideStaticObjects.begin(); itr != _mapWideStaticObjects.end(); ++itr)
			{
				count = (*itr)->BuildCreateUpdateBlockForPlayer(&m_createBuffer, plObj);
				plObj->PushCreationData(&m_createBuffer, count);
				m_createBuffer.clear();
			}
		}
	}

	if(plObj && InactiveMoveTime && !forced_expire)
		InactiveMoveTime = 0;
}


void MapMgr::PushStaticObject(Object *obj)
{
	_mapWideStaticObjects.insert(obj);

	switch(obj->GetTypeFromGUID())
	{
		case HIGHGUID_TYPE_UNIT:
			m_CreatureStorage[obj->GetUIdFromGUID()] = (Creature*)obj;
			break;

		case HIGHGUID_TYPE_GAMEOBJECT:
			m_gameObjectStorage[obj->GetUIdFromGUID()] = static_cast<GameObject*>(obj);
			break;

		default:
			// maybe add a warning, shouldnt be needed
			break;
	}
}

void MapMgr::RemoveObject(Object *obj, bool free_guid)
{
	/////////////
	// Assertions
	/////////////

	ASSERT(obj);
	ASSERT(obj->GetMapId() == _mapId);
	//ASSERT(obj->GetPositionX() > _minX && obj->GetPositionX() < _maxX);
	//ASSERT(obj->GetPositionY() > _minY && obj->GetPositionY() < _maxY);
	ASSERT(_cells);

	_updates.erase(obj);
	obj->ClearUpdateMask();
	Player* plObj = (obj->GetTypeId() == TYPEID_PLAYER) ? static_cast< Player* >( obj ) : 0;

	///////////////////////////////////////
	// Remove object from all needed places
	///////////////////////////////////////
 
	switch(obj->GetTypeFromGUID())
	{
		case HIGHGUID_TYPE_UNIT:
			ASSERT(obj->GetUIdFromGUID() <= m_CreatureHighGuid);
			m_CreatureStorage[obj->GetUIdFromGUID()] = 0;
			if(((Creature*)obj)->m_spawn != nullptr)
			{
				_sqlids_creatures.erase(((Creature*)obj)->m_spawn->id);
			}

			if(free_guid)
				_reusable_guids_creature.push_back(obj->GetUIdFromGUID());

		  break;

		case HIGHGUID_TYPE_PET:
			// check iterator
			if( __pet_iterator != m_PetStorage.end() && __pet_iterator->second == TO_PET(obj) )
				++__pet_iterator;

			m_PetStorage.erase(obj->GetUIdFromGUID());
			break;

		case HIGHGUID_TYPE_DYNAMICOBJECT:
			m_DynamicObjectStorage.erase(obj->GetLowGUID());
			break;

		case HIGHGUID_TYPE_GAMEOBJECT:
			m_gameObjectStorage.erase(obj->GetGUID());
			if(((GameObject*)obj)->m_spawn != nullptr)
			{
				_sqlids_gameobjects.erase(((GameObject*)obj)->m_spawn->id);
			}

			break;

		case HIGHGUID_TYPE_PLAYER:
			// check iterator
			if( __player_iterator != m_PlayerStorage.end() && __player_iterator->second == TO_PLAYER(obj) )
				++__player_iterator;

			break;
	}

	if(obj->Active)
		obj->Deactivate(this);

	// That object types are not map objects. TODO: add AI groups here?
	if(obj->GetTypeId() == TYPEID_ITEM || obj->GetTypeId() == TYPEID_CONTAINER || obj->GetTypeId()==10)
	{
		return;
	}

	if(obj->GetTypeId() == TYPEID_CORPSE)
	{
		m_corpses.erase(((Corpse*)obj));
	}

	if(!obj->GetMapCell())
	{
		/* set the map cell correctly */
		if(obj->GetPositionX() >= _maxX || obj->GetPositionX() <= _minY ||
			obj->GetPositionY() >= _maxY || obj->GetPositionY() <= _minY)
		{
			// do nothing
		}
		else
		{
			obj->SetMapCell(this->GetCellByCoords(obj->GetPositionX(), obj->GetPositionY()));
		}		
	}

	if(obj->GetMapCell())
	{
		ASSERT(obj->GetMapCell());
	
		// Remove object from cell
		obj->GetMapCell()->RemoveObject(obj);
	
		// Unset object's cell
		obj->SetMapCell(nullptr);
	}

	// Clear any updates pending
	if(obj->GetTypeId() == TYPEID_PLAYER)
	{
		_processQueue.erase(TO_PLAYER(obj));
		static_cast< Player* >( obj )->ClearAllPendingUpdates();
	}
	
	// Remove object from all objects 'seeing' him
	for (Object::InRangeSet::iterator iter = obj->GetInRangeSetBegin();
		iter != obj->GetInRangeSetEnd(); ++iter)
	{
		if( (*iter) )
		{
			if( (*iter)->GetTypeId() == TYPEID_PLAYER )
			{
				if( static_cast< Player* >( *iter )->IsVisible( obj ) && static_cast< Player* >( *iter )->m_TransporterGUID != obj->GetGUID() )
					static_cast< Player* >( *iter )->PushOutOfRange(obj->GetNewGUID());
			}
			(*iter)->RemoveInRangeObject(obj);
		}
	}
	
	// Clear object's in-range set
	obj->ClearInRangeSet();

	// If it's a player - update his nearby cells
	if(obj->GetTypeId() == TYPEID_PLAYER)
	{
		// get x/y
		if(obj->GetPositionX() >= _maxX || obj->GetPositionX() <= _minY ||
			obj->GetPositionY() >= _maxY || obj->GetPositionY() <= _minY)
		{
			// do nothing
		}
		else
		{
			uint32 x = GetPosX(obj->GetPositionX());
			uint32 y = GetPosY(obj->GetPositionY());
			UpdateCellActivity(x, y, 2);
		}
		m_PlayerStorage.erase( static_cast< Player* >( obj )->GetLowGUID() );
	}

	// Remove the session from our set if it is a player.
	if(plObj)
	{
		for(set<Object*>::iterator itr = _mapWideStaticObjects.begin(); itr != _mapWideStaticObjects.end(); ++itr)
		{
			plObj->PushOutOfRange((*itr)->GetNewGUID());
		}

		// Setting an instance ID here will trigger the session to be removed
		// by MapMgr::run(). :)
		plObj->GetSession()->SetInstance(0);

		// Add it to the global session set.
		// Don't "re-add" to session if it is being deleted.
		if(!plObj->GetSession()->bDeleted)
			sWorld.AddGlobalSession(plObj->GetSession());
	}

	if(!HasPlayers() && !InactiveMoveTime && !forced_expire && GetMapInfo()->type != INSTANCE_NULL)
	{
		InactiveMoveTime = UNIXTIME + (MAPMGR_INACTIVE_MOVE_TIME * 60);	   // 5 mins -> move to inactive
	}
}

void MapMgr::ChangeObjectLocation( Object *obj )
{
	// Items and containers are of no interest for us
	if( obj->GetTypeId() == TYPEID_ITEM || obj->GetTypeId() == TYPEID_CONTAINER || obj->GetMapMgr() != this )
	{
		return;
	}

	Player* plObj;

	if( obj->GetTypeId() == TYPEID_PLAYER )
	{
		plObj = static_cast< Player* >( obj );
	}
	else
	{
		plObj = nullptr;
	}

	Object* curObj;
	float fRange;

	///////////////////////////////////////
	// Update in-range data for old objects
	///////////////////////////////////////

	/** let's duplicate some code here :P Less branching is always good.
	 * - Burlex
	 */
/*#define IN_RANGE_LOOP \
	for (Object::InRangeSet::iterator iter = obj->GetInRangeSetBegin(), iter2; \
		iter != obj->GetInRangeSetEnd();) \
	{ \
		curObj = *iter; \
		iter2 = iter; \
		++iter; \
		if(curObj->IsPlayer() && obj->IsPlayer() && plObj->m_TransporterGUID && plObj->m_TransporterGUID == static_cast< Player* >( curObj )->m_TransporterGUID ) \
			fRange = 0.0f;		\
		else if((curObj->GetGUIDHigh() == HIGHGUID_TRANSPORTER || obj->GetGUIDHigh() == HIGHGUID_TRANSPORTER)) \
			fRange = 0.0f;		\
		else if((curObj->GetGUIDHigh() == HIGHGUID_GAMEOBJECT && curObj->GetByte(GAMEOBJECT_STATE, GAMEOBJECT_BYTES_TYPE_ID) == GAMEOBJECT_TYPE_TRANSPORT || obj->GetGUIDHigh() == HIGHGUID_GAMEOBJECT && obj->GetByte(GAMEOBJECT_STATE, GAMEOBJECT_BYTES_TYPE_ID) == GAMEOBJECT_TYPE_TRANSPORT)) \
			fRange = 0.0f;		\
		else \
			fRange = m_UpdateDistance;	\
		if (curObj->GetDistance2dSq(obj) > fRange && fRange > 0) \

#define END_IN_RANGE_LOOP } \

	if(plObj)
	{
		IN_RANGE_LOOP
		{
			plObj->RemoveIfVisible(curObj);
			plObj->RemoveInRangeObject(iter2);

			if(curObj->NeedsInRangeSet())
				curObj->RemoveInRangeObject(obj);

			if(curObj->IsPlayer())
				static_cast< Player* >( curObj )->RemoveIfVisible(obj);
		}
		END_IN_RANGE_LOOP
	}
	else if(obj->NeedsInRangeSet())
	{
		IN_RANGE_LOOP
		{
			if(curObj->NeedsInRangeSet())
				curObj->RemoveInRangeObject(obj);

			if(curObj->IsPlayer())
				static_cast< Player* >( curObj )->RemoveIfVisible(obj);

			obj->RemoveInRangeObject(iter2);
		}
		END_IN_RANGE_LOOP
	}
	else
	{
		IN_RANGE_LOOP
		{
			if(curObj->NeedsInRangeSet())
				curObj->RemoveInRangeObject(obj);

			if(curObj->IsPlayer())
			{
				static_cast< Player* >( curObj )->RemoveIfVisible(obj);
				obj->RemoveInRangePlayer(curObj);
			}
		}
		END_IN_RANGE_LOOP
	}

#undef IN_RANGE_LOOP
#undef END_IN_RANGE_LOOP*/

	if(obj->HasInRangeObjects()) {
		for (Object::InRangeSet::iterator iter = obj->GetInRangeSetBegin(), iter2;
			iter != obj->GetInRangeSetEnd();)
		{
			curObj = *iter;
			iter2 = iter++;
			if( curObj->IsPlayer() && obj->IsPlayer() && plObj->m_TransporterGUID && plObj->m_TransporterGUID == static_cast< Player* >( curObj )->m_TransporterGUID )
				fRange = 0.0f; // unlimited distance for people on same boat
			else if( curObj->GetTypeFromGUID() == HIGHGUID_TYPE_TRANSPORTER )
				fRange = 0.0f; // unlimited distance for transporters (only up to 2 cells +/- anyway.)
			else
				fRange = m_UpdateDistance; // normal distance

			if( fRange > 0.0f && curObj->GetDistance2dSq(obj) > fRange )
			{
				if( plObj )
					plObj->RemoveIfVisible(curObj);

				if( curObj->IsPlayer() )
					static_cast< Player* >( curObj )->RemoveIfVisible(obj);

				curObj->RemoveInRangeObject(obj);

				if( obj->GetMapMgr() != this )
				{
					/* Something removed us. */
					return;
				}
				obj->RemoveInRangeObject(iter2);
			}
		}
	}

	///////////////////////////
	// Get new cell coordinates
	///////////////////////////
	if(obj->GetMapMgr() != this)
	{
		/* Something removed us. */
		return;
	}

	if(obj->GetPositionX() >= _maxX || obj->GetPositionX() <= _minX ||
		obj->GetPositionY() >= _maxY || obj->GetPositionY() <= _minY)
	{
		if(obj->IsPlayer())
		{
			Player* plr = static_cast< Player* >( obj );
			if(plr->GetBindMapId() != GetMapId())
			{
				plr->SafeTeleport(plr->GetBindMapId(),0,plr->GetBindPositionX(),plr->GetBindPositionY(),plr->GetBindPositionZ(),0);
				plr->GetSession()->SystemMessage("Teleported you to your chrono location as you were out of the map boundaries.");
				return;
			}
			else
			{
				obj->GetPositionV()->ChangeCoords(plr->GetBindPositionX(),plr->GetBindPositionY(),plr->GetBindPositionZ(),0);
				plr->GetSession()->SystemMessage("Teleported you to your chrono location as you were out of the map boundaries.");
				WorldPacket * data = plr->BuildTeleportAckMsg(plr->GetPosition());
				plr->GetSession()->SendPacket(data);
				delete data;
			}
		}
		else
		{
			obj->GetPositionV()->ChangeCoords(0,0,0,0);
		}
	}

	uint32 cellX = GetPosX(obj->GetPositionX());
	uint32 cellY = GetPosY(obj->GetPositionY());

	if(cellX >= _sizeX || cellY >= _sizeY)
	{
		return;
	}

	MapCell *objCell = GetCell(cellX, cellY);
	MapCell * pOldCell = obj->GetMapCell();
	if (!objCell)
	{
		objCell = Create(cellX,cellY);
		objCell->Init(cellX, cellY, _mapId, this);
	}

	// If object moved cell
	if (objCell != obj->GetMapCell())
	{
		// THIS IS A HACK!
		// Current code, if a creature on a long waypoint path moves from an active
		// cell into an inactive one, it will disable itself and will never return.
		// This is to prevent cpu leaks. I will think of a better solution very soon :P

		if(!objCell->IsActive() && !plObj && obj->Active)
			obj->Deactivate(this);

		if(obj->GetMapCell())
			obj->GetMapCell()->RemoveObject(obj);
	
		objCell->AddObject(obj);
		obj->SetMapCell(objCell);

		// if player we need to update cell activity
		// radius = 2 is used in order to update both
		// old and new cells
		if(obj->GetTypeId() == TYPEID_PLAYER)
		{
			// have to unlock/lock here to avoid a deadlock situation.
			UpdateCellActivity(cellX, cellY, 2);
			if( pOldCell != nullptr )
			{
				// only do the second check if theres -/+ 2 difference
				if( abs( (int)cellX - (int)pOldCell->_x ) > 2 ||
					abs( (int)cellY - (int)pOldCell->_y ) > 2 )
				{
					UpdateCellActivity( pOldCell->_x, pOldCell->_y, 2 );
				}
			}
		}
	}


	//////////////////////////////////////
	// Update in-range set for new objects
	//////////////////////////////////////

	uint32 endX = cellX <= _sizeX ? cellX + 1 : (_sizeX-1);
	uint32 endY = cellY <= _sizeY ? cellY + 1 : (_sizeY-1);
	uint32 startX = cellX > 0 ? cellX - 1 : 0;
	uint32 startY = cellY > 0 ? cellY - 1 : 0;
	uint32 posX, posY;
	MapCell *cell;
	MapCell::ObjectSet::iterator iter;

	for (posX = startX; posX <= endX; ++posX )
	{
		for (posY = startY; posY <= endY; ++posY )
		{
			cell = GetCell(posX, posY);
			if (cell)
				UpdateInRangeSet(obj, plObj, cell);
		}
	}
}

void MapMgr::UpdateInRangeSet( Object* obj, Player* plObj, MapCell* cell )
{
	if( cell == nullptr )
		return;

	Object* curObj;
	Player* plObj2;
	int count;
	ObjectSet::iterator iter = cell->Begin();
	ObjectSet::iterator itr;
	float fRange;
	bool cansee, isvisible;

	while( iter != cell->End() )
	{
		curObj = *iter;
		++iter;

		if( curObj == nullptr )
			continue;

		if( curObj->IsPlayer() && obj->IsPlayer() && plObj && plObj->m_TransporterGUID && plObj->m_TransporterGUID == TO_PLAYER( curObj )->m_TransporterGUID )
			fRange = 0.0f; // unlimited distance for people on same boat
		else if( curObj->GetTypeFromGUID() == HIGHGUID_TYPE_TRANSPORTER)
			fRange = 0.0f; // unlimited distance for transporters (only up to 2 cells +/- anyway.)
		else
			fRange = m_UpdateDistance; // normal distance

		if ( curObj != obj && ( curObj->GetDistance2dSq( obj ) <= fRange || fRange == 0.0f ) )
		{
			if( !obj->IsInRangeSet( curObj ) )
			{
				// Object in range, add to set
				obj->AddInRangeObject( curObj );
				curObj->AddInRangeObject( obj );

				if( curObj->IsPlayer() )
				{
					plObj2 = TO_PLAYER( curObj );

					if( plObj2->CanSee( obj ) && !plObj2->IsVisible( obj ) )
					{
						count = obj->BuildCreateUpdateBlockForPlayer(&m_createBuffer, plObj2);
						plObj2->PushCreationData(&m_createBuffer, count);
						plObj2->AddVisibleObject(obj);
						m_createBuffer.clear();
					}
				}

				if( plObj != nullptr )
				{
					if( plObj->CanSee( curObj ) && !plObj->IsVisible( curObj ) )
					{
						count = curObj->BuildCreateUpdateBlockForPlayer( &m_createBuffer, plObj );
						plObj->PushCreationData( &m_createBuffer, count );
						plObj->AddVisibleObject( curObj );
						m_createBuffer.clear();
					}
				}
			}
			else
			{
				// Check visiblility
				if( curObj->IsPlayer() )
				{
					plObj2 = TO_PLAYER( curObj );
					cansee = plObj2->CanSee(obj);
					isvisible = plObj2->GetVisibility(obj, &itr);
					if(!cansee && isvisible)
					{
						plObj2->PushOutOfRange(obj->GetNewGUID());
						plObj2->RemoveVisibleObject(itr);
					}
					else if(cansee && !isvisible)
					{
						count = obj->BuildCreateUpdateBlockForPlayer(&m_createBuffer, plObj2);
						plObj2->PushCreationData(&m_createBuffer, count);
						plObj2->AddVisibleObject(obj);
						m_createBuffer.clear();
					}
				}

				if( plObj )
				{
					cansee = plObj->CanSee( curObj );
					isvisible = plObj->GetVisibility( curObj, &itr );
					if(!cansee && isvisible)
					{
						plObj->PushOutOfRange( curObj->GetNewGUID() );
						plObj->RemoveVisibleObject( itr );
					}
					else if(cansee && !isvisible)
					{
						count = curObj->BuildCreateUpdateBlockForPlayer( &m_createBuffer, plObj );
						plObj->PushCreationData( &m_createBuffer, count );
						plObj->AddVisibleObject( curObj );
						m_createBuffer.clear();
					}
				}
			}
		}
	}
}

void MapMgr::_UpdateObjects()
{
	if(!_updates.size() && !_processQueue.size())
	{
		return;
	}
	//m_updateMutex.Release();

	Object *pObj;
	Player *pOwner;
	//std::set<Object*>::iterator it_start, it_end, itr;
	std::set<Player*>::iterator it_start, it_end, itr;
	Player * lplr;
	uint32 count = 0;
	
	//m_updateMutex.Acquire();
	UpdateQueue::iterator iter = _updates.begin();
	//m_updateMutex.Release();
	PUpdateQueue::iterator it, eit;

	for(; iter != _updates.end();)
	{
		pObj = *iter;
		++iter;
		if(!pObj) continue;

		if(pObj->GetTypeId() == TYPEID_ITEM || pObj->GetTypeId() == TYPEID_CONTAINER)
		{
			// our update is only sent to the owner here.
			pOwner = static_cast< Item* >(pObj)->GetOwner();
			if( pOwner != nullptr )
			{
				count = static_cast< Item* >( pObj )->BuildValuesUpdateBlockForPlayer( &m_updateBuffer, pOwner );
				// send update to owner
				if( count )
				{
					pOwner->PushUpdateData( &m_updateBuffer, count );
					m_updateBuffer.clear();
				}
			}
		}
		else
		{
			if( pObj->IsInWorld() )
			{
				// players have to receive their own updates ;)
				if( pObj->GetTypeId() == TYPEID_PLAYER )
				{
					// need to be different! ;)
					count = pObj->BuildValuesUpdateBlockForPlayer( &m_updateBuffer, static_cast< Player* >( pObj ) );
					if( count )
					{
						static_cast< Player* >( pObj )->PushUpdateData( &m_updateBuffer, count );
						m_updateBuffer.clear();
					}
				}

				if( pObj->IsUnit() && pObj->HasUpdateField( UNIT_FIELD_HEALTH ) )
					static_cast< Unit* >( pObj )->EventHealthChangeSinceLastUpdate();

				// build the update
				count = pObj->BuildValuesUpdateBlockForPlayer( &m_updateBuffer, static_cast< Player* >( nullptr ) );

				if( count )
				{
					it_start = pObj->GetInRangePlayerSetBegin();
					it_end = pObj->GetInRangePlayerSetEnd();
					for(itr = it_start; itr != it_end;)
					{
						lplr = *itr;
						++itr;
						// Make sure that the target player can see us.
						if( lplr->GetTypeId() == TYPEID_PLAYER && lplr->IsVisible( pObj ) )
							lplr->PushUpdateData( &m_updateBuffer, count );
					}
					m_updateBuffer.clear();
				}
			}
		}
		pObj->ClearUpdateMask();
	}
	//_updates.clear();
	//m_updateMutex.Release();
	
	// generate pending a9packets and send to clients.
	Player *plyr;
	for(it = _processQueue.begin(); it != _processQueue.end();)
	{
		plyr = *it;
		eit = it;
		++it;
		_processQueue.erase(eit);
		if(plyr->GetMapMgr() == this)
			plyr->ProcessPendingUpdates();
	}
}
void MapMgr::LoadAllCells()
{
	// eek
	MapCell * cellInfo;
	CellSpawns * spawns;

	for( uint32 x = 0 ; x < _sizeX ; x ++ )
	{
		for( uint32 y = 0 ; y < _sizeY ; y ++ )
		{
			cellInfo = GetCell( x , y );
			
			if( !cellInfo )
			{
				// Cell doesn't exist, create it.
				// There is no spoon. Err... cell.
				cellInfo = Create( x , y );
				cellInfo->Init( x , y , _mapId , this );
				DEBUG_LOG( "Created cell [%u,%u] on map %d (instance %d)." , x , y , _mapId , m_instanceID );
				cellInfo->SetActivity( true );
				_map->CellGoneActive( x , y );
				ASSERT( !cellInfo->IsLoaded() );

				spawns = _map->GetSpawnsList( x , y );
				if( spawns )
					cellInfo->LoadObjects( spawns );
			}
			else
			{
				// Cell exists, but is inactive
				if ( !cellInfo->IsActive() )
				{
					DEBUG_LOG("Activated cell [%u,%u] on map %d (instance %d).", x, y, _mapId, m_instanceID );
					_map->CellGoneActive( x , y );
					cellInfo->SetActivity( true );

					if (!cellInfo->IsLoaded())
					{
						//DEBUG_LOG("Loading objects for Cell [%d][%d] on map %d (instance %d)...", 
						//	posX, posY, this->_mapId, m_instanceID);
						spawns = _map->GetSpawnsList( x , y );
						if( spawns )
							cellInfo->LoadObjects( spawns );
					}
				}
			}
		}
	}
}

void MapMgr::UpdateCellActivity(uint32 x, uint32 y, int radius)
{
	CellSpawns * sp;
	uint32 endX = (x + radius) <= _sizeX ? x + radius : (_sizeX-1);
	uint32 endY = (y + radius) <= _sizeY ? y + radius : (_sizeY-1);
	uint32 startX = x - radius > 0 ? x - radius : 0;
	uint32 startY = y - radius > 0 ? y - radius : 0;
	uint32 posX, posY;

	MapCell *objCell;

	for (posX = startX; posX <= endX; posX++ )
	{
		for (posY = startY; posY <= endY; posY++ )
		{
			objCell = GetCell(posX, posY);

			if (!objCell)
			{
				if (_CellActive(posX, posY))
				{
					objCell = Create(posX, posY);
					objCell->Init(posX, posY, _mapId, this);

//					DEBUG_LOG("Cell [%d,%d] on map %d (instance %d) is now active.", 
//						posX, posY, this->_mapId, m_instanceID);
					objCell->SetActivity(true);
					_map->CellGoneActive(posX, posY);
					_terrain->LoadTile((int32)posX / 8, (int32)posY / 8);

					ASSERT(!objCell->IsLoaded());

//					DEBUG_LOG("Loading objects for Cell [%d][%d] on map %d (instance %d)...", 
//						posX, posY, this->_mapId, m_instanceID);

					sp = _map->GetSpawnsList(posX, posY);
					if(sp) objCell->LoadObjects(sp);
				}
			}
			else
			{
				//Cell is now active
				if (_CellActive(posX, posY) && !objCell->IsActive())
				{
//					DEBUG_LOG("Cell [%d,%d] on map %d (instance %d) is now active.", 
//						posX, posY, this->_mapId, m_instanceID);
					_map->CellGoneActive(posX, posY);
					_terrain->LoadTile((int32)posX / 8, (int32)posY / 8);
					objCell->SetActivity(true);

					if (!objCell->IsLoaded())
					{
//						DEBUG_LOG("Loading objects for Cell [%d][%d] on map %d (instance %d)...", 
//							posX, posY, this->_mapId, m_instanceID);
						sp = _map->GetSpawnsList(posX, posY);
						if(sp) objCell->LoadObjects(sp);
					}
				}
				//Cell is no longer active
				else if (!_CellActive(posX, posY) && objCell->IsActive())
				{
//					DEBUG_LOG("Cell [%d,%d] on map %d (instance %d) is now idle.", 
//						posX, posY, this->_mapId, m_instanceID);
					_map->CellGoneIdle(posX, posY);
					objCell->SetActivity(false);
					_terrain->UnloadTile((int32)posX / 8, (int32)posY / 8);
				}
			}
		}
	}

}

void MapMgr::AddForcedCell(MapCell * c)
{
	c->SetPermanentActivity(true);
	UpdateCellActivity(c->GetPositionX(), c->GetPositionY(), 1);
}

void MapMgr::RemoveForcedCell(MapCell * c)
{
	c->SetPermanentActivity(false);
	UpdateCellActivity(c->GetPositionX(), c->GetPositionY(), 1);
}

bool MapMgr::_CellActive(uint32 x, uint32 y)
{
	uint32 endX = ((x+1) <= _sizeX) ? x + 1 : (_sizeX-1);
	uint32 endY = ((y+1) <= _sizeY) ? y + 1 : (_sizeY-1);
	uint32 startX = x > 0 ? x - 1 : 0;
	uint32 startY = y > 0 ? y - 1 : 0;
	uint32 posX, posY;

	MapCell *objCell;

	for (posX = startX; posX <= endX; posX++ )
	{
		for (posY = startY; posY <= endY; posY++ )
		{
			objCell = GetCell(posX, posY);

			if (objCell)
			{
				if (objCell->HasPlayers() || objCell->IsForcedActive() )
				{
					return true;
				}
			}
		}
	}

	return false;
}

void MapMgr::ObjectUpdated(Object *obj)
{
	// set our fields to dirty
	// stupid fucked up code in places.. i hate doing this but i've got to :<
	// - burlex
	_updates.insert(obj);
}

void MapMgr::PushToProcessed(Player* plr)
{
	_processQueue.insert(plr);
}


void MapMgr::ChangeFarsightLocation(Player *plr, Creature *farsight)
{
	if(farsight == 0)
	{
		// We're clearing.
		for(ObjectSet::iterator itr = plr->m_visibleFarsightObjects.begin(); itr != plr->m_visibleFarsightObjects.end();
			++itr)
		{
			if(plr->IsVisible((*itr)) && !plr->CanSee((*itr)))
			{
				// Send destroy
				plr->PushOutOfRange((*itr)->GetNewGUID());
			}
		}
		plr->m_visibleFarsightObjects.clear();
	}
	else
	{
		plr->m_visibleFarsightObjects.clear();
		uint32 cellX = GetPosX(farsight->GetPositionX());
		uint32 cellY = GetPosY(farsight->GetPositionY());
		uint32 endX = (cellX <= _sizeX) ? cellX + 1 : (_sizeX-1);
		uint32 endY = (cellY <= _sizeY) ? cellY + 1 : (_sizeY-1);
		uint32 startX = cellX > 0 ? cellX - 1 : 0;
		uint32 startY = cellY > 0 ? cellY - 1 : 0;
		uint32 posX, posY;
		MapCell *cell;
		Object *obj;
		MapCell::ObjectSet::iterator iter, iend;
		uint32 count;
		for (posX = startX; posX <= endX; ++posX )
		{
			for (posY = startY; posY <= endY; ++posY )
			{
				cell = GetCell(posX, posY);
				if (cell)
				{
					iter = cell->Begin();
					iend = cell->End();
					for(; iter != iend; ++iter)
					{
						obj = (*iter);
						if(!plr->IsVisible(obj) && plr->CanSee(obj) && farsight->GetDistance2dSq(obj) <= m_UpdateDistance)
						{
							ByteBuffer buf;
							count = obj->BuildCreateUpdateBlockForPlayer(&buf, plr);
							plr->PushCreationData(&buf, count);
							plr->m_visibleFarsightObjects.insert(obj);
						}
					}
					
				}
			}
		}
	}
}

/* new stuff
*/

bool MapMgr::run()
{
	return Do();
}

bool MapMgr::Do()
{
#ifdef WIN32
	threadid=GetCurrentThreadId();
#endif
	thread_running = true;
	SetThreadName("Map mgr - M%u|I%u",this->_mapId ,this->m_instanceID);
	ObjectSet::iterator i;
	uint32 last_exec=getMSTime();

	/* create static objects */
	for(GOSpawnList::iterator itr = _map->staticSpawns.GOSpawns.begin(); itr != _map->staticSpawns.GOSpawns.end(); ++itr)
	{
		GameObject * obj = CreateGameObject((*itr)->entry);
		obj->Load((*itr));
		_mapWideStaticObjects.insert(obj);
	}

	for(CreatureSpawnList::iterator itr = _map->staticSpawns.CreatureSpawns.begin(); itr != _map->staticSpawns.CreatureSpawns.end(); ++itr)
	{
		Creature * obj = CreateCreature((*itr)->entry);
		obj->Load(*itr, 0, pMapInfo);
		_mapWideStaticObjects.insert(obj);
	}

	/* add static objects */
	for(set<Object*>::iterator itr = _mapWideStaticObjects.begin(); itr != _mapWideStaticObjects.end(); ++itr)
		PushStaticObject(*itr);

	/* load corpses */
	objmgr.LoadCorpses(this);

	// initialize worldstates
	sWorldStateTemplateManager.ApplyMapTemplate(this);

	if( GetMapInfo()->type == INSTANCE_NULL )
		sHookInterface.OnContinentCreate(this);

	// always declare local variables outside of the loop!
	// otherwise theres a lot of sub esp; going on.

	uint32 exec_time, exec_start;
#ifdef WIN32
	HANDLE hThread = GetCurrentThread();
#endif
	while(m_threadRunning)
	{
		exec_start=getMSTime();
		//first push to world new objects
		m_objectinsertlock.Acquire();//<<<<<<<<<<<<<<<<
		if(m_objectinsertpool.size())
		{
			for(i=m_objectinsertpool.begin();i!=m_objectinsertpool.end();++i)
			{
				//PushObject(*i);
				(*i)->PushToWorld(this);
			}
			m_objectinsertpool.clear();
		}
		m_objectinsertlock.Release();//>>>>>>>>>>>>>>>>
		//-------------------------------------------------------
				
		//Now update sessions of this map + objects
		_PerformObjectDuties();

		last_exec=getMSTime();
		exec_time=last_exec-exec_start;
		if(exec_time<MAP_MGR_UPDATE_PERIOD)
		{
			/*
				The common place I see this is waiting for a Win32 thread to exit. I used to come up with all sorts of goofy,
				elaborate event-based systems to do this myself until I discovered that thread handles are waitable. Just use
				WaitForSingleObject() on the thread handle and you're done. No risking race conditions with the thread exit code.
				I think pthreads has pthread_join() for this too.

				- http://www.virtualdub.org/blog/pivot/entry.php?id=62
			*/

#ifdef WIN32
			WaitForSingleObject(hThread, MAP_MGR_UPDATE_PERIOD-exec_time);
#else
			Sleep(MAP_MGR_UPDATE_PERIOD-exec_time);
#endif
		}

		//////////////////////////////////////////////////////////////////////////
		// Check if we have to die :P
		//////////////////////////////////////////////////////////////////////////
		if(InactiveMoveTime && UNIXTIME >= InactiveMoveTime)
			break;
	}

	// Clear the instance's reference to us.
	if(m_battleground)
	{
		BattlegroundManager.DeleteBattleground(m_battleground);
		sInstanceMgr.DeleteBattlegroundInstance( GetMapId(), GetInstanceID() );
	}

	if(pInstance)
	{
		// check for a non-raid instance, these expire after 10 minutes.
		if(GetMapInfo()->type == INSTANCE_NONRAID || pInstance->m_isBattleground)
		{
			pInstance->m_mapMgr = nullptr;
			sInstanceMgr._DeleteInstance(pInstance, true);
		}
		else
		{
			// just nullptr out the pointer
			pInstance->m_mapMgr=nullptr;
		}
	}
	else if(GetMapInfo()->type == INSTANCE_NULL)
		sInstanceMgr.m_singleMaps[GetMapId()] = nullptr;

	// Teleport any left-over players out.
	TeleportPlayers();	

	thread_running = false;
	if(thread_kill_only)
		return false;

	// delete ourselves
	Destructor();

	// already deleted, so the threadpool doesn't have to.
	return false;
}

void MapMgr::BeginInstanceExpireCountdown()
{
	WorldPacket data(SMSG_RAID_GROUP_ONLY, 8);
	PlayerStorageMap::iterator itr;

	// so players getting removed don't overwrite us
	forced_expire = true;

	// send our sexy packet
    data << uint32(60000) << uint32(1);
	for(itr = m_PlayerStorage.begin(); itr != m_PlayerStorage.end(); ++itr)
	{
		if(!itr->second->raidgrouponlysent)
			itr->second->GetSession()->SendPacket(&data);
	}

	// set our expire time to 60 seconds.
	InactiveMoveTime = UNIXTIME + 60;
}

void MapMgr::AddObject(Object *obj)
{
	m_objectinsertlock.Acquire();//<<<<<<<<<<<<
	m_objectinsertpool.insert(obj);
	m_objectinsertlock.Release();//>>>>>>>>>>>>
}


Unit* MapMgr::GetUnit(const uint64 & guid)
{
	switch(GET_TYPE_FROM_GUID(guid))
	{
	case HIGHGUID_TYPE_UNIT:
		return GetCreature( GET_LOWGUID_PART(guid) );
		break;

	case HIGHGUID_TYPE_PLAYER:
		return GetPlayer( (uint32)guid );
		break;

	case HIGHGUID_TYPE_PET:
		return GetPet( GET_LOWGUID_PART(guid) );
		break;
	}

	return NULLUNIT;
}

Object* MapMgr::_GetObject(const uint64 & guid)
{
	switch(GET_TYPE_FROM_GUID(guid))
	{
	case	HIGHGUID_TYPE_GAMEOBJECT:
		return GetGameObject(GET_LOWGUID_PART(guid));
		break;
	case	HIGHGUID_TYPE_UNIT:
		return GetCreature(GET_LOWGUID_PART(guid));
		break;
	case	HIGHGUID_TYPE_DYNAMICOBJECT:
		return GetDynamicObject((uint32)guid);
		break;
	case	HIGHGUID_TYPE_TRANSPORTER:
		return objmgr.GetTransporter(Chrono::Util::GUID_LOPART(guid));
		break;
	case HIGHGUID_TYPE_CORPSE:
		return objmgr.GetCorpse(Chrono::Util::GUID_LOPART(guid));
		break;
	default:
		return GetUnit(guid);
		break;
	}
}

void MapMgr::_PerformObjectDuties()
{
	++mLoopCounter;
	uint32 mstime = getMSTime();
	uint32 difftime = mstime - lastUnitUpdate;
	if(difftime > 500)
		difftime = 500;

	Creature* ptr;
	__creature_iterator = activeCreatures.begin();
	for(; __creature_iterator != activeCreatures.end();)
	{
		ptr = *__creature_iterator;
		++__creature_iterator;

		ptr->Update(difftime);
	}
	
	Pet* ptr2;
	__pet_iterator = m_PetStorage.begin();
	for(; __pet_iterator != m_PetStorage.end();)
	{
		ptr2 = __pet_iterator->second;
		++__pet_iterator;

		ptr2->Update(difftime);
	}		
	
	// Update any events.
	eventHolder.Update(difftime);
	
	// Update players.
	Player* ptr3;
	__player_iterator = m_PlayerStorage.begin();
	for(; __player_iterator != m_PlayerStorage.end();)
	{
		ptr3 = __player_iterator->second;;
		++__player_iterator;

		ptr3->Update( difftime );
	}
	
	lastUnitUpdate = mstime;

	// Update gameobjects (not on every loop, however)
	if( mLoopCounter % 2 )
	{
		difftime = mstime - lastGameobjectUpdate;

		GameObject* ptr4;
		__gameobject_iterator = activeGameObjects.begin();
		for(; __gameobject_iterator != activeGameObjects.end(); )
		{
			ptr4 = *__gameobject_iterator;
			++__gameobject_iterator;

			ptr4->Update( difftime );
		}

		lastGameobjectUpdate = mstime;
	}	

	// Sessions are updated every loop.
	{
		int result = 0;
		WorldSession * MapSession;
		SessionSet::iterator itr = MapSessions.begin();
		SessionSet::iterator it2;

		for(; itr != MapSessions.end();)
		{
			MapSession = (*itr);
			it2 = itr;
			++itr;

			//we have teleported to another map, remove us here.
			if(MapSession->GetInstance() != m_instanceID)
			{
				MapSessions.erase(it2);
				continue;
			}

			// Don't update players not on our map.
			// If we abort in the handler, it means we will "lose" packets, or not process this.
			// .. and that could be diasterous to our client :P
			if( MapSession->GetPlayer()->GetMapMgr()== nullptr || 
				MapSession->GetPlayer()->GetMapMgr() != this)
				continue;


			result = MapSession->Update(m_instanceID);
			if(result)//session or socket deleted?
			{
				if(result == 1)//socket don't exist anymore, delete from both world- and map-sessions.
					sWorld.DeleteGlobalSession(MapSession);
				MapSessions.erase(it2);
			}
		}
	}

	// Finally, A9 Building/Distribution
	_UpdateObjects();
}

void MapMgr::EventCorpseDespawn(uint64 guid)
{
	Corpse * pCorpse = objmgr.GetCorpse((uint32)guid);
	if(pCorpse == 0)	// Already Deleted
		return;

	if(pCorpse->GetMapMgr() != this)
		return;

	pCorpse->Despawn();
	delete pCorpse;
}

void MapMgr::TeleportPlayers()
{
	PlayerStorageMap::iterator itr =  m_PlayerStorage.begin();
	if(!bServerShutdown)
	{
		for(; itr !=  m_PlayerStorage.end();)
		{
			Player *p = itr->second;
			++itr;
			p->EjectFromInstance();
		}
	}
	else
	{
		for(; itr !=  m_PlayerStorage.end();)
		{
			Player *p = itr->second;
			++itr;
			if(p->GetSession())
				p->GetSession()->LogoutPlayer(false);
			else
				delete p;
		}
	}
}

void MapMgr::UnloadCell(uint32 x,uint32 y)
{
	MapCell * c = GetCell(x,y);
	if(c == nullptr || c->HasPlayers() || _CellActive(x,y) || !c->IsUnloadPending()) return;

	DEBUG_LOG("Unloading Cell [%d][%d] on map %d (instance %d)...", 
		x,y,_mapId,m_instanceID);

	c->Unload();
}

void MapMgr::EventRespawnCreature(Creature * c, MapCell * p)
{
	ObjectSet::iterator itr = p->_respawnObjects.find( ((Object*)c) );
	if(itr != p->_respawnObjects.end())
	{
		c->m_respawnCell=nullptr;
		p->_respawnObjects.erase(itr);
		c->OnRespawn(this);
	}
}

void MapMgr::EventRespawnGameObject(GameObject * o, MapCell * c)
{
	ObjectSet::iterator itr = c->_respawnObjects.find( ((Object*)o) );
	if(itr != c->_respawnObjects.end())
	{
		o->m_respawnCell=nullptr;
		c->_respawnObjects.erase(itr);
		o->Spawn(this);
	}
}

void MapMgr::SendMessageToCellPlayers(Object * obj, WorldPacket * packet, uint32 cell_radius /* = 2 */)
{
	uint32 cellX = GetPosX(obj->GetPositionX());
	uint32 cellY = GetPosY(obj->GetPositionY());
	uint32 endX = ((cellX+cell_radius) <= _sizeX) ? cellX + cell_radius : (_sizeX-1);
	uint32 endY = ((cellY+cell_radius) <= _sizeY) ? cellY + cell_radius : (_sizeY-1);
	uint32 startX = (cellX-cell_radius) > 0 ? cellX - cell_radius : 0;
	uint32 startY = (cellY-cell_radius) > 0 ? cellY - cell_radius : 0;

	uint32 posX, posY;
	MapCell *cell;
	MapCell::ObjectSet::iterator iter, iend;
	for (posX = startX; posX <= endX; ++posX )
	{
		for (posY = startY; posY <= endY; ++posY )
		{
			cell = GetCell(posX, posY);
			if (cell && cell->HasPlayers() )
			{
				iter = cell->Begin();
				iend = cell->End();
				for(; iter != iend; ++iter)
				{
					if((*iter)->IsPlayer())
					{
						static_cast< Player* >(*iter)->GetSession()->SendPacket(packet);
					}
				}
			}
		}
	}
}

void MapMgr::SendChatMessageToCellPlayers(Object * obj, WorldPacket * packet, uint32 cell_radius, uint32 langpos, int32 lang, WorldSession * originator)
{
	uint32 cellX = GetPosX(obj->GetPositionX());
	uint32 cellY = GetPosY(obj->GetPositionY());
	uint32 endX = ((cellX+cell_radius) <= _sizeX) ? cellX + cell_radius : (_sizeX-1);
	uint32 endY = ((cellY+cell_radius) <= _sizeY) ? cellY + cell_radius : (_sizeY-1);
	uint32 startX = (cellX-cell_radius) > 0 ? cellX - cell_radius : 0;
	uint32 startY = (cellY-cell_radius) > 0 ? cellY - cell_radius : 0;

	uint32 posX, posY;
	MapCell *cell;
	MapCell::ObjectSet::iterator iter, iend;
	for (posX = startX; posX <= endX; ++posX )
	{
		for (posY = startY; posY <= endY; ++posY )
		{
			cell = GetCell(posX, posY);
			if (cell && cell->HasPlayers() )
			{
				iter = cell->Begin();
				iend = cell->End();
				for(; iter != iend; ++iter)
				{
					if((*iter)->IsPlayer())
					{
						//static_cast< Player* >(*iter)->GetSession()->SendPacket(packet);
						static_cast< Player* >(*iter)->GetSession()->SendChatPacket(packet, langpos, lang, originator);
					}
				}
			}
		}
	}
}

Creature * MapMgr::GetSqlIdCreature(uint32 sqlid)
{
	CreatureSqlIdMap::iterator itr = _sqlids_creatures.find(sqlid);
	return (itr == _sqlids_creatures.end()) ? nullptr : itr->second;
}

GameObject * MapMgr::GetSqlIdGameObject(uint32 sqlid)
{
	GameObjectSqlIdMap::iterator itr = _sqlids_gameobjects.find(sqlid);
	return (itr == _sqlids_gameobjects.end()) ? nullptr : itr->second;
}

void MapMgr::HookOnAreaTrigger(Player * plr, uint32 id)
{
	switch (id)
	{
	case 4591:
		//Only opens when the first one steps in, if 669 if you find a way, put it in :P (else was used to increase the time the door stays opened when another one steps on it)
		GameObject *door = GetInterface()->GetGameObjectNearestCoords(803.827f, 6869.38f, -38.5434f, 184212);
		if (door && (door->GetUInt32Value(GAMEOBJECT_STATE) == 1))
		{
			door->SetUInt32Value(GAMEOBJECT_STATE, 0);
			//sEventMgr.AddEvent(door, &GameObject::SetUInt32Value, GAMEOBJECT_STATE, 1, EVENT_SCRIPT_UPDATE_EVENT, 10000, 1, 0);
		}
		break;
	}
}

Creature * MapMgr::CreateCreature(uint32 entry)
{
	uint64 newguid = ( (uint64)HIGHGUID_TYPE_UNIT << 32 ) | ( (uint64)entry << 24 );
	Creature* cr = NULLCREATURE;
	if(_reusable_guids_creature.size())
	{
		uint32 guid = _reusable_guids_creature.front();
		_reusable_guids_creature.pop_front();

		newguid |= guid;
	}
	else
		newguid |= ++m_CreatureHighGuid;

	cr = new Creature(newguid);
	cr->Init();

	ASSERT( cr->GetTypeFromGUID() == HIGHGUID_TYPE_UNIT );
	return cr;
}

GameObject * MapMgr::CreateGameObject(uint32 entry)
{
	//Validate the entry
	GameObjectInfo *goi = nullptr;
	goi = GameObjectNameStorage.LookupEntry( entry );
	if( goi == nullptr )
	{
		Log.Warning("MapMgr", "Skipping CreateGameObject for entry %u due to incomplete database.", entry);
		return NULLGOB;
	}

	uint64 new_guid = ( (uint64)HIGHGUID_TYPE_GAMEOBJECT << 32 ) | ( (uint64)entry << 24 );
	m_GOHighGuid &= 0x00FFFFFF;
	new_guid |= (uint64)(++m_GOHighGuid);

	GameObject* go = NULLGOB;
	go = new GameObject(new_guid);
	go->Init();

	ASSERT( go->GetTypeFromGUID() == HIGHGUID_TYPE_GAMEOBJECT );
	return go;
}

DynamicObject * MapMgr::CreateDynamicObject()
{
	DynamicObject* dyn = nullptr;
	dyn = new DynamicObject(HIGHGUID_TYPE_DYNAMICOBJECT,(++m_DynamicObjectHighGuid));
	dyn->Init();

	ASSERT( dyn->GetTypeFromGUID() == HIGHGUID_TYPE_DYNAMICOBJECT );
	return dyn;
}

void MapMgr::SendPacketToPlayers(int32 iZoneMask, int32 iFactionMask, StackPacket *pData)
{
	// Update all players on map.
	Player* ptr = nullptr;
	PlayerStorageMap::iterator itr1 = m_PlayerStorage.begin();
	for(itr1 = m_PlayerStorage.begin(); itr1 != m_PlayerStorage.end();)
	{
		ptr = itr1->second;
		itr1++;
		if(ptr->GetSession())
		{
			//Are we in the right zone?
			if( iZoneMask != ZONE_MASK_ALL && ptr->GetZoneId() != (uint32)iZoneMask )
				continue;

			//Are we the right faction?
			if( iFactionMask != FACTION_MASK_ALL && ptr->GetTeam() != (uint32)iFactionMask )
				continue;

			ptr->GetSession()->SendPacket(pData);
		}
	}
}

void MapMgr::SendPacketToPlayers(int32 iZoneMask, int32 iFactionMask, WorldPacket *pData)
{
	// Update all players on map.
	Player * ptr = nullptr;
	PlayerStorageMap::iterator itr1 = m_PlayerStorage.begin();
	for(itr1 = m_PlayerStorage.begin(); itr1 != m_PlayerStorage.end();)
	{
		ptr = itr1->second;
		itr1++;
		if(ptr->GetSession())
		{
			//Are we in the right zone?
			if( iZoneMask != ZONE_MASK_ALL && ptr->GetZoneId() != (uint32)iZoneMask )
				continue;

			//Are we the right faction?
			if( iFactionMask != FACTION_MASK_ALL && ptr->GetTeam() != (uint32)iFactionMask )
				continue;

			ptr->GetSession()->SendPacket(pData);
		}
	}
}

void MapMgr::RemoveAuraFromPlayers(int32 iFactionMask, uint32 uAuraId)
{
	// Update all players on map.
	Player* ptr;
	for(__player_iterator = m_PlayerStorage.begin(); __player_iterator != m_PlayerStorage.end();__player_iterator++)
	{
		ptr = __player_iterator->second;;

		if(ptr->GetSession())
		{
			if( iFactionMask != FACTION_MASK_ALL && ptr->GetTeam() != (uint32)iFactionMask )
				continue;

			ptr->RemoveAura(uAuraId);
		}
	}
}

void MapMgr::RemovePositiveAuraFromPlayers(int32 iFactionMask, uint32 uAuraId)
{
	// Update all players on map.
	Player* ptr;
	for(__player_iterator = m_PlayerStorage.begin(); __player_iterator != m_PlayerStorage.end();__player_iterator++)
	{
		ptr = __player_iterator->second;;

		if(ptr->GetSession())
		{
			if( iFactionMask != FACTION_MASK_ALL && ptr->GetTeam() != (uint32)iFactionMask )
				continue;
			ptr->RemovePositiveAura(uAuraId);
		}
	}
}

void MapMgr::CastSpellOnPlayers(int32 iFactionMask, uint32 uSpellId)
{
	PlayerStorageMap::iterator itr = m_PlayerStorage.begin();
	SpellEntry * sp = dbcSpell.LookupEntryForced(uSpellId);
	if( !sp )
		return;

	for(; itr !=  m_PlayerStorage.end();)
	{
		Player *p = itr->second;
		++itr;

		if( iFactionMask != FACTION_MASK_ALL && p->GetTeam() != (uint32)iFactionMask )
			continue;

		p->CastSpell(p, sp, true);
	}
}

void MapMgr::SendPvPCaptureMessage(int32 iZoneMask, uint32 ZoneId, const char * Format, ...)
{
	va_list ap;
	va_start(ap,Format);

	WorldPacket data(SMSG_PVP_CAPTURE_STATE_MSG, 208);
	char msgbuf[200];
	vsnprintf(msgbuf, 200, Format, ap);
	va_end(ap);

	data << ZoneId;
	data << uint32(strlen(msgbuf)+1);
	data << msgbuf;

	PlayerStorageMap::iterator itr = m_PlayerStorage.begin();
	for(; itr !=  m_PlayerStorage.end();)
	{
		Player *p = itr->second;
		++itr;

		if( ( iZoneMask != ZONE_MASK_ALL && p->GetZoneId() != (uint32)iZoneMask) )
			continue;

		p->GetSession()->SendPacket(&data);
	}
}

void MapMgr::LoadInstanceScript()
{
	mInstanceScript = sScriptMgr.CreateScriptClassForInstance( _mapId, this );
};

void MapMgr::CallScriptUpdate()
{
	ASSERT( mInstanceScript );
	mInstanceScript->UpdateEvent();
};

uint16 MapMgr::GetAreaID( float x, float y )
{
	uint32 exploreFlag = _terrain->GetAreaFlag(x, y);

	std::map<uint32, AreaTable*>::iterator itr = sWorld.mAreaIDToTable.find(exploreFlag);

	if (itr == sWorld.mAreaIDToTable.end())
		return 0;
	return itr->second->AreaId;
}