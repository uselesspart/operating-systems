#include "channel.h"

#include "conn.h"

#include <cerrno>
#include <cstring>
#include <ctime>
#include <initializer_list>
#include <system_error>

#include <syslog.h>

namespace {

	void initChannel(Channel& channel){
		if (sem_init(&channel.empty, 1, 1) < 0 || sem_init(&channel.full, 1, 0) < 0) {
			throw std::system_error(errno, std::generic_category(), "sem_init");
		}
		channel.size = 0;
	}

	bool waitSemaphore(sem_t* sem){
		timespec deadline{};
		clock_gettime(CLOCK_REALTIME, &deadline);
		deadline.tv_sec += CONN_TIMEOUT_SEC;

		int result;
		while ((result = sem_timedwait(sem, &deadline)) < 0 && errno == EINTR) {}

		if (result < 0 && errno == ETIMEDOUT) {
			syslog(LOG_ERR, "semaphore wait timed out after %d s", CONN_TIMEOUT_SEC);
		} else if (result < 0) {
			syslog(LOG_ERR, "sem_timedwait: %m");
		}
		return result == 0;
	}

}

void initChannels(ChannelPair& pair){
	initChannel(pair.toClient);
	initChannel(pair.toHost);
}

void destroyChannels(ChannelPair& pair){
	for (Channel* channel : {&pair.toClient, &pair.toHost}) {
		sem_destroy(&channel->empty);
		sem_destroy(&channel->full);
	}
}

bool channelWrite(Channel& channel, const void* buf, size_t count){
	bool written = false;
	if (count > CHANNEL_CAPACITY) {
		syslog(LOG_ERR, "message of %zu bytes does not fit into the channel", count);
	} else if (waitSemaphore(&channel.empty)) {
		memcpy(channel.data, buf, count);
		channel.size = count;
		sem_post(&channel.full);
		written = true;
	}
	return written;
}

bool channelRead(Channel& channel, void* buf, size_t count){
	bool received = false;
	if (waitSemaphore(&channel.full)) {
		if (channel.size == count) {
			memcpy(buf, channel.data, count);
			received = true;
		} else {
			syslog(LOG_ERR, "expected a message of %zu bytes, got %zu", count, channel.size);
		}
		sem_post(&channel.empty);
	}
	return received;
}
