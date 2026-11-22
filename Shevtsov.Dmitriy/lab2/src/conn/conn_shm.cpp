#include "conn.h"

#include "channel.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <new>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

	using namespace std::chrono;

	const auto POLL_INTERVAL = milliseconds(10);

	struct Segment {
		ChannelPair channels;
		std::atomic<int> ready;
	};

	std::string segmentName(int id){
		return "/wolf_goats_" + std::to_string(id);
	}

	[[noreturn]] void fail(const std::string& name, const char* what){
		int error = errno;
		shm_unlink(name.c_str());
		throw std::system_error(error, std::generic_category(), what);
	}

	Segment* createSegment(const std::string& name){
		shm_unlink(name.c_str());
		int fd = shm_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
		if (fd < 0) {
			fail(name, "shm_open");
		}
		if (ftruncate(fd, sizeof(Segment)) < 0) {
			close(fd);
			fail(name, "ftruncate");
		}

		void* memory = mmap(nullptr, sizeof(Segment), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		close(fd);
		if (memory == MAP_FAILED) {
			fail(name, "mmap");
		}

		auto* segment = new (memory) Segment();
		initChannels(segment->channels);
		segment->ready.store(1);
		return segment;
	}

	Segment* openSegment(const std::string& name){
		auto deadline = steady_clock::now() + seconds(CONN_TIMEOUT_SEC);
		while (true) {
			int fd = shm_open(name.c_str(), O_RDWR, 0);
			if (fd < 0 && errno != ENOENT) {
				throw std::system_error(errno, std::generic_category(), "shm_open");
			}

			if (fd >= 0) {
				struct stat info{};
				bool sized = fstat(fd, &info) == 0 && static_cast<size_t>(info.st_size) >= sizeof(Segment);
				void* memory = MAP_FAILED;
				if (sized) {
					memory = mmap(nullptr, sizeof(Segment), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
				}
				close(fd);

				if (memory != MAP_FAILED) {
					auto* segment = static_cast<Segment*>(memory);
					if (segment->ready.load()) {
						return segment;
					}
					munmap(memory, sizeof(Segment));
				}
			}

			if (steady_clock::now() > deadline) {
				throw std::runtime_error("shared memory " + name + " is not ready after " + std::to_string(CONN_TIMEOUT_SEC) + " s");
			}
			std::this_thread::sleep_for(POLL_INTERVAL);
		}
	}

}

struct Conn::Impl {
	std::string name;
	Segment* segment;
	bool host;
};

void Conn::Prepare(){}

Conn::Conn(int id, bool create) : impl(std::make_unique<Impl>()){
	impl->name = segmentName(id);
	if (create) {
		impl->segment = createSegment(impl->name);
	} else {
		impl->segment = openSegment(impl->name);
	}
	impl->host = create;
}

Conn::~Conn(){
	if (impl->host) {
		destroyChannels(impl->segment->channels);
	}
	munmap(impl->segment, sizeof(Segment));
	if (impl->host) {
		shm_unlink(impl->name.c_str());
	}
}

bool Conn::Read(void* buf, size_t count){
	return channelRead(inbox(impl->segment->channels, impl->host), buf, count);
}

bool Conn::Write(const void* buf, size_t count){
	return channelWrite(outbox(impl->segment->channels, impl->host), buf, count);
}
