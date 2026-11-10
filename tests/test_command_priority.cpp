#include "check.h"
#include "Core/CommandQueue.h"
#include "Core/StateMachine.h"

#include <string>
#include <thread>
#include <vector>

using namespace scf::core;

TEST(TestPriorityArbitrationOrder)
{
	CommandQueue queue;

	// Enqueue in reverse priority order: Tactical first, then Player, then Lifecycle
	queue.Enqueue({
		.summonId = 1,
		.priority = CommandPriority::Tactical,
		.type = CommandType::SetOrder,
		.order = SummonOrder::Follow,
		.timestamp = 1.0f
	});

	queue.Enqueue({
		.summonId = 1,
		.priority = CommandPriority::PlayerDirect,
		.type = CommandType::SetOrder,
		.order = SummonOrder::WaitGuard,
		.timestamp = 2.0f
	});

	queue.Enqueue({
		.summonId = 1,
		.priority = CommandPriority::Lifecycle,
		.type = CommandType::Dispel,
		.timestamp = 3.0f
	});

	CHECK(queue.GetPendingCount() == 3);

	// 1st pop MUST be Lifecycle (highest priority)
	Command c1;
	CHECK(queue.PopNext(4.0f, c1));
	CHECK(c1.priority == CommandPriority::Lifecycle);
	CHECK(c1.type == CommandType::Dispel);

	// 2nd pop MUST be PlayerDirect
	Command c2;
	CHECK(queue.PopNext(4.0f, c2));
	CHECK(c2.priority == CommandPriority::PlayerDirect);
	CHECK(c2.order == SummonOrder::WaitGuard);

	// 3rd pop MUST be Tactical
	Command c3;
	CHECK(queue.PopNext(4.0f, c3));
	CHECK(c3.priority == CommandPriority::Tactical);
	CHECK(c3.order == SummonOrder::Follow);

	CHECK(queue.GetPendingCount() == 0);
}

TEST(TestCommandCoalescing)
{
	CommandQueue queue;

	// Rapid succession of player orders before game tick
	queue.Enqueue({
		.summonId = 5,
		.priority = CommandPriority::PlayerDirect,
		.type = CommandType::SetOrder,
		.order = SummonOrder::Follow
	});

	queue.Enqueue({
		.summonId = 5,
		.priority = CommandPriority::PlayerDirect,
		.type = CommandType::SetOrder,
		.order = SummonOrder::WaitGuard
	});

	// Should coalesce into 1 pending command with the latest order (WaitGuard)
	CHECK(queue.GetPendingCount() == 1);

	Command outCmd;
	CHECK(queue.PopNext(1.0f, outCmd));
	CHECK(outCmd.order == SummonOrder::WaitGuard);
}

TEST(TestTTLExpiration)
{
	CommandQueue queue;

	queue.Enqueue({
		.summonId = 1,
		.priority = CommandPriority::PlayerDirect,
		.type = CommandType::SetOrder,
		.order = SummonOrder::Follow,
		.timestamp = 10.0f,
		.ttl = 2.0f // Valid until t = 12.0s
	});

	Command outCmd;

	// At t = 11.5s: still valid
	CHECK(queue.GetPendingCount() == 1);

	// Check at t = 13.0s: expired!
	const bool popped = queue.PopNext(13.0f, outCmd);
	CHECK(!popped);
	CHECK(queue.GetPendingCount() == 0);
}

TEST(TestDrainForSummonIncludesBroadcast)
{
	CommandQueue queue;

	// Target-specific for 101
	queue.Enqueue({ .summonId = 101, .priority = CommandPriority::PlayerDirect, .type = CommandType::SetOrder, .order = SummonOrder::Return });
	// Broadcast for all summons (InvalidEntityID)
	queue.Enqueue({ .summonId = InvalidEntityID, .priority = CommandPriority::ModeChange, .type = CommandType::SetAggression, .aggression = AggressionMode::Passive });
	// Target-specific for 202
	queue.Enqueue({ .summonId = 202, .priority = CommandPriority::PlayerDirect, .type = CommandType::SetOrder, .order = SummonOrder::Follow });

	CHECK(queue.GetPendingCount() == 3);

	// Drain for 101
	const auto commands = queue.DrainForSummon(101, 1.0f);
	CHECK(commands.size() == 2); // 101 command + broadcast command
	CHECK(commands[0].priority == CommandPriority::PlayerDirect); // PlayerDirect before ModeChange
	CHECK(commands[1].priority == CommandPriority::ModeChange);

	// 202 command should still be in the queue
	CHECK(queue.GetPendingCount() == 1);
	Command c202;
	CHECK(queue.PopNext(1.0f, c202));
	CHECK(c202.summonId == 202);
}

TEST(TestApplyToModelWithFSM)
{
	StateMachine fsm;
	CommandQueue queue;

	SummonModel model{ .id = 1, .state = SummonState::FollowOwner, .order = SummonOrder::Follow };

	// Command 1: Order Return
	Command cmdReturn{ .type = CommandType::SetOrder, .order = SummonOrder::Return };
	queue.ApplyToModel(cmdReturn, model, &fsm);
	CHECK(model.order == SummonOrder::Return);
	CHECK(model.state == SummonState::Return);

	// Command 2: Dispel
	Command cmdDispel{ .type = CommandType::Dispel };
	queue.ApplyToModel(cmdDispel, model, &fsm);
	CHECK(model.state == SummonState::Despawn);
}

TEST(TestThreadSafeConcurrentEnqueue)
{
	CommandQueue queue;
	constexpr int threadCount = 4;
	constexpr int perThreadCount = 50;

	std::vector<std::thread> workers;
	workers.reserve(threadCount);

	for (int t = 0; t < threadCount; ++t) {
		workers.emplace_back([&queue, t]() {
			for (int i = 0; i < perThreadCount; ++i) {
				queue.Enqueue({
					.summonId = static_cast<EntityID>(t * 1000 + i),
					.priority = CommandPriority::Tactical,
					.type = CommandType::SetOrder,
					.order = SummonOrder::Follow
				});
			}
		});
	}

	for (auto& w : workers) {
		w.join();
	}

	CHECK(queue.GetPendingCount() == threadCount * perThreadCount);
}
