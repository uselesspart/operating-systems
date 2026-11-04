#include "conn.h"

#include "channel.h"

#include <cerrno>
#include <stdexcept>
#include <system_error>

#include <sys/mman.h>

namespace {

	ChannelPair* region = nullptr;

}

struct Conn::Impl {
	ChannelPair* channels;
	bool host;
};

void Conn::Prepare(){
	void* memory = mmap(nullptr, sizeof(ChannelPair), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (memory == MAP_FAILED) {
		throw std::system_error(errno, std::generic_category(), "mmap");
	}

	region = static_cast<ChannelPair*>(memory);
	initChannels(*region);
}

Conn::Conn(int, bool create) : impl(std::make_unique<Impl>()){
	if (!region) {
		throw std::logic_error("Conn::Prepare() must be called before fork()");
	}
	impl->channels = region;
	impl->host = create;
}

Conn::~Conn(){
	if (impl->host) {
		destroyChannels(*impl->channels);
	}
	munmap(impl->channels, sizeof(ChannelPair));
	region = nullptr;
}

bool Conn::Read(void* buf, size_t count){
	return channelRead(inbox(*impl->channels, impl->host), buf, count);
}

bool Conn::Write(const void* buf, size_t count){
	return channelWrite(outbox(*impl->channels, impl->host), buf, count);
}
