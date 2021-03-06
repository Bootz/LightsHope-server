/*
 * Copyright (C) 2005-2011 MaNGOS <http://getmangos.com/>
 * Copyright (C) 2009-2011 MaNGOSZero <https://github.com/mangos/zero>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "ScriptMgr.h"
#include "Policies/SingletonImp.h"
#include "Log.h"
#include "ProgressBar.h"
#include "ObjectMgr.h"
#include "WaypointManager.h"
#include "World.h"
#include "GossipDef.h"
#include "SpellAuras.h"
#include "ScriptLoader.h"

typedef std::vector<Script*> ScriptVector;
int num_sc_scripts;
ScriptVector m_scripts;

ScriptMapMap sQuestEndScripts;
ScriptMapMap sQuestStartScripts;
ScriptMapMap sSpellScripts;
ScriptMapMap sCreatureSpellScripts;
ScriptMapMap sGameObjectScripts;
ScriptMapMap sEventScripts;
ScriptMapMap sGossipScripts;
ScriptMapMap sCreatureMovementScripts;

INSTANTIATE_SINGLETON_1(ScriptMgr);

ScriptMgr::ScriptMgr() : m_scheduledScripts(0), m_spellSummary(nullptr)
{
}

ScriptMgr::~ScriptMgr()
{
    delete[] m_spellSummary;

    // Free resources before library unload
    for (ScriptVector::iterator itr = m_scripts.begin(); itr != m_scripts.end(); ++itr)
        delete *itr;

    m_scripts.clear();

    num_sc_scripts = 0;
}

void ScriptMgr::LoadScripts(ScriptMapMap& scripts, const char* tablename)
{
    if (IsScriptScheduled())                                // function don't must be called in time scripts use.
        return;

    sLog.outString("%s :", tablename);

    scripts.clear();                                        // need for reload support

    //                                                  0    1       2         3         4          5          6         7           8             9          10        11        12        13        14    15 16 17 18
    QueryResult *result = WorldDatabase.PQuery("SELECT id, delay, command, datalong, datalong2, datalong3, datalong4, buddy_id, buddy_radius, buddy_type, data_flags, dataint, dataint2, dataint3, dataint4, x, y, z, o FROM %s", tablename);

    uint32 count = 0;

    if (!result)
    {
        BarGoLink bar(1);
        bar.step();

        sLog.outString();
        sLog.outString(">> Loaded %u script definitions", count);
        return;
    }

    BarGoLink bar(result->GetRowCount());

    do
    {
        bar.step();

        Field *fields = result->Fetch();
        ScriptInfo tmp;
        tmp.id           = fields[0].GetUInt32();
        tmp.delay        = fields[1].GetUInt32();
        tmp.command      = fields[2].GetUInt32();
        tmp.raw.data[0]  = fields[3].GetUInt32();
        tmp.raw.data[1]  = fields[4].GetUInt32();
        tmp.raw.data[2]  = fields[5].GetUInt32();
        tmp.raw.data[3]  = fields[6].GetUInt32();

        tmp.buddy_id     = fields[7].GetUInt32();
        tmp.buddy_radius = fields[8].GetUInt32();
        tmp.buddy_type   = fields[9].GetUInt8();

        tmp.raw.data[4]  = fields[10].GetUInt32();
        tmp.raw.data[5]  = fields[11].GetInt32();
        tmp.raw.data[6]  = fields[12].GetInt32();
        tmp.raw.data[7]  = fields[13].GetInt32();
        tmp.raw.data[8]  = fields[14].GetInt32();
        tmp.x            = fields[15].GetFloat();
        tmp.y            = fields[16].GetFloat();
        tmp.z            = fields[17].GetFloat();
        tmp.o            = fields[18].GetFloat();

        if (tmp.buddy_id)
        {
            switch (tmp.buddy_type)
            {
                case BUDDY_TYPE_CREATURE_ENTRY:
                {
                    if (!ObjectMgr::GetCreatureTemplate(tmp.buddy_id))
                    {
                        sLog.outErrorDb("Table `%s` has buddy_id = %u for script id %u, but this creature_template does not exist.", tablename, tmp.buddy_id, tmp.id);
                        continue;
                    }
                    if (!tmp.buddy_radius)
                    {
                        sLog.outErrorDb("Table `%s` has buddy_id = %u for script id %u, but search radius is too small (buddy_radius = %u).", tablename, tmp.buddy_id, tmp.id, tmp.buddy_radius);
                        continue;
                    }
                    break;
                }
                case BUDDY_TYPE_CREATURE_GUID:
                {
                    if (!sObjectMgr.GetCreatureData(tmp.buddy_id))
                    {
                        sLog.outErrorDb("Table `%s` has buddy_id = %u for script id %u, but this creature guid does not exist.", tablename, tmp.buddy_id, tmp.id);
                        continue;
                    }
                    break;
                }
                case BUDDY_TYPE_GAMEOBJECT_ENTRY:
                {
                    if (!ObjectMgr::GetGameObjectInfo(tmp.buddy_id))
                    {
                        sLog.outErrorDb("Table `%s` has buddy_id = %u for script id %u, but this gameobject_template does not exist.", tablename, tmp.buddy_id, tmp.id);
                        continue;
                    }
                    break;
                }
                case BUDDY_TYPE_GAMEOBJECT_GUID:
                {
                    GameObjectData const* data = sObjectMgr.GetGOData(tmp.buddy_id);
                    if (!data)
                    {
                        sLog.outErrorDb("Table `%s` has buddy_id = %u for script id %u, but this gameobject guid does not exist.", tablename, tmp.buddy_id, tmp.id);
                        continue;
                    }

                    GameObjectInfo const* info = ObjectMgr::GetGameObjectInfo(data->id);
                    if (!info)
                    {
                        sLog.outErrorDb("Table `%s` has buddy_id = %u for script id %u, but this guid is for a non-existent gameobject entry %u.", tablename, tmp.buddy_id, tmp.id, data->id);
                        continue;
                    }
                    break;
                }
                case BUDDY_TYPE_CREATURE_INSTANCE_DATA:
                case BUDDY_TYPE_GAMEOBJECT_INSTANCE_DATA:
                    break;
                default:
                {
                    sLog.outError("Table `%s` has an unknown buddy_type = %u used for script id %u.", tablename, tmp.buddy_type, tmp.id);
                    break;
                }
            }
        }

        if (!tmp.buddy_id && (tmp.raw.data[4] & SF_GENERAL_SWAP_INITIAL_TARGETS) && (tmp.raw.data[4] & SF_GENERAL_SWAP_FINAL_TARGETS))
        {
            sLog.outErrorDb("Table `%s` has nonsensical flag combination (data_flags = %u) without a buddy for script id %u", tablename, tmp.moveTo.flags, tmp.id);
            continue;
        }

        // generic command args check
        switch (tmp.command)
        {
            case SCRIPT_COMMAND_TALK:
            {
                if (tmp.talk.chatType > CHAT_TYPE_ZONE_YELL)
                {
                    sLog.outErrorDb("Table `%s` has invalid CHAT_TYPE_ (datalong = %u) in SCRIPT_COMMAND_TALK for script id %u", tablename, tmp.talk.chatType, tmp.id);
                    continue;
                }

                if (tmp.talk.textId[0] == 0)
                {
                    sLog.outErrorDb("Table `%s` has invalid talk text id (dataint = %i) in SCRIPT_COMMAND_TALK for script id %u", tablename, tmp.talk.textId[0], tmp.id);
                    continue;
                }

                for (int i = 0; i < MAX_TEXT_ID; ++i)
                {
                    if (tmp.talk.textId[i] < 0)
                    {
                        sLog.outErrorDb("Table `%s` has out of range broadcast text id (dataint = %i, expected positive value) in SCRIPT_COMMAND_TALK for script id %u", tablename, tmp.talk.textId[i], tmp.id);
                        continue;
                    }
                }
                break;
            }
            case SCRIPT_COMMAND_EMOTE:
            {
                if (!sEmotesStore.LookupEntry(tmp.emote.emoteId))
                {
                    sLog.outErrorDb("Table `%s` has invalid emote id (datalong = %u) in SCRIPT_COMMAND_EMOTE for script id %u", tablename, tmp.emote.emoteId, tmp.id);
                    continue;
                }
                break;
            }
            case SCRIPT_COMMAND_MOVE_TO:
            {
                if (tmp.moveTo.coordinatesType >= MOVETO_COORDINATES_MAX)
                {
                    sLog.outErrorDb("Table `%s` has invalid coordinates type (datalong = %u) in SCRIPT_COMMAND_MOVE_TO for script id %u", tablename, tmp.moveTo.coordinatesType, tmp.id);
                    continue;
                }

                // combined flags of MoveOptions enum
                if (tmp.moveTo.movementOptions > 511)
                {
                    sLog.outErrorDb("Table `%s` has invalid movement options (datalong3 = %u) in SCRIPT_COMMAND_MOVE_TO for script id %u", tablename, tmp.moveTo.movementOptions, tmp.id);
                    continue;
                }

                break;
            }
            case SCRIPT_COMMAND_TELEPORT_TO:
            {
                if (!sMapStorage.LookupEntry<MapEntry>(tmp.teleportTo.mapId))
                {
                    sLog.outErrorDb("Table `%s` has invalid map (Id: %u) in SCRIPT_COMMAND_TELEPORT_TO for script id %u", tablename, tmp.teleportTo.mapId, tmp.id);
                    continue;
                }

                if (!MaNGOS::IsValidMapCoord(tmp.x, tmp.y, tmp.z, tmp.o))
                {
                    sLog.outErrorDb("Table `%s` has invalid coordinates (X: %f Y: %f) in SCRIPT_COMMAND_TELEPORT_TO for script id %u", tablename, tmp.x, tmp.y, tmp.id);
                    continue;
                }
                break;
            }
            case SCRIPT_COMMAND_QUEST_EXPLORED:
            {
                Quest const* quest = sObjectMgr.GetQuestTemplate(tmp.questExplored.questId);
                if (!quest)
                {
                    sLog.outErrorDb("Table `%s` has invalid quest (ID: %u) in SCRIPT_COMMAND_QUEST_EXPLORED in `datalong` for script id %u", tablename, tmp.questExplored.questId, tmp.id);
                    continue;
                }

                if (!quest->HasSpecialFlag(QUEST_SPECIAL_FLAG_EXPLORATION_OR_EVENT))
                {
                    sLog.outErrorDb("Table `%s` has quest (ID: %u) in SCRIPT_COMMAND_QUEST_EXPLORED in `datalong` for script id %u, but quest not have flag QUEST_SPECIAL_FLAG_EXPLORATION_OR_EVENT in quest flags. Script command or quest flags wrong. Quest modified to require objective.", tablename, tmp.questExplored.questId, tmp.id);

                    // this will prevent quest completing without objective
                    const_cast<Quest*>(quest)->SetSpecialFlag(QUEST_SPECIAL_FLAG_EXPLORATION_OR_EVENT);

                    // continue; - quest objective requirement set and command can be allowed
                }

                if (float(tmp.questExplored.distance) > DEFAULT_VISIBILITY_DISTANCE)
                {
                    sLog.outErrorDb("Table `%s` has too large distance (%u) for exploring objective complete in `datalong2` in SCRIPT_COMMAND_QUEST_EXPLORED in `datalong` for script id %u",
                                    tablename, tmp.questExplored.distance, tmp.id);
                    continue;
                }

                if (tmp.questExplored.distance && float(tmp.questExplored.distance) > DEFAULT_VISIBILITY_DISTANCE)
                {
                    sLog.outErrorDb("Table `%s` has too large distance (%u) for exploring objective complete in `datalong2` in SCRIPT_COMMAND_QUEST_EXPLORED in `datalong` for script id %u, max distance is %f or 0 for disable distance check",
                                    tablename, tmp.questExplored.distance, tmp.id, DEFAULT_VISIBILITY_DISTANCE);
                    continue;
                }

                if (tmp.questExplored.distance && float(tmp.questExplored.distance) < INTERACTION_DISTANCE)
                {
                    sLog.outErrorDb("Table `%s` has too small distance (%u) for exploring objective complete in `datalong2` in SCRIPT_COMMAND_QUEST_EXPLORED in `datalong` for script id %u, min distance is %f or 0 for disable distance check",
                                    tablename, tmp.questExplored.distance, tmp.id, INTERACTION_DISTANCE);
                    continue;
                }

                break;
            }
            case SCRIPT_COMMAND_KILL_CREDIT:
            {
                if (!ObjectMgr::GetCreatureTemplate(tmp.killCredit.creatureEntry))
                {
                    sLog.outErrorDb("Table `%s` has invalid creature (Entry: %u) in SCRIPT_COMMAND_KILL_CREDIT for script id %u", tablename, tmp.killCredit.creatureEntry, tmp.id);
                    continue;
                }
                break;
            }
            case SCRIPT_COMMAND_RESPAWN_GAMEOBJECT:
            {
                if (tmp.GetGOGuid()) // cant check when using buddy\source\target instead
                {
                    GameObjectData const* data = sObjectMgr.GetGOData(tmp.GetGOGuid());
                    if (!data)
                    {
                        sLog.outErrorDb("Table `%s` has invalid gameobject (GUID: %u) in SCRIPT_COMMAND_RESPAWN_GAMEOBJECT for script id %u", tablename, tmp.GetGOGuid(), tmp.id);
                        continue;
                    }

                    GameObjectInfo const* info = ObjectMgr::GetGameObjectInfo(data->id);
                    if (!info)
                    {
                        sLog.outErrorDb("Table `%s` has gameobject with invalid entry (GUID: %u Entry: %u) in SCRIPT_COMMAND_RESPAWN_GAMEOBJECT for script id %u", tablename, tmp.GetGOGuid(), data->id, tmp.id);
                        continue;
                    }

                    if (info->type == GAMEOBJECT_TYPE_FISHINGNODE ||
                        info->type == GAMEOBJECT_TYPE_FISHINGHOLE ||
                        info->type == GAMEOBJECT_TYPE_DOOR ||
                        info->type == GAMEOBJECT_TYPE_BUTTON ||
                        info->type == GAMEOBJECT_TYPE_TRAP)
                    {
                        sLog.outErrorDb("Table `%s` have gameobject type (%u) unsupported by command SCRIPT_COMMAND_RESPAWN_GAMEOBJECT for script id %u", tablename, info->id, tmp.id);
                        continue;
                    }
                }
                
                break;
            }
            case SCRIPT_COMMAND_TEMP_SUMMON_CREATURE:
            {
                if (!MaNGOS::IsValidMapCoord(tmp.x, tmp.y, tmp.z, tmp.o))
                {
                    sLog.outErrorDb("Table `%s` has invalid coordinates (X: %f Y: %f) in SCRIPT_COMMAND_TEMP_SUMMON_CREATURE for script id %u", tablename, tmp.x, tmp.y, tmp.id);
                    continue;
                }

                if (!ObjectMgr::GetCreatureTemplate(tmp.summonCreature.creatureEntry))
                {
                    sLog.outErrorDb("Table `%s` has invalid creature (Entry: %u) in SCRIPT_COMMAND_TEMP_SUMMON_CREATURE for script id %u", tablename, tmp.summonCreature.creatureEntry, tmp.id);
                    continue;
                }
                break;
            }
            case SCRIPT_COMMAND_OPEN_DOOR:
            case SCRIPT_COMMAND_CLOSE_DOOR:
            {
                if (tmp.GetGOGuid()) // cant check when using buddy\source\target instead
                {
                    GameObjectData const* data = sObjectMgr.GetGOData(tmp.GetGOGuid());
                    if (!data)
                    {
                        sLog.outErrorDb("Table `%s` has invalid gameobject (GUID: %u) in %s for script id %u", tablename, tmp.GetGOGuid(), (tmp.command == SCRIPT_COMMAND_OPEN_DOOR ? "SCRIPT_COMMAND_OPEN_DOOR" : "SCRIPT_COMMAND_CLOSE_DOOR"), tmp.id);
                        continue;
                    }

                    GameObjectInfo const* info = ObjectMgr::GetGameObjectInfo(data->id);
                    if (!info)
                    {
                        sLog.outErrorDb("Table `%s` has gameobject with invalid entry (GUID: %u Entry: %u) in %s for script id %u", tablename, tmp.GetGOGuid(), data->id, (tmp.command == SCRIPT_COMMAND_OPEN_DOOR ? "SCRIPT_COMMAND_OPEN_DOOR" : "SCRIPT_COMMAND_CLOSE_DOOR"), tmp.id);
                        continue;
                    }

                    if (info->type != GAMEOBJECT_TYPE_DOOR)
                    {
                        sLog.outErrorDb("Table `%s` has gameobject type (%u) non supported by command %s for script id %u", tablename, info->id, (tmp.command == SCRIPT_COMMAND_OPEN_DOOR ? "SCRIPT_COMMAND_OPEN_DOOR" : "SCRIPT_COMMAND_CLOSE_DOOR"), tmp.id);
                        continue;
                    }
                }

                break;
            }
            case SCRIPT_COMMAND_REMOVE_AURA:
            {
                if (!sSpellMgr.GetSpellEntry(tmp.removeAura.spellId))
                {
                    sLog.outErrorDb("Table `%s` using nonexistent spell (id: %u) in SCRIPT_COMMAND_REMOVE_AURA or SCRIPT_COMMAND_CAST_SPELL for script id %u",
                                    tablename, tmp.removeAura.spellId, tmp.id);
                    continue;
                }
                break;
            }
            case SCRIPT_COMMAND_CAST_SPELL:
            {
                if (!sSpellMgr.GetSpellEntry(tmp.castSpell.spellId))
                {
                    sLog.outErrorDb("Table `%s` using nonexistent spell (id: %u) in SCRIPT_COMMAND_REMOVE_AURA or SCRIPT_COMMAND_CAST_SPELL for script id %u",
                                    tablename, tmp.castSpell.spellId, tmp.id);
                    continue;
                }
                if (tmp.castSpell.flags & ~0x3)
                {
                    sLog.outErrorDb("Table `%s` using unknown flags in datalong2 (%u)i n SCRIPT_COMMAND_CAST_SPELL for script id %u",
                                    tablename, tmp.castSpell.flags, tmp.id);
                    continue;
                }
                break;
            }
            case SCRIPT_COMMAND_CREATE_ITEM:
            {
                if (!ObjectMgr::GetItemPrototype(tmp.createItem.itemEntry))
                {
                    sLog.outErrorDb("Table `%s` has nonexistent item (entry: %u) in SCRIPT_COMMAND_CREATE_ITEM for script id %u",
                                    tablename, tmp.createItem.itemEntry, tmp.id);
                    continue;
                }
                if (!tmp.createItem.amount)
                {
                    sLog.outErrorDb("Table `%s` SCRIPT_COMMAND_CREATE_ITEM but amount is %u for script id %u",
                                    tablename, tmp.createItem.amount, tmp.id);
                    continue;
                }
                break;
            }
            case SCRIPT_COMMAND_DESPAWN_CREATURE:
            {
                break;
            }
            case SCRIPT_COMMAND_SET_EQUIPMENT:
            {
                bool cancel = false;
                for (int i = 0; i < 3; i++)
                {
                    if (tmp.setEquipment.slot[i] > 0)
                    {
                        if (!ObjectMgr::GetItemPrototype(tmp.setEquipment.slot[i]))
                        {
                            sLog.outErrorDb("Table `%s` has nonexistent item (dataint%i: %u) in SCRIPT_COMMAND_SET_EQUIPMENT for script id %u",
                                tablename, i, tmp.setEquipment.slot[i], tmp.id);
                            cancel=true;
                        }
                    }
                }
                
                if (cancel)
                    continue;

                break;
            }
            case SCRIPT_COMMAND_MOVEMENT:
            {
                switch (tmp.movement.movementType)
                {
                    case IDLE_MOTION_TYPE:
                    case RANDOM_MOTION_TYPE:
                    case WAYPOINT_MOTION_TYPE:
                    case CONFUSED_MOTION_TYPE:
                    case CHASE_MOTION_TYPE:
                    case HOME_MOTION_TYPE:
                    case FLEEING_MOTION_TYPE:
                    case DISTRACT_MOTION_TYPE:
                    case FOLLOW_MOTION_TYPE:
                    case CHARGE_MOTION_TYPE:
                        break;
                    default:
                    {
                        sLog.outErrorDb("Table `%s` SCRIPT_COMMAND_MOVEMENT has invalid MovementType %u for script id %u",
                            tablename, tmp.movement.movementType, tmp.id);
                        continue;
                    }
                }

                if (tmp.movement.boolParam > 1)
                {
                    sLog.outErrorDb("Table `%s` SCRIPT_COMMAND_MOVEMENT has wrong value in datalong2=%u (must be bool 0/1) for script id %u",
                                    tablename, tmp.movement.boolParam, tmp.id);
                    continue;
                }

                break;
            }
            case SCRIPT_COMMAND_SET_ACTIVEOBJECT:
            {
                break;
            }
            case SCRIPT_COMMAND_SET_FACTION:
            {
                if (tmp.faction.factionId && !sFactionStore.LookupEntry(tmp.faction.factionId))
                {
                    sLog.outErrorDb("Table `%s` has datalong2 = %u in SCRIPT_COMMAND_SET_FACTION for script id %u, but this faction does not exist.", tablename, tmp.faction.factionId, tmp.id);
                    continue;
                }

                break;
            }
            case SCRIPT_COMMAND_MORPH_TO_ENTRY_OR_MODEL:
            {
                if (tmp.morph.isDisplayId)
                {
                    if (tmp.morph.creatureOrModelEntry && !sCreatureDisplayInfoStore.LookupEntry(tmp.morph.creatureOrModelEntry))
                    {
                        sLog.outErrorDb("Table `%s` has datalong = %u in SCRIPT_COMMAND_MORPH_TO_ENTRY_OR_MODEL for script id %u, but this model does not exist.", tablename, tmp.morph.creatureOrModelEntry, tmp.id);
                        continue;
                    }
                }
                else
                {
                    if (tmp.morph.creatureOrModelEntry && !ObjectMgr::GetCreatureTemplate(tmp.morph.creatureOrModelEntry))
                    {
                        sLog.outErrorDb("Table `%s` has datalong = %u in SCRIPT_COMMAND_MORPH_TO_ENTRY_OR_MODEL for script id %u, but this creature_template does not exist.", tablename, tmp.morph.creatureOrModelEntry, tmp.id);
                        continue;
                    }
                }

                break;
            }
            case SCRIPT_COMMAND_MOUNT_TO_ENTRY_OR_MODEL:
            {
                if (tmp.mount.isDisplayId)
                {
                    if (tmp.mount.creatureOrModelEntry && !sCreatureDisplayInfoStore.LookupEntry(tmp.mount.creatureOrModelEntry))
                    {
                        sLog.outErrorDb("Table `%s` has datalong2 = %u in SCRIPT_COMMAND_MOUNT_TO_ENTRY_OR_MODEL for script id %u, but this model does not exist.", tablename, tmp.mount.creatureOrModelEntry, tmp.id);
                        continue;
                    }
                }
                else
                {
                    if (tmp.mount.creatureOrModelEntry && !ObjectMgr::GetCreatureTemplate(tmp.mount.creatureOrModelEntry))
                    {
                        sLog.outErrorDb("Table `%s` has datalong2 = %u in SCRIPT_COMMAND_MOUNT_TO_ENTRY_OR_MODEL for script id %u, but this creature_template does not exist.", tablename, tmp.mount.creatureOrModelEntry, tmp.id);
                        continue;
                    }
                }

                break;
            }
            case SCRIPT_COMMAND_SET_RUN:
            {
                break;
            }
            case SCRIPT_COMMAND_ATTACK_START:
            {
                break;
            }
            case SCRIPT_COMMAND_GO_LOCK_STATE:
            {
                if (// lock(0x01) and unlock(0x02) together
                    ((tmp.goLockState.lockState & SF_GOLOCKSTATE_LOCK) && (tmp.goLockState.lockState & SF_GOLOCKSTATE_UNLOCK)) ||
                    // non-interact (0x4) and interact (0x08) together
                    ((tmp.goLockState.lockState & SF_GOLOCKSTATE_NO_INTERACT) && (tmp.goLockState.lockState & SF_GOLOCKSTATE_INTERACT)) ||
                    // no setting
                    !tmp.goLockState.lockState ||
                    // invalid number
                    tmp.goLockState.lockState >= SF_GOLOCKSTATE_MAX)
                {
                    sLog.outErrorDb("Table `%s` has invalid lock state (datalong = %u) in SCRIPT_COMMAND_GO_LOCK_STATE for script id %u.", tablename, tmp.goLockState.lockState, tmp.id);
                    continue;
                }
                break;
            }
            case SCRIPT_COMMAND_STAND_STATE:
            {
                if (tmp.standState.stand_state >= MAX_UNIT_STAND_STATE)
                {
                    sLog.outErrorDb("Table `%s` has invalid stand state (datalong = %u) in SCRIPT_COMMAND_STAND_STATE for script id %u", tablename, tmp.standState.stand_state, tmp.id);
                    continue;
                }
                break;
            }
            case SCRIPT_COMMAND_MODIFY_NPC_FLAGS:
            {
                break;
            }
            case SCRIPT_COMMAND_SEND_TAXI_PATH:
            {
                if (!sTaxiPathStore.LookupEntry(tmp.sendTaxiPath.taxiPathId))
                {
                    sLog.outErrorDb("Table `%s` has datalong = %u in SCRIPT_COMMAND_SEND_TAXI_PATH for script id %u, but this taxi path does not exist.", tablename, tmp.sendTaxiPath.taxiPathId, tmp.id);
                    continue;
                }
                break;
            }
            case SCRIPT_COMMAND_TERMINATE_SCRIPT:
            {
                if (tmp.terminateScript.creatureEntry && !ObjectMgr::GetCreatureTemplate(tmp.terminateScript.creatureEntry))
                {
                    sLog.outErrorDb("Table `%s` has datalong = %u in SCRIPT_COMMAND_TERMINATE_SCRIPT for script id %u, but this npc entry does not exist.", tablename, tmp.terminateScript.creatureEntry, tmp.id);
                    continue;
                }
                if (tmp.terminateScript.creatureEntry && !tmp.terminateScript.searchRadius)
                {
                    sLog.outErrorDb("Table `%s` has datalong = %u in  SCRIPT_COMMAND_TERMINATE_SCRIPT for script id %u, but search radius is too small (datalong2 = %u).", tablename, tmp.terminateScript.creatureEntry, tmp.id, tmp.terminateScript.searchRadius);
                    continue;
                }
                break;
            }
            case SCRIPT_COMMAND_TERMINATE_CONDITION:
            {
                if (!sConditionStorage.LookupEntry<PlayerCondition>(tmp.terminateCond.conditionId))
                {
                    sLog.outErrorDb("Table `%s` has datalong = %u in SCRIPT_COMMAND_TERMINATE_CONDITION for script id %u, but this condition_id does not exist.", tablename, tmp.terminateCond.conditionId, tmp.id);
                    continue;
                }
                if (tmp.terminateCond.failQuest && !sObjectMgr.GetQuestTemplate(tmp.terminateCond.failQuest))
                {
                    sLog.outErrorDb("Table `%s` has datalong2 = %u in SCRIPT_COMMAND_TERMINATE_CONDITION for script id %u, but this questId does not exist.", tablename, tmp.terminateCond.failQuest, tmp.id);
                    continue;
                }
                break;
            }
            case SCRIPT_COMMAND_ENTER_EVADE_MODE:
            {
                break;
            }
            case SCRIPT_COMMAND_SET_HOME_POSITION:
            {
                break;
            }
            case SCRIPT_COMMAND_TURN_TO:
            {
                if (tmp.turnTo.facingLogic > 1)
                {
                    sLog.outErrorDb("Table `%s` using unknown option in datalong (%u) in SCRIPT_COMMAND_TURN_TO for script id %u", tablename, tmp.turnTo.facingLogic, tmp.id);
                    continue;
                }
                break;
            }
            case SCRIPT_COMMAND_MEETINGSTONE:
            {
                if (!sAreaStorage.LookupEntry<AreaEntry>(tmp.meetingstone.areaId))
                {
                    sLog.outErrorDb("Table `%s` has datalong = %u in  SCRIPT_COMMAND_MEETINGSTONE for script id %u, but there is no area with this Id.", tablename, tmp.meetingstone.areaId, tmp.id);
                    continue;
                }
                break;
            }
            case SCRIPT_COMMAND_SET_INST_DATA:
            {
                if (tmp.setData.type >= SO_INSTDATA_MAX)
                {
                    sLog.outErrorDb("Table `%s` has invalid option datalong3 = %u in SCRIPT_COMMAND_SET_INST_DATA for script id %u.", tablename, tmp.setData.type, tmp.id);
                    continue;
                }
                break;
            }
            case SCRIPT_COMMAND_SET_INST_DATA64:
            {
                if (tmp.setData64.type >= SO_INSTDATA64_MAX)
                {
                    sLog.outErrorDb("Table `%s` has invalid option datalong3 = %u in SCRIPT_COMMAND_SET_INST_DATA64 for script id %u.", tablename, tmp.setData64.type, tmp.id);
                    continue;
                }
                break;
            }
        }

        if (scripts.find(tmp.id) == scripts.end())
        {
            ScriptMap emptyMap;
            scripts[tmp.id] = emptyMap;
        }
        scripts[tmp.id].insert(ScriptMap::value_type(tmp.delay, tmp));

        ++count;
    }
    while (result->NextRow());

    delete result;

    sLog.outString();
    sLog.outString(">> Loaded %u script definitions", count);
}

void ScriptMgr::LoadGameObjectScripts()
{
    LoadScripts(sGameObjectScripts, "gameobject_scripts");

    // check ids
    for (ScriptMapMap::const_iterator itr = sGameObjectScripts.begin(); itr != sGameObjectScripts.end(); ++itr)
    {
        if (!sObjectMgr.GetGOData(itr->first))
            sLog.outErrorDb("Table `gameobject_scripts` has not existing gameobject (GUID: %u) as script id", itr->first);
    }
}

void ScriptMgr::LoadQuestEndScripts()
{
    LoadScripts(sQuestEndScripts, "quest_end_scripts");

    // check ids
    for (ScriptMapMap::const_iterator itr = sQuestEndScripts.begin(); itr != sQuestEndScripts.end(); ++itr)
    {
        if (!sObjectMgr.GetQuestTemplate(itr->first) && (sWorld.GetWowPatch()==WOW_PATCH_112))
            sLog.outErrorDb("Table `quest_end_scripts` has not existing quest (Id: %u) as script id", itr->first);
    }
}

void ScriptMgr::LoadQuestStartScripts()
{
    LoadScripts(sQuestStartScripts, "quest_start_scripts");

    // check ids
    for (ScriptMapMap::const_iterator itr = sQuestStartScripts.begin(); itr != sQuestStartScripts.end(); ++itr)
    {
        if (!sObjectMgr.GetQuestTemplate(itr->first) && (sWorld.GetWowPatch() == WOW_PATCH_112))
            sLog.outErrorDb("Table `quest_start_scripts` has not existing quest (Id: %u) as script id", itr->first);
    }
}

void ScriptMgr::LoadSpellScripts()
{
    LoadScripts(sSpellScripts, "spell_scripts");

    // check ids
    for (ScriptMapMap::const_iterator itr = sSpellScripts.begin(); itr != sSpellScripts.end(); ++itr)
    {
        SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(itr->first);

        if (!spellInfo)
        {
            sLog.outErrorDb("Table `spell_scripts` has not existing spell (Id: %u) as script id", itr->first);
            continue;
        }

        //check for correct spellEffect
        bool found = false;
        for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
        {
            // skip empty effects
            if (!spellInfo->Effect[i])
                continue;

            if (spellInfo->Effect[i] == SPELL_EFFECT_SCRIPT_EFFECT)
            {
                found =  true;
                break;
            }
        }

        if (!found)
            sLog.outErrorDb("Table `spell_scripts` has unsupported spell (Id: %u) without SPELL_EFFECT_SCRIPT_EFFECT (%u) spell effect", itr->first, SPELL_EFFECT_SCRIPT_EFFECT);
    }
}

void ScriptMgr::LoadEventScripts()
{
    LoadScripts(sEventScripts, "event_scripts");

    std::set<uint32> eventIds;                              // Store possible event ids

    CollectPossibleEventIds(eventIds);

    // Then check if all scripts are in above list of possible script entries
    for (ScriptMapMap::const_iterator itr = sEventScripts.begin(); itr != sEventScripts.end(); ++itr)
    {
        std::set<uint32>::const_iterator itr2 = eventIds.find(itr->first);
        if (itr2 == eventIds.end())
            sLog.outErrorDb("Table `event_scripts` has script (Id: %u) not referring to any gameobject_template type 10 data2 field, type 3 data6 field, type 13 data 2 field, type 29 or any spell effect %u",
                            itr->first, SPELL_EFFECT_SEND_EVENT);
    }
}

void ScriptMgr::LoadCreatureSpellScripts()
{
    LoadScripts(sCreatureSpellScripts, "creature_spells_scripts");

    // checks are done in LoadCreatureSpells
}

void ScriptMgr::LoadGossipScripts()
{
    LoadScripts(sGossipScripts, "gossip_scripts");

    // checks are done in LoadGossipMenuItems
}

void ScriptMgr::LoadCreatureMovementScripts()
{
    LoadScripts(sCreatureMovementScripts, "creature_movement_scripts");

    // checks are done in WaypointManager::Load
}

void ScriptMgr::CheckAllScriptTexts()
{
    CheckScriptTexts(sQuestEndScripts);
    CheckScriptTexts(sQuestStartScripts);
    CheckScriptTexts(sSpellScripts);
    CheckScriptTexts(sCreatureSpellScripts);
    CheckScriptTexts(sGameObjectScripts);
    CheckScriptTexts(sEventScripts);
    CheckScriptTexts(sGossipScripts);
    CheckScriptTexts(sCreatureMovementScripts);

    sWaypointMgr.CheckTextsExistance();
}

void ScriptMgr::CheckScriptTexts(ScriptMapMap const& scripts)
{
    for (ScriptMapMap::const_iterator itrMM = scripts.begin(); itrMM != scripts.end(); ++itrMM)
    {
        for (ScriptMap::const_iterator itrM = itrMM->second.begin(); itrM != itrMM->second.end(); ++itrM)
        {
            if (itrM->second.command == SCRIPT_COMMAND_TALK)
            {
                for (int i = 0; i < MAX_TEXT_ID; ++i)
                {
                    if (itrM->second.talk.textId[i] && !sObjectMgr.GetBroadcastTextLocale(itrM->second.talk.textId[i]))
                        sLog.outErrorDb("Table `broadcast_text` is missing text id %u, used in database script id %u.", itrM->second.talk.textId[i], itrMM->first);
                }
            }
        }
    }
}

void ScriptMgr::LoadAreaTriggerScripts()
{
    m_AreaTriggerScripts.clear();                           // need for reload case
    QueryResult *result = WorldDatabase.Query("SELECT entry, ScriptName FROM scripted_areatrigger");

    uint32 count = 0;

    if (!result)
    {
        BarGoLink bar(1);
        bar.step();

        sLog.outString();
        sLog.outString(">> Loaded %u scripted areatrigger", count);
        return;
    }

    BarGoLink bar(result->GetRowCount());

    do
    {
        ++count;
        bar.step();

        Field *fields = result->Fetch();

        uint32 triggerId       = fields[0].GetUInt32();
        const char *scriptName = fields[1].GetString();

        if (!sAreaTriggerStore.LookupEntry(triggerId))
        {
            sLog.outErrorDb("Table `scripted_areatrigger` has area trigger (ID: %u) not listed in `AreaTrigger.dbc`.", triggerId);
            continue;
        }

        m_AreaTriggerScripts[triggerId] = GetScriptId(scriptName);
    }
    while (result->NextRow());

    delete result;

    sLog.outString();
    sLog.outString(">> Loaded %u areatrigger scripts", count);
}

void ScriptMgr::LoadEventIdScripts()
{
    m_EventIdScripts.clear();                           // need for reload case
    QueryResult *result = WorldDatabase.Query("SELECT id, ScriptName FROM scripted_event_id");

    uint32 count = 0;

    if (!result)
    {
        BarGoLink bar(1);
        bar.step();

        sLog.outString();
        sLog.outString(">> Loaded %u scripted event id", count);
        return;
    }

    BarGoLink bar(result->GetRowCount());

    std::set<uint32> eventIds;                              // Store possible event ids

    CollectPossibleEventIds(eventIds);

    do
    {
        ++count;
        bar.step();

        Field *fields = result->Fetch();

        uint32 eventId          = fields[0].GetUInt32();
        const char *scriptName  = fields[1].GetString();

        std::set<uint32>::const_iterator itr = eventIds.find(eventId);
        if (itr == eventIds.end())
            sLog.outErrorDb("Table `scripted_event_id` has id %u not referring to any gameobject_template type 10 data2 field, type 3 data6 field, type 13 data 2 field, type 29 or any spell effect %u or path taxi node data",
                            eventId, SPELL_EFFECT_SEND_EVENT);

        m_EventIdScripts[eventId] = GetScriptId(scriptName);
    }
    while (result->NextRow());

    delete result;

    sLog.outString();
    sLog.outString(">> Loaded %u scripted event id", count);
}

void ScriptMgr::LoadScriptNames()
{
    m_scriptNames.push_back("");
    QueryResult *result = WorldDatabase.Query(
                              "SELECT DISTINCT(ScriptName) FROM creature_template WHERE ScriptName <> '' "
                              "UNION "
                              "SELECT DISTINCT(ScriptName) FROM gameobject_template WHERE ScriptName <> '' "
                              "UNION "
                              "SELECT DISTINCT(ScriptName) FROM item_template WHERE ScriptName <> '' "
                              "UNION "
                              "SELECT DISTINCT(ScriptName) FROM scripted_areatrigger WHERE ScriptName <> '' "
                              "UNION "
                              "SELECT DISTINCT(ScriptName) FROM scripted_event_id WHERE ScriptName <> '' "
                              "UNION "
                              "SELECT DISTINCT(ScriptName) FROM map_template WHERE ScriptName <> ''");

    if (!result)
    {
        BarGoLink bar(1);
        bar.step();
        sLog.outString();
        sLog.outErrorDb(">> Loaded empty set of Script Names!");
        return;
    }

    BarGoLink bar(result->GetRowCount());
    uint32 count = 0;

    do
    {
        bar.step();
        m_scriptNames.push_back((*result)[0].GetString());
        ++count;
    }
    while (result->NextRow());
    delete result;

    std::sort(m_scriptNames.begin(), m_scriptNames.end());
    sLog.outString();
    sLog.outString(">> Loaded %d Script Names", count);
}

uint32 ScriptMgr::GetScriptId(const char *name) const
{
    // use binary search to find the script name in the sorted vector
    // assume "" is the first element
    if (!name)
        return 0;

    ScriptNameMap::const_iterator itr =
        std::lower_bound(m_scriptNames.begin(), m_scriptNames.end(), name);

    if (itr == m_scriptNames.end() || *itr != name)
        return 0;

    return uint32(itr - m_scriptNames.begin());
}

uint32 ScriptMgr::GetAreaTriggerScriptId(uint32 triggerId) const
{
    AreaTriggerScriptMap::const_iterator itr = m_AreaTriggerScripts.find(triggerId);
    if (itr != m_AreaTriggerScripts.end())
        return itr->second;

    return 0;
}

uint32 ScriptMgr::GetEventIdScriptId(uint32 eventId) const
{
    EventIdScriptMap::const_iterator itr = m_EventIdScripts.find(eventId);
    if (itr != m_EventIdScripts.end())
        return itr->second;

    return 0;
}

CreatureAI* ScriptMgr::GetCreatureAI(Creature* pCreature)
{
    Script* pTempScript = m_scripts[pCreature->GetScriptId()];

    if (!pTempScript || !pTempScript->GetAI)
        return nullptr;

    return pTempScript->GetAI(pCreature);
}

GameObjectAI* ScriptMgr::GetGameObjectAI(GameObject* pGobj)
{
    Script* pTempScript = m_scripts[pGobj->GetGOInfo()->ScriptId];

    if (!pTempScript || !pTempScript->GOGetAI)
        return nullptr;

    return pTempScript->GOGetAI(pGobj);
}

InstanceData* ScriptMgr::CreateInstanceData(Map* pMap)
{
    Script* pTempScript = m_scripts[pMap->GetScriptId()];

    if (!pTempScript || !pTempScript->GetInstanceData)
        return nullptr;

    return pTempScript->GetInstanceData(pMap);
}

bool ScriptMgr::OnGossipHello(Player* pPlayer, Creature* pCreature)
{
    Script* pTempScript = m_scripts[pCreature->GetScriptId()];

    if (!pTempScript || !pTempScript->pGossipHello)
        return false;

    pPlayer->PlayerTalkClass->ClearMenus();

    return pTempScript->pGossipHello(pPlayer, pCreature);
}

bool ScriptMgr::OnGossipHello(Player* pPlayer, GameObject* pGameObject)
{
    Script* pTempScript = m_scripts[pGameObject->GetGOInfo()->ScriptId];

    if (!pTempScript || !pTempScript->pGOGossipHello)
        return false;

    pPlayer->PlayerTalkClass->ClearMenus();

    return pTempScript->pGOGossipHello(pPlayer, pGameObject);
}

bool ScriptMgr::OnGossipSelect(Player* pPlayer, Creature* pCreature, uint32 sender, uint32 action, const char* code)
{
    sLog.outDebug("Gossip selection%s, sender: %d, action: %d", code ? " with code" : "", sender, action);

    Script* pTempScript = m_scripts[pCreature->GetScriptId()];

    if (code)
    {
        if (pTempScript && pTempScript->pGossipSelectWithCode)
        {
            pPlayer->PlayerTalkClass->ClearMenus();
            return pTempScript->pGossipSelectWithCode(pPlayer, pCreature, sender, action, code);
        }
    }
    else
    {
        if (pTempScript && pTempScript->pGossipSelect)
        {
            pPlayer->PlayerTalkClass->ClearMenus();
            return pTempScript->pGossipSelect(pPlayer, pCreature, sender, action);
        }
    }

    return false;
}

bool ScriptMgr::OnGossipSelect(Player* pPlayer, GameObject* pGameObject, uint32 sender, uint32 action, const char* code)
{
    sLog.outDebug("Gossip selection%s, sender: %d, action: %d", code ? " with code" : "", sender, action);

    Script* pTempScript = m_scripts[pGameObject->GetGOInfo()->ScriptId];

    if (code)
    {
        if (pTempScript && pTempScript->pGOGossipSelectWithCode)
        {
            pPlayer->PlayerTalkClass->ClearMenus();
            return pTempScript->pGOGossipSelectWithCode(pPlayer, pGameObject, sender, action, code);
        }
    }
    else
    {
        if (pTempScript && pTempScript->pGOGossipSelect)
        {
            pPlayer->PlayerTalkClass->ClearMenus();
            return pTempScript->pGOGossipSelect(pPlayer, pGameObject, sender, action);
        }
    }

    return false;
}

bool ScriptMgr::OnQuestAccept(Player* pPlayer, Creature* pCreature, Quest const* pQuest)
{
    Script* pTempScript = m_scripts[pCreature->GetScriptId()];

    if (!pTempScript || !pTempScript->pQuestAcceptNPC)
        return false;

    pPlayer->PlayerTalkClass->ClearMenus();

    return pTempScript->pQuestAcceptNPC(pPlayer, pCreature, pQuest);
}

bool ScriptMgr::OnQuestAccept(Player* pPlayer, GameObject* pGameObject, Quest const* pQuest)
{
    Script* pTempScript = m_scripts[pGameObject->GetGOInfo()->ScriptId];

    if (!pTempScript || !pTempScript->pGOQuestAccept)
        return false;

    pPlayer->PlayerTalkClass->ClearMenus();

    return pTempScript->pGOQuestAccept(pPlayer, pGameObject, pQuest);
}

bool ScriptMgr::OnQuestAccept(Player* pPlayer, Item* pItem, Quest const* pQuest)
{
    Script* pTempScript = m_scripts[pItem->GetProto()->ScriptId];

    if (!pTempScript || !pTempScript->pItemHello)
        return false;

    pPlayer->PlayerTalkClass->ClearMenus();

    return pTempScript->pItemHello(pPlayer, pItem, pQuest);
}

bool ScriptMgr::OnQuestRewarded(Player* pPlayer, Creature* pCreature, Quest const* pQuest)
{
    Script* pTempScript = m_scripts[pCreature->GetScriptId()];

    if (!pTempScript || !pTempScript->pQuestRewardedNPC)
        return false;

    pPlayer->PlayerTalkClass->ClearMenus();

    return pTempScript->pQuestRewardedNPC(pPlayer, pCreature, pQuest);
}

bool ScriptMgr::OnQuestRewarded(Player* pPlayer, GameObject* pGameObject, Quest const* pQuest)
{
    Script* pTempScript = m_scripts[pGameObject->GetGOInfo()->ScriptId];

    if (!pTempScript || !pTempScript->pQuestRewardedGO)
        return false;

    pPlayer->PlayerTalkClass->ClearMenus();

    return pTempScript->pQuestRewardedGO(pPlayer, pGameObject, pQuest);
}

uint32 ScriptMgr::GetDialogStatus(Player* pPlayer, Creature* pCreature)
{
    Script* pTempScript = m_scripts[pCreature->GetScriptId()];

    if (!pTempScript || !pTempScript->pNPCDialogStatus)
        return DIALOG_STATUS_UNDEFINED;

    pPlayer->PlayerTalkClass->ClearMenus();

    return pTempScript->pNPCDialogStatus(pPlayer, pCreature);
}

uint32 ScriptMgr::GetDialogStatus(Player* pPlayer, GameObject* pGameObject)
{
    Script* pTempScript = m_scripts[pGameObject->GetGOInfo()->ScriptId];

    if (!pTempScript || !pTempScript->pGODialogStatus)
        return DIALOG_STATUS_UNDEFINED;

    pPlayer->PlayerTalkClass->ClearMenus();

    return pTempScript->pGODialogStatus(pPlayer, pGameObject);
}

bool ScriptMgr::OnGameObjectOpen(Player* pPlayer, GameObject* pGameObject)
{
    Script* pTempScript = m_scripts[pGameObject->GetGOInfo()->ScriptId];

    if (!pTempScript || !pTempScript->GOOpen)
        return false;

    return pTempScript->GOOpen(pPlayer, pGameObject);
}

bool ScriptMgr::OnGameObjectUse(Player* pPlayer, GameObject* pGameObject)
{
    Script* pTempScript = m_scripts[pGameObject->GetGOInfo()->ScriptId];

    if (!pTempScript || !pTempScript->pGOHello)
        return false;

    pPlayer->PlayerTalkClass->ClearMenus();

    return pTempScript->pGOHello(pPlayer, pGameObject);
}

bool ScriptMgr::OnItemUse(Player* pPlayer, Item* pItem, SpellCastTargets const& targets)
{
    Script* pTempScript = m_scripts[pItem->GetProto()->ScriptId];

    if (!pTempScript || !pTempScript->pItemUse)
        return false;

    return pTempScript->pItemUse(pPlayer, pItem, targets);
}

bool ScriptMgr::OnAreaTrigger(Player* pPlayer, AreaTriggerEntry const* atEntry)
{
    Script* pTempScript = m_scripts[GetAreaTriggerScriptId(atEntry->id)];

    if (!pTempScript || !pTempScript->pAreaTrigger)
        return false;

    return pTempScript->pAreaTrigger(pPlayer, atEntry);
}

bool ScriptMgr::OnProcessEvent(uint32 eventId, Object* pSource, Object* pTarget, bool isStart)
{
    Script* pTempScript = m_scripts[GetEventIdScriptId(eventId)];

    if (!pTempScript || !pTempScript->pProcessEventId)
        return false;

    // isStart may be false, when event is from taxi node events (arrival=false, departure=true)
    return pTempScript->pProcessEventId(eventId, pSource, pTarget, isStart);
}

bool ScriptMgr::OnEffectDummy(Unit* pCaster, uint32 spellId, SpellEffectIndex effIndex, Creature* pTarget)
{
    Script* pTempScript = m_scripts[pTarget->GetScriptId()];

    if (!pTempScript || !pTempScript->pEffectDummyCreature)
        return false;

    return pTempScript->pEffectDummyCreature(pCaster, spellId, effIndex, pTarget);
}

bool ScriptMgr::OnEffectDummy(Unit* pCaster, uint32 spellId, SpellEffectIndex effIndex, GameObject* pTarget)
{
    Script* pTempScript = m_scripts[pTarget->GetGOInfo()->ScriptId];

    if (!pTempScript || !pTempScript->pEffectDummyGameObj)
        return false;

    return pTempScript->pEffectDummyGameObj(pCaster, spellId, effIndex, pTarget);
}

bool ScriptMgr::OnEffectDummy(Unit* pCaster, uint32 spellId, SpellEffectIndex effIndex, Item* pTarget)
{
    Script* pTempScript = m_scripts[pTarget->GetProto()->ScriptId];

    if (!pTempScript || !pTempScript->pEffectDummyItem)
        return false;

    return pTempScript->pEffectDummyItem(pCaster, spellId, effIndex, pTarget);
}

bool ScriptMgr::OnAuraDummy(Aura const* pAura, bool apply)
{
    Script* pTempScript = m_scripts[((Creature*)pAura->GetTarget())->GetScriptId()];

    if (!pTempScript || !pTempScript->pEffectAuraDummy)
        return false;

    return pTempScript->pEffectAuraDummy(pAura, apply);
}

uint32 GetAreaTriggerScriptId(uint32 triggerId)
{
    return sScriptMgr.GetAreaTriggerScriptId(triggerId);
}

uint32 GetEventIdScriptId(uint32 eventId)
{
    return sScriptMgr.GetEventIdScriptId(eventId);
}

uint32 GetScriptId(const char *name)
{
    return sScriptMgr.GetScriptId(name);
}

char const* GetScriptName(uint32 id)
{
    return sScriptMgr.GetScriptName(id);
}

uint32 GetScriptIdsCount()
{
    return sScriptMgr.GetScriptIdsCount();
}

void ScriptMgr::Initialize()
{
    // Load database (must be called after SD2Config.SetSource).
    LoadDatabase();

    sLog.outString("Loading C++ scripts");
    BarGoLink bar(1);
    bar.step();
    sLog.outString("");

    // Resize script ids to needed ammount of assigned ScriptNames (from core)
    m_scripts.resize(GetScriptIdsCount(), nullptr);

    FillSpellSummary();

    AddScripts();

    // Check existance scripts for all registered by core script names
    for (uint32 i = 1; i < GetScriptIdsCount(); ++i)
    {
        if (!m_scripts[i])
            sLog.outError("No script found for ScriptName '%s'.", GetScriptName(i));
    }

    sLog.outString(">> Loaded %i C++ Scripts.", num_sc_scripts);
}

void ScriptMgr::LoadDatabase()
{
    LoadScriptTexts();
    LoadScriptTextsCustom();
    LoadScriptGossipTexts();
    LoadScriptWaypoints();
    LoadEscortData();
}

void ScriptMgr::LoadScriptTexts()
{
    sLog.outString("Loading Script Texts...");
    LoadMangosStrings(WorldDatabase, "script_texts", TEXT_SOURCE_TEXT_START, TEXT_SOURCE_TEXT_END, true);

    QueryResult* result = WorldDatabase.PQuery("SELECT entry, sound, type, language, emote FROM script_texts WHERE entry BETWEEN %i AND %i", TEXT_SOURCE_GOSSIP_END, TEXT_SOURCE_TEXT_START);

    sLog.outString("Loading Script Texts additional data...");

    if (result)
    {
        BarGoLink bar(result->GetRowCount());
        uint32 uiCount = 0;

        do
        {
            bar.step();
            Field* pFields = result->Fetch();
            StringTextData pTemp;

            int32 iId           = pFields[0].GetInt32();
            pTemp.SoundId     = pFields[1].GetUInt32();
            pTemp.Type        = pFields[2].GetUInt32();
            pTemp.Language    = pFields[3].GetUInt32();
            pTemp.Emote       = pFields[4].GetUInt32();

            if (iId >= 0)
            {
                sLog.outErrorDb("Entry %i in table `script_texts` is not a negative value.", iId);
                continue;
            }

            if (pTemp.SoundId)
            {
                if (!GetSoundEntriesStore()->LookupEntry(pTemp.SoundId))
                    sLog.outErrorDb("Entry %i in table `script_texts` has soundId %u but sound does not exist.", iId, pTemp.SoundId);
            }

            if (!GetLanguageDescByID(pTemp.Language))
                sLog.outErrorDb("Entry %i in table `script_texts` using Language %u but Language does not exist.", iId, pTemp.Language);

            if (pTemp.Type > CHAT_TYPE_ZONE_YELL)
                sLog.outErrorDb("Entry %i in table `script_texts` has Type %u but this Chat Type does not exist.", iId, pTemp.Type);

            m_mTextDataMap[iId] = pTemp;
            ++uiCount;
        } while (result->NextRow());

        delete result;

        sLog.outString("");
        sLog.outString(">> Loaded %u additional Script Texts data.", uiCount);
    }
    else
    {
        BarGoLink bar(1);
        bar.step();
        sLog.outString("");
        sLog.outString(">> Loaded 0 additional Script Texts data. DB table `script_texts` is empty.");
    }
}

void ScriptMgr::LoadScriptTextsCustom()
{
    sLog.outString("Loading Custom Texts...");
    LoadMangosStrings(WorldDatabase, "custom_texts", TEXT_SOURCE_CUSTOM_START, TEXT_SOURCE_CUSTOM_END, true);

    QueryResult* result = WorldDatabase.PQuery("SELECT entry, sound, type, language, emote FROM custom_texts WHERE entry BETWEEN %i AND %i", TEXT_SOURCE_CUSTOM_END, TEXT_SOURCE_CUSTOM_START);

    sLog.outString("Loading Custom Texts additional data...");

    if (result)
    {
        BarGoLink bar(result->GetRowCount());
        uint32 uiCount = 0;

        do
        {
            bar.step();
            Field* pFields = result->Fetch();
            StringTextData pTemp;

            int32 iId              = pFields[0].GetInt32();
            pTemp.SoundId        = pFields[1].GetUInt32();
            pTemp.Type           = pFields[2].GetUInt32();
            pTemp.Language       = pFields[3].GetUInt32();
            pTemp.Emote          = pFields[4].GetUInt32();

            if (iId >= 0)
            {
                sLog.outErrorDb("Entry %i in table `custom_texts` is not a negative value.", iId);
                continue;
            }

            if (pTemp.SoundId)
            {
                if (!GetSoundEntriesStore()->LookupEntry(pTemp.SoundId))
                    sLog.outErrorDb("Entry %i in table `custom_texts` has soundId %u but sound does not exist.", iId, pTemp.SoundId);
            }

            if (!GetLanguageDescByID(pTemp.Language))
                sLog.outErrorDb("Entry %i in table `custom_texts` using Language %u but Language does not exist.", iId, pTemp.Language);

            if (pTemp.Type > CHAT_TYPE_ZONE_YELL)
                sLog.outErrorDb("Entry %i in table `custom_texts` has Type %u but this Chat Type does not exist.", iId, pTemp.Type);

            m_mTextDataMap[iId] = pTemp;
            ++uiCount;
        } while (result->NextRow());

        delete result;

        sLog.outString("");
        sLog.outString(">> Loaded %u additional Custom Texts data.", uiCount);
    }
    else
    {
        BarGoLink bar(1);
        bar.step();
        sLog.outString("");
        sLog.outString(">> Loaded 0 additional Custom Texts data. DB table `custom_texts` is empty.");
    }
}

void ScriptMgr::LoadScriptGossipTexts()
{
    sLog.outString("Loading Gossip Texts...");
    LoadMangosStrings(WorldDatabase, "gossip_texts", TEXT_SOURCE_GOSSIP_START, TEXT_SOURCE_GOSSIP_END);
}

void ScriptMgr::LoadScriptWaypoints()
{
    // Drop Existing Waypoint list
    m_mPointMoveMap.clear();

    uint64 uiCreatureCount = 0;

    // Load Waypoints
    QueryResult* result = WorldDatabase.PQuery("SELECT COUNT(entry) FROM script_waypoint GROUP BY entry");
    if (result)
    {
        uiCreatureCount = result->GetRowCount();
        delete result;
    }

    sLog.outString("Loading Script Waypoints for %u creature(s)...", uiCreatureCount);

    result = WorldDatabase.PQuery("SELECT entry, pointid, location_x, location_y, location_z, waittime FROM script_waypoint ORDER BY pointid");

    if (result)
    {
        BarGoLink bar(result->GetRowCount());
        uint32 uiNodeCount = 0;

        do
        {
            bar.step();
            Field* pFields = result->Fetch();
            ScriptPointMove pTemp;

            pTemp.uiCreatureEntry   = pFields[0].GetUInt32();
            uint32 uiEntry          = pTemp.uiCreatureEntry;
            pTemp.uiPointId         = pFields[1].GetUInt32();
            pTemp.fX                = pFields[2].GetFloat();
            pTemp.fY                = pFields[3].GetFloat();
            pTemp.fZ                = pFields[4].GetFloat();
            pTemp.uiWaitTime        = pFields[5].GetUInt32();

            CreatureInfo const* pCInfo = GetCreatureTemplateStore(pTemp.uiCreatureEntry);

            if (!pCInfo)
            {
                sLog.outErrorDb("DB table script_waypoint has waypoint for nonexistant creature entry %u", pTemp.uiCreatureEntry);
                continue;
            }

            if (!pCInfo->ScriptID)
                sLog.outErrorDb("DB table script_waypoint has waypoint for creature entry %u, but creature does not have ScriptName defined and then useless.", pTemp.uiCreatureEntry);

            m_mPointMoveMap[uiEntry].push_back(pTemp);
            ++uiNodeCount;
        } while (result->NextRow());

        delete result;

        sLog.outString("");
        sLog.outString(">> Loaded %u Script Waypoint nodes.", uiNodeCount);
    }
    else
    {
        BarGoLink bar(1);
        bar.step();
        sLog.outString("");
        sLog.outString(">> Loaded 0 Script Waypoints. DB table `script_waypoint` is empty.");
    }
}

void ScriptMgr::LoadEscortData()
{
    // Drop Existing Escort list
    m_mEscortDataMap.clear();

    uint64 EscortDataCount = 0;

    QueryResult* pResult = WorldDatabase.PQuery("SELECT creature_id, quest, escort_faction FROM script_escort_data");

    if (pResult)
    {
        barGoLink bar(pResult->GetRowCount());
        do
        {
            bar.step();
            Field* pFields = pResult->Fetch();
            CreatureEscortData pTemp;

            pTemp.uiCreatureEntry    = pFields[0].GetUInt32();
            pTemp.uiQuestEntry       = pFields[1].GetUInt32();
            pTemp.uiEscortFaction    = pFields[2].GetUInt32();

            CreatureInfo const* pCInfo = GetCreatureTemplateStore(pTemp.uiCreatureEntry);

            if (!pCInfo)
            {
                sLog.outErrorDb("DB table script_escort_data has data for non-existant creature entry %u", pTemp.uiCreatureEntry);
                continue;
            }

            if (!pCInfo->ScriptID)
                sLog.outErrorDb("DB table script_escort_data has data for creature entry %u, but creature does not have ScriptName defined and then useless.", pTemp.uiCreatureEntry);

            // Calcul de uiLastWaypointEntry, et mise en "cache"
            std::vector<ScriptPointMove> const points = GetPointMoveList(pTemp.uiCreatureEntry);
            if(points.empty())
            {
                sLog.outErrorDb("Le PNJ %u de script_escort_data n'a pas de donnees de Waypoints !", pTemp.uiCreatureEntry);
                continue;
            }
			pTemp.uiLastWaypointEntry = 0;
            std::vector<ScriptPointMove>::const_iterator it;
            for(it = points.begin(); it != points.end(); ++it)
            {
                if(it->uiPointId > pTemp.uiLastWaypointEntry)
                    pTemp.uiLastWaypointEntry = it->uiPointId;
            }
            m_mEscortDataMap[pTemp.uiCreatureEntry] = pTemp;
            ++EscortDataCount;
        } while (pResult->NextRow());

        delete pResult;

        sLog.outString("");
        sLog.outString(">> Loaded %u Escort Creature Data", EscortDataCount);
    }
    else
    {
        barGoLink bar(1);
        bar.step();
        sLog.outString("");
        sLog.outString(">> Loaded 0 Escort Creature Data. DB table `script_escort_data` is empty.");
    }
}

void ScriptMgr::CollectPossibleEventIds(std::set<uint32>& eventIds)
{
    // Load all possible script entries from gameobject
    for (auto itr = sGOStorage.begin<GameObjectInfo>(); itr < sGOStorage.end<GameObjectInfo>(); ++itr)
    {
        switch (itr->type)
        {
            case GAMEOBJECT_TYPE_GOOBER:
                eventIds.insert(itr->goober.eventId);
                break;
            case GAMEOBJECT_TYPE_CHEST:
                eventIds.insert(itr->chest.eventId);
                break;
            case GAMEOBJECT_TYPE_CAMERA:
                eventIds.insert(itr->camera.eventID);
                break;
            case GAMEOBJECT_TYPE_CAPTURE_POINT:
                eventIds.insert(itr->capturePoint.neutralEventID1);
                eventIds.insert(itr->capturePoint.neutralEventID2);
                eventIds.insert(itr->capturePoint.contestedEventID1);
                eventIds.insert(itr->capturePoint.contestedEventID2);
                eventIds.insert(itr->capturePoint.progressEventID1);
                eventIds.insert(itr->capturePoint.progressEventID2);
                eventIds.insert(itr->capturePoint.winEventID1);
                eventIds.insert(itr->capturePoint.winEventID2);
                break;
            default:
                break;
        }
    }

    // Load all possible script entries from spells
    for (uint32 i = 1; i < sSpellMgr.GetMaxSpellId(); ++i)
    {
        SpellEntry* spell = ((SpellEntry*)sSpellMgr.GetSpellEntry(i));
        if (spell)
        {
            for (int j = 0; j < MAX_EFFECT_INDEX; ++j)
            {
                if (spell->Effect[j] == SPELL_EFFECT_SEND_EVENT)
                {
                    if (spell->EffectMiscValue[j])
                        eventIds.insert(spell->EffectMiscValue[j]);
                }
            }
        }
    }

    // Load all possible script entries from EventAI.
    // Has to be done with a query because it's loaded later.
    QueryResult* result = WorldDatabase.Query("SELECT action1_param1 FROM creature_ai_scripts WHERE action1_type=50");

    Field* fields;

    if (result)
    {
        do
        {
            fields = result->Fetch();
            uint32 eventId = fields[0].GetUInt32();
            if (eventId)
             eventIds.insert(eventId);
        } while (result->NextRow());
        delete result;
    }

    result = WorldDatabase.Query("SELECT action2_param1 FROM creature_ai_scripts WHERE action2_type=50");
    
    if (result)
    {
        do
        {
            fields = result->Fetch();
            uint32 eventId = fields[0].GetUInt32();
            if (eventId)
                eventIds.insert(eventId);
        } while (result->NextRow());
        delete result;
    }

    result = WorldDatabase.Query("SELECT action3_param1 FROM creature_ai_scripts WHERE action3_type=50");

    if (result)
    {
        do
        {
            fields = result->Fetch();
            uint32 eventId = fields[0].GetUInt32();
            if (eventId)
                eventIds.insert(eventId);
        } while (result->NextRow());
        delete result;
    }
}

void DoScriptText(int32 iTextEntry, WorldObject* pSource, Unit* pTarget, uint32 chatTypeOverride)
{
    if (!pSource)
    {
        sLog.outError("DoScriptText entry %i, invalid Source pointer.", iTextEntry);
        return;
    }

    uint8 Type;
    uint32 Emote;
    uint32 Language;
    uint32 SoundId;

    if (iTextEntry >= 0)
    {
        if (const BroadcastText* bct = sObjectMgr.GetBroadcastTextLocale(iTextEntry))
        {
            Type = bct->Type;
            Emote = bct->EmoteId0;
            Language = bct->Language;
            SoundId = bct->SoundId;
        }
        else
        {
            sLog.outError("DoScriptText with source entry %u (TypeId=%u, guid=%u) attempts to process broadcast text id %i, but text id does not exist.", pSource->GetEntry(), pSource->GetTypeId(), pSource->GetGUIDLow(), iTextEntry);
            return;
        }
    }
    else
    {
        if (const StringTextData* pData = sScriptMgr.GetTextData(iTextEntry))
        {
            Type = pData->Type;
            Emote = pData->Emote;
            Language = pData->Language;
            SoundId = pData->SoundId;
        }
        else
        {
            sLog.outError("DoScriptText with source entry %u (TypeId=%u, guid=%u) could not find text entry %i.", pSource->GetEntry(), pSource->GetTypeId(), pSource->GetGUIDLow(), iTextEntry);
            return;
        }
    }

    if (chatTypeOverride)
        Type = chatTypeOverride;

    DEBUG_LOG("DoScriptText: text entry=%i, Sound=%u, Type=%u, Language=%u, Emote=%u",
        iTextEntry, SoundId, Type, Language, Emote);

    if (SoundId)
    {
        if (GetSoundEntriesStore()->LookupEntry(SoundId))
        {
            if(Type == CHAT_TYPE_ZONE_YELL)
            {
                if(Map* pZone = pSource->GetMap())
                    pZone->PlayDirectSoundToMap(SoundId);
            }
            else
                pSource->PlayDirectSound(SoundId);
        }
        else
            sLog.outError("DoScriptText entry %i tried to process invalid sound id %u.", iTextEntry, SoundId);
    }

    if (Emote)
    {
        if (pSource->GetTypeId() == TYPEID_UNIT || pSource->GetTypeId() == TYPEID_PLAYER)
            ((Unit*)pSource)->HandleEmoteCommand(Emote);
        else
            sLog.outError("DoScriptText entry %i tried to process emote for invalid TypeId (%u).", iTextEntry, pSource->GetTypeId());
    }

    switch (Type)
    {
        case CHAT_TYPE_SAY:
            pSource->MonsterSay(iTextEntry, Language, pTarget);
            break;
        case CHAT_TYPE_YELL:
            pSource->MonsterYell(iTextEntry, Language, pTarget);
            break;
        case CHAT_TYPE_TEXT_EMOTE:
            pSource->MonsterTextEmote(iTextEntry, pTarget);
            break;
        case CHAT_TYPE_BOSS_EMOTE:
            pSource->MonsterTextEmote(iTextEntry, pTarget, true);
            break;
        case CHAT_TYPE_WHISPER:
            if (pTarget && pTarget->GetTypeId() == TYPEID_PLAYER)
                pSource->MonsterWhisper(iTextEntry, pTarget);
            else
                sLog.outError("DoScriptText entry %i cannot whisper without target unit (TYPEID_PLAYER).", iTextEntry);

            break;
        case CHAT_TYPE_BOSS_WHISPER:
        {
            if (pTarget && pTarget->GetTypeId() == TYPEID_PLAYER)
                pSource->MonsterWhisper(iTextEntry, pTarget, true);
            else
                sLog.outError("DoScriptText entry %i cannot whisper without target unit (TYPEID_PLAYER).", iTextEntry);

            break;
        }
        case CHAT_TYPE_ZONE_YELL:
            pSource->MonsterYellToZone(iTextEntry, Language, pTarget);
            break;
    }
}


/**
 * Function that either simulates or does script text for a map
 *
 * @param iTextEntry Entry of the text, stored in SD2-database, only type CHAT_TYPE_ZONE_YELL supported
 * @param uiCreatureEntry Id of the creature of whom saying will be simulated
 * @param pMap Given Map on which the map-wide text is displayed
 * @param pCreatureSource Can be nullptr. If pointer to Creature is given, then the creature does the map-wide text
 * @param pTarget Can be nullptr. Possible target for the text
 */
