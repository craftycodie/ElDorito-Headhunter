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
#include "../Blam/BlamTime.hpp"
#include "../ElDorito.hpp"
#include "../Patch.hpp"
#include "../Patches/Ui.hpp"
#include <cstdint>
#include <cassert>
#include <unordered_map>

namespace
{
	void EquipmentPickupHook();
	void EquipmentActionStateHook();
	char __cdecl Unit_EquipmentDetachHook(uint32_t unitObjectIndex, uint32_t equipmentObjectIndex, int a3);
	void* __cdecl Player_GetArmorAbilitiesCHUDHook(Blam::Players::PlayerDatum* playerDatum);
	bool __cdecl UnitUpdateHook(uint32_t unitObjectIndex);
	void __cdecl EquipmentUseHook(int unitObjectIndex, int slotIndex, unsigned int isClient);
	void __cdecl UnitDeathHook(int unitObjectIndex, int a2, int a3);
	void DespawnEquipmentHook();

	void Hill_ScoreHook();
}

namespace Patches::Headhunter
{
	int headCount;

	void ApplyAll()
	{
		Patches::Ui::UpdateHeadhunterSkullsString();

		Patch::NopFill(Pointer::Base(0x5D67DA), 0x15); // Prevent hill contested while still allowing scoring.
		Hook(0x5D5F87, Hill_ScoreHook).Apply(); // Score heads
		Patch(0x5D668E, { 0xFF }).Apply(); //Remove crown icon from hill zones.
		Patch(0x5D5F6C, { 0x90, 0x90 }).Apply(); //Instant scoring, rather than waiting 1 second
	}

	bool GetHeadhunterEnabled()
	{
		return true;
	}
}

namespace
{
	const auto IsClient = (bool(*)())(0x00531D70);
	using namespace Patches::Headhunter;

	__declspec(naked) void Hill_ScoreHook()
	{
		__asm
		{
			mov ecx, headCount
			mov headCount, 0
			push ecx
			push edi
			mov ecx, 0x54D840
			call ecx
		}

		Patches::Ui::UpdateHeadhunterSkullsString();

		__asm
		{
			mov ecx, 0x9D5F91
			jmp ecx
		}
	}
}
