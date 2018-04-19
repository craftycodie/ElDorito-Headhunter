#include "Equipment.hpp"
#include "../Pointer.hpp"
#include "../Blam/BlamTypes.hpp"
#include "../Blam/BlamPlayers.hpp"
#include "../Blam/BlamData.hpp"
#include "../Blam/BlamObjects.hpp"
#include "../Blam/Math/RealVector3D.hpp"
#include "../Blam/Tags/TagBlock.hpp"
#include "../Blam/Tags/TagInstance.hpp"
#include "../Blam/Tags/Items/Item.hpp"
#include "../Blam/Tags/Game/MultiplayerGlobals.hpp"
#include "../Patches/CustomPackets.hpp"
#include "../Patches/Headhunter.hpp"
#include "../Blam/BlamTime.hpp"
#include "../ElDorito.hpp"
#include "../Patch.hpp"
#include "../Patches/Ui.hpp"
#include "../Patches/Core.hpp"
#include <cstdint>
#include <cassert>
#include <unordered_map>

using namespace Patches::Headhunter;

namespace
{
	typedef Patches::CustomPackets::PacketSender<HeadhunterMessage> HeadhunterMessagePacketSender;
	typedef Patches::CustomPackets::Packet<HeadhunterMessage> HeadhunterMessagePacket;

	void EquipmentPickupHook();
	void EquipmentActionStateHook();
	char __cdecl Unit_EquipmentDetachHook(uint32_t unitObjectIndex, uint32_t equipmentObjectIndex, int a3);
	void* __cdecl Player_GetArmorAbilitiesCHUDHook(Blam::Players::PlayerDatum* playerDatum);
	bool __cdecl UnitUpdateHook(uint32_t unitObjectIndex);
	void __cdecl EquipmentUseHook(int unitObjectIndex, int slotIndex, unsigned int isClient);
	void __cdecl UnitDeathHook(int unitObjectIndex, int a2, int a3);
	void DespawnEquipmentHook();
	void game_engine_player_marker_hook();

	void Hill_FFAScoreHook();
	void Hill_TeamScoreHook();
	void Hill_TraitsHook();
}

namespace Patches::Headhunter
{
	const int MAX_SKULL_COUNT = 10;
	const auto IsClient = (bool(*)())(0x00531D70);

	int GetMaxSkullCount()
	{
		return MAX_SKULL_COUNT;
	}

	std::map<uint64_t, uint32_t> skullCounts;

	std::shared_ptr<HeadhunterMessagePacketSender> HeadhunterPacketSender;
	void ReceivedHeadhunterMessage(Blam::Network::Session *session, int peer, const HeadhunterMessage &message);

	class HeadhunterOutputHandler : public HeadhunterMessageHandler
	{
	public:
		HeadhunterOutputHandler() {};

		void MessageReceived(const HeadhunterMessage &message) override
		{
			if (message.Type == HeadhunterMessageType::SkullCount)
			{
				skullCounts[message.playerUid] = message.skullCount;

				auto &players = Blam::Players::GetPlayers();

				auto localPlayerDatumIndex = Blam::Players::GetLocalPlayer(0);
				if (localPlayerDatumIndex == Blam::DatumHandle::Null)
					return;

				auto localPlayer = players.Get(localPlayerDatumIndex);
				if (!localPlayer)
					return;

				if(message.playerUid == localPlayer->Properties.Uid)
					Patches::Ui::UpdateHeadhunterSkullsString(message.skullCount);
			}
		}

	};

	// Packet handler for voting messages
	class HeadhunterMessagePacketHandler : public Patches::CustomPackets::PacketHandler<HeadhunterMessage>
	{
	public:
		void Serialize(Blam::BitStream *stream, const HeadhunterMessage *data) override
		{
			stream->WriteUnsigned(static_cast<uint32_t>(data->Type), 0U, static_cast<uint32_t>(HeadhunterMessageType::Count));

			if (data->Type == HeadhunterMessageType::SkullCount)
			{
				stream->WriteUnsigned<uint64_t>(data->playerUid, 64);
				stream->WriteUnsigned<uint32_t>(data->skullCount, 32);
			}
		}

