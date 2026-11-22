#pragma once

enum class MessageType : int {
	Hello,
	Round,
	Move,
	Status,
	Stop,
};

struct Message {
	MessageType type;
	int value;
};
