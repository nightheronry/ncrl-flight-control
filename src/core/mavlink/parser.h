#ifndef __MAVLINK_PARSER_H__
#define __MAVLINK_PARSER_H__

#include <stdint.h>

#define CMD_LEN(list) (sizeof(list) / sizeof(struct mavlink_parser_item))
#define MAV_CMD_DEF(handler_function, id) \
	[handler_function ## _ID] = {.handler = handler_function, .msg_id = id}
#define ENUM_HANDLER_FUNC(handler_function) handler_function ## _ID

struct mavlink_parser_item {
	uint16_t msg_id;
	void (*handler)(void);
};

void parse_mavlink_received_msg(mavlink_message_t *msg);

#endif
