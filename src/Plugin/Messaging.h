#pragma once

#include "SKSE/SKSE.h"

namespace scf::plugin
{
	void RegisterMessaging();
	void OnMessage(SKSE::MessagingInterface::Message* a_message);
}