void DoOrSimulateScriptTextForMap(int32 iTextEntry, uint32 uiCreatureEntry, Map* pMap, Creature* pCreatureSource /*=nullptr*/, Unit* pTarget /*=nullptr*/)
{
    if (!pMap)
    {
	sLog.outError("DoOrSimulateScriptTextForMap entry %i, invalid Map pointer.", iTextEntry);
	return;
    }

    CreatureInfo const* pInfo = GetCreatureTemplateStore(uiCreatureEntry);
    if (!pInfo)
    {
        sLog.outError("DoOrSimulateScriptTextForMap has invalid source entry %u for map %u.", uiCreatureEntry, pMap->GetId());
        return;
    }

    uint8 Type;
    uint32 Emote;
    uint32 LanguageId;
    uint32 SoundId;

    if (iTextEntry >= 0)
    {
        if (const BroadcastText* bct = sObjectMgr.GetBroadcastTextLocale(iTextEntry))
        {
            Type = bct->Type;
            Emote = bct->EmoteId0;
            LanguageId = bct->Language;
            SoundId = bct->SoundId;
        }
        else
        {
            sLog.outError("DoOrSimulateScriptTextForMap with source entry %u for map %u could not find broadcast text id %i.", uiCreatureEntry, pMap->GetId(), iTextEntry);
            return;
        }
    }
    else
    {
        if (MangosStringLocale const* pData = sObjectMgr.GetMangosStringLocale(iTextEntry))
        {
            Type = pData->Type;
            Emote = pData->Emote;
            LanguageId = pData->LanguageId;
            SoundId = pData->SoundId;
        }
        else
        {
            sLog.outError("DoOrSimulateScriptTextForMap with source entry %u for map %u could not find script text entry %i.", uiCreatureEntry, pMap->GetId(), iTextEntry);
            return;
        }
    }

    sLog.outDebug("SD2: DoOrSimulateScriptTextForMap: text entry=%i, Sound=%u, Type=%u, Language=%u, Emote=%u",
	      iTextEntry, SoundId, Type, LanguageId, Emote);

    if (Type != CHAT_TYPE_ZONE_YELL)
    {
	    sLog.outError("DoSimulateScriptTextForMap entry %i has not supported chat type %u.", iTextEntry, Type);
	    return;
    }

    if (SoundId)
	    pMap->PlayDirectSoundToMap(SoundId);

    if (pCreatureSource)                                // If provided pointer for sayer, use direct version
	    pMap->MonsterYellToMap(pCreatureSource->GetObjectGuid(), iTextEntry, LanguageId, pTarget);
    else                                                // Simulate yell
	    pMap->MonsterYellToMap(pInfo, iTextEntry, LanguageId, pTarget);
}

