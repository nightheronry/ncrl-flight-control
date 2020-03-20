#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "mpu6500.h"
#include "uart.h"
#include "sbus_receiver.h"
#include "quadshell.h"
#include "autopilot.h"
#include "perf.h"

static bool parse_float_from_str(char *str, float *value)
{
	char *end_ptr = NULL;
	errno = 0;
	*value = strtof(str, &end_ptr);
	if (errno != 0 || *end_ptr != '\0') {
		return false;
	} else {
		return true;
	}
}

void shell_cmd_help(char param_list[PARAM_LIST_SIZE_MAX][PARAM_LEN_MAX], int param_cnt)
{
	char *s = "supported commands:\n\r"
	          "clear\n\r"
	          "disarm\n\r"
	          "takeoff\n\r"
	          "land\n\r"
	          "fly x y z\n\r"
	          "mission\n\r"
	          "radio\n\r"
	          "radio_raw\n\r"
	          "acc_calib\n\r"
	          "perf\n\r";
	shell_puts(s);
}

void shell_cmd_clear(char param_list[PARAM_LIST_SIZE_MAX][PARAM_LEN_MAX], int param_cnt)
{
	shell_cls();
}

void shell_cmd_disarm(char param_list[PARAM_LIST_SIZE_MAX][PARAM_LEN_MAX], int param_cnt)
{
	char user_agree[CMD_LEN_MAX];
	struct shell_struct shell;
	shell_init_struct(&shell, "confirm disarm command [y/n]: ", user_agree);
	shell_cli(&shell);

	if(strcmp(user_agree, "y") == 0 || strcmp(user_agree, "Y") == 0) {
	} else {
		shell_puts("abort.\n\r");
	}
}

void shell_cmd_arm(char param_list[PARAM_LIST_SIZE_MAX][PARAM_LEN_MAX], int param_cnt)
{
	char user_agree[CMD_LEN_MAX];
	struct shell_struct shell;
	shell_init_struct(&shell, "confirm arm command [y/n]: ", user_agree);
	shell_cli(&shell);

	if(strcmp(user_agree, "y") == 0 || strcmp(user_agree, "Y") == 0) {
	} else {
		shell_puts("abort.\n\r");
	}
}

void shell_cmd_takeoff(char param_list[PARAM_LIST_SIZE_MAX][PARAM_LEN_MAX], int param_cnt)
{
	char user_agree[CMD_LEN_MAX];
	struct shell_struct shell;
	shell_init_struct(&shell, "confirm takeoff command [y/n]: ", user_agree);
	shell_cli(&shell);

	if(strcmp(user_agree, "y") == 0 || strcmp(user_agree, "Y") == 0) {
		int ret_val = autopilot_trigger_auto_takeoff();
		if(ret_val == AUTOPILOT_SET_SUCCEED) {
			shell_puts("command accept.\n\r");
		} else {
			shell_puts("failed, uav had already takeoff\n\r");
		}
	} else {
		shell_puts("abort.\n\r");
	}
}

void shell_cmd_land(char param_list[PARAM_LIST_SIZE_MAX][PARAM_LEN_MAX], int param_cnt)
{
	char user_agree[CMD_LEN_MAX];
	struct shell_struct shell;
	shell_init_struct(&shell, "confirm landing command [y/n]: ", user_agree);
	shell_cli(&shell);

	if(strcmp(user_agree, "y") == 0 || strcmp(user_agree, "Y") == 0) {
		int ret_val = autopilot_trigger_auto_landing();
		if(ret_val == AUTOPILOT_SET_SUCCEED) {
			shell_puts("command accept.\n\r");
		} else {
			shell_puts("failed, uav can only be landed while hovering at a point!\n\r");
		}
	} else {
		shell_puts("abort.\n\r");
	}
}

