#include <stdbool.h>

#include "oslib/wimp.h"
#include "archiludo.h"

wimp_t task_handle;

static void create_iconbar_icon(void)
{
	wimp_icon_create icon;

	icon.w = wimp_ICON_BAR_RIGHT;
	icon.icon.extent.x0 = 0;
	icon.icon.extent.y0 = 0;
	icon.icon.extent.x1 = 68;
	icon.icon.extent.y1 = 68;
	icon.icon.flags = wimp_ICON_TEXT | wimp_ICON_HCENTRED | wimp_ICON_VCENTRED
	                | (wimp_BUTTON_CLICK << wimp_ICON_BUTTON_TYPE_SHIFT);
	icon.icon.data.text[0] = 'A';
	icon.icon.data.text[1] = 'L';
	icon.icon.data.text[2] = '\0';

	wimp_create_icon(&icon);
}

void archiludo_initialise(void)
{
	wimp_version_no version_out;

	task_handle = wimp_initialise(wimp_VERSION_RO30, APP_NAME, NULL, &version_out);
	create_iconbar_icon();
}

void archiludo_poll_loop(void)
{
	bool quit = false;
	wimp_block block;

	while (!quit) {
		wimp_event_no reason = wimp_poll(0, &block, NULL);

		switch (reason) {
		case wimp_MOUSE_CLICK:
			if (block.pointer.w == wimp_ICON_BAR)
				quit = true;
			break;

		case wimp_USER_MESSAGE:
		case wimp_USER_MESSAGE_RECORDED:
			if (block.message.action == message_QUIT)
				quit = true;
			break;

		default:
			break;
		}
	}
}

int main(void)
{
	archiludo_initialise();
	archiludo_poll_loop();

	wimp_close_down(task_handle);

	return 0;
}
