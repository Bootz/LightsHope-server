/* Copyright (C) 2006 - 2009 ScriptDev2 <https://scriptdev2.svn.sourceforge.net/>
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

/* ScriptData
SDName: Npc_Professions
SD%Complete: 80
SDComment: Provides learn/unlearn/relearn-options for professions. Not supported: Unlearn engineering, re-learn engineering, re-learn leatherworking.
SDCategory: NPCs
EndScriptData */

#include "scriptPCH.h"

/*
A few notes for future developement:
- A full implementation of gossip for GO's is required. They must have the same scripting capabilities as creatures. Basically,
there is no difference here (except that default text is chosen with `gameobject_template`.`data3` (for GO type2, different dataN for a few others)
- It's possible blacksmithing still require some tweaks and adjustments due to the way we _have_ to use reputation.
*/

/*
-- UPDATE `gameobject_template` SET `ScriptName` = 'go_soothsaying_for_dummies' WHERE `entry` = 177226;
*/



/*############################################################################################################
# VIP���˵�������
############################################################################################################*/

/*###
# VIP�۸���
###*/

#define GOSSIP_SENDER_INQUIRECOIN                      60   //��ѯ����
#define GOSSIP_SENDER_INSTANTFLIGHT                    61   //˲�ɲ���
#define GOSSIP_SENDER_CHANGENAME                       62   //����
#define GOSSIP_SENDER_LEVELUP                          63   //�ȼ�����
#define GOSSIP_SENDER_CHANGERACE                       64   //�޸�����
#define GOSSIP_SENDER_INQUIRECOIN_CHANGE              601   //�޸�����
#define GOSSIP_SENDER_BACK						       59    //����

#define C_FLYINGMON_COIN          50           //��Ҫ50���ֿ�˲��һ����
#define C_TIMETOCOIN              7200          //ÿ2Сʱ�仯1�����
#define C_FLYINGMONSECOND         2592000       //˲��30�������
#define C_CHANGENAME_COIN         300           //������Ҫ300��
#define C_LEVELUP_COIN            10          //����һ����Ҫ�ĵ���
#define C_MAXLEVEL_COIN           500          //ֱ��������Ҫ����
	
#define GOSSIP_VIP_TEXT_INQUIRECOIN					"����Ҫ��ѯ�ҵĻ���"
#define GOSSIP_VIP_TEXT_INSTANTFLIGHT				"����Ҫ�˽�˲�ɵ���"
#define GOSSIP_VIP_TEXT_CHANGENAME					"����Ҫ�޸�����"
#define GOSSIP_VIP_TEXT_LEVELUP						"����Ҫ�����ȼ�"
#define GOSSIP_VIP_TEXT_CHANGERACE					"����Ҫ�ı�����"
#define GOSSIP_VIP_TEXT_BACK						"����"
#define GOSSIP_VIP_TEXT_VENDOR                      "�鿴������Ʒ"
#define GOSSIP_VIP_TEXT_INQUIRECOIN_CHANGE          "����ת������"