void shell_cmd_fly(char param_list[PARAM_LIST_SIZE_MAX][PARAM_LEN_MAX], int param_cnt)
{
	char s[300] = {'\0'};

	if(param_cnt != 4 && param_cnt != 3) {
		shell_puts("abort, bad arguments!\n\r"
		           "fly_enu x y z\n\r");
		return;
	}

	float pos[3] = {0.0f, 0.0f, 1.5f};

	if (parse_float_from_str(param_list[1], &pos[0]) == false) {
		shell_puts("abort, bad arguments!\n\r"
		           "fly_enu x y z\n\r");
		return;
	}

	if (parse_float_from_str(param_list[2], &pos[1]) == false) {
		shell_puts("abort, bad arguments!\n\r"
		           "fly_enu x y z\n\r");
		return;
	}

	/* check if user set the height */
	bool change_z = false;
	if(param_cnt == 4) {
		change_z = true;
		if (parse_float_from_str(param_list[3], &pos[2]) == false) {
			shell_puts("abort, bad arguments!\n\r"
			           "fly_enu x y z\n\r");
			return;
		}

		sprintf(s, "east-north-up waypoint (x, y, z) = (%f, %f, %f)\n\r", pos[0], pos[1], pos[2]);
	} else if(param_cnt == 3) {
		sprintf(s, "east-north-up waypoint (x, y) = (%f, %f)\n\r", pos[0], pos[1]);
	}
	shell_puts(s);

	char user_agree[CMD_LEN_MAX];
	struct shell_struct shell;
	shell_init_struct(&shell, "confirm fly command [y/n]: ", user_agree);
	shell_cli(&shell);

	if(strcmp(user_agree, "y") == 0 || strcmp(user_agree, "Y") == 0) {
		/* convert from [m] to [cm] */

		pos[0] *= 100.0f;
		pos[1] *= 100.0f;
		pos[2] *= 100.0f;

		int ret_val = autopilot_goto_waypoint_now(pos, change_z);
		if(ret_val == AUTOPILOT_WP_OUT_OF_FENCE) {
			shell_puts("failed, waypoint out of geo-fence!\n\r");
		} else {
			shell_puts("command accept.\n\r");
		}
	} else {
		shell_puts("abort.\n\r");
	}
}

static void mission_add_cmd_handler(char param_list[PARAM_LIST_SIZE_MAX][PARAM_LEN_MAX])
{
	float pos[3] = {0.0f, 0.0f, 0.0f};
	float heading = 0.0f;
	float stay_time_sec = 1.0f;
	float radius = 0.0f;

	if(parse_float_from_str(param_list[2], &pos[0]) == false) {
		shell_puts("abort, bad arguments\n\r");
		return;
	}
	if(parse_float_from_str(param_list[3], &pos[1]) == false) {
		shell_puts("abort, bad arguments\n\r");
		return;
	}
	if(parse_float_from_str(param_list[4], &pos[2]) == false) {
		shell_puts("abort, bad arguments\n\r");
		return;

	}
	if(parse_float_from_str(param_list[5], &heading) == false) {
		shell_puts("abort, bad arguments\n\r");
		return;
	}
	if(parse_float_from_str(param_list[6], &stay_time_sec) == false) {
		shell_puts("abort, bad arguments\n\r");
		return;
	}
	if(parse_float_from_str(param_list[7], &radius) == false) {
		shell_puts("abort, bad arguments\n\r");
		return;
	}

	//XXX
	pos[0] *= 100.0f;
	pos[1] *= 100.0f;
	pos[2] *= 100.0f;
	radius *= 100.0f;

	char s[200] = {'\0'};
	sprintf(s, "new waypoint: x=%.1f, y=%.1f, z=%.1f, heading=%.1f, stay_time=%.1f, radius=%.1f\n\r",
	        pos[0], pos[1], pos[2], heading, stay_time_sec, radius);
	shell_puts(s);

	char user_agree[CMD_LEN_MAX];
	struct shell_struct shell;
	shell_init_struct(&shell, "confirm mission add command [y/n]: ", user_agree);
	shell_cli(&shell);

	if(strcmp(user_agree, "y") == 0 || strcmp(user_agree, "Y") == 0) {
		int ret_val = autopilot_add_new_waypoint(pos, heading, stay_time_sec, radius);
		if(ret_val == AUTOPILOT_SET_SUCCEED) {
			shell_puts("successfully  added new waypoint.\n\r");
		} else if(ret_val == AUTOPILOT_WP_LIST_FULL) {
			shell_puts("failed, mission is executing!\n\r");
		}
	} else {
		shell_puts("abort.\n\r");
	}
}

