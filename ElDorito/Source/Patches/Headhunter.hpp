#pragma once
#include <string>

namespace Patches::Headhunter
{
	// Chat message types.
	enum class HeadhunterMessageType : uint32_t
	{
		SkullCount, //A player skull count change.
		Count
	};

	// Voting Message Data
	struct HeadhunterMessage
	{
		HeadhunterMessage() { }

		HeadhunterMessage(HeadhunterMessageType type);
		// The message type.
		HeadhunterMessageType Type;

		uint64_t playerUid; //Big, but there's not much in here anyway. Also, this means players leaving and joining wont cause issues.
		uint32_t skullCount;
		bool playerScored;
	};

	// Interface for a class which processes and handles voting messages.
	class HeadhunterMessageHandler
	{
	public:
		virtual ~HeadhunterMessageHandler() { }

		// Called after a message has been received.
		virtual void MessageReceived(const HeadhunterMessage &message) = 0;
	};


	bool BroadcastHeadhunterMessage(HeadhunterMessage &message);
	//bool SendHeadhunterMessageToPeer(HeadhunterMessage &message, int peer);
	void InitializePackets();

	//bool SendHeadhunterSkullsToHost(const int vote);

	void AddMessageHandler(std::shared_ptr<HeadhunterMessageHandler> handler);

	void ApplyAll();
	void Init();

	bool GetHeadhunterEnabled();
	void UpdateSkullCountByUid(uint64_t uid, uint32_t skullCount, bool didScore);
	void UpdateSkullCountByHandle(int playerHandle, uint32_t skullCount, bool didScore);

	uint32_t GetSkullCountByUid(uint64_t uid);
	uint32_t GetSkullCountByHandle(int handle);
	bool InHill(uint64_t uid);

	int GetMaxSkullCount();
}