/*###
# start menues for VIPNPC (engineering and leatherworking)
###*/
void SendChildMenu_INQUIRECOIN(Player* pPlayer, Creature* pCreature) {
	
	pPlayer->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_VIP_TEXT_INQUIRECOIN_CHANGE, GOSSIP_SENDER_INQUIRECOIN_CHANGE, GOSSIP_ACTION_INFO_DEF + 1);
	pPlayer->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_VIP_TEXT_BACK, GOSSIP_SENDER_BACK, GOSSIP_ACTION_INFO_DEF + 2);
	char sMessage[200];
	sprintf(sMessage, "�𾴵�%s��ʿ������ʣ�����Ϊ%d,δת������Ϊ%d", pPlayer->GetName(), pPlayer->getVipInfo(4), pPlayer->getVipInfoTimeToCoin() / C_TIMETOCOIN);
	pPlayer->SEND_GOSSIP_TEXT(sMessage);
	pPlayer->SEND_GOSSIP_MENU(0x7FFFFFFF, pCreature->GetGUID()); //80001ΪVIP���˲˵�

}
bool GossipHello_npc_prof_vipnpc(Player* pPlayer, Creature* pCreature)
{
	sLog.outDebug("==========================================================�򿪲˵�ѡ��.");
	pPlayer->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_VIP_TEXT_INQUIRECOIN, GOSSIP_SENDER_MAIN, GOSSIP_SENDER_INQUIRECOIN);
	pPlayer->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_VIP_TEXT_INSTANTFLIGHT, GOSSIP_SENDER_MAIN, GOSSIP_SENDER_INSTANTFLIGHT);
	pPlayer->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_VIP_TEXT_CHANGENAME, GOSSIP_SENDER_MAIN, GOSSIP_SENDER_CHANGENAME);
	pPlayer->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_VIP_TEXT_CHANGERACE, GOSSIP_SENDER_MAIN, GOSSIP_SENDER_CHANGERACE);
	if (pPlayer->getLevel() < 60) {
	{
		pPlayer->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_VIP_TEXT_LEVELUP, GOSSIP_SENDER_MAIN, GOSSIP_SENDER_LEVELUP);
	}
	pPlayer->ADD_GOSSIP_ITEM(GOSSIP_ICON_VENDOR, GOSSIP_VIP_TEXT_VENDOR, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_TRADE);
		//char sMessage[200];
		//sprintf(sMessage, "��ӭ���� %s !", pPlayer->GetName());
		//pPlayer->SEND_GOSSIP_TEXT(sMessage);
		pPlayer->SEND_GOSSIP_MENU(0x7FFFFFFF, pCreature->GetGUID()); //80001ΪVIP���˲˵�
		return true;
	}
}
bool GossipSelect_npc_prof_vipnpc(Player* pPlayer, Creature* pCreature, uint32 uiSender, uint32 uiAction)
{
	switch (uiSender)
	{
	case GOSSIP_SENDER_INQUIRECOIN:   //��ѯ����
		SendChildMenu_INQUIRECOIN(pPlayer, pCreature);
		break;
	case GOSSIP_SENDER_INSTANTFLIGHT:  //˲��
		//SendActionMenu_npc_prof_leather(pPlayer, pCreature, uiAction);
		break;
	case GOSSIP_SENDER_CHANGENAME:     //����
		//SendActionMenu_npc_prof_leather(pPlayer, pCreature, uiAction);
		break;
	case GOSSIP_SENDER_CHANGERACE:     //�޸�����
		//SendActionMenu_npc_prof_leather(pPlayer, pCreature, uiAction);
		break;
	case GOSSIP_SENDER_LEVELUP:        //����
		//SendActionMenu_npc_prof_leather(pPlayer, pCreature, uiAction);
		break;
	}
	return true;
}


/*###
# start menues for GO (engineering and leatherworking)
###*/

/*bool GOHello_go_soothsaying_for_dummies(Player* pPlayer, GameObject* pGo)
{
pPlayer->ADD_GOSSIP_ITEM(0,GOSSIP_LEARN_DRAGON, GOSSIP_SENDER_INFO, GOSSIP_ACTION_INFO_DEF);
pPlayer->SEND_GOSSIP_MENU(5584, pGo->GetGUID());

return true;
}*/

/*###
#
###*/

void AddSC_npc_vip()
{
	Script *newscript;


	
	
	newscript = new Script;
	newscript->Name = "npc_prof_vipnpc";
	newscript->pGossipHello = &GossipHello_npc_prof_vipnpc; //���˵�
	newscript->pGossipSelect = &GossipSelect_npc_prof_vipnpc;
	newscript->RegisterSelf();
	/*newscript = new Script;
	newscript->Name = "go_soothsaying_for_dummies";
	newscript->pGOHello =  &GOHello_go_soothsaying_for_dummies;
	//newscript->pGossipSelect = &GossipSelect_go_soothsaying_for_dummies;
	newscript->RegisterSelf();*/
}
