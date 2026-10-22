#include "platform/discord.hpp"

// The PSP has no Discord client to talk to.
void DiscordStart(std::string) {}
void DiscordSetActivity(const DiscordActivity &) {}
void DiscordStop() {}