		bool Deserialize(Blam::BitStream *stream, HeadhunterMessage *data) override
		{
			memset(data, 0, sizeof(*data));

			// Message type
			data->Type = static_cast<HeadhunterMessageType>(stream->ReadUnsigned(0U, static_cast<uint32_t>(HeadhunterMessageType::Count)));
			if (static_cast<uint32_t>(data->Type) >= static_cast<uint32_t>(HeadhunterMessageType::Count))
				return false;

			if (data->Type == HeadhunterMessageType::SkullCount)
			{
				data->playerUid = stream->ReadUnsigned<uint64_t>(64);
				data->skullCount = stream->ReadUnsigned<uint32_t>(32);
			}

			return true;
		}

		void HandlePacket(Blam::Network::ObserverChannel *sender, const HeadhunterMessagePacket *packet) override
		{
			auto session = Blam::Network::GetActiveSession();
			if (!session)
				return;

			//at this level, it doesnt matter if we are the host or not. If we receive any sort of voting message, handle it
			ReceivedHeadhunterMessage(session, session->GetChannelPeer(sender), packet->Data);

		}
	};

	// Sends a voting message to a peer as a packet.
	bool SendHeadhunterMessagePacket(Blam::Network::Session *session, int peer, const HeadhunterMessage &message)
	{
		if (peer < 0)
			return false;
		// if we are trying to send something to ourself, pretend someone else sent it to us. 
		if (peer == session->MembershipInfo.LocalPeerIndex)
		{
			ReceivedHeadhunterMessage(session, peer, message);
			return true;
		}
		auto packet = HeadhunterPacketSender->New();
		packet.Data = message;
		HeadhunterPacketSender->Send(peer, packet);
		return true;
	}

	HeadhunterMessage::HeadhunterMessage(HeadhunterMessageType type)
	{
		memset(this, 0, sizeof(*this));
		Type = type;
	}

	// Registered message handlers
	std::vector<std::shared_ptr<HeadhunterMessageHandler>> skullMessageHandlers;

	void OnMapLoaded(std::string mapPath)
	{
		skullCounts.empty();

		for (auto player : Blam::Players::GetPlayers())
		{
			skullCounts[player.Properties.Uid] = 0;
		}

		Patches::Ui::UpdateHeadhunterSkullsString(0);
	}

	// Callback for when a message is received.
	void ReceivedHeadhunterMessage(Blam::Network::Session *session, int peer, const HeadhunterMessage &message)
	{
		// Send the message out to handlers
		for (auto &&handler : skullMessageHandlers)
			handler->MessageReceived(message);
	}

	void InitializePackets()
	{
		// Register custom packet type
		auto handler = std::make_shared<HeadhunterMessagePacketHandler>();
		HeadhunterPacketSender = Patches::CustomPackets::RegisterPacket<HeadhunterMessage>("eldewrito-headhunter-message", handler);
	}

	void Init()
	{
		InitializePackets();
		AddMessageHandler(std::make_shared<HeadhunterOutputHandler>());
		Patches::Core::OnMapLoaded(OnMapLoaded);
	}

	void ApplyAll()
	{
		//Scoring
		Hook(0x5D5F71, Hill_FFAScoreHook).Apply(); // Score heads in FFA
		Hook(0x5D5E9F, Hill_TeamScoreHook).Apply(); // Score heads in Teams
		Patch::NopFill(Pointer::Base(0x5D5F6C), 0x2); // Instant scoring, rather than waiting 1 second, FFA
		Patch::NopFill(Pointer::Base(0x5D5DB5), 0x6); // Above but teams.

		//Hill Control
		Patch::NopFill(Pointer::Base(0x5D67DA), 0x15); // Prevent hill contested while still allowing scoring.
		Hook(0x5D537F, Hill_TraitsHook).Apply(); // Apply On-Hill traits to skull holders.

		//HUD
		Patch(0x5D668E, { 0x04 }).Apply(); // Replace hill crown with generic objective diamond.
		Hook(0x34943B, game_engine_player_marker_hook).Apply(); // Add skull count to player markers.
	}

	bool GetHeadhunterEnabled()
	{
		return true;
	}

	//Sends a voting message to all peers
	bool BroadcastHeadhunterMessage(HeadhunterMessage &message)
	{
		auto session = Blam::Network::GetActiveSession();
		auto membership = &session->MembershipInfo;
		for (int peer = membership->FindFirstPeer(); peer >= 0; peer = membership->FindNextPeer(peer))
		{
			if (!SendHeadhunterMessagePacket(session, peer, message))
				return false;
		}
		return true;
	}