static void mission_start_cmd_handler(char param_list[PARAM_LIST_SIZE_MAX][PARAM_LEN_MAX])
{
	char user_agree[CMD_LEN_MAX];
	struct shell_struct shell;
	shell_init_struct(&shell, "confirm mission start command [y/n]: ", user_agree);
	shell_cli(&shell);

	if(strcmp(user_agree, "y") == 0 || strcmp(user_agree, "Y") == 0) {
		int ret_val = autopilot_waypoint_mission_start(false);
		if(ret_val == AUTOPILOT_SET_SUCCEED) {
			shell_puts("successfully started the mission.\n\r");
		} else if(ret_val == AUTOPILOT_WP_LIST_EMPYT) {
			shell_puts("failed, waypoint list is full\n\r");
		} else if(ret_val == AUTOPILOT_MISSION_EXECUTING) {
			shell_puts("failed, mission is executing!\n\r");
		}
	} else {
		shell_puts("abort.\n\r");
	}
}

static void mission_loop_cmd_handler(char param_list[PARAM_LIST_SIZE_MAX][PARAM_LEN_MAX])
{
	char user_agree[CMD_LEN_MAX];
	struct shell_struct shell;
	shell_init_struct(&shell, "confirm mission start command [y/n]: ", user_agree);
	shell_cli(&shell);

	if(strcmp(user_agree, "y") == 0 || strcmp(user_agree, "Y") == 0) {
		int ret_val = autopilot_waypoint_mission_start(true);
		if(ret_val == AUTOPILOT_SET_SUCCEED) {
			shell_puts("successfully started the mission.\n\r");
		} else if(ret_val == AUTOPILOT_WP_LIST_EMPYT) {
			shell_puts("failed, waypoint list is full\n\r");
		} else if(ret_val == AUTOPILOT_MISSION_EXECUTING) {
			shell_puts("failed, mission is executing!\n\r");
		}
	} else {
		shell_puts("abort.\n\r");
	}
}

static void mission_list_cmd_handler(char param_list[PARAM_LIST_SIZE_MAX][PARAM_LEN_MAX])
{
	debug_print_waypoint_list();
}

static void mission_halt_cmd_handler(char param_list[PARAM_LIST_SIZE_MAX][PARAM_LEN_MAX])
{
	char user_agree[CMD_LEN_MAX];
	struct shell_struct shell;
	shell_init_struct(&shell, "confirm mission halt command [y/n]: ", user_agree);
	shell_cli(&shell);

	if(strcmp(user_agree, "y") == 0 || strcmp(user_agree, "Y") == 0) {
		int ret_val = autopilot_halt_waypoint_mission();
		if(ret_val == AUTOPILOT_SET_SUCCEED) {
			shell_puts("successfully halted the waypoint mission.\n\r");
		} else if(ret_val == AUTOPILOT_NO_EXECUTING_MISSION) {
			shell_puts("failed, no executing mission!\n\r");
		}
	} else {
		shell_puts("abort.\n\r");
	}
}

static void mission_resume_cmd_handler(char param_list[PARAM_LIST_SIZE_MAX][PARAM_LEN_MAX])
{
	char user_agree[CMD_LEN_MAX];
	struct shell_struct shell;
	shell_init_struct(&shell, "confirm mission resume command [y/n]: ", user_agree);
	shell_cli(&shell);

	if(strcmp(user_agree, "y") == 0 || strcmp(user_agree, "Y") == 0) {
		int ret_val = autopilot_resume_waypoint_mission();
		if(ret_val == AUTOPILOT_SET_SUCCEED) {
			shell_puts("successfully resumed the waypoint mission.\n\r");
		} else if(ret_val == AUTOPILOT_NO_EXECUTING_MISSION) {
			shell_puts("failed, no halting mission!\n\r");
		}
	} else {
		shell_puts("abort.\n\r");
	}
}

