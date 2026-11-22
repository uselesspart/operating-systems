#pragma once

#include <cstddef>

#include <semaphore.h>

constexpr size_t CHANNEL_CAPACITY = 256;

struct Channel {
	sem_t empty;
	sem_t full;
	size_t size;
	char data[CHANNEL_CAPACITY];
};

struct ChannelPair {
	Channel toClient;
	Channel toHost;
};

void initChannels(ChannelPair& pair);
void destroyChannels(ChannelPair& pair);

bool channelWrite(Channel& channel, const void* buf, size_t count);
bool channelRead(Channel& channel, void* buf, size_t count);

inline Channel& inbox(ChannelPair& pair, bool host){
	Channel* channel = &pair.toClient;
	if (host) {
		channel = &pair.toHost;
	}
	return *channel;
}

inline Channel& outbox(ChannelPair& pair, bool host){
	Channel* channel = &pair.toHost;
	if (host) {
		channel = &pair.toClient;
	}
	return *channel;
}