	void UpdateSkullCountByHandle(int playerHandle, uint32_t skullCount)
	{
		auto players = Blam::Players::GetPlayers();
		auto player = players.Get(playerHandle);
		UpdateSkullCountByUid(player->Properties.Uid, skullCount);
	}

	void UpdateSkullCountByUid(uint64_t uid, uint32_t skullCount)
	{
		auto* session = Blam::Network::GetActiveSession();
		if (!(session && session->IsEstablished() && session->IsHost()))
			return;

		if (GetSkullCountByUid(uid) == skullCount)
			return;

		if (skullCount > MAX_SKULL_COUNT)
			skullCount = MAX_SKULL_COUNT;

		//Host update now.
		skullCounts[uid] = skullCount;

		HeadhunterMessage message = HeadhunterMessage(HeadhunterMessageType::SkullCount);
		message.playerUid = uid;
		message.skullCount = skullCount;
		BroadcastHeadhunterMessage(message);
	}

	void AddMessageHandler(std::shared_ptr<HeadhunterMessageHandler> handler)
	{
		skullMessageHandlers.push_back(handler);
	}

	uint32_t GetSkullCountByHandle(int handle)
	{
		auto players = Blam::Players::GetPlayers();
		auto player = players.Get(handle);
		return GetSkullCountByUid(player->Properties.Uid);
	}

	uint32_t GetSkullCountByUid(uint64_t uid)
	{
		if (skullCounts.find(uid) != skullCounts.end())
		{
			return skullCounts[uid];
		}
		else
		{
			return 0;
		}
	}
}

namespace
{
	// Registered message handlers
	std::vector<std::shared_ptr<HeadhunterMessageHandler>> skullMessageHandlers;

	using namespace Patches::Headhunter;

	int __stdcall HillScore(int playerHandle)
	{
		int skulls = GetSkullCountByHandle(playerHandle);
		UpdateSkullCountByHandle(playerHandle, 0);
		return skulls;
	}

	__declspec(naked) void Hill_TraitsHook()
	{
		__asm
		{
			mov eax, 0x748960
			call eax

			push ecx
			push ebx
			push edx
			push ebp
			push esi

			push edi
			call GetSkullCountByHandle
			pop edi

			pop esi
			pop ebp
			pop edx
			pop ebx
			pop ecx

			cmp eax, 0
			je no_skulls
			mov word ptr [esi+0x2DEC], 01
			jmp ed_return

			no_skulls:
			mov word ptr [esi+0x2DEC], 00

			ed_return:
			mov eax, 0x9D5384
			jmp eax
		}
	}

	__declspec(naked) void Hill_FFAScoreHook()
	{
		__asm
		{
			test esi, esi
			je no_score

			mov edi, ecx

			push ecx
			push eax
			push ebx
			push edx
			push ebp

			mov ecx, edi

			mov edi, 0x537830
			call edi

			push eax
			call HillScore
			mov esi, eax

			pop ebp
			pop edx
			pop ebx
			pop eax
			pop ecx

			no_score:
			mov edi, 0x537830
			call edi

			mov edi, 0x9D5F76
			jmp edi
		}
	}

	__declspec(naked) void Hill_TeamScoreHook()
	{
		__asm
		{
			pop eax
			mov eax, 0x537830
			call eax
			mov ecx, eax

			push ebx
			push edx
			push ebp
			push esi
			push ecx

			push eax 
			call HillScore

			pop ecx
			pop esi
			pop ebp
			pop edx
			pop ebx

			push eax
			push ecx

			mov ecx, 0x9D5EA5
			jmp ecx
		}
	}

	__declspec(naked) void game_engine_player_marker_hook()
	{
		using namespace Patches::Headhunter;

		__asm
		{

				push eax
				push ecx
				push ebx
				push edx
				push esi
				push ebp

				call GetHeadhunterEnabled;
				test ax, ax
				je ed_return

				pop ebp
				mov eax, [ebp + 0xC]
				push ebp

				push eax
				call GetSkullCountByHandle
				test eax, eax
				je ed_return
				cmp eax, 10
				jl number_icon
				mov dword ptr[ebx + 8], 0x09
				jmp ed_return

			number_icon:
				add eax, 10
				mov dword ptr[ebx + 8], eax

			ed_return:
				pop eax
				pop ebp
				pop esi
				pop edx
				pop ebx
				pop ecx
				pop eax

				cmp byte ptr[ebp - 8], 0
				movss[esi + 0x8], xmm0
				mov eax, 0x749440
				jmp eax

		}
	}

}