static void mission_clear_cmd_handler(char param_list[PARAM_LIST_SIZE_MAX][PARAM_LEN_MAX])
{
	char user_agree[CMD_LEN_MAX];
	struct shell_struct shell;
	shell_init_struct(&shell, "confirm mission clear command [y/n]: ", user_agree);
	shell_cli(&shell);

	if(strcmp(user_agree, "y") == 0 || strcmp(user_agree, "Y") == 0) {
		int ret_val = autopilot_clear_waypoint_list();
		if(ret_val == AUTOPILOT_SET_SUCCEED) {
			shell_puts("successfully cleared the waypoint list.\n\r");
		} else if(ret_val == AUTOPILOT_MISSION_EXECUTING) {
			shell_puts("failed, waypoint list is empty!\n\r");
		}
	} else {
		shell_puts("abort.\n\r");
	}
}

static void mission_status_cmd_handler(char param_list[PARAM_LIST_SIZE_MAX][PARAM_LEN_MAX])
{
	debug_print_waypoint_status();
}

void shell_cmd_mission(char param_list[PARAM_LIST_SIZE_MAX][PARAM_LEN_MAX], int param_cnt)
{
	if(param_cnt == 8) {
		if(strcmp(param_list[1], "add") == 0) {
			mission_add_cmd_handler(param_list);
		}
	} else if(param_cnt == 2) {
		if(strcmp(param_list[1], "start") == 0) {
			mission_start_cmd_handler(param_list);
		} else if(strcmp(param_list[1], "loop") == 0) {
			mission_loop_cmd_handler(param_list);
		} else if(strcmp(param_list[1], "list") == 0) {
			mission_list_cmd_handler(param_list);
		} else if(strcmp(param_list[1], "halt") == 0) {
			mission_halt_cmd_handler(param_list);
		} else if(strcmp(param_list[1], "resume") == 0) {
			mission_resume_cmd_handler(param_list);
		} else if(strcmp(param_list[1], "clear") == 0) {
			mission_clear_cmd_handler(param_list);
		} else if(strcmp(param_list[1], "status") == 0) {
			mission_status_cmd_handler(param_list);
		} else {
			shell_puts("unknown mission command!\n\r");
		}
	} else {
		shell_puts("mission add x y z heading stay_time_sec radius: add new waypoint\n\r"
		           "mission start: start waypoint mission\n\r"
		           "mission loop: loop waypoint mission\n\r"
		           "mission list: list current waypoint list\n\r"
		           "mission halt: halt current executing waypoint mission\n\r"
		           "mission resume: resume current halting waypoint mission\n\r"
		           "mission clear: clear waypoint list\n\r"
		           "mission status: show mission status\n\r");
	}
}

void shell_cmd_radio(char param_list[PARAM_LIST_SIZE_MAX][PARAM_LEN_MAX], int param_cnt)
{
	shell_puts("press [q] to stop.\n\r");
	char c = '\0';
	while(1) {
		if(uart3_getc(&c, 0) == true) {
			if(c == 'q') break;
		}
		debug_print_rc_info();
	}
}

void shell_cmd_radio_raw(char param_list[PARAM_LIST_SIZE_MAX][PARAM_LEN_MAX], int param_cnt)
{
	shell_puts("press [q] to stop.\n\r");
	char c = '\0';
	while(1) {
		if(uart3_getc(&c, 0) == true) {
			if(c == 'q') break;
		}
		debug_print_rc_val();
	}
}

void shell_cmd_acc_calib(char param_list[PARAM_LIST_SIZE_MAX][PARAM_LEN_MAX], int param_cnt)
{
	shell_puts("press [q] to stop.\n\r");
	char c = '\0';
	while(1) {
		if(uart3_getc(&c, 0) == true) {
			if(c == 'q') break;
		}
		debug_print_mpu6500_accel();
	}
}

void shell_cmd_perf(char param_list[PARAM_LIST_SIZE_MAX][PARAM_LEN_MAX], int param_cnt)
{
	float exec_time_ms;
	float exec_freq_hz;

	char s[100];
	shell_puts("perf_list:\n\r");
	int i;
	for(i = 0; i < perf_get_list_size(); i++) {
		exec_time_ms = perf_get_time_ms(i);
		exec_freq_hz = 1.0f / (exec_time_ms/ 1000.0f);
		sprintf(s, "%s: %fms (%fHz)\n\r", perf_get_name(i), exec_time_ms, exec_freq_hz);
		shell_puts(s);
	}
}