void Script::RegisterSelf(bool bReportError)
{
    if (uint32 id = sScriptMgr.GetScriptId(Name.c_str()))
    {
        m_scripts[id] = this;
        ++num_sc_scripts;
    }
    else
    {
        // Don't report unused generic scripts
        if (bReportError)
        {
            if (!(strstr(Name.c_str(), "generic") || strstr(Name.c_str(), "npc_escort")))
                sLog.outError("Script registering but ScriptName %s is not assigned in database. Script will not be used.", Name.c_str());
        }

        delete this;
    }
}

void ScriptMgr::FillSpellSummary()
{
    delete[] m_spellSummary;

    m_spellSummary = new TSpellSummary[sSpellMgr.GetMaxSpellId()];

    SpellEntry const* pTempSpell;

    for (uint32 i = 0; i < sSpellMgr.GetMaxSpellId(); ++i)
    {
        m_spellSummary[i].Effects = 0;
        m_spellSummary[i].Targets = 0;

        pTempSpell = sSpellMgr.GetSpellEntry(i);
        // This spell doesn't exist
        if (!pTempSpell)
            continue;

        for (uint8 j = 0; j < 3; ++j)
        {
            // Spell targets self
            if (pTempSpell->EffectImplicitTargetA[j] == TARGET_SELF)
                m_spellSummary[i].Targets |= 1 << (SELECT_TARGET_SELF - 1);

            // Spell targets a single enemy
            if (pTempSpell->EffectImplicitTargetA[j] == TARGET_CHAIN_DAMAGE ||
                pTempSpell->EffectImplicitTargetA[j] == TARGET_CURRENT_ENEMY_COORDINATES)
                m_spellSummary[i].Targets |= 1 << (SELECT_TARGET_SINGLE_ENEMY - 1);

            // Spell targets AoE at enemy
            if (pTempSpell->EffectImplicitTargetA[j] == TARGET_ALL_ENEMY_IN_AREA ||
                pTempSpell->EffectImplicitTargetA[j] == TARGET_ALL_ENEMY_IN_AREA_INSTANT ||
                pTempSpell->EffectImplicitTargetA[j] == TARGET_CASTER_COORDINATES ||
                pTempSpell->EffectImplicitTargetA[j] == TARGET_ALL_ENEMY_IN_AREA_CHANNELED)
                m_spellSummary[i].Targets |= 1 << (SELECT_TARGET_AOE_ENEMY - 1);

            // Spell targets an enemy
            if (pTempSpell->EffectImplicitTargetA[j] == TARGET_CHAIN_DAMAGE ||
                pTempSpell->EffectImplicitTargetA[j] == TARGET_CURRENT_ENEMY_COORDINATES ||
                pTempSpell->EffectImplicitTargetA[j] == TARGET_ALL_ENEMY_IN_AREA ||
                pTempSpell->EffectImplicitTargetA[j] == TARGET_ALL_ENEMY_IN_AREA_INSTANT ||
                pTempSpell->EffectImplicitTargetA[j] == TARGET_CASTER_COORDINATES ||
                pTempSpell->EffectImplicitTargetA[j] == TARGET_ALL_ENEMY_IN_AREA_CHANNELED)
                m_spellSummary[i].Targets |= 1 << (SELECT_TARGET_ANY_ENEMY - 1);

            // Spell targets a single friend(or self)
            if (pTempSpell->EffectImplicitTargetA[j] == TARGET_SELF ||
                pTempSpell->EffectImplicitTargetA[j] == TARGET_SINGLE_FRIEND ||
                pTempSpell->EffectImplicitTargetA[j] == TARGET_SINGLE_PARTY)
                m_spellSummary[i].Targets |= 1 << (SELECT_TARGET_SINGLE_FRIEND - 1);

            // Spell targets aoe friends
            if (pTempSpell->EffectImplicitTargetA[j] == TARGET_ALL_PARTY_AROUND_CASTER ||
                pTempSpell->EffectImplicitTargetA[j] == TARGET_AREAEFFECT_PARTY ||
                pTempSpell->EffectImplicitTargetA[j] == TARGET_CASTER_COORDINATES)
                m_spellSummary[i].Targets |= 1 << (SELECT_TARGET_AOE_FRIEND - 1);

            // Spell targets any friend(or self)
            if (pTempSpell->EffectImplicitTargetA[j] == TARGET_SELF ||
                pTempSpell->EffectImplicitTargetA[j] == TARGET_SINGLE_FRIEND ||
                pTempSpell->EffectImplicitTargetA[j] == TARGET_SINGLE_PARTY ||
                pTempSpell->EffectImplicitTargetA[j] == TARGET_ALL_PARTY_AROUND_CASTER ||
                pTempSpell->EffectImplicitTargetA[j] == TARGET_AREAEFFECT_PARTY ||
                pTempSpell->EffectImplicitTargetA[j] == TARGET_CASTER_COORDINATES)
                m_spellSummary[i].Targets |= 1 << (SELECT_TARGET_ANY_FRIEND - 1);

            // Make sure that this spell includes a damage effect
            if (pTempSpell->Effect[j] == SPELL_EFFECT_SCHOOL_DAMAGE ||
                pTempSpell->Effect[j] == SPELL_EFFECT_INSTAKILL ||
                pTempSpell->Effect[j] == SPELL_EFFECT_ENVIRONMENTAL_DAMAGE ||
                pTempSpell->Effect[j] == SPELL_EFFECT_HEALTH_LEECH)
                m_spellSummary[i].Effects |= 1 << (SELECT_EFFECT_DAMAGE - 1);

            // Make sure that this spell includes a healing effect (or an apply aura with a periodic heal)
            if (pTempSpell->Effect[j] == SPELL_EFFECT_HEAL ||
                pTempSpell->Effect[j] == SPELL_EFFECT_HEAL_MAX_HEALTH ||
                pTempSpell->Effect[j] == SPELL_EFFECT_HEAL_MECHANICAL ||
                (pTempSpell->Effect[j] == SPELL_EFFECT_APPLY_AURA  && pTempSpell->EffectApplyAuraName[j] == 8))
                m_spellSummary[i].Effects |= 1 << (SELECT_EFFECT_HEALING - 1);

            // Make sure that this spell applies an aura
            if (pTempSpell->Effect[j] == SPELL_EFFECT_APPLY_AURA)
                m_spellSummary[i].Effects |= 1 << (SELECT_EFFECT_AURA - 1);
        }
    }
}
